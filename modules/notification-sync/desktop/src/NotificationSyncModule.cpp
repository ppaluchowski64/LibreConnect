#include <NotificationSyncModule.h>
#include <ConnectionManager.h>

constexpr size_t FUTURES_WAIT_DELAY = 10;

asio::awaitable<void> NotificationSyncModule::FetchNotificationList() {
    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NOTIFICATION_SYNC_MODULE_ALL_NOTIFICATIONS_REQUEST);
    if (!response.has_value()) {
        Debug::LogError("Notification sync failed");
        co_return;
    }

    const PC_Package& package = response.value();
    uint16_t notificationsCount = package->GetValue<uint16_t>();
    const uint16_t totalNotifications = notificationsCount;
    bool receiveFailed = false;
    Debug::Log("Notification sync started. Notifications to fetch: {}", totalNotifications);

    std::vector<std::future<void>> futures;
    futures.reserve(notificationsCount);
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    while (notificationsCount > 0) {
        notificationsCount--;
        std::optional<NotificationPacket> notification = co_await m_channel->Receive();

        if (!notification.has_value()) {
            Debug::LogError("Receiving notification failed. Remaining notifications: {}", notificationsCount + 1);
            receiveFailed = true;
            break;
        }

        futures.push_back(ThreadPool::PostFuture([instance, packet = std::move(notification.value())]() mutable {
            instance->ProcessNotificationPacket(std::move(packet));
        }));
    }

    asio::steady_timer timer(m_context.get_executor());
    Debug::Log("Waiting for {} notification processing tasks", futures.size());
    for (auto& future : futures) {
        while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            timer.expires_after(std::chrono::milliseconds(FUTURES_WAIT_DELAY));
            co_await timer.async_wait();
        }

        try {
            future.get();
        } catch (const std::exception& exception) {
            Debug::LogError("Processing notification failed: {}", exception.what());
        } catch (...) {
            Debug::LogError("Processing notification failed with unknown exception");
        }
    }

    if (receiveFailed) {
        Debug::LogWarning("Notification sync finished with errors. Processed {} out of {}", futures.size(), totalNotifications);
    } else {
        Debug::Log("Notification sync completed. Processed {}", futures.size());
    }
}

void NotificationSyncModule::ProcessNotificationPacket(NotificationPacket&& packet) {
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

    std::lock_guard lock(m_notificationsVectorMutex);
    m_notifications.emplace_back(notificationRecord);
    Debug::Log("Notification packet appended to cache");
}

void NotificationSyncModule::EnableResponseCallbacks() {

}

void NotificationSyncModule::DisableResponseCallbacks() {

}

void NotificationSyncModule::OnInitialize() {

}

asio::awaitable<void> NotificationSyncModule::OnEnable() {
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);

}

asio::awaitable<void> NotificationSyncModule::OnDisable() {
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
}

asio::awaitable<void> NotificationSyncModule::OnShutdown() {

}
