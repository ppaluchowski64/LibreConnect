#include <TransferChannel.h>
#include <ThreadPool.h>
#include <Packable.h>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <chrono>
#include <future>
#include <thread>

// The fixture supplies a loopback TLS peer without starting discovery or pairing.
class TransferChannelTestAccess {
  public:
    static std::shared_ptr<SSLSocket> Attach(const std::shared_ptr<TransferChannel> &channel, SSLContext_ &context) {
        const std::shared_ptr<SSLSocket> socket = std::make_shared<SSLSocket>(channel->m_strand, context);
        std::atomic_store(&channel->m_socket, socket);
        channel->m_connectionState.store(ConnectionState::CONNECTED);
        return socket;
    }
};

class TransferChannelDisconnectTest : public testing::Test {
  protected:
    static IOContext& TestContext() {
        Debug::SetSettings({"build/transfer-test-logs", "TransferTests", 1048576, 4, 86400});
        return ThreadPool::GetContext();
    }
    IOContext &context = TestContext();
    SSLContext_ clientContext{SSLContext_::tls_client};
    SSLContext_ serverContext{SSLContext_::tls_server};
    std::shared_ptr<TransferChannel> channel;
    std::shared_ptr<SSLSocket> client;
    std::unique_ptr<SSLSocket> server;

    void SetUp() override {
        const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_RSA_gen(2048), EVP_PKEY_free);
        ASSERT_NE(key, nullptr);
        const std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
        ASSERT_NE(cert, nullptr);
        X509_set_version(cert.get(), 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600);
        X509_set_pubkey(cert.get(), key.get());
        X509_NAME *name = X509_get_subject_name(cert.get());
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("localhost"), -1,
                                   -1, 0);
        X509_set_issuer_name(cert.get(), name);
        ASSERT_GT(X509_sign(cert.get(), key.get(), EVP_sha256()), 0);
        ASSERT_EQ(SSL_CTX_use_certificate(serverContext.native_handle(), cert.get()), 1);
        ASSERT_EQ(SSL_CTX_use_PrivateKey(serverContext.native_handle(), key.get()), 1);
        clientContext.set_verify_mode(asio::ssl::verify_none);
        channel = std::make_shared<TransferChannel>();
        client = TransferChannelTestAccess::Attach(channel, clientContext);
        server = std::make_unique<SSLSocket>(context, serverContext);
        TCPAcceptor acceptor(context, TCPEndpoint(asio::ip::address_v4::loopback(), 0));
        std::future<void> accepted = std::async(std::launch::async, [&]() {
            acceptor.accept(server->next_layer());
            server->handshake(SSLStreamBase::server);
        });
        client->next_layer().connect(acceptor.local_endpoint());
        client->handshake(SSLStreamBase::client);
        accepted.get();
    }

    void TearDown() override {
        std::error_code ignored;
        if (server)
            server->lowest_layer().close(ignored);
        if (channel)
            asio::co_spawn(context, channel->Disconnect(), asio::use_future).get();
    }

    void SendHeader(size_t size) {
        const std::string relativePath;
        std::vector<uint8_t> header(GetObjectSerializedSize(relativePath) + sizeof(size_t) + sizeof(bool));
        size_t offset = 0;
        SerializeObject(relativePath, header, offset);
        SerializeObject(size, header, offset);
        SerializeObject(true, header, offset);
        asio::write(*server, asio::buffer(header));
    }
};

TEST_F(TransferChannelDisconnectTest, PeerDisappearingDuringFileFailsAndReleasesChannel) {
    constexpr size_t chunkSize = 256 * 1024;
    std::promise<void> receivedChunk;
    std::future<void> chunk = receivedChunk.get_future();
    std::future<void> transfer =
        asio::co_spawn(context,
                       channel->ReceiveFileData(chunkSize * 2,
                                                [&receivedChunk](std::span<const uint8_t>) -> asio::awaitable<void> {
                                                    receivedChunk.set_value();
                                                    co_return;
                                                }),
                       asio::use_future);
    SendHeader(chunkSize * 2);
    const std::vector<uint8_t> bytes(chunkSize, 42);
    asio::write(*server, asio::buffer(bytes));
    ASSERT_EQ(chunk.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    std::error_code ignored;
    server->lowest_layer().close(ignored);
    ASSERT_EQ(transfer.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_THROW(transfer.get(), std::system_error);
    EXPECT_FALSE(channel->IsUsed(false));
}

TEST_F(TransferChannelDisconnectTest, LocalDisconnectCancelsPendingReadWithoutDestroyingActiveSocket) {
    std::future<void> transfer = asio::co_spawn(
        context, channel->ReceiveFileData(1024, [](std::span<const uint8_t>) -> asio::awaitable<void> { co_return; }),
        asio::use_future);
    SendHeader(1024);
    std::future<void> disconnect = asio::co_spawn(context, channel->Disconnect(), asio::use_future);
    ASSERT_EQ(disconnect.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_NO_THROW(disconnect.get());
    ASSERT_EQ(transfer.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_THROW(transfer.get(), std::exception);
    EXPECT_FALSE(channel->IsUsed(false));
    EXPECT_EQ(channel->GetConnectionState(), ConnectionState::DISCONNECTED);
}

TEST_F(TransferChannelDisconnectTest, FileBytesArriveIntactWithoutDiskStaging) {
    const std::vector<uint8_t> bytes(300 * 1024, 73);
    std::vector<uint8_t> received;
    std::future<void> transfer =
        asio::co_spawn(context,
                       channel->ReceiveFileData(bytes.size(),
                                                [&received](std::span<const uint8_t> chunk) -> asio::awaitable<void> {
                                                    received.insert(received.end(), chunk.begin(), chunk.end());
                                                    co_return;
                                                }),
                       asio::use_future);
    SendHeader(bytes.size());
    asio::write(*server, asio::buffer(bytes));
    ASSERT_EQ(transfer.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_NO_THROW(transfer.get());
    EXPECT_EQ(received, bytes);
    EXPECT_EQ(channel->FetchTransferProgress(), bytes.size());
    EXPECT_FALSE(channel->IsUsed(false));
}
