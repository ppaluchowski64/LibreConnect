#include <InitialConnection.h>
#include <asio/buffer.hpp>
#include <Events.h>
#include <ConnectionManager.h>

InitialConnection::InitialConnection(IOContext& context) : m_context(context), m_strand(asio::make_strand(context)),
                                                           m_sendFlag(context.get_executor()), m_socket(context) {
}

std::shared_ptr<InitialConnection> InitialConnection::Create(IOContext& context) {
    return std::make_shared<InitialConnection>(context);
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

asio::awaitable<void> InitialConnection::CoConnect(TCPEndpoint endpoint, const InitialConnectionMode mode) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();
    m_connectionState = ConnectionState::CONNECTING;

    try {
        m_socket = TCPSocket(m_context, endpoint.protocol());

        co_await asio::async_connect(m_socket, std::initializer_list<TCPEndpoint>({endpoint}), asio::use_awaitable);
        Debug::Log("Accepted TCP initial connection to {}:{}",  m_socket.remote_endpoint().address().to_string(), m_socket.remote_endpoint().port());

        m_connectionState = ConnectionState::CONNECTED;

        TCPEndpoint anyEndpoint = TCPEndpoint(asio::ip::tcp::v4(), 0);

        // ReSharper disable once CppPassValueParameterByConstReference
        // ReSharper disable once CppDeclarationHidesUncapturedLocal
        ConnectionManager::SeekPrimary(std::move(anyEndpoint), [this, mode](const TCPEndpoint endpoint) {
            asio::co_spawn(m_strand, CoSend(), asio::detached);
            asio::co_spawn(m_strand, CoReceive(), asio::detached);

            InitialConnectionData data;

            data.deviceInfo = DeviceInfo::GetThisDeviceInfo();
            data.deviceInfo.deviceAddressPort = endpoint.port();
            data.initialConnectionMode = mode;

            std::unique_ptr<Package<InitialConnectionPackageType>> package = Package<InitialConnectionPackageType>::CreateUnique(InitialConnectionPackageType::CONNECT_INFO, data);
            m_packagesOut.emplace_back(std::move(package));
            m_sendFlag.Signal();
        });

    } catch (std::system_error& error) {
        if (!(error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe)) {
            Debug::LogError("InitialConnection connection error: {}", error.what());
        }

        Disconnect();
    }

    co_return;
}

asio::awaitable<void> InitialConnection::CoSeek(TCPEndpoint endpoint, std::function<void(TCPEndpoint endpoint)> callback) {
    const std::shared_ptr<InitialConnection> self = shared_from_this();
    m_connectionState = ConnectionState::CONNECTING;

    try {
        m_socket = TCPSocket(m_context, endpoint.protocol());
        TCPAcceptor acceptor(m_context, endpoint);
        acceptor.set_option(asio::socket_base::reuse_address(true));

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
        if (!(error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe)) {
            Debug::LogError("InitialConnection seek error: {}", error.what());
        }

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
         if (!(error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe)) {
             Debug::LogError("InitialConnection disconnect error: {}", error.what());
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
        if (!(error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe)) {
            Debug::LogError("InitialConnection send error: {}", error.what());
        }

        Disconnect();
    }
}

asio::awaitable<void> InitialConnection::CoReceive() {
    try {
        const std::shared_ptr<InitialConnection> self = shared_from_this();
        std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
        PackageHeader header{};

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

            switch (header.type) {
            case static_cast<uint16_t>(InitialConnectionPackageType::CONNECT_INFO):
                InitialConnectionData data = package->GetValue<InitialConnectionData>();
                data.deviceInfo.deviceAddress = m_socket.local_endpoint().address().to_string();

                if (data.initialConnectionMode == InitialConnectionMode::PAIR_AND_CONNECT) {


                } else if (data.initialConnectionMode == InitialConnectionMode::CONNECT_WITH_PAIR) {

                } else {

                }

                break;

            }
        }


    } catch (std::system_error& error) {
        if (!(error.code() == asio::error::eof || error.code() == asio::error::connection_reset || error.code() == asio::error::operation_aborted || error.code() == asio::error::connection_aborted || error.code() == asio::error::broken_pipe)) {
            Debug::LogError("InitialConnection receive error: {}", error.what());
        }

        Disconnect();
    }
}