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

enum class PC_PackageType : PackageTypeInt {
    NONE = 0,
    MESSAGE = 1,
    PAIR_REQUEST = 2,
    PAIR_REQUEST_DENIED = 3,
    PAIR_REQUEST_ACCEPTED = 4
};

constexpr size_t MAX_PACKAGE_SIZE = 8192;

class PrimaryConnection final : public std::enable_shared_from_this<PrimaryConnection> {
public:
    explicit PrimaryConnection(IOContext& context);

    PrimaryConnection() = delete;
    static std::shared_ptr<PrimaryConnection> Create(IOContext& context);

    void Connect(TCPEndpoint&& endpoint, const std::shared_ptr<SSLContext>& sslContext, ConnectionCallbackType&& callback = nullptr);
    void Seek(TCPEndpoint&& endpoint, const std::shared_ptr<SSLContext>& sslContext, ConnectionCallbackType&& callback = nullptr);
    void Disconnect(DisconnectionCallbackType&& callback = nullptr);

    template <Serializable... Args>
    void Send(PC_PackageType type, Args&&... args) {
        static thread_local moodycamel::ProducerToken token(m_packageOut);

        m_packageOut.enqueue(token, Package<PC_PackageType>::CreateUnique(type, std::forward<Args>(args)...));
        m_sendFlag.Signal();
    }

    template <Serializable... Args>
    void SendWithFlag(const PC_PackageType type, const uint8_t flag, Args&&... args) {
        static thread_local moodycamel::ProducerToken token(m_packageOut);

        std::unique_ptr<Package<PC_PackageType>> package = Package<PC_PackageType>::CreateUnique(type, std::forward<Args>(args)...);
        package->GetHeader().flags = flag;

        m_packageOut.enqueue(token, std::move(package));
        m_sendFlag.Signal();
    }

    std::optional<std::unique_ptr<Package<PC_PackageType>>> GetPackage();
    std::shared_ptr<AwaitableFlag> GetReceiveFlag() const;
    bool HasPendingPackages() const;


private:
    asio::awaitable<void> CoConnect(TCPEndpoint endpoint, ConnectionCallbackType callback);
    asio::awaitable<void> CoSeek(TCPEndpoint endpoint, ConnectionCallbackType callback);
    asio::awaitable<void> CoDisconnect(DisconnectionCallbackType callback);
    asio::awaitable<void> CoSend();
    asio::awaitable<void> CoReceive();

    IOContext&      m_context;
    asio::strand<asio::io_context::executor_type> m_strand;

    std::shared_ptr<SSLContext> m_sslContext;
    std::unique_ptr<SSLSocket>  m_socket;

    AwaitableFlag  m_sendFlag;
    std::shared_ptr<AwaitableFlag> m_receiveFlag;

    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageOut;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageIn;

    ConnectionState    m_connectionState{ConnectionState::DISCONNECTED};

};

#endif //PRIMARY_CONNECTION_H
