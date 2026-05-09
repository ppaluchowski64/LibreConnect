#include <TransferChannelPool.h>
#include <ThreadPool.h>

#include <chrono>
#include <ConnectionManager.h>

namespace {
constexpr size_t BORROW_WAIT_POLL_MS = 100;
constexpr size_t CHANNEL_CONNECT_POLL_MS = 10;
constexpr auto CHANNELS_CONNECT_TIMEOUT = std::chrono::seconds(15);
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
#ifdef DESKTOP_DEVICE
        Debug::Log("TransferChannelPool: Received peer connect request");
        asio::co_spawn(ThreadPool::GetContext(), Seek(), asio::detached);
#else
        Debug::LogWarning("TransferChannelPool: Ignoring peer connect request on mobile");
#endif
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::TRANSFER_CHANNEL_POOL_PORT_INFO, [](PC_Package&& package) {
        if (!s_instance) {
            Debug::LogError("TransferChannelPool: Received port info before initialization");
            return;
        }

        const size_t index = s_instance->m_channelConnectionIncrement.fetch_add(1);

        if (index >= s_instance->m_count) {
            Debug::LogWarning(
                "TransferChannelPool: Ignoring extra port info for channel {} (pool size: {})",
                index,
                s_instance->m_count
            );
            return;
        }

        const uint16_t port = package->GetValue<uint16_t>();
        Debug::Log("TransferChannelPool: Received port {} for channel {}", port, index);

        const auto opt = Get(index);
        if (!opt.has_value() || !opt.value()) {
            Debug::LogError("TransferChannelPool: Channel {} unavailable for connect", index);
            return;
        }

        const auto& channel = opt.value();

        asio::co_spawn(ThreadPool::GetContext(), channel->Connect(TCPEndpoint(ConnectionManager::GetPeerAddress(), port)), asio::detached);

#ifdef MOBILE_DEVICE
        if (index + 1 == s_instance->m_count) {
            Debug::Log("TransferChannelPool: Received final port info, waiting for all channels to connect");
            asio::co_spawn(ThreadPool::GetContext(), WaitForAllChannelsConnected(), asio::detached);
        }
#endif
    });
}

asio::awaitable<void> TransferChannelPool::Connect() {
#ifndef MOBILE_DEVICE
    Debug::Log("TransferChannelPool: Desktop role waiting for peer connect request");
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
    s_instance->m_channelConnectionIncrement.store(0);
    ConnectionManager::Send(PC_PackageType::TRANSFER_CHANNEL_POOL_CONNECT);
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

    std::vector<std::pair<std::unique_ptr<AwaitableFlag>, uint16_t>> ports;
    ports.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        ports.emplace_back(
            std::make_unique<AwaitableFlag>(context.get_executor()), 0
        );

        const auto channel = Get(i);
        if (!channel.has_value() || !channel.value()) {
            Debug::LogError("TransferChannelPool: Missing channel {} while starting seek flow", i);
            s_connectionState.store(ConnectionState::DISCONNECTED);
            co_return;
        }

        asio::co_spawn(
            context,
            channel.value()->Seek(*ports[i].first, ports[i].second),
            asio::detached
        );
    }

    asio::steady_timer timer(context);
    for (size_t i = 0; i < count; ++i) {
        co_await ports[i].first->Wait();
        if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED) {
            Debug::LogWarning("TransferChannelPool: Seek aborted because primary connection dropped before port publish");
            s_connectionState.store(ConnectionState::DISCONNECTED);
            co_return;
        }

        Debug::Log("TransferChannelPool: Publishing port {} for channel {}", ports[i].second, i);
        ConnectionManager::Send(PC_PackageType::TRANSFER_CHANNEL_POOL_PORT_INFO, ports[i].second);
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
    s_instance->m_channelConnectionIncrement.store(0);
    s_instance->Clear();
    s_instance->InitializeChannels();
}

void TransferChannelPool::Clear() {
    Debug::Log("TransferChannelPool: Clearing pool state");
    ClearReservations();
    m_channels.clear();
}

void TransferChannelPool::ClearReservations() const {
    std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
    if (!m_reservedIncomingPostChannels.empty()) {
        Debug::Log("TransferChannelPool: Clearing {} reservations", m_reservedIncomingPostChannels.size());
    }
    m_reservedIncomingPostChannels.clear();
}

void TransferChannelPool::InitializeChannels() {
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

    const auto& channels = s_instance->m_channels;

    while (true) {
        for (size_t i = 0; i < channels.size(); ++i) {
            if (s_instance->IsReserved(i)) {
                continue;
            }

            const std::shared_ptr<TransferChannel> channel = channels[i];
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
            co_return false;
        }

        bool allConnected = true;
        for (size_t i = 0; i < s_instance->m_count; ++i) {
            const auto channel = Get(i);
            if (!channel.has_value() || !channel.value()) {
                Debug::LogError("TransferChannelPool: Channel {} unavailable while waiting for connections", i);
                s_connectionState.store(ConnectionState::DISCONNECTED);
                co_return false;
            }

            if (channel.value()->GetConnectionState() != ConnectionState::CONNECTED) {
                allConnected = false;
                break;
            }
        }

        if (allConnected) {
            s_connectionState.store(ConnectionState::CONNECTED);
            Debug::Log("TransferChannelPool: All {} channels connected", s_instance->m_count);
            co_return true;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            Debug::LogError("TransferChannelPool: Timed out waiting for all transfer channels to connect");
            s_connectionState.store(ConnectionState::DISCONNECTED);
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
    return std::shared_ptr<void>(nullptr, [this, index](void*) {
        std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
        m_reservedIncomingPostChannels.erase(index);
        Debug::Log("TransferChannelPool: Released reservation for channel {}", index);
    });
}
