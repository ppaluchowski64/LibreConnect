#include <NotificationSyncModule.h>
#include <ConnectionManager.h>
#include <NotificationEmitter.h>
#include <boost/nowide/convert.hpp>

constexpr size_t FUTURES_WAIT_DELAY = 10;

asio::awaitable<void> NotificationSyncModule::FetchNotificationList() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());
    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NOTIFICATION_SYNC_MODULE_ALL_NOTIFICATIONS_REQUEST);
    if (!response.has_value()) {
        Debug::LogError("Notification sync failed");
        co_return;
    }

    const PC_Package& package = response.value();
    uint16_t notificationsCount = package->GetValue<uint16_t>();
    const uint16_t totalNotifications = notificationsCount;
    bool receiveFailed = false;

    {
        std::lock_guard lock(m_notificationsVectorMutex);
        m_notifications.clear();
    }

    Debug::Log("Notification sync started. Notifications to fetch: {}", totalNotifications);

    uint16_t processedNotifications = 0;

    while (notificationsCount > 0) {
        notificationsCount--;
        std::optional<NotificationPacket> notification = co_await m_channel->Receive();

        if (!notification.has_value()) {
            Debug::LogError("Receiving notification failed. Remaining notifications: {}", notificationsCount + 1);
            receiveFailed = true;
            break;
        }

        instance->ProcessNotificationPacket(std::move(notification.value()));
        processedNotifications++;
    }

    if (receiveFailed) {
        Debug::LogWarning("Notification sync finished with errors. Processed {} out of {}", processedNotifications, totalNotifications);
    } else {
        Debug::Log("Notification sync completed. Processed {}", processedNotifications);
    }
}

void NotificationSyncModule::ProcessNotificationPacket(NotificationPacket&& packet) {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    Debug::Log("Processing notification packet");
    NotificationRecord notificationRecord;

    notificationRecord.key = std::move(packet.key);
    notificationRecord.title = std::move(packet.title);
    notificationRecord.content = std::move(packet.content);
    notificationRecord.timestamp = std::move(packet.timestamp);
    notificationRecord.buttons = std::move(packet.buttons);

    if (!packet.iconImage.empty()) {
        notificationRecord.iconPath = std::filesystem::temp_directory_path() / (boost::uuids::to_string(boost::uuids::random_generator()()) + ".png");
        std::ofstream stream(notificationRecord.iconPath.value(), std::ios::binary);

        if (!stream) {
            Debug::LogError("Could not open image file stream");
            return;
        }

        stream.write(reinterpret_cast<const char*>(packet.iconImage.data()), packet.iconImage.size());

    } else {
        notificationRecord.iconPath = std::nullopt;
    }

    if (!packet.mainImage.empty()) {
        notificationRecord.mainImagePath = std::filesystem::temp_directory_path() / (boost::uuids::to_string(boost::uuids::random_generator()()) + ".png");
        std::ofstream stream(notificationRecord.mainImagePath.value(), std::ios::binary);

        if (!stream) {
            Debug::LogError("Could not open image file stream");
            return;
        }

        stream.write(reinterpret_cast<const char*>(packet.mainImage.data()), packet.mainImage.size());

    } else {
        notificationRecord.mainImagePath = std::nullopt;
    }

    std::vector<NotificationEmitter::ButtonAction> notificationEmitterButtonActions;
    notificationEmitterButtonActions.reserve(notificationRecord.buttons.size());

    std::weak_ptr<NotificationSyncModule> weakInstance = instance;
    std::shared_ptr<int64_t> notificationID = std::make_shared<int64_t>();
    for (const auto& button : notificationRecord.buttons) {
        std::wstring buttonWString = boost::nowide::widen(button);

        notificationEmitterButtonActions.emplace_back(
            buttonWString,
            [weakInstance, notificationID, buttonWString]() mutable {
                if (const std::shared_ptr<NotificationSyncModule> module = weakInstance.lock()) {
                    module->ProcessNotificationButtonAction(*notificationID, std::move(buttonWString));
                }
            }
        );
    }

    *notificationID = NotificationEmitter::Emit(
        boost::nowide::widen(notificationRecord.title),
        boost::nowide::widen(notificationRecord.content),
        notificationRecord.iconPath,
        notificationRecord.mainImagePath,
        notificationEmitterButtonActions
    );

    std::lock_guard lock(m_notificationsVectorMutex);
    m_notifications[*notificationID] = std::move(notificationRecord);
    Debug::Log("Notification packet appended to cache");
}

void NotificationSyncModule::ProcessNotificationButtonAction(const int64_t id, std::wstring&& option) {
    std::lock_guard lock(m_notificationsVectorMutex);
    if (m_notifications.contains(id)) {
        Debug::Log("Notification button action pressed. NotificationID: {}", id);
        ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_BUTTON_ACTION_PRESSED, std::string(m_notifications.at(id).key), boost::nowide::narrow(option));
    } else {
        Debug::LogWarning("Notification button action ignored. Unknown notification id: {}", id);
    }
}

void NotificationSyncModule::EnableResponseCallbacks() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_NEW_NOTIFICATION, [instance](PC_Package&& package) -> asio::awaitable<void> {
        Debug::Log("Received new-notification signal");
        std::optional<NotificationPacket> notification = co_await instance->m_channel->Receive();
        if (!notification.has_value()) {
            Debug::LogError("Could not receive notification");
            co_return;
        }

        Debug::Log("Received notification packet. Key: {}, Title: {}", notification->key, notification->title);
        instance->ProcessNotificationPacket(std::move(notification.value()));
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE, [instance](PC_Package&& package) {
        Debug::Log("Received notification sync enable request");
        instance->Enable(true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE, [instance](PC_Package&& package) {
        Debug::Log("Received notification sync disable request");
        instance->Disable(true);
    });
}

void NotificationSyncModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_NEW_NOTIFICATION);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
}

void NotificationSyncModule::OnInitialize() {}

asio::awaitable<void> NotificationSyncModule::OnEnable() {
    Debug::Log("Notification sync module enabling");
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);

    m_channel.reset();
    m_channel = std::make_shared<NotificationTransferChannel>(ConnectionManager::GetSSLContextServer(), m_context);

    AwaitableFlag flag(m_context.get_executor());
    uint16_t port{};

    asio::co_spawn(m_context, m_channel->Seek(flag, port), asio::detached);
    co_await flag.Wait();
    Debug::Log("Notification channel seek opened local port {}", port);
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_CONNECTION_PORT_INFO, port);

    asio::steady_timer timer(m_context.get_executor());
    while (m_channel && m_channel->GetConnectionState() != ConnectionState::CONNECTED) {
        timer.expires_after(std::chrono::milliseconds(FUTURES_WAIT_DELAY));
        co_await timer.async_wait();
    }
    Debug::Log("Notification channel connected");

    co_await FetchNotificationList();
    Debug::Log("Notification sync module enabled");
    co_return;
}

asio::awaitable<void> NotificationSyncModule::OnDisable() {
    Debug::Log("Notification sync module disabling");
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);

    if (m_channel) {
        co_await m_channel->Disconnect();
        m_channel.reset();
    }

    {
        std::lock_guard lock(m_notificationsVectorMutex);
        m_notifications.clear();
    }
    Debug::Log("Notification sync module disabled");
}

asio::awaitable<void> NotificationSyncModule::OnShutdown() {
    Debug::Log("Notification sync module shutdown");
    m_channel.reset();
    co_return;
}
