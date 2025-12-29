#include <ConnectionManager.h>
#include <CryptographicIdentityManager.h>
#include <QCoreApplication>
#include <Events.h>
#include <DeviceInfo.h>
#include <QPointer>
#include <utility>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <InitialConnection.h>
#include <iostream>
#include <nlohmann/json.hpp>

ConnectionManager* ConnectionManager::s_instance{nullptr};
std::mutex         ConnectionManager::s_mutex{};
std::atomic<bool>  ConnectionManager::s_isInitialized{false};

class PrimaryConnection;
class InitialConnection;
class LanDeviceScanner;

void ConnectionManager::ConnectPrimary(const InitialConnectionData& data) {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    if (data.initialConnectionMode == InitialConnectionMode::CONNECT_WITH_PAIR) {
        s_instance->m_sslContext = CreateSSLContext(true, data.deviceInfo.deviceID);
    } else {
        s_instance->m_sslContext = CreateSSLContext(true);
    }

    s_instance->m_primaryConnection->Connect(s_instance->m_sslContext, data);
}

void ConnectionManager::SeekPrimary(const InitialConnectionData& data, std::function<void(TCPEndpoint)>&& callback) {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    if (data.initialConnectionMode == InitialConnectionMode::CONNECT_WITH_PAIR) {
        s_instance->m_sslContext = CreateSSLContext(true, data.deviceInfo.deviceID);
    } else {
        s_instance->m_sslContext = CreateSSLContext(true);
    }

    s_instance->m_primaryConnection->Seek(s_instance->m_sslContext, data, std::forward<std::function<void(TCPEndpoint)>>(callback));
}

void ConnectionManager::SeekInitialConnection(TCPEndpoint endpoint) {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    const std::shared_ptr<InitialConnection> connection = InitialConnection::Create(s_instance->m_context);

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        bool placed = false;

        for (auto& elem : s_instance->m_initialConnectionsIn) {
            if (!elem.lock()) {
                elem = connection;
                placed = true;
                break;
            }
        }

        if (!placed) {
            s_instance->m_initialConnectionsIn.push_back(connection);
        }
    }

    connection->TemporaryOwnership(connection);
    connection->Seek(std::move(endpoint), [](TCPEndpoint ep) {
        SeekInitialConnection(std::move(ep));
    });
}

void ConnectionManager::StartAcceptingConnections() {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);

        if (s_instance->m_initialConnectionOut != nullptr) {
            s_instance->m_initialConnectionOut->Disconnect();
        }

        s_instance->m_initialConnectionOut = InitialConnection::Create(s_instance->m_context);
    }

    const TCPEndpoint endpoint(asio::ip::tcp::v4(), 0);

    SeekInitialConnection(endpoint);
}

void ConnectionManager::StopAcceptingConnections() {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_instance->m_initialConnectionOut != nullptr) {
        s_instance->m_initialConnectionOut->Disconnect();
        s_instance->m_initialConnectionOut.reset();
    }

    for (const auto& elem : s_instance->m_initialConnectionsIn) {
        if (const auto ref = elem.lock()) {
            ref->Disconnect();
        }
    }

    s_instance->m_initialConnectionsIn.clear();
}

void ConnectionManager::Connect(const std::string& address, const uint16_t port, const InitialConnectionMode mode) {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    if (s_instance->m_initialConnectionOut == nullptr) {
        return;
    }

    TCPEndpoint endpoint(asio::ip::make_address_v4(address), port);
    s_instance->m_initialConnectionOut->Connect(std::move(endpoint), mode);
}

std::vector<DeviceInfoLite> ConnectionManager::GetPairedDevices() {
    std::vector<DeviceInfoLite> devices;
    std::error_code errorCode;

    static boost::uuids::string_generator generator;

    if (!std::filesystem::exists("certs", errorCode)) {
        Debug::LogWarning("Certs directory does not exist");
        return devices;
    }

    for (const auto& entry : std::filesystem::directory_iterator("certs", errorCode)) {
        if (errorCode) {
            Debug::LogError("GetPairedDevices error: {}", errorCode.message());
            break;
        }

        std::string name = entry.path().filename().string();
        if (entry.is_directory() && name != "local") {
            DeviceInfoLite device{};

            try {
                device.deviceID = generator(name);
            } catch (const std::system_error& e) {
                Debug::LogWarning("Invalid UUID directory: {}", name);
                continue;
            }

            std::filesystem::path path = entry.path() / "data.JSON";
            std::ifstream file(path.string(), std::ios::binary);

            if (!file) {
                Debug::LogWarning("Missing data.JSON for device {}", name);
                continue;
            }

            try {
                std::string rawJson{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

                nlohmann::json json = nlohmann::json::parse(rawJson);
                device.deviceName = json.value("name", "");
                if (json.contains("type") && json["type"].is_number_integer()) {
                    device.deviceType = static_cast<DeviceType>(json["type"].get<int>());
                } else {
                    device.deviceType = DeviceType::Unknown;
                }

                devices.push_back(device);

            } catch (const nlohmann::json::exception& exception) {
                Debug::LogWarning("Invalid JSON in {}: {}", path.string(), exception.what());
            }
        }
    }

    return devices;
}

void ConnectionManager::Disconnect(const std::error_code errorCode) {
    if (!s_isInitialized.load()) {
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
        context->load_verify_file(targetCertificatePath);
    } else {
        context->set_verify_mode(asio::ssl::verify_none);
        context->set_verify_callback(std::bind(&ConnectionManager::VerifyCallbackAlwaysAccept, std::placeholders::_1, std::placeholders::_2));
    }

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

bool ConnectionManager::VerifyCallbackAlwaysAccept(const bool preverified, asio::ssl::verify_context& ctx) {
    if (!preverified) {
        const X509_STORE_CTX* cert_ctx = ctx.native_handle();
        const int error_code = X509_STORE_CTX_get_error(cert_ctx);
        const char* error_string = X509_verify_cert_error_string(error_code);

        Debug::Log("Certificate presented but failed standard verification checks: {}", error_string);
    }

    return true;
}

void ConnectionManager::RunContext() {
    s_instance->m_context.run();
}

void ConnectionManager::SetSeekingEndpoint(TCPEndpoint endpoint) {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_instance->m_seekingEndpoint = std::move(endpoint);
}

asio::awaitable<void> ConnectionManager::CoProcessPackages() {
    const std::shared_ptr<AwaitableFlag> receiveFlag = m_primaryConnection->GetReceiveFlag();

    // ReSharper disable once CppDFAEndlessLoop
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
        Initialize();
    }

    s_instance->m_responseHandlerMap.InsertOrAssign(type, std::forward<RequestCallbackType>(handler));
}

void ConnectionManager::AddEventListener(const QPointer<QObject>& object) {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_instance->m_eventObjects.push_back(object);
}

TCPEndpoint ConnectionManager::GetSeekEndpoint() {
    if (!s_isInitialized.load()) {
        Initialize();
    }

    std::lock_guard<std::mutex> lock(s_mutex);
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
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_isInitialized.load()) {
        return;
    }

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