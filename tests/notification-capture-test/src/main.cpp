#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml>

#include <NotificationData.h>

extern std::vector<NotificationData> g_notificationDatas;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    return app.exec();
}