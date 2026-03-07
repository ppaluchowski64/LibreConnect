#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <NotificationData.h>
#include "Backend.h"

#include <NotificationBridge.h>

extern std::vector<NotificationData> g_notificationDatas;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    qmlRegisterType<Backend>("LibreConnect.mobile", 1, 0, "Backend");

    NotificationBridge::InitializePermissions();
    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    return app.exec();
}