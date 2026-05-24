#include <TransferChannelPool.h>
#include <ThreadPool.h>

#include <chrono>
#include <ConnectionManager.h>

namespace {
constexpr size_t BORROW_WAIT_POLL_MS = 10;
constexpr size_t CHANNEL_CONNECT_POLL_MS = 10;
constexpr size_t CONNECT_REQUEST_RETRY_MS = 1000;
constexpr auto CHANNELS_CONNECT_TIMEOUT = std::chrono::seconds(15);
constexpr auto CONNECT_REQUEST_TIMEOUT = std::chrono::seconds(15);
}

TransferChannelPool* TransferChannelPool::s_instance{};
std::atomic<ConnectionState> TransferChannelPool::s_connectionState{ConnectionState::DISCONNECTED};

TransferChannelPool::TransferChannelPool(const size_t count) : m_count(count) {
    Debug::Log("TransferChannelPool: Constructing pool with {} channels", count);
    InitializeChannels();
}

void TransferChannelPool::Initialize(const size_t count) {
    if (s_instance) {
        Debug::LogError("TransferChannelPool::Initialize: instance already initialized");
        return;
    }

    Debug::Log("TransferChannelPool: Initializing with {} channels", count);
    s_instance = new TransferChannelPool(count);

    ConnectionManager::AddResponseHandler(PC_PackageType::TRANSFER_CHANNEL_POOL_CONNECT, [](PC_Package&& package) {
#ifdef MOBILE_DEVICE
        Debug::Log("TransferChannelPool: Received peer connect request");
        asio::co_spawn(ThreadPool::GetContext(), Seek(), asio::detached);
#else
        Debug::LogWarning("TransferChannelPool: Ignoring peer connect request on desktop");
#endif
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::TRANSFER_CHANNEL_POOL_PORT_INFO, [](PC_Package&& package) {
        if (!s_instance) {
            Debug::LogError("TransferChannelPool: Received port info before initialization");
            return;
        }

        const uint32_t index = package->GetValue<uint32_t>();
        const uint16_t port = package->GetValue<uint16_t>();
        Debug::Log("TransferChannelPool: Received port {} for channel {}", port, index);

        if (index >= s_instance->m_count) {
            Debug::LogWarning(
                "TransferChannelPool: Ignoring extra port info for channel {} (pool size: {})",
                index,
                s_instance->m_count
            );
            return;
        }

        const auto opt = Get(index);
        if (!opt.has_value() || !opt.value()) {
            Debug::LogError("TransferChannelPool: Channel {} unavailable for connect", index);
            return;
        }

        const auto& channel = opt.value();

        asio::co_spawn(ThreadPool::GetContext(), [channel, port]() -> asio::awaitable<void> {
            co_await channel->Connect(TCPEndpoint(ConnectionManager::GetPeerAddress(), port));
        }, asio::detached);

#ifdef DESKTOP_DEVICE
        if (!s_instance->m_waitingForConnections.exchange(true)) {
            Debug::Log("TransferChannelPool: Starting wait for all channels to connect");
            asio::co_spawn(ThreadPool::GetContext(), []() -> asio::awaitable<void> {
                co_await WaitForAllChannelsConnected();
            }, asio::detached);
        }
#endif
    });
}

asio::awaitable<void> TransferChannelPool::Connect() {
#ifndef DESKTOP_DEVICE
    Debug::Log("TransferChannelPool: Mobile role waiting for peer connect request");
    co_return;
#else
    if (!s_instance) {
        Debug::LogError("TransferChannelPool::Connect: instance not initialized");
        co_return;
    }

    {
        auto& connectionMutex = s_instance->m_connectionMutex;
        std::lock_guard<std::mutex> lock(connectionMutex);

        if (s_connectionState.load() != ConnectionState::DISCONNECTED) {
            Debug::LogWarning(
                "TransferChannelPool: Connect skipped because state is {}",
                static_cast<int>(s_connectionState.load())
            );
            co_return;
        }
    }

    Debug::Log("TransferChannelPool: Starting outbound connect flow");
    s_connectionState.store(ConnectionState::CONNECTING);

    IOContext& context = ThreadPool::GetContext();
    asio::steady_timer timer(context);
    const auto deadline = std::chrono::steady_clock::now() + CONNECT_REQUEST_TIMEOUT;

    while (s_connectionState.load() == ConnectionState::CONNECTING) {
        if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED) {
            Debug::LogWarning("TransferChannelPool: Connect aborted because primary connection dropped");
            s_connectionState.store(ConnectionState::DISCONNECTED);
            co_return;
        }

        if (s_instance->m_waitingForConnections.load()) {
            co_return;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            Debug::LogError("TransferChannelPool: Timed out waiting for peer transfer channel ports");
            s_connectionState.store(ConnectionState::DISCONNECTED);
            co_return;
        }

        ConnectionManager::Send(PC_PackageType::TRANSFER_CHANNEL_POOL_CONNECT);

        timer.expires_after(std::chrono::milliseconds(CONNECT_REQUEST_RETRY_MS));
        co_await timer.async_wait();
    }
#endif
}

