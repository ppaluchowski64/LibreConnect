#ifdef ANDROID_DEVICE

#include <ModulesManager.h>
#include <QtCore/qcoreapplication_platform.h>

namespace {
constexpr const char* MAIN_SERVICE_CLASS = "com.LibreConnect.mobile.MainService";
constexpr const char* ACTION_START_BACKEND = "com.LibreConnect.mobile.action.START_BACKEND";
constexpr const char* ACTION_STOP_BACKEND = "com.LibreConnect.mobile.action.STOP_BACKEND";
}

void ModulesManager::StartMainService() {
    SetMainServiceBackendEnabled(true);
}

void ModulesManager::SetMainServiceBackendEnabled(const bool enabled) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }

    const QJniObject serviceClass = QJniObject::callStaticObjectMethod(
        "java/lang/Class",
        "forName",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        QJniObject::fromString(MAIN_SERVICE_CLASS).object<jstring>()
    );

    if (!serviceClass.isValid()) {
        return;
    }

    const QJniObject intent(
        "android/content/Intent",
        "(Landroid/content/Context;Ljava/lang/Class;)V",
        context.object<jobject>(),
        serviceClass.object<jclass>()
    );

    if (!intent.isValid()) {
        return;
    }

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
