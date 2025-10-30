#include <ConnectionManager.h>
#include <CryptographicIdentityManager.h>
#include <QCoreApplication>
#include <Events.h>
#include <DeviceInfo.h>

ConnectionManager* ConnectionManager::s_instance{nullptr};
std::mutex         ConnectionManager::s_mutex{};
std::atomic<bool>  ConnectionManager::s_isInitialized{false};


void ConnectionManager::Connect(TCPEndpoint&& endpoint) {
    if (!s_isInitialized.load()) {
        std::lock_guard<std::mutex> lock(s_mutex);
        Initialize();
    }

    Debug::Log("Started Connecting");

    if (s_instance->m_currentSSLContextCurrentMode != SSLContextCurrentMode::CLIENT) {
        s_instance->m_sslContext = CreateSSLContext(false);
    }

    s_instance->m_primaryConnection->Connect(std::forward<TCPEndpoint>(endpoint), s_instance->m_sslContext);
}

void ConnectionManager::Seek(TCPEndpoint&& endpoint) {
    if (!s_isInitialized.load()) {
        std::lock_guard<std::mutex> lock(s_mutex);
        Initialize();
    }

    Debug::Log("Started Seeking");

    if (s_instance->m_currentSSLContextCurrentMode != SSLContextCurrentMode::SERVER) {
        s_instance->m_sslContext = CreateSSLContext(true);
    }

    s_instance->m_seekingEndpoint = endpoint;
    s_instance->m_primaryConnection->Seek(std::forward<TCPEndpoint>(endpoint), s_instance->m_sslContext);
}

void ConnectionManager::Disconnect(const std::error_code errorCode) {
    if (!s_isInitialized.load()) {
        std::lock_guard<std::mutex> lock(s_mutex);
        Initialize();
    }

    s_instance->m_primaryConnection->Disconnect(false);

    DisconnectedEvent* event = new DisconnectedEvent(errorCode);
    SendEvent(event);
}

std::shared_ptr<SSLContext> ConnectionManager::CreateSSLContext(const bool isServer) {
    const std::filesystem::path rootPath{"certs"};
    const std::string privateKeyPath{"certs/privateKey.key"};
    const std::string certificatePath{"certs/certificate.crt"};

    if (!CryptographicIdentityManager::IsCertificateValid(rootPath)) {
        CryptographicIdentityManager::GenerateCertificate(rootPath);
    }

    std::shared_ptr<SSLContext> context = std::make_shared<SSLContext>(isServer ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);
    context->set_options(
        SSLContext::default_workarounds |
        SSLContext::no_sslv2 |
        SSLContext::no_sslv3 |
        SSLContext::no_tlsv1 |
        SSLContext::no_tlsv1_1
    );

    context->use_certificate_chain_file(certificatePath);
    context->use_private_key_file(privateKeyPath, SSLContext::pem);

    return context;
}

void ConnectionManager::RunContext() {
    s_instance->m_context.run();
}

[[noreturn]] asio::awaitable<void> ConnectionManager::CoProcessPackages() {
    const std::shared_ptr<AwaitableFlag> receiveFlag = m_primaryConnection->GetReceiveFlag();

    while (true) {
        co_await receiveFlag->Wait();
        receiveFlag->Reset();

        std::optional<std::unique_ptr<Package<PC_PackageType>>> packageOptional = m_primaryConnection->GetPackage();
        while (packageOptional.has_value()) {
            std::unique_ptr<Package<PC_PackageType>> value = std::move(packageOptional.value());
            const PackageHeader header = value->GetHeader();

            if ((header.flags & PackageFlag::REQUEST_WITH_RESPONSE) != 0) {
                size_t requestID = value->GetValue<size_t>();
                std::optional<RequestCallbackType> callbackOptional = m_requestCallbackMap.Get(requestID);

                if (callbackOptional.has_value()) {
                    asio::post(m_context, [callback = std::move(callbackOptional.value()), package = std::move(value)]() mutable {
                        callback(std::move(package));
                    });
                }

            } else {
                PC_PackageType type = static_cast<PC_PackageType>(header.type);
                std::optional<RequestCallbackType> callbackOptional = m_responseHandlerMap.Get(type);

                if (callbackOptional.has_value()) {
                    asio::post(m_context, [callback = std::move(callbackOptional.value()), package = std::move(value)]() mutable {
                        callback(std::move(package));
                    });
                }
            }

            packageOptional = m_primaryConnection->GetPackage();
        }
    }
}

void ConnectionManager::AddResponseHandler(const PC_PackageType type, RequestCallbackType&& handler) {
    if (!s_isInitialized.load()) {
        std::lock_guard<std::mutex> lock(s_mutex);
        Initialize();
    }

    s_instance->m_responseHandlerMap.InsertOrAssign(type, std::forward<RequestCallbackType>(handler));
}

void ConnectionManager::PairDevice(CallbackWithResult&& callback) {
    // if (!s_isInitialized.load()) {
    //     std::lock_guard<std::mutex> lock(s_mutex);
    //     Initialize();
    // }
    //
    // SendRequest(PC_PackageType::PAIR_REQUEST, [callback = std::move(callback)](std::unique_ptr<Package<PC_PackageType>>&& package) mutable {
    //     const PackageTypeInt type = package->GetHeader().type;
    //
    //     if (PackageTypeIntHasFlag(type, static_cast<PackageTypeInt>(PC_PackageType::PAIR_REQUEST_ACCEPTED))) {
    //         std::string publicKey = package->GetValue<std::string>();
    //         DeviceInfo deviceInfo = package->GetValue<DeviceInfo>();
    //         callback(true);
    //     } else {
    //         callback(false);
    //     }
    //
    // }, std::move(CryptographicIdentityManager::GetPublicKey()), DeviceInfo::GetThisDeviceInfo());
}

TCPEndpoint ConnectionManager::GetSeekEndpoint() {
    if (!s_isInitialized.load()) {
        std::lock_guard<std::mutex> lock(s_mutex);
        Initialize();
    }

    return s_instance->m_seekingEndpoint;
}

ConnectionManager::ConnectionManager() : m_workGuard(asio::make_work_guard(m_context)) {
    s_instance = this;

    for (int i = 0; i < 2; i++) {
        m_threads.emplace_back(RunContext);
    }

    m_primaryConnection = PrimaryConnection::Create(m_context);
    asio::co_spawn(m_context, CoProcessPackages(), asio::detached);
}

void ConnectionManager::Initialize() {
    s_instance = new ConnectionManager();
    s_isInitialized.store(true);
}

void ConnectionManager::SendEvent(QEvent* event) {
    if (!s_isInitialized.load()) {
        std::lock_guard<std::mutex> lock(s_mutex);
        Initialize();
    }

    std::lock_guard<std::mutex> lock(s_mutex);

    const std::vector<QObject*>& objects = s_instance->m_eventObjects;

    if (objects.empty()) {
        delete event;
    }

    QCoreApplication::postEvent(objects[0], event);
    for (int i = 1; i < objects.size(); i++) {
        QCoreApplication::postEvent(objects[i], event->clone());
    }
}