asio::awaitable<void> TransferChannelPool::Seek() {
    if (!s_instance) {
        Debug::LogError("TransferChannelPool::Seek: instance not initialized");
        co_return;
    }

    {
        auto& connectionMutex = s_instance->m_connectionMutex;
        std::lock_guard<std::mutex> lock(connectionMutex);

        if (s_connectionState.load() != ConnectionState::DISCONNECTED) {
            Debug::LogWarning(
                "TransferChannelPool: Seek skipped because state is {}",
                static_cast<int>(s_connectionState.load())
            );
            co_return;
        }
    }

    s_connectionState.store(ConnectionState::CONNECTING);
    const size_t count = s_instance->m_count;
    IOContext& context = ThreadPool::GetContext();
    Debug::Log("TransferChannelPool: Starting inbound seek flow for {} channels", count);

    struct PortInfo {
        std::unique_ptr<AwaitableFlag> flag;
        uint16_t port{0};
    };
    auto ports = std::make_shared<std::vector<PortInfo>>();
    ports->reserve(count);

    for (size_t i = 0; i < count; ++i) {
        ports->push_back({std::make_unique<AwaitableFlag>(context.get_executor()), 0});
    }

    for (size_t i = 0; i < count; ++i) {
        const auto channelOpt = Get(i);
        if (!channelOpt.has_value() || !channelOpt.value()) {
            Debug::LogError("TransferChannelPool: Missing channel {} while starting seek flow", i);
            s_connectionState.store(ConnectionState::DISCONNECTED);
            co_return;
        }

        asio::co_spawn(
            context,
            [channel = channelOpt.value(), ports, i]() -> asio::awaitable<void> {
                co_await channel->Seek(*(*ports)[i].flag, (*ports)[i].port);
            },
            asio::detached
        );
    }

    for (size_t i = 0; i < count; ++i) {
        co_await (*ports)[i].flag->Wait();
        if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED) {
            Debug::LogWarning("TransferChannelPool: Seek aborted because primary connection dropped before port publish");
            s_connectionState.store(ConnectionState::DISCONNECTED);
            co_return;
        }

        Debug::Log("TransferChannelPool: Publishing port {} for channel {}", (*ports)[i].port, i);
        ConnectionManager::Send(PC_PackageType::TRANSFER_CHANNEL_POOL_PORT_INFO, static_cast<uint32_t>(i), (*ports)[i].port);
    }

    co_await WaitForAllChannelsConnected();
}

void TransferChannelPool::Reset() {
    if (!s_instance) {
        Debug::LogWarning("TransferChannelPool::Reset: reset failed, transfer channel pool isn't initialized");
        return;
    }

    Debug::Log("TransferChannelPool: Resetting {} channels", s_instance->m_count);
    s_connectionState.store(ConnectionState::DISCONNECTED);
    s_instance->m_waitingForConnections.store(false);
    s_instance->ClearReservations();

    std::vector<std::shared_ptr<TransferChannel>> channelsToDisconnect;
    {
        std::lock_guard<std::mutex> lock(s_instance->m_channelsMutex);
        channelsToDisconnect = std::move(s_instance->m_channels);
        s_instance->m_channels.clear();
        s_instance->m_channels.reserve(s_instance->m_count);
        for (size_t i = 0; i < s_instance->m_count; ++i) {
            s_instance->m_channels.emplace_back(std::make_shared<TransferChannel>());
        }
    }

    for (const auto& channel : channelsToDisconnect) {
        asio::co_spawn(ThreadPool::GetContext(), [channel]() -> asio::awaitable<void> {
            co_await channel->Disconnect();
        }, asio::detached);
    }
}

void TransferChannelPool::Clear() {
    Debug::Log("TransferChannelPool: Clearing pool state");
    ClearReservations();

    std::vector<std::shared_ptr<TransferChannel>> channelsToDisconnect;
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        channelsToDisconnect = std::move(m_channels);
        m_channels.clear();
    }

    for (const auto& channel : channelsToDisconnect) {
        asio::co_spawn(ThreadPool::GetContext(), [channel]() -> asio::awaitable<void> {
            co_await channel->Disconnect();
        }, asio::detached);
    }
}

void TransferChannelPool::ClearReservations() const {
    std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
    if (!m_reservedIncomingPostChannels.empty()) {
        Debug::Log("TransferChannelPool: Clearing {} reservations", m_reservedIncomingPostChannels.size());
    }
    m_reservedIncomingPostChannels.clear();
}

void TransferChannelPool::InitializeChannels() {
    std::lock_guard<std::mutex> lock(m_channelsMutex);
    m_channels.reserve(m_count);
    for (size_t i = 0; i < m_count; ++i) {
        m_channels.emplace_back(std::make_shared<TransferChannel>());
    }
}

