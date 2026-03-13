#include <ModulesManager.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

ModulesManager::ModulesManager() {
    m_fileShareModule = std::make_shared<FileShareModule>();
    m_fileShareModule->Initialize();

    m_networkCameraModule = std::make_shared<NetworkCameraModule>();
    m_networkCameraModule->Initialize();

#ifndef IOS_DEVICE
    m_notificationSyncModule = std::make_shared<NotificationSyncModule>();
    m_notificationSyncModule->Initialize();
#endif

#ifdef ANDROID_DEVICE
    StartMainService();
#endif
}

void ModulesManager::Initialize() {
    if (s_instance == nullptr) {
        s_instance = new ModulesManager();
    }
}

#ifdef ANDROID_DEVICE
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
