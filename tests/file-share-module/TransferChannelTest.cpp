#include <TransferChannel.h>
#include <ThreadPool.h>
#include <fstream>
#include <CryptographicIdentityManager.h>

std::shared_ptr<SSLContext> CreateSSLContext(const bool isServer) {
    constexpr std::string_view privateKeyPath{"certs/local/pkey.key"};
    constexpr std::string_view certificatePath{"certs/local/cert.key"};

    if (!CryptographicIdentityManager::IsCertificateValid(certificatePath)) {
        CryptographicIdentityManager::GenerateCertificate(privateKeyPath, certificatePath);
    }

    std::shared_ptr<SSLContext> context = std::make_shared<SSLContext>(isServer ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);
    context->set_verify_mode(asio::ssl::verify_none);

    context->set_options(
        SSLContext::default_workarounds |
        SSLContext::no_sslv2 |
        SSLContext::no_sslv3 |
        SSLContext::no_tlsv1 |
        SSLContext::no_tlsv1_1
    );

    try {
        context->use_certificate_chain_file(certificatePath.data());
        context->use_private_key_file(privateKeyPath.data(), SSLContext::pem);
    } catch (const std::system_error& e) {
        Debug::LogError("Failed to load SSL certs: {}", e.what());
    }

    return context;
}

void CreateTestFile(const size_t size) {
    std::ofstream file("test.txt", std::ios::binary);
    constexpr size_t bufferSize = 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    for (size_t i = 0; i < bufferSize; ++i) buffer[i] = '0' + (i % 9);

    size_t written = 0;
    while (written < size) {
        const size_t toWrite = std::min(bufferSize, size - written);
        file.write(buffer.data(), toWrite);
        written += toWrite;
    }
}

bool VerifyTestFile(const std::string& original, const std::string& received) {
    std::ifstream f1(original, std::ios::binary);
    std::ifstream f2(received, std::ios::binary);

    if (!f1.is_open() || !f2.is_open()) return false;
    if (std::filesystem::file_size(original) != std::filesystem::file_size(received)) return false;

    constexpr size_t bufferSize = 1024 * 1024;
    std::vector<char> buf1(bufferSize);
    std::vector<char> buf2(bufferSize);

    while (f1 && f2) {
        f1.read(buf1.data(), bufferSize);
        f2.read(buf2.data(), bufferSize);
        if (f1.gcount() != f2.gcount()) return false;
        if (!std::equal(buf1.begin(), buf1.begin() + f1.gcount(), buf2.begin())) return false;
    }

    return true;
}

asio::awaitable<void> Execute() {
    IOContext& context = ThreadPool::GetContext();
    std::shared_ptr<SSLContext> serverSSLContext = CreateSSLContext(true);
    std::shared_ptr<SSLContext> clientSSLContext = CreateSSLContext(false);

    constexpr size_t TEST_FILE_SIZE = 1024 * 1024 * 1024;
    CreateTestFile(TEST_FILE_SIZE);

    std::shared_ptr<TransferChannel> serverChannel = std::make_shared<TransferChannel>(serverSSLContext, context);
    std::shared_ptr<TransferChannel> clientChannel = std::make_shared<TransferChannel>(clientSSLContext, context);

    AwaitableFlag flag(context.get_executor());
    uint16_t port;

    asio::co_spawn(context, serverChannel->Seek(flag, port), asio::detached);

    co_await flag.Wait();
    co_await clientChannel->Connect(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), port));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const size_t size = std::filesystem::file_size("test.txt");
    asio::co_spawn(context, serverChannel->Send(std::filesystem::path("./test.txt")), asio::detached);
    auto fut = asio::co_spawn(context, clientChannel->Receive(std::filesystem::path("./result.txt")), asio::use_future);

    size_t currentProgress = 0;

    auto start = std::chrono::high_resolution_clock::now();

    while (fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        Debug::Log("Progress: {} MB", currentProgress / (1024.f * 1024));
        currentProgress = clientChannel->FetchTransferProgress();

        asio::steady_timer timer(context.get_executor());
        timer.expires_after(std::chrono::milliseconds(100));

        co_await timer.async_wait();
    }
    Debug::Log("Progress: {} MB", currentProgress / (1024.f * 1024));
    auto end = std::chrono::high_resolution_clock::now();

    Debug::Log("Result: {}MBs", TEST_FILE_SIZE / std::chrono::duration<double>(end - start).count() / (1024.f * 1024));
}

int main() {
    IOContext& context = ThreadPool::GetContext();
    auto fut = asio::co_spawn(context, Execute(), asio::use_future);
    fut.get();
}