#ifndef PERMISSIONMANAGER_H
#define PERMISSIONMANAGER_H

#include <asio.hpp>
#include <QtCore>

class PermissionManager {
public:
    static asio::awaitable<bool> RequestLocalNetworkAccessPermission();
    static bool IsLocalNetworkAccessPermissionGranted();

#ifdef ANDROID_DEVICE
    static asio::awaitable<bool> RequestDisablingBatteryOptimizations();
    static asio::awaitable<bool> RequestNotificationAccessPermission();
    static asio::awaitable<bool> RequestNotificationEmitPermission();
    static asio::awaitable<bool> RequestCameraAccessPermission();
    static asio::awaitable<bool> RequestMicrophoneAccessPermission();
    static asio::awaitable<bool> RequestFileAccessPermission();
    static asio::awaitable<bool> RequestManagingExternalStoragePermission();
    static asio::awaitable<bool> RequestReceiveSmsPermission();
    static asio::awaitable<bool> RequestReadContactsPermission();
    static asio::awaitable<bool> RequestReadSmsPermission();
    static asio::awaitable<bool> RequestSendSmsPermission();
    static asio::awaitable<void> WaitForReturnToApp();

    static bool IsNotificationAccessPermissionGranted();
    static bool IsNotificationEmitPermissionGranted();
    static bool IsCameraAccessPermissionGranted();
    static bool IsMicrophoneAccessPermissionGranted();
    static bool IsFileAccessPermissionGranted();
    static bool IsManagingExternalStoragePermissionGranted();
    static bool IsBatteryOptimizationIgnored();
    static bool IsReceiveSmsPermissionGranted();
    static bool IsReadContactsPermissionGranted();
    static bool IsReadSmsPermissionGranted();
    static bool IsSendSmsPermissionGranted();
#endif

private:
#ifdef ANDROID_DEVICE
    static bool IsNotificationListenerEnabled();
    static bool IsIgnoringBatteryOptimizations();
    static asio::awaitable<bool> RequestPermission(QString&& permission);
#endif
};

#endif // PERMISSIONMANAGER_H
