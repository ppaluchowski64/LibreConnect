#include <ConnectionManager.h>
#include <CertificateManager.h>

ConnectionManager* ConnectionManager::s_instance = nullptr;

void ConnectionManager::Connect(const TCPEndpoint& endpoint, const ConnectionCallbackType& callback) {
    std::call_once(s_initFlag, Initialize);
    if (s_instance->m_isConnected.load()) {
        Debug::LogWarning("ConnectionManager::Connect: existing connection terminated before starting new one");
        s_instance->m_primaryConnection->Disconnect();
    }

    if (s_instance->m_currentSSLContextCurrentMode != SSLContextCurrentMode::CLIENT) {
        s_instance->m_sslContext = CreateSSLContext(false);
    }

    s_instance->m_primaryConnection->Connect(endpoint, s_instance->m_sslContext, callback);
}

void ConnectionManager::Seek(const TCPEndpoint& endpoint, const std::function<void(bool)>& callback) {
    std::call_once(s_initFlag, Initialize);
    if (s_instance->m_isConnected.load()) {
        Debug::LogWarning("ConnectionManager::Connect: existing connection terminated before starting new one");
        s_instance->m_primaryConnection->Disconnect();
    }

    if (s_instance->m_currentSSLContextCurrentMode != SSLContextCurrentMode::SERVER) {
        s_instance->m_sslContext = CreateSSLContext(true);
    }

    s_instance->m_primaryConnection->Seek(endpoint, s_instance->m_sslContext, callback);
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
    std::call_once(s_initFlag, Initialize);
    s_instance->m_responseHandlerMap.InsertOrAssign(type, std::forward<RequestCallbackType>(handler));
}

ConnectionManager::ConnectionManager() : m_workGuard(asio::make_work_guard(m_context)) {
    for (int i = 0; i < 1; i++) {
        m_threads.emplace_back(RunContext);
    }

    m_primaryConnection = PrimaryConnection::Create(m_context);
}

void ConnectionManager::Initialize() {
    s_instance = new ConnectionManager();
}
