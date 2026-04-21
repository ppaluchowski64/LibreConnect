#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QtQml>
#include <QFontDatabase>
#include <QFont>
#include <QQuickStyle>
#include <DebugLog.h>
#include "DeviceDiscovery.h"
#include "DeviceModel.h"
#include "DeviceConnectionController.h"
#include "NotificationSyncController.h"
#include "ClipboardSyncController.h"
#include "PermissionStateController.h"
#include "FileManagerController.h"
#include "TemporaryStorageController.h"
#ifndef MACOS_DEVICE
#include "VirtualCameraController.h"
#endif
#include "ThemeController.h"


int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    app.setOrganizationName("LibreConnect");
    app.setApplicationName("LibreConnect");

    QFontDatabase::addApplicationFont(QStringLiteral(":/LibreConnect/desktop/Inter-VariableFont.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/LibreConnect/desktop/Inter-Italic-VariableFont.ttf"));
    app.setFont(QFont(QStringLiteral("Inter")));

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataPath.isEmpty()) {
        QDir().mkpath(appDataPath);
        QDir::setCurrent(appDataPath);

        const Debug::Settings settings{
            .rootPath = appDataPath.toStdString(),
            .maxFileSize = 2 * 1024 * 1024 * 1024ULL,
            .maxLogFilesAmount = 10,
            .deleteLogsAfter = 60 * 60 * 24 * 7
        };

        try {
            Debug::SetSettings(settings);
        } catch (...) {}
    }

    ThemeController themeController;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &themeController);

    qmlRegisterType<DeviceDiscovery>("LibreConnect.desktop", 1, 0, "DeviceDiscovery");
    qmlRegisterType<DeviceModel>("LibreConnect.desktop", 1, 0, "DeviceModel");
    qmlRegisterType<DeviceConnectionController>("LibreConnect.desktop", 1, 0, "DeviceConnectionController");
    qmlRegisterType<NotificationSyncController>("LibreConnect.desktop", 1, 0, "NotificationSyncController");
    qmlRegisterType<ClipboardSyncController>("LibreConnect.desktop", 1, 0, "ClipboardSyncController");
    qmlRegisterType<PermissionStateController>("LibreConnect.desktop", 1, 0, "PermissionStateController");
    qmlRegisterType<FileManagerController>("LibreConnect.desktop", 1, 0, "FileManagerController");
    qmlRegisterType<TemporaryStorageController>("LibreConnect.desktop", 1, 0, "TemporaryStorageController");
#ifndef MACOS_DEVICE
    qmlRegisterType<VirtualCameraController>("LibreConnect.desktop", 1, 0, "VirtualCameraController");
#endif

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    const QUrl url = QUrl("qrc:/LibreConnect/desktop/MainWindow.qml");

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                 [url](QObject *obj, const QUrl& urld) {
                     if (!obj) {
                         qDebug() << "Failed to load QML at" << urld;
                     }

                     if (url != urld) {
                         qDebug() << "Qml url does not match url" << url;
                     }

                     qDebug() << "Qml object created";
                 },
                 Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
