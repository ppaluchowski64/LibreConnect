#include <NotificationSyncModule.h>
#include <NotificationListenerHandler.h>
#include <ConnectionManager.h>

constexpr size_t FUTURES_WAIT_DELAY = 10;

extern std::mutex g_notificationDatasMutex;
extern std::vector<NotificationData> g_notificationDatas;
extern std::mutex g_notificationCallbackMutex;
extern std::function<void(std::string key)> g_notificationCallback;

asio::awaitable<void> NotificationSyncModule::SendNewNotification(const std::string key) const {
    if (GetModuleState() != ModuleState::Enabled) {
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
    packet.title = std::move(notification.title);
    packet.content = std::move(notification.content);
    packet.key = std::move(notification.key);
    packet.buttons = std::move(notification.buttons);
    packet.mainImage = std::move(notification.mainImage);
    packet.iconImage = std::move(notification.largeIcon);

    co_await m_channel->Send(packet);
}

void NotificationSyncModule::EnableResponseCallbacks() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE, [instance](PC_Package&& package) {
        instance->Enable();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE, [instance](PC_Package&& package) {
        instance->Disable();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_CONNECTION_PORT_INFO, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const uint16_t port = package->GetValue<uint16_t>();
        const IPAddress address = ConnectionManager::GetPeerAddress();

        m_channel = std::make_shared<NotificationTransferChannel>(ConnectionManager::GetSSLContext(), m_context);
        co_await m_channel->Connect(TCPEndpoint(address, port));
        m_connectedFlag.Signal();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ALL_NOTIFICATIONS_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID = package->GetValue<size_t>();
        uint16_t notificationCount = 0;

        std::vector<NotificationData> notifications;

        {
            std::lock_guard lock(g_notificationDatasMutex);
            notificationCount = g_notificationDatas.size();

            ConnectionManager::SendRequestResponse(
                requestID,
                PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST_RESPONSE,
                std::move(notificationCount)
            );

            notifications = g_notificationDatas;
        }

        for (auto& notification : notifications) {
            NotificationPacket packet;

            packet.title = std::move(notification.title);
            packet.content = std::move(notification.content);
            packet.key = std::move(notification.key);
            packet.buttons = std::move(notification.buttons);
            packet.mainImage = std::move(notification.mainImage);
            packet.iconImage = std::move(notification.largeIcon);

            co_await m_channel->Send(packet);
        }
    });
}

void NotificationSyncModule::DisableResponseCallbacks() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_CONNECTION_PORT_INFO);
}

void NotificationSyncModule::OnInitialize() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    {
        std::lock_guard lock(g_notificationCallbackMutex);
        g_notificationCallback = [instance](const std::string& key) {
            asio::co_spawn(instance->m_context, instance->SendNewNotification(key), asio::detached);
        };
    }
}

asio::awaitable<void> NotificationSyncModule::OnEnable() {
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);
    co_await m_connectedFlag.Wait();
}

asio::awaitable<void> NotificationSyncModule::OnDisable() {
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
    m_connectedFlag.Reset();

    if (m_channel) {
        co_await m_channel->Disconnect();
        m_channel.reset();
    }
}

asio::awaitable<void> NotificationSyncModule::OnShutdown() {
    co_return;
}
