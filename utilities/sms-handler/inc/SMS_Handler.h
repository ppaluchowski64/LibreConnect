#ifndef SMS_HANDLER_H
#define SMS_HANDLER_H

#include <vector>
#include <string>
#include <jni.h>

extern "C" {
    JNIEXPORT void JNICALL
    Java_com_LibreConnect_mobile_SmsReceiver_onSmsReceivedCPP(
        JNIEnv* env,
        jobject /* this */,
        jstring sender,
        jstring body,
        jlong timestamp
    );
}

class SmsUtilsWrapper {
public:
    static std::vector<std::pair<std::string, std::string>> GetContactList();
    static std::vector<std::string> GetMessagesFromNumber(const std::string& target);
    static bool SendSMS(const std::string& target, const std::string& message);
};

#endif // SMS_HANDLER_H
