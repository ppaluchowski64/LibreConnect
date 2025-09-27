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

enum class PC_PackageType : PackageTypeInt {
    NONE = 0,
};

constexpr size_t MAX_PACKAGE_SIZE = 8192;

class PrimaryConnection final : public std::enable_shared_from_this<PrimaryConnection> {
public:
    explicit PrimaryConnection(IOContext& context, SSLContext& sslContext, std::atomic<bool>& shutdownRequested);
    void Connect(const TCPEndpoint& endpoint, const std::function<void(bool)>& callback);
    void Seek(const TCPEndpoint& endpoint, const std::function<void(bool)>& callback);
    void Disconnect();

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
    asio::awaitable<void> CoConnect(TCPEndpoint endpoint, std::function<void(bool)> callback);
    asio::awaitable<void> CoSeek(TCPEndpoint endpoint, std::function<void(bool)> callback);
    asio::awaitable<void> CoDisconnect();
    asio::awaitable<void> CoSend();
    asio::awaitable<void> CoReceive();

    IOContext&      m_context;
    SSLContext&     m_sslContext;
    asio::strand<asio::io_context::executor_type> m_strand;
    SSLSocket       m_socket;

    AwaitableFlag  m_sendFlag;

    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageOut;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>> m_packageIn;

    std::atomic<bool>& m_shutdownRequested;
    bool               m_isRunning;

};

#endif //PRIMARY_CONNECTION_H
