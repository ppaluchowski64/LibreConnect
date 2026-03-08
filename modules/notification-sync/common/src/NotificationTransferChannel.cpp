#include <NotificationTransferChannel.h>
#include <Package.h>

NotificationTransferChannel::NotificationTransferChannel(const std::shared_ptr<SSLContext>& sslContext, IOContext& context) : m_context(context), m_socket(nullptr), m_sslContext(sslContext), m_buffer(1024 * 128) {}

bool NotificationTransferChannel::IsUsed() const {
    return m_used.load();
}

ConnectionState NotificationTransferChannel::GetConnectionState() const {
    return m_connectionState.load();
}

asio::awaitable<void> NotificationTransferChannel::Connect(TCPEndpoint endpoint) {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        co_await asio::async_connect(m_socket->lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);
        co_await m_socket->async_handshake(SSLStreamBase::client, asio::use_awaitable);

        Debug::Log("Accepted TLS connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> NotificationTransferChannel::Seek(AwaitableFlag& flag, uint16_t& port) {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        TCPAcceptor acceptor(m_context);
        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 0);

        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        port = acceptor.local_endpoint().port();
        flag.Signal();

        co_await acceptor.async_accept(m_socket->lowest_layer(), asio::use_awaitable);
        co_await m_socket->async_handshake(SSLStreamBase::server, asio::use_awaitable);

        Debug::Log("Accepted TLS connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> NotificationTransferChannel::Disconnect() {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();

    if (m_connectionState.load() == ConnectionState::DISCONNECTED || m_connectionState.load() == ConnectionState::DISCONNECTING) {
        co_return;
    }

    m_connectionState.store(ConnectionState::DISCONNECTING);
    co_await CleanupConnection();
    m_connectionState.store(ConnectionState::DISCONNECTED);
}

asio::awaitable<void> NotificationTransferChannel::CleanupConnection() {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();
    co_await CleanupSSLSocket(m_socket.get());
    m_socket.reset();
}

asio::awaitable<bool> NotificationTransferChannel::Send(const NotificationData& data) {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();
    try {

        {
            size_t offset = 0;
            data.Serialize(m_buffer, offset);
        }

        const size_t size = data.GetSerializedSize();

        if (m_buffer.size() < size) {
            m_buffer.resize(size);
        }

        {
            size_t offset = 0;
            SerializeObject(size, m_buffer, offset);

            const asio::const_buffer buffer(m_buffer.data(), GetObjectSerializedSize(size));
            co_await asio::async_write(*m_socket, buffer, asio::use_awaitable);
        }

        {
            const asio::const_buffer buffer(m_buffer.data(), size);
            co_await asio::async_write(*m_socket, buffer, asio::use_awaitable);
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(m_context, Disconnect(), asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<std::optional<NotificationData>> NotificationTransferChannel::Receive() {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();
    NotificationData data;

    try {
        size_t payloadSize = 0;

        {
            const asio::mutable_buffer buffer(m_buffer.data(), GetObjectSerializedSize(payloadSize));
            co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);
        }

        if (m_buffer.size() < payloadSize) {
            m_buffer.resize(payloadSize);
        }

        {
            const asio::mutable_buffer buffer(m_buffer.data(), payloadSize);
            co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);
        }

        size_t offset = 0;
        data.Deserialize(m_buffer, offset);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(m_context, Disconnect(), asio::detached);
        co_return std::nullopt;
    }

    co_return data;
}