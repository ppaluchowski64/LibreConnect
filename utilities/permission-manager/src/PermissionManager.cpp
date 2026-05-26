#ifdef ANDROID_DEVICE

#include <PermissionManager.h>
#include <QtCore/qcoreapplication_platform.h>
#include <QtGui/QGuiApplication>
#include <atomic>
#include <chrono>

#include "DebugLog.h"
#include <AndroidContextProvider.h>

namespace {
using namespace std::chrono_literals;

constexpr std::chrono::milliseconds kPermissionFlowRetryDelay = 50ms;
constexpr std::chrono::seconds kPermissionRequestTimeout = 120s;
constexpr jint kAndroidPermissionGranted = 0;

std::atomic<bool> g_permissionFlowInProgress{false};

QJniObject GetAndroidContext()
{
    return AndroidContextProvider::GetAndroidContext();
}

class PermissionFlowLock final {
public:
    explicit PermissionFlowLock(const bool locked = false) noexcept : m_locked(locked) {}
    PermissionFlowLock(PermissionFlowLock&& other) noexcept : m_locked(other.m_locked) {
        other.m_locked = false;
    }
    PermissionFlowLock& operator=(PermissionFlowLock&& other) noexcept {
        if (this != &other) {
            Release();
            m_locked = other.m_locked;
            other.m_locked = false;
        }
        return *this;
    }

    PermissionFlowLock(const PermissionFlowLock&) = delete;
    PermissionFlowLock& operator=(const PermissionFlowLock&) = delete;

    ~PermissionFlowLock() {
        Release();
    }

private:
    void Release() noexcept {
        if (m_locked) {
            g_permissionFlowInProgress.store(false, std::memory_order_release);
            m_locked = false;
        }
    }

    bool m_locked{false};
};

asio::awaitable<PermissionFlowLock> AcquirePermissionFlowLock() {
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer waitTimer(executor);

    while (true) {
        bool expected = false;
        if (g_permissionFlowInProgress.compare_exchange_weak(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
            co_return PermissionFlowLock(true);
        }

        waitTimer.expires_after(kPermissionFlowRetryDelay);
        asio::error_code ec;
        co_await waitTimer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    }
}

#ifdef ANDROID_DEVICE
bool StartSettingsActivity(const QJniObject& intent) {
    if (!intent.isValid()) {
        return false;
    }

    const QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;"
    );

    if (activity.isValid()) {
        activity.callMethod<void>(
            "startActivity",
            "(Landroid/content/Intent;)V",
            intent.object<jobject>()
        );
        return true;
    }

    const jint newTaskFlag = QJniObject::getStaticField<jint>(
        "android/content/Intent",
        "FLAG_ACTIVITY_NEW_TASK"
    );

    intent.callMethod<jobject>(
        "addFlags",
        "(I)Landroid/content/Intent;",
        newTaskFlag
    );

    const QJniObject context = GetAndroidContext();
    if (!context.isValid()) {
        return false;
    }

    context.callMethod<void>(
        "startActivity",
        "(Landroid/content/Intent;)V",
        intent.object<jobject>()
    );
    return !QJniEnvironment().checkAndClearExceptions();
}

bool OpenAppPermissionSettings() {
    const QJniObject action = QJniObject::getStaticObjectField(
        "android/provider/Settings",
        "ACTION_APPLICATION_DETAILS_SETTINGS",
        "Ljava/lang/String;"
    );
    if (!action.isValid()) {
        return false;
    }

    const QJniObject context = GetAndroidContext();
    if (!context.isValid()) {
        return false;
    }

    const QString packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
    if (packageName.isEmpty()) {
        return false;
    }

    const QJniObject packageUri = QJniObject::fromString("package:" + packageName);
    const QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri",
        "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        packageUri.object<jstring>()
    );

    const QJniObject intent(
        "android/content/Intent",
        "(Ljava/lang/String;Landroid/net/Uri;)V",
        action.object<jstring>(),
        uri.object<jobject>()
    );

    return StartSettingsActivity(intent);
}

bool ShouldShowPermissionRationale(const QString& permission) {
    bool result = false;
    AndroidContextProvider::WithJniEnv([&](JNIEnv* env) {
        jclass bridgeClass = AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/AndroidPermissionBridge");
        if (!bridgeClass) {
            return;
        }

        jmethodID method = env->GetStaticMethodID(
            bridgeClass,
            "shouldShowRequestPermissionRationale",
            "(Ljava/lang/String;)Z"
        );
        if (!method) {
            env->ExceptionClear();
            env->DeleteLocalRef(bridgeClass);
            return;
        }

        const QJniObject jPermission = QJniObject::fromString(permission);
        result = env->CallStaticBooleanMethod(bridgeClass, method, jPermission.object<jstring>());
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            result = false;
        }
        env->DeleteLocalRef(bridgeClass);
    });
    return result;
}

