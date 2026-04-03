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
#include <ThreadPool.h>
#include <boost/uuid/string_generator.hpp>

ConnectionManager* ConnectionManager::s_instance{nullptr};
std::mutex         ConnectionManager::s_mutex{};
std::once_flag     ConnectionManager::s_flag{};

void ConnectionManager::ConnectPrimary(const InitialConnectionData& data) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Attempting ConnectPrimary for device {}", boost::uuids::to_string(data.deviceInfo.deviceID));

    const uuid targetUUID = data.initialConnectionMode == InitialConnectionMode::CONNECT_WITH_PAIR
        ? data.deviceInfo.deviceID
        : boost::uuids::nil_uuid();

    s_instance->m_sslContextClient = CreateSSLContext(false, targetUUID);
    s_instance->m_sslContextServer = CreateSSLContext(true, targetUUID);

    s_instance->m_primaryConnection->Connect(s_instance->m_sslContextClient, data);
}

void ConnectionManager::SeekPrimary(const InitialConnectionData& data, std::function<void(TCPEndpoint)>&& callback) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Starting SeekPrimary for device {}", boost::uuids::to_string(data.deviceInfo.deviceID));

    const uuid targetUUID = data.initialConnectionMode == InitialConnectionMode::CONNECT_WITH_PAIR
        ? data.deviceInfo.deviceID
        : boost::uuids::nil_uuid();

    s_instance->m_sslContextClient = CreateSSLContext(false, targetUUID);
    s_instance->m_sslContextServer = CreateSSLContext(true, targetUUID);

    s_instance->m_primaryConnection->Seek(s_instance->m_sslContextServer, data, std::forward<std::function<void(TCPEndpoint)>>(callback));
}

void ConnectionManager::SeekInitialConnection(TCPEndpoint endpoint) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Seeking initial connection on {}:{}", endpoint.address().to_string(), endpoint.port());

    const std::shared_ptr<InitialConnection> connection = InitialConnection::Create();

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
        Debug::Log("ConnectionManager: Initial connection seek triggered recursing seek");
        SeekInitialConnection(std::move(ep));
    });
}

void ConnectionManager::StartAcceptingConnections() {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Starting to accept connections");

    {
        std::lock_guard<std::mutex> lock(s_mutex);

        if (s_instance->m_initialConnectionOut != nullptr) {
            Debug::Log("ConnectionManager: Disconnecting existing outgoing initial connection");
            s_instance->m_initialConnectionOut->Disconnect();
        }

        s_instance->m_initialConnectionOut = InitialConnection::Create();
    }

    const TCPEndpoint endpoint(asio::ip::tcp::v4(), 0);
    SeekInitialConnection(endpoint);
}

void ConnectionManager::StopAcceptingConnections() {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Stopping all connection acceptance");

    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_instance->m_initialConnectionOut != nullptr) {
        s_instance->m_initialConnectionOut->Disconnect();
        s_instance->m_initialConnectionOut.reset();
    }

    size_t activeInbound = 0;
    for (const auto& elem : s_instance->m_initialConnectionsIn) {
        if (const auto ref = elem.lock()) {
            ref->Disconnect();
            activeInbound++;
        }
    }

    Debug::Log("ConnectionManager: Disconnected {} inbound connections", activeInbound);
    s_instance->m_initialConnectionsIn.clear();
}

void ConnectionManager::Connect(const std::string& address, const uint16_t port, const InitialConnectionMode mode) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Connecting to {}:{} with mode {}", address, port, static_cast<int>(mode));

    if (s_instance->m_initialConnectionOut == nullptr) {
        Debug::LogWarning("ConnectionManager: Cannot connect, initialConnectionOut is null");
        return;
    }

    TCPEndpoint endpoint(asio::ip::make_address_v4(address), port);
    s_instance->m_initialConnectionOut->Connect(std::move(endpoint), mode);
}

