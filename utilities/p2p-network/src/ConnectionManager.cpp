#include <ConnectionManager.h>
#include <CryptographicIdentityManager.h>
#include <QCoreApplication>
#include <Events.h>
#include <DeviceInfo.h>
#include <QPointer>

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

    s_instance->m_primaryConnection->Disconnect(std::error_code{}, false);

    const std::unique_ptr<QEvent> event = std::make_unique<DisconnectedEvent>(errorCode);
    SendEvent(event);
}

std::shared_ptr<SSLContext> ConnectionManager::CreateSSLContext(const bool isServer, const uuid targetUUID) {
    const std::string targetCertificatePath{"certs/" + boost::uuids::to_string(targetUUID) + "/cert.key"};
    constexpr std::string_view privateKeyPath{"certs/local/pkey.key"};
    constexpr std::string_view certificatePath{"certs/local/cert.key"};

    if (!CryptographicIdentityManager::IsCertificateValid(certificatePath)) {
        CryptographicIdentityManager::GenerateCertificate(privateKeyPath, certificatePath);
    }

    std::shared_ptr<SSLContext> context = std::make_shared<SSLContext>(isServer ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);

    if (targetUUID != boost::uuids::nil_uuid() && std::filesystem::exists(targetCertificatePath)) {
        context->set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
        context->load_verify_file(targetCertificatePath.c_str());
    } else {
        context->set_verify_mode(asio::ssl::verify_none);
    }

    context->set_options(
        SSLContext::default_workarounds |
        SSLContext::no_sslv2 |
        SSLContext::no_sslv3 |
        SSLContext::no_tlsv1 |
        SSLContext::no_tlsv1_1
    );

    context->use_certificate_chain_file(certificatePath.data());
    context->use_private_key_file(privateKeyPath.data(), SSLContext::pem);

    return context;
}

void ConnectionManager::RunContext() {
    s_instance->m_context.run();
}

asio::awaitable<void> ConnectionManager::CoProcessPackages() {
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

void ConnectionManager::AddEventListener(const QPointer<QObject>& object) {
    if (!s_isInitialized.load()) {
        std::lock_guard<std::mutex> lock(s_mutex);
        Initialize();
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_instance->m_eventObjects.push_back(object);
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

void ConnectionManager::SendEvent(const std::unique_ptr<QEvent>& event) {
    std::vector<QPointer<QObject>> targets;

    {
        std::lock_guard<std::mutex> lock(s_mutex);

        if (!s_isInitialized.load()) {
            Initialize();
        }

        auto& objects = s_instance->m_eventObjects;
        std::erase_if(objects, [](const QPointer<QObject>& obj) {
            return obj.isNull();
        });

        if (objects.empty()) {
            return;
        }

        targets = objects;
    }

    for (const auto& obj : targets) {
        QMetaObject::invokeMethod(obj, [event = event->clone(), obj]() {
            if (obj.isNull()) return;
            obj->event(event);
        });
    }
}