std::optional<std::shared_ptr<TransferChannel>> TransferChannelPool::Get(const size_t index) {
    if (!s_instance) {
        Debug::LogError("TransferChannelPool::Get: instance not initialized");
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(s_instance->m_channelsMutex);
    const auto& channels = s_instance->m_channels;
    if (index >= channels.size()) {
        Debug::LogError(
            "TransferChannelPool::Get: channel {} out of range (pool size: {})",
            index,
            channels.size()
        );
        return std::nullopt;
    }
    return channels[index];
}

asio::awaitable<BorrowedTransferChannel> TransferChannelPool::BorrowTransferChannel(const bool reserveIncomingPost) {
    if (!s_instance) {
        Debug::LogWarning("TransferChannelPool::BorrowTransferChannel: instance not initialized");
        throw std::runtime_error("TransferChannelPool::BorrowTransferChannel: instance not initialized");
    }

    Debug::Log(
        "TransferChannelPool: Borrow requested. reserveIncomingPost={}",
        reserveIncomingPost
    );
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor);

    while (true) {
        size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(s_instance->m_channelsMutex);
            count = s_instance->m_channels.size();
        }

        for (size_t i = 0; i < count; ++i) {
            if (s_instance->IsReserved(i)) {
                continue;
            }

            const auto opt = Get(i);
            if (!opt.has_value()) {
                continue;
            }
            const std::shared_ptr<TransferChannel> channel = opt.value();

            if (channel->GetConnectionState() != ConnectionState::CONNECTED || channel->IsUsed(false)) {
                continue;
            }

            if (!reserveIncomingPost) {
                Debug::Log("TransferChannelPool: Borrow granted on channel {}", i);
                co_return BorrowedTransferChannel{i, std::move(channel), nullptr};
            }

            const std::shared_ptr<void> reservationGuard = s_instance->TryReserve(i);
            if (!reservationGuard) {
                continue;
            }

            if (channel->GetConnectionState() == ConnectionState::CONNECTED && !channel->IsUsed(false)) {
                Debug::Log("TransferChannelPool: Reserved borrow granted on channel {}", i);
                co_return BorrowedTransferChannel{i, std::move(channel), reservationGuard};
            }
        }

        timer.expires_after(std::chrono::milliseconds(BORROW_WAIT_POLL_MS));
        co_await timer.async_wait();
    }
}

asio::awaitable<bool> TransferChannelPool::WaitForAllChannelsConnected() {
    if (!s_instance) {
        Debug::LogError("TransferChannelPool::WaitForAllChannelsConnected: instance not initialized");
        co_return false;
    }

    IOContext& context = ThreadPool::GetContext();
    asio::steady_timer timer(context);
    const auto deadline = std::chrono::steady_clock::now() + CHANNELS_CONNECT_TIMEOUT;

    while (true) {
        if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED) {
            Debug::LogWarning("TransferChannelPool: Channel wait aborted because primary connection is no longer connected");
            s_connectionState.store(ConnectionState::DISCONNECTED);
            s_instance->m_waitingForConnections.store(false);
            co_return false;
        }

        bool allConnected = true;
        for (size_t i = 0; i < s_instance->m_count; ++i) {
            const auto channel = Get(i);
            if (!channel.has_value() || !channel.value()) {
                Debug::LogError("TransferChannelPool: Channel {} unavailable while waiting for connections", i);
                s_connectionState.store(ConnectionState::DISCONNECTED);
                s_instance->m_waitingForConnections.store(false);
                co_return false;
            }

            if (channel.value()->GetConnectionState() != ConnectionState::CONNECTED) {
                allConnected = false;
                break;
            }
        }

        if (allConnected) {
            s_connectionState.store(ConnectionState::CONNECTED);
            s_instance->m_waitingForConnections.store(false);
            Debug::Log("TransferChannelPool: All {} channels connected", s_instance->m_count);
            co_return true;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            Debug::LogError("TransferChannelPool: Timed out waiting for all transfer channels to connect");
            s_connectionState.store(ConnectionState::DISCONNECTED);
            s_instance->m_waitingForConnections.store(false);
            co_return false;
        }

        timer.expires_after(std::chrono::milliseconds(CHANNEL_CONNECT_POLL_MS));
        co_await timer.async_wait();
    }
}

ConnectionState TransferChannelPool::GetConnectionState() {
    return s_connectionState.load();
}

bool TransferChannelPool::IsReserved(const size_t index) const {
    std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
    return m_reservedIncomingPostChannels.contains(index);
}

std::shared_ptr<void> TransferChannelPool::TryReserve(const size_t index) const {
    std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
    if (m_reservedIncomingPostChannels.contains(index)) {
        return nullptr;
    }

    m_reservedIncomingPostChannels.insert(index);
    Debug::Log("TransferChannelPool: Reserved channel {}", index);
    const std::shared_ptr<bool> reservationGuard(new bool(true), [this, index](bool* token) {
        delete token;
        std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
        m_reservedIncomingPostChannels.erase(index);
        Debug::Log("TransferChannelPool: Released reservation for channel {}", index);
    });
    return reservationGuard;
}
