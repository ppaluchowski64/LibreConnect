#include <PrimaryConnection.h>
#include <asio/buffer.hpp>

PrimaryConnection::PrimaryConnection(IOContext& context)
    : m_context(context), m_strand(asio::make_strand(context)), m_sslContext(nullptr), m_socket(nullptr),
      m_sendFlag(context.get_executor()), m_receiveFlag(std::make_shared<AwaitableFlag>(context.get_executor())) {
}

std::shared_ptr<PrimaryConnection> PrimaryConnection::Create(IOContext& context) {
    return std::make_shared<PrimaryConnection>(context);
}

void PrimaryConnection::Connect(TCPEndpoint&& endpoint, const std::shared_ptr<SSLContext>& sslContext, ConnectionCallbackType&& callback) {
    asio::co_spawn(m_strand, CoConnect(std::move(endpoint), sslContext, std::move(callback)), asio::detached);
}

void PrimaryConnection::Seek(TCPEndpoint&& endpoint, const std::shared_ptr<SSLContext>& sslContext, ConnectionCallbackType&& callback) {
    asio::co_spawn(m_strand, CoSeek(std::move(endpoint), sslContext, std::move(callback)), asio::detached);
}

void PrimaryConnection::Disconnect(DisconnectionCallbackType&& callback) {
    asio::co_spawn(m_strand, CoDisconnect(std::move(callback)), asio::detached);
}

std::optional<std::unique_ptr<Package<PC_PackageType>>> PrimaryConnection::GetPackage() {
    static thread_local moodycamel::ConsumerToken consumerToken(m_packageIn);

    if (std::unique_ptr<Package<PC_PackageType>> package; m_packageIn.try_dequeue(consumerToken, package)) {
        return std::move(package);
    }

    return std::nullopt;
}

std::shared_ptr<AwaitableFlag> PrimaryConnection::GetReceiveFlag() const {
    return m_receiveFlag;
}

bool PrimaryConnection::HasPendingPackages() const {
    return m_packageIn.size_approx() > 0;
}

asio::awaitable<void> PrimaryConnection::CoConnect(TCPEndpoint endpoint, std::shared_ptr<SSLContext> sslContext, ConnectionCallbackType callback) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();
    m_sslContext = sslContext;
    m_connectionState.store(ConnectionState::CONNECTING);

    try {
        if (m_socket && m_socket->lowest_layer().is_open()) {
            co_await CoDisconnect([](){});
        }

        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        co_await asio::async_connect(m_socket->lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);
        co_await m_socket->async_handshake(SSLStreamBase::client, asio::use_awaitable);

        Debug::Log("Accepted TLS primary connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());

        m_connectionState.store(ConnectionState::CONNECTED);

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);

        callback(true);

    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection connection error: {}", std::string(error.what()));
        }

        Disconnect();
        callback(false);
    }

    co_return;
}

asio::awaitable<void> PrimaryConnection::CoSeek(TCPEndpoint endpoint, std::shared_ptr<SSLContext> sslContext, ConnectionCallbackType callback) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();
    m_connectionState.store(ConnectionState::CONNECTING);
    m_sslContext = sslContext;

    try {
        if (m_socket && m_socket->lowest_layer().is_open()) {
            co_await CoDisconnect([](){});
            m_socket.reset();
        }

        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        TCPAcceptor acceptor(m_context, endpoint);
        co_await acceptor.async_accept(m_socket->lowest_layer(), asio::use_awaitable);
        co_await m_socket->async_handshake(SSLStreamBase::server, asio::use_awaitable);

        Debug::Log("Accepted TLS primary connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());

        m_connectionState.store(ConnectionState::CONNECTED);

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);

        callback(true);

    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::ssl::error::stream_truncated || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection connection seek error: {}", std::string(error.what()));
        }

        Disconnect();
        callback(false);
    }

    co_return;
}

asio::awaitable<void> PrimaryConnection::CoDisconnect(const DisconnectionCallbackType callback) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();
    m_connectionState.store(ConnectionState::DISCONNECTING);

    try {
        if (m_socket) {
            m_socket->lowest_layer().cancel();

            if (m_socket->lowest_layer().is_open()) {
                co_await m_socket->async_shutdown(asio::use_awaitable);
                m_socket->lowest_layer().close();
            }

            m_socket.reset();
        }

    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection disconnect error: {}", std::string(error.what()));
        }
    }

    m_connectionState.store(ConnectionState::DISCONNECTED);
    callback();
    co_return;
}

asio::awaitable<void> PrimaryConnection::CoSend() {
    try {
        const std::shared_ptr<PrimaryConnection> self = shared_from_this();
        moodycamel::ConsumerToken token(m_packageOut);

        co_await m_sendFlag.Wait();
        m_sendFlag.Reset();

        std::vector<uint8_t> buffer;
        buffer.resize(PackageHeader::GetSerializedSize());

        while (m_connectionState.load() == ConnectionState::CONNECTED) {
            if (std::unique_ptr<Package<PC_PackageType>> package; m_packageOut.try_dequeue(token, package)) {
                PackageHeader& header = package->GetHeader();

                std::size_t offset = 0;
                header.Serialize(buffer, offset);

                std::vector<asio::const_buffer> constBuffers {
                    asio::const_buffer(buffer.data(), buffer.size()),
                    asio::const_buffer(package->GetRawBody(), header.size)
                };

                co_await asio::async_write(*m_socket, constBuffers, asio::use_awaitable);
            } else {
                co_await m_sendFlag.Wait();
                m_sendFlag.Reset();
            }
        }
    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection send error: {}", std::string(error.what()));
        }

        Disconnect();
    }
}

asio::awaitable<void> PrimaryConnection::CoReceive() {
    try {
        const std::shared_ptr<PrimaryConnection> self = shared_from_this();
        moodycamel::ProducerToken token(m_packageIn);
        std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
        PackageHeader header{};

        while (m_connectionState.load() == ConnectionState::CONNECTED) {
            asio::mutable_buffer headerMutableBuffer(headerBuffer.data(), headerBuffer.size());
            co_await asio::async_read(*m_socket, headerMutableBuffer, asio::use_awaitable);

            size_t offset = 0;
            header.Deserialize(headerBuffer, offset);

            if (m_connectionState.load() != ConnectionState::CONNECTED) break;

            if (header.size > MAX_PACKAGE_SIZE) {
                throw std::runtime_error("PrimaryConnection receive package size too large");
            }

            std::unique_ptr<Package<PC_PackageType>> package = std::make_unique<Package<PC_PackageType>>(header);
            asio::mutable_buffer packageBuffer(package->GetRawBody(), header.size);

            co_await asio::async_read(*m_socket, packageBuffer, asio::use_awaitable);

            if (m_connectionState.load() != ConnectionState::CONNECTED) break;

            m_packageIn.enqueue(std::move(package));
            m_receiveFlag->Signal();
        }
    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection receive error: {}", std::string(error.what()));
        }

        Disconnect();
    }
}


