#include <SignalSender.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

SignalSender::SignalSender() {
    m_socket.open(asio::ip::udp::v4());
    m_socket.connect(UDPEndpoint(asio::ip::make_address_v4("127.0.0.1"), DAEMON_SIGNAL_PORT));
}

void SignalSender::ConnectionSignal(const uuid id) {
    asio::co_spawn(m_strand, CoSendPayload({true, {id, GetPid()}}), asio::detached);
}

void SignalSender::DisconnectionSignal(const uuid id) {
    asio::co_spawn(m_strand, CoSendPayload({false, {id, GetPid()}}), asio::detached);
}


asio::awaitable<void> SignalSender::CoSendPayload(const Payload payload) {
    const asio::const_buffer buffer(&payload, sizeof(payload));
    co_await m_socket.async_send(buffer, asio::use_awaitable);
}
