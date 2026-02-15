#include <TransferChannel.h>
#include <fstream>
#include <zstd.h>

constexpr int FILE_COMPRESSION_LEVEL = 3;

TransferChannel::TransferChannel(const std::shared_ptr<SSLContext>& sslContext, IOContext& context) : m_context(context), m_socket(nullptr), m_sslContext(sslContext), m_bufferIn(ZSTD_CStreamInSize()), m_bufferOut(ZSTD_CStreamOutSize()) { }

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

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        co_await asio::async_connect(m_socket->lowest_layer(), std::initializer_list<TCPEndpoint>{endpoint}, asio::use_awaitable);
        co_await m_socket->async_handshake(SSLStreamBase::client, asio::use_awaitable);

        Debug::Log("Accepted TLS connection to {}:{}", m_socket->lowest_layer().remote_endpoint().address().to_string(), m_socket->lowest_layer().remote_endpoint().port());
        m_connectionState.store(ConnectionState::CONNECTED);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> TransferChannel::Seek(AwaitableFlag& flag, uint16_t& port) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        m_connectionState.store(ConnectionState::CONNECTING);

        co_await CleanupConnection();
        m_socket = std::make_unique<SSLSocket>(m_context, *m_sslContext);

        TCPAcceptor acceptor(m_context);
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
        co_spawn(m_context, Disconnect(), asio::detached);
    }
}

asio::awaitable<void> TransferChannel::Receive(const std::filesystem::path destination) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            co_return;
        }

        // Prevent concurrent operations if needed, similar to Send
        if (m_receive.load()) {
            co_return;
        }
        m_receive.store(true);

        std::ofstream fileStream(destination, std::ios::binary);
        if (!fileStream.is_open()) {
            Debug::LogError("Failed to open destination file");
            m_receive.store(false);
            co_return;
        }

        const std::unique_ptr<ZSTD_DStream, decltype(&ZSTD_freeDStream)> dStream(ZSTD_createDStream(), &ZSTD_freeDStream);
        size_t initResult = ZSTD_initDStream(dStream.get());
        if (ZSTD_isError(initResult)) {
            throw std::runtime_error("ZSTD Init failed");
        }

        m_progress.store(0);

        size_t zstdRet = 1;
        while (zstdRet != 0) {
            size_t bytesRead = co_await m_socket->async_read_some(asio::buffer(m_bufferIn), asio::use_awaitable);

            ZSTD_inBuffer input = { m_bufferIn.data(), bytesRead, 0 };

            while (input.pos < input.size) {
                ZSTD_outBuffer output = { m_bufferOut.data(), m_bufferOut.size(), 0 };

                zstdRet = ZSTD_decompressStream(dStream.get(), &output, &input);

                if (ZSTD_isError(zstdRet)) {
                    throw std::runtime_error(std::string("ZSTD Decompression failed: ") + ZSTD_getErrorName(zstdRet));
                }

                if (output.pos > 0) {
                    m_progress.fetch_add(output.pos);
                    fileStream.write(reinterpret_cast<char*>(m_bufferOut.data()), output.pos);
                }

                if (zstdRet == 0) {
                    break;
                }
            }
        }

        fileStream.close();
        Debug::Log("File received successfully");

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        co_spawn(m_context, Disconnect(), asio::detached);
    } catch (std::exception& ex) {
        Debug::LogError(ex.what());
        co_spawn(m_context, Disconnect(), asio::detached);
    }

    m_receive.store(false);
}
asio::awaitable<void> TransferChannel::ReceiveDirectory(const std::filesystem::path& path, uint64_t length) {
    co_return;
}

asio::awaitable<void> TransferChannel::SendDirectory(std::filesystem::path path) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            co_return;
        }

        if (!std::filesystem::exists(path)) {
            Debug::LogError("Path doesnt exists");
            co_return;
        }

        if (m_send.load()) {
            co_return;
        }

        m_send.store(true);

        std::ifstream fileStream(path, std::ios::binary);
        m_progress.store(0);


    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        co_spawn(m_context, Disconnect(), asio::detached);
    }

    m_send.store(false);
}

asio::awaitable<void> TransferChannel::Send(const std::filesystem::path file) {
    const std::shared_ptr<TransferChannel> self = shared_from_this();

    try {
        if (m_connectionState.load() != ConnectionState::CONNECTED) {
            co_return;
        }

        if (!std::filesystem::exists(file)) {
            Debug::LogError("Path doesnt exists");
            co_return;
        }

        if (m_send.load()) {
            co_return;
        }

        m_send.store(true);

        const size_t length = std::filesystem::file_size(file);
        std::ifstream fileStream(file, std::ios::binary);
        m_progress.store(0);

        const std::unique_ptr<ZSTD_CStream, decltype(&ZSTD_freeCStream)> cStream(ZSTD_createCStream(), &ZSTD_freeCStream);
        ZSTD_initCStream(cStream.get(), FILE_COMPRESSION_LEVEL);

        size_t totalRead = 0;
        while (totalRead < length) {
            const size_t toRead = std::min(m_bufferIn.size(), length - totalRead);
            fileStream.read(reinterpret_cast<char*>(m_bufferIn.data()), toRead);
            totalRead += toRead;

            ZSTD_inBuffer input = { m_bufferIn.data(), toRead, 0 };

            while (input.pos < input.size) {
                ZSTD_outBuffer output = { m_bufferOut.data(), m_bufferOut.size(), 0 };

                size_t const remaining = ZSTD_compressStream(cStream.get(), &output, &input);
                if (ZSTD_isError(remaining)) {
                    throw std::runtime_error("ZSTD Compression failed");
                }

                if (output.pos > 0) {
                    co_await asio::async_write(*m_socket, asio::buffer(output.dst, output.pos), asio::use_awaitable);
                }
            }

            m_progress.store(totalRead);
        }

        size_t remaining = 0;
        do {
            ZSTD_outBuffer output = { m_bufferOut.data(), m_bufferOut.size(), 0 };
            remaining = ZSTD_endStream(cStream.get(), &output);

            if (output.pos > 0) {
                co_await asio::async_write(*m_socket, asio::buffer(output.dst, output.pos), asio::use_awaitable);
            }

        } while (remaining != 0);

    } catch (std::system_error& error) {
        HandleAsioError(error.code());
        co_spawn(m_context, Disconnect(), asio::detached);
    }

    m_send.store(false);
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
