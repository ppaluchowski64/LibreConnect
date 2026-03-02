#include <CryptographicIdentityManager.h>
#include <nlohmann/json.hpp>

#include <ConnectionManager.h>
#include <Events.h>
#include <PrimaryConnection.h>
#include <asio/buffer.hpp>

class ConnectionManager;

PrimaryConnection::PrimaryConnection(IOContext& context)
    : m_context(context), m_strand(asio::make_strand(context)), m_sslContext(nullptr), m_socket(nullptr),
      m_sendFlag(context.get_executor()), m_receiveFlag(std::make_shared<AwaitableFlag>(context.get_executor())) {
}

std::shared_ptr<PrimaryConnection> PrimaryConnection::Create(IOContext& context) {
    return std::make_shared<PrimaryConnection>(context);
}

void PrimaryConnection::Connect(const std::shared_ptr<SSLContext>& sslContext, const InitialConnectionData& data) {
    asio::co_spawn(m_strand, CoConnect(sslContext, data), asio::detached);
}

void PrimaryConnection::Seek(const std::shared_ptr<SSLContext>& sslContext, const InitialConnectionData& data, std::function<void(TCPEndpoint)>&& callback) {
    asio::co_spawn(m_strand, CoSeek(sslContext, data, std::move(callback)), asio::detached);
}

void PrimaryConnection::Disconnect(const std::error_code errorCode, const bool callConnectionManagerDisconnect) {
    asio::co_spawn(m_strand, CoDisconnect(errorCode, callConnectionManagerDisconnect), asio::detached);
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

asio::awaitable<void> PrimaryConnection::CoConnect(const std::shared_ptr<SSLContext> sslContext, const InitialConnectionData data) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();
    m_sslContext = sslContext;
    m_connectionState.store(ConnectionState::CONNECTING);

    try {
        TCPEndpoint endpoint(asio::ip::make_address_v4(data.deviceInfo.deviceAddress), data.deviceInfo.deviceAddressPort);

        Debug::Log("PrimaryConnection: Attempting to connect to {}:{}", endpoint.address().to_string(), endpoint.port());

        co_await CoCleanupConnection();
        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        co_await asio::async_connect(m_socket->lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);

        Debug::Log("PrimaryConnection: TCP Connection established. Starting SSL handshake...");
        co_await m_socket->async_handshake(SSLStreamBase::client, asio::use_awaitable);

        Debug::Log("PrimaryConnection: Accepted TLS primary connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());

        m_connectionState.store(ConnectionState::CONNECTED);

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);

        const std::unique_ptr<QEvent> event = std::make_unique<ConnectedEvent>(EventResult::SUCCESS);
        ConnectionManager::SendEvent(event);

        if (data.initialConnectionMode != InitialConnectionMode::CONNECTION_WITHOUT_PAIR) {
            SavePairData(data);
        }

        if (data.initialConnectionMode == InitialConnectionMode::PAIR_AND_CONNECT) {
            SaveCertificate(data);
        }

    } catch (std::system_error& error) {
        Debug::Log("PrimaryConnection: Connection failed: {} (code: {})", error.what(), error.code().value());
        HandleAsioError(error.code());
        Disconnect(error.code());
        const std::unique_ptr<QEvent> event = std::make_unique<ConnectedEvent>(EventResult::FAILURE);
        ConnectionManager::SendEvent(event);
    }
}

asio::awaitable<void> PrimaryConnection::CoSeek(const std::shared_ptr<SSLContext> sslContext, InitialConnectionData data, std::function<void(TCPEndpoint)> callback) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();
    m_sslContext = sslContext;
    m_connectionState.store(ConnectionState::CONNECTING);

    try {
        co_await CoCleanupConnection();
        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        TCPAcceptor acceptor(m_context);
        TCPEndpoint listenEndpoint(asio::ip::tcp::v4(), 0);
        acceptor.open(listenEndpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(listenEndpoint);
        acceptor.listen();

        listenEndpoint = acceptor.local_endpoint();
        Debug::Log("PrimaryConnection: Seeking connection: Listening on port {}", listenEndpoint.port());

        asio::post(
            m_context,
            [cb = std::move(callback), listenEndpoint]() mutable {
                cb(listenEndpoint);
            }
        );

        co_await acceptor.async_accept(m_socket->lowest_layer(), asio::use_awaitable);
        Debug::Log("PrimaryConnection: Incoming connection accepted. Starting SSL handshake as server...");

        co_await m_socket->async_handshake(SSLStreamBase::server, asio::use_awaitable);

        Debug::Log("PrimaryConnection: Accepted TLS primary connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());

        m_connectionState.store(ConnectionState::CONNECTED);

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);

        const std::unique_ptr<QEvent> event = std::make_unique<ConnectedEvent>(EventResult::SUCCESS);
        ConnectionManager::SendEvent(event);

        if (data.initialConnectionMode != InitialConnectionMode::CONNECTION_WITHOUT_PAIR) {
            SavePairData(data);
        }

        if (data.initialConnectionMode == InitialConnectionMode::PAIR_AND_CONNECT) {
            SaveCertificate(data);
        }

    } catch (std::system_error& error) {
        Debug::Log("PrimaryConnection: Seek failed: {} (code: {})", error.what(), error.code().value());
        HandleAsioError(error.code());
        Disconnect(error.code());
        const std::unique_ptr<QEvent> event = std::make_unique<ConnectedEvent>(EventResult::FAILURE);
        ConnectionManager::SendEvent(event);
    }
}