std::vector<DeviceInfoLite> ConnectionManager::GetPairedDevices() {
    Debug::Log("ConnectionManager: Scanning for paired devices in 'certs/'");
    std::vector<DeviceInfoLite> devices;
    std::error_code errorCode;

    static boost::uuids::string_generator generator;

    if (!std::filesystem::exists("certs", errorCode)) {
        Debug::LogWarning("ConnectionManager: Certs directory does not exist");
        return devices;
    }

    for (const auto& entry : std::filesystem::directory_iterator("certs", errorCode)) {
        if (errorCode) {
            Debug::LogError("ConnectionManager: GetPairedDevices iterator error: {}", errorCode.message());
            break;
        }

        std::string name = entry.path().filename().string();
        if (entry.is_directory() && name != "local") {
            DeviceInfoLite device{};

            try {
                device.deviceID = generator(name);
            } catch (const std::exception&) {
                Debug::LogWarning("ConnectionManager: Invalid UUID directory found: {}", name);
                continue;
            }

            std::filesystem::path path = entry.path() / "data.JSON";
            std::ifstream file(path.string(), std::ios::binary);

            if (!file) {
                Debug::LogWarning("ConnectionManager: Missing data.JSON for device {}", name);
                continue;
            }

            try {
                std::string rawJson{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
                nlohmann::json json = nlohmann::json::parse(rawJson);

                device.deviceName = json.value("name", "Unknown Device");
                if (json.contains("type") && json["type"].is_number_integer()) {
                    device.deviceType = static_cast<DeviceType>(json["type"].get<int>());
                } else {
                    device.deviceType = DeviceType::Unknown;
                }

                devices.push_back(device);
                Debug::Log("ConnectionManager: Found paired device: {} ({})", device.deviceName, name);

            } catch (const nlohmann::json::exception& exception) {
                Debug::LogWarning("ConnectionManager: Invalid JSON in {}: {}", path.string(), exception.what());
            }
        }
    }

    return devices;
}

bool ConnectionManager::RemovePairedDevice(const std::string& deviceId) {
    static boost::uuids::string_generator generator;

    boost::uuids::uuid parsedId;
    try {
        parsedId = generator(deviceId);
    } catch (const std::exception&) {
        Debug::LogWarning("ConnectionManager: RemovePairedDevice received invalid UUID '{}'", deviceId);
        return false;
    }

    const std::filesystem::path targetPath = std::filesystem::path("certs") / boost::uuids::to_string(parsedId);
    std::error_code errorCode;

    if (!std::filesystem::exists(targetPath, errorCode)) {
        if (errorCode) {
            Debug::LogWarning("ConnectionManager: Failed checking paired device path '{}': {}",
                              targetPath.string(), errorCode.message());
        }
        return false;
    }

    std::filesystem::remove_all(targetPath, errorCode);
    if (errorCode) {
        Debug::LogError("ConnectionManager: Failed removing paired device '{}' at '{}': {} ({})",
                        deviceId, targetPath.string(), errorCode.message(), errorCode.value());
        return false;
    }

    Debug::Log("ConnectionManager: Removed paired device '{}' at '{}'", deviceId, targetPath.string());
    return true;
}

void ConnectionManager::Disconnect(const std::error_code errorCode) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Disconnecting primary connection. Reason: {}", errorCode.message());

    s_instance->m_primaryConnection->Disconnect(errorCode, true);

    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_instance->m_initialConnectionOut != nullptr) {
        s_instance->m_initialConnectionOut->Disconnect();
    }
}

std::shared_ptr<SSLContext> ConnectionManager::CreateSSLContext(const bool isServer, const uuid targetUUID) {
    Debug::Log("ConnectionManager: Creating SSL Context (Role: {}, Target: {})",
               isServer ? "Server" : "Client", boost::uuids::to_string(targetUUID));

    const std::string targetCertificatePath{"certs/" + boost::uuids::to_string(targetUUID) + "/cert.key"};
    constexpr std::string_view privateKeyPath{"certs/local/pkey.key"};
    constexpr std::string_view certificatePath{"certs/local/cert.key"};

    if (!CryptographicIdentityManager::IsCertificateValid(certificatePath)) {
        Debug::Log("ConnectionManager: Local certificate invalid or missing. Generating new identity...");
        CryptographicIdentityManager::GenerateCertificate(privateKeyPath, certificatePath);
    }

    std::shared_ptr<SSLContext> context = std::make_shared<SSLContext>(isServer ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);

    if (targetUUID != boost::uuids::nil_uuid() && std::filesystem::exists(targetCertificatePath)) {
        Debug::Log("ConnectionManager: Loading verification file for target device");
        context->set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
        context->load_verify_file(targetCertificatePath);
    } else {
        Debug::Log("ConnectionManager: No target certificate found, requesting peer certificate and using 'Always Accept' callback");
        context->set_verify_mode(asio::ssl::verify_peer);
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
        Debug::LogError("ConnectionManager: Failed to load SSL local certs: {}", e.what());
    }

    return context;
}

