#ifdef ANDROID_DEVICE

#include <ModulesManager.h>
#include <QtCore/private/qandroidextras_p.h>

void ModulesManager::StartMainService() {
    const QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;"
    );

    if (!activity.isValid())
        return;

    const QJniObject serviceClass = QJniObject::callStaticObjectMethod(
        "java/lang/Class",
        "forName",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        QJniObject::fromString("com.LibreConnect.mobile.MainService").object<jstring>()
    );

    if (!serviceClass.isValid())
        return;

    const QJniObject intent(
        "android/content/Intent",
        "(Landroid/content/Context;Ljava/lang/Class;)V",
        activity.object<jobject>(),
        serviceClass.object<jclass>()
    );

    if (!intent.isValid())
        return;

    const jint sdkInt = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
    if (sdkInt >= 26) {
        activity.callObjectMethod(
            "startForegroundService",
            "(Landroid/content/Intent;)Landroid/content/ComponentName;",
            intent.object<jobject>()
        );
    } else {
        activity.callObjectMethod(
            "startService",
            "(Landroid/content/Intent;)Landroid/content/ComponentName;",
            intent.object<jobject>()
        );
    }
}

#endif
