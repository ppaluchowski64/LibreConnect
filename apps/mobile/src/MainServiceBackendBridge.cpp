#include <jni.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <system_error>
#include <memory>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QSettings>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>

#include <ConnectionManager.h>
#include <ModulesManager.h>
#include <ClipboardSyncModule.h>
#include <FileShareModule.h>
#include <PermissionManager.h>
#include <Scanner.h>
#include <DebugLog.h>
#include <SystemInfo.h>
#include <ThreadPool.h>
#include <Events.h>
#include <AndroidContextProvider.h>
#include <RemoteInputModule.h>
#include <RemoteInputEvents.h>
#include <InputTypes.h>
#include <NotificationBridge.h>
#include <Qt>

std::mutex g_backendMutex;
enum class BackendState {
    Stopped,
    Starting,
    Running,
    Stopping
};

BackendState g_backendState = BackendState::Stopped;
bool g_findMyHandlersRegistered = false;
std::mutex g_storageConfigMutex;
bool g_storageConfigured = false;
std::string g_storageRoot;
std::mutex g_clipboardMutex;
std::string g_lastRemoteClipboard;
std::mutex g_cameraFrameCallbackMutex;
std::mutex g_jniStateMutex;
JavaVM* g_javaVm = nullptr;
jobject g_serviceContextGlobal = nullptr;
jclass g_findMyPhoneClass = nullptr;
jmethodID g_findMyPhoneStartMethod = nullptr;
jmethodID g_findMyPhoneStopMethod = nullptr;
std::mutex g_qtRuntimeMutex;
std::condition_variable g_qtRuntimeCv;
std::thread g_qtRuntimeThread;
QCoreApplication* g_qtRuntimeApp = nullptr;
bool g_qtRuntimeReady = false;
bool g_qtRuntimeStopping = false;
std::mutex g_connectionPromptMutex;
std::unique_ptr<ConnectionPendingEvent> g_pendingConnectionPrompt;
std::unique_ptr<ConnectionApprovalRequestedEvent> g_pendingApprovalPrompt;

void StartBackendIfNeeded();
void StopBackendIfNeeded();

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    AndroidContextProvider::CacheClassLoader(vm, env);
    return JNI_VERSION_1_6;
}

void BackendQtRuntimeMain()
{
    int argc = 1;
    char appName[] = "LibreConnectBackend";
    char* argv[] = {appName, nullptr};

    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("LibreConnect"));
    app.setApplicationName(QStringLiteral("LibreConnectMobile"));
    {
        std::lock_guard<std::mutex> lock(g_qtRuntimeMutex);
        g_qtRuntimeApp = &app;
        g_qtRuntimeReady = false;
    }

    StartBackendIfNeeded();
    {
        std::lock_guard<std::mutex> lock(g_qtRuntimeMutex);
        g_qtRuntimeReady = true;
        g_qtRuntimeCv.notify_all();
    }

    app.exec();
    StopBackendIfNeeded();

    {
        std::lock_guard<std::mutex> lock(g_qtRuntimeMutex);
        g_qtRuntimeApp = nullptr;
        g_qtRuntimeReady = false;
        g_qtRuntimeStopping = false;
        g_qtRuntimeCv.notify_all();
    }
}

bool EnsureBackendQtRuntime()
{
    std::thread finishedThread;
    {
        std::lock_guard<std::mutex> lock(g_qtRuntimeMutex);
        if (g_qtRuntimeThread.joinable() && g_qtRuntimeApp == nullptr && !g_qtRuntimeStopping) {
            finishedThread = std::move(g_qtRuntimeThread);
        }
    }

    if (finishedThread.joinable()) {
        finishedThread.join();
    }

    std::unique_lock<std::mutex> lock(g_qtRuntimeMutex);
    if (g_qtRuntimeReady) {
        return true;
    }

    if (g_qtRuntimeStopping) {
        return false;
    }

    if (!g_qtRuntimeThread.joinable()) {
        g_qtRuntimeThread = std::thread(BackendQtRuntimeMain);
    }

    return g_qtRuntimeCv.wait_for(lock, std::chrono::seconds(10), [] {
        return g_qtRuntimeReady || g_qtRuntimeStopping;
    }) && g_qtRuntimeReady;
}

void StopBackendQtRuntime()
{
    QCoreApplication* app = nullptr;
    std::thread runtimeThread;
    {
        std::unique_lock<std::mutex> lock(g_qtRuntimeMutex);
        if (!g_qtRuntimeThread.joinable()) {
            return;
        }
        g_qtRuntimeStopping = true;
        if (!g_qtRuntimeApp) {
            g_qtRuntimeCv.wait_for(lock, std::chrono::seconds(10), [] {
                return g_qtRuntimeApp != nullptr;
            });
        }
        app = g_qtRuntimeApp;
        runtimeThread = std::move(g_qtRuntimeThread);
    }

    if (app) {
        QMetaObject::invokeMethod(app, [] {
            QCoreApplication::quit();
        }, Qt::QueuedConnection);
    }

    if (runtimeThread.joinable()) {
        runtimeThread.join();
    }
}

void PostToBackendQt(std::function<void()> task)
{
    if (!EnsureBackendQtRuntime()) {
        return;
    }

    QCoreApplication* app = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_qtRuntimeMutex);
        app = g_qtRuntimeApp;
    }

    if (!app) {
        return;
    }

    QMetaObject::invokeMethod(app, [task = std::move(task)]() mutable {
        task();
    }, Qt::QueuedConnection);
}

std::mutex g_mediaMutex;
std::string g_mediaTitle;
std::string g_mediaArtist;
std::string g_mediaCollection;
std::string g_mediaElapsed;
bool g_mediaPlaying = false;
double g_mediaPositionSeconds = 0.0;
double g_mediaDurationSeconds = 0.0;
int g_mediaVolume = 0;
std::string g_mediaCoverPath;