jint CheckAndroidPermission(const QString& permission) {
    jint result = -1;
    AndroidContextProvider::WithJniEnv([&](JNIEnv* env) {
        jclass bridgeClass = AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/AndroidPermissionBridge");
        if (!bridgeClass) {
            return;
        }

        jmethodID method = env->GetStaticMethodID(
            bridgeClass,
            "checkPermission",
            "(Ljava/lang/String;)I"
        );
        if (!method) {
            env->ExceptionClear();
            env->DeleteLocalRef(bridgeClass);
            return;
        }

        const QJniObject jPermission = QJniObject::fromString(permission);
        result = env->CallStaticIntMethod(bridgeClass, method, jPermission.object<jstring>());
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            result = -1;
        }
        env->DeleteLocalRef(bridgeClass);
    });

    Debug::Log("CheckingAndroidPermissions: permission {}, state {}", permission.toStdString(), result == kAndroidPermissionGranted);

    return result;
}

bool RequestAndroidPermissionBlocking(const QString& permission, const int timeoutMs) {
    bool result = false;
    AndroidContextProvider::WithJniEnv([&](JNIEnv* env) {
        jclass bridgeClass = AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/AndroidPermissionBridge");
        if (!bridgeClass) {
            return;
        }

        jmethodID method = env->GetStaticMethodID(
            bridgeClass,
            "requestPermissionBlocking",
            "(Ljava/lang/String;I)Z"
        );
        if (!method) {
            env->ExceptionClear();
            env->DeleteLocalRef(bridgeClass);
            return;
        }

        const QJniObject jPermission = QJniObject::fromString(permission);
        result = env->CallStaticBooleanMethod(
            bridgeClass,
            method,
            jPermission.object<jstring>(),
            static_cast<jint>(timeoutMs)
        );
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            result = false;
        }
        env->DeleteLocalRef(bridgeClass);
    });
    return result;
}
#endif
}


asio::awaitable<bool> PermissionManager::RequestDisablingBatteryOptimizations() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (AndroidContextProvider::GetSdkVersion() < 23) {
        co_return true;
    }

    if (IsIgnoringBatteryOptimizations()) {
        co_return true;
    }

    {
        const QJniObject action = QJniObject::getStaticObjectField(
            "android/provider/Settings",
            "ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS",
            "Ljava/lang/String;"
        );

        if (!action.isValid()) {
            Debug::LogWarning("Failed to resolve ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS.");
            co_return false;
        }

        const QJniObject intent(
            "android/content/Intent",
            "(Ljava/lang/String;)V",
            action.object<jstring>()
        );

        if (!intent.isValid()) {
            Debug::LogWarning("Failed to create Intent for battery optimization settings.");
            co_return false;
        }

        const QJniObject context = GetAndroidContext();
        if (!context.isValid()) {
            Debug::LogWarning("Failed to obtain Android context while requesting battery optimization settings.");
            co_return false;
        }

        const QString packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();

        const QJniObject uriString = QJniObject::fromString("package:" + packageName);
        const QJniObject uri = QJniObject::callStaticObjectMethod(
            "android/net/Uri",
            "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;",
            uriString.object<jstring>()
        );

        intent.callObjectMethod(
            "setData",
            "(Landroid/net/Uri;)Landroid/content/Intent;",
            uri.object<jobject>()
        );

        if (!StartSettingsActivity(intent)) {
            Debug::LogWarning("Failed to start battery optimization settings activity.");
            co_return false;
        }
    }

    co_await WaitForReturnToApp();
    co_return IsIgnoringBatteryOptimizations();
}

asio::awaitable<bool> PermissionManager::RequestNotificationAccessPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (IsNotificationListenerEnabled()) {
        co_return true;
    }

    {
        const QJniObject action = QJniObject::getStaticObjectField(
            "android/provider/Settings",
            "ACTION_NOTIFICATION_LISTENER_SETTINGS",
            "Ljava/lang/String;"
        );

        if (!action.isValid()) {
            Debug::LogWarning("Failed to resolve ACTION_NOTIFICATION_LISTENER_SETTINGS.");
            co_return false;
        }

        const QJniObject intent(
            "android/content/Intent",
            "(Ljava/lang/String;)V",
            action.object<jstring>()
        );

        if (!intent.isValid()) {
            Debug::LogWarning("Failed to create Intent for notification listener settings.");
            co_return false;
        }

        if (!StartSettingsActivity(intent)) {
            Debug::LogWarning("Failed to start notification listener settings activity.");
            co_return false;
        }
    }

    co_await WaitForReturnToApp();
    co_return IsNotificationListenerEnabled();
}

