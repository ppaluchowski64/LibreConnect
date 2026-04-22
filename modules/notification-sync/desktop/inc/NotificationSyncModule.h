#ifndef NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H
#define NOTIFICATIONLISTENER_KT_NOTIFICATIONSYNCMODULE_H

#include <BaseModule.h>
#include <NotificationData.h>
#include <NotificationTransferChannel.h>
#include <mutex>
#include <unordered_map>
#include <utility>

class NotificationReceivedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 340);
    explicit NotificationReceivedEvent(const NotificationRecord& record) : QEvent(Type), m_record(record) {}
    const NotificationRecord& GetNotification() const { return m_record; }

    NotificationReceivedEvent* clone() const override {
        return new NotificationReceivedEvent(*this);
    }

private:
    NotificationRecord m_record;

};

class NotificationRemovedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 341);
    explicit NotificationRemovedEvent(std::string key) : QEvent(Type), m_key(std::move(key)) {}
    const std::string& GetKey() const { return m_key; }

    NotificationRemovedEvent* clone() const override {
        return new NotificationRemovedEvent(*this);
    }

private:
    std::string m_key;
};

class NotificationSyncModule final : public BaseModule {
public:
    bool DismissNotificationByKey(const std::string& key);

private:
    asio::awaitable<void> FetchNotificationList();
    void ProcessNotificationPacket(const NotificationPacket& packet, bool emitDesktopNotification);
    bool ProcessNotificationRemoval(const std::string& key, bool emitEvent);
    void ClearNotificationCache(bool emitEvents);
    void ProcessNotificationButtonAction(int64_t id, const std::wstring& option);
    std::shared_ptr<NotificationTransferChannel> GetChannel() const;
    void SetChannel(const std::shared_ptr<NotificationTransferChannel>& channel);
    std::shared_ptr<NotificationTransferChannel> TakeChannel();

    std::mutex m_notificationsVectorMutex;
    mutable std::mutex m_channelMutex;
    std::unordered_map<int64_t, NotificationRecord> m_notifications;
    std::unordered_map<std::string, int64_t> m_notificationIdsByKey;
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
