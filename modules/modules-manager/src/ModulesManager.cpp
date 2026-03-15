#include <ModulesManager.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

ModulesManager* ModulesManager::s_instance{nullptr};
std::once_flag ModulesManager::s_flag{};

ModulesManager::ModulesManager() {
    m_fileShareModule = std::make_shared<FileShareModule>();
    m_fileShareModule->Initialize();

    m_networkCameraModule = std::make_shared<NetworkCameraModule>();
    m_networkCameraModule->Initialize();

#ifndef IOS_DEVICE
    m_notificationSyncModule = std::make_shared<NotificationSyncModule>();
    m_notificationSyncModule->Initialize();
#endif

#ifdef ANDROID_DEVICE
    StartMainService();
#endif
}

void ModulesManager::Initialize() {
    if (s_instance == nullptr) {
        s_instance = new ModulesManager();
    }
}