#include <ConnectionManager.h>

ConnectionManager* ConnectionManager::s_instance = nullptr;

void ConnectionManager::Connect(const TCPEndpoint& endpoint, const ConnectionCallbackType& callback) {
    std::call_once(s_initFlag, Initialize);

    if (s_instance->m_isConnected.load()) {
        Debug::LogWarning("ConnectionManager::Connect: existing connection terminated before starting new one");
        s_instance->m_primaryConnection->Disconnect();
    }

    s_instance->m_primaryConnection->Connect(endpoint, callback);
}

void ConnectionManager::AddResponseHandler(const PC_PackageType type, RequestCallbackType&& handler) {
    std::call_once(s_initFlag, Initialize);
    s_instance->m_responseHandlerMap.InsertOrAssign(type, std::forward<RequestCallbackType>(handler));
}

void ConnectionManager::Initialize() {
    s_instance = new ConnectionManager();
}
