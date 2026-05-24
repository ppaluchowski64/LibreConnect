#ifdef ANDROID_DEVICE

#include <AndroidContextProvider.h>
#include <QtCore/qcoreapplication_platform.h>
#include <DebugLog.h>

#include <mutex>
#include <string>
#include <cstring>

static std::mutex g_contextMutex{};
static JavaVM* g_javaVm = nullptr;
static jobject g_serviceContext = nullptr;
static jobject g_appClassLoader = nullptr;
static jmethodID g_loadClassMethod = nullptr;
static int g_sdkVersion = -1;

void AndroidContextProvider::CacheClassLoader(JavaVM* vm, JNIEnv* env) {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    g_javaVm = vm;

    if (!env) {
        return;
    }

    jclass mainServiceClass = env->FindClass("com/LibreConnect/mobile/MainService");
    if (!mainServiceClass) {
        env->ExceptionClear();
        Debug::LogWarning("AndroidContextProvider: Failed to find MainService class for classloader caching");
        return;
    }

    jclass classClass = env->GetObjectClass(mainServiceClass);
    if (!classClass) {
        env->DeleteLocalRef(mainServiceClass);
        return;
    }

    jmethodID getClassLoaderMethod = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getClassLoaderMethod) {
        env->ExceptionClear();
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(mainServiceClass);
        Debug::LogWarning("AndroidContextProvider: Failed to find getClassLoader method");
        return;
    }

    jobject classLoader = env->CallObjectMethod(mainServiceClass, getClassLoaderMethod);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(mainServiceClass);
        Debug::LogWarning("AndroidContextProvider: Exception calling getClassLoader");
        return;
    }

    if (classLoader) {
        if (g_appClassLoader) {
            env->DeleteGlobalRef(g_appClassLoader);
        }
        g_appClassLoader = env->NewGlobalRef(classLoader);
        env->DeleteLocalRef(classLoader);

        jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
        if (classLoaderClass) {
            g_loadClassMethod = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            if (!g_loadClassMethod) {
                env->ExceptionClear();
                Debug::LogWarning("AndroidContextProvider: Failed to find ClassLoader.loadClass method");
            }
            env->DeleteLocalRef(classLoaderClass);
        }

        Debug::Log("AndroidContextProvider: App classloader cached successfully");
    }

    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (versionClass) {
        jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
        if (sdkIntField) {
            g_sdkVersion = env->GetStaticIntField(versionClass, sdkIntField);
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(versionClass);
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(mainServiceClass);
}

void AndroidContextProvider::SetServiceContext(JNIEnv* env, jobject serviceContext) {
    if (!env || !serviceContext) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_contextMutex);

    if (g_serviceContext) {
        env->DeleteGlobalRef(g_serviceContext);
    }

    g_serviceContext = env->NewGlobalRef(serviceContext);
    Debug::Log("AndroidContextProvider: Service context cached");
}

void AndroidContextProvider::ClearServiceContext(JNIEnv* env) {
    if (!env) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_contextMutex);

    if (g_serviceContext) {
        env->DeleteGlobalRef(g_serviceContext);
        g_serviceContext = nullptr;
    }
}

QJniObject AndroidContextProvider::GetAndroidContext() {
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        return context;
    }

    return GetServiceContext();
}

QJniObject AndroidContextProvider::GetServiceContext() {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    if (!g_serviceContext) {
        return {};
    }
    return QJniObject::fromLocalRef(QJniEnvironment().jniEnv()->NewLocalRef(g_serviceContext));
}

jclass AndroidContextProvider::FindClass(JNIEnv* env, const char* className) {
    if (!env || !className) {
        return nullptr;
    }

    jclass result = env->FindClass(className);
    if (result) {
        return result;
    }
    env->ExceptionClear();

    jobject classLoader = nullptr;
    jmethodID loadClassMethod = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        classLoader = g_appClassLoader;
        loadClassMethod = g_loadClassMethod;
    }

    if (!classLoader || !loadClassMethod) {
        return nullptr;
    }

    std::string dotName(className);
    for (char& c : dotName) {
        if (c == '/') c = '.';
    }

    jstring jClassName = env->NewStringUTF(dotName.c_str());
    if (!jClassName) {
        return nullptr;
    }

    result = static_cast<jclass>(env->CallObjectMethod(classLoader, loadClassMethod, jClassName));
    env->DeleteLocalRef(jClassName);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    return result;
}

JavaVM* AndroidContextProvider::GetJavaVM() {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    return g_javaVm;
}

bool AndroidContextProvider::HasServiceContext() {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    return g_serviceContext != nullptr;
}

int AndroidContextProvider::GetSdkVersion() {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    if (g_sdkVersion >= 0) {
        return g_sdkVersion;
    }

    return QNativeInterface::QAndroidApplication::sdkVersion();
}

#endif // ANDROID_DEVICE
