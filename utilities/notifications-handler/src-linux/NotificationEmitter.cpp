#include <NotificationEmitter.h>
#include <QtCore>
#include <QGuiApplication>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <string>
#include <DebugLog.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

int64_t NotificationEmitter::Emit(
    const std::wstring& notificationName,
    const std::wstring& notificationContent,
    const std::optional<std::filesystem::path>& appIconPath,
    const std::optional<std::filesystem::path>& mainImagePath,
    const std::vector<ButtonAction>& buttons) {

    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        Debug::LogError("NotificationEmitter: QCoreApplication instance is null");
        return 0;
    }

    if (QThread::currentThread() != app->thread()) {
        std::promise<int64_t> promise;
        auto future = promise.get_future();

        QMetaObject::invokeMethod(
            app,
            [p = std::move(promise), notificationName, notificationContent, appIconPath, mainImagePath, buttons]() mutable {
                p.set_value(NotificationEmitter::Emit(notificationName, notificationContent, appIconPath, mainImagePath, buttons));
            },
            Qt::QueuedConnection
        );

        return future.get();
    }

    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        Debug::LogError("NotificationEmitter: D-Bus session bus is not connected");
        return 0;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "Notify"
    );

    QString appName = "LibreConnect";
    uint32_t replacesID = 0;
    QString icon = appIconPath.has_value() ? QString::fromStdString(appIconPath.value().string()) : QString();
    QString title = QString::fromStdWString(notificationName);
    QString body = QString::fromStdWString(notificationContent);

    QStringList actions;
    int actionIdx = 0;
    for (const auto& btn : buttons) {
        actions << QString::number(actionIdx++) << QString::fromStdWString(btn.text);
    }

    QVariantMap hints;
    if (mainImagePath.has_value()) {
        hints["image-path"] = QString::fromStdString(mainImagePath.value().string());
    }
    int32_t timeout = -1;

    msg << appName << replacesID << icon << title << body << actions << hints << timeout;

    const QDBusMessage reply = bus.call(msg);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        Debug::LogError("NotificationEmitter: D-Bus call failed: {}", reply.errorMessage().toStdString());
        return 0;
    }

    if (reply.arguments().isEmpty()) {
        Debug::LogError("NotificationEmitter: D-Bus reply has no arguments");
        return 0;
    }

    return static_cast<int64_t>(reply.arguments().at(0).toUInt());
}

bool NotificationEmitter::RequestPermission() {
    return true;
}

bool NotificationEmitter::IsPermissionGranted() {
    return true;
}

void NotificationEmitter::Remove(const int64_t id) {
    if (id <= 0) return;

    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return;

    if (QThread::currentThread() != app->thread()) {
        QMetaObject::invokeMethod(
            app,
            [id]() { NotificationEmitter::Remove(id); },
            Qt::QueuedConnection
        );
        return;
    }

    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "CloseNotification"
    );

    msg << static_cast<uint32_t>(id);
    bus.call(msg);
}
