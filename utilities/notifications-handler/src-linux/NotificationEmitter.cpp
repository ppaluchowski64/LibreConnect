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

    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        std::promise<int> promise;
        auto future = promise.get_future();

        QMetaObject::invokeMethod(
            QGuiApplication::instance(),
            [p = std::move(promise), notificationName, notificationContent, appIconPath, mainImagePath, buttons]() mutable {
                p.set_value(NotificationEmitter::Emit(notificationName, notificationContent, appIconPath, mainImagePath, buttons));
            },
            Qt::QueuedConnection
        );

        return future.get();
    }

    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return 0;

    QDBusInterface interface(
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        bus
    );

    if (!interface.isValid()) return 0;

    QString appName = "LibreConnect";
    uint retID = 0;
    QString icon = appIconPath.has_value() ?  QString::fromStdString(appIconPath.value().string()) : "";
    QString title = QString::fromStdWString(notificationName);
    QString body = QString::fromStdWString(notificationContent);

    QStringList actions;
    for (const auto& btn : buttons) {
        const boost::uuids::uuid uuid = boost::uuids::random_generator()();
        const std::string strID= boost::uuids::to_string(uuid);

        QString id    = QString::fromStdString(strID);
        QString label = QString::fromStdWString(btn.text);
        actions << id << label;
    }

    QVariantMap hints;
    if (mainImagePath.has_value()) {
        hints["image-path"] = QString::fromStdString(mainImagePath.value().string());
    }
    int timeout = -1;

    const QDBusMessage msg = interface.call(
        "Notify",
        appName,
        retID,
        icon,
        title,
        body,
        actions,
        hints,
        timeout
    );

    if (msg.type() == QDBusMessage::ErrorMessage) {
        Debug::LogError(msg.errorMessage().toStdString());
        return 0;
    }

    return static_cast<int64_t>(msg.arguments().at(0).toUInt());
}

bool NotificationEmitter::RequestPermission() {
    return true;
}

bool NotificationEmitter::IsPermissionGranted() {
    return true;
}

void NotificationEmitter::Remove(const int64_t id) {
    if (id <= 0) return;

    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        QMetaObject::invokeMethod(
            QGuiApplication::instance(),
            [id]() { NotificationEmitter::Remove(id); },
            Qt::QueuedConnection
        );
        return;
    }

    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    QDBusInterface interface(
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        bus
    );

    if (!interface.isValid()) return;
    interface.call("CloseNotification", static_cast<uint>(id));
}
