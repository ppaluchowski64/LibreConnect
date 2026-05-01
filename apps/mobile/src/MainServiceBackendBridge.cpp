#include <jni.h>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <system_error>

#include <ConnectionManager.h>
#include <Scanner.h>
#include <DebugLog.h>
#include <ThreadPool.h>

namespace
{
std::mutex g_backendMutex;
bool g_backendRunning = false;
std::mutex g_storageConfigMutex;
bool g_storageConfigured = false;
std::string g_storageRoot;
std::mutex g_cameraFrameCallbackMutex;

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
    JNIEnv*,
    jobject)
{
    StartBackendIfNeeded();
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeStopBackend(
    JNIEnv*,
    jobject)
{
    StopBackendIfNeeded();
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeShareLogs(
    JNIEnv* env,
    jobject obj)
{
    // obj is the MainService instance, which is a Context
    jclass utilsClass = env->FindClass("com/LibreConnect/mobile/FileSystemUtils");
    if (!utilsClass) return;

    jmethodID shareMethod = env->GetStaticMethodID(utilsClass, "shareLogs", "(Landroid/content/Context;)V");
    if (!shareMethod) return;

    env->CallStaticVoidMethod(utilsClass, shareMethod, obj);
}
