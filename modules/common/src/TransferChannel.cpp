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

TransferChannel::TransferChannel() : m_socket(nullptr), m_bufferIn(TRANSFER_BUFFER_SIZE), m_bufferOut(TRANSFER_BUFFER_SIZE) {}

size_t TransferChannel::FetchTransferProgress() const {
    return m_progress.load();
}

bool TransferChannel::IsUsed(const bool outTransfer) const {
    return outTransfer ? m_send.load() : m_receive.load();
}

ConnectionState TransferChannel::GetConnectionState() const {
    return m_connectionState.load();
}

asio::awaitable<void> TransferChannel::Connect(TCPEndpoint endpoint) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    IOContext& context = ThreadPool::GetContext();
    m_sslContext = ConnectionManager::GetSSLContextClient();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        m_socket = std::make_unique<SSLSocket>(context, *m_sslContext);

        co_await asio::async_connect(m_socket->lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);
        co_await m_socket->async_handshake(SSLStreamBase::client, asio::use_awaitable);

        Debug::Log("Accepted TLS connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> TransferChannel::Seek(AwaitableFlag& flag, uint16_t& port) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    IOContext& context = ThreadPool::GetContext();
    m_sslContext = ConnectionManager::GetSSLContextServer();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        m_socket = std::make_unique<SSLSocket>(context, *m_sslContext);

        TCPAcceptor acceptor(context);
        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 0);

        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        port = acceptor.local_endpoint().port();
        flag.Signal();

        co_await acceptor.async_accept(m_socket->lowest_layer(), asio::use_awaitable);
        co_await m_socket->async_handshake(SSLStreamBase::server, asio::use_awaitable);

        Debug::Log("Accepted TLS connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> TransferChannel::ReceiveDirectory(std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    if (m_connectionState.load() != ConnectionState::CONNECTED || m_receive.load()) {
        co_return;
    }

    m_receive.store(true);
    m_progress.store(0);

    try {
        std::filesystem::create_directories(path);
        std::error_code errorCode;
        const std::filesystem::path canonicalBasePath = std::filesystem::weakly_canonical(path, errorCode);
        if (errorCode) {
            Debug::LogError("Failed to canonicalize destination directory '{}' ({})", path.string(), errorCode.message());
            m_receive.store(false);
            co_return;
        }

        FileHeader fileHeader{};
        std::vector<uint8_t> buffer(1024);
        std::vector<uint8_t> headerBuffer(sizeof(size_t));

        do {
            size_t fileHeaderSize = 0;
            co_await asio::async_read(*m_socket, asio::mutable_buffer(headerBuffer.data(), headerBuffer.size()), asio::use_awaitable);

            size_t offset = 0;
            DeserializeObject(fileHeaderSize, headerBuffer, offset);

            if (fileHeaderSize > buffer.size()) {
                Debug::LogError("The header size ({}) is exceeding the buffer size ({})", fileHeaderSize, buffer.size());
                m_receive.store(false);
                co_return;
            }

            asio::mutable_buffer mutableBuffer(buffer.data(), fileHeaderSize);
            co_await asio::async_read(*m_socket, mutableBuffer, asio::use_awaitable);

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
                co_await DrainSocketBytes(*m_socket, m_bufferIn, fileHeader.fileSize);
                continue;
            }

            const bool received = co_await Receive(filePath.value(), fileHeader.fileSize);
            if (!received) {
                m_receive.store(false);
                co_return;
            }

        } while (!fileHeader.last);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), Disconnect(), asio::detached);
    }

    m_receive.store(false);
}

asio::awaitable<void> TransferChannel::SendDirectory(const std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    if (m_connectionState.load() != ConnectionState::CONNECTED || m_send.load()) co_return;

    m_send.store(true);

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

            co_await asio::async_write(*m_socket, asio::buffer(buffer), asio::use_awaitable);
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

            co_await asio::async_write(*m_socket, asio::buffer(buffer), asio::use_awaitable);
            const bool sent = co_await Send(currentFilePath);
            if (!sent) {
                m_send.store(false);
                co_return;
            }
        }

    } catch (const std::exception& e) {
        Debug::LogError("Directory transfer failed");
        asio::co_spawn(ThreadPool::GetContext(), Disconnect(), asio::detached);
    }

    m_send.store(false);
}

asio::awaitable<void> TransferChannel::ReceiveFile(std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    if (m_connectionState.load() != ConnectionState::CONNECTED || m_receive.load()) {
        co_return;
    }

    m_receive.store(true);
    m_progress.store(0);

    FileHeader fileHeader{};
    size_t offset = 0;

    std::vector<uint8_t> buffer(fileHeader.GetSerializedSize());
    const asio::mutable_buffer mutableBuffer(buffer.data(), buffer.size());

    co_await asio::async_read(*m_socket, mutableBuffer, asio::use_awaitable);
    fileHeader.Deserialize(buffer, offset);

    const bool received = co_await Receive(path, fileHeader.fileSize);
    if (!received) {
        m_receive.store(false);
        co_return;
    }

    m_receive.store(false);
}

asio::awaitable<void> TransferChannel::SendFile(std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    if (m_connectionState.load() != ConnectionState::CONNECTED || m_send.load()) {
        co_return;
    }

    m_send.store(true);
    m_progress.store(0);

    const FileHeader fileHeader{
        "",
        std::filesystem::file_size(path),
        true
    };

    size_t offset = 0;
    std::vector<uint8_t> buffer(fileHeader.GetSerializedSize());

    fileHeader.Serialize(buffer, offset);
    const asio::const_buffer constBuffer(buffer.data(), buffer.size());

    co_await asio::async_write(*m_socket, constBuffer, asio::use_awaitable);
    const bool sent = co_await Send(path);
    if (!sent) {
        m_send.store(false);
        co_return;
    }

    m_send.store(false);
}

asio::awaitable<void> TransferChannel::SendDirectoryEntries(std::vector<FileEntry>&& entries) {
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

asio::awaitable<void> TransferChannel::ReceiveDirectoryEntries(std::vector<FileEntry>& entries) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();
    if (m_connectionState.load() != ConnectionState::CONNECTED || m_receive.load()) {
        Debug::LogWarning(
            "TransferChannel: ReceiveDirectoryEntries skipped. State: {}, ReceiveInUse: {}",
            static_cast<int>(m_connectionState.load()),
            m_receive.load()
        );
        co_return;
    }

    m_receive.store(true);
    m_progress.store(0);
    constexpr size_t frameHeaderSize = sizeof(size_t) * 2;

    size_t totalCount = 0;
    size_t offset = 0;

    {
        const asio::mutable_buffer buffer(m_bufferIn.data(), sizeof(size_t));
        co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);
        DeserializeObject(totalCount, m_bufferIn, offset);
    }
    Debug::Log("TransferChannel: ReceiveDirectoryEntries started. Expected entries: {}", totalCount);

    entries.reserve(totalCount);
    const size_t expectedTotalCount = totalCount;
    size_t receivedCount = 0;
    size_t chunkCount = 0;

    while (true) {
        const asio::mutable_buffer headerBuffer(m_bufferIn.data(), frameHeaderSize);
        co_await asio::async_read(*m_socket, headerBuffer, asio::use_awaitable);

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
            co_await asio::async_read(*m_socket, payloadBuffer, asio::use_awaitable);
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
            co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);

            fileStream.write(reinterpret_cast<const char*>(m_bufferIn.data()), bufferSize);
            if (!fileStream.good()) {
                Debug::LogError("Failed while writing destination file '{}'", destination.string());
                co_await Disconnect();
                co_return false;
            }

            offset += bufferSize;
            m_progress.fetch_add(bufferSize);
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), Disconnect(), asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<bool> TransferChannel::SendBuffer(const size_t size) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        const asio::const_buffer buffer(m_bufferOut.data(), size);
        co_await asio::async_write(*m_socket, buffer, asio::use_awaitable);
        m_progress.fetch_add(size);
    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), Disconnect(), asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<bool> TransferChannel::Send(const std::filesystem::path file) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        std::ifstream fileStream(file, std::ios::binary);
        if (!fileStream.is_open()) {
            Debug::LogError("Failed to open file");
            co_return false;
        }

        const size_t length = std::filesystem::file_size(file);
        size_t offset = 0;

        while (offset < length) {
            const size_t bufferSize = std::min(m_bufferOut.size(), length - offset);
            offset += bufferSize;

            fileStream.read(reinterpret_cast<char*>(m_bufferOut.data()), bufferSize);
            asio::const_buffer buffer(m_bufferOut.data(), bufferSize);

            co_await asio::async_write(*m_socket, buffer, asio::use_awaitable);

            m_progress.fetch_add(bufferSize);
        }

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        asio::co_spawn(ThreadPool::GetContext(), Disconnect(), asio::detached);
        co_return false;
    }

    co_return true;
}

asio::awaitable<void> TransferChannel::Disconnect() {
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
    co_await CleanupSSLSocket(m_socket.get());
    m_socket.reset();
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
