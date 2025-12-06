#ifndef INITIAL_CONNECTION_H
#define INITIAL_CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <Package.h>
#include <Packable.h>
#include <AsioCommon.h>
#include <concurrentqueue.h>
#include <asio/awaitable.hpp>
#include <AwaitableFlag.h>
#include <deque>
#include <optional>
#include <functional>

enum class InitialConnectionMode : uint8_t {
    PAIR_AND_CONNECT,
    CONNECT_WITH_PAIR,
    CONNECTION_WITHOUT_PAIR
};

enum class InitialConnectionPackageType : PackageTypeInt {
    CONNECT_INFO
};

class InitialConnection final : public std::enable_shared_from_this<InitialConnection> {
public:
    explicit InitialConnection(IOContext& context);
    InitialConnection() = delete;

    static std::shared_ptr<InitialConnection> Create(IOContext& context);

    void Connect(TCPEndpoint&& endpoint, InitialConnectionMode mode);
    void Seek(TCPEndpoint&& endpoint);
    void Disconnect();

private:
    asio::awaitable<void> CoConnect(TCPEndpoint endpoint, InitialConnectionMode mode);
    asio::awaitable<void> CoSeek(TCPEndpoint endpoint);
    asio::awaitable<void> CoDisconnect();
    asio::awaitable<void> CoSend();
    asio::awaitable<void> CoReceive();

    IOContext& m_context;
    asio::strand<asio::io_context::executor_type> m_strand;

    AwaitableFlag m_sendFlag;
    TCPSocket m_socket;

    std::deque<std::unique_ptr<Package<InitialConnectionPackageType>>> m_packagesOut;
    ConnectionState m_connectionState{ConnectionState::DISCONNECTED};
};

#endif //INITIAL_CONNECTION_H
