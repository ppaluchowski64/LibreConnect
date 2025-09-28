#include <ConnectionManager.h>
#include <CertificateManager.h>

ConnectionManager* ConnectionManager::s_instance{nullptr};
std::mutex         ConnectionManager::s_mutex{};
std::atomic<bool>  ConnectionManager::s_isInitialized{false};


void ConnectionManager::Connect(TCPEndpoint&& endpoint, ConnectionCallbackType&& callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_isInitialized.load()) {
        Initialize();
    }

    if (s_instance->m_currentSSLContextCurrentMode != SSLContextCurrentMode::CLIENT) {
        s_instance->m_sslContext = CreateSSLContext(false);
    }

    s_instance->m_primaryConnection->Connect(std::forward<TCPEndpoint>(endpoint), s_instance->m_sslContext, std::forward<ConnectionCallbackType>(callback));
}

void ConnectionManager::Seek(TCPEndpoint&& endpoint, ConnectionCallbackType&& callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_isInitialized.load()) {
        Initialize();
    }

    if (s_instance->m_currentSSLContextCurrentMode != SSLContextCurrentMode::SERVER) {
        s_instance->m_sslContext = CreateSSLContext(true);
    }

    s_instance->m_primaryConnection->Seek(std::forward<TCPEndpoint>(endpoint), s_instance->m_sslContext, std::forward<ConnectionCallbackType>(callback));
}

void ConnectionManager::Disconnect(DisconnectionCallbackType&& callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_isInitialized.load()) {
        Initialize();
    }

    s_instance->m_primaryConnection->Disconnect(std::forward<DisconnectionCallbackType>(callback));
}

std::shared_ptr<SSLContext> ConnectionManager::CreateSSLContext(const bool isServer) {
    if (!TLS::CertificateManager::IsCertificateValid("certs")) {
        TLS::CertificateManager::GenerateCertificate("certs");
    }

    std::shared_ptr<SSLContext> context = std::make_shared<SSLContext>(isServer ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);
    context->set_options(
        SSLContext::default_workarounds |
        SSLContext::no_sslv2 |
        SSLContext::no_sslv3 |
        SSLContext::no_tlsv1 |
        SSLContext::no_tlsv1_1
    );

    context->use_certificate_chain_file("certs/certificate.crt");
    context->use_private_key_file("certs/privateKey.key", SSLContext::pem);

    return context;
}

void ConnectionManager::RunContext() {
    s_instance->m_context.run();
}

void ConnectionManager::AddResponseHandler(const PC_PackageType type, RequestCallbackType&& handler) {
    if (!s_isInitialized.load()) {
        return;
    }

    s_instance->m_responseHandlerMap.InsertOrAssign(type, std::forward<RequestCallbackType>(handler));
}

ConnectionManager::ConnectionManager() : m_workGuard(asio::make_work_guard(m_context)) {
    s_instance = this;

    for (int i = 0; i < 1; i++) {
        m_threads.emplace_back(RunContext);
    }

    m_primaryConnection = PrimaryConnection::Create(m_context);
}

void ConnectionManager::Initialize() {
    s_instance = new ConnectionManager();
    s_isInitialized.store(true);
}
