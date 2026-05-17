#ifndef MODULES_MANAGER_H
#define MODULES_MANAGER_H

#include <BaseModule.h>

#include <FileShareModule.h>
#include <NotificationSyncModule.h>
#include <ClipboardSyncModule.h>
#include <RemoteInputModule.h>
#include <SmsBridgeModule.h>
#include <SystemInfoShareModule.h>
#include <NetworkMicrophoneModule.h>

#include <QObject>
#include <QEvent>

#include "Scanner.h"

#ifndef MACOS_DEVICE
#include <NetworkCameraModule.h>
#endif

template<class>
inline constexpr bool always_false = false;

template<typename T>
concept ModuleType_ = std::is_base_of_v<BaseModule, T>;

class ModulesManager : public QObject {
    Q_OBJECT

public:
    explicit ModulesManager();
    static void Initialize();
    static void Shutdown();
    static void SetMainServiceBackendEnabled(bool enabled);

    template <ModuleType_ type>
    static auto& GetModuleReference() {
        Initialize();

        if constexpr (std::is_same_v<type, FileShareModule>) {
            return s_instance->m_fileShareModule;
        }
#ifndef MACOS_DEVICE
        else if constexpr (std::is_same_v<type, NetworkCameraModule>) {
            return s_instance->m_networkCameraModule;
        }
#endif
        else if constexpr (std::is_same_v<type, NotificationSyncModule>) {
#ifndef IOS_DEVICE
            return s_instance->m_notificationSyncModule;
#else
            static_assert(always_false<std::shared_ptr<type>>, "NotificationSyncModule have no support for IOS");
#endif
        } else if constexpr (std::is_same_v<type, ClipboardSyncModule>) {
            return s_instance->m_clipboardSyncModule;
        } else if constexpr (std::is_same_v<type, RemoteInputModule>) {
            return s_instance->m_remoteInputModule;
        } else if constexpr (std::is_same_v<type, SmsBridgeModule>) {
            return s_instance->m_smsBridgeModule;
        } else if constexpr (std::is_same_v<type, SystemInfoShareModule>) {
            return s_instance->m_systemInfoShareModule;
        } else if constexpr (std::is_same_v<type, NetworkMicrophoneModule>) {
            return s_instance->m_networkMicrophoneModule;
        } else {
            static_assert(always_false<std::shared_ptr<type>>, "Unknown module type");
        }
    }

protected:
    bool event(QEvent* event) override;

private:
    static ModulesManager* s_instance;
    static std::mutex s_mutex;
    static std::atomic_bool s_initialized;

#ifdef ANDROID_DEVICE
    static void StartMainService();
#endif

    std::shared_ptr<FileShareModule> m_fileShareModule;
#ifndef MACOS_DEVICE
    std::shared_ptr<NetworkCameraModule> m_networkCameraModule;
#endif
    std::shared_ptr<NotificationSyncModule> m_notificationSyncModule;
    std::shared_ptr<ClipboardSyncModule> m_clipboardSyncModule;
    std::shared_ptr<RemoteInputModule> m_remoteInputModule;
    std::shared_ptr<SmsBridgeModule> m_smsBridgeModule;
    std::shared_ptr<SystemInfoShareModule> m_systemInfoShareModule;
    std::shared_ptr<NetworkMicrophoneModule> m_networkMicrophoneModule;
};

#endif
