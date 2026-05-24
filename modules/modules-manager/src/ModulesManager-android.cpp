#ifdef ANDROID_DEVICE

#include <ModulesManager.h>
#include <AndroidContextProvider.h>

namespace {
constexpr const char* MAIN_SERVICE_CLASS = "com.LibreConnect.mobile.MainService";
constexpr const char* ACTION_START_BACKEND = "com.LibreConnect.mobile.action.START_BACKEND";
constexpr const char* ACTION_STOP_BACKEND = "com.LibreConnect.mobile.action.STOP_BACKEND";
}

void ModulesManager::StartMainService() {
    SetMainServiceBackendEnabled(true);
}

void ModulesManager::SetMainServiceBackendEnabled(const bool enabled) {
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return;
    }

    const QJniObject intent(
        "android/content/Intent",
        "()V"
    );

    if (!intent.isValid()) {
        return;
    }

    const QJniObject packageName = context.callObjectMethod(
        "getPackageName",
        "()Ljava/lang/String;"
    );

    if (!packageName.isValid()) {
        return;
    }

    intent.callObjectMethod(
        "setClassName",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        packageName.object<jstring>(),
        QJniObject::fromString(MAIN_SERVICE_CLASS).object<jstring>()
    );

    intent.callObjectMethod(
        "setAction",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        QJniObject::fromString(enabled ? ACTION_START_BACKEND : ACTION_STOP_BACKEND).object<jstring>()
    );

    const jint sdkInt = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
    if (enabled && sdkInt >= 26) {
        context.callObjectMethod(
            "startForegroundService",
            "(Landroid/content/Intent;)Landroid/content/ComponentName;",
            intent.object<jobject>()
        );
        return;
    }

    context.callObjectMethod(
        "startService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
        intent.object<jobject>()
    );
}

#endif
