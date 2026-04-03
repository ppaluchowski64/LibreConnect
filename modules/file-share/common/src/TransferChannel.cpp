#include <TransferChannel.h>
#include <fstream>
#include <Packable.h>
#include <fmt/os.h>
#include <ConnectionManager.h>
#include <ThreadPool.h>

static constexpr size_t TRANSFER_BUFFER_SIZE = 1024 * 256;

TransferChannel::TransferChannel() : m_socket(nullptr), m_bufferIn(TRANSFER_BUFFER_SIZE), m_bufferOut(TRANSFER_BUFFER_SIZE) {
    Debug::Log("TransferChannel created or smth");
}

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

        FileHeader fileHeader{};
        std::vector<uint8_t> buffer(1024);
        std::vector<uint8_t> headerBuffer(sizeof(size_t));

        do {
            size_t fileHeaderSize = 0;
            co_await asio::async_read(*m_socket, asio::mutable_buffer(headerBuffer.data(), headerBuffer.size()), asio::use_awaitable);

            size_t offset = 0;
            DeserializeObject(fileHeaderSize, headerBuffer, offset);

            if (fileHeaderSize > buffer.size()) {
                Debug::LogError("The header size ({}) is exceeding the buffer size ({})", fileHeaderSize, headerBuffer.size());
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

            const std::filesystem::path filePath = path / fileHeader.relativePath;
            co_await Receive(filePath, fileHeader.fileSize);

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
            fileHeader.relativePath = std::filesystem::relative(currentFilePath, path).string();
            fileHeader.last = isLast;

            std::vector<uint8_t> buffer(fileHeader.GetSerializedSize() + sizeof(size_t));
            size_t offset = 0;
            SerializeObject(fileHeader.GetSerializedSize(), buffer, offset);
            fileHeader.Serialize(buffer, offset);

            co_await asio::async_write(*m_socket, asio::buffer(buffer), asio::use_awaitable);
            co_await Send(currentFilePath);
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

    co_await Receive(path, fileHeader.fileSize);

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
    co_await Send(path);

    m_send.store(false);
}

asio::awaitable<bool> TransferChannel::Receive(const std::filesystem::path destination, size_t length) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        std::filesystem::create_directories(destination.parent_path());

        std::ofstream fileStream(destination, std::ios::binary);
        if (!fileStream.is_open()) {
            Debug::LogError("Failed to open destination file");
            co_return false;
        }

        size_t offset = 0;

        while (offset < length) {
            const size_t bufferSize = std::min(m_bufferIn.size(), length - offset);
            offset += bufferSize;

            asio::mutable_buffer buffer(m_bufferIn.data(), bufferSize);
            co_await asio::async_read(*m_socket, buffer, asio::use_awaitable);

            fileStream.write(reinterpret_cast<const char*>(m_bufferIn.data()), bufferSize);
            m_progress.fetch_add(bufferSize);
        }

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
