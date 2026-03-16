#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml>

#include <ModulesManager.h>

#include "MobileConnectionController.h"
#include "AndroidAdvertiser.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    ModulesManager::Initialize();

    qmlRegisterType<MobileConnectionController>(
        "LibreConnect.mobile", 1, 0, "MobileConnectionController"
    );

    qmlRegisterType<AndroidAdvertiser>(
        "LibreConnect.mobile", 1, 0, "AndroidAdvertiser"
    );

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    return app.exec();
}
