#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml>
#include <QQmlContext>
#include <QStandardPaths>
#ifdef ANDROID_DEVICE
#include <jni.h>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif
#include <ModulesManager.h>
#include <DebugLog.h>
#include "MobileConnectionController.h"
#include "AndroidAdvertiser.h"
#include "MobileThemeController.h"
#include "MobileNotificationSyncController.h"
#include "MobileClipboardSyncController.h"
#include "MobileRemoteInputController.h"
#include "MobileMediaNotificationController.h"

extern void ConfigureStorage(const std::string& storageRootPath, const std::string& logRootPath);

namespace
{
QString MobileStorageRoot()
{
#ifdef ANDROID_DEVICE
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        const QJniObject filesDir = context.callObjectMethod(
            "getFilesDir",
            "()Ljava/io/File;"
        );
        if (filesDir.isValid()) {
            const QJniObject absolutePath = filesDir.callObjectMethod(
                "getAbsolutePath",
                "()Ljava/lang/String;"
            );
            const QString path = absolutePath.toString();
            if (!path.isEmpty()) {
                return path;
            }
        }
    }
#endif

    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString MobileLogRoot()
{
#ifdef ANDROID_DEVICE
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        const QJniObject externalFilesDir = context.callObjectMethod(
            "getExternalFilesDir",
            "(Ljava/lang/String;)Ljava/io/File;",
            static_cast<jstring>(nullptr)
        );
        if (externalFilesDir.isValid()) {
            const QJniObject absolutePath = externalFilesDir.callObjectMethod(
                "getAbsolutePath",
                "()Ljava/lang/String;"
            );
            const QString path = absolutePath.toString();
            if (!path.isEmpty()) {
                return path;
            }
        }
    }
#endif

    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
}

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
    qInstallMessageHandler(LibreConnectLogHandler);
    Debug::Log("main: start");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("LibreConnect");
    app.setApplicationName("LibreConnectMobile");
    app.setApplicationVersion(QStringLiteral(LIBRECONNECT_APP_VERSION));

    const QString storagePath = MobileStorageRoot();
    const QString logPath = MobileLogRoot();
    Debug::Log("main: calling ConfigureStorage");
    ConfigureStorage(storagePath.toStdString(), logPath.toStdString());

    Debug::Log("main: initializing theme and registering types");
    MobileThemeController themeController;


    qmlRegisterType<MobileConnectionController>(
        "LibreConnect.mobile", 1, 0, "MobileConnectionController"
    );

    qmlRegisterType<AndroidAdvertiser>(
        "LibreConnect.mobile", 1, 0, "AndroidAdvertiser"
    );

    qmlRegisterType<MobileNotificationSyncController>(
        "LibreConnect.mobile", 1, 0, "MobileNotificationSyncController"
    );

    qmlRegisterType<MobileClipboardSyncController>(
        "LibreConnect.mobile", 1, 0, "MobileClipboardSyncController"
    );

    qmlRegisterType<MobileRemoteInputController>(
        "LibreConnect.mobile", 1, 0, "MobileRemoteInputController"
    );

    qmlRegisterType<MobileMediaNotificationController>(
        "LibreConnect.mobile", 1, 0, "MobileMediaNotificationController"
    );

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &themeController);
    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    Debug::Log("main: calling app.exec()");
    return app.exec();
}
