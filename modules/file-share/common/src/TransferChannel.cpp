#include <TransferChannel.h>
#include <fstream>

static constexpr size_t TRANSFER_BUFFER_SIZE = 1024 * 256;

TransferChannel::TransferChannel(const std::shared_ptr<SSLContext>& sslContext, IOContext& context) : m_context(context), m_socket(nullptr), m_sslContext(sslContext), m_buffer(TRANSFER_BUFFER_SIZE) { }

size_t TransferChannel::FetchTransferProgress() const {
    return m_progress.load();
}

bool TransferChannel::IsUsed(const bool outTransfer) const {
    return outTransfer ? m_send.load() : m_recv.load();
}

ConnectionState TransferChannel::GetConnectionState() const {
    return m_connectionState.load();
}

asio::awaitable<void> TransferChannel::Connect(TCPEndpoint endpoint) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

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
        co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> TransferChannel::Seek(AwaitableFlag& flag, uint16_t& port) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

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
        co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> TransferChannel::Receive(const std::filesystem::path& file, const uint64_t length) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            co_return;
        }

        if (std::filesystem::is_directory(file)) {
            Debug::LogError("File doesnt exists");
            co_return;
        }

        if (m_recv.load()) {
            co_return;
        }

        m_recv.store(true);

        std::fstream fileStream(file, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        size_t offset = 0;
        m_progress.store(0);

        while (offset < length) {
            const size_t bufferSize = std::min(m_buffer.size(), length - offset);
            offset += bufferSize;

            asio::mutable_buffer buffer(m_buffer.data(), bufferSize);
            co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);

            fileStream.write(reinterpret_cast<const char*>(m_buffer.data()), bufferSize);
            m_progress.fetch_add(bufferSize);
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        co_spawn(m_context, Disconnect(), asio::detached);
    }

    m_recv.store(false);
}

asio::awaitable<void> TransferChannel::Send(const std::filesystem::path file, const uint64_t length) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            co_return;
        }

        if (!std::filesystem::exists(file) || std::filesystem::is_directory(file)) {
            Debug::LogError("File doesnt exists");
            co_return;
        }

        if (m_send.load()) {
            co_return;
        }

        m_send.store(true);

        std::ifstream fileStream(file, std::ios::binary);

        size_t offset = 0;
        m_progress.store(0);

        while (offset < length) {
            const size_t bufferSize = std::min(m_buffer.size(), length - offset);
            offset += bufferSize;

            fileStream.read(reinterpret_cast<char*>(m_buffer.data()), bufferSize);
            asio::const_buffer buffer(m_buffer.data(), bufferSize);

            co_await asio::async_write(*m_socket, buffer, asio::use_awaitable);
            m_progress.fetch_add(bufferSize);
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        co_spawn(m_context, Disconnect(), asio::detached);
    }

    m_send.store(false);
}

asio::awaitable<void> TransferChannel::Disconnect() {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    if (m_connectionState.load() == ConnectionState::DISCONNECTED || m_connectionState.load() == ConnectionState::DISCONNECTING) {
        co_return;
    }

    m_connectionState.store(ConnectionState::DISCONNECTING);
    co_await CleanupConnection();
    m_connectionState.store(ConnectionState::DISCONNECTED);
}

asio::awaitable<void> TransferChannel::CleanupConnection() {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    co_await CleanupSSLSocket(m_socket.get());
    m_socket.reset();
}
