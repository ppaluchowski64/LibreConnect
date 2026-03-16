#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <atomic>
#include <filesystem>
#include <Packable.h>
#include <optional>

#include <QObject>
#include <PrimaryConnection.h>
#include <InitialConnection.h>
#include <AwaitableFlag.h>
#include <ConcurrentUnorderedMap.h>
#include <boost/uuid/nil_generator.hpp>

enum class InitialConnectionMode : uint8_t;
typedef std::unique_ptr<Package<PC_PackageType>> PC_Package;
typedef std::function<void(PC_Package&&)> RequestCallbackType;
typedef std::function<asio::awaitable<void>(PC_Package&&)> RequestAwaitableCallbackType;
typedef std::function<void(bool)> CallbackWithResult;

class ConnectionManager final {
public:
    static void StartAcceptingConnections();
    static void StopAcceptingConnections();
    static void Connect(const std::string& address, uint16_t port, InitialConnectionMode mode);
    static void SendEvent(const std::unique_ptr<QEvent>& event);

    static std::vector<DeviceInfoLite> GetPairedDevices();

    static void Disconnect(std::error_code errorCode = std::error_code{});
    static void AddResponseHandler(PC_PackageType type, RequestCallbackType&& handler);
    static void AddAwaitableResponseHandler(PC_PackageType type, RequestAwaitableCallbackType&& handler);
    static void RemoveResponseHandler(PC_PackageType type);
    static void AddEventListener(const QPointer<QObject>& object);
    static TCPEndpoint GetSeekEndpoint();
    static IPAddress GetPeerAddress();

    template <Serializable... Args>
    static void Send(PC_PackageType type, Args&&... args) {
        std::call_once(s_flag, Initialize);

        s_instance->m_primaryConnection->Send(type, std::forward<Args>(args)...);
    }

    template <Serializable... Args>
    static void SendRequestResponse(const size_t requestID, PC_PackageType type, Args&&... args) {
        std::call_once(s_flag, Initialize);

        constexpr uint8_t packageFlag = static_cast<uint8_t>(PackageFlag::REQUEST_AWAITABLE_RESPONSE);
        s_instance->m_primaryConnection->SendWithFlag(type, packageFlag, static_cast<size_t>(requestID), std::forward<Args>(args)...);
    }

    template <Serializable... Args>
    // ReSharper disable once CppParameterMayBeConst
    static asio::awaitable<std::optional<std::unique_ptr<Package<PC_PackageType>>>> SendRequest(PC_PackageType type, Args&&... args) {
        std::call_once(s_flag, Initialize);

        constexpr uint8_t packageFlag = static_cast<uint8_t>(PackageFlag::REQUEST_AWAITABLE);
        const size_t requestID = s_instance->m_currentRequestID.fetch_add(1);
        s_instance->m_primaryConnection->SendWithFlag(type, packageFlag, static_cast<size_t>(requestID), std::forward<Args>(args)...);

        const std::shared_ptr<AwaitableFlag> flag = std::make_shared<AwaitableFlag>(
            s_instance->m_context.get_executor()
        );

        s_instance->m_requestAwaitableMap.InsertOrAssign(requestID, flag);
        co_await flag->Wait();
        s_instance->m_requestAwaitableMap.Erase(requestID);

        co_return s_instance->m_requestPackageMap.Pop(requestID);
    }

    static std::shared_ptr<SSLContext> GetSSLContextClient();
    static std::shared_ptr<SSLContext> GetSSLContextServer();


private:
    ConnectionManager();

    friend class PrimaryConnection;
    friend class LanDeviceScanner;
    friend class InitialConnection;

    static void ConnectPrimary(const InitialConnectionData& data);
    static void SeekPrimary(const InitialConnectionData& data, std::function<void(TCPEndpoint)>&& callback = nullptr);

    static void SeekInitialConnection(TCPEndpoint endpoint);

    static void Initialize();
    static std::shared_ptr<SSLContext> CreateSSLContext(bool isServer, uuid targetUUID = boost::uuids::nil_uuid());
    static bool VerifyCallbackAlwaysAccept(bool preverified, asio::ssl::verify_context& ctx);
    static void RunContext();
    static void SetSeekingEndpoint(TCPEndpoint endpoint);

    asio::awaitable<void> CoProcessPackages();

    static ConnectionManager* s_instance;
    static std::mutex         s_mutex;
    static std::once_flag     s_flag;

    IOContext& m_context;
    std::shared_ptr<SSLContext> m_sslContextClient;
    std::shared_ptr<SSLContext> m_sslContextServer;

    std::atomic<size_t> m_currentRequestID{0};

    std::shared_ptr<PrimaryConnection> m_primaryConnection;
    ConcurrentUnorderedMap<size_t, std::shared_ptr<AwaitableFlag>> m_requestAwaitableMap;
    ConcurrentUnorderedMap<size_t, std::unique_ptr<Package<PC_PackageType>>> m_requestPackageMap;

    ConcurrentUnorderedMap<PC_PackageType, RequestCallbackType> m_responseHandlerMap;
    ConcurrentUnorderedMap<PC_PackageType, RequestAwaitableCallbackType> m_responseAwaitableHandlerMap;

    std::shared_ptr<InitialConnection> m_initialConnectionOut;
    std::vector<std::weak_ptr<InitialConnection>> m_initialConnectionsIn;

    std::vector<QPointer<QObject>> m_eventObjects;
    TCPEndpoint m_seekingEndpoint;
};

#endif //CONNECTION_MANAGER_H
