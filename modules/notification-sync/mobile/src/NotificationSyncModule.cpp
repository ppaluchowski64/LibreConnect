#include <NotificationSyncModule.h>
#include <NotificationListenerHandler.h>
#include <ConnectionManager.h>
#include <PermissionManager.h>
#include <QString>
#include <utility>

constexpr size_t FUTURES_WAIT_DELAY = 10;

extern std::mutex g_notificationDatasMutex;
extern std::vector<NotificationData> g_notificationDatas;
extern std::mutex g_notificationCallbackMutex;
extern std::function<void(const std::string& key)> g_notificationCallback;
extern std::mutex g_notificationRemovedCallbackMutex;
extern std::function<void(const std::string& key)> g_notificationRemovedCallback;

std::shared_ptr<NotificationTransferChannel> NotificationSyncModule::GetChannel() const {
    std::lock_guard lock(m_channelMutex);
    return m_channel;
}

void NotificationSyncModule::SetChannel(const std::shared_ptr<NotificationTransferChannel>& channel) {
    std::lock_guard lock(m_channelMutex);
    m_channel = channel;
}

std::shared_ptr<NotificationTransferChannel> NotificationSyncModule::TakeChannel() {
    std::lock_guard lock(m_channelMutex);
    return std::exchange(m_channel, nullptr);
}

asio::awaitable<void> NotificationSyncModule::SendNewNotification(const std::string key) const {
    const ModuleState state = GetModuleState();
    if (state != ModuleState::Enabled && state != ModuleState::Enabling) {
        co_return;
    }

    const std::shared_ptr<NotificationTransferChannel> channel = GetChannel();
    if (!channel || channel->GetConnectionState() != ConnectionState::CONNECTED) {
        Debug::LogWarning("Notification transfer channel not connected, skipping new notification send");
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_NEW_NOTIFICATION);
    NotificationData notification;
    bool found{false};

    {
        std::lock_guard lock(g_notificationDatasMutex);
        for (int i = g_notificationDatas.size() - 1; i >= 0; i--) {
            if (g_notificationDatas[i].key == key) {
                notification = g_notificationDatas[i];
                found = true;
                break;
            }
        }

        if (!found) {
            co_return;
        }
    }

    NotificationPacket packet;
    packet.appName = std::move(notification.appName);
    packet.title = std::move(notification.title);
    packet.content = std::move(notification.content);
    packet.key = std::move(notification.key);
    packet.timestamp = notification.timestamp;
    packet.dismissable = notification.dismissable;
    packet.buttons = std::move(notification.buttons);
    packet.mainImage = std::move(notification.mainImage);
    packet.iconImage = std::move(notification.largeIcon);

    co_await channel->Send(packet);
}

asio::awaitable<void> NotificationSyncModule::SendNotificationRemoved(std::string key) const {
    const ModuleState state = GetModuleState();
    if (state != ModuleState::Enabled && state != ModuleState::Enabling) {
        co_return;
    }

    if (key.empty()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_NOTIFICATION_REMOVED, std::move(key));
}

void NotificationSyncModule::RegisterNotificationCallbacks(const std::shared_ptr<NotificationSyncModule>& instance) {
    {
        std::lock_guard lock(g_notificationCallbackMutex);
        g_notificationCallback = [instance](const std::string& key) {
            asio::co_spawn(instance->m_context, instance->SendNewNotification(key), asio::detached);
        };
    }

    {
        std::lock_guard lock(g_notificationRemovedCallbackMutex);
        g_notificationRemovedCallback = [instance](const std::string& key) {
            asio::co_spawn(instance->m_context, instance->SendNotificationRemoved(key), asio::detached);
        };
    }
}

void NotificationSyncModule::ClearNotificationCallbacks() {
    {
        std::lock_guard lock(g_notificationCallbackMutex);
        g_notificationCallback = {};
    }

    {
        std::lock_guard lock(g_notificationRemovedCallbackMutex);
        g_notificationRemovedCallback = {};
    }
}

void NotificationSyncModule::DismissNotificationOnPhone(const std::string& key) const {
    if (key.empty()) {
        return;
    }

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }

    const QJniObject jKey = QJniObject::fromString(QString::fromStdString(key));
    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/NotificationListener",
        "dismissNotification",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        context.object<jobject>(),
        jKey.object<jstring>()
    );
}

void NotificationSyncModule::EnableResponseCallbacks() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE, [instance](PC_Package&& package) {
        Debug::Log("Received notification sync enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, true);
            return;
        }
        instance->Enable();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE, [instance](PC_Package&& package) {
        Debug::Log("Received notification sync disable request");
        instance->m_peerModuleEnabled.store(false);
        ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, false);
        instance->Disable();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISMISS_NOTIFICATION, [instance](PC_Package&& package) {
        const std::string key = package->GetValue<std::string>();
        instance->DismissNotificationOnPhone(key);
    });

    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_CONNECTION_PORT_INFO, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const uint16_t port = package->GetValue<uint16_t>();
        const IPAddress address = ConnectionManager::GetPeerAddress();

        Debug::Log("Received notification sync port info. Address: {}, Port: {}", address.to_string(), port);
        const std::shared_ptr<NotificationTransferChannel> channel = std::make_shared<NotificationTransferChannel>(ConnectionManager::GetSSLContextClient(), m_context);
        SetChannel(channel);
        Debug::Log("Connecting notification transfer channel");
        co_await channel->Connect(TCPEndpoint(address, port));
        Debug::Log("Notification transfer channel connected");
        if (channel->GetConnectionState() != ConnectionState::CONNECTED || GetChannel() != channel) {
            Debug::LogError("Notification transfer channel failed to connect");
            instance->ProcessError(ModuleFailReason::Timeout);
            co_return;
        }
        m_connectedFlag.Signal();
    });

    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ALL_NOTIFICATIONS_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID = package->GetValue<size_t>();
        uint16_t notificationCount = 0;

        std::vector<NotificationData> notifications;
        const std::shared_ptr<NotificationTransferChannel> channel = GetChannel();

        Debug::Log("Received all notifications request. RequestID: {}", requestID);
        {
            std::lock_guard lock(g_notificationDatasMutex);
            notificationCount = g_notificationDatas.size();

            ConnectionManager::SendRequestResponse(
                requestID,
                PC_PackageType::NOTIFICATION_SYNC_MODULE_ALL_NOTIFICATIONS_RESPONSE,
                std::move(notificationCount)
            );

            notifications = g_notificationDatas;
        }

        if (!channel || channel->GetConnectionState() != ConnectionState::CONNECTED) {
            Debug::LogWarning("Notification transfer channel not connected, cannot send notifications");
            co_return;
        }

        Debug::Log("Sending {} notifications", notificationCount);
        for (auto& notification : notifications) {
            NotificationPacket packet;

            packet.appName = std::move(notification.appName);
            packet.title = std::move(notification.title);
            packet.content = std::move(notification.content);
            packet.key = std::move(notification.key);
            packet.timestamp = notification.timestamp;
            packet.dismissable = notification.dismissable;
            packet.buttons = std::move(notification.buttons);
            packet.mainImage = std::move(notification.mainImage);
            packet.iconImage = std::move(notification.largeIcon);

            co_await channel->Send(packet);
        }
        Debug::Log("Finished sending notifications");
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, [instance](PC_Package&& package) {
        instance->m_peerModuleEnabled.store(package->GetValue<bool>());
    });
}

void NotificationSyncModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISMISS_NOTIFICATION);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_CONNECTION_PORT_INFO);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ALL_NOTIFICATIONS_REQUEST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE);
}

void NotificationSyncModule::OnInitialize() {}

asio::awaitable<void> NotificationSyncModule::OnEnable() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());
    m_peerModuleEnabled.store(false);
    ClearNotificationCallbacks();

    ClearNotificationDatas();

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/NotificationListener",
        "requestSync",
        "(Landroid/content/Context;)V",
        context.object<jobject>()
    );

    ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::Notifications);
    if (!co_await PermissionManager::RequestNotificationEmitPermission()) {
        ConnectionManager::Send(PC_PackageType::PERMISSION_REJECTED, PermissionType::Notifications);
        Disable();
        co_return;
    }
    ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::Notifications);

    if (ShouldAbortEnable()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::Notifications);
    if (!co_await PermissionManager::RequestNotificationAccessPermission()) {
        ConnectionManager::Send(PC_PackageType::PERMISSION_REJECTED, PermissionType::Notifications);
        Disable();
        co_return;
    }
    ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::Notifications);

    if (ShouldAbortEnable()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::Battery);
    if (!co_await PermissionManager::RequestDisablingBatteryOptimizations()) {
        ConnectionManager::Send(PC_PackageType::PERMISSION_REJECTED, PermissionType::Battery);
        Debug::LogWarning(
            "NotificationSyncModule: Battery optimization is still enabled; notification relay reliability may be reduced"
        );
    } else {
        ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::Battery);
    }

    if (ShouldAbortEnable()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);
    if (co_await m_connectedFlag.WaitFor(std::chrono::seconds(5)) == AwaitableFlag::Result::TIMEOUT) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        ProcessError(ModuleFailReason::Timeout);
        Disable();
        co_return;
    }

    if (ShouldAbortEnable()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, true);

    asio::steady_timer timer(m_context.get_executor());
    while (!m_peerModuleEnabled.load()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }

    RegisterNotificationCallbacks(instance);
}

asio::awaitable<void> NotificationSyncModule::OnDisable() {
    ClearNotificationCallbacks();

    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, false);
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
    m_connectedFlag.Reset();

    if (const std::shared_ptr<NotificationTransferChannel> channel = TakeChannel()) {
        co_await channel->Disconnect();
    }

}

asio::awaitable<void> NotificationSyncModule::OnShutdown() {
    ClearNotificationCallbacks();

    if (const std::shared_ptr<NotificationTransferChannel> channel = TakeChannel()) {
        co_await channel->Disconnect();
    }

    co_return;
}

const char* NotificationSyncModule::GetModuleName() const {
    return "NotificationSyncModule";
}

ModuleType NotificationSyncModule::GetModuleType() const {
    return ModuleType::NotificationSync;
}
