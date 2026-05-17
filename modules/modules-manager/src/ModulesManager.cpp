#include <ModulesManager.h>
#include <ConnectionManager.h>
#include <TransferChannelPool.h>
#include <DebugLog.h>
#include <QPointer>
#include <Events.h>
#include <NetworkMicrophoneModule.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <PermissionManager.h>
#endif

#ifdef MACOS_DEVICE
#include <ApplicationServices/ApplicationServices.h>
#include <NotificationEmitter.h>
#endif

namespace {
#ifdef MACOS_DEVICE
bool RequestMacAccessibilityPermission() {
    const void* keys[] = { kAXTrustedCheckOptionPrompt };
    const void* values[] = { kCFBooleanTrue };
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    const bool trusted = AXIsProcessTrustedWithOptions(options);
    if (options != nullptr) {
        CFRelease(options);
    }

    return trusted;
}
#endif

    constexpr size_t TRANSFER_CHANNEL_COUNT = 10;

}

ModulesManager* ModulesManager::s_instance{nullptr};
std::mutex ModulesManager::s_mutex{};
std::atomic_bool ModulesManager::s_initialized{};

bool ModulesManager::event(QEvent* event) {
    if (event->type() == ConnectedEvent::Type) {
        const auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        if (connectedEvent->GetResult() != EventResult::SUCCESS) {
            Debug::Log("ModulesManager: ConnectedEvent reported failure, skipping module initialization");
            return true;
        }

        Debug::Log("ModulesManager: ConnectedEvent reported success, ensuring modules are initialized");
        Initialize();
        TransferChannelPool::Reset();

#ifdef DESKTOP_DEVICE
        asio::co_spawn(ThreadPool::GetContext(), TransferChannelPool::Connect(), asio::detached);
#endif
        return true;
    }

    if (event->type() == DisconnectedEvent::Type) {
        Debug::Log("ModulesManager: DisconnectedEvent received, shutting down modules");
        Shutdown();
        TransferChannelPool::Reset();
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
    m_smsBridgeModule = std::make_shared<SmsBridgeModule>();
    m_systemInfoShareModule = std::make_shared<SystemInfoShareModule>();
    m_networkMicrophoneModule = std::make_shared<NetworkMicrophoneModule>();

#ifdef ANDROID_DEVICE
    SetMainServiceBackendEnabled(true);
#endif

    ConnectionManager::AddEventListener(QPointer<QObject>(this));
    TransferChannelPool::Initialize(TRANSFER_CHANNEL_COUNT);
}

void ModulesManager::Initialize() {
    if (s_initialized.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_instance == nullptr) {
        s_instance = new ModulesManager();

        Debug::Log("ModulesManager: Instance created");
    }

    if (s_instance->m_fileShareModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_fileShareModule->Initialize(true);
    }

#ifndef MACOS_DEVICE
    if (s_instance->m_networkCameraModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_networkCameraModule->Initialize(true);
    }
#endif

#ifndef IOS_DEVICE
    if (s_instance->m_notificationSyncModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_notificationSyncModule->Initialize(true);
    }
#endif

    if (s_instance->m_clipboardSyncModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_clipboardSyncModule->Initialize(true);
    }

    if (s_instance->m_remoteInputModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_remoteInputModule->Initialize(true);
    }

    if (s_instance->m_smsBridgeModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_smsBridgeModule->Initialize(true);
    }

    if (s_instance->m_systemInfoShareModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_systemInfoShareModule->Initialize(true);
    }

    if (s_instance->m_networkMicrophoneModule->GetModuleState() == ModuleState::Uninitialized) {
        s_instance->m_networkMicrophoneModule->Initialize(true);
    }

    ConnectionManager::AddResponseHandler(PC_PackageType::PERMISSION_REQUESTED, [](PC_Package&& package) {
        const PermissionType type = package->GetValue<PermissionType>();
        const std::unique_ptr<QEvent> event = std::make_unique<ModuleRequestedPermission>(type);
        ConnectionManager::SendEvent(event);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::PERMISSION_REJECTED, [](PC_Package&& package) {
        const PermissionType type = package->GetValue<PermissionType>();
        const std::unique_ptr<QEvent> event = std::make_unique<ModuleRequestedPermissionRejected>(type);
        ConnectionManager::SendEvent(event);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::PERMISSION_GRANTED, [](PC_Package&& package) {
        const PermissionType type = package->GetValue<PermissionType>();
        const std::unique_ptr<QEvent> event = std::make_unique<ModuleRequestedPermissionGranted>(type);
        ConnectionManager::SendEvent(event);
    });

#ifdef ANDROID_DEVICE
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::PERMISSION_SYNC_REQUEST, [](PC_Package&& package) -> asio::awaitable<void> {
        auto sendStatus = [](const PermissionType type, const bool granted) {
            ConnectionManager::Send(
                granted ? PC_PackageType::PERMISSION_GRANTED : PC_PackageType::PERMISSION_REJECTED,
                type
            );
        };

        const bool cameraGranted = PermissionManager::IsCameraAccessPermissionGranted();
        const bool microphoneGranted = PermissionManager::IsMicrophoneAccessPermissionGranted();
        const bool notificationsGranted = PermissionManager::IsNotificationEmitPermissionGranted() && PermissionManager::IsNotificationAccessPermissionGranted();
        const bool fileSystemGranted = PermissionManager::IsFileAccessPermissionGranted() && PermissionManager::IsManagingExternalStoragePermissionGranted();
        const bool batteryGranted = PermissionManager::IsBatteryOptimizationIgnored();
        const bool smsGranted =
            PermissionManager::IsReceiveSmsPermissionGranted() &&
            PermissionManager::IsReadContactsPermissionGranted() &&
            PermissionManager::IsReadSmsPermissionGranted() &&
            PermissionManager::IsSendSmsPermissionGranted();

        sendStatus(PermissionType::Camera, cameraGranted);
        sendStatus(PermissionType::Microphone, microphoneGranted);
        sendStatus(PermissionType::Notifications, notificationsGranted);
        sendStatus(PermissionType::FileSystem, fileSystemGranted);
        sendStatus(PermissionType::Battery, batteryGranted);
        sendStatus(PermissionType::Sms, smsGranted);

        co_return;
    });

    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::PERMISSION_REQUEST, [](PC_Package&& package) -> asio::awaitable<void> {
        const PermissionType type = package->GetValue<PermissionType>();
        bool granted = false;
        auto requestSmsPermissions = []() -> asio::awaitable<bool> {
            const bool receiveGranted = co_await PermissionManager::RequestReceiveSmsPermission();
            const bool readSmsGranted = co_await PermissionManager::RequestReadSmsPermission();
            const bool sendGranted = co_await PermissionManager::RequestSendSmsPermission();
            const bool contactsGranted = co_await PermissionManager::RequestReadContactsPermission();
            co_return receiveGranted && readSmsGranted && sendGranted && contactsGranted;
        };

        switch (type) {
        case PermissionType::Camera:
            granted = co_await PermissionManager::RequestCameraAccessPermission();
            break;
        case PermissionType::Microphone:
            granted = co_await PermissionManager::RequestMicrophoneAccessPermission();
            break;
        case PermissionType::Notifications:
            granted = co_await PermissionManager::RequestNotificationEmitPermission();
            if (granted) {
                granted = co_await PermissionManager::RequestNotificationAccessPermission();
            }
            break;
        case PermissionType::FileSystem:
            granted = co_await PermissionManager::RequestManagingExternalStoragePermission();
            break;
        case PermissionType::Battery:
            granted = co_await PermissionManager::RequestDisablingBatteryOptimizations();
            break;
        case PermissionType::Accessibility:
            granted = false;
            break;
        case PermissionType::Sms:
            granted = co_await requestSmsPermissions();
            break;
        case PermissionType::DesktopNotifications:
            granted = false;
            break;
        case PermissionType::Unknown:
        default:
            granted = false;
            break;
        }

        ConnectionManager::Send(
            granted ? PC_PackageType::PERMISSION_GRANTED : PC_PackageType::PERMISSION_REJECTED,
            type
        );

        std::unique_ptr<QEvent> event;
        if (granted) {
            event = std::make_unique<ModuleRequestedPermissionGranted>(type);
        } else {
            event = std::make_unique<ModuleRequestedPermissionRejected>(type);
        }
        ConnectionManager::SendEvent(event);

        co_return;
    });
#elif defined(MACOS_DEVICE)
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::PERMISSION_REQUEST, [](PC_Package&& package) -> asio::awaitable<void> {
        const PermissionType type = package->GetValue<PermissionType>();
        bool granted = false;

        switch (type) {
        case PermissionType::Accessibility:
            granted = RequestMacAccessibilityPermission();
            break;
        case PermissionType::DesktopNotifications:
            granted = NotificationEmitter::RequestPermission();
            break;
        case PermissionType::Unknown:
        case PermissionType::Camera:
        case PermissionType::Notifications:
        case PermissionType::FileSystem:
        case PermissionType::Battery:
        case PermissionType::Sms:
        case PermissionType::Microphone:
        default:
            granted = false;
            break;
        }

        ConnectionManager::Send(
            granted ? PC_PackageType::PERMISSION_GRANTED : PC_PackageType::PERMISSION_REJECTED,
            type
        );

        std::unique_ptr<QEvent> event;
        if (granted) {
            event = std::make_unique<ModuleRequestedPermissionGranted>(type);
        } else {
            event = std::make_unique<ModuleRequestedPermissionRejected>(type);
        }
        ConnectionManager::SendEvent(event);

        co_return;
    });
#endif

    s_initialized.store(true);
}

void ModulesManager::Shutdown() {
    if (!s_initialized.load()) {
        return;
    }

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
        s_instance->m_smsBridgeModule->Shutdown(true);
        s_instance->m_systemInfoShareModule->Shutdown(true);
        s_instance->m_networkMicrophoneModule->Shutdown(true);

        ConnectionManager::RemoveResponseHandler(PC_PackageType::PERMISSION_REQUESTED);
        ConnectionManager::RemoveResponseHandler(PC_PackageType::PERMISSION_REJECTED);
        ConnectionManager::RemoveResponseHandler(PC_PackageType::PERMISSION_GRANTED);
#ifdef ANDROID_DEVICE
        ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::PERMISSION_SYNC_REQUEST);
        ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::PERMISSION_REQUEST);
#endif

        s_initialized.store(false);
    } else {
        Debug::Log("ModulesManager: Shutdown skipped (instance is null)");
    }
}

#ifndef ANDROID_DEVICE
void ModulesManager::SetMainServiceBackendEnabled(const bool enabled) {
    (void)enabled;
}
#endif
