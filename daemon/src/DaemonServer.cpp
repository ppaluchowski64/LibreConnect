#include <DaemonServer.h>
#include <ThreadPool.h>
#include <DaemonCommon.h>
#include <Package.h>
#include <Packable.h>
#include <DebugLog.h>
#include <boost/uuid/uuid_io.hpp>

DaemonServer::DaemonServer() {}

void DaemonServer::Start() {
    Debug::Log("DaemonServer: Starting...");
    auto instance = shared_from_this();
    asio::co_spawn(ThreadPool::GetContext(), [instance]() -> asio::awaitable<void> { co_await instance->SeekConnections(); }, asio::detached);
}

std::vector<uuid> DaemonServer::GetConnectedDevices() {
    std::lock_guard<std::mutex> lock(m_clientsMutex);

    std::vector<uuid> devices;
    devices.reserve(m_clients.size());

    for (const auto& client : m_clients) {
        devices.emplace_back(client->m_uuid);
    }

    return devices;
}

asio::awaitable<void> DaemonServer::SeekConnections() {
    const auto instance = shared_from_this();

    try {
        Debug::Log("DaemonServer: Listening on 127.0.0.1:{}", DAEMON_SIGNAL_PORT);
        TCPAcceptor acceptor(ThreadPool::GetContext(), TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), DAEMON_SIGNAL_PORT));

        while (true) {
            auto clientData = std::make_shared<ClientData>(
                TCPSocket(ThreadPool::GetContext()),
                uuid{}
            );

            co_await acceptor.async_accept(clientData->m_socket, asio::use_awaitable);
            Debug::Log("DaemonServer: New client accepted");

            asio::co_spawn(ThreadPool::GetContext(), [instance, clientData]() -> asio::awaitable<void> { co_await instance->ProcessClient(clientData); }, asio::detached);

            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                m_clients.emplace_back(clientData);
            }
        }
    } catch (const std::system_error& error) {
        Debug::LogError("DaemonServer: SeekConnections error: {}", error.what());
    } catch (...) {
        Debug::LogError("DaemonServer: SeekConnections unknown error");
    }
}

asio::awaitable<void> DaemonServer::ProcessClient(std::shared_ptr<ClientData> client) {
    Debug::Log("DaemonServer: Processing client...");
    try {
        std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
        PackageHeader header{};

        while (true) {
            co_await asio::async_read(client->m_socket, asio::buffer(headerBuffer), asio::use_awaitable);

            size_t offset = 0;
            header.Deserialize(headerBuffer, offset);

            if (header.size > MAX_NON_FILE_PACKAGE_SIZE) {
                Debug::LogWarning("DaemonServer: Received package too large: {}", header.size);
                break;
            }

            const auto package = std::make_unique<Package<DaemonPackage>>(header);
            co_await asio::async_read(client->m_socket, asio::buffer(package->GetRawBody(), header.size), asio::use_awaitable);

            const DaemonPackage type = static_cast<DaemonPackage>(header.type);

            switch (type) {
            case DaemonPackage::CONNECTED:
                package->GetValue(client->m_uuid);
                package->GetValue(client->m_pid);
                Debug::Log("DaemonServer: Client connected (UUID: {}, PID: {})", boost::uuids::to_string(client->m_uuid), client->m_pid);
                break;
            case DaemonPackage::REQUEST_CONNECTED_WINDOW:
            {
                uuid id = package->GetValue<uuid>();
                Debug::Log("DaemonServer: Request connected window for UUID: {}", boost::uuids::to_string(id));
                bool found{false};

                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);

                    if (id != uuid{}) {
                        for (const auto& cl : m_clients) {
                            if (cl != client && cl->m_uuid == id) {
                                Send(client, DaemonPackage::REQUEST_CONNECTED_WINDOW_RESPONSE, id, true);
                                Send(cl, DaemonPackage::SHOW_WINDOW_REQUEST);
                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found) {
                    Send(client, DaemonPackage::REQUEST_CONNECTED_WINDOW_RESPONSE, id, false);
                }

                break;
            }
            default:
                Debug::LogWarning("DaemonServer: Received unknown package type: {}", static_cast<int>(type));
                break;
            }
        }
    } catch (const std::system_error& error) {
        if (error.code() != asio::error::operation_aborted && error.code() != asio::error::eof) {
            Debug::Log("DaemonServer: ProcessClient error: {}", error.what());
        }
    } catch (...) { }

    Debug::Log("DaemonServer: Client disconnected");
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        std::erase_if(m_clients, [&client](const auto& c) { return c == client; });
    }
}
