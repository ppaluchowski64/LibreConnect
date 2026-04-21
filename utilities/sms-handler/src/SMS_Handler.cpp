#include <QGuiApplication>

#include <SMS_Handler.h>
#include <ConnectionManager.h>
#include <DebugLog.h>

std::vector<std::pair<std::string, std::string>> SmsUtilsWrapper::GetContactList() {
    std::vector<std::pair<std::string, std::string>> contactsList;
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return contactsList;
    }

    const QJniObject listObj = QJniObject::callStaticObjectMethod(
        "com/LibreConnect/mobile/SmsUtils",
        "getAllContacts",
        "(Landroid/content/Context;)Ljava/util/List;",
        context.object()
    );

    if (listObj.isValid()) {
        const jint size = listObj.callMethod<jint>("size");
        contactsList.reserve(size);

        for (int i = 0; i < size; i++) {
            QJniObject pairObj = listObj.callObjectMethod("get", "(I)Ljava/lang/Object;", i);

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
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return messagesList;
    }

    const QJniObject jTargetNumber = QJniObject::fromString(QString::fromStdString(target));
    const QJniObject listObj = QJniObject::callStaticObjectMethod(
        "com/LibreConnect/mobile/SmsUtils",
        "getMessagesFromNumber",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/util/List;",
        context.object(),
        jTargetNumber.object<jstring>()
    );

    if (listObj.isValid()) {
        const jint size = listObj.callMethod<jint>("size");
        messagesList.reserve(size);

        for (int i = 0; i < size; ++i) {
            QJniObject stringObj = listObj.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
            if (stringObj.isValid()) {
                messagesList.push_back(stringObj.toString().toStdString());
            }
        }
    }

    return messagesList;
}

bool SmsUtilsWrapper::SendSMS(const std::string& target, const std::string& message) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        Debug::LogError("SmsUtilsWrapper::SendSMS: Invalid Android context");
        return false;
    }

    const QJniObject jTargetNumber = QJniObject::fromString(QString::fromStdString(target));
    const QJniObject jMessage = QJniObject::fromString(QString::fromStdString(message));

    const jboolean result = QJniObject::callStaticMethod<jboolean>(
        "com/LibreConnect/mobile/SmsUtils",
        "sendSms",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        context.object(),
        jTargetNumber.object<jstring>(),
        jMessage.object<jstring>()
    );

    return result;
}
