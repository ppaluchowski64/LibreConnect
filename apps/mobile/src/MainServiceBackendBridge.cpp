#include <jni.h>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <system_error>
#include <memory>
#include <QSettings>

#include <ConnectionManager.h>
#include <Scanner.h>
#include <DebugLog.h>
#include <ThreadPool.h>
#include <Events.h>

namespace
{
std::mutex g_backendMutex;
bool g_backendRunning = false;
bool g_findMyHandlersRegistered = false;
std::mutex g_storageConfigMutex;
bool g_storageConfigured = false;
std::string g_storageRoot;
std::mutex g_cameraFrameCallbackMutex;
std::mutex g_jniStateMutex;
JavaVM* g_javaVm = nullptr;
jobject g_serviceContextGlobal = nullptr;
jclass g_findMyPhoneClass = nullptr;
jmethodID g_findMyPhoneStartMethod = nullptr;
jmethodID g_findMyPhoneStopMethod = nullptr;

void ReleaseFindMyPhoneJniState(JNIEnv* env)
{
    if (!env) {
        return;
    }

    if (g_serviceContextGlobal) {
        env->DeleteGlobalRef(g_serviceContextGlobal);
        g_serviceContextGlobal = nullptr;
    }

    if (g_findMyPhoneClass) {
        env->DeleteGlobalRef(g_findMyPhoneClass);
        g_findMyPhoneClass = nullptr;
    }

    g_findMyPhoneStartMethod = nullptr;
    g_findMyPhoneStopMethod = nullptr;
}

void CacheFindMyPhoneJniState(JNIEnv* env, jobject serviceContext)
{
    if (!env || !serviceContext) {
        return;
    }

    env->GetJavaVM(&g_javaVm);

    std::lock_guard<std::mutex> lock(g_jniStateMutex);
    ReleaseFindMyPhoneJniState(env);

    g_serviceContextGlobal = env->NewGlobalRef(serviceContext);
    jclass localClass = env->FindClass("com/LibreConnect/mobile/FindMyPhone");
    if (!localClass) {
        env->ExceptionClear();
        Debug::LogError("MainServiceBackendBridge: Failed to resolve FindMyPhone class");
        return;
    }

    g_findMyPhoneClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
    if (!g_findMyPhoneClass) {
        Debug::LogError("MainServiceBackendBridge: Failed to create global class reference for FindMyPhone");
        return;
    }

    g_findMyPhoneStartMethod = env->GetStaticMethodID(
        g_findMyPhoneClass,
        "startAlert",
        "(Landroid/content/Context;Ljava/lang/String;)V"
    );
    if (!g_findMyPhoneStartMethod) {
        env->ExceptionClear();
        Debug::LogError("MainServiceBackendBridge: Failed to resolve FindMyPhone.startAlert");
    }

    g_findMyPhoneStopMethod = env->GetStaticMethodID(
        g_findMyPhoneClass,
        "stopAlert",
        "(Landroid/content/Context;)V"
    );
    if (!g_findMyPhoneStopMethod) {
        env->ExceptionClear();
        Debug::LogError("MainServiceBackendBridge: Failed to resolve FindMyPhone.stopAlert");
    }
}

template <typename Fn>
void WithJniEnv(Fn&& fn)
{
    JavaVM* vm = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_jniStateMutex);
        vm = g_javaVm;
    }

    if (!vm) {
        return;
    }

    JNIEnv* env = nullptr;
    bool attachedHere = false;
    const jint envStatus = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (envStatus == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            Debug::LogError("MainServiceBackendBridge: Failed to attach thread to JVM");
            return;
        }
        attachedHere = true;
    } else if (envStatus != JNI_OK || !env) {
        Debug::LogError("MainServiceBackendBridge: Failed to acquire JNIEnv");
        return;
    }

    fn(env);

    if (attachedHere) {
        vm->DetachCurrentThread();
    }
}

void StartFindMyPhoneAlertFromService()
{
    bool started = false;
    WithJniEnv([&started](JNIEnv* env) {
        if (!env) {
            return;
        }

        jobject context = nullptr;
        jclass clazz = nullptr;
        jmethodID method = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_jniStateMutex);
            context = g_serviceContextGlobal;
            clazz = g_findMyPhoneClass;
            method = g_findMyPhoneStartMethod;
        }

        if (!context || !clazz || !method) {
            Debug::LogWarning("MainServiceBackendBridge: FindMyPhone JNI state unavailable for start alert");
            return;
        }

        QSettings settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"));
        const QString ringtoneUri = settings.value(QStringLiteral("findMyPhone/ringtoneUri"), QString()).toString().trimmed();
        const jstring ringtoneJString = env->NewStringUTF(ringtoneUri.toUtf8().constData());

        env->CallStaticVoidMethod(clazz, method, context, ringtoneJString);
        env->DeleteLocalRef(ringtoneJString);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            Debug::LogError("MainServiceBackendBridge: Exception while calling FindMyPhone.startAlert");
            return;
        }

        settings.setValue(QStringLiteral("findMyPhone/alertActive"), true);
        started = true;
    });

    if (started) {
        ConnectionManager::SendEvent(std::make_unique<FindMyPhoneAlertStateEvent>(true));
    }
}

void StopFindMyPhoneAlertFromService()
{
    WithJniEnv([](JNIEnv* env) {
        if (!env) {
            return;
        }

        jobject context = nullptr;
        jclass clazz = nullptr;
        jmethodID method = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_jniStateMutex);
            context = g_serviceContextGlobal;
            clazz = g_findMyPhoneClass;
            method = g_findMyPhoneStopMethod;
        }

        if (!context || !clazz || !method) {
            QSettings settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"));
            settings.setValue(QStringLiteral("findMyPhone/alertActive"), false);
            return;
        }

        env->CallStaticVoidMethod(clazz, method, context);
        QSettings settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"));
        settings.setValue(QStringLiteral("findMyPhone/alertActive"), false);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            Debug::LogError("MainServiceBackendBridge: Exception while calling FindMyPhone.stopAlert");
        }
    });

    ConnectionManager::SendEvent(std::make_unique<FindMyPhoneAlertStateEvent>(false));
}

std::string JStringToStdString(JNIEnv* env, jstring value)
{
    if (!env || !value) {
        return {};
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) {
        return {};
    }

    std::string output(chars);
    env->ReleaseStringUTFChars(value, chars);
    return output;
}

std::vector<int32_t> JIntArrayToVector(JNIEnv* env, jintArray array)
{
    if (!env || !array) {
        return {};
    }

    const jsize length = env->GetArrayLength(array);
    if (length <= 0) {
        return {};
    }

    std::vector<int32_t> output(static_cast<size_t>(length));
    jint* elems = env->GetIntArrayElements(array, nullptr);
    if (!elems) {
        return {};
    }

    for (jsize i = 0; i < length; ++i) {
        output[static_cast<size_t>(i)] = static_cast<int32_t>(elems[i]);
    }

    env->ReleaseIntArrayElements(array, elems, JNI_ABORT);
    return output;
}

std::vector<std::vector<uint8_t>> JObjectArrayToByteVectors(JNIEnv* env, jobjectArray array)
{
    if (!env || !array) {
        return {};
    }

    const jsize length = env->GetArrayLength(array);
    if (length <= 0) {
        return {};
    }

    std::vector<std::vector<uint8_t>> output;
    output.reserve(static_cast<size_t>(length));

    for (jsize i = 0; i < length; ++i) {
        auto planeArray = static_cast<jbyteArray>(env->GetObjectArrayElement(array, i));
        if (!planeArray) {
            output.emplace_back();
            continue;
        }

        const jsize planeLength = env->GetArrayLength(planeArray);
        std::vector<uint8_t> plane(static_cast<size_t>(planeLength));
        if (planeLength > 0) {
            env->GetByteArrayRegion(
                planeArray,
                0,
                planeLength,
                reinterpret_cast<jbyte*>(plane.data())
            );
        }

        output.emplace_back(std::move(plane));
        env->DeleteLocalRef(planeArray);
    }

    return output;
}

void StartBackendIfNeeded()
{
    std::lock_guard<std::mutex> lock(g_backendMutex);
    if (g_backendRunning) {
        return;
    }

    Debug::Log("MainServiceBackendBridge: starting backend");
    ThreadPool::Start();
    ConnectionManager::StartAcceptingConnections();
    if (!g_findMyHandlersRegistered) {
        ConnectionManager::AddResponseHandler(PC_PackageType::FIND_MY_PHONE_START_RINGING, [](PC_Package&&) {
            StartFindMyPhoneAlertFromService();
        });
        ConnectionManager::AddResponseHandler(PC_PackageType::FIND_MY_PHONE_STOP_RINGING, [](PC_Package&&) {
            StopFindMyPhoneAlertFromService();
        });
        g_findMyHandlersRegistered = true;
    }
    LanDeviceScanner::BeginScan();
    g_backendRunning = true;
}

void StopBackendIfNeeded()
{
    std::lock_guard<std::mutex> lock(g_backendMutex);
    if (!g_backendRunning) {
        return;
    }

    Debug::Log("MainServiceBackendBridge: stopping backend");
    if (g_findMyHandlersRegistered) {
        ConnectionManager::RemoveResponseHandler(PC_PackageType::FIND_MY_PHONE_START_RINGING);
        ConnectionManager::RemoveResponseHandler(PC_PackageType::FIND_MY_PHONE_STOP_RINGING);
        g_findMyHandlersRegistered = false;
    }
    StopFindMyPhoneAlertFromService();
    LanDeviceScanner::EndScan();
    ConnectionManager::StopAcceptingConnections();

    const auto shutdownFence = ThreadPool::PostFuture([] {});
    if (shutdownFence.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        Debug::LogWarning("MainServiceBackendBridge: shutdown fence timed out before ThreadPool::Stop");
    }

    ThreadPool::Stop();
    g_backendRunning = false;
}

void ConfigureStorage(const std::string& storageRootPath)
{
    if (storageRootPath.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_storageConfigMutex);

    if (g_storageConfigured && g_storageRoot == storageRootPath) {
        return;
    }

    const std::filesystem::path storageRoot(storageRootPath);

    std::error_code ec;
    std::filesystem::create_directories(storageRoot, ec);

    std::filesystem::current_path(storageRoot, ec);

    const Debug::Settings settings{
        .rootPath = storageRoot.string(),
        .maxFileSize = 64 * 1024 * 1024ULL,
        .maxLogFilesAmount = 5,
        .deleteLogsAfter = 60 * 60 * 24 * 7
    };

    try {
        Debug::SetSettings(settings);
    } catch (...) {}

    g_storageRoot = storageRootPath;
    g_storageConfigured = true;
}
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeConfigureStorage(
    JNIEnv* env,
    jobject,
    jstring storageRootPath)
{
    ConfigureStorage(JStringToStdString(env, storageRootPath));
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeStartBackend(
    JNIEnv* env,
    jobject thiz)
{
    CacheFindMyPhoneJniState(env, thiz);
    StartBackendIfNeeded();
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeStopBackend(
    JNIEnv* env,
    jobject)
{
    StopBackendIfNeeded();
    std::lock_guard<std::mutex> lock(g_jniStateMutex);
    ReleaseFindMyPhoneJniState(env);
}
