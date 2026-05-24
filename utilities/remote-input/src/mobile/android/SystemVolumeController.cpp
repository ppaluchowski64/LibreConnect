#include "SystemVolumeController.h"

#include <AndroidContextProvider.h>
#include <DebugLog.h>

#include <algorithm>

namespace {
QJniObject GetContext() {
    return AndroidContextProvider::GetAndroidContext();
}
}

int SystemVolumeController::GetVolume() {
    const QJniObject context = GetContext();

    if (!context.isValid())
        return 0;

    int result = 0;
    AndroidContextProvider::WithJniEnv([&result, &context](JNIEnv* env) {
        jclass bridgeClass = AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/MediaRemoteBridge");
        if (!bridgeClass) {
            Debug::LogWarning("Android SystemVolumeController: failed to resolve MediaRemoteBridge for getVolume");
            return;
        }

        jmethodID method = env->GetStaticMethodID(bridgeClass, "getVolume", "(Landroid/content/Context;)I");
        if (!method) {
            env->ExceptionClear();
            env->DeleteLocalRef(bridgeClass);
            Debug::LogWarning("Android SystemVolumeController: failed to resolve MediaRemoteBridge.getVolume");
            return;
        }

        result = env->CallStaticIntMethod(bridgeClass, method, context.object<jobject>());
        env->DeleteLocalRef(bridgeClass);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            result = 0;
        }
    });

    return std::clamp(result, 0, 100);
}

void SystemVolumeController::SetVolume(int percentage) {
    percentage = std::clamp(percentage, 0, 100);

    const QJniObject context = GetContext();

    if (!context.isValid())
        return;

    AndroidContextProvider::WithJniEnv([percentage, &context](JNIEnv* env) {
        jclass bridgeClass = AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/MediaRemoteBridge");
        if (!bridgeClass) {
            Debug::LogWarning("Android SystemVolumeController: failed to resolve MediaRemoteBridge for setVolume");
            return;
        }

        jmethodID method = env->GetStaticMethodID(bridgeClass, "setVolume", "(Landroid/content/Context;I)V");
        if (!method) {
            env->ExceptionClear();
            env->DeleteLocalRef(bridgeClass);
            Debug::LogWarning("Android SystemVolumeController: failed to resolve MediaRemoteBridge.setVolume");
            return;
        }

        env->CallStaticVoidMethod(bridgeClass, method, context.object<jobject>(), static_cast<jint>(percentage));
        env->DeleteLocalRef(bridgeClass);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    });
}
