#ifdef ANDROID_DEVICE

#include <PermissionManager.h>
#include <QtCore/qcoreapplication_platform.h>
#include <QtCore/private/qandroidextras_p.h>
#include <QtGui/QGuiApplication>
#include <atomic>
#include <chrono>

#include "DebugLog.h"

namespace {
using namespace std::chrono_literals;

constexpr std::chrono::milliseconds kPermissionFlowRetryDelay = 50ms;
constexpr std::chrono::seconds kPermissionRequestTimeout = 120s;

std::atomic<bool> g_permissionFlowInProgress{false};

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

    QtAndroidPrivate::startActivity(intent, 0);
    return true;
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

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
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
    const QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;"
    );
    if (!activity.isValid()) {
        return false;
    }

    return activity.callMethod<jboolean>(
        "shouldShowRequestPermissionRationale",
        "(Ljava/lang/String;)Z",
        QJniObject::fromString(permission).object<jstring>()
    );
}
#endif
}


asio::awaitable<bool> PermissionManager::RequestDisablingBatteryOptimizations() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (QNativeInterface::QAndroidApplication::sdkVersion() < 23) {
        co_return true;
    }

    if (IsIgnoringBatteryOptimizations()) {
        co_return true;
    }

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

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
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

    co_await WaitForReturnToApp();
    co_return IsIgnoringBatteryOptimizations();
}

asio::awaitable<bool> PermissionManager::RequestNotificationAccessPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (IsNotificationListenerEnabled()) {
        co_return true;
    }

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

    co_await WaitForReturnToApp();
    co_return IsNotificationListenerEnabled();
}

asio::awaitable<bool> PermissionManager::RequestNotificationEmitPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (QNativeInterface::QAndroidApplication::sdkVersion() < 33) {
        co_return true;
    }

    co_return co_await RequestPermission(QString("android.permission.POST_NOTIFICATIONS"));
}

asio::awaitable<bool> PermissionManager::RequestCameraAccessPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();
    co_return co_await RequestPermission(QString("android.permission.CAMERA"));
}

asio::awaitable<bool> PermissionManager::RequestFileAccessPermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
        co_return true;
    }

    co_return co_await RequestPermission(QString("android.permission.WRITE_EXTERNAL_STORAGE"));
}

asio::awaitable<bool> PermissionManager::RequestManagingExternalStoragePermission() {
    auto permissionFlowLock = co_await AcquirePermissionFlowLock();

    if (QNativeInterface::QAndroidApplication::sdkVersion() < 30) {
        co_return co_await RequestPermission(QString("android.permission.WRITE_EXTERNAL_STORAGE"));
    }

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        Debug::LogWarning("Failed to obtain Android context while requesting managing external storage.");
    }

    const bool isExternalStorageManager = QJniObject::callStaticMethod<jboolean>(
        "android/os/Environment", "isExternalStorageManager");

    if (isExternalStorageManager) co_return true;

    const QJniObject action = QJniObject::fromString("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
    const QJniObject packageName = QJniObject::fromString("package:" + context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString());
    const QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", packageName.object());

    const QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V", action.object(), uri.object());
    if (!StartSettingsActivity(intent)) {
        Debug::LogWarning("Failed to start manage external storage settings activity.");
        co_return false;
    }

    co_await WaitForReturnToApp();
    co_return QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager");
}

asio::awaitable<void> PermissionManager::WaitForReturnToApp() {
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer phaseTimer(executor);
    bool appWentBackground = (qApp->applicationState() != Qt::ApplicationActive);

    // Some settings flows may not background the app (overlay/popup). If no
    // transition happens shortly, continue instead of waiting indefinitely.
    phaseTimer.expires_after(std::chrono::milliseconds(1500));
    QMetaObject::Connection connection = QObject::connect(
        qApp, &QGuiApplication::applicationStateChanged,
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

    if (!appWentBackground || qApp->applicationState() == Qt::ApplicationActive) {
        co_return;
    }

    phaseTimer.expires_after(std::chrono::seconds(120));
    connection = QObject::connect(
        qApp, &QGuiApplication::applicationStateChanged,
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
    const auto status = QtAndroidPrivate::checkPermission(permission).result();

    if (status == QtAndroidPrivate::PermissionResult::Authorized) {
        co_return true;
    }

    const QFuture<QtAndroidPrivate::PermissionResult> future = QtAndroidPrivate::requestPermission(permission);
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer pollTimer(executor);
    const std::chrono::time_point<std::chrono::steady_clock> timeoutAt = std::chrono::steady_clock::now() + kPermissionRequestTimeout;

    while (!future.isFinished()) {
        if (std::chrono::steady_clock::now() >= timeoutAt) {
            Debug::LogWarning("Timed out waiting for permission result: {}", permission.toStdString());
            co_return false;
        }

        pollTimer.expires_after(kPermissionFlowRetryDelay);
        asio::error_code ec;
        co_await pollTimer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    }

    const QtAndroidPrivate::PermissionResult result = future.result();
    if (result == QtAndroidPrivate::PermissionResult::Authorized) {
        co_return true;
    }

    if (ShouldShowPermissionRationale(permission)) {
        co_return false;
    }

    if (!OpenAppPermissionSettings()) {
        co_return false;
    }

    co_await WaitForReturnToApp();
    const auto finalStatus = QtAndroidPrivate::checkPermission(permission).result();
    co_return finalStatus == QtAndroidPrivate::PermissionResult::Authorized;
}

bool PermissionManager::IsNotificationAccessPermissionGranted() {
    return IsNotificationListenerEnabled();
}

bool PermissionManager::IsNotificationEmitPermissionGranted() {
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 33) {
        return true;
    }

    return QtAndroidPrivate::checkPermission(QString("android.permission.POST_NOTIFICATIONS")).result()
        == QtAndroidPrivate::PermissionResult::Authorized;
}

bool PermissionManager::IsCameraAccessPermissionGranted() {
    return QtAndroidPrivate::checkPermission(QString("android.permission.CAMERA")).result()
        == QtAndroidPrivate::PermissionResult::Authorized;
}

bool PermissionManager::IsFileAccessPermissionGranted() {
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
        return true;
    }

    return QtAndroidPrivate::checkPermission(QString("android.permission.WRITE_EXTERNAL_STORAGE")).result()
        == QtAndroidPrivate::PermissionResult::Authorized;
}

bool PermissionManager::IsManagingExternalStoragePermissionGranted() {
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 30) {
        return IsFileAccessPermissionGranted();
    }

    return QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager");
}

bool PermissionManager::IsBatteryOptimizationIgnored() {
    return IsIgnoringBatteryOptimizations();
}

bool PermissionManager::IsNotificationListenerEnabled() {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return false;

    const QString packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
    const QString className = packageName + ".NotificationListener";

    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 27) {
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
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 23) {
        return true;
    }

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
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
