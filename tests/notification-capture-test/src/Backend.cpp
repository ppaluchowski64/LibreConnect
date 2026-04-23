#include "Backend.h"

#include <DebugLog.h>
#include <NotificationBridge.h>
#include <NotificationData.h>
#include <QDateTime>
#include <vector>

extern std::vector<NotificationData> g_notificationDatas;

void Backend::notification(QString message) {
    Debug::Log("Notification message: {}", message.toStdString());

    NotificationBridge::PostNotification(NotificationData(
        "adasdasdsasdsdddd",
        "dd",
        "title",
        message.toStdString(),
        static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()),
        false,
        {"button"},
        {}, {}, {}
    ));

    NotificationBridge::AddNotificationActionHandler("adasdasdsasdsdddd", "button", []() {
        NotificationBridge::PostNotification(NotificationData(
            "adasdasdsasdsddddddd",
            "dd",
            "Button clicked",
            "button was clicked",
            static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()),
            false,
            {},{}, {}, {}
        ));
    });
}

void Backend::displayNotifications() {
    Debug::Log("Captured notifications count: {}", g_notificationDatas.size());

    for (size_t i = 0; i < g_notificationDatas.size(); ++i) {
        const auto& notification = g_notificationDatas[i];
        Debug::Log(
            "[{}] key='{}', title='{}', content='{}', timestamp={}",
            i,
            notification.key,
            notification.title,
            notification.content,
            notification.timestamp
        );
    }
}
