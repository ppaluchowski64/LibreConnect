#include "Backend.h"
#include <NotificationBridge.h>

void Backend::notification(QString message) {
    NotificationBridge::PostNotification(NotificationData{
        "adasdasdsasdsdddd",
        "title",
        message.toStdString(),
        0, {}, {}
    });
}