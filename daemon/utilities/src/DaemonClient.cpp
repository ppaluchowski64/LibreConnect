#include <DaemonClient.h>
#include <ThreadPool.h>
#include <Events.h>
#include <ConnectionManager.h>

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
    client->m_connected.store(false, std::memory_order_release);
    try { client->m_socket.close(); } catch (...) {}
}

bool DaemonClient::IsConnected() const {
    return m_connected.load(std::memory_order_acquire);
}

void DaemonClient::ConnectedSignal(const uuid uuid) {
    if (!IsConnected()) {
        return;
    }

    Send(DaemonPackage::CONNECTED, uuid, GetPid());
}

asio::awaitable<bool> DaemonClient::RequestConnectedWindow(const uuid uuid) {
    if (!IsConnected()) {
        co_return false;
    }

    const auto flag = std::make_shared<AwaitableFlag>(m_socket.get_executor());
    m_windowRequestFlags.InsertOrAssign(uuid, flag);
    
    Send(DaemonPackage::REQUEST_CONNECTED_WINDOW, uuid);

    const auto result = co_await flag->WaitFor(std::chrono::seconds(1));

    m_windowRequestFlags.Erase(uuid);
    if (result == AwaitableFlag::Result::TIMEOUT) {
        co_return false;
    }

    co_return m_windowRequestResults.Pop(uuid).value_or(false);
}

DaemonClient::DaemonClient() : m_socket(ThreadPool::GetContext()) {}

asio::awaitable<void> DaemonClient::CoConnect() {
    try {
        co_await m_socket.async_connect(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), DAEMON_SIGNAL_PORT), asio::use_awaitable);
        m_connected.store(true, std::memory_order_release);
        co_await CoReceive();
    } catch (...) {
        m_connected.store(false, std::memory_order_release);
    }

    m_connected.store(false, std::memory_order_release);
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

            const DaemonPackage type = static_cast<DaemonPackage>(header.type);
            if (type == DaemonPackage::REQUEST_CONNECTED_WINDOW_RESPONSE) {
                uuid id{};
                bool result{false};
                package->GetValue(id);
                package->GetValue(result);

                if (auto flag = m_windowRequestFlags.Get(id)) {
                    m_windowRequestResults.InsertOrAssign(id, result);
                    flag.value()->Signal();
                }
            } else if (type == DaemonPackage::SHOW_WINDOW_REQUEST) {
                ConnectionManager::SendEvent(std::make_unique<ShowWindowEvent>());
            }
        }
    } catch (...) {}
}
