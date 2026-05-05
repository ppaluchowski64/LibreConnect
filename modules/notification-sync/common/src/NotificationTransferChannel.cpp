#include <NotificationTransferChannel.h>
#include <Package.h>

static constexpr size_t MAX_NOTIFICATION_PACKET_SIZE = 32 * 1024 * 1024;
static constexpr size_t IO_WAIT_DELAY_MS = 5;

NotificationTransferChannel::NotificationTransferChannel(const std::shared_ptr<SSLContext_>& sslContext, IOContext& context) : m_context(context), m_socket(nullptr), m_sslContext(sslContext), m_buffer(1024 * 128) {}

asio::awaitable<void> NotificationTransferChannel::WaitForIoSlot(std::atomic<bool>& flag) const {
    asio::steady_timer timer(m_context.get_executor());

    while (m_connectionState.load() == ConnectionState::CONNECTED) {
        bool expected = false;
        if (flag.compare_exchange_weak(expected, true, std::memory_order_acq_rel)) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(IO_WAIT_DELAY_MS));
        co_await timer.async_wait(asio::use_awaitable);
    }
}

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
        Debug::Log("Notification transfer channel connecting to {}:{}", endpoint.address().to_string(), endpoint.port());

        co_await CleanupConnection();
        auto socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        Debug::Log("Notification transfer channel socket created, starting TLS connect");
        co_await asio::async_connect(socket->lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);
        co_await socket->async_handshake(SSLStreamBase::client, asio::use_awaitable);

        if (m_connectionState.load() != ConnectionState::CONNECTING) {
            co_await CleanupSSLSocket(socket.get());
            co_return;
        }

        m_socket = std::move(socket);
        Debug::Log("Accepted TLS connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);
        Debug::Log("Notification transfer channel connected");

    } catch (const std::exception& error) {
        Debug::LogError("Notification transfer channel connect failed: {}", error.what());
        asio::co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> NotificationTransferChannel::Seek(AwaitableFlag& flag, uint16_t& port) {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);
        Debug::Log("Notification transfer channel opening listener");

        co_await CleanupConnection();
        auto socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);
        const std::shared_ptr<TCPAcceptor> acceptor = std::make_shared<TCPAcceptor>(m_context);
        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 0);

        {
            std::lock_guard lock(m_acceptorMutex);
            m_acceptor = acceptor;
        }

        acceptor->open(endpoint.protocol());
        acceptor->set_option(asio::socket_base::reuse_address(true));
        acceptor->bind(endpoint);
        acceptor->listen();

        port = acceptor->local_endpoint().port();
        flag.Signal();
        Debug::Log("Notification transfer channel listening on port {}", port);

        Debug::Log("Notification transfer channel waiting for incoming connection");
        co_await acceptor->async_accept(socket->lowest_layer(), asio::use_awaitable);
        co_await socket->async_handshake(SSLStreamBase::server, asio::use_awaitable);

        {
            std::lock_guard lock(m_acceptorMutex);
            if (m_acceptor == acceptor) {
                m_acceptor.reset();
            }
        }

        if (m_connectionState.load() != ConnectionState::CONNECTING) {
            co_await CleanupSSLSocket(socket.get());
            co_return;
        }

        m_socket = std::move(socket);
        Debug::Log("Accepted TLS connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);
        Debug::Log("Notification transfer channel connected");

    } catch (const std::exception& error) {
        std::lock_guard lock(m_acceptorMutex);
        m_acceptor.reset();
        Debug::LogError("Notification transfer channel seek failed: {}", error.what());
        asio::co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> NotificationTransferChannel::Disconnect() {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();

    if (m_connectionState.load() == ConnectionState::DISCONNECTED || m_connectionState.load() == ConnectionState::DISCONNECTING) {
        co_return;
    }

    Debug::Log("Notification transfer channel disconnect requested");
    m_connectionState.store(ConnectionState::DISCONNECTING);

    std::shared_ptr<TCPAcceptor> acceptor;
    {
        std::lock_guard lock(m_acceptorMutex);
        acceptor = std::move(m_acceptor);
    }

    if (acceptor) {
        std::error_code ec;
        acceptor->cancel(ec);
        if (ec && ec != asio::error::not_connected && ec != asio::error::bad_descriptor) {
            HandleAsioError(ec);
        }

        ec.clear();
        acceptor->close(ec);
        if (ec && ec != asio::error::not_connected && ec != asio::error::bad_descriptor) {
            HandleAsioError(ec);
        }
    }

    co_await CleanupConnection();
    m_connectionState.store(ConnectionState::DISCONNECTED);
    Debug::Log("Notification transfer channel disconnected");
}

asio::awaitable<void> NotificationTransferChannel::CleanupConnection() {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();
    Debug::Log("Notification transfer channel cleaning up socket");
    co_await CleanupSSLSocket(m_socket.get());
    m_socket.reset();
}

asio::awaitable<bool> NotificationTransferChannel::Send(const NotificationPacket& data) {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();
    try {
        if (!m_socket || m_connectionState.load() != ConnectionState::CONNECTED) {
            Debug::LogWarning("Notification transfer channel send requested while disconnected");
            co_return false;
        }

        co_await WaitForIoSlot(m_writeInProgress);
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            m_writeInProgress.store(false, std::memory_order_release);
            co_return false;
        }

        const size_t size = data.GetSerializedSize();

        if (m_buffer.size() < size) {
            m_buffer.resize(size);
        }

        {
            size_t offset = 0;
            data.Serialize(m_buffer, offset);
        }

        Debug::Log("Notification transfer channel sending packet (payload bytes: {}, buffer size: {})", size, m_buffer.size());

        {
            std::vector<uint8_t> headerBuffer(sizeof(size_t));
            size_t offset = 0;
            SerializeObject(size, headerBuffer, offset);

            const asio::const_buffer buffer(headerBuffer.data(), headerBuffer.size());
            co_await asio::async_write(*m_socket, buffer, asio::use_awaitable);
        }

        {
            const asio::const_buffer buffer(m_buffer.data(), size);
            co_await asio::async_write(*m_socket, buffer, asio::use_awaitable);
        }
        Debug::Log("Notification transfer channel sent packet (payload bytes: {})", size);
        m_writeInProgress.store(false, std::memory_order_release);

    } catch (const std::exception& error) {
        Debug::LogError("Notification transfer channel send failed: {}", error.what());
        m_writeInProgress.store(false, std::memory_order_release);
        asio::co_spawn(m_context, Disconnect(), asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<std::optional<NotificationPacket>> NotificationTransferChannel::Receive() {
    const std::shared_ptr<NotificationTransferChannel> self = shared_from_this();
    NotificationPacket data;

    try {
        if (!m_socket || m_connectionState.load() != ConnectionState::CONNECTED) {
            Debug::LogWarning("Notification transfer channel receive requested while disconnected");
            co_return std::nullopt;
        }

        co_await WaitForIoSlot(m_readInProgress);
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            m_readInProgress.store(false, std::memory_order_release);
            co_return std::nullopt;
        }

        size_t payloadSize = 0;
        std::vector<uint8_t> headerBuffer(sizeof(size_t));

        {
            const asio::mutable_buffer buffer(headerBuffer.data(), headerBuffer.size());
            Debug::Log("Notification transfer channel awaiting packet header");
            co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);
        }

        size_t offset = 0;
        DeserializeObject(payloadSize, headerBuffer, offset);

        if (payloadSize > MAX_NOTIFICATION_PACKET_SIZE) {
            Debug::LogError("Notification packet too large: {} bytes", payloadSize);
            m_readInProgress.store(false, std::memory_order_release);
            asio::co_spawn(m_context, Disconnect(), asio::detached);
            co_return std::nullopt;
        }

        if (m_buffer.size() < payloadSize) {
            m_buffer.resize(payloadSize);
        }

        {
            const asio::mutable_buffer buffer(m_buffer.data(), payloadSize);
            Debug::Log("Notification transfer channel awaiting packet payload");
            co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);
        }

        offset = 0;
        data.Deserialize(m_buffer, offset);
        Debug::Log("Notification transfer channel received packet");
        m_readInProgress.store(false, std::memory_order_release);

    } catch (const std::exception& error) {
        Debug::LogError("Notification transfer channel receive failed: {}", error.what());
        m_readInProgress.store(false, std::memory_order_release);
        asio::co_spawn(m_context, Disconnect(), asio::detached);
        co_return std::nullopt;
    }

    co_return data;
}
