#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <atomic>
#include <filesystem>
#include <Packable.h>

#include <QObject>
#include <PrimaryConnection.h>
#include <ConcurrentUnorderedMap.h>
#include <magic_enum/magic_enum.hpp>

class ConnectionManager final {
public:
    typedef std::function<void(std::unique_ptr<Package<PC_PackageType>>&&)> RequestCallbackType;
    typedef std::function<void(bool)> CallbackWithResult;

    static void Connect(TCPEndpoint&& endpoint);
    static void Seek(TCPEndpoint&& endpoint);

    static void Disconnect(std::error_code errorCode = std::error_code{});
    static void AddResponseHandler(PC_PackageType type, RequestCallbackType&& handler);
    static void PairDevice(CallbackWithResult&& callback);
    static TCPEndpoint GetSeekEndpoint();

    template <Serializable... Args>
    static void Send(PC_PackageType type, Args&&... args) {
        if (!s_isInitialized.load()) {
            std::lock_guard<std::mutex> lock(s_mutex);
            Initialize();
        }

        s_instance->m_primaryConnection->Send(type, std::forward<Args>(args)...);
    }

    template <Serializable... Args>
    static void SendRequest(PC_PackageType type, RequestCallbackType&& requestResponseCallback, Args&&... args) {
        if (!s_isInitialized.load()) {
            std::lock_guard<std::mutex> lock(s_mutex);
            Initialize();
        }

        constexpr uint8_t packageFlag = static_cast<uint8_t>(PackageFlag::REQUEST_WITH_RESPONSE);
        const size_t requestID = s_instance->m_currentRequestID.fetch_add(1);
        s_instance->m_primaryConnection->SendWithFlag(type, packageFlag, static_cast<size_t>(requestID), std::forward<Args>(args)...);
        s_instance->m_requestCallbackMap.InsertOrAssign(requestID, std::forward<RequestCallbackType>(requestResponseCallback));
    }

private:
    ConnectionManager();

    friend class PrimaryConnection;

    static void Initialize();
    static void SendEvent(QEvent* event);
    static std::shared_ptr<SSLContext> CreateSSLContext(bool isServer);
    static void RunContext();

    asio::awaitable<void> CoProcessPackages();

    enum class SSLContextCurrentMode : uint8_t {
        NONE   = 0,
        SERVER = 1,
        CLIENT = 2
    };

    static ConnectionManager* s_instance;
    static std::mutex         s_mutex;
    static std::atomic<bool>  s_isInitialized;


    IOContext  m_context;
    std::shared_ptr<SSLContext> m_sslContext;
    IOWorkGuard m_workGuard;
    TCPEndpoint m_seekingEndpoint;

    std::atomic<size_t> m_currentRequestID{0};
    std::atomic<SSLContextCurrentMode> m_currentSSLContextCurrentMode{SSLContextCurrentMode::NONE};

    std::vector<std::thread> m_threads;
    std::shared_ptr<PrimaryConnection> m_primaryConnection;
    ConcurrentUnorderedMap<size_t, RequestCallbackType> m_requestCallbackMap;
    ConcurrentUnorderedMap<PC_PackageType, RequestCallbackType> m_responseHandlerMap;

    std::vector<QObject*> m_eventObjects;

};

#endif //CONNECTION_MANAGER_H
