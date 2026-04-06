#include <InitialConnection.h>
#include <asio/buffer.hpp>
#include <Events.h>
#include <ConnectionManager.h>
#include <ThreadPool.h>
#include <magic_enum/magic_enum.hpp>
#include <openssl/sha.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

typedef std::unique_ptr<Package<InitialConnectionPackageType>> InitialConnectionPackagePtr;

std::string InitialConnection::ComputePairingCode(const std::string& localFingerprint, const std::string& remoteFingerprint) {
    if (localFingerprint.empty() || remoteFingerprint.empty()) {
        return {};
    }

    std::string first = localFingerprint;
    std::string second = remoteFingerprint;
    if (second < first) {
        std::swap(first, second);
    }

    const std::string material = first + "|" + second;
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    SHA256(
        reinterpret_cast<const unsigned char*>(material.data()),
        material.size(),
        digest
    );

    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(value); ++i) {
        value = (value << 8) | static_cast<uint64_t>(digest[i]);
    }

    value %= 1000000000000ULL;

    std::ostringstream oss;
    oss << std::setw(12) << std::setfill('0') << value;
    return oss.str();
}

InitialConnection::InitialConnection() : m_context(ThreadPool::GetContext()), m_strand(asio::make_strand(m_context)),
                                         m_sendFlag(m_context.get_executor()), m_socket(m_context), m_challengeLeftTries(0) { }

std::shared_ptr<InitialConnection> InitialConnection::Create() {
    return std::make_shared<InitialConnection>();
}

void InitialConnection::Connect(TCPEndpoint&& endpoint, const InitialConnectionMode mode) {
    Debug::Log("InitialConnection: Initiating Connect to {}:{}", endpoint.address().to_string(), endpoint.port());
    asio::co_spawn(m_strand, CoConnect(std::move(endpoint), mode), asio::detached);
}

void InitialConnection::Seek(TCPEndpoint&& endpoint, std::function<void(TCPEndpoint endpoint)>&& callback) {
    Debug::Log("InitialConnection: Initiating Seek on {}:{}", endpoint.address().to_string(), endpoint.port());
    asio::co_spawn(m_strand, CoSeek(std::move(endpoint), std::move(callback)), asio::detached);
}

void InitialConnection::Disconnect(const bool cancelSeeking) {
    Debug::Log("InitialConnection: Disconnect requested (cancelSeeking: {})", cancelSeeking);
    asio::co_spawn(m_strand, CoDisconnect(cancelSeeking), asio::detached);
}

void InitialConnection::TemporaryOwnership(const std::shared_ptr<InitialConnection>& ptr) {
    m_temporaryOwnership = ptr;
}

asio::awaitable<void> InitialConnection::CoConnect(TCPEndpoint endpoint, const InitialConnectionMode mode) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();
    m_connectionState = ConnectionState::CONNECTING;
    m_localCertificateFingerprint = DeviceInfo::GetThisDeviceInfo().certificateFingerprint;
    m_expectedChallengeCode.clear();

    try {
        m_socket = TCPSocket(m_context, endpoint.protocol());

        co_await asio::async_connect(m_socket, std::initializer_list<TCPEndpoint>({endpoint}), asio::use_awaitable);
        Debug::Log("InitialConnection: Successfully connected to {}:{}",  m_socket.remote_endpoint().address().to_string(), m_socket.remote_endpoint().port());

        m_connectionState = ConnectionState::CONNECTED;

        asio::co_spawn(m_strand, CoSend(), asio::detached);
        asio::co_spawn(m_strand, CoReceive(), asio::detached);

        InitialConnectionData data;
        data.deviceInfo = DeviceInfo::GetThisDeviceInfo();
        data.initialConnectionMode = mode;

        Debug::Log("InitialConnection: Sending DEVICE_DATA_FC (Client Identity)");
        InitialConnectionPackagePtr package = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::DEVICE_DATA_FC, data);
        m_packagesOut.emplace_back(std::move(package));
        m_sendFlag.Signal();

    } catch (std::system_error& error) {
        Debug::Log("InitialConnection: CoConnect Error - {}", error.what());
        HandleAsioError(error.code());
        Disconnect();
    }

    co_return;
}

