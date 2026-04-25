#include <NotificationSyncModule.h>
#include <ConnectionManager.h>
#include <NotificationEmitter.h>
#include <boost/nowide/convert.hpp>
#include <atomic>
#include <optional>
#include <utility>
#include <vector>

constexpr size_t FUTURES_WAIT_DELAY = 10;
std::atomic<int64_t> g_fallbackNotificationId{-1};

namespace {
#ifdef MACOS_DEVICE
void ReportDesktopNotificationPermissionState(const bool granted, const bool requestStarted, const bool forceEmit = false) {
    static std::optional<bool> s_lastReportedState = std::nullopt;

    const bool stateChanged = !s_lastReportedState.has_value() || s_lastReportedState.value() != granted;
    if (!stateChanged && !forceEmit) {
        return;
    }

    if (!granted) {
        if (requestStarted) {
            ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::DesktopNotifications);
        }
        ConnectionManager::Send(PC_PackageType::PERMISSION_REJECTED, PermissionType::DesktopNotifications);
    } else {
        ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::DesktopNotifications);
    }

    s_lastReportedState = granted;
}
#endif
}

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

bool NotificationSyncModule::DismissNotificationByKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }

    bool knownDismissable = true;
    bool knownNotification = false;
    {
        std::lock_guard lock(m_notificationsVectorMutex);
        auto keyIt = m_notificationIdsByKey.find(key);
        if (keyIt != m_notificationIdsByKey.end()) {
            const auto notificationIt = m_notifications.find(keyIt->second);
            if (notificationIt != m_notifications.end()) {
                knownNotification = true;
                knownDismissable = notificationIt->second.dismissable;
            }
        }
    }

    if (knownNotification && !knownDismissable) {
        return false;
    }

    const std::shared_ptr<NotificationTransferChannel> channel = GetChannel();
    if (!channel || channel->GetConnectionState() != ConnectionState::CONNECTED) {
        return false;
    }

    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISMISS_NOTIFICATION, key);
    return true;
}

asio::awaitable<void> NotificationSyncModule::FetchNotificationList() {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());
    const std::shared_ptr<NotificationTransferChannel> channel = GetChannel();
    if (!channel || channel->GetConnectionState() != ConnectionState::CONNECTED) {
        Debug::LogError("Notification sync failed: transfer channel not connected");
        ProcessError(ModuleFailReason::Timeout);
        Disable();
        co_return;
    }

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NOTIFICATION_SYNC_MODULE_ALL_NOTIFICATIONS_REQUEST);
    if (!response.has_value()) {
        Debug::LogError("Notification sync failed");
        ProcessError(ModuleFailReason::Timeout);
        Disable();
        co_return;
    }

    const PC_Package& package = response.value();
    uint16_t notificationsCount = package->GetValue<uint16_t>();
    const uint16_t totalNotifications = notificationsCount;
    bool receiveFailed = false;

    ClearNotificationCache(true);

    Debug::Log("Notification sync started. Notifications to fetch: {}", totalNotifications);

    uint16_t processedNotifications = 0;

    while (notificationsCount > 0) {
        notificationsCount--;
        std::optional<NotificationPacket> notification = co_await channel->Receive();

        if (!notification.has_value()) {
            Debug::LogError("Receiving notification failed. Remaining notifications: {}", notificationsCount + 1);
            ProcessError(ModuleFailReason::Timeout);
            receiveFailed = true;
            break;
        }

        instance->ProcessNotificationPacket(notification.value(), false);
        processedNotifications++;
    }

    if (receiveFailed) {
        Debug::LogWarning("Notification sync finished with errors. Processed {} out of {}", processedNotifications, totalNotifications);
    } else {
        Debug::Log("Notification sync completed. Processed {}", processedNotifications);
    }
}

