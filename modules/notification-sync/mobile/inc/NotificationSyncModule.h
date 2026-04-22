#ifndef NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H
#define NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H

#include <BaseModule.h>
#include <NotificationData.h>
#include <NotificationTransferChannel.h>

class NotificationSyncModule final : public BaseModule {
public:
    explicit NotificationSyncModule() : m_connectedFlag(m_context.get_executor()) {}

private:
    asio::awaitable<void> SendNewNotification(std::string key) const;
    asio::awaitable<void> SendNotificationRemoved(std::string key) const;
    void RegisterNotificationCallbacks(const std::shared_ptr<NotificationSyncModule>& instance);
    void ClearNotificationCallbacks();
    void DismissNotificationOnPhone(const std::string& key) const;
    std::shared_ptr<NotificationTransferChannel> GetChannel() const;
    void SetChannel(const std::shared_ptr<NotificationTransferChannel>& channel);
    std::shared_ptr<NotificationTransferChannel> TakeChannel();

    AwaitableFlag m_connectedFlag;
    mutable std::mutex m_channelMutex;
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
