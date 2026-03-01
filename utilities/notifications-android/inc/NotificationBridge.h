#ifndef NOTIFICATION_LISTENER_KT_KOTLIN_POST_NOTIFICATION_HANDLER_H
#define NOTIFICATION_LISTENER_KT_KOTLIN_POST_NOTIFICATION_HANDLER_H

#include <unordered_map>
#include <NotificationData.h>

class NotificationBridge {
public:
    static void PostNotification(const NotificationData& notificationData);
    static void RemoveNotification(const std::string& key);

};

#endif // NOTIFICATION_LISTENER_KT_KOTLIN_POST_NOTIFICATION_HANDLER_H