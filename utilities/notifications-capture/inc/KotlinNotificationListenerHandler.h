#ifndef NT_KOTLIN_NOTIFICATION_LISTENER_HANDLER_H
#define NT_KOTLIN_NOTIFICATION_LISTENER_HANDLER_H

#include <jni.h>
#include <vector>
#include <DebugLog.h>

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_NotificationListener_onNotificationReceivedCPP(
    JNIEnv* env,
    jobject,
    jstring key,
    jstring title,
    jstring content,
    jlong timestamp,
    jbyteArray iconBytes
);

#endif // NT_KOTLIN_NOTIFICATION_LISTENER_HANDLER_H