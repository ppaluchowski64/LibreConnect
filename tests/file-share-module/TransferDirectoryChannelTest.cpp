#include <TransferChannel.h>
#include <ThreadPool.h>
#include <fstream>
#include <CryptographicIdentityManager.h>
#include <random>

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

void CreateTestFile(const size_t size, const std::filesystem::path name = "test.txt");

void CreateDirectoryTree(int& left, std::filesystem::path path) {
    constexpr size_t MAX_DIRECTORY_COUNT = 16;
    constexpr size_t FILE_SIZE = 1024 * 1024 * 2;
    constexpr size_t FILE_COUNT = 8;

    static std::random_device rd;
    static std::mt19937 gen(rd());

    std::uniform_int_distribution<> dist(0, 1);
    size_t count = 0;

    while (count < MAX_DIRECTORY_COUNT && left > 0) {
        const int num = dist(gen);
        if (num == 0) break;

        count++;
        left--;

        std::filesystem::path next = path / std::to_string(count);
        std::filesystem::create_directories(next);
        CreateDirectoryTree(left, next);
    }

    for (int i = 0; i < FILE_COUNT; i++) {
        CreateTestFile(FILE_SIZE, path / ("testFile-" + std::to_string(i)));
    }
}

void CreateTestFile(const size_t size, const std::filesystem::path name) {
    std::ofstream file(name, std::ios::binary);
    constexpr size_t bufferSize = 1024 * 1024;
    std::vector<char> buffer(bufferSize, '0');
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

    std::shared_ptr<TransferChannel> serverChannel = std::make_shared<TransferChannel>(serverSSLContext, context);
    std::shared_ptr<TransferChannel> clientChannel = std::make_shared<TransferChannel>(clientSSLContext, context);

    AwaitableFlag flag(context.get_executor());
    uint16_t port;

    asio::co_spawn(context, serverChannel->Seek(flag, port), asio::detached);

    co_await flag.Wait();
    co_await clientChannel->Connect(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), port));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::filesystem::remove_all("result/");

    size_t totalSize = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("test/")) {
        if (entry.is_regular_file()) {
            totalSize += entry.file_size();
        }
    }

    asio::co_spawn(context, serverChannel->SendDirectory(std::filesystem::path("test/")), asio::detached);
    auto fut = asio::co_spawn(context, clientChannel->ReceiveDirectory(std::filesystem::path("result/")), asio::use_future);

    auto start = std::chrono::high_resolution_clock::now();
    size_t currentProgress = 0;

    while (fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        Debug::Log("Progress: {} MB", currentProgress / (1024.f * 1024));
        currentProgress = clientChannel->FetchTransferProgress();

        asio::steady_timer timer(context.get_executor());
        timer.expires_after(std::chrono::milliseconds(100));

        co_await timer.async_wait();
    }
    Debug::Log("Progress: {} MB", currentProgress / (1024.f * 1024));
    auto end = std::chrono::high_resolution_clock::now();

    Debug::Log("Result: {}MBs , total time {}", totalSize / std::chrono::duration<double>(end - start).count() / (1024.f * 1024), std::chrono::duration<double>(end - start).count());
}

int main() {
    std::filesystem::remove_all("./test");

    int left = 20;
    const std::filesystem::path testDir("test/");
    for (; left > 0; left--) {
        const std::filesystem::path dir(testDir / (std::to_string(left) + "/"));

        std::filesystem::create_directories(dir);
        CreateDirectoryTree(left, dir);
    }

    IOContext& context = ThreadPool::GetContext();
    auto fut = asio::co_spawn(context, Execute(), asio::use_future);
    fut.get();
}