#include <KotlinNotificationListenerHandler.h>
#include <NotificationData.h>

static void GetString(JNIEnv* env, const jstring& str, std::string& dst) {
    const jsize len = env->GetStringLength(str);
    dst.resize(len);
    env->GetStringRegion(str, 0, len, reinterpret_cast<jchar*>(dst.data()));
}

static void GetByteArray(JNIEnv* env, const jbyteArray byteArray, std::vector<uint8_t>& dst) {
    const jsize len = env->GetArrayLength(byteArray);
    dst.resize(len);
    env->GetByteArrayRegion(byteArray, 0, len, reinterpret_cast<jbyte*>(dst.data()));
}

void Java_com_LibreConnect_mobile_NotificationListener_onNotificationReceivedCPP(
    JNIEnv* env,
    jobject,
    const jstring key,
    const jstring title,
    const jstring content,
    const jlong timestamp,
    const jbyteArray iconBytes) {

    if (key == nullptr || title == nullptr || content == nullptr) {
        return;
    }

    NotificationData notificationData{};

    GetString(env, key, notificationData.key);
    GetString(env, title, notificationData.title);
    GetString(env, content, notificationData.content);

    if (iconBytes == nullptr) {
        notificationData.icon = std::nullopt;
    } else {
        GetByteArray(env, iconBytes, notificationData.icon.value());
    }
}

