#ifndef TRANSFER_CHANNEL_POOL_H
#define TRANSFER_CHANNEL_POOL_H

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include <TransferChannel.h>

struct BorrowedTransferChannel {
    size_t index{};
    std::shared_ptr<TransferChannel> channel;
    std::shared_ptr<void> reservationGuard;
};

class TransferChannelPool final {
public:
    TransferChannelPool() = delete;

    static void Initialize(size_t count);
    static asio::awaitable<void> Connect();
    static void Reset();

    static std::optional<std::shared_ptr<TransferChannel>> Get(size_t index);
    static asio::awaitable<BorrowedTransferChannel> BorrowTransferChannel(bool reserveIncomingPost = false);
    static ConnectionState GetConnectionState();

private:
    explicit TransferChannelPool(size_t count);
    static asio::awaitable<void> Seek();
    static asio::awaitable<bool> WaitForAllChannelsConnected();

    void Clear();
    void InitializeChannels();
    void ClearReservations() const;
    bool IsReserved(size_t index) const;
    std::shared_ptr<void> TryReserve(size_t index) const;

    static TransferChannelPool* s_instance;
    static std::atomic<ConnectionState> s_connectionState;

    std::mutex m_connectionMutex;
    std::vector<std::shared_ptr<TransferChannel>> m_channels;
    size_t m_count{};
    std::atomic<size_t> m_channelConnectionIncrement{};
    mutable std::mutex m_incomingPostReservationMutex;
    mutable std::unordered_set<size_t> m_reservedIncomingPostChannels;
};

#endif //TRANSFER_CHANNEL_POOL_H
