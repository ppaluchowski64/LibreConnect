#include <QCoreApplication>
#include <QMetaObject>
#include <QString>
#include <QJniObject>
#include <QJniArray>
#include <exception>

#include <QtCore/qjniobject.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qcoreapplication_platform.h>
#include <AndroidContextProvider.h>

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
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        Debug::LogError("NotificationBridge::PostNotification: Invalid Android context");
        return;
    }

    QJniEnvironment envWrapper;
    JNIEnv* env = envWrapper.jniEnv();
    if (!env) {
        Debug::LogError("NotificationBridge::PostNotification: No JNIEnv");
        return;
    }

    jclass bridgeClass = AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/NotificationBridge");
    if (!bridgeClass) {
        Debug::LogError("NotificationBridge::PostNotification: Failed to find NotificationBridge class");
        return;
    }

    // Get constructor
    jmethodID constructor = env->GetMethodID(bridgeClass, "<init>", "(Landroid/content/Context;)V");
    if (!constructor) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        Debug::LogError("NotificationBridge::PostNotification: Failed to find constructor");
        return;
    }

    jobject bridge = env->NewObject(bridgeClass, constructor, context.object<jobject>());
    env->DeleteLocalRef(bridgeClass);
    if (!bridge) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        Debug::LogError("NotificationBridge::PostNotification: Failed to create bridge instance");
        return;
    }

    // Get postNotification method
    jclass instanceClass = env->GetObjectClass(bridge);
    jmethodID postMethod = env->GetMethodID(
        instanceClass,
        "postNotification",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J[B[B[Ljava/lang/String;)V"
    );
    env->DeleteLocalRef(instanceClass);
    if (!postMethod) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridge);
        Debug::LogError("NotificationBridge::PostNotification: Failed to find postNotification method");
        return;
    }

    jstring jKey = env->NewStringUTF(notificationData.key.c_str());
    jstring jTitle = env->NewStringUTF(notificationData.title.c_str());
    jstring jContent = env->NewStringUTF(notificationData.content.c_str());

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

    jarray jButtons = createStringArray(envWrapper, notificationData.buttons);

    env->CallVoidMethod(
        bridge,
        postMethod,
        jKey, jTitle, jContent,
        static_cast<jlong>(notificationData.timestamp),
        jSmallIcon, jLargeIcon, jButtons
    );

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    if (jButtons) env->DeleteLocalRef(jButtons);
    if (jLargeIcon) env->DeleteLocalRef(jLargeIcon);
    if (jSmallIcon) env->DeleteLocalRef(jSmallIcon);
    env->DeleteLocalRef(jContent);
    env->DeleteLocalRef(jTitle);
    env->DeleteLocalRef(jKey);
    env->DeleteLocalRef(bridge);
}

void NotificationBridge::RemoveNotification(const std::string& key) {
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return;
    }

    QJniEnvironment envWrapper;
    JNIEnv* env = envWrapper.jniEnv();
    if (!env) return;

    jclass bridgeClass = AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/NotificationBridge");
    if (!bridgeClass) return;

    jmethodID constructor = env->GetMethodID(bridgeClass, "<init>", "(Landroid/content/Context;)V");
    if (!constructor) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return;
    }

    jobject bridge = env->NewObject(bridgeClass, constructor, context.object<jobject>());
    env->DeleteLocalRef(bridgeClass);
    if (!bridge) {
        if (env->ExceptionCheck()) { env->ExceptionClear(); }
        return;
    }

    jclass instanceClass = env->GetObjectClass(bridge);
    jmethodID removeMethod = env->GetMethodID(instanceClass, "removeNotification", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(instanceClass);
    if (!removeMethod) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridge);
        return;
    }

    jstring jKey = env->NewStringUTF(key.c_str());
    env->CallVoidMethod(bridge, removeMethod, jKey);
    env->DeleteLocalRef(jKey);
    env->DeleteLocalRef(bridge);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
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
