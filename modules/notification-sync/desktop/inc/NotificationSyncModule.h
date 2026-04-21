#ifndef NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H
#define NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H

#include <BaseModule.h>
#include <NotificationData.h>
#include <NotificationTransferChannel.h>

class NotificationReceivedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 301);
    explicit NotificationReceivedEvent(const NotificationRecord& record) : QEvent(Type), m_record(record) {}
    const NotificationRecord& GetNotification() const { return m_record; }

    NotificationReceivedEvent* clone() const override {
        return new NotificationReceivedEvent(*this);
    }

private:
    NotificationRecord m_record;

};

class NotificationSyncModule final : public BaseModule {
private:
    asio::awaitable<void> FetchNotificationList();
    void ProcessNotificationPacket(const NotificationPacket& packet);
    void ProcessNotificationButtonAction(int64_t id, const std::wstring& option);
    std::shared_ptr<NotificationTransferChannel> GetChannel() const;
    void SetChannel(const std::shared_ptr<NotificationTransferChannel>& channel);
    std::shared_ptr<NotificationTransferChannel> TakeChannel();

    std::mutex m_notificationsVectorMutex;
    mutable std::mutex m_channelMutex;
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
