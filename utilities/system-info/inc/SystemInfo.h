#ifndef SYSTEMINFO_H
#define SYSTEMINFO_H

#include <QString>
#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <jni.h>
#endif

class SystemInfo {
public:
    static float GetBatteryLevel();
    static bool IsVirtualCameraSupported();
    static QString VirtualCameraUnavailableReason();
#ifdef Q_OS_ANDROID
    static void SetAndroidContext(JNIEnv* env, jobject context);
    static void ClearAndroidContext(JNIEnv* env);
#endif
};

#endif // SYSTEMINFO_H
