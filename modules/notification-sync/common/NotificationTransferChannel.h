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

    asio::awaitable<bool> Send();
    asio::awaitable<std::optional<> Receive();

private:
    IOContext& m_context;
    std::unique_ptr<SSLSocket> m_socket;
    std::shared_ptr<SSLContext> m_sslContext;
    std::vector<uint8_t> m_buffer;
    std::atomic<bool> m_used;
    std::atomic<ConnectionState> m_connectionState{ConnectionState::DISCONNECTED};
};

#endif //NOTIFICATIONLISTENER_KT_NOTIFICATIONTRANSFERCHANNEL_H