#ifndef PRIMARY_CONNECTION_H
#define PRIMARY_CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <Package.h>
#include <AsioCommon.h>
#include <concurrentqueue.h>
#include <asio/awaitable.hpp>
#include <AwaitableFlag.h>

enum class PC_PackageType : PackageTypeInt {
    NONE = 0,
};

constexpr size_t MAX_PACKAGE_SIZE = 8192;

class PrimaryConnection final : public std::enable_shared_from_this<PrimaryConnection> {
public:
    explicit PrimaryConnection(IOContext& context, SSLContext& sslContext, std::atomic<bool>& shutdownRequested, moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>>& packageIn);
    void Connect(TCPEndpoint endpoint, const std::function<void(bool)>& callback);
    void Seek(TCPEndpoint endpoint, const std::function<void(bool)>& callback);
    void Disconnect();

    template <StdLayoutOrVecOrString... Args>
    void Send(PC_PackageType type, Args&&... args) {
        static thread_local moodycamel::ProducerToken token(m_packageOut);
        m_packageOut.enqueue(token, Package<PC_PackageType>::CreateUnique(type, std::forward<Args>(args)...));
        m_sendFlag.Signal();
    }


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

    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>>  m_packageOut;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<PC_PackageType>>>& m_packageIn;

    std::atomic<bool>& m_shutdownRequested;
    bool               m_isRunning;

};

#endif //PRIMARY_CONNECTION_H