asio::awaitable<void> InitialConnection::CoSeek(TCPEndpoint endpoint, std::function<void(TCPEndpoint endpoint)> callback) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();
    m_temporaryOwnership.reset();

    m_connectionState = ConnectionState::CONNECTING;
    m_localCertificateFingerprint = DeviceInfo::GetThisDeviceInfo().certificateFingerprint;
    m_expectedChallengeCode.clear();

    try {
        m_socket = TCPSocket(m_context);
        TCPAcceptor acceptor(m_context);
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        Debug::Log("InitialConnection: Listening for connections on {}:{}", endpoint.address().to_string(), endpoint.port());
        ConnectionManager::SetSeekingEndpoint(acceptor.local_endpoint());

        co_await acceptor.async_accept(m_socket, asio::use_awaitable);
        Debug::Log("InitialConnection: Accepted connection from {}:{}",  m_socket.remote_endpoint().address().to_string(), m_socket.remote_endpoint().port());

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
        Debug::Log("InitialConnection: CoSeek Error - {}", error.what());
        HandleAsioError(error.code());
        Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoDisconnect(const bool cancelSeeking) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();

    if (m_connectionState == ConnectionState::DISCONNECTED || m_connectionState == ConnectionState::DISCONNECTING) {
        co_return;
    }

    Debug::Log("InitialConnection: Closing socket and cleaning up...");
    m_connectionState = ConnectionState::DISCONNECTING;

    if (m_socket.is_open()) {
        std::error_code ec;
        m_socket.cancel(ec);
        if (ec && ec != asio::error::not_connected) {
            HandleAsioError(ec);
        }

        ec.clear();
        m_socket.shutdown(asio::socket_base::shutdown_both, ec);
        if (ec && ec != asio::error::not_connected) {
            HandleAsioError(ec);
        }

        ec.clear();
        m_socket.close(ec);
        if (ec && ec != asio::error::not_connected) {
            HandleAsioError(ec);
        }
    }

    m_connectionState = ConnectionState::DISCONNECTED;

    if (cancelSeeking) {
        ConnectionManager::Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoSend() {
    try {
        const std::shared_ptr<InitialConnection> self = shared_from_this();

        while (m_connectionState == ConnectionState::CONNECTED) {
            if (m_packagesOut.empty()) {
                co_await m_sendFlag.Wait();
                m_sendFlag.Reset();
            }

            if (m_connectionState != ConnectionState::CONNECTED) break;

            while (!m_packagesOut.empty()) {
                const std::unique_ptr<Package<InitialConnectionPackageType>> package = std::move(m_packagesOut.front());
                m_packagesOut.pop_front();

                PackageHeader& header = package->GetHeader();

                Debug::Log("InitialConnection: [OUT] Type: {} Size: {} bytes", magic_enum::enum_name(static_cast<InitialConnectionPackageType>(header.type)), header.size);

                std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
                size_t offset = 0;
                header.Serialize(headerBuffer, offset);

                std::vector<asio::const_buffer> constBuffers {
                    asio::const_buffer(headerBuffer.data(), headerBuffer.size()),
                    asio::const_buffer(package->GetRawBody(), header.size)
                };

                co_await asio::async_write(m_socket, constBuffers, asio::use_awaitable);
            }
        }

    } catch (std::system_error& error) {
        Debug::Log("InitialConnection: CoSend Error - {}", error.what());
        HandleAsioError(error.code());
        Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoReceive() {
    try {
        const std::shared_ptr<InitialConnection> self = shared_from_this();
        std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
        PackageHeader header{};

        InitialConnectionData data{};

        while (m_connectionState == ConnectionState::CONNECTED) {
            asio::mutable_buffer headerMutableBuffer(headerBuffer.data(), headerBuffer.size());
            co_await asio::async_read(m_socket, headerMutableBuffer, asio::use_awaitable);

            size_t offset = 0;
            header.Deserialize(headerBuffer, offset);

            Debug::Log("InitialConnection: [IN] Type: {} Size: {} bytes", header.type, header.size);

            if (header.size > MAX_PACKAGE_SIZE) {
                Debug::Log("InitialConnection: Received package size ({}) exceeds limit!", header.size);
                throw std::runtime_error("InitialConnection receive package size too large");
            }

            const std::unique_ptr<Package<InitialConnectionPackageType>> package = std::make_unique<Package<InitialConnectionPackageType>>(header);
            asio::mutable_buffer packageBuffer(package->GetRawBody(), header.size);

            co_await asio::async_read(m_socket, packageBuffer, asio::use_awaitable);
            if (m_connectionState != ConnectionState::CONNECTED) break;

            if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::DEVICE_DATA_FC)) {
                package->GetValue(data);
                data.deviceInfo.deviceAddress = m_socket.remote_endpoint().address().to_string();

                Debug::Log("InitialConnection: Handshake step 1 - Received DEVICE_DATA_FC from {}", data.deviceInfo.deviceName);

                if (data.initialConnectionMode != InitialConnectionMode::CONNECT_WITH_PAIR) {
                    std::string pairingCode{};
                    if (data.initialConnectionMode == InitialConnectionMode::PAIR_AND_CONNECT) {
                        pairingCode = ComputePairingCode(
                            m_localCertificateFingerprint,
                            data.deviceInfo.certificateFingerprint
                        );
                    }

                    std::unique_ptr<QEvent> event = std::make_unique<ConnectionPendingEvent>(data.deviceInfo, data.initialConnectionMode, std::move(pairingCode), [ref = shared_from_this(), data](const bool actionResult, std::string challenge) {
                        try {
                            asio::co_spawn(ref->m_strand, ref->CoProcessConnectionPendingCallback(actionResult, data, std::move(challenge)), asio::detached);
                        } catch (const std::exception& ex) {
                            Debug::LogError("InitialConnection: Failed to spawn pending callback coroutine - {}", ex.what());
                        }
                    });

                    ConnectionManager::SendEvent(event);
                } else {
                    asio::co_spawn(m_strand, CoProcessConnectionPendingCallback(true, data, ""), asio::detached);
                }

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::DEVICE_DATA_FS)) {
                // Client side receiving final confirmation
                data = package->GetValue<InitialConnectionData>();
                data.deviceInfo.deviceAddress = m_socket.remote_endpoint().address().to_string();

                Debug::Log("InitialConnection: Handshake step 2 - Received DEVICE_DATA_FS ({}:{}). Transitioning to Primary.", data.deviceInfo.deviceAddress, data.deviceInfo.deviceAddressPort);
                ConnectionManager::ConnectPrimary(data);

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::CHALLENGE_RESPONSE)) {
                const std::string response = package->GetValue<std::string>();
                Debug::Log("InitialConnection: Received Challenge Response. Tries left: {}", m_challengeLeftTries);

                if (response != m_challengeResult) {
                    m_challengeLeftTries--;
                    Debug::Log("InitialConnection: Challenge Mismatch! Tries remaining: {}", m_challengeLeftTries);

                    InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::CHALLENGE_WRONG_ANSWER, m_challengeLeftTries);
                    m_packagesOut.emplace_back(std::move(out));
                    m_sendFlag.Signal();
                    continue;
                }

                Debug::Log("InitialConnection: Challenge Verified. Seeking Primary...");
                ConnectionManager::SeekPrimary(data, [ref = shared_from_this(), initialConnectionData = data](const TCPEndpoint endpoint) mutable {
                    initialConnectionData.deviceInfo = DeviceInfo::GetThisDeviceInfo();
                    initialConnectionData.deviceInfo.deviceAddress = endpoint.address().to_string();
                    initialConnectionData.deviceInfo.deviceAddressPort = endpoint.port();
                    asio::co_spawn(ref->m_strand, ref->CoPrimaryConnectionCallback(initialConnectionData), asio::detached);
                });

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::CHALLENGE_WRONG_ANSWER)) {
                int32_t leftTries = package->GetValue<int32_t>();
                Debug::Log("InitialConnection: Server reported wrong challenge answer. Tries left: {}", leftTries);

                std::unique_ptr<QEvent> event = std::make_unique<ConnectionFailedVerificationEvent>(leftTries);
                ConnectionManager::SendEvent(event);

            } else if (header.type == static_cast<uint16_t>(InitialConnectionPackageType::CHALLENGE_ANSWER_REQUEST)) {
                Debug::Log("InitialConnection: Received Challenge Request. Spawning Verification UI Event.");
                std::string remoteFingerprint;
                if (header.size > 0) {
                    remoteFingerprint = package->GetValue<std::string>();
                }

                m_expectedChallengeCode = ComputePairingCode(m_localCertificateFingerprint, remoteFingerprint);
                std::unique_ptr<ConnectionVerificationEvent> event = std::make_unique<ConnectionVerificationEvent>([ref = shared_from_this()](std::string response) {
                    try {
                        asio::co_spawn(ref->m_strand, ref->CoProcessConnectionVerificationEvent(std::move(response)), asio::detached);
                    } catch (const std::exception& ex) {
                        Debug::LogError("InitialConnection: Failed to spawn verification callback coroutine - {}", ex.what());
                    }
                });

                ConnectionManager::SendEvent(std::move(event));
            }
        }

    } catch (std::system_error& error) {
        Debug::Log("InitialConnection: CoReceive Error - {}", error.what());
        HandleAsioError(error.code());
        Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoProcessConnectionVerificationEvent(std::string response) {
    if (!m_expectedChallengeCode.empty() && response != m_expectedChallengeCode) {
        Debug::LogWarning("InitialConnection: Local pairing-code verification failed; rejecting verification response");
        response.clear();
    }

    Debug::Log("InitialConnection: User provided challenge response. Sending back to server.");
    InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::CHALLENGE_RESPONSE, std::move(response));

    m_packagesOut.emplace_back(std::move(out));
    m_sendFlag.Signal();
    m_expectedChallengeCode.clear();

    co_return;
}

