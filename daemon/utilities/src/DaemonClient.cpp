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

bool DaemonClient::HasFinishedConnectAttempt() const {
    return m_connectAttemptFinished.load(std::memory_order_acquire);
}

asio::awaitable<bool> DaemonClient::WaitForConnectResult(const std::chrono::milliseconds timeout) {
    if (IsConnected()) {
        co_return true;
    }

    if (HasFinishedConnectAttempt()) {
        co_return false;
    }

    const auto result = co_await m_connectedFlag.WaitFor(timeout);
    if (result != AwaitableFlag::Result::SUCCESS) {
        co_return IsConnected();
    }

    co_return IsConnected();
}

void DaemonClient::ConnectedSignal(const uuid uuid) {
    if (!IsConnected()) {
        const std::shared_ptr<DaemonClient> self = shared_from_this();
        asio::co_spawn(ThreadPool::GetContext(), [self, uuid]() -> asio::awaitable<void> {
            const bool connected = co_await self->WaitForConnectResult(std::chrono::milliseconds(250));
            if (connected) {
                self->Send(DaemonPackage::CONNECTED, uuid, GetPid());
            }
            co_return;
        }, asio::detached);
        return;
    }

    Send(DaemonPackage::CONNECTED, uuid, GetPid());
}

asio::awaitable<bool> DaemonClient::RequestConnectedWindow(const uuid uuid) {
    if (!IsConnected()) {
        const bool connected = co_await WaitForConnectResult(std::chrono::milliseconds(250));
        if (!connected) {
            co_return false;
        }
    }

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

DaemonClient::DaemonClient()
    : m_socket(ThreadPool::GetContext()),
      m_connectedFlag(m_socket.get_executor()) {}

asio::awaitable<void> DaemonClient::CoConnect() {
    try {
        co_await m_socket.async_connect(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), DAEMON_SIGNAL_PORT), asio::use_awaitable);
        m_connected.store(true, std::memory_order_release);
        m_connectAttemptFinished.store(true, std::memory_order_release);
        m_connectedFlag.Signal();
        co_await CoReceive();
    } catch (...) {
        m_connected.store(false, std::memory_order_release);
        m_connectAttemptFinished.store(true, std::memory_order_release);
        m_connectedFlag.Signal();
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
