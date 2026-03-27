#include <ModulesManager.h>
#include <ConnectionManager.h>
#include <DebugLog.h>
#include <QPointer>
#include <Events.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

ModulesManager* ModulesManager::s_instance{nullptr};
std::once_flag ModulesManager::s_flag{};
std::mutex ModulesManager::s_mutex{};

bool ModulesManager::event(QEvent* event) {
    if (event->type() == DisconnectedEvent::Type) {
        Debug::Log("ModulesManager: DisconnectedEvent received, shutting down modules");
        Shutdown();
        return true;
    }

    return QObject::event(event);
}

ModulesManager::ModulesManager() {
    Debug::Log("ModulesManager: Constructing modules");

    m_fileShareModule = std::make_shared<FileShareModule>();
    m_fileShareModule->Initialize();

    m_networkCameraModule = std::make_shared<NetworkCameraModule>();
    m_networkCameraModule->Initialize();

#ifndef IOS_DEVICE
    m_notificationSyncModule = std::make_shared<NotificationSyncModule>();
    m_notificationSyncModule->Initialize();
#endif

#ifdef ANDROID_DEVICE
    SetMainServiceBackendEnabled(true);
#endif

    ConnectionManager::AddEventListener(QPointer<QObject>(this));
}

void ModulesManager::Initialize() {
    std::lock_guard<std::mutex> lock(s_mutex);
    Debug::Log("ModulesManager: Initialize called");

    if (s_instance == nullptr) {
        s_instance = new ModulesManager();
        Debug::Log("ModulesManager: Instance created");
    } else {
        Debug::Log("ModulesManager: Initialize skipped (instance already exists)");
    }
}

void ModulesManager::Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    Debug::Log("ModulesManager: Shutdown called");

    if (s_instance != nullptr) {
        s_instance->m_fileShareModule->Shutdown(true);
        s_instance->m_networkCameraModule->Shutdown(true);

#ifndef IOS_DEVICE
        s_instance->m_notificationSyncModule->Shutdown(true);
#endif
    } else {
        Debug::Log("ModulesManager: Shutdown skipped (instance is null)");
    }
}

#ifndef ANDROID_DEVICE
void ModulesManager::SetMainServiceBackendEnabled(const bool enabled) {
    (void)enabled;
}
#endif
