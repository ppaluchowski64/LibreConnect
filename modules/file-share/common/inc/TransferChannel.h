#ifndef TRANSFER_CHANNEL_H
#define TRANSFER_CHANNEL_H

#include <filesystem>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <AsioCommon.h>
#include <AwaitableFlag.h>


class TransferChannel final : public std::enable_shared_from_this<TransferChannel>{
public:
    TransferChannel() = delete;
    TransferChannel(const TransferChannel&) = delete;
    TransferChannel& operator=(const TransferChannel&) = delete;
    explicit TransferChannel(const std::shared_ptr<SSLContext>& sslContext, IOContext& context);

    size_t FetchTransferProgress() const;
    bool IsUsed() const;
    ConnectionState GetConnectionState() const;

    asio::awaitable<void> Connect(TCPEndpoint endpoint);
    asio::awaitable<void> Seek(AwaitableFlag& flag, uint16_t& port);
    asio::awaitable<void> Disconnect();
    asio::awaitable<void> Receive(const std::filesystem::path& file, uint32_t partitionCount, uint32_t index);
    asio::awaitable<void> Send(std::filesystem::path file, uint32_t partitionCount, uint32_t index);
    asio::awaitable<void> CleanupConnection();

private:
    IOContext& m_context;
    std::unique_ptr<SSLSocket> m_socket;
    std::shared_ptr<SSLContext> m_sslContext;
    std::vector<uint8_t> m_buffer;
    std::atomic<size_t> m_progress{0};
    std::atomic<ConnectionState> m_connectionState{ConnectionState::DISCONNECTED};
    std::atomic<bool> m_isUsed{false};

};

#endif //TRANSFER_CHANNEL_H