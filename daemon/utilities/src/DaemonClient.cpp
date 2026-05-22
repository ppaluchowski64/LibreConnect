#include <DaemonClient.h>
#include <ThreadPool.h>

static auto GetPid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

std::shared_ptr<DaemonClient> DaemonClient::Create() {
    auto instance = std::make_shared<DaemonClient>();
    asio::co_spawn(ThreadPool::GetContext(), [instance]() -> asio::awaitable<void> { co_await instance->CoConnect(); }, asio::detached);
    return instance;
}

void DaemonClient::Destroy(const std::shared_ptr<DaemonClient>& client) {
    try { client->m_socket.close(); } catch (...) {}
}

void DaemonClient::ConnectedSignal(const uuid uuid) {
    Send(DaemonPackage::CONNECTED, uuid, GetPid());
}

void DaemonClient::RequestConnectedWindow(const uuid uuid) {
    Send(DaemonPackage::REQUEST_CONNECTED_WINDOW, uuid);
}

DaemonClient::DaemonClient() : m_socket(ThreadPool::GetContext()) {}

asio::awaitable<void> DaemonClient::CoConnect() {
    try {
        co_await m_socket.async_connect(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), DAEMON_SIGNAL_PORT), asio::use_awaitable);
        co_await CoReceive();
    } catch (...) {}
}

asio::awaitable<void> DaemonClient::CoReceive() {
    try {
        std::vector<uint8_t> headerBuffer(PackageHeader::GetSerializedSize());
        PackageHeader header{};

        while (true) {
            co_await asio::async_read(m_socket, asio::buffer(headerBuffer), asio::use_awaitable);

            size_t offset = 0;
            header.Deserialize(headerBuffer, offset);

            if (header.size > MAX_NON_FILE_PACKAGE_SIZE) {
                break;
            }

            const auto package = std::make_unique<Package<DaemonPackage>>(header);
            co_await asio::async_read(m_socket, asio::buffer(package->GetRawBody(), header.size), asio::use_awaitable);
        }
    } catch (...) {}
}