#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "Backend.h"

#include <ModulesManager.h>

asio::awaitable<void> RequestPermissions() {
    while (!co_await ModulesManager::RequestNotificationEmitPermission()) {}
    while (!co_await ModulesManager::RequestNotificationAccessPermission()) {}
    while (!co_await ModulesManager::RequestDisablingBatteryOptimizations()) {}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    qmlRegisterType<Backend>("LibreConnect.mobile", 1, 0, "Backend");

    ModulesManager::Initialize();
    asio::co_spawn(ThreadPool::GetContext(), RequestPermissions(), asio::detached);

    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    return app.exec();
}