void NotificationSyncModule::ProcessNotificationPacket(const NotificationPacket& packet, const bool emitDesktopNotification) {
    const std::shared_ptr<NotificationSyncModule> instance = std::static_pointer_cast<NotificationSyncModule>(shared_from_this());

    Debug::Log("Processing notification packet");
    try {
        NotificationRecord notificationRecord;

        notificationRecord.key = packet.key;
        notificationRecord.appName = packet.appName;
        notificationRecord.title = packet.title;
        notificationRecord.content = packet.content;
        notificationRecord.timestamp = packet.timestamp;
        notificationRecord.dismissable = packet.dismissable;
        notificationRecord.buttons = packet.buttons;

        if (!packet.iconImage.empty()) {
            notificationRecord.iconPath = std::filesystem::temp_directory_path() / (boost::uuids::to_string(boost::uuids::random_generator()()) + ".png");
            std::ofstream stream(notificationRecord.iconPath.value(), std::ios::binary);

            if (!stream) {
                Debug::LogError("Could not open image file stream");
                ProcessError(ModuleFailReason::InternalError);
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
                ProcessError(ModuleFailReason::InternalError);
                return;
            }

            stream.write(reinterpret_cast<const char*>(packet.mainImage.data()), packet.mainImage.size());

        } else {
            notificationRecord.mainImagePath = std::nullopt;
        }

        ProcessNotificationRemoval(notificationRecord.key, false);

        int64_t notificationID = g_fallbackNotificationId.fetch_sub(1, std::memory_order_relaxed);
        if (emitDesktopNotification) {
            std::vector<NotificationEmitter::ButtonAction> notificationEmitterButtonActions;
            notificationEmitterButtonActions.reserve(notificationRecord.buttons.size());

            std::weak_ptr<NotificationSyncModule> weakInstance = instance;
            std::shared_ptr<int64_t> emittedNotificationID = std::make_shared<int64_t>();
            for (const auto& button : notificationRecord.buttons) {
                std::wstring buttonWString = boost::nowide::widen(button);

                notificationEmitterButtonActions.emplace_back(
                    buttonWString,
                    [weakInstance, emittedNotificationID, buttonWString]() {
                        if (const std::shared_ptr<NotificationSyncModule> module = weakInstance.lock()) {
                            module->ProcessNotificationButtonAction(*emittedNotificationID, buttonWString);
                        }
                    }
                );
            }

#ifdef MACOS_DEVICE
            notificationID = NotificationEmitter::Emit(
                boost::nowide::widen(notificationRecord.title),
                boost::nowide::widen(notificationRecord.appName),
                boost::nowide::widen(notificationRecord.content),
                notificationRecord.iconPath,
                notificationRecord.mainImagePath,
                notificationEmitterButtonActions
            );
#else
            notificationID = NotificationEmitter::Emit(
                boost::nowide::widen(notificationRecord.title),
                boost::nowide::widen(notificationRecord.content),
                notificationRecord.iconPath,
                notificationRecord.mainImagePath,
                notificationEmitterButtonActions
            );
#endif

            if (notificationID < 0) {
                notificationID = g_fallbackNotificationId.fetch_sub(1, std::memory_order_relaxed);
            } else {
                *emittedNotificationID = notificationID;
            }
        }

        {
            std::lock_guard lock(m_notificationsVectorMutex);
            m_notifications[notificationID] = notificationRecord;
            m_notificationIdsByKey[notificationRecord.key] = notificationID;
        }

        {
            std::unique_ptr<QEvent> event = std::make_unique<NotificationReceivedEvent>(notificationRecord);
            ConnectionManager::SendEvent(event);
        }

        Debug::Log("Notification packet appended to cache");
    } catch (const std::exception& exception) {
        Debug::LogError("Processing notification packet failed: {}", exception.what());
        ProcessError(ModuleFailReason::InternalError);
    } catch (...) {
        Debug::LogError("Processing notification packet failed: unknown error");
        ProcessError(ModuleFailReason::InternalError);
    }
}

bool NotificationSyncModule::ProcessNotificationRemoval(const std::string& key, const bool emitEvent) {
    if (key.empty()) {
        return false;
    }

    int64_t notificationId = -1;
    std::optional<NotificationRecord> removedRecord;

    {
        std::lock_guard lock(m_notificationsVectorMutex);
        auto keyIt = m_notificationIdsByKey.find(key);
        if (keyIt != m_notificationIdsByKey.end()) {
            notificationId = keyIt->second;
            m_notificationIdsByKey.erase(keyIt);

            auto notificationIt = m_notifications.find(notificationId);
            if (notificationIt != m_notifications.end()) {
                removedRecord = std::move(notificationIt->second);
                m_notifications.erase(notificationIt);
            }
        } else {
            for (auto notificationIt = m_notifications.begin(); notificationIt != m_notifications.end(); ++notificationIt) {
                if (notificationIt->second.key == key) {
                    notificationId = notificationIt->first;
                    removedRecord = std::move(notificationIt->second);
                    m_notifications.erase(notificationIt);
                    break;
                }
            }
        }
    }

    if (notificationId >= 0) {
        NotificationEmitter::Remove(notificationId);
    }

    if (removedRecord.has_value()) {
        if (removedRecord->iconPath.has_value()) {
            std::error_code errorCode;
            std::filesystem::remove(*removedRecord->iconPath, errorCode);
        }
        if (removedRecord->mainImagePath.has_value()) {
            std::error_code errorCode;
            std::filesystem::remove(*removedRecord->mainImagePath, errorCode);
        }

        if (emitEvent) {
            std::unique_ptr<QEvent> event = std::make_unique<NotificationRemovedEvent>(key);
            ConnectionManager::SendEvent(event);
        }
    }

    return removedRecord.has_value();
}

void NotificationSyncModule::ClearNotificationCache(const bool emitEvents) {
    std::vector<int64_t> idsToRemove;
    std::vector<NotificationRecord> removedRecords;

    {
        std::lock_guard lock(m_notificationsVectorMutex);
        idsToRemove.reserve(m_notifications.size());
        removedRecords.reserve(m_notifications.size());

        for (const auto& [id, record] : m_notifications) {
            idsToRemove.push_back(id);
            removedRecords.push_back(record);
        }

        m_notifications.clear();
        m_notificationIdsByKey.clear();
    }

    for (const int64_t id : idsToRemove) {
        NotificationEmitter::Remove(id);
    }

    for (const NotificationRecord& record : removedRecords) {
        if (record.iconPath.has_value()) {
            std::error_code errorCode;
            std::filesystem::remove(*record.iconPath, errorCode);
        }
        if (record.mainImagePath.has_value()) {
            std::error_code errorCode;
            std::filesystem::remove(*record.mainImagePath, errorCode);
        }

        if (emitEvents) {
            std::unique_ptr<QEvent> event = std::make_unique<NotificationRemovedEvent>(record.key);
            ConnectionManager::SendEvent(event);
        }
    }
}

void NotificationSyncModule::ProcessNotificationButtonAction(const int64_t id, const std::wstring& option) {
    std::lock_guard lock(m_notificationsVectorMutex);
    if (m_notifications.contains(id)) {
        const std::shared_ptr<NotificationTransferChannel> channel = GetChannel();
        if (!channel || channel->GetConnectionState() != ConnectionState::CONNECTED) {
            Debug::LogWarning("Notification action ignored: Phone is disconnected.");
            return;
        }

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
        const std::shared_ptr<NotificationTransferChannel> channel = instance->GetChannel();
        if (!channel || channel->GetConnectionState() != ConnectionState::CONNECTED) {
            Debug::LogWarning("Notification packet ignored: transfer channel not connected");
            co_return;
        }

        std::optional<NotificationPacket> notification = co_await channel->Receive();
        if (!notification.has_value()) {
            Debug::LogError("Could not receive notification");
            instance->ProcessError(ModuleFailReason::Timeout);
            co_return;
        }

        Debug::Log("Received notification packet. Key: {}, Title: {}", notification->key, notification->title);
        instance->ProcessNotificationPacket(notification.value(), true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_NOTIFICATION_REMOVED, [instance](PC_Package&& package) {
        const std::string key = package->GetValue<std::string>();
        instance->ProcessNotificationRemoval(key, true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE, [instance](PC_Package&& package) {
        Debug::Log("Received notification sync enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, true);
            return;
        }
        instance->Enable(true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE, [instance](PC_Package&& package) {
        Debug::Log("Received notification sync disable request");
        instance->m_peerModuleEnabled.store(false);
        ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, false);
        instance->Disable(true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, [instance](PC_Package&& package) {
        instance->m_peerModuleEnabled.store(package->GetValue<bool>());
    });
}

void NotificationSyncModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_NEW_NOTIFICATION);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_NOTIFICATION_REMOVED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE);
}

void NotificationSyncModule::OnInitialize() {}

asio::awaitable<void> NotificationSyncModule::OnEnable() {
    m_peerModuleEnabled.store(false);

#ifdef MACOS_DEVICE
    if (!NotificationEmitter::IsPermissionGranted()) {
        ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::DesktopNotifications);
    }

    if (!NotificationEmitter::RequestPermission()) {
        ReportDesktopNotificationPermissionState(false, false, true);
        Debug::LogWarning("NotificationSyncModule: Desktop notification permission not granted on macOS.");
        Disable();
        co_return;
    }

    ReportDesktopNotificationPermissionState(true, false, true);
#endif

    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_ENABLE);

    if (const std::shared_ptr<NotificationTransferChannel> previousChannel = TakeChannel()) {
        co_await previousChannel->Disconnect();
    }

    if (ShouldAbortEnable()) {
        co_return;
    }

    const std::shared_ptr<NotificationTransferChannel> channel = std::make_shared<NotificationTransferChannel>(ConnectionManager::GetSSLContextServer(), m_context);
    SetChannel(channel);

    AwaitableFlag flag(m_context.get_executor());
    uint16_t port{};

    asio::co_spawn(m_context, channel->Seek(flag, port), asio::detached);
    co_await flag.Wait();
    if (ShouldAbortEnable()) {
        co_return;
    }

    Debug::Log("Notification channel seek opened local port {}", port);
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_CONNECTION_PORT_INFO, port);

    asio::steady_timer timer(m_context.get_executor());
    while (channel->GetConnectionState() != ConnectionState::CONNECTED) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        if (GetChannel() != channel) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(FUTURES_WAIT_DELAY));
        co_await timer.async_wait();
    }

    if (GetChannel() != channel) {
        co_return;
    }

    Debug::Log("Notification channel connected");

    while (!m_peerModuleEnabled.load()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }

    co_await FetchNotificationList();
    if (ShouldAbortEnable()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, true);
}

asio::awaitable<void> NotificationSyncModule::OnDisable() {
    Debug::Log("Notification sync module disabling");
    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE, false);
    ConnectionManager::Send(PC_PackageType::NOTIFICATION_SYNC_MODULE_DISABLE);

    if (const std::shared_ptr<NotificationTransferChannel> channel = TakeChannel()) {
        co_await channel->Disconnect();
    }

    ClearNotificationCache(true);
    Debug::Log("Notification sync module disabled");
}

asio::awaitable<void> NotificationSyncModule::OnShutdown() {
    Debug::Log("Notification sync module shutdown");
    ClearNotificationCache(false);
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