asio::awaitable<bool> PermissionManager::RequestNotificationEmitPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (AndroidContextProvider::GetSdkVersion() < 33) {
        co_return true;
    }

    co_return co_await RequestPermission(QString("android.permission.POST_NOTIFICATIONS"));
}

asio::awaitable<bool> PermissionManager::RequestCameraAccessPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();
    co_return co_await RequestPermission(QString("android.permission.CAMERA"));
}

asio::awaitable<bool> PermissionManager::RequestMicrophoneAccessPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();
    co_return co_await RequestPermission(QString("android.permission.RECORD_AUDIO"));
}

asio::awaitable<bool> PermissionManager::RequestReceiveSmsPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();
    co_return co_await RequestPermission(QString("android.permission.RECEIVE_SMS"));
}

asio::awaitable<bool> PermissionManager::RequestReadContactsPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();
    co_return co_await RequestPermission(QString("android.permission.READ_CONTACTS"));
}

asio::awaitable<bool> PermissionManager::RequestReadSmsPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();
    co_return co_await RequestPermission(QString("android.permission.READ_SMS"));
}

asio::awaitable<bool> PermissionManager::RequestSendSmsPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();
    co_return co_await RequestPermission(QString("android.permission.SEND_SMS"));
}

asio::awaitable<bool> PermissionManager::RequestFileAccessPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (AndroidContextProvider::GetSdkVersion() >= 30) {
        co_return true;
    }

    co_return co_await RequestPermission(QString("android.permission.WRITE_EXTERNAL_STORAGE"));
}

asio::awaitable<bool> PermissionManager::RequestManagingExternalStoragePermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (AndroidContextProvider::GetSdkVersion() < 30) {
        co_return co_await RequestPermission(QString("android.permission.WRITE_EXTERNAL_STORAGE"));
    }

    const bool isExternalStorageManager = QJniObject::callStaticMethod<jboolean>(
        "android/os/Environment", "isExternalStorageManager");

    if (isExternalStorageManager) co_return true;

    {
        const QJniObject context = GetAndroidContext();
        if (!context.isValid()) {
            Debug::LogWarning("Failed to obtain Android context while requesting managing external storage.");
            co_return false;
        }

        const QJniObject action = QJniObject::fromString("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
        const QJniObject packageName = QJniObject::fromString("package:" + context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString());
        const QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", packageName.object());

        const QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V", action.object(), uri.object());
        if (!StartSettingsActivity(intent)) {
            Debug::LogWarning("Failed to start manage external storage settings activity.");
            co_return false;
        }
    }

    co_await WaitForReturnToApp();
    co_return QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager");
}

asio::awaitable<void> PermissionManager::WaitForReturnToApp() {
    // In the headless backend process, qApp is a QCoreApplication (not QGuiApplication),
    // so applicationStateChanged signal is unavailable. Skip the wait in that case.
    auto* guiApp = qobject_cast<QGuiApplication*>(qApp);
    if (!guiApp) {
        co_return;
    }

    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer phaseTimer(executor);
    bool appWentBackground = (guiApp->applicationState() != Qt::ApplicationActive);

    // Some settings flows may not background the app (overlay/popup). If no
    // transition happens shortly, continue instead of waiting indefinitely.
    phaseTimer.expires_after(std::chrono::milliseconds(1500));
    QMetaObject::Connection connection = QObject::connect(
        guiApp, &QGuiApplication::applicationStateChanged,
        [&phaseTimer, &appWentBackground](const Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) {
                appWentBackground = true;
                phaseTimer.cancel();
            }
        }
    );

    asio::error_code ec;
    co_await phaseTimer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    QObject::disconnect(connection);

    if (!appWentBackground || guiApp->applicationState() == Qt::ApplicationActive) {
        co_return;
    }

    phaseTimer.expires_after(std::chrono::seconds(120));
    connection = QObject::connect(
        guiApp, &QGuiApplication::applicationStateChanged,
        [&phaseTimer](const Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                phaseTimer.cancel();
            }
        }
    );

    ec.clear();
    co_await phaseTimer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    QObject::disconnect(connection);
}

asio::awaitable<bool> PermissionManager::RequestPermission(QString&& permission) {
    const jint status = CheckAndroidPermission(permission);

    if (status == kAndroidPermissionGranted) {
        co_return true;
    }

    const bool granted = RequestAndroidPermissionBlocking(permission, static_cast<int>(kPermissionRequestTimeout.count() * 1000));
    if (granted) {
        co_return true;
    }

    if (ShouldShowPermissionRationale(permission)) {
        co_return false;
    }

    if (!OpenAppPermissionSettings()) {
        co_return false;
    }

    co_await WaitForReturnToApp();

    const bool result = CheckAndroidPermission(permission) == kAndroidPermissionGranted;
    Debug::Log("RequestPermission: permission {}, result {}", permission.toStdString(), result);

    co_return result;
}

