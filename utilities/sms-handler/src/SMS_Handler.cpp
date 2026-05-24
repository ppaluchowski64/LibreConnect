#include <QGuiApplication>

#include <SMS_Handler.h>
#include <ConnectionManager.h>
#include <DebugLog.h>
#include <AndroidContextProvider.h>

std::vector<std::pair<std::string, std::string>> SmsUtilsWrapper::GetContactList() {
    std::vector<std::pair<std::string, std::string>> contactsList;
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return contactsList;
    }

    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv) {
        return contactsList;
    }

    jclass smsUtilsClass = AndroidContextProvider::FindClass(jniEnv, "com/LibreConnect/mobile/SmsUtils");
    if (!smsUtilsClass) {
        Debug::LogWarning("SmsUtilsWrapper: Failed to find SmsUtils class");
        return contactsList;
    }

    jmethodID method = jniEnv->GetStaticMethodID(
        smsUtilsClass,
        "getAllContacts",
        "(Landroid/content/Context;)Ljava/util/List;"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(smsUtilsClass);
        Debug::LogWarning("SmsUtilsWrapper: Failed to find getAllContacts method");
        return contactsList;
    }

    jobject listObj = jniEnv->CallStaticObjectMethod(smsUtilsClass, method, context.object<jobject>());
    jniEnv->DeleteLocalRef(smsUtilsClass);

    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionDescribe();
        jniEnv->ExceptionClear();
        return contactsList;
    }

    if (listObj) {
        const QJniObject list = QJniObject::fromLocalRef(listObj);
        const jint size = list.callMethod<jint>("size");
        contactsList.reserve(size);

        for (int i = 0; i < size; i++) {
            QJniObject pairObj = list.callObjectMethod("get", "(I)Ljava/lang/Object;", i);

            if (pairObj.isValid()) {
                QJniObject firstObj = pairObj.callObjectMethod("getFirst", "()Ljava/lang/Object;");
                QJniObject secondObj = pairObj.callObjectMethod("getSecond", "()Ljava/lang/Object;");

                std::string name = firstObj.isValid() ? firstObj.toString().toStdString() : "";
                std::string number = secondObj.isValid() ? secondObj.toString().toStdString() : "";

                contactsList.push_back(std::make_pair(name, number));
            }
        }
    }

    return contactsList;
}

std::vector<std::string> SmsUtilsWrapper::GetMessagesFromNumber(const std::string& target) {
    std::vector<std::string> messagesList;
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return messagesList;
    }

    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv) {
        return messagesList;
    }

    jclass smsUtilsClass = AndroidContextProvider::FindClass(jniEnv, "com/LibreConnect/mobile/SmsUtils");
    if (!smsUtilsClass) {
        Debug::LogWarning("SmsUtilsWrapper: Failed to find SmsUtils class for getMessagesFromNumber");
        return messagesList;
    }

    jmethodID method = jniEnv->GetStaticMethodID(
        smsUtilsClass,
        "getMessagesFromNumber",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/util/List;"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(smsUtilsClass);
        Debug::LogWarning("SmsUtilsWrapper: Failed to find getMessagesFromNumber method");
        return messagesList;
    }

    const QJniObject jTargetNumber = QJniObject::fromString(QString::fromStdString(target));
    jobject listObj = jniEnv->CallStaticObjectMethod(
        smsUtilsClass, method,
        context.object<jobject>(),
        jTargetNumber.object<jstring>()
    );
    jniEnv->DeleteLocalRef(smsUtilsClass);

    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionDescribe();
        jniEnv->ExceptionClear();
        return messagesList;
    }

    if (listObj) {
        const QJniObject list = QJniObject::fromLocalRef(listObj);
        const jint size = list.callMethod<jint>("size");
        messagesList.reserve(size);

        for (int i = 0; i < size; ++i) {
            QJniObject stringObj = list.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
            if (stringObj.isValid()) {
                messagesList.push_back(stringObj.toString().toStdString());
            }
        }
    }

    return messagesList;
}

bool SmsUtilsWrapper::SendSMS(const std::string& target, const std::string& message) {
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        Debug::LogError("SmsUtilsWrapper::SendSMS: Invalid Android context");
        return false;
    }

    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv) {
        Debug::LogError("SmsUtilsWrapper::SendSMS: No JNIEnv");
        return false;
    }

    jclass smsUtilsClass = AndroidContextProvider::FindClass(jniEnv, "com/LibreConnect/mobile/SmsUtils");
    if (!smsUtilsClass) {
        Debug::LogError("SmsUtilsWrapper::SendSMS: Failed to find SmsUtils class");
        return false;
    }

    jmethodID method = jniEnv->GetStaticMethodID(
        smsUtilsClass,
        "sendSms",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(smsUtilsClass);
        Debug::LogError("SmsUtilsWrapper::SendSMS: Failed to find sendSms method");
        return false;
    }

    const QJniObject jTargetNumber = QJniObject::fromString(QString::fromStdString(target));
    const QJniObject jMessage = QJniObject::fromString(QString::fromStdString(message));

    const jboolean result = jniEnv->CallStaticBooleanMethod(
        smsUtilsClass, method,
        context.object<jobject>(),
        jTargetNumber.object<jstring>(),
        jMessage.object<jstring>()
    );
    jniEnv->DeleteLocalRef(smsUtilsClass);

    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionDescribe();
        jniEnv->ExceptionClear();
        return false;
    }

    return result;
}

