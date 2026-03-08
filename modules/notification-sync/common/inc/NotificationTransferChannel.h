#ifndef NOTIFICATIONLISTENER_KT_NOTIFICATIONTRANSFERCHANNEL_H
#define NOTIFICATIONLISTENER_KT_NOTIFICATIONTRANSFERCHANNEL_H

#include <filesystem>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <AsioCommon.h>
#include <AwaitableFlag.h>

#include <NotificationData.h>

class NotificationTransferChannel final : public std::enable_shared_from_this<NotificationTransferChannel> {
public:
    NotificationTransferChannel() = delete;
    NotificationTransferChannel(const NotificationTransferChannel&) = delete;
    NotificationTransferChannel& operator=(const NotificationTransferChannel&) = delete;
    explicit NotificationTransferChannel(const std::shared_ptr<SSLContext>& sslContext, IOContext& context);

    bool IsUsed() const;
    ConnectionState GetConnectionState() const;

    asio::awaitable<void> Connect(TCPEndpoint endpoint);
    asio::awaitable<void> Seek(AwaitableFlag& flag, uint16_t& port);
    asio::awaitable<void> Disconnect();
    asio::awaitable<void> CleanupConnection();

    // To call this function use co_await, NEVER CALL IT WITH CO_SPAWN
    asio::awaitable<bool> Send(const NotificationData& data);
    asio::awaitable<std::optional<NotificationData>> Receive();

private:
    IOContext& m_context;
    std::unique_ptr<SSLSocket> m_socket;
    std::shared_ptr<SSLContext> m_sslContext;
    std::vector<uint8_t> m_buffer;
    std::atomic<bool> m_used;
    std::atomic<ConnectionState> m_connectionState{ConnectionState::DISCONNECTED};
};

#endif //NOTIFICATIONLISTENER_KT_NOTIFICATIONTRANSFERCHANNEL_H