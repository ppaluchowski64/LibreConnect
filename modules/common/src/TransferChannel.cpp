#include <TransferChannel.h>
#include <fstream>
#include <Packable.h>
#include <fmt/os.h>
#include <ConnectionManager.h>
#include <ThreadPool.h>
#include <algorithm>
#include <optional>

static constexpr size_t TRANSFER_BUFFER_SIZE = 1024 * 256;

namespace {

bool IsSubpath(const std::filesystem::path& basePath, const std::filesystem::path& candidatePath) {
    const auto mismatch = std::mismatch(basePath.begin(), basePath.end(), candidatePath.begin(), candidatePath.end());
    return mismatch.first == basePath.end();
}

std::optional<std::filesystem::path> BuildSafeReceivePath(const std::filesystem::path& canonicalBasePath, const std::string& relativePathRaw) {
    std::filesystem::path relativePath(relativePathRaw);
    if (relativePath.empty() || relativePath.is_absolute() || relativePath.has_root_name() || relativePath.has_root_directory()) {
        return std::nullopt;
    }

    relativePath = relativePath.lexically_normal();
    if (relativePath.empty()) {
        return std::nullopt;
    }

    for (const auto& part : relativePath) {
        if (part == "..") {
            return std::nullopt;
        }
    }

    std::error_code errorCode;
    std::filesystem::path candidatePath = std::filesystem::weakly_canonical(canonicalBasePath / relativePath, errorCode);
    if (errorCode) {
        return std::nullopt;
    }

    if (!IsSubpath(canonicalBasePath, candidatePath)) {
        return std::nullopt;
    }

    return candidatePath;
}

asio::awaitable<void> DrainSocketBytes(SSLSocket& socket, std::vector<uint8_t>& buffer, const size_t bytesToDrain) {
    size_t drainedBytes = 0;
    while (drainedBytes < bytesToDrain) {
        const size_t chunkSize = std::min(buffer.size(), bytesToDrain - drainedBytes);
        co_await asio::async_read(socket, asio::mutable_buffer(buffer.data(), chunkSize), asio::use_awaitable);
        drainedBytes += chunkSize;
    }
}
}

TransferChannel::TransferChannel() : m_strand(asio::make_strand(ThreadPool::GetContext())), m_socket(nullptr), m_bufferIn(TRANSFER_BUFFER_SIZE), m_bufferOut(TRANSFER_BUFFER_SIZE) {}

asio::awaitable<void> TransferChannel::Connect(TCPEndpoint endpoint) {
    return asio::co_spawn(m_strand, CoConnect(endpoint), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::Seek(AwaitableFlag& flag, uint16_t& port) {
    return asio::co_spawn(m_strand, CoSeek(flag, port), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::Disconnect() {
    return asio::co_spawn(m_strand, CoDisconnect(), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::ReceiveDirectory(std::filesystem::path path) {
    return asio::co_spawn(m_strand, CoReceiveDirectory(std::move(path)), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::SendDirectory(std::filesystem::path path) {
    return asio::co_spawn(m_strand, CoSendDirectory(std::move(path)), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::ReceiveFile(std::filesystem::path path) {
    return asio::co_spawn(m_strand, CoReceiveFile(std::move(path)), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::SendFile(std::filesystem::path path) {
    return asio::co_spawn(m_strand, CoSendFile(std::move(path)), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::SendDirectoryEntries(std::vector<FileEntry> entries) {
    return asio::co_spawn(m_strand, CoSendDirectoryEntries(std::move(entries)), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::ReceiveDirectoryEntries(std::vector<FileEntry>& entries) {
    return asio::co_spawn(m_strand, CoReceiveDirectoryEntries(entries), asio::use_awaitable);
}

asio::awaitable<void> TransferChannel::ReceiveFileData(size_t expectedSize, FileSink sink) {
    return asio::co_spawn(m_strand, CoReceiveFileData(expectedSize, std::move(sink)), asio::use_awaitable);
}

size_t TransferChannel::FetchTransferProgress() const {
    return m_progress.load();
}

size_t TransferChannel::FetchCurrentFileProgress() const {
    return m_currentFileProgress.load();
}

size_t TransferChannel::FetchCurrentFileSize() const {
    return m_currentFileSize.load();
}

bool TransferChannel::IsUsed(const bool outTransfer) const {
    return outTransfer ? m_send.load() : m_receive.load();
}

ConnectionState TransferChannel::GetConnectionState() const {
    return m_connectionState.load();
}

asio::awaitable<void> TransferChannel::CoConnect(TCPEndpoint endpoint) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    IOContext& context = ThreadPool::GetContext();
    m_sslContext = ConnectionManager::GetSSLContextClient();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        const std::shared_ptr<SSLSocket> socket = std::make_shared<SSLSocket>(m_strand, *m_sslContext);
        std::atomic_store(&m_socket, socket);

        co_await asio::async_connect(socket->lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);
        co_await socket->async_handshake(SSLStreamBase::client, asio::use_awaitable);

        Debug::Log("Accepted TLS connection to {}:{}", socket->lowest_layer().remote_endpoint().address().to_string(), socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(context, [self]() -> asio::awaitable<void> { co_await self->Disconnect(); }, asio::detached);
    }
}

asio::awaitable<void> TransferChannel::CoSeek(AwaitableFlag& flag, uint16_t& port) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    IOContext& context = ThreadPool::GetContext();
    m_sslContext = ConnectionManager::GetSSLContextServer();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        const std::shared_ptr<SSLSocket> socket = std::make_shared<SSLSocket>(m_strand, *m_sslContext);
        std::atomic_store(&m_socket, socket);

        const std::shared_ptr<TCPAcceptor> acceptor = std::make_shared<TCPAcceptor>(m_strand);
        std::atomic_store(&m_acceptor, acceptor);
        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 0);

        acceptor->open(endpoint.protocol());
        acceptor->set_option(asio::socket_base::reuse_address(true));
        acceptor->bind(endpoint);
        acceptor->listen();

        port = acceptor->local_endpoint().port();
        flag.Signal();

        co_await acceptor->async_accept(socket->lowest_layer(), asio::use_awaitable);
        co_await socket->async_handshake(SSLStreamBase::server, asio::use_awaitable);

        Debug::Log("Accepted TLS connection to {}:{}", socket->lowest_layer().remote_endpoint().address().to_string(), socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);

    } catch (std::system_error& error) {
        if (port == 0) {
            flag.Signal();
        }
        HandleAsioError(error.code());
        asio::co_spawn(context, [self]() -> asio::awaitable<void> { co_await self->Disconnect(); }, asio::detached);
    }
}

asio::awaitable<void> TransferChannel::CoReceiveDirectory(std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);
    if (!socket || m_connectionState.load() != ConnectionState::CONNECTED || m_receive.load()) {
        throw std::runtime_error("Transfer channel is unavailable for receiving");
    }

    m_receive.store(true);
    struct ResetFlag { std::atomic<bool>& flag; ~ResetFlag() { flag.store(false); } } reset{m_receive};
    m_progress.store(0);
    m_currentFileSize.store(0);
    m_currentFileProgress.store(0);

    try {
        std::filesystem::create_directories(path);
        std::error_code errorCode;
        const std::filesystem::path canonicalBasePath = std::filesystem::weakly_canonical(path, errorCode);
        if (errorCode) {
            Debug::LogError("Failed to canonicalize destination directory '{}' ({})", path.string(), errorCode.message());
            throw std::system_error(errorCode);
        }

        FileHeader fileHeader{};
        std::vector<uint8_t> buffer(1024);
        std::vector<uint8_t> headerBuffer(sizeof(size_t));

        do {
            size_t fileHeaderSize = 0;
            co_await asio::async_read(*socket, asio::mutable_buffer(headerBuffer.data(), headerBuffer.size()), asio::use_awaitable);

            size_t offset = 0;
            DeserializeObject(fileHeaderSize, headerBuffer, offset);

            if (fileHeaderSize > buffer.size()) {
                Debug::LogError("The header size ({}) is exceeding the buffer size ({})", fileHeaderSize, buffer.size());
                throw std::runtime_error("Invalid directory transfer header");
            }

            asio::mutable_buffer mutableBuffer(buffer.data(), fileHeaderSize);
            co_await asio::async_read(*socket, mutableBuffer, asio::use_awaitable);

            offset = 0;
            fileHeader.Deserialize(buffer, offset);

            if (fileHeader.last && fileHeader.fileSize == 0 && fileHeader.relativePath.empty()) {
                break;
            }

            const std::optional<std::filesystem::path> filePath = BuildSafeReceivePath(canonicalBasePath, fileHeader.relativePath);
            if (!filePath.has_value()) {
                Debug::LogError(
                    "Rejected incoming file path '{}' outside destination '{}'",
                    fileHeader.relativePath,
                    canonicalBasePath.string()
                );
                co_await DrainSocketBytes(*socket, m_bufferIn, fileHeader.fileSize);
                continue;
            }

            const bool received = co_await Receive(filePath.value(), fileHeader.fileSize);
            if (!received) {
                throw std::runtime_error("Failed to receive directory file");
            }

        } while (!fileHeader.last);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), [self]() -> asio::awaitable<void> { co_await self->Disconnect(); }, asio::detached);
        throw;
    }

}

asio::awaitable<void> TransferChannel::CoSendDirectory(const std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);

    if (!socket || m_connectionState.load() != ConnectionState::CONNECTED || m_send.load()) {
        throw std::runtime_error("Transfer channel is unavailable for sending");
    }

    m_send.store(true);
    struct ResetFlag { std::atomic<bool>& flag; ~ResetFlag() { flag.store(false); } } reset{m_send};
    m_progress.store(0);
    m_currentFileSize.store(0);
    m_currentFileProgress.store(0);

    try {
        std::vector<std::filesystem::path> fileList;
        for (auto& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                fileList.push_back(entry.path());
            }
        }

        if (fileList.empty()) {
            FileHeader fileHeader{};
            fileHeader.relativePath = "";
            fileHeader.fileSize = 0;
            fileHeader.last = true;

            std::vector<uint8_t> buffer(fileHeader.GetSerializedSize() + sizeof(size_t));
            size_t offset = 0;
            SerializeObject(fileHeader.GetSerializedSize(), buffer, offset);
            fileHeader.Serialize(buffer, offset);

            co_await asio::async_write(*socket, asio::buffer(buffer), asio::use_awaitable);
        }

        for (size_t i = 0; i < fileList.size(); ++i) {
            const auto& currentFilePath = fileList[i];
            const bool isLast = (i == fileList.size() - 1);

            FileHeader fileHeader{};
            fileHeader.fileSize = std::filesystem::file_size(currentFilePath);
            fileHeader.relativePath = std::filesystem::relative(currentFilePath, path).generic_string();
            fileHeader.last = isLast;

            std::vector<uint8_t> buffer(fileHeader.GetSerializedSize() + sizeof(size_t));
            size_t offset = 0;
            SerializeObject(fileHeader.GetSerializedSize(), buffer, offset);
            fileHeader.Serialize(buffer, offset);

            co_await asio::async_write(*socket, asio::buffer(buffer), asio::use_awaitable);
            const bool sent = co_await Send(currentFilePath);
            if (!sent) {
                throw std::runtime_error("Failed to send directory file");
            }
        }

    } catch (const std::exception& e) {
        Debug::LogError("Directory transfer failed");
        asio::co_spawn(ThreadPool::GetContext(), [self]() -> asio::awaitable<void> { co_await self->Disconnect(); }, asio::detached);
        throw;
    }

}

asio::awaitable<void> TransferChannel::CoReceiveFile(std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);
    if (!socket || m_connectionState.load() != ConnectionState::CONNECTED || m_receive.load()) {
        throw std::runtime_error("Transfer channel is unavailable for receiving");
    }

    m_receive.store(true);
    struct ResetFlag { std::atomic<bool>& flag; ~ResetFlag() { flag.store(false); } } reset{m_receive};
    m_progress.store(0);
    m_currentFileSize.store(0);
    m_currentFileProgress.store(0);

    FileHeader fileHeader{};
    size_t offset = 0;

    std::vector<uint8_t> buffer(fileHeader.GetSerializedSize());
    const asio::mutable_buffer mutableBuffer(buffer.data(), buffer.size());

    co_await asio::async_read(*socket, mutableBuffer, asio::use_awaitable);
    fileHeader.Deserialize(buffer, offset);

    const bool received = co_await Receive(path, fileHeader.fileSize);
    if (!received) {
        throw std::runtime_error("Failed to receive file");
    }

}

asio::awaitable<void> TransferChannel::CoSendFile(std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);

    if (!socket || m_connectionState.load() != ConnectionState::CONNECTED || m_send.load()) {
        throw std::runtime_error("Transfer channel is unavailable for sending");
    }

    m_send.store(true);
    struct ResetFlag { std::atomic<bool>& flag; ~ResetFlag() { flag.store(false); } } reset{m_send};
    m_progress.store(0);
    m_currentFileSize.store(0);
    m_currentFileProgress.store(0);

    const FileHeader fileHeader{
        "",
        std::filesystem::file_size(path),
        true
    };

    size_t offset = 0;
    std::vector<uint8_t> buffer(fileHeader.GetSerializedSize());

    fileHeader.Serialize(buffer, offset);
    const asio::const_buffer constBuffer(buffer.data(), buffer.size());

    co_await asio::async_write(*socket, constBuffer, asio::use_awaitable);
    const bool sent = co_await Send(path);
    if (!sent) {
        throw std::runtime_error("Failed to send file");
    }

}

asio::awaitable<void> TransferChannel::CoSendDirectoryEntries(std::vector<FileEntry> entries) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    if (m_connectionState.load() != ConnectionState::CONNECTED || m_send.load()) {
        Debug::LogWarning(
            "TransferChannel: SendDirectoryEntries skipped. State: {}, SendInUse: {}",
            static_cast<int>(m_connectionState.load()),
            m_send.load()
        );
        co_return;
    }

    m_send.store(true);
    m_progress.store(0);
    const size_t totalCount = entries.size();
    size_t sentCount = 0;
    size_t chunkCount = 0;
    constexpr size_t frameHeaderSize = sizeof(size_t) * 2;
    Debug::Log("TransferChannel: SendDirectoryEntries started. Entries: {}", totalCount);

    size_t offset = 0;

    {
        SerializeObject(totalCount, m_bufferOut, offset);
        if (!co_await SendBuffer(sizeof(size_t))) {
            m_send.store(false);
            co_return;
        }
    }

    offset = frameHeaderSize;
    size_t count = 0;
    for (const auto& entry : entries) {
        const size_t entrySize = entry.GetSerializedSize();
        if (offset + entrySize > m_bufferOut.size()) {
            {
                size_t zeroOffset = 0;
                SerializeObject(offset - frameHeaderSize, m_bufferOut, zeroOffset);
                SerializeObject(count, m_bufferOut, zeroOffset);
            }

            if (!co_await SendBuffer(offset)) {
                m_send.store(false);
                co_return;
            }
            sentCount += count;
            chunkCount++;
            Debug::Log(
                "TransferChannel: SendDirectoryEntries chunk sent. Chunk: {}, EntriesInChunk: {}, EntriesSent: {}/{}",
                chunkCount,
                count,
                sentCount,
                totalCount
            );
            offset = frameHeaderSize;
            count = 0;
        }

        count++;
        entry.Serialize(m_bufferOut, offset);
    }

    {
        size_t zeroOffset = 0;
        SerializeObject(offset - frameHeaderSize, m_bufferOut, zeroOffset);
        SerializeObject(count, m_bufferOut, zeroOffset);
    }

    if (!co_await SendBuffer(offset)) {
        m_send.store(false);
        co_return;
    }
    sentCount += count;
    chunkCount++;
    Debug::Log(
        "TransferChannel: SendDirectoryEntries finished. Chunks: {}, EntriesSent: {}/{}",
        chunkCount,
        sentCount,
        totalCount
    );
    m_send.store(false);
}

asio::awaitable<void> TransferChannel::CoReceiveDirectoryEntries(std::vector<FileEntry>& entries) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);
    if (!socket || m_connectionState.load() != ConnectionState::CONNECTED || m_receive.load()) {
        Debug::LogWarning(
            "TransferChannel: ReceiveDirectoryEntries skipped. State: {}, ReceiveInUse: {}",
            static_cast<int>(m_connectionState.load()),
            m_receive.load()
        );
        throw std::runtime_error("Directory channel is unavailable");
    }

    m_receive.store(true);
    struct ResetFlag { std::atomic<bool>& flag; ~ResetFlag() { flag.store(false); } } reset{m_receive};
    m_progress.store(0);
    constexpr size_t frameHeaderSize = sizeof(size_t) * 2;

    size_t totalCount = 0;
    size_t offset = 0;

    {
        const asio::mutable_buffer buffer(m_bufferIn.data(), sizeof(size_t));
        co_await asio::async_read(*socket, buffer, asio::use_awaitable);
        DeserializeObject(totalCount, m_bufferIn, offset);
    }
    Debug::Log("TransferChannel: ReceiveDirectoryEntries started. Expected entries: {}", totalCount);

    entries.reserve(totalCount);
    const size_t expectedTotalCount = totalCount;
    size_t receivedCount = 0;
    size_t chunkCount = 0;

    while (true) {
        const asio::mutable_buffer headerBuffer(m_bufferIn.data(), frameHeaderSize);
        co_await asio::async_read(*socket, headerBuffer, asio::use_awaitable);

        size_t payloadSize = 0;
        size_t count = 0;
        offset = 0;
        DeserializeObject(payloadSize, m_bufferIn, offset);
        DeserializeObject(count, m_bufferIn, offset);

        if (payloadSize > m_bufferIn.size() - frameHeaderSize) {
            Debug::LogError(
                "TransferChannel: ReceiveDirectoryEntries payload exceeds buffer. Payload: {}, Capacity: {}",
                payloadSize,
                m_bufferIn.size() - frameHeaderSize
            );
            m_receive.store(false);
            co_return;
        }

        if (payloadSize > 0) {
            const asio::mutable_buffer payloadBuffer(m_bufferIn.data() + frameHeaderSize, payloadSize);
            co_await asio::async_read(*socket, payloadBuffer, asio::use_awaitable);
        }

        offset = frameHeaderSize;
        for (size_t i = 0; i < count; i++) {
            entries.push_back({});
            FileEntry& entry = entries.back();
            entry.Deserialize(m_bufferIn, offset);
        }
        receivedCount += count;
        chunkCount++;
        Debug::Log(
            "TransferChannel: ReceiveDirectoryEntries chunk received. Chunk: {}, EntriesInChunk: {}, EntriesReceived: {}/{}",
            chunkCount,
            count,
            receivedCount,
            expectedTotalCount
        );

        totalCount -= count;
        if (totalCount == 0) {
            break;
        }
    }

    Debug::Log(
        "TransferChannel: ReceiveDirectoryEntries finished. Chunks: {}, EntriesReceived: {}/{}",
        chunkCount,
        receivedCount,
        expectedTotalCount
    );
    m_receive.store(false);
}

asio::awaitable<bool> TransferChannel::Receive(const std::filesystem::path destination, size_t length) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);
    if (!socket) co_return false;
    m_currentFileProgress.store(0);
    m_currentFileSize.store(length);

    try {
        std::filesystem::create_directories(destination.parent_path());

        std::ofstream fileStream(destination, std::ios::binary);
        if (!fileStream.is_open()) {
            Debug::LogError("Failed to open destination file '{}'", destination.string());
            co_await Disconnect();
            co_return false;
        }

        size_t offset = 0;

        while (offset < length) {
            const size_t bufferSize = std::min(m_bufferIn.size(), length - offset);
            asio::mutable_buffer buffer(m_bufferIn.data(), bufferSize);
            co_await asio::async_read(*socket, buffer, asio::use_awaitable);

            fileStream.write(reinterpret_cast<const char*>(m_bufferIn.data()), bufferSize);
            if (!fileStream.good()) {
                Debug::LogError("Failed while writing destination file '{}'", destination.string());
                co_await Disconnect();
                co_return false;
            }

            offset += bufferSize;
            m_progress.fetch_add(bufferSize);
            m_currentFileProgress.store(offset);
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), [self]() -> asio::awaitable<void> { co_await self->Disconnect(); }, asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<bool> TransferChannel::SendBuffer(const size_t size) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);
    if (!socket) co_return false;

    try {
        const asio::const_buffer buffer(m_bufferOut.data(), size);
        co_await asio::async_write(*socket, buffer, asio::use_awaitable);
        m_progress.fetch_add(size);
    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), [self]() -> asio::awaitable<void> { co_await self->Disconnect(); }, asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<bool> TransferChannel::Send(const std::filesystem::path file) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);
    if (!socket) co_return false;

    try {
        std::ifstream fileStream(file, std::ios::binary);
        if (!fileStream.is_open()) {
            Debug::LogError("Failed to open file");
            co_return false;
        }

        const size_t length = std::filesystem::file_size(file);
        size_t offset = 0;
        m_currentFileProgress.store(0);
        m_currentFileSize.store(length);

        while (offset < length) {
            const size_t bufferSize = std::min(m_bufferOut.size(), length - offset);
            offset += bufferSize;

            fileStream.read(reinterpret_cast<char*>(m_bufferOut.data()), bufferSize);
            if (fileStream.gcount() != static_cast<std::streamsize>(bufferSize)) {
                Debug::LogError("Failed while reading source file '{}'", file.string());
                co_await Disconnect();
                co_return false;
            }
            asio::const_buffer buffer(m_bufferOut.data(), bufferSize);

            co_await asio::async_write(*socket, buffer, asio::use_awaitable);

            m_progress.fetch_add(bufferSize);
            m_currentFileProgress.store(offset);
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), [self]() -> asio::awaitable<void> { co_await self->Disconnect(); }, asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<void> TransferChannel::CoDisconnect() {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    if (m_connectionState.load() == ConnectionState::DISCONNECTED || m_connectionState.load() == ConnectionState::DISCONNECTING) {
        co_return;
    }

    m_connectionState.store(ConnectionState::DISCONNECTING);
    co_await CleanupConnection();
    m_connectionState.store(ConnectionState::DISCONNECTED);
}

asio::awaitable<void> TransferChannel::CleanupConnection() {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<TCPAcceptor> acceptor = std::atomic_exchange(&m_acceptor, std::shared_ptr<TCPAcceptor>{});
    if (acceptor) {
        std::error_code errorCode;
        acceptor->close(errorCode);
    }
    const std::shared_ptr<SSLSocket> socket = std::atomic_exchange(&m_socket, std::shared_ptr<SSLSocket>{});
    co_await CleanupSSLSocket(socket.get());
}

void TransferChannel::FileHeader::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(relativePath, buffer, offset);
    SerializeObject(fileSize, buffer, offset);
    SerializeObject(last, buffer, offset);
}

void TransferChannel::FileHeader::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(relativePath, buffer, offset);
    DeserializeObject(fileSize, buffer, offset);
    DeserializeObject(last, buffer, offset);
}

size_t TransferChannel::FileHeader::GetSerializedSize() const {
    return GetObjectSerializedSize(relativePath) + GetObjectSerializedSize(fileSize) + GetObjectSerializedSize(last);
}

asio::awaitable<void> TransferChannel::CoReceiveFileData(const size_t expectedSize, FileSink sink) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    const std::shared_ptr<SSLSocket> socket = std::atomic_load(&m_socket);
    if (!socket || m_connectionState.load() != ConnectionState::CONNECTED || m_receive.exchange(true)) {
        throw std::runtime_error("Transfer channel is unavailable for streaming");
    }
    struct ResetFlag { std::atomic<bool>& flag; ~ResetFlag() { flag.store(false); } } reset{m_receive};
    m_progress.store(0);
    m_currentFileProgress.store(0);
    m_currentFileSize.store(expectedSize);
    try {
        FileHeader header{};
        std::vector<uint8_t> bytes(header.GetSerializedSize());
        co_await asio::async_read(*socket, asio::buffer(bytes), asio::use_awaitable);
        size_t offset = 0;
        header.Deserialize(bytes, offset);
        if (header.fileSize != expectedSize || !header.relativePath.empty() || !header.last) {
            throw std::runtime_error("Remote file changed while preparing the transfer");
        }
        size_t received = 0;
        while (received < expectedSize) {
            const size_t count = std::min(m_bufferIn.size(), expectedSize - received);
            co_await asio::async_read(*socket, asio::buffer(m_bufferIn.data(), count), asio::use_awaitable);
            co_await sink(std::span<const uint8_t>(m_bufferIn.data(), count));
            received += count;
            m_progress.store(received);
            m_currentFileProgress.store(received);
        }
    } catch (...) {
        // The stream cannot be reused after a partial frame.
        asio::co_spawn(m_strand, [self]() -> asio::awaitable<void> { co_await self->CoDisconnect(); }, asio::detached);
        throw;
    }
}
