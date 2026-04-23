#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml>
#include <QQmlContext>
#include <ModulesManager.h>
#include "MobileConnectionController.h"
#include "AndroidAdvertiser.h"
#include "MobileThemeController.h"
#include "MobileNotificationSyncController.h"
#include "MobileClipboardSyncController.h"

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
    QGuiApplication app(argc, argv);
    app.setOrganizationName("LibreConnect");
    app.setApplicationName("LibreConnectMobile");

    ModulesManager::Initialize();
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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &themeController);
    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    return app.exec();
}