asio::awaitable<void> PrimaryConnection::CoDisconnect(const std::error_code errorCode, const bool callConnectionManagerDisconnect) {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();

    if (m_connectionState == ConnectionState::DISCONNECTED || m_connectionState == ConnectionState::DISCONNECTING) {
        co_return;
    }

    Debug::Log("PrimaryConnection: Disconnecting primary connection. Reason: {} (code: {})", errorCode.message(), errorCode.value());

    m_connectionState.store(ConnectionState::DISCONNECTING);
    co_await CoCleanupConnection();
    m_connectionState.store(ConnectionState::DISCONNECTED);

    Debug::Log("PrimaryConnection: Disconnected TLS primary connection successfully.");

    if (callConnectionManagerDisconnect) {
        ConnectionManager::Disconnect(errorCode);
    }
}

asio::awaitable<void> PrimaryConnection::CoCleanupConnection() {
    const std::shared_ptr<PrimaryConnection> self = shared_from_this();
    if (!m_socket) {
        co_return;
    }

    try {
        if (m_socket->lowest_layer().is_open()) {
            m_socket->lowest_layer().cancel();
            co_await m_socket->async_shutdown(asio::use_awaitable);
            m_socket->lowest_layer().close();
        }

        m_socket.reset();
    } catch (std::system_error& error) {
        HandleAsioError(error.code());
    }
}

asio::awaitable<void> PrimaryConnection::CoSend() {
    try {
        const std::shared_ptr<PrimaryConnection> self = shared_from_this();
        moodycamel::ConsumerToken token(m_packageOut);

        Debug::Log("PrimaryConnection: Starting CoSend loop.");

        while (m_connectionState.load() == ConnectionState::CONNECTED) {
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
                    break;
                }
            }
        }
    } catch (std::system_error& error) {
        if (error.code() != asio::error::operation_aborted) {
            Debug::Log("PrimaryConnection: CoSend error: {}", error.what());
        }
        HandleAsioError(error.code());
        Disconnect(error.code());
    }
}

asio::awaitable<void> PrimaryConnection::CoReceive() {
    try {
        const std::shared_ptr<PrimaryConnection> self = shared_from_this();
        moodycamel::ProducerToken token(m_packageIn);
        std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
        PackageHeader header{};

        Debug::Log("PrimaryConnection: Starting CoReceive loop.");

        while (m_connectionState.load() == ConnectionState::CONNECTED) {
            asio::mutable_buffer headerMutableBuffer(headerBuffer.data(), headerBuffer.size());
            co_await asio::async_read(*m_socket, headerMutableBuffer, asio::use_awaitable);

            size_t offset = 0;
            header.Deserialize(headerBuffer, offset);

            if (m_connectionState.load() != ConnectionState::CONNECTED) break;

            if (header.size > MAX_PACKAGE_SIZE) {
                Debug::Log("PrimaryConnection: Protocol Error: Received package size {} exceeds MAX_PACKAGE_SIZE", header.size);
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
        if (error.code() != asio::error::operation_aborted && error.code() != asio::error::eof) {
            Debug::Log("PrimaryConnection: CoReceive error: {}", error.what());
        }
        HandleAsioError(error.code());
        Disconnect(error.code());
    }
}

void PrimaryConnection::SavePairData(const InitialConnectionData& data) {
    const std::string targetDataPath{"certs/" + boost::uuids::to_string(data.deviceInfo.deviceID) + "/data.JSON"};

    nlohmann::json targetData;
    targetData["name"] = data.deviceInfo.deviceName;
    targetData["type"] = data.deviceInfo.deviceType;

    std::ofstream file(targetDataPath);
    file << targetData.dump(4);
}

void PrimaryConnection::SaveCertificate(const InitialConnectionData& data) const {
    const std::string targetCertificatePath{"certs/" + boost::uuids::to_string(data.deviceInfo.deviceID) + "/cert.key"};
    CryptographicIdentityManager::SavePeerCertificate(targetCertificatePath, m_socket.get());
}
