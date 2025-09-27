#include <ConnectionManager.h>

ConnectionManager* ConnectionManager::s_instance = nullptr;

void ConnectionManager::AddResponseHandler(const PC_PackageType type, CallbackType&& handler) {
    s_instance->m_responseHandlerMap.InsertOrAssign(type, std::forward<CallbackType>(handler));
}