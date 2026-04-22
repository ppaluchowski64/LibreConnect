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
#include "MobileRemoteInputController.h"

int main(int argc, char *argv[])
{
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

    qmlRegisterType<MobileRemoteInputController>(
        "LibreConnect.mobile", 1, 0, "MobileRemoteInputController"
    );

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &themeController);
    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    return app.exec();
}