std::mutex g_downloadPathMutex;
std::string g_downloadPath;
std::string g_downloadPathStatus;

namespace {
    struct KeyMapping {
        int key;
        bool requiresShift;
    };
    constexpr int INVALID_KEY = -1;

    KeyMapping mapQtSpecialKey(int qtKey) {
        switch (qtKey) {
        case Qt::Key_Backspace: return { static_cast<int>(Key::Backspace), false };
        case Qt::Key_Return:
        case Qt::Key_Enter: return { static_cast<int>(Key::Enter), false };
        case Qt::Key_Tab: return { static_cast<int>(Key::Tab), false };
        case Qt::Key_Escape: return { static_cast<int>(Key::Escape), false };
        case Qt::Key_Space: return { static_cast<int>(Key::Space), false };
        case Qt::Key_Up: return { static_cast<int>(Key::Up), false };
        case Qt::Key_Down: return { static_cast<int>(Key::Down), false };
        case Qt::Key_Left: return { static_cast<int>(Key::Left), false };
        case Qt::Key_Right: return { static_cast<int>(Key::Right), false };
        case Qt::Key_PageUp: return { static_cast<int>(Key::PageUp), false };
        case Qt::Key_PageDown: return { static_cast<int>(Key::PageDown), false };
        case Qt::Key_Home: return { static_cast<int>(Key::Home), false };
        case Qt::Key_End: return { static_cast<int>(Key::End), false };
        case Qt::Key_Delete: return { static_cast<int>(Key::Delete), false };
        case Qt::Key_Insert: return { static_cast<int>(Key::Insert), false };
        case Qt::Key_Shift: return { static_cast<int>(Key::LeftShift), false };
        case Qt::Key_Control: return { static_cast<int>(Key::LeftControl), false };
        case Qt::Key_Alt: return { static_cast<int>(Key::LeftAlt), false };
        case Qt::Key_Meta: return { static_cast<int>(Key::LeftSuper), false };
        case Qt::Key_Menu: return { static_cast<int>(Key::Menu), false };
        case Qt::Key_F1: return { static_cast<int>(Key::F1), false };
        case Qt::Key_F2: return { static_cast<int>(Key::F2), false };
        case Qt::Key_F3: return { static_cast<int>(Key::F3), false };
        case Qt::Key_F4: return { static_cast<int>(Key::F4), false };
        case Qt::Key_F5: return { static_cast<int>(Key::F5), false };
        case Qt::Key_F6: return { static_cast<int>(Key::F6), false };
        case Qt::Key_F7: return { static_cast<int>(Key::F7), false };
        case Qt::Key_F8: return { static_cast<int>(Key::F8), false };
        case Qt::Key_F9: return { static_cast<int>(Key::F9), false };
        case Qt::Key_F10: return { static_cast<int>(Key::F10), false };
        case Qt::Key_F11: return { static_cast<int>(Key::F11), false };
        case Qt::Key_F12: return { static_cast<int>(Key::F12), false };
        default: return { INVALID_KEY, false };
        }
    }

    KeyMapping mapCharacter(char32_t c) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            bool isUpper = (c >= 'A' && c <= 'Z');
            char32_t lower = isUpper ? (c - 'A' + 'a') : c;
            int offset = lower - 'a';
            return { static_cast<int>(Key::A) + offset, isUpper };
        }
        if (c >= '0' && c <= '9') {
            int offset = c - '0';
            return { static_cast<int>(Key::Num0) + offset, false };
        }
        switch (c) {
        case ' ': return { static_cast<int>(Key::Space), false };
        case '-': return { static_cast<int>(Key::Minus), false };
        case '_': return { static_cast<int>(Key::Minus), true };
        case '=': return { static_cast<int>(Key::Equal), false };
        case '+': return { static_cast<int>(Key::Equal), true };
        case '[': return { static_cast<int>(Key::LeftBracket), false };
        case '{': return { static_cast<int>(Key::LeftBracket), true };
        case ']': return { static_cast<int>(Key::RightBracket), false };
        case '}': return { static_cast<int>(Key::RightBracket), true };
        case '\\': return { static_cast<int>(Key::Backslash), false };
        case '|': return { static_cast<int>(Key::Backslash), true };
        case ';': return { static_cast<int>(Key::Semicolon), false };
        case ':': return { static_cast<int>(Key::Semicolon), true };
        case '\'': return { static_cast<int>(Key::Apostrophe), false };
        case '"': return { static_cast<int>(Key::Apostrophe), true };
        case ',': return { static_cast<int>(Key::Comma), false };
        case '<': return { static_cast<int>(Key::Comma), true };
        case '.': return { static_cast<int>(Key::Period), false };
        case '>': return { static_cast<int>(Key::Period), true };
        case '/': return { static_cast<int>(Key::Slash), false };
        case '?': return { static_cast<int>(Key::Slash), true };
        case '`': return { static_cast<int>(Key::Grave), false };
        case '~': return { static_cast<int>(Key::Grave), true };
        case '!': return { static_cast<int>(Key::Num1), true };
        case '@': return { static_cast<int>(Key::Num2), true };
        case '#': return { static_cast<int>(Key::Num3), true };
        case '$': return { static_cast<int>(Key::Num4), true };
        case '%': return { static_cast<int>(Key::Num5), true };
        case '^': return { static_cast<int>(Key::Num6), true };
        case '&': return { static_cast<int>(Key::Num7), true };
        case '*': return { static_cast<int>(Key::Num8), true };
        case '(': return { static_cast<int>(Key::Num9), true };
        case ')': return { static_cast<int>(Key::Num0), true };
        case '\n':
        case '\r': return { static_cast<int>(Key::Enter), false };
        default: return { INVALID_KEY, false };
        }
    }

    bool sendMappedKey(int key, bool requiresShift, int modifiers) {
        if (key == INVALID_KEY) return false;
        const Key mappedKey = static_cast<Key>(key);
        const bool pressShift = requiresShift || (modifiers & Qt::ShiftModifier);
        const bool pressControl = modifiers & Qt::ControlModifier;
        const bool pressAlt = modifiers & Qt::AltModifier;
        const bool pressSuper = modifiers & Qt::MetaModifier;

        if (pressControl) {
            RemoteInputModule::SendInput(Key::LeftControl, InputEventType::PRESS);
        }
        if (pressAlt) {
            RemoteInputModule::SendInput(Key::LeftAlt, InputEventType::PRESS);
        }
        if (pressSuper) {
            RemoteInputModule::SendInput(Key::LeftSuper, InputEventType::PRESS);
        }
        if (pressShift) {
            RemoteInputModule::SendInput(Key::LeftShift, InputEventType::PRESS);
        }

        RemoteInputModule::SendInput(mappedKey, InputEventType::PRESS_AND_RELEASE);

        if (pressShift) {
            RemoteInputModule::SendInput(Key::LeftShift, InputEventType::RELEASE);
        }
        if (pressSuper) {
            RemoteInputModule::SendInput(Key::LeftSuper, InputEventType::RELEASE);
        }
        if (pressAlt) {
            RemoteInputModule::SendInput(Key::LeftAlt, InputEventType::RELEASE);
        }
        if (pressControl) {
            RemoteInputModule::SendInput(Key::LeftControl, InputEventType::RELEASE);
        }
        return true;
    }
}