bool PermissionManager::IsNotificationAccessPermissionGranted() {
    return IsNotificationListenerEnabled();
}

bool PermissionManager::IsNotificationEmitPermissionGranted() {
    if (AndroidContextProvider::GetSdkVersion() < 33) {
        return true;
    }

    return CheckAndroidPermission(QString("android.permission.POST_NOTIFICATIONS")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsCameraAccessPermissionGranted() {
    return CheckAndroidPermission(QString("android.permission.CAMERA")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsMicrophoneAccessPermissionGranted() {
    return CheckAndroidPermission(QString("android.permission.RECORD_AUDIO")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsFileAccessPermissionGranted() {
    if (AndroidContextProvider::GetSdkVersion() >= 30) {
        return true;
    }

    return CheckAndroidPermission(QString("android.permission.WRITE_EXTERNAL_STORAGE")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsManagingExternalStoragePermissionGranted() {
    if (AndroidContextProvider::GetSdkVersion() < 30) {
        return IsFileAccessPermissionGranted();
    }

    return QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager");
}

bool PermissionManager::IsBatteryOptimizationIgnored() {
    return IsIgnoringBatteryOptimizations();
}

bool PermissionManager::IsReceiveSmsPermissionGranted() {
    return CheckAndroidPermission(QString("android.permission.RECEIVE_SMS")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsReadContactsPermissionGranted() {
    return CheckAndroidPermission(QString("android.permission.READ_CONTACTS")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsReadSmsPermissionGranted() {
    return CheckAndroidPermission(QString("android.permission.READ_SMS")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsSendSmsPermissionGranted() {
    return CheckAndroidPermission(QString("android.permission.SEND_SMS")) == kAndroidPermissionGranted;
}

bool PermissionManager::IsNotificationListenerEnabled() {
    const QJniObject context = GetAndroidContext();
    if (!context.isValid()) return false;

    const QString packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
    const QString className = packageName + ".NotificationListener";

    if (AndroidContextProvider::GetSdkVersion() >= 27) {
        const QJniObject notificationService = QJniObject::getStaticObjectField(
            "android/content/Context", "NOTIFICATION_SERVICE", "Ljava/lang/String;");

        const QJniObject notificationManager = context.callObjectMethod(
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", notificationService.object());

        if (notificationManager.isValid()) {
            const QJniObject componentName("android/content/ComponentName",
                                           "(Ljava/lang/String;Ljava/lang/String;)V",
                                           QJniObject::fromString(packageName).object(),
                                           QJniObject::fromString(className).object());

            return notificationManager.callMethod<jboolean>(
                "isNotificationListenerAccessGranted",
                "(Landroid/content/ComponentName;)Z",
                componentName.object());
        }
    }

    const QJniObject contentResolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    const QJniObject enabledListeners = QJniObject::callStaticObjectMethod(
        "android/provider/Settings$Secure",
        "getString",
        "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;",
        contentResolver.object(),
        QJniObject::fromString("enabled_notification_listeners").object());

    if (!enabledListeners.isValid()) return false;

    const QString target = packageName + "/" + className;
    return enabledListeners.toString().contains(target);
}

bool PermissionManager::IsIgnoringBatteryOptimizations() {
    if (AndroidContextProvider::GetSdkVersion() < 23) {
        return true;
    }

    const QJniObject context = GetAndroidContext();
    if (!context.isValid()) {
        Debug::LogWarning("Failed to obtain Android context while checking battery optimization status.");
        return false;
    }

    const QJniObject powerService = QJniObject::getStaticObjectField(
        "android/content/Context",
        "POWER_SERVICE",
        "Ljava/lang/String;"
    );

    if (!powerService.isValid()) {
        Debug::LogWarning("Failed to resolve POWER_SERVICE while checking battery optimization status.");
        return false;
    }

    const QJniObject powerManager = context.callObjectMethod(
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        powerService.object<jstring>()
    );

    if (!powerManager.isValid()) {
        Debug::LogWarning("Failed to obtain PowerManager while checking battery optimization status.");
        return false;
    }

    const QString packageName = context.callObjectMethod(
        "getPackageName",
        "()Ljava/lang/String;"
    ).toString();

    if (packageName.isEmpty()) {
        return false;
    }

    return powerManager.callMethod<jboolean>(
        "isIgnoringBatteryOptimizations",
        "(Ljava/lang/String;)Z",
        QJniObject::fromString(packageName).object<jstring>()
    );
}

#endif
