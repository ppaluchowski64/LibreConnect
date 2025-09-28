#ifndef PRIMARY_CONNECTION_H
#define PRIMARY_CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <Package.h>
#include <AsioCommon.h>
#include <concurrentqueue.h>
#include <asio/awaitable.hpp>
#include <AwaitableFlag.h>
#include <optional>
#include <functional>

enum class PC_PackageType : PackageTypeInt {
    NONE = 0,
};

constexpr size_t MAX_PACKAGE_SIZE = 8192;

class PrimaryConnection final : public std::enable_shared_from_this<PrimaryConnection> {
public:
    static std::shared_ptr<PrimaryConnection> Create(IOContext& context);

    void Connect(const TCPEndpoint& endpoint, std::shared_ptr<SSLContext> sslContext, const std::function<void(bool)>& callback = nullptr);
    void Seek(const TCPEndpoint& endpoint, std::shared_ptr<SSLContext> sslContext, const std::function<void(bool)>& callback = nullptr);
    void Disconnect(const std::function<void(bool)>& callback = nullptr);

    template <StdLayoutOrVecOrString... Args>
    void Send(PC_PackageType type, Args&&... args) {
        static thread_local moodycamel::ProducerToken token(m_packageOut);

        m_packageOut.enqueue(token, Package<PC_PackageType>::CreateUnique(type, std::forward<Args>(args)...));
        m_sendFlag.Signal();
    }

    template <StdLayoutOrVecOrString... Args>
    void SendWithFlag(const PC_PackageType type, const uint8_t flag, Args&&... args) {
        static thread_local moodycamel::ProducerToken token(m_packageOut);

        std::unique_ptr<Package<PC_PackageType>> package = Package<PC_PackageType>::CreateUnique(type, std::forward<Args>(args)...);
        package->GetHeader().flags = flag;

        m_packageOut.enqueue(token, std::move(package));
        m_sendFlag.Signal();
    }

    std::optional<std::unique_ptr<Package<PC_PackageType>>> GetPackage();
    bool HasPendingPackages() const;


private:
    explicit PrimaryConnection(IOContext& context);

    asio::awaitable<void> CoConnect(TCPEndpoint endpoint, std::function<void(bool)> callback);
    asio::awaitable<void> CoSeek(TCPEndpoint endpoint, std::function<void(bool)> callback);
    asio::awaitable<void> CoDisconnect(std::function<void(bool)> callback);
    asio::awaitable<void> CoSend();
    asio::awaitable<void> CoReceive();

    IOContext&      m_context;
    asio::strand<asio::io_context::executor_type> m_strand;

    std::shared_ptr<SSLContext> m_sslContext;
    std::unique_ptr<SSLSocket>  m_socket;

    AwaitableFlag  m_sendFlag;

    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageOut;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageIn;

    ConnectionState    m_connectionState{ConnectionState::DISCONNECTED};

};

#endif //PRIMARY_CONNECTION_H