std::string JsonEscapeString(const std::string& value)
{
    return nlohmann::json(value).dump();
}

void WriteBackendStateSnapshot(const bool connected, std::string peerId = {}, std::string peerName = {})
{
    std::string storageRoot;
    {
        std::lock_guard<std::mutex> lock(g_storageConfigMutex);
        storageRoot = g_storageRoot;
    }

    if (storageRoot.empty()) {
        return;
    }

    bool clipboardSyncEnabled = false;
    bool notificationSyncEnabled = false;
    bool remoteInputReady = false;
    try {
        std::lock_guard<std::mutex> lock(g_backendMutex);
        if (g_backendState == BackendState::Running) {
            clipboardSyncEnabled = (ModulesManager::GetModuleReference<ClipboardSyncModule>()->GetModuleState() == ModuleState::Enabled);
            notificationSyncEnabled = (ModulesManager::GetModuleReference<NotificationSyncModule>()->GetModuleState() == ModuleState::Enabled);
            remoteInputReady = (ModulesManager::GetModuleReference<RemoteInputModule>()->GetModuleState() == ModuleState::Enabled);
        }
    } catch (...) {}

    std::string mediaTitle;
    std::string mediaArtist;
    std::string mediaCollection;
    std::string mediaElapsed;
    bool mediaPlaying = false;
    double mediaPosition = 0.0;
    double mediaDuration = 0.0;
    int mediaVolume = 0;
    std::string mediaCoverPath;
    {
        std::lock_guard<std::mutex> lock(g_mediaMutex);
        mediaTitle = g_mediaTitle;
        mediaArtist = g_mediaArtist;
        mediaCollection = g_mediaCollection;
        mediaElapsed = g_mediaElapsed;
        mediaPlaying = g_mediaPlaying;
        mediaPosition = g_mediaPositionSeconds;
        mediaDuration = g_mediaDurationSeconds;
        mediaVolume = g_mediaVolume;
        mediaCoverPath = g_mediaCoverPath;
    }

    std::string downloadPath;
    std::string downloadPathStatus;
    {
        std::lock_guard<std::mutex> lock(g_downloadPathMutex);
        downloadPath = g_downloadPath;
        downloadPathStatus = g_downloadPathStatus;
    }

    std::string lastRemoteClipboard;
    {
        std::lock_guard<std::mutex> lock(g_clipboardMutex);
        lastRemoteClipboard = g_lastRemoteClipboard;
    }

    const std::filesystem::path statePath = std::filesystem::path(storageRoot) / "backend_state.json";
    const std::filesystem::path tempPath = std::filesystem::path(storageRoot) / "backend_state.json.tmp";

    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    file << "{"
         << "\"connected\":" << (connected ? "true" : "false") << ","
         << "\"peerId\":" << JsonEscapeString(peerId) << ","
         << "\"peerName\":" << JsonEscapeString(peerName) << ","
         << "\"clipboardSyncEnabled\":" << (clipboardSyncEnabled ? "true" : "false") << ","
         << "\"notificationSyncEnabled\":" << (notificationSyncEnabled ? "true" : "false") << ","
         << "\"remoteInputReady\":" << (remoteInputReady ? "true" : "false") << ","
         << "\"mediaTitle\":" << JsonEscapeString(mediaTitle) << ","
         << "\"mediaArtist\":" << JsonEscapeString(mediaArtist) << ","
         << "\"mediaCollection\":" << JsonEscapeString(mediaCollection) << ","
         << "\"mediaElapsed\":" << JsonEscapeString(mediaElapsed) << ","
         << "\"mediaPlaying\":" << (mediaPlaying ? "true" : "false") << ","
         << "\"mediaPosition\":" << mediaPosition << ","
         << "\"mediaDuration\":" << mediaDuration << ","
         << "\"mediaVolume\":" << mediaVolume << ","
         << "\"mediaCoverPath\":" << JsonEscapeString(mediaCoverPath) << ","
         << "\"downloadPath\":" << JsonEscapeString(downloadPath) << ","
         << "\"downloadPathStatus\":" << JsonEscapeString(downloadPathStatus) << ","
         << "\"lastRemoteClipboard\":" << JsonEscapeString(lastRemoteClipboard) << ","
         << "\"findMyPhoneAlertActive\":" << (QSettings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile")).value(QStringLiteral("findMyPhone/alertActive"), false).toBool() ? "true" : "false")
         << "}";
    file.close();

    std::error_code ec;
    std::filesystem::rename(tempPath, statePath, ec);
    if (ec) {
        std::filesystem::copy_file(tempPath, statePath, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tempPath, ec);
    }
}

void WriteCurrentBackendStateSnapshot()
{
    const bool connected = ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED;
    WriteBackendStateSnapshot(
        connected,
        connected ? boost::uuids::to_string(ConnectionManager::GetPeerUUID()) : std::string{},
        connected ? ConnectionManager::GetPeerDeviceName() : std::string{}
    );
}

extern "C" void UpdateLastRemoteClipboard(const std::string& text)
{
    {
        std::lock_guard<std::mutex> lock(g_clipboardMutex);
        g_lastRemoteClipboard = text;
    }
    WriteBackendStateSnapshot(ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED,
                              boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                              ConnectionManager::GetPeerDeviceName());
}

void SendAndroidPermissionSnapshotToPeer()
{
    if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED) {
        return;
    }

    auto sendStatus = [](const PermissionType type, const bool granted) {
        ConnectionManager::Send(
            granted ? PC_PackageType::PERMISSION_GRANTED : PC_PackageType::PERMISSION_REJECTED,
            type
        );
    };

    sendStatus(PermissionType::Camera, PermissionManager::IsCameraAccessPermissionGranted());
    sendStatus(PermissionType::Microphone, PermissionManager::IsMicrophoneAccessPermissionGranted());
    sendStatus(
        PermissionType::Notifications,
        PermissionManager::IsNotificationEmitPermissionGranted() &&
            PermissionManager::IsNotificationAccessPermissionGranted()
    );
    sendStatus(
        PermissionType::FileSystem,
        PermissionManager::IsFileAccessPermissionGranted() &&
            PermissionManager::IsManagingExternalStoragePermissionGranted()
    );
    sendStatus(PermissionType::Battery, PermissionManager::IsBatteryOptimizationIgnored());
    sendStatus(
        PermissionType::Sms,
        PermissionManager::IsReceiveSmsPermissionGranted() &&
            PermissionManager::IsReadContactsPermissionGranted() &&
            PermissionManager::IsReadSmsPermissionGranted() &&
            PermissionManager::IsSendSmsPermissionGranted()
    );
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

void PublishBackendEvent(const QEvent& event)
{
    if (event.type() == RemoteMediaInfoEvent::Type) {
        const auto& mediaEvent = static_cast<const RemoteMediaInfoEvent&>(event);
        {
            std::lock_guard<std::mutex> lock(g_mediaMutex);
            g_mediaTitle = mediaEvent.GetTitle();
            g_mediaArtist = mediaEvent.GetArtist();
            g_mediaCollection = mediaEvent.GetCollection();
            g_mediaElapsed = mediaEvent.GetElapsed();
            g_mediaPlaying = mediaEvent.IsPlaying();
            g_mediaPositionSeconds = mediaEvent.GetPositionSeconds();
            g_mediaDurationSeconds = mediaEvent.GetDurationSeconds();
            g_mediaVolume = mediaEvent.GetVolume();

            const auto& coverBytes = mediaEvent.GetCoverBytes();
            if (!coverBytes.empty()) {
                std::string storageRoot;
                {
                    std::lock_guard<std::mutex> storeLock(g_storageConfigMutex);
                    storageRoot = g_storageRoot;
                }
                if (!storageRoot.empty()) {
                    g_mediaCoverPath = storageRoot + "/libreconnect_remote_cover.jpg";
                    std::ofstream coverFile(g_mediaCoverPath, std::ios::binary | std::ios::trunc);
                    if (coverFile) {
                        coverFile.write(reinterpret_cast<const char*>(coverBytes.data()), coverBytes.size());
                        coverFile.close();
                    }
                }
            } else {
                g_mediaCoverPath.clear();
            }
        }
        WriteBackendStateSnapshot(ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED,
                                  boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                                  ConnectionManager::GetPeerDeviceName());
        return;
    }

    if (event.type() == ConnectionPendingEvent::Type) {
        const auto& pendingEvent = static_cast<const ConnectionPendingEvent&>(event);
        const DeviceInfo deviceInfo = pendingEvent.GetDeviceInfo();
        const std::string deviceId = boost::uuids::to_string(deviceInfo.deviceID);
        const std::string pairingCode = pendingEvent.GetPairingCode();
        const int connectionMode = static_cast<int>(pendingEvent.GetInitialConnectionMode());

        {
            std::lock_guard<std::mutex> lock(g_connectionPromptMutex);
            g_pendingConnectionPrompt.reset(pendingEvent.clone());
        }

        Debug::Log(
            "MainServiceBackendBridge: publishing connection pending event for {} ({})",
            deviceInfo.deviceName,
            deviceId
        );

        WithJniEnv([&](JNIEnv* env) {
            jobject context = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_jniStateMutex);
                context = g_serviceContextGlobal;
            }

            if (!context) {
                Debug::LogWarning("MainServiceBackendBridge: cannot publish connection pending event without service context");
                return;
            }

            jclass serviceClass = env->GetObjectClass(context);
            if (!serviceClass) {
                Debug::LogError("MainServiceBackendBridge: failed to resolve MainService for connection pending event");
                return;
            }

            jmethodID method = env->GetStaticMethodID(
                serviceClass,
                "publishBackendConnectionPending",
                "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)V"
            );
            if (!method) {
                env->ExceptionClear();
                env->DeleteLocalRef(serviceClass);
                Debug::LogError("MainServiceBackendBridge: failed to resolve publishBackendConnectionPending");
                return;
            }

            jstring deviceIdString = env->NewStringUTF(deviceId.c_str());
            jstring deviceNameString = env->NewStringUTF(deviceInfo.deviceName.c_str());
            jstring pairingCodeString = env->NewStringUTF(pairingCode.c_str());
            env->CallStaticVoidMethod(serviceClass, method, context, deviceIdString, deviceNameString, static_cast<jint>(connectionMode), pairingCodeString);
            env->DeleteLocalRef(pairingCodeString);
            env->DeleteLocalRef(deviceNameString);
            env->DeleteLocalRef(deviceIdString);
            env->DeleteLocalRef(serviceClass);

            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                Debug::LogError("MainServiceBackendBridge: exception publishing connection pending event");
            }
        });
        return;
    }

    if (event.type() == ConnectionApprovalRequestedEvent::Type) {
        const auto& approvalEvent = static_cast<const ConnectionApprovalRequestedEvent&>(event);
        const DeviceInfo deviceInfo = approvalEvent.GetDeviceInfo();
        const std::string deviceId = boost::uuids::to_string(deviceInfo.deviceID);

        {
            std::lock_guard<std::mutex> lock(g_connectionPromptMutex);
            g_pendingApprovalPrompt.reset(approvalEvent.clone());
        }

        Debug::Log(
            "MainServiceBackendBridge: publishing connection approval event for {} ({})",
            deviceInfo.deviceName,
            deviceId
        );

        WithJniEnv([&](JNIEnv* env) {
            jobject context = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_jniStateMutex);
                context = g_serviceContextGlobal;
            }

            if (!context) {
                Debug::LogWarning("MainServiceBackendBridge: cannot publish connection approval event without service context");
                return;
            }

            jclass serviceClass = env->GetObjectClass(context);
            if (!serviceClass) {
                Debug::LogError("MainServiceBackendBridge: failed to resolve MainService for connection approval event");
                return;
            }

            jmethodID method = env->GetStaticMethodID(
                serviceClass,
                "publishBackendConnectionApproval",
                "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V"
            );
            if (!method) {
                env->ExceptionClear();
                env->DeleteLocalRef(serviceClass);
                Debug::LogError("MainServiceBackendBridge: failed to resolve publishBackendConnectionApproval");
                return;
            }

            jstring deviceIdString = env->NewStringUTF(deviceId.c_str());
            jstring deviceNameString = env->NewStringUTF(deviceInfo.deviceName.c_str());
            env->CallStaticVoidMethod(serviceClass, method, context, deviceIdString, deviceNameString);
            env->DeleteLocalRef(deviceNameString);
            env->DeleteLocalRef(deviceIdString);
            env->DeleteLocalRef(serviceClass);

            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                Debug::LogError("MainServiceBackendBridge: exception publishing connection approval event");
            }
        });
        return;
    }

    if (event.type() == ConnectedEvent::Type) {
        const auto& connectedEvent = static_cast<const ConnectedEvent&>(event);
        if (connectedEvent.GetResult() == EventResult::SUCCESS) {
            try {
                auto& remoteInput = ModulesManager::GetModuleReference<RemoteInputModule>();
                remoteInput->Enable(true);
            } catch (...) {}
            WriteBackendStateSnapshot(
                true,
                boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                ConnectionManager::GetPeerDeviceName()
            );
            return;
        }
    }

    if (event.type() == DisconnectedEvent::Type || event.type() == ConnectedEvent::Type) {
        try {
            auto& remoteInput = ModulesManager::GetModuleReference<RemoteInputModule>();
            remoteInput->Disable(true);
        } catch (...) {}
        WriteBackendStateSnapshot(false);
    }
}

void RequestClipboardSync(std::string localClipboardText = {})
{
    StartBackendIfNeeded();
    Debug::Log("MainServiceBackendBridge: manual clipboard sync requested");
    auto& module = ModulesManager::GetModuleReference<ClipboardSyncModule>();
    if (localClipboardText.empty()) {
        module->RequestSyncWithPeer();
        return;
    }

    module->RequestSyncWithPeer(std::move(localClipboardText));
}

void SendLocalClipboard(std::string localClipboardText = {})
{
    StartBackendIfNeeded();
    Debug::Log("MainServiceBackendBridge: local clipboard send requested");
    auto& module = ModulesManager::GetModuleReference<ClipboardSyncModule>();
    if (localClipboardText.empty()) {
        module->SendLocalClipboard();
        return;
    }

    module->SendLocalClipboard(std::move(localClipboardText));
}

void PostSharedFile(std::string path)
{
    if (path.empty()) {
        Debug::LogWarning("MainServiceBackendBridge: shared file post skipped because path is empty");
        return;
    }

    StartBackendIfNeeded();
    Debug::Log("MainServiceBackendBridge: shared file post requested for {}", path);
    asio::co_spawn(ThreadPool::GetContext(), [path = std::move(path)]() -> asio::awaitable<void> {
        auto& module = ModulesManager::GetModuleReference<FileShareModule>();
        if (module->GetModuleState() != ModuleState::Enabled) {
            co_await module->EnableAwaitable(true);
        }

        if (module->GetModuleState() != ModuleState::Enabled) {
            Debug::LogWarning("MainServiceBackendBridge: shared file post skipped because file module is not enabled");
            co_return;
        }

        module->PostEntry(path, std::filesystem::path{}, true);
    }, asio::detached);
}

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
        WriteCurrentBackendStateSnapshot();
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
    ConnectionManager::Send(PC_PackageType::FIND_MY_PHONE_STOP_RINGING);
    WriteCurrentBackendStateSnapshot();
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
    {
        std::lock_guard<std::mutex> lock(g_backendMutex);
        if (g_backendState == BackendState::Running) {
            Debug::Log("MainServiceBackendBridge: backend already running");
            return;
        }

        if (g_backendState == BackendState::Starting) {
            Debug::Log("MainServiceBackendBridge: backend already starting");
            return;
        }

        if (g_backendState == BackendState::Stopping) {
            Debug::LogWarning("MainServiceBackendBridge: backend start skipped because shutdown is in progress");
            return;
        }

        g_backendState = BackendState::Starting;
    }

    Debug::Log("MainServiceBackendBridge: starting backend - start");
    try {
        ThreadPool::Start();
        ModulesManager::Initialize();
        ConnectionManager::StartAcceptingConnections();
        if (!g_findMyHandlersRegistered) {
            ConnectionManager::AddResponseHandler(PC_PackageType::FIND_MY_PHONE_START_RINGING, [](PC_Package&&) {
                StartFindMyPhoneAlertFromService();
            });
            ConnectionManager::AddResponseHandler(PC_PackageType::FIND_MY_PHONE_STOP_RINGING, [](PC_Package&&) {
                StopFindMyPhoneAlertFromService();
            });
            NotificationBridge::AddNotificationActionHandler("find_my_phone", "Stop", []() {
                StopFindMyPhoneAlertFromService();
            });
            g_findMyHandlersRegistered = true;
        }
        LanDeviceScanner::BeginScan();
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> lock(g_backendMutex);
        g_backendState = BackendState::Stopped;
        Debug::LogError("MainServiceBackendBridge: backend start failed: {}", exception.what());
        return;
    } catch (...) {
        std::lock_guard<std::mutex> lock(g_backendMutex);
        g_backendState = BackendState::Stopped;
        Debug::LogError("MainServiceBackendBridge: backend start failed: unknown exception");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_backendMutex);
        g_backendState = BackendState::Running;
    }
    WriteCurrentBackendStateSnapshot();

    {
        QSettings settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"));
        const bool mirroringEnabled = settings.value(QStringLiteral("mediaNotification/enabled"), true).toBool();
        RemoteInputModule::SetMirroringEnabled(mirroringEnabled);
        Debug::Log("MainServiceBackendBridge: restored mirroring enabled = {}", mirroringEnabled);
    }

    Debug::Log("MainServiceBackendBridge: starting backend - done");
}

