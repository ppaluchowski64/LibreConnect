#ifndef TRANSFER_CHANNEL_H
#define TRANSFER_CHANNEL_H

#include <filesystem>
#include <functional>
#include <span>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <AsioCommon.h>
#include <AwaitableFlag.h>

#include <FileEntry.h>

class TransferChannel final : public std::enable_shared_from_this<TransferChannel>{
public:
    TransferChannel();
    TransferChannel(const TransferChannel&) = delete;
    TransferChannel& operator=(const TransferChannel&) = delete;

    size_t FetchTransferProgress() const;
    size_t FetchCurrentFileProgress() const;
    size_t FetchCurrentFileSize() const;
    bool IsUsed(bool outTransfer) const;
    ConnectionState GetConnectionState() const;

    asio::awaitable<void> Connect(TCPEndpoint endpoint);
    asio::awaitable<void> Seek(AwaitableFlag& flag, uint16_t& port);
    asio::awaitable<void> Disconnect();

    asio::awaitable<void> ReceiveDirectory(std::filesystem::path path);
    asio::awaitable<void> SendDirectory(std::filesystem::path path);

    asio::awaitable<void> ReceiveFile(std::filesystem::path path);
    asio::awaitable<void> SendFile(std::filesystem::path path);

    asio::awaitable<void> SendDirectoryEntries(std::vector<FileEntry> entries);
    asio::awaitable<void> ReceiveDirectoryEntries(std::vector<FileEntry>& entries);

    using FileSink = std::function<asio::awaitable<void>(std::span<const uint8_t>)>;
    asio::awaitable<void> ReceiveFileData(size_t expectedSize, FileSink sink);

private:
    friend class TransferChannelTestAccess;
    IOContextStrand m_strand;
    asio::awaitable<void> CoConnect(TCPEndpoint endpoint);
    asio::awaitable<void> CoSeek(AwaitableFlag& flag, uint16_t& port);
    asio::awaitable<void> CoDisconnect();
    asio::awaitable<void> CoReceiveDirectory(std::filesystem::path path);
    asio::awaitable<void> CoSendDirectory(std::filesystem::path path);
    asio::awaitable<void> CoReceiveFile(std::filesystem::path path);
    asio::awaitable<void> CoSendFile(std::filesystem::path path);
    asio::awaitable<void> CoSendDirectoryEntries(std::vector<FileEntry> entries);
    asio::awaitable<void> CoReceiveDirectoryEntries(std::vector<FileEntry>& entries);
    asio::awaitable<void> CoReceiveFileData(size_t expectedSize, FileSink sink);
    asio::awaitable<void> CleanupConnection();
    struct FileHeader {
        std::string relativePath;
        size_t fileSize;
        bool last;

        void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
        void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
        size_t GetSerializedSize() const;
    };

    asio::awaitable<bool> Send(std::filesystem::path file);
    asio::awaitable<bool> Receive(std::filesystem::path destination, size_t length);

    asio::awaitable<bool> SendBuffer(size_t size);

    std::shared_ptr<SSLSocket> m_socket;
    std::shared_ptr<TCPAcceptor> m_acceptor;
    std::shared_ptr<SSLContext_> m_sslContext;
    std::vector<uint8_t> m_bufferIn;
    std::vector<uint8_t> m_bufferOut;
    std::atomic<size_t> m_progress{0};
    std::atomic<size_t> m_currentFileProgress{0};
    std::atomic<size_t> m_currentFileSize{0};
    std::atomic<ConnectionState> m_connectionState{ConnectionState::DISCONNECTED};
    std::atomic<bool> m_receive{false};
    std::atomic<bool> m_send{false};
};

#endif //TRANSFER_CHANNEL_H
