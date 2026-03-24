#ifndef NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H
#define NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H

#include <BaseModule.h>
#include <NotificationData.h>
#include <NotificationTransferChannel.h>

class NotificationSyncModule final : public BaseModule {
private:
    asio::awaitable<void> FetchNotificationList();
    void ProcessNotificationPacket(NotificationPacket&& packet);
    void ProcessNotificationButtonAction(int64_t id, std::wstring&& option);

    std::mutex m_notificationsVectorMutex;
    std::unordered_map<int64_t, NotificationRecord> m_notifications;
    std::shared_ptr<NotificationTransferChannel> m_channel;

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;
};

#endif //NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H
