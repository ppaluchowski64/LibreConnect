#include <NotificationEmitter.h>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <QVariantMap>
#include <QStringList>
#include <string>

uint64_t NotificationEmitter::Emit(
    const std::wstring& notificationName,
    const std::wstring& notificationContent,
    const std::optional<std::filesystem::path>& appIconPath,
    const std::optional<std::filesystem::path>& mainImagePath,
    const std::vector<std::wstring>& buttons) {

    QDBusInterface interface(
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        QDBusConnection::sessionBus()
    );

    if (!interface.isValid()) return 0;

    QString appName = "LibreConnect";
    uint reitID = 0;
    QString icon = appIconPath ? QString::fromStdWString(appIconPath->wstring()) : "";
    QString title = QString::fromStdWString(notificationName);
    QString body = QString::fromStdWString(notificationContent);

    QStringList actions;
    for (const auto& btn : buttons) {
        actions << QString::fromStdWString(btn) << QString::fromStdWString(btn);
    }

    QVariantMap hints;
    if (mainImagePath) {
        hints["image-path"] = QString::fromStdWString(mainImagePath->wstring());
    }
    int timeout = -1;

    const QDBusReply<uint> reply = interface.call(
        "Notify",
        appName,
        reitID,
        icon,
        title,
        body,
        actions,
        hints,
        timeout
    );

    if (reply.isValid()) {
        return static_cast<uint64_t>(reply.value());
    }

    return 0;
}

void NotificationEmitter::Remove(const uint64_t id) {
    QDBusInterface interface(
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        QDBusConnection::sessionBus()
    );

    if (interface.isValid() && id > 0) {
        interface.call("CloseNotification", static_cast<uint>(id));
    }
}