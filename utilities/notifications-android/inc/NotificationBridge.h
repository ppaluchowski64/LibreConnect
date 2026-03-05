#ifndef NOTIFICATION_LISTENER_KT_KOTLIN_POST_NOTIFICATION_HANDLER_H
#define NOTIFICATION_LISTENER_KT_KOTLIN_POST_NOTIFICATION_HANDLER_H

#include <NotificationData.h>
#include <ConcurrentUnorderedMap.h>

class NotificationBridge {
public:
    static void PostNotification(const NotificationData& notificationData);
    static void RemoveNotification(const std::string& key);
    static void AddNotificationActionHandler(const std::string& key, const std::string& option, std::function<void()> callback);
    static void CallNotificationActionHandler(const std::string& key, const std::string& option);
    static void InitializePermissions();

private:
    static ConcurrentUnorderedMap<std::string, std::function<void()>> m_notificationHandlers;

};

#endif // NOTIFICATION_LISTENER_KT_KOTLIN_POST_NOTIFICATION_HANDLER_H