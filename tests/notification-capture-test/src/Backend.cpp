#include "Backend.h"
#include <NotificationBridge.h>
#include <QDateTime>

void Backend::notification(QString message) {
    NotificationBridge::PostNotification(NotificationData{
        "adasdasdsasdsdddd",
        "title",
        message.toStdString(),
        static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()),
        {}, {}
    });
}
