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
#include <filesystem>
#include <DebugLog.h>
#include "DeviceDiscovery.h"
#include "DeviceModel.h"
#include "DeviceConnectionController.h"
#include "NotificationSyncController.h"
#include "ClipboardSyncController.h"
#include "SmsBridgeController.h"
#include "PermissionStateController.h"
#include "FileManagerController.h"
#include "TemporaryStorageController.h"
#ifndef MACOS_DEVICE
#include "VirtualCameraController.h"
#endif
#include "ThemeController.h"

void LibreConnectLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString logMessage = QString("[QT] %1").arg(msg);
    std::string stdMsg = logMessage.toStdString();

    switch (type) {
    case QtDebugMsg:
        Debug::Log(stdMsg);
        break;
    case QtInfoMsg:
        Debug::Log(stdMsg);
        break;
    case QtWarningMsg:
        Debug::LogWarning(stdMsg);
        break;
    case QtCriticalMsg:
        Debug::LogError(stdMsg);
        break;
    case QtFatalMsg:
        Debug::LogError(stdMsg);
        abort();
    }
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("LibreConnect");
    app.setApplicationName("LibreConnect");

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataPath.isEmpty()) {
        QDir().mkpath(appDataPath);
        QDir::setCurrent(appDataPath);
        QDir::setCurrent(QCoreApplication::applicationDirPath());

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

    std::filesystem::current_path(appDataPath.toStdString());

    qInstallMessageHandler(LibreConnectLogHandler);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QFontDatabase::addApplicationFont(QStringLiteral(":/LibreConnect/desktop/Inter-VariableFont.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/LibreConnect/desktop/Inter-Italic-VariableFont.ttf"));
    app.setFont(QFont(QStringLiteral("Inter")));

    ThemeController themeController;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &themeController);

    qmlRegisterType<DeviceDiscovery>("LibreConnect.desktop", 1, 0, "DeviceDiscovery");
    qmlRegisterType<DeviceModel>("LibreConnect.desktop", 1, 0, "DeviceModel");
    qmlRegisterType<DeviceConnectionController>("LibreConnect.desktop", 1, 0, "DeviceConnectionController");
    qmlRegisterType<NotificationSyncController>("LibreConnect.desktop", 1, 0, "NotificationSyncController");
    qmlRegisterType<ClipboardSyncController>("LibreConnect.desktop", 1, 0, "ClipboardSyncController");
    qmlRegisterType<SmsBridgeController>("LibreConnect.desktop", 1, 0, "SmsBridgeController");
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
        []() {
            Debug::LogError("Failed to create QML object");
            QCoreApplication::exit(-1);
        },
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
