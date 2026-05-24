#pragma once

#include <QJsonObject>
#include <QString>

#ifdef ANDROID_DEVICE
namespace BackendBridge
{
    QJsonObject ReadStateSnapshot();

    void SendAction(const char* action);
    void SendAction(const char* action, const char* extraKey, bool value);
    void SendAction(const char* action, const char* extraKey, int value);
    void SendAction(const char* action, const char* extraKey, double value);
    void SendAction(const char* action, const char* extraKey, const QString& value);

    void SendAction(const char* action,
                    const char* key1, int val1,
                    const char* key2, const QString& val2,
                    const char* key3, int val3);

    constexpr auto kActionToggleClipboardSync     = "com.LibreConnect.mobile.action.TOGGLE_CLIPBOARD_SYNC";
    constexpr auto kActionSyncClipboard           = "com.LibreConnect.mobile.action.SYNC_CLIPBOARD";
    constexpr auto kActionToggleNotificationSync   = "com.LibreConnect.mobile.action.TOGGLE_NOTIFICATION_SYNC";
    constexpr auto kActionSendMediaSignal          = "com.LibreConnect.mobile.action.SEND_MEDIA_SIGNAL";
    constexpr auto kActionSendKeyInput             = "com.LibreConnect.mobile.action.SEND_KEY_INPUT";
    constexpr auto kActionMediaSeek                = "com.LibreConnect.mobile.action.MEDIA_SEEK";
    constexpr auto kActionMediaSetVolume           = "com.LibreConnect.mobile.action.MEDIA_SET_VOLUME";
    constexpr auto kActionRefreshDownloadPath       = "com.LibreConnect.mobile.action.REFRESH_DOWNLOAD_PATH";
    constexpr auto kActionSetDownloadPath           = "com.LibreConnect.mobile.action.SET_DOWNLOAD_PATH";
    constexpr auto kActionSendLocalClipboard       = "com.LibreConnect.mobile.action.SEND_LOCAL_CLIPBOARD";
    constexpr auto kActionDisconnect               = "com.LibreConnect.mobile.action.DISCONNECT";
    constexpr auto kActionSetMirroringEnabled       = "com.LibreConnect.mobile.action.SET_MIRRORING_ENABLED";

    constexpr auto kExtraEnabled   = "com.LibreConnect.mobile.EXTRA_ENABLED";
    constexpr auto kExtraSignal    = "com.LibreConnect.mobile.EXTRA_SIGNAL";
    constexpr auto kExtraKey       = "com.LibreConnect.mobile.EXTRA_KEY";
    constexpr auto kExtraText      = "com.LibreConnect.mobile.EXTRA_TEXT";
    constexpr auto kExtraClipboardText = "com.LibreConnect.mobile.EXTRA_CLIPBOARD_TEXT";
    constexpr auto kExtraModifiers = "com.LibreConnect.mobile.EXTRA_MODIFIERS";
    constexpr auto kExtraPosition  = "com.LibreConnect.mobile.EXTRA_POSITION";
    constexpr auto kExtraVolume    = "com.LibreConnect.mobile.EXTRA_VOLUME";
    constexpr auto kExtraPath      = "com.LibreConnect.mobile.EXTRA_PATH";
}
#endif
