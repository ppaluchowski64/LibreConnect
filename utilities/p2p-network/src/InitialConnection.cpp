#include <InitialConnection.h>
#include <asio/buffer.hpp>
#include <Events.h>
#include <ConnectionManager.h>
#include <ThreadPool.h>

typedef std::unique_ptr<Package<InitialConnectionPackageType>> InitialConnectionPackagePtr;

InitialConnection::InitialConnection() : m_context(ThreadPool::GetContext()), m_strand(asio::make_strand(m_context)),
                                        m_sendFlag(m_context.get_executor()), m_socket(m_context), m_challengeLeftTries(0) { }

std::shared_ptr<InitialConnection> InitialConnection::Create() {
    return std::make_shared<InitialConnection>();
}

void InitialConnection::Connect(TCPEndpoint&& endpoint, const InitialConnectionMode mode) {
    asio::co_spawn(m_strand, CoConnect(std::move(endpoint), mode), asio::detached);
}

void InitialConnection::Seek(TCPEndpoint&& endpoint, std::function<void(TCPEndpoint endpoint)>&& callback) {
    asio::co_spawn(m_strand, CoSeek(std::move(endpoint), std::move(callback)), asio::detached);
}

void InitialConnection::Disconnect(const bool cancelSeeking) {
    asio::co_spawn(m_strand, CoDisconnect(cancelSeeking), asio::detached);
}

void InitialConnection::TemporaryOwnership(const std::shared_ptr<InitialConnection>& ptr) {
    m_temporaryOwnership = ptr;
}

asio::awaitable<void> InitialConnection::CoConnect(TCPEndpoint endpoint, const InitialConnectionMode mode) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();
    m_connectionState = ConnectionState::CONNECTING;

    try {
        m_socket = TCPSocket(m_context, endpoint.protocol());

        co_await asio::async_connect(m_socket, std::initializer_list<TCPEndpoint>({endpoint}), asio::use_awaitable);
        Debug::Log("Accepted TCP initial connection to {}:{}",  m_socket.remote_endpoint().address().to_string(), m_socket.remote_endpoint().port());

        m_connectionState = ConnectionState::CONNECTED;

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);

        InitialConnectionData data;

        data.deviceInfo = DeviceInfo::GetThisDeviceInfo();
        data.initialConnectionMode = mode;

        InitialConnectionPackagePtr package = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::DEVICE_DATA_FC, data);
        m_packagesOut.emplace_back(std::move(package));
        m_sendFlag.Signal();

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        Disconnect();
    }

    co_return;
}

