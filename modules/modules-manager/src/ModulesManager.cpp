#include <ModulesManager.h>
#include <ConnectionManager.h>
#include <DebugLog.h>
#include <QPointer>
#include <Events.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

ModulesManager* ModulesManager::s_instance{nullptr};
std::mutex ModulesManager::s_mutex{};

bool ModulesManager::event(QEvent* event) {
    if (event->type() == ConnectedEvent::Type) {
        Debug::Log("ModulesManager: ConnectedEvent received, ensuring modules are initialized");
        Initialize();
        return true;
    }

    if (event->type() == DisconnectedEvent::Type) {
        Debug::Log("ModulesManager: DisconnectedEvent received, shutting down modules");
        Shutdown();
        return true;
    }

    if (event->type() == ModuleErrorEvent::Type) {
        const auto* moduleError = static_cast<ModuleErrorEvent*>(event);
        Debug::LogError(
            "ModulesManager: ModuleErrorEvent received from {} with reason {}",
            ModuleTypeToString(moduleError->GetModuleType()),
            ModuleFailReasonToString(moduleError->GetError())
        );
        return true;
    }

    return QObject::event(event);
}

ModulesManager::ModulesManager() {
    Debug::Log("ModulesManager: Constructing modules");

    m_fileShareModule = std::make_shared<FileShareModule>();

#ifndef MACOS_DEVICE
    m_networkCameraModule = std::make_shared<NetworkCameraModule>();
#endif

#ifndef IOS_DEVICE
    m_notificationSyncModule = std::make_shared<NotificationSyncModule>();
#endif

    m_clipboardSyncModule = std::make_shared<ClipboardSyncModule>();
    m_remoteInputModule = std::make_shared<RemoteInputModule>();

#ifdef ANDROID_DEVICE
    SetMainServiceBackendEnabled(true);
#endif

    ConnectionManager::AddEventListener(QPointer<QObject>(this));
}

void ModulesManager::Initialize() {
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_instance == nullptr) {
        s_instance = new ModulesManager();
        Debug::Log("ModulesManager: Instance created");
    }

    s_instance->m_fileShareModule->Initialize(true);

#ifndef MACOS_DEVICE
    s_instance->m_networkCameraModule->Initialize(true);
#endif

#ifndef IOS_DEVICE
    s_instance->m_notificationSyncModule->Initialize(true);
#endif

    s_instance->m_clipboardSyncModule->Initialize(true);
    s_instance->m_remoteInputModule->Initialize(true);
}

void ModulesManager::Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    Debug::Log("ModulesManager: Shutdown called");

    if (s_instance != nullptr) {
        s_instance->m_fileShareModule->Shutdown(true);

#ifndef MACOS_DEVICE
        s_instance->m_networkCameraModule->Shutdown(true);
#endif

#ifndef IOS_DEVICE
        s_instance->m_notificationSyncModule->Shutdown(true);
#endif

        s_instance->m_clipboardSyncModule->Shutdown(true);
        s_instance->m_remoteInputModule->Shutdown(true);
    } else {
        Debug::Log("ModulesManager: Shutdown skipped (instance is null)");
    }
}

#ifndef ANDROID_DEVICE
void ModulesManager::SetMainServiceBackendEnabled(const bool enabled) {
    (void)enabled;
}
#endif
