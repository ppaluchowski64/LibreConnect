#ifndef ANDROID_CONTEXT_PROVIDER_H
#define ANDROID_CONTEXT_PROVIDER_H

#ifdef ANDROID_DEVICE

#include <jni.h>
#include <QJniObject>
#include <QJniEnvironment>

class AndroidContextProvider {
public:
    static void CacheClassLoader(JavaVM* vm, JNIEnv* env);
    static void SetServiceContext(JNIEnv* env, jobject serviceContext);
    static void ClearServiceContext(JNIEnv* env);
    static QJniObject GetAndroidContext();
    static QJniObject GetServiceContext();
    static jclass FindClass(JNIEnv* env, const char* className);
    template <typename Fn>
    static void WithJniEnv(Fn&& fn);
    static JavaVM* GetJavaVM();
    static bool HasServiceContext();
    static int GetSdkVersion();
};


template <typename Fn>
void AndroidContextProvider::WithJniEnv(Fn&& fn) {
    JavaVM* vm = GetJavaVM();
    if (!vm) {
        return;
    }

    JNIEnv* env = nullptr;
    bool attachedHere = false;
    const jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attachedHere = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    fn(env);

    if (attachedHere) {
        vm->DetachCurrentThread();
    }
}

#endif // ANDROID_DEVICE
#endif // ANDROID_CONTEXT_PROVIDER_H