asio::awaitable<void> InitialConnection::CoSeek(TCPEndpoint endpoint, std::function<void(TCPEndpoint endpoint)> callback) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();
    m_temporaryOwnership.reset();

    m_connectionState = ConnectionState::CONNECTING;

    try {
        m_socket = TCPSocket(m_context);
        TCPAcceptor acceptor(m_context);
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        ConnectionManager::SetSeekingEndpoint(acceptor.local_endpoint());

        co_await acceptor.async_accept(m_socket, asio::use_awaitable);
        Debug::Log("Accepted TCP initial connection to {}:{}",  m_socket.remote_endpoint().address().to_string(), m_socket.remote_endpoint().port());

        TCPEndpoint acceptorEndpoint = acceptor.local_endpoint();

        asio::post(
            m_context,
            [cb = std::move(callback), ep = std::move(acceptorEndpoint)]() mutable {
                cb(std::move(ep));
            }
        );

        m_connectionState = ConnectionState::CONNECTED;

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoDisconnect(const bool cancelSeeking) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();

    if (m_connectionState == ConnectionState::DISCONNECTED || m_connectionState == ConnectionState::DISCONNECTING) {
        co_return;
    }

    m_connectionState = ConnectionState::DISCONNECTING;

    try {
        if (m_socket.is_open()) {
            m_socket.cancel();
            m_socket.shutdown(asio::socket_base::shutdown_both);
            m_socket.close();
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
    }

    m_connectionState = ConnectionState::DISCONNECTED;

    if (cancelSeeking) {
        ConnectionManager::Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoSend() {
    try {
        const std::shared_ptr<InitialConnection> self = shared_from_this();

        co_await m_sendFlag.Wait();
        m_sendFlag.Reset();

        std::vector<uint8_t> buffer;
        buffer.resize(PackageHeader::GetSerializedSize());

        while (m_connectionState == ConnectionState::CONNECTED) {
            if (!m_packagesOut.empty()) {
                const std::unique_ptr<Package<InitialConnectionPackageType>> package = std::move(m_packagesOut.front());
                m_packagesOut.pop_front();

                PackageHeader& header = package->GetHeader();

                size_t offset = 0;
                header.Serialize(buffer, offset);

                std::vector<asio::const_buffer> constBuffers {
                    asio::const_buffer(buffer.data(), buffer.size()),
                    asio::const_buffer(package->GetRawBody(), header.size)
                };

                co_await asio::async_write(m_socket, constBuffers, asio::use_awaitable);

            } else {
                co_await m_sendFlag.Wait();
                m_sendFlag.Reset();
            }
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoReceive() {
    try {
        const std::shared_ptr<InitialConnection> self = shared_from_this();
        std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
        PackageHeader header{};

        InitialConnectionData data;


        while (m_connectionState == ConnectionState::CONNECTED) {
            asio::mutable_buffer headerMutableBuffer(headerBuffer.data(), headerBuffer.size());
            co_await asio::async_read(m_socket, headerMutableBuffer, asio::use_awaitable);
            if (m_connectionState != ConnectionState::CONNECTED) break;

            size_t offset = 0;
            header.Deserialize(headerBuffer, offset);

            if (header.size > MAX_PACKAGE_SIZE) {
                throw std::runtime_error("PrimaryConnection receive package size too large");
            }

            const std::unique_ptr<Package<InitialConnectionPackageType>> package = std::make_unique<Package<InitialConnectionPackageType>>(header);
            asio::mutable_buffer packageBuffer(package->GetRawBody(), header.size);

            co_await asio::async_read(m_socket, packageBuffer, asio::use_awaitable);
            if (m_connectionState != ConnectionState::CONNECTED) break;

            if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::DEVICE_DATA_FC)) {
                package->GetValue(data);
                data.deviceInfo.deviceAddress = m_socket.remote_endpoint().address().to_string();

                // ReSharper disable once CppPassValueParameterByConstReference
                std::unique_ptr<QEvent> event = std::make_unique<ConnectionPendingEvent>(data.deviceInfo, data.initialConnectionMode, [ref = shared_from_this(), data, this](const bool actionResult, std::string challenge) {
                    asio::co_spawn(m_strand, CoProcessConnectionPendingCallback(actionResult, data, std::move(challenge)), asio::detached);
                });

                ConnectionManager::SendEvent(event);

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::DEVICE_DATA_FS)) {
                auto [deviceInfo, initialConnectionMode] = package->GetValue<InitialConnectionData>();
                deviceInfo.deviceAddress = m_socket.remote_endpoint().address().to_string();

                TCPEndpoint endpoint(asio::ip::make_address_v4(deviceInfo.deviceAddress), deviceInfo.deviceAddressPort);
                ConnectionManager::ConnectPrimary(data);

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::CHALLENGE_RESPONSE)) {
                if (m_challengeLeftTries <= 0) {
                    continue;
                }

                const std::string response = package->GetValue<std::string>();
                if (response != m_challengeResult) {
                    m_challengeLeftTries--;

                    InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::CHALLENGE_WRONG_ANSWER, m_challengeLeftTries);

                    m_packagesOut.emplace_back(std::move(out));
                    m_sendFlag.Signal();
                    continue;
                }

                // ReSharper disable once CppPassValueParameterByConstReference
                ConnectionManager::SeekPrimary(data, [this, initialConnectionData = std::move(data)](const TCPEndpoint endpoint) mutable {
                    initialConnectionData.deviceInfo.deviceAddress = endpoint.address().to_string();
                    initialConnectionData.deviceInfo.deviceAddressPort = endpoint.port();

                    asio::co_spawn(m_strand, CoPrimaryConnectionCallback(initialConnectionData), asio::detached);
                });

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::CHALLENGE_WRONG_ANSWER)) {
                int32_t leftTries = package->GetValue<int32_t>();

                std::unique_ptr<QEvent> event = std::make_unique<ConnectionFailedVerificationEvent>(leftTries);
                ConnectionManager::SendEvent(event);

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::CHALLENGE_ANSWER_REQUEST)) {
                std::unique_ptr<ConnectionVerificationEvent> event = std::make_unique<ConnectionVerificationEvent>([this](std::string response) {
                    asio::co_spawn(m_strand, CoProcessConnectionVerificationEvent(std::move(response)), asio::detached);
                });

            }
        }


    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoProcessConnectionVerificationEvent(std::string&& response) {
    InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::CHALLENGE_RESPONSE, std::move(response));

    m_packagesOut.emplace_back(std::move(out));
    m_sendFlag.Signal();

    co_return;
}

asio::awaitable<void> InitialConnection::CoProcessConnectionPendingCallback(const bool actionResult, InitialConnectionData data, std::string&& challenge){
    if (!actionResult) {
        Disconnect();
        co_return;
    }

    m_challengeLeftTries = MAX_NUMBER_OF_VERIFICATION_TRIES;
    m_challengeResult = std::move(challenge);

    if (!m_challengeResult.empty()) {
        InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::CHALLENGE_ANSWER_REQUEST);

        m_packagesOut.emplace_back(std::move(out));
        m_sendFlag.Signal();
        co_return;
    }

    // ReSharper disable once CppPassValueParameterByConstReference
    ConnectionManager::SeekPrimary(data, [this, initialConnectionData = std::move(data)](const TCPEndpoint endpoint) mutable {
        initialConnectionData.deviceInfo.deviceAddress = endpoint.address().to_string();
        initialConnectionData.deviceInfo.deviceAddressPort = endpoint.port();

        asio::co_spawn(m_strand, CoPrimaryConnectionCallback(initialConnectionData), asio::detached);
    });
}

asio::awaitable<void> InitialConnection::CoPrimaryConnectionCallback(const InitialConnectionData data) {
    InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::DEVICE_DATA_FS, data);

    m_packagesOut.emplace_back(std::move(out));
    m_sendFlag.Signal();

    co_return;
}
