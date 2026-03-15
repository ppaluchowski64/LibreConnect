#ifndef PERMISSIONMANAGER_H
#define PERMISSIONMANAGER_H

#include <asio.hpp>
#include <QtCore>

class PermissionManager {
public:
#ifdef ANDROID_DEVICE
    static asio::awaitable<bool> RequestDisablingBatteryOptimizations();
    static asio::awaitable<bool> RequestNotificationAccessPermission();
    static asio::awaitable<bool> RequestNotificationEmitPermission();
    static asio::awaitable<bool> RequestCameraAccessPermission();
    static asio::awaitable<bool> RequestManagingExternalStoragePermission();
    static asio::awaitable<void> WaitForReturnToApp();
#endif

private:
#ifdef ANDROID_DEVICE
    static bool IsNotificationListenerEnabled();
    static bool IsIgnoringBatteryOptimizations();
    static asio::awaitable<bool> RequestPermission(QString&& permission);
#endif
};

#endif // PERMISSIONMANAGER_H