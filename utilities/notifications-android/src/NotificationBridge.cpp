#include <QCoreApplication>
#include <QString>

#include <NotificationBridge.h>
#include <DebugLog.h>

void NotificationBridge::PostNotification(const NotificationData& notificationData) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject bridge = QJniObject(
        "com/LibreConnect/mobile/NotificationBridge",
        "(Landroid/content/Context;)V",
        context.object()
    );

    if (!bridge.isValid()) {
        Debug::LogError("Failed to find Kotlin Bridge class!");
        return;
    }

    const QJniObject jKey = QJniObject::fromString(notificationData.key.data());
    const QJniObject jTitle = QJniObject::fromString(notificationData.title.data());
    const QJniObject jContent = QJniObject::fromString(notificationData.content.data());

    const QJniEnvironment env;
    jbyteArray jSmallIcon = nullptr;
    if (!notificationData.smallIcon.empty()) {
        jSmallIcon = env->NewByteArray(notificationData.smallIcon.size());
        env->SetByteArrayRegion(jSmallIcon, 0, notificationData.smallIcon.size(), reinterpret_cast<const jbyte*>(notificationData.smallIcon.data()));
    }

    jbyteArray jLargeIcon = nullptr;
    if (!notificationData.largeIcon.empty()) {
        jLargeIcon = env->NewByteArray(notificationData.largeIcon.size());
        env->SetByteArrayRegion(jLargeIcon, 0, notificationData.largeIcon.size(), reinterpret_cast<const jbyte*>(notificationData.largeIcon.data()));
    }

    bridge.callMethod<void>(
        "postNotification",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J[B[B)V",
        jKey.object<jstring>(),
        jTitle.object<jstring>(),
        jContent.object<jstring>(),
        static_cast<jlong>(notificationData.timestamp),
        jSmallIcon,
        jLargeIcon
    );

    if (jSmallIcon) env->DeleteLocalRef(jSmallIcon);
    if (jLargeIcon) env->DeleteLocalRef(jLargeIcon);
}

void NotificationBridge::RemoveNotification(const std::string& key) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject bridge = QJniObject(
        "com/LibreConnect/mobile/NotificationBridge",
        "(Landroid/content/Context;)V",
        context.object()
    );

    const QJniObject jKey = QJniObject::fromString(key.data());
    bridge.callMethod<void>("removeNotification", "(Ljava/lang/String;)V", jKey.object<jstring>());
}