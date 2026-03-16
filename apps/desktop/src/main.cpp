#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QtQml>
#include <DebugLog.h>
#include "DeviceDiscovery.h"
#include "DeviceModel.h"
#include "DeviceConnectionController.h"
#include "NotificationSyncController.h"
#include "FileManagerController.h"
#include "VirtualCameraController.h"


int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("LibreConnect");
    app.setApplicationName("LibreConnect");

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataPath.isEmpty()) {
        QDir().mkpath(appDataPath);
        QDir::setCurrent(appDataPath);

        Debug::Settings settings{
            .rootPath = appDataPath.toStdString(),
            .maxFileSize = 2 * 1024 * 1024,
            .maxLogFilesAmount = 10,
            .deleteLogsAfter = 60 * 60 * 24 * 7
        };

        try {
            Debug::SetSettings(settings);
        } catch (...) {}
    }

    QQmlApplicationEngine engine;
    qmlRegisterType<DeviceDiscovery>("LibreConnect.desktop", 1, 0, "DeviceDiscovery");
    qmlRegisterType<DeviceModel>("LibreConnect.desktop", 1, 0, "DeviceModel");
    qmlRegisterType<DeviceConnectionController>("LibreConnect.desktop", 1, 0, "DeviceConnectionController");
    qmlRegisterType<NotificationSyncController>("LibreConnect.desktop", 1, 0, "NotificationSyncController");
    qmlRegisterType<FileManagerController>("LibreConnect.desktop", 1, 0, "FileManagerController");
    qmlRegisterType<VirtualCameraController>("LibreConnect.desktop", 1, 0, "VirtualCameraController");

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
