#ifndef NT_KOTLIN_NOTIFICATION_LISTENER_HANDLER_H
#define NT_KOTLIN_NOTIFICATION_LISTENER_HANDLER_H

#include <jni.h>
#include <vector>
#include <DebugLog.h>

void ClearNotificationDatas();

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_NotificationListener_onNotificationReceivedCPP(
    JNIEnv* env,
    jobject,
    jstring key,
    jstring appName,
    jstring title,
    jstring content,
    jlong timestamp,
    jboolean dismissable,
    jbyteArray iconBytes,
    jbyteArray imageBytes
);

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_NotificationListener_onNotificationRemovedCPP(
    JNIEnv* env,
    jobject,
    jstring key
);

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_NotificationActionReceiver_onNotificationActionReceivedCPP(
    JNIEnv* env,
    jobject,
    jstring key,
    jstring option
);

#endif // NT_KOTLIN_NOTIFICATION_LISTENER_HANDLER_H
