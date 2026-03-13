#include <QCoreApplication>
#include <QMetaObject>
#include <QString>
#include <QJniObject>
#include <QJniArray>
#include <exception>

#include <QtCore/qjniobject.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qcoreapplication_platform.h>
#include <QtCore/private/qandroidextras_p.h>

#include <NotificationBridge.h>
#include <DebugLog.h>
#include <ThreadPool.h>

ConcurrentUnorderedMap<std::string, std::function<void()>> NotificationBridge::m_notificationHandlers{};

static jobjectArray createStringArray(const QJniEnvironment& env, const std::vector<std::string>& vec)
{
    const jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) return nullptr;

    jobjectArray array = env->NewObjectArray(
        static_cast<jsize>(vec.size()),
        stringClass,
        nullptr
    );

    if (!array) return nullptr;

    for (jsize i = 0; i < vec.size(); ++i)
    {
        const jstring jStr = env->NewStringUTF(vec[i].c_str());
        if (!jStr) continue;

        env->SetObjectArrayElement(array, i, jStr);
        env->DeleteLocalRef(jStr);
    }

    return array;
}

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

    jarray jButtons = createStringArray(env, notificationData.buttons);

    bridge.callMethod<void>(
        "postNotification",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J[B[B[Ljava/lang/String;)V",
        jKey.object<jstring>(),
        jTitle.object<jstring>(),
        jContent.object<jstring>(),
        static_cast<jlong>(notificationData.timestamp),
        jSmallIcon,
        jLargeIcon,
        jButtons
    );

    if (jSmallIcon) env->DeleteLocalRef(jSmallIcon);
    if (jLargeIcon) env->DeleteLocalRef(jLargeIcon);
    if (jButtons) env->DeleteLocalRef(jButtons);
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

void NotificationBridge::AddNotificationActionHandler(const std::string& key, const std::string& option, std::function<void()> callback) {
    const std::string mkey = key + "_" + option;
    m_notificationHandlers.InsertOrAssign(mkey, std::move(callback));
}

void NotificationBridge::CallNotificationActionHandler(const std::string& key, const std::string& option) {
    const std::string mkey = key + "_" + option;
    const std::optional<std::function<void()>> callback = m_notificationHandlers.Get(mkey);

    if (!callback) {
        Debug::LogWarning("Notification action handler not found for key '{}'", mkey);
        return;
    }

    ThreadPool::Post(std::move(*callback));
}