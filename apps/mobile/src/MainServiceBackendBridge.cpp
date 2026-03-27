#include <jni.h>
#include <mutex>

#include <ConnectionManager.h>
#include <Scanner.h>
#include <DebugLog.h>

namespace
{
std::mutex g_backendMutex;
bool g_backendRunning = false;

void StartBackendIfNeeded()
{
    std::lock_guard<std::mutex> lock(g_backendMutex);
    if (g_backendRunning) {
        return;
    }

    Debug::Log("MainServiceBackendBridge: starting backend");
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
    g_backendRunning = false;
}
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
