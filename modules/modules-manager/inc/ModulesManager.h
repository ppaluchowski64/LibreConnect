#ifndef MODULES_MANAGER_H
#define MODULES_MANAGER_H

#include <BaseModule.h>

#include <NetworkCameraModule.h>
#include <FileShareModule.h>
#include <NotificationSyncModule.h>
#include <ClipboardSyncModule.h>

#include <QObject>
#include <QEvent>


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
        } else if constexpr (std::is_same_v<type, NetworkCameraModule>) {
#ifndef MACOS_DEVICE
            return s_instance->m_networkCameraModule;
#else
            static_assert(always_false<std::shared_ptr<type>>, "NetworkCameraModule have no support for MacOS");
#endif
        } else if constexpr (std::is_same_v<type, NotificationSyncModule>) {
#ifndef IOS_DEVICE
            return s_instance->m_notificationSyncModule;
#else
            static_assert(always_false<std::shared_ptr<type>>, "NotificationSyncModule have no support for IOS");
#endif
        } else if constexpr (std::is_same_v<type, ClipboardSyncModule>) {
            return s_instance->m_clipboardSyncModule;
        } else {
            static_assert(always_false<std::shared_ptr<type>>, "Unknown module type");
        }
    }

protected:
    bool event(QEvent* event) override;

private:
    static ModulesManager* s_instance;
    static std::mutex s_mutex;

#ifdef ANDROID_DEVICE
    static void StartMainService();
#endif

    std::shared_ptr<FileShareModule> m_fileShareModule;
    std::shared_ptr<NetworkCameraModule> m_networkCameraModule;
    std::shared_ptr<NotificationSyncModule> m_notificationSyncModule;
    std::shared_ptr<ClipboardSyncModule> m_clipboardSyncModule;
};

#endif
