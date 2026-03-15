#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "Backend.h"

#include <PermissionManager.h>
#include <ModulesManager.h>

asio::awaitable<void> RequestPermissions() {
    while (!co_await PermissionManager::RequestNotificationEmitPermission()) {}
    while (!co_await PermissionManager::RequestNotificationAccessPermission()) {}
    while (!co_await PermissionManager::RequestDisablingBatteryOptimizations()) {}
    while (!co_await PermissionManager::RequestManagingExternalStoragePermission()) {}
    while (!co_await PermissionManager::RequestCameraAccessPermission()) {}
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