void StopBackendIfNeeded()
{
    {
        std::lock_guard<std::mutex> lock(g_backendMutex);
        if (g_backendState == BackendState::Stopped) {
            Debug::Log("MainServiceBackendBridge: backend already stopped");
            return;
        }

        if (g_backendState == BackendState::Stopping) {
            Debug::Log("MainServiceBackendBridge: backend already stopping");
            return;
        }

        if (g_backendState == BackendState::Starting) {
            Debug::LogWarning("MainServiceBackendBridge: backend stop skipped because startup is in progress");
            return;
        }

        g_backendState = BackendState::Stopping;
    }

    Debug::Log("MainServiceBackendBridge: stopping backend - start");
    if (g_findMyHandlersRegistered) {
        ConnectionManager::RemoveResponseHandler(PC_PackageType::FIND_MY_PHONE_START_RINGING);
        ConnectionManager::RemoveResponseHandler(PC_PackageType::FIND_MY_PHONE_STOP_RINGING);
        g_findMyHandlersRegistered = false;
    }
    StopFindMyPhoneAlertFromService();
    LanDeviceScanner::EndScan();
    ConnectionManager::Disconnect();
    ModulesManager::Shutdown();
    ConnectionManager::StopAcceptingConnections();

    Debug::Log("MainServiceBackendBridge: posting shutdown fence");
    const auto shutdownFence = ThreadPool::PostFuture([] {});
    if (shutdownFence.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        Debug::LogWarning("MainServiceBackendBridge: shutdown fence timed out before ThreadPool::Stop");
    }

    Debug::Log("MainServiceBackendBridge: calling ThreadPool::Stop");
    ThreadPool::Stop();
    {
        std::lock_guard<std::mutex> lock(g_backendMutex);
        g_backendState = BackendState::Stopped;
    }
    WriteBackendStateSnapshot(false);
    Debug::Log("MainServiceBackendBridge: stopping backend - done");
}