bool ConnectionManager::VerifyCallbackAlwaysAccept(const bool preverified, asio::ssl::verify_context& ctx) {
    if (!preverified) {
        const X509_STORE_CTX* cert_ctx = ctx.native_handle();
        const int error_code = X509_STORE_CTX_get_error(cert_ctx);
        const char* error_string = X509_verify_cert_error_string(error_code);

        Debug::Log("ConnectionManager: Certificate presented but failed standard verification: {}", error_string);
    } else {
        Debug::Log("ConnectionManager: Certificate pre-verified successfully");
    }

    return true;
}

void ConnectionManager::RunContext() {
    Debug::Log("ConnectionManager: IO Context thread started");
    s_instance->m_context.run();
    Debug::Log("ConnectionManager: IO Context thread stopped");
}

void ConnectionManager::SetSeekingEndpoint(TCPEndpoint endpoint) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Setting seeking endpoint to {}:{}", endpoint.address().to_string(), endpoint.port());

    std::lock_guard<std::mutex> lock(s_mutex);
    s_instance->m_seekingEndpoint = std::move(endpoint);
}

asio::awaitable<void> ConnectionManager::CoProcessPackages() {
    Debug::Log("ConnectionManager: Package processing coroutine started");
    if (!m_primaryConnection) {
        Debug::LogError("ConnectionManager: CoProcessPackages started with null primaryConnection");
        co_return;
    }

    const std::shared_ptr<AwaitableFlag> receiveFlag = m_primaryConnection->GetReceiveFlag();
    if (!receiveFlag) {
        Debug::LogError("ConnectionManager: CoProcessPackages receive flag is null");
        co_return;
    }

    while (true) {
        try {
            co_await receiveFlag->Wait();
            receiveFlag->Reset();

            std::optional<std::unique_ptr<Package<PC_PackageType>>> packageOptional = m_primaryConnection->GetPackage();
            while (packageOptional.has_value()) {
                std::unique_ptr<Package<PC_PackageType>> value = std::move(packageOptional.value());
                if (!value) {
                    Debug::LogWarning("ConnectionManager: Received null package pointer");
                    packageOptional = m_primaryConnection->GetPackage();
                    continue;
                }
                const PackageHeader header = value->GetHeader();
                if ((header.flags & PackageFlag::REQUEST_AWAITABLE_RESPONSE) != 0) {
                    size_t requestID = value->GetValue<size_t>();
                    auto flag = m_requestAwaitableMap.Pop(requestID);

                    if (flag.has_value()) {
                        m_requestPackageMap.InsertOrAssign(requestID, std::move(value));
                        flag.value()->Signal();
                    } else {
                        Debug::LogWarning("ConnectionManager: No awaitable flag for Request ID: {}", requestID);
                    }

                } else {
                    PC_PackageType type = static_cast<PC_PackageType>(header.type);
                    if (type == PC_PackageType::HEARTBEAT) {
                        m_primaryConnection->MarkHeartbeatReceived();
                        packageOptional = m_primaryConnection->GetPackage();
                        continue;
                    }

                    std::optional<RequestCallbackType> callbackOptional = m_responseHandlerMap.Get(type);
                    std::optional<RequestAwaitableCallbackType> awaitableCallbackOptional = m_responseAwaitableHandlerMap.Get(type);

                    if (callbackOptional.has_value()) {
                        asio::post(m_context, [callback = std::move(callbackOptional.value()), package = std::move(value)]() mutable {
                            try {
                                callback(std::move(package));
                            } catch (const std::exception& exception) {
                                Debug::LogError("ConnectionManager: Response handler exception: {}", exception.what());
                            } catch (...) {
                                Debug::LogError("ConnectionManager: Response handler exception: unknown");
                            }
                        });

                        packageOptional = m_primaryConnection->GetPackage();
                        continue;
                    }

                    if (awaitableCallbackOptional.has_value()) {
                        RequestAwaitableCallbackType awaitableCallback = std::move(awaitableCallbackOptional.value());
                        asio::co_spawn(m_context, [callback = std::move(awaitableCallback), package = std::move(value)]() mutable -> asio::awaitable<void> {
                            try {
                                co_await callback(std::move(package));
                            } catch (const std::exception& exception) {
                                Debug::LogError("ConnectionManager: Awaitable response handler exception: {}", exception.what());
                            } catch (...) {
                                Debug::LogError("ConnectionManager: Awaitable response handler exception: unknown");
                            }

                            co_return;
                        }(), asio::detached);
                        packageOptional = m_primaryConnection->GetPackage();
                        continue;
                    }

                    Debug::LogWarning("ConnectionManager: No handler registered for package type {}", static_cast<int>(type));
                }

                packageOptional = m_primaryConnection->GetPackage();
            }
        } catch (const std::exception& exception) {
            Debug::LogError("ConnectionManager: Package processing exception: {}", exception.what());
            Disconnect(std::make_error_code(std::errc::bad_message));
        } catch (...) {
            Debug::LogError("ConnectionManager: Package processing exception: unknown");
            Disconnect(std::make_error_code(std::errc::bad_message));
        }
    }
}

