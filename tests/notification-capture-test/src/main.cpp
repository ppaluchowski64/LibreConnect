#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <NotificationData.h>
#include "Backend.h"

#include <NotificationBridge.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

extern std::vector<NotificationData> g_notificationDatas;

static void StartMainService()
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;"
    );

    if (!activity.isValid())
        return;

    QJniObject serviceClass = QJniObject::callStaticObjectMethod(
        "java/lang/Class",
        "forName",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        QJniObject::fromString("com.LibreConnect.mobile.MainService").object<jstring>()
    );

    if (!serviceClass.isValid())
        return;

    QJniObject intent(
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
#endif
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    qmlRegisterType<Backend>("LibreConnect.mobile", 1, 0, "Backend");

    NotificationBridge::InitializePermissions();
    StartMainService();
    engine.load(QUrl(QStringLiteral("qrc:/LibreConnect/mobile/Main.qml")));

    return app.exec();
}