void ConfigureStorage(const std::string& storageRootPath, const std::string& logRootPath)
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

    const QString storageRootQt = QString::fromStdString(storageRootPath);
    QDir::setCurrent(storageRootQt);

    const std::filesystem::path logRoot(logRootPath.empty() ? storageRootPath : logRootPath);
    std::filesystem::create_directories(logRoot, ec);

    const Debug::Settings settings{
        .rootPath = logRoot.string(),
        .applicationName = "LibreConnectNative",
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

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeConfigureStorage(
    JNIEnv* env,
    jobject,
    jstring storageRootPath,
    jstring logRootPath)
{
    ConfigureStorage(JStringToStdString(env, storageRootPath), JStringToStdString(env, logRootPath));
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeStartBackend(
    JNIEnv* env,
    jobject thiz)
{
    AndroidContextProvider::SetServiceContext(env, thiz);
    CacheFindMyPhoneJniState(env, thiz);
    EnsureBackendQtRuntime();
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeStopBackend(
    JNIEnv* env,
    jobject)
{
    StopBackendQtRuntime();
    {
        std::lock_guard<std::mutex> lock(g_jniStateMutex);
        ReleaseFindMyPhoneJniState(env);
    }
    AndroidContextProvider::ClearServiceContext(env);
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeRespondConnectionPending(
    JNIEnv* env,
    jobject,
    jboolean accepted,
    jstring challenge)
{
    std::string challengeValue = JStringToStdString(env, challenge);
    PostToBackendQt([accepted = accepted == JNI_TRUE, challengeValue = std::move(challengeValue)]() mutable {
        std::unique_ptr<ConnectionPendingEvent> pendingEvent;
        {
            std::lock_guard<std::mutex> lock(g_connectionPromptMutex);
            pendingEvent = std::move(g_pendingConnectionPrompt);
        }

        if (!pendingEvent) {
            Debug::LogWarning("MainServiceBackendBridge: connection pending response ignored because no prompt is pending");
            return;
        }

        Debug::Log("MainServiceBackendBridge: connection pending response received (accepted: {})", accepted);
        if (accepted) {
            pendingEvent->AcceptConnectionIfVerified(challengeValue);
        } else {
            pendingEvent->DenyConnection();
        }
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeRespondConnectionApproval(
    JNIEnv*,
    jobject,
    jboolean approved)
{
    PostToBackendQt([approved = approved == JNI_TRUE]() {
        std::unique_ptr<ConnectionApprovalRequestedEvent> approvalEvent;
        {
            std::lock_guard<std::mutex> lock(g_connectionPromptMutex);
            approvalEvent = std::move(g_pendingApprovalPrompt);
        }

        if (!approvalEvent) {
            Debug::LogWarning("MainServiceBackendBridge: connection approval response ignored because no prompt is pending");
            return;
        }

        Debug::Log("MainServiceBackendBridge: connection approval response received (approved: {})", approved);
        if (approved) {
            approvalEvent->AcceptConnection();
        } else {
            approvalEvent->DenyConnection();
        }
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeRequestClipboardSync(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([] {
        RequestClipboardSync();
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_ClipboardActionActivity_nativeRequestClipboardSync(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([] {
        RequestClipboardSync();
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_ClipboardSyncDispatcher_nativeRequestClipboardSyncWithText(
    JNIEnv* env,
    jclass,
    jstring clipboardText)
{
    PostToBackendQt([text = JStringToStdString(env, clipboardText)] {
        RequestClipboardSync(text);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeOnAudioCaptured(
    JNIEnv* env,
    jobject,
    jbyteArray samples)
{
    jsize len = env->GetArrayLength(samples);
    std::vector<uint8_t> pcm(len);
    env->GetByteArrayRegion(samples, 0, len, reinterpret_cast<jbyte*>(pcm.data()));

    PostToBackendQt([pcm = std::move(pcm)] {
        auto& module = ModulesManager::GetModuleReference<NetworkMicrophoneModule>();
        if (module->GetModuleState() == ModuleState::Enabled) {
            module->ProcessAndSendAudio(pcm);
        }
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_ClipboardSyncDispatcher_nativeSendClipboardWithText(
    JNIEnv* env,
    jclass,
    jstring clipboardText)
{
    PostToBackendQt([text = JStringToStdString(env, clipboardText)] {
        SendLocalClipboard(text);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_SharedFileDispatcher_nativePostSharedFile(
    JNIEnv* env,
    jclass,
    jstring path)
{
    PostToBackendQt([filePath = JStringToStdString(env, path)] {
        PostSharedFile(filePath);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeShareLogs(
    JNIEnv* env,
    jobject obj)
{
    jclass utilsClass = env->FindClass("com/LibreConnect/mobile/FileSystemUtils");
    if (!utilsClass) return;

    jmethodID shareMethod = env->GetStaticMethodID(utilsClass, "shareLogs", "(Landroid/content/Context;)V");
    if (!shareMethod) return;

    env->CallStaticVoidMethod(utilsClass, shareMethod, obj);
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeDisableCameraModule(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([] {
        {
            std::lock_guard<std::mutex> lock(g_backendMutex);
            if (g_backendState != BackendState::Running) {
                return;
            }
        }

        auto& module = ModulesManager::GetModuleReference<NetworkCameraModule>();
        module->Disable(true);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeDisableMicrophoneModule(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([] {
        {
            std::lock_guard<std::mutex> lock(g_backendMutex);
            if (g_backendState != BackendState::Running) {
                return;
            }
        }

        auto& module = ModulesManager::GetModuleReference<NetworkMicrophoneModule>();
        module->Disable(true);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeSetClipboardSyncEnabled(
    JNIEnv*,
    jobject,
    jboolean enabled)
{
    PostToBackendQt([enabled]() {
        auto& module = ModulesManager::GetModuleReference<ClipboardSyncModule>();
        if (enabled) {
            module->Enable(true);
        } else {
            module->Disable(true);
        }
        WriteBackendStateSnapshot(ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED,
                                  boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                                  ConnectionManager::GetPeerDeviceName());
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeSetNotificationSyncEnabled(
    JNIEnv*,
    jobject,
    jboolean enabled)
{
    PostToBackendQt([enabled]() {
        auto& module = ModulesManager::GetModuleReference<NotificationSyncModule>();
        if (enabled) {
            module->Enable(true);
        } else {
            module->Disable(true);
        }
        WriteBackendStateSnapshot(ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED,
                                  boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                                  ConnectionManager::GetPeerDeviceName());
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeSendMediaSignal(
    JNIEnv*,
    jobject,
    jint signal)
{
    PostToBackendQt([signal]() {
        RemoteInputModule::SendMediaInput(static_cast<MediaSignal>(signal));
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeMediaSeek(
    JNIEnv*,
    jobject,
    jdouble position)
{
    PostToBackendQt([position]() {
        RemoteInputModule::SetMediaPosition(position);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeMediaSetVolume(
    JNIEnv*,
    jobject,
    jint volume)
{
    PostToBackendQt([volume]() {
        RemoteInputModule::SetVolume(volume);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeSendKeyInput(
    JNIEnv* env,
    jobject,
    jint key,
    jstring text,
    jint modifiers)
{
    std::string textStr = JStringToStdString(env, text);
    PostToBackendQt([key, textStr = std::move(textStr), modifiers]() {
        KeyMapping special = mapQtSpecialKey(key);
        if (special.key != INVALID_KEY) {
            const bool shifted = special.requiresShift || (modifiers & Qt::ShiftModifier);
            sendMappedKey(special.key, shifted, modifiers);
            return;
        }

        if (textStr.empty()) {
            return;
        }

        QString qText = QString::fromStdString(textStr);
        for (int i = 0; i < qText.length(); ++i) {
            const QChar qc = qText.at(i);
            const KeyMapping mapping = mapCharacter(qc.unicode());
            if (mapping.key != INVALID_KEY) {
                sendMappedKey(mapping.key, mapping.requiresShift, modifiers);
            }
        }
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeRefreshDownloadPath(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([]() {
        if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED) {
            std::lock_guard<std::mutex> lock(g_downloadPathMutex);
            g_downloadPathStatus = "Connect to a desktop device to load the default download path.";
            WriteBackendStateSnapshot(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_downloadPathMutex);
            g_downloadPathStatus = "Loading default download path...";
        }
        WriteBackendStateSnapshot(true,
                                  boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                                  ConnectionManager::GetPeerDeviceName());

        asio::co_spawn(ThreadPool::GetContext(), []() -> asio::awaitable<void> {
            const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(
                PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_GET_REQUEST
            );

            std::string path;
            std::string status;
            if (response.has_value()) {
                path = response.value()->GetValue<std::string>();
                status = "Default download path loaded.";
            } else {
                status = "Could not load the desktop default download path.";
            }

            {
                std::lock_guard<std::mutex> lock(g_downloadPathMutex);
                if (!path.empty()) {
                    g_downloadPath = path;
                }
                g_downloadPathStatus = status;
            }
            WriteBackendStateSnapshot(ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED,
                                      boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                                      ConnectionManager::GetPeerDeviceName());
        }, asio::detached);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeSetDownloadPath(
    JNIEnv* env,
    jobject,
    jstring path)
{
    std::string pathStr = JStringToStdString(env, path);
    PostToBackendQt([pathStr = std::move(pathStr)]() {
        if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED) {
            std::lock_guard<std::mutex> lock(g_downloadPathMutex);
            g_downloadPathStatus = "Connect to a desktop device before changing the download path.";
            WriteBackendStateSnapshot(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_downloadPathMutex);
            g_downloadPathStatus = "Saving default download path...";
        }
        WriteBackendStateSnapshot(true,
                                  boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                                  ConnectionManager::GetPeerDeviceName());

        asio::co_spawn(ThreadPool::GetContext(), [pathStr]() -> asio::awaitable<void> {
            const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(
                PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_SET_REQUEST,
                pathStr
            );

            bool success = false;
            std::string resolvedPath = pathStr;
            std::string message = "Could not save the desktop default download path.";
            if (response.has_value()) {
                success = response.value()->GetValue<bool>();
                resolvedPath = response.value()->GetValue<std::string>();
                message = response.value()->GetValue<std::string>();
            }

            {
                std::lock_guard<std::mutex> lock(g_downloadPathMutex);
                if (success) {
                    g_downloadPath = resolvedPath;
                }
                g_downloadPathStatus = message;
            }
            WriteBackendStateSnapshot(ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED,
                                      boost::uuids::to_string(ConnectionManager::GetPeerUUID()),
                                      ConnectionManager::GetPeerDeviceName());
        }, asio::detached);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeDisconnect(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([]() {
        ConnectionManager::Disconnect();
        WriteBackendStateSnapshot(false);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeRemovePairedDevice(
    JNIEnv* env,
    jobject,
    jstring deviceId)
{
    std::string deviceIdStr = JStringToStdString(env, deviceId);
    PostToBackendQt([deviceIdStr = std::move(deviceIdStr)]() {
        if (deviceIdStr.empty()) {
            return;
        }

        const bool wasActivePeer =
            ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED &&
            boost::uuids::to_string(ConnectionManager::GetPeerUUID()) == deviceIdStr;

        const bool removed = ConnectionManager::RemovePairedDevice(deviceIdStr);
        if (wasActivePeer) {
            ConnectionManager::Disconnect();
            WriteBackendStateSnapshot(false);
        } else if (removed) {
            const bool connected = ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED;
            WriteBackendStateSnapshot(
                connected,
                connected ? boost::uuids::to_string(ConnectionManager::GetPeerUUID()) : std::string{},
                connected ? ConnectionManager::GetPeerDeviceName() : std::string{}
            );
        }
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeStopFindMyPhoneAlert(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([] {
        StopFindMyPhoneAlertFromService();
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeSyncPermissionSnapshot(
    JNIEnv*,
    jobject)
{
    PostToBackendQt([] {
        SendAndroidPermissionSnapshotToPeer();
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeSetMirroringEnabled(
    JNIEnv*,
    jobject,
    jboolean enabled)
{
    PostToBackendQt([enabled]() {
        RemoteInputModule::SetMirroringEnabled(enabled == JNI_TRUE);
        Debug::Log("MainServiceBackendBridge: mirroring enabled set to {}", enabled == JNI_TRUE);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeNotificationAction(
    JNIEnv* env,
    jobject,
    jstring key,
    jstring option)
{
    std::string keyStr = JStringToStdString(env, key);
    std::string optionStr = JStringToStdString(env, option);
    PostToBackendQt([keyStr = std::move(keyStr), optionStr = std::move(optionStr)]() {
        Debug::Log("MainServiceBackendBridge: notification action received: key={}, option={}", keyStr, optionStr);
        NotificationBridge::CallNotificationActionHandler(keyStr, optionStr);
    });
}
