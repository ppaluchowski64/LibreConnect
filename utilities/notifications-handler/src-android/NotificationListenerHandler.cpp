#include <NotificationBridge.h>
#include <NotificationListenerHandler.h>
#include <NotificationData.h>
#include <algorithm>
#include <mutex>

static void GetString(JNIEnv* env, const jstring& str, std::string& dst) {
    if (str == nullptr) {
        dst.clear();
        return;
    }

    const char* utfChars = env->GetStringUTFChars(str, nullptr);
    if (utfChars == nullptr) {
        dst.clear();
        return;
    }

    dst.assign(utfChars);
    env->ReleaseStringUTFChars(str, utfChars);
}

static void GetByteArray(JNIEnv* env, const jbyteArray byteArray, std::vector<uint8_t>& dst) {
    const jsize len = env->GetArrayLength(byteArray);
    dst.resize(len);
    env->GetByteArrayRegion(byteArray, 0, len, reinterpret_cast<jbyte*>(dst.data()));
}

std::mutex g_notificationDatasMutex;
std::vector<NotificationData> g_notificationDatas;

std::mutex g_notificationCallbackMutex;
std::function<void(const std::string& key)> g_notificationCallback;

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_NotificationListener_onNotificationReceivedCPP(
    JNIEnv* env,
    jobject,
    const jstring key,
    const jstring title,
    const jstring content,
    const jlong timestamp,
    const jbyteArray iconBytes,
    const jbyteArray imageBytes) {

    if (key == nullptr) {
        return;
    }

    NotificationData notificationData{};
    GetString(env, key, notificationData.key);
    GetString(env, title, notificationData.title);
    GetString(env, content, notificationData.content);

    if (iconBytes != nullptr) {
        GetByteArray(env, iconBytes, notificationData.largeIcon);
    }

    if (imageBytes != nullptr) {
        GetByteArray(env, imageBytes, notificationData.mainImage);
    }

    Debug::Log("captured notification {}", notificationData.key);

    notificationData.timestamp = timestamp;

    const std::string keyC = notificationData.key;

    {
        std::lock_guard lock(g_notificationDatasMutex);
        g_notificationDatas.push_back(notificationData);
    }

    {
        std::lock_guard lock(g_notificationCallbackMutex);
        if (!g_notificationCallback) {
            Debug::LogWarning("notification callback not set (key={})", keyC);
            return;
        }

        g_notificationCallback(keyC);
    }
}

void ClearNotificationDatas() {
    std::lock_guard lock(g_notificationDatasMutex);
    g_notificationDatas.clear();
    Debug::Log("NotificationSync: Cleared notification datas");
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_NotificationListener_onNotificationRemovedCPP(
    JNIEnv* env,
    jobject,
    const jstring key
) {
    if (key == nullptr) {
        return;
    }

    std::string keyStr;
    GetString(env, key, keyStr);

    {
        std::lock_guard lock(g_notificationDatasMutex);
        std::erase_if(g_notificationDatas, [&keyStr](const NotificationData& notification) {
            return notification.key == keyStr;
        });
    }

    Debug::Log("removed notification {}", keyStr);
}



extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_NotificationActionReceiver_onNotificationActionReceivedCPP(
    JNIEnv* env,
    jobject,
    const jstring key,
    const jstring option
) {
    if (key == nullptr || option == nullptr) {
        return;
    }

    std::string keyStr;
    std::string optionStr;

    GetString(env, key, keyStr);
    GetString(env, option, optionStr);

    Debug::Log("Action {}:{}", keyStr, optionStr);
    NotificationBridge::CallNotificationActionHandler(keyStr, optionStr);
}
