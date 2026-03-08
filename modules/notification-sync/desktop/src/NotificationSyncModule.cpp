#include <NotificationSyncModule.h>
#include <ConnectionManager.h>
#include <NotificationEmitter.h>
#include <boost/nowide/convert.hpp>
#include <exception>
#include <future>

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
    Debug::Log("Notification sync started. Notifications to fetch: {}", totalNotifications);

    std::vector<std::future<void>> futures;
    futures.reserve(notificationsCount);

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

    std::promise<void> completionPromise;
    std::future<void> completionFuture = completionPromise.get_future();
    std::weak_ptr<NotificationSyncModule> weakInstance = instance;

    asio::post(m_moduleStrand, [weakInstance, notificationRecord = std::move(notificationRecord), completionPromise = std::move(completionPromise)]() mutable {
        try {
            const std::shared_ptr<NotificationSyncModule> lockedInstance = weakInstance.lock();
            if (!lockedInstance) {
                completionPromise.set_value();
                return;
            }

            std::vector<NotificationEmitter::ButtonAction> notificationEmitterButtonActions;
            notificationEmitterButtonActions.reserve(notificationRecord.buttons.size());

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

            std::lock_guard lock(lockedInstance->m_notificationsVectorMutex);
            lockedInstance->m_notifications[*notificationID] = std::move(notificationRecord);
            Debug::Log("Notification packet appended to cache");
            completionPromise.set_value();
        } catch (...) {
            completionPromise.set_exception(std::current_exception());
        }
    });

    completionFuture.get();
}

void NotificationSyncModule::EnableResponseCallbacks() {

}

void NotificationSyncModule::DisableResponseCallbacks() {

}

void NotificationSyncModule::OnInitialize() {

}

asio::awaitable<void> NotificationSyncModule::OnEnable() {
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);
    co_return;
}

asio::awaitable<void> NotificationSyncModule::OnDisable() {
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
    co_return;
}

asio::awaitable<void> NotificationSyncModule::OnShutdown() {
    co_return;
}
