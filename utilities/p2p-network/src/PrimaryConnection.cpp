#include <PrimaryConnection.h>
#include <asio/buffer.hpp>

PrimaryConnection::PrimaryConnection(IOContext& context, SSLContext& sslContext, std::atomic<bool>& shutdownRequested)
    : m_context(context), m_sslContext(sslContext), m_strand(asio::make_strand(context)), m_socket(context, sslContext),
      m_sendFlag(context.get_executor()), m_shutdownRequested(shutdownRequested), m_isRunning(false) {
}

void PrimaryConnection::Connect(const TCPEndpoint& endpoint, const std::function<void(bool)>& callback) {
    if (m_socket.lowest_layer().is_open()) {
        return;
    }

    Debug::Log("PrimaryConnection::Connect");

    asio::co_spawn(m_strand, CoConnect({endpoint}, callback), asio::detached);
}

void PrimaryConnection::Seek(const TCPEndpoint& endpoint, const std::function<void(bool)>& callback) {
    if (m_socket.lowest_layer().is_open()) {
        return;
    }

    Debug::Log("PrimaryConnection::Seek");

    asio::co_spawn(m_strand, CoSeek({endpoint}, callback), asio::detached);
}

void PrimaryConnection::Disconnect() {
    if (!m_socket.lowest_layer().is_open()) {
        return;
    }

    asio::co_spawn(m_strand, CoDisconnect(), asio::detached);
}

std::optional<std::unique_ptr<Package<PC_PackageType>>> PrimaryConnection::GetPackage() {
    static thread_local moodycamel::ConsumerToken token(m_packageIn);

    if (std::unique_ptr<Package<PC_PackageType>> package; m_packageIn.try_dequeue(token, package)) {
        return std::move(package);
    }

    return std::nullopt;
}

bool PrimaryConnection::HasPendingPackages() const {
    return m_packageIn.size_approx() > 0;
}

asio::awaitable<void> PrimaryConnection::CoConnect(const TCPEndpoint endpoint, const std::function<void(bool)> callback) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();

    try {
        co_await asio::async_connect(m_socket.lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);
        co_await m_socket.async_handshake(SSLStreamBase::client, asio::use_awaitable);

        Debug::Log("Accepted TLS primary connection to {}:{}", m_socket.lowest_layer().remote_endpoint().address().to_string(), m_socket.lowest_layer().remote_endpoint().port());

        m_isRunning = true;

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);
        callback(true);

    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection connection error: {}", error.what());
        }

        m_shutdownRequested.store(true);
        callback(false);
    }

    co_return;
}

asio::awaitable<void> PrimaryConnection::CoSeek(const TCPEndpoint endpoint, const std::function<void(bool)> callback) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();

    try {
        TCPAcceptor acceptor(m_context, endpoint);
        co_await acceptor.async_accept(m_socket.lowest_layer(), asio::use_awaitable);
        co_await m_socket.async_handshake(SSLStreamBase::server, asio::use_awaitable);

        Debug::Log("Accepted TLS primary connection to {}:{}", m_socket.lowest_layer().remote_endpoint().address().to_string(), m_socket.lowest_layer().remote_endpoint().port());

        m_isRunning = true;

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);
        callback(true);

    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection connection seek error: {}", error.what());
        }

        m_shutdownRequested.store(true);
        callback(false);
    }

    co_return;
}

asio::awaitable<void> PrimaryConnection::CoDisconnect() {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();
    m_isRunning = false;

    try {
        m_socket.lowest_layer().cancel();
        co_await m_socket.async_shutdown(asio::use_awaitable);
        m_socket.lowest_layer().close();
    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection disconnect error: {}", error.what());
        }

        m_shutdownRequested.store(true);
    }
}

asio::awaitable<void> PrimaryConnection::CoSend() {
    try {
        const std::shared_ptr<PrimaryConnection> self = shared_from_this();
        moodycamel::ConsumerToken token(m_packageOut);

        co_await m_sendFlag.Wait();
        m_sendFlag.Reset();

        while (m_isRunning) {
            if (std::unique_ptr<Package<PC_PackageType>> package; m_packageOut.try_dequeue(token, package)) {\
                PackageHeader& header = package->GetHeader();

                std::vector<asio::const_buffer> constBuffers {
                    asio::const_buffer(&header, sizeof(PackageHeader)),
                    asio::const_buffer(package->GetRawBody(), header.size)
                };

                header.FromNativeToBigEndian();
                co_await asio::async_write(m_socket, constBuffers, asio::use_awaitable);
            } else {
                co_await m_sendFlag.Wait();
                m_sendFlag.Reset();
            }
        }
    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection send error: {}", error.what());
        }

        m_shutdownRequested.store(true);
    }
}

asio::awaitable<void> PrimaryConnection::CoReceive() {
    try {
        const std::shared_ptr<PrimaryConnection> self = shared_from_this();
        moodycamel::ProducerToken token(m_packageIn);
        PackageHeader header{};

        while (m_isRunning) {
            asio::mutable_buffer headerBuffer(&header, sizeof(PackageHeader));
            co_await asio::async_read(m_socket, headerBuffer, asio::use_awaitable);
            header.FromBigEndianToNative();

            if (!m_isRunning) break;

            if (header.size > MAX_PACKAGE_SIZE) {
                throw std::runtime_error("PrimaryConnection receive package size too large");
            }

            std::unique_ptr<Package<PC_PackageType>> package = std::make_unique<Package<PC_PackageType>>(header);
            asio::mutable_buffer packageBuffer(package->GetRawBody(), header.size);

            co_await asio::async_read(m_socket, packageBuffer, asio::use_awaitable);

            if (!m_isRunning) break;

            m_packageIn.enqueue(std::move(package));
        }
    } catch (std::system_error& error) {
        if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe) {
            Debug::Log("Connection closed by peer");
        } else {
            Debug::LogError("PrimaryConnection receive error: {}", error.what());
        }

        m_shutdownRequested.store(true);
    }
}