asio::awaitable<void> InitialConnection::CoProcessConnectionPendingCallback(const bool actionResult, InitialConnectionData data, std::string challenge){
    if (!actionResult) {
        Debug::Log("InitialConnection: Connection rejected by user/manager callback.");
        Disconnect();
        co_return;
    }

    m_challengeLeftTries = MAX_NUMBER_OF_VERIFICATION_TRIES;
    m_challengeResult = std::move(challenge);

    if (data.initialConnectionMode == InitialConnectionMode::PAIR_AND_CONNECT) {
        const std::string expectedCode = ComputePairingCode(
            m_localCertificateFingerprint,
            data.deviceInfo.certificateFingerprint
        );

        if (expectedCode.empty()) {
            Debug::LogError("InitialConnection: Failed to derive pairing code from certificate fingerprints");
            Disconnect();
            co_return;
        }

        if (m_challengeResult.empty()) {
            m_challengeResult = expectedCode;
        }

        if (m_challengeResult != expectedCode) {
            Debug::LogWarning("InitialConnection: UI challenge did not match expected pairing code; using expected value");
            m_challengeResult = expectedCode;
        }
    }

    if (!m_challengeResult.empty()) {
        Debug::Log("InitialConnection: Sending CHALLENGE_ANSWER_REQUEST to Client.");
        InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(
            InitialConnectionPackageType::CHALLENGE_ANSWER_REQUEST,
            m_localCertificateFingerprint
        );

        m_packagesOut.emplace_back(std::move(out));
        m_sendFlag.Signal();
        co_return;
    }

    Debug::Log("InitialConnection: No challenge required. Moving to Primary Seek.");

    responseData.deviceInfo = DeviceInfo::GetThisDeviceInfo();

    ConnectionManager::SeekPrimary(data, [ref = shared_from_this(), data](const TCPEndpoint endpoint) mutable {
        responseData.deviceInfo.deviceAddress = endpoint.address().to_string();
        responseData.deviceInfo.deviceAddressPort = endpoint.port();

        asio::co_spawn(ref->m_strand, ref->CoPrimaryConnectionCallback(data), asio::detached);
    });
}

asio::awaitable<void> InitialConnection::CoPrimaryConnectionCallback(const InitialConnectionData data) {
    Debug::Log("InitialConnection: Primary listener ready. Sending final DEVICE_DATA_FS.");
    InitialConnectionPackagePtr out = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::DEVICE_DATA_FS, data);

    m_packagesOut.emplace_back(std::move(out));
    m_sendFlag.Signal();

    co_return;
}
