#include <NotificationBridge.h>
#include <NotificationListenerHandler.h>
#include <NotificationData.h>
#include <algorithm>
#include <limits>
#include <mutex>

namespace {
constexpr size_t MAX_NOTIFICATION_IMAGE_BYTES = 8 * 1024 * 1024;
}

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
    if (byteArray == nullptr) {
        dst.clear();
        return;
    }

    const jsize len = env->GetArrayLength(byteArray);
    if (len <= 0) {
        dst.clear();
        return;
    }

    if (static_cast<size_t>(len) > MAX_NOTIFICATION_IMAGE_BYTES) {
        Debug::LogWarning(
            "Skipping oversized notification image payload: {} bytes (limit: {})",
            len,
            MAX_NOTIFICATION_IMAGE_BYTES
        );
        dst.clear();
        return;
    }

    if (static_cast<size_t>(len) > std::numeric_limits<size_t>::max() / sizeof(uint8_t)) {
        dst.clear();
        return;
    }

    dst.resize(len);
    env->GetByteArrayRegion(byteArray, 0, len, reinterpret_cast<jbyte*>(dst.data()));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        Debug::LogWarning("Failed to copy notification image byte array from JNI");
        dst.clear();
    }
}

std::mutex g_notificationDatasMutex;
std::vector<NotificationData> g_notificationDatas;

std::mutex g_notificationCallbackMutex;
std::function<void(const std::string& key)> g_notificationCallback;

std::mutex g_notificationRemovedCallbackMutex;
std::function<void(const std::string& key)> g_notificationRemovedCallback;

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_NotificationListener_onNotificationReceivedCPP(
    JNIEnv* env,
    jobject,
    const jstring key,
    const jstring appName,
    const jstring title,
    const jstring content,
    const jlong timestamp,
    const jboolean dismissable,
    const jbyteArray iconBytes,
    const jbyteArray imageBytes) {

    if (key == nullptr) {
        return;
    }

    NotificationData notificationData{};
    GetString(env, key, notificationData.key);
    GetString(env, appName, notificationData.appName);
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
    notificationData.dismissable = dismissable == JNI_TRUE;

    const std::string keyC = notificationData.key;
    bool existed = false;

    {
        std::lock_guard lock(g_notificationDatasMutex);
        for (NotificationData& existing : g_notificationDatas) {
            if (existing.key == notificationData.key) {
                existing = std::move(notificationData);
                existed = true;
                break;
            }
        }

        if (!existed) {
            g_notificationDatas.push_back(std::move(notificationData));
        }
    }

    {
        std::lock_guard lock(g_notificationCallbackMutex);
        if (g_notificationCallback) {
            g_notificationCallback(keyC);
        }
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

    {
        std::lock_guard lock(g_notificationRemovedCallbackMutex);
        if (g_notificationRemovedCallback) {
            g_notificationRemovedCallback(keyStr);
        }
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
