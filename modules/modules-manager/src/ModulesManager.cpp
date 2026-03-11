#include <ModulesManager.h>

ModulesManager::ModulesManager() {
    m_fileShareModule = std::make_shared<FileShareModule>();
    m_fileShareModule->Initialize();

    m_networkCameraModule = std::make_shared<NetworkCameraModule>();
    m_networkCameraModule->Initialize();

#ifndef IOS_DEVICE
    m_notificationSyncModule = std::make_shared<NotificationSyncModule>();
    m_notificationSyncModule->Initialize();
#endif
}

void ModulesManager::Initialize() {
    if (s_instance == nullptr) {
        s_instance = new ModulesManager();
    }
}