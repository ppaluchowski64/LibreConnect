#ifndef PRIMARY_CONNECTION_H
#define PRIMARY_CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <Package.h>
#include <Packable.h>
#include <AsioCommon.h>
#include <concurrentqueue.h>
#include <asio/awaitable.hpp>
#include <AwaitableFlag.h>
#include <optional>
#include <functional>
#include <InitialConnection.h>

#if defined(DESKTOP_DEVICE)
#include <DaemonClient.h>
#endif

constexpr size_t MAX_PACKAGE_SIZE = 1024 * 256;
static constexpr size_t MAX_INBOUND_QUEUED_BYTES = 1024 * 1024 * 16;

class PrimaryConnection final : public std::enable_shared_from_this<PrimaryConnection> {
public:
    explicit PrimaryConnection();

    static std::shared_ptr<PrimaryConnection> Create();

    void Connect(const std::shared_ptr<SSLContext_>& sslContext, const InitialConnectionData& data);
    void Seek(const std::shared_ptr<SSLContext_>& sslContext, const InitialConnectionData& data, std::function<void(TCPEndpoint)>&& callback);

    void Disconnect(std::error_code errorCode, bool callConnectionManagerDisconnect = true);

    template <Serializable... Args>
    void Send(PC_PackageType type, Args&&... args) {
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            Debug::LogWarning(
                "PrimaryConnection: Dropping outbound package type {} because primary is not connected",
                static_cast<int>(type)
            );
            return;
        }

        static thread_local moodycamel::ProducerToken producerToken(m_packageOut);

        m_packageOut.enqueue(producerToken, Package<PC_PackageType>::CreateUnique(type, std::forward<Args>(args)...));
        m_sendFlag.Signal();
    }

    template <Serializable... Args>
    void SendWithFlag(const PC_PackageType type, const uint8_t flag, Args&&... args) {
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            Debug::LogWarning(
                "PrimaryConnection: Dropping outbound package type {} because primary is not connected",
                static_cast<int>(type)
            );
            return;
        }

        static thread_local moodycamel::ProducerToken producerToken(m_packageOut);
        std::unique_ptr<Package<PC_PackageType>> package = Package<PC_PackageType>::CreateUnique(type, std::forward<Args>(args)...);
        package->GetHeader().flags = flag;

        m_packageOut.enqueue(producerToken, std::move(package));
        m_sendFlag.Signal();
    }

    std::optional<std::unique_ptr<Package<PC_PackageType>>> GetPackage();
    std::shared_ptr<AwaitableFlag> GetReceiveFlag() const;
    IPAddress GetPeerAddress() const;
    uuid GetPeerUUID();
    bool HasPendingPackages() const;
    void MarkHeartbeatReceived();
    ConnectionState GetConnectionState() const;
    std::string GetPeerDeviceName();


private:
    asio::awaitable<void> CoConnect(std::shared_ptr<SSLContext_> sslContext, InitialConnectionData data);
    asio::awaitable<void> CoSeek(std::shared_ptr<SSLContext_> sslContext, InitialConnectionData data, std::function<void(TCPEndpoint)> callback);

    asio::awaitable<void> CoCleanupConnection();
    asio::awaitable<void> CoDisconnect(std::error_code errorCode, bool callConnectionManagerDisconnect = true);
    asio::awaitable<void> CoSend();
    asio::awaitable<void> CoReceive();
    asio::awaitable<void> CoSendHeartbeat();
    asio::awaitable<void> CoHeartbeatMonitor();
    void ClearQueuedPackages();

    static void SavePairData(const InitialConnectionData& data);
    void SaveCertificate(const InitialConnectionData& data) const;

    IOContext& m_context;
    IOContextStrand m_strand;

    std::shared_ptr<SSLContext_> m_sslContext;
    std::unique_ptr<SSLSocket> m_socket;

    std::mutex m_peerDataMutex;
    std::optional<DeviceInfo> m_peerData;

    AwaitableFlag m_sendFlag;
    std::shared_ptr<AwaitableFlag> m_receiveFlag;

    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageOut;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageIn;

    std::atomic<uint64_t> m_inboundQueuedBytes{0};
    std::atomic<bool> m_heartbeatReceived{false};
    std::atomic<bool> m_disconnectedEventSent{false};
    std::atomic<ConnectionState> m_connectionState{ConnectionState::DISCONNECTED};

};

#endif //PRIMARY_CONNECTION_H