void ConnectionManager::AddResponseHandler(const PC_PackageType type, RequestCallbackType&& handler) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Adding response handler for type {}", static_cast<int>(type));
    s_instance->m_responseHandlerMap.InsertOrAssign(type, std::forward<RequestCallbackType>(handler));
}

void ConnectionManager::AddAwaitableResponseHandler(const PC_PackageType type, RequestAwaitableCallbackType&& handler) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Adding awaitable response handler for type {}", static_cast<int>(type));
    s_instance->m_responseAwaitableHandlerMap.InsertOrAssign(type, std::forward<RequestAwaitableCallbackType>(handler));
}

void ConnectionManager::RemoveResponseHandler(const PC_PackageType type) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Removing response handler for type {}", static_cast<int>(type));
    s_instance->m_responseHandlerMap.Erase(type);
}

void ConnectionManager::RemoveAwaitableResponseHandler(const PC_PackageType type) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: Removing awaitable response handler for type {}", static_cast<int>(type));
    s_instance->m_responseAwaitableHandlerMap.Erase(type);
}

void ConnectionManager::AddEventListener(const QPointer<QObject>& object) {
    std::call_once(s_flag, Initialize);
    Debug::Log("ConnectionManager: New QObject event listener added");

    std::lock_guard<std::mutex> lock(s_mutex);
    s_instance->m_eventObjects.push_back(object);
}

TCPEndpoint ConnectionManager::GetSeekEndpoint() {
    std::call_once(s_flag, Initialize);
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_instance->m_seekingEndpoint;
}

IPAddress ConnectionManager::GetPeerAddress() {
    std::call_once(s_flag, Initialize);
    return s_instance->m_primaryConnection->GetPeerAddress();
}

std::shared_ptr<SSLContext> ConnectionManager::GetSSLContextClient() {
    std::call_once(s_flag, Initialize);
    return s_instance->m_sslContextClient;
}

std::shared_ptr<SSLContext> ConnectionManager::GetSSLContextServer() {
    std::call_once(s_flag, Initialize);
    return s_instance->m_sslContextServer;
}

ConnectionManager::ConnectionManager() : m_context(ThreadPool::GetContext()) { }

void ConnectionManager::Initialize() {
    s_instance = new ConnectionManager();

    s_instance->m_primaryConnection = PrimaryConnection::Create();
    asio::co_spawn(s_instance->m_context, s_instance->CoProcessPackages(), asio::detached);
}

void ConnectionManager::SendEvent(const std::unique_ptr<QEvent>& event) {
    std::call_once(s_flag, Initialize);

    std::vector<QPointer<QObject>> targets;
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        auto& objects = s_instance->m_eventObjects;
        std::erase_if(objects, [](const QPointer<QObject>& obj) {
            return obj.isNull();
        });

        if (objects.empty()) {
            Debug::Log("ConnectionManager: SendEvent called but no active listeners registered");
            return;
        }

        targets = objects;
    }

    Debug::Log("ConnectionManager: Dispatching event to {} listeners", targets.size());
    for (const auto& obj : targets) {
        QMetaObject::invokeMethod(obj, [event = event->clone(), obj]() {
            if (obj.isNull()) return;
            obj->event(event);
        });
    }
}
