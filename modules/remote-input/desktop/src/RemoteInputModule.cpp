#include <RemoteInputModule.h>
#include <ConnectionManager.h>
#include <MediaTrackInfo.h>

#include <asio/post.hpp>

#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef MACOS_DEVICE
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace {
constexpr size_t FUTURES_WAIT_DELAY = 10;
constexpr size_t MAX_MEDIA_COVER_BYTES = 512 * 1024;
constexpr auto PERMISSION_STATE_POLL_INTERVAL = std::chrono::milliseconds(500);

bool IsInputDeliveryState(const ModuleState state) {
    return state == ModuleState::Enabled || state == ModuleState::Enabling;
}

struct MediaInfoSnapshot {
    std::string title;
    std::string artist;
    std::string collection;
    std::string elapsed;
    bool playing = false;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    std::vector<uint8_t> coverBytes;
};

std::string FormatElapsedTime(const long long elapsedSeconds) {
    const long long safeSeconds = std::max(0LL, elapsedSeconds);
    const long long hours = safeSeconds / 3600;
    const long long minutes = (safeSeconds % 3600) / 60;
    const long long seconds = safeSeconds % 60;

    std::ostringstream stream;
    stream << std::setfill('0');
    if (hours > 0) {
        stream << hours << ':' << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
        return stream.str();
    }

    stream << minutes << ':' << std::setw(2) << seconds;
    return stream.str();
}

MediaInfoSnapshot GetMediaInfoSnapshot() {
    if (const auto track = MediaTrackInfo::GetCurrentTrack(); track.has_value()) {
        MediaInfoSnapshot snapshot;
        snapshot.title = track->title;
        snapshot.artist = track->artist;
        snapshot.collection = track->album;
        snapshot.playing = track->playing;
        snapshot.positionSeconds = std::max(0.0, track->position);
        snapshot.durationSeconds = std::max(0.0, track->duration);

        if (snapshot.positionSeconds > 0.0) {
            snapshot.elapsed = FormatElapsedTime(static_cast<long long>(snapshot.positionSeconds));
        }

        if (!track->cover.empty() && track->cover.size() <= MAX_MEDIA_COVER_BYTES) {
            snapshot.coverBytes = track->cover;
        }

        return snapshot;
    }

    return {};
}

void SendMediaInfoSnapshot(const MediaInfoSnapshot& snapshot) {
    std::vector<uint8_t> coverPayload = snapshot.coverBytes;

    ConnectionManager::Send(
        PC_PackageType::REMOTE_INPUT_MODULE_MEDIA_INFO_UPDATE,
        snapshot.title,
        snapshot.artist,
        snapshot.collection,
        snapshot.elapsed,
        snapshot.playing,
        snapshot.positionSeconds,
        snapshot.durationSeconds,
        std::move(coverPayload)
    );
}

#ifdef MACOS_DEVICE
bool IsAccessibilityTrusted(const bool promptForPermission) {
    const void* keys[] = { kAXTrustedCheckOptionPrompt };
    const void* values[] = { promptForPermission ? kCFBooleanTrue : kCFBooleanFalse };
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    const bool trusted = AXIsProcessTrustedWithOptions(options);
    if (options != nullptr) {
        CFRelease(options);
    }

    return trusted;
}

void ReportRemoteInputPermissionState(const bool granted, const bool requestStarted, const bool forceEmitState) {
    static std::optional<bool> s_lastReportedState = std::nullopt;

    const bool stateChanged = !s_lastReportedState.has_value() || s_lastReportedState.value() != granted;
    if (!stateChanged && !forceEmitState) {
        return;
    }

    if (!granted) {
        if (requestStarted) {
            ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::Accessibility);
        }
        ConnectionManager::Send(PC_PackageType::PERMISSION_REJECTED, PermissionType::Accessibility);
    } else {
        ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::Accessibility);
    }

    s_lastReportedState = granted;
}

bool EnsureRemoteInputPermission(const bool promptForPermission, const bool forceEmitState = false) {
    const bool trusted = IsAccessibilityTrusted(promptForPermission);
    ReportRemoteInputPermissionState(trusted, promptForPermission && !trusted, forceEmitState);
    return trusted;
}
#else
bool EnsureRemoteInputPermission(const bool promptForPermission, const bool forceEmitState = false) {
    (void)promptForPermission;
    (void)forceEmitState;
    return true;
}
#endif
}

void RemoteInputModule::EnableResponseCallbacks() {
    const std::shared_ptr<RemoteInputModule> instance = std::static_pointer_cast<RemoteInputModule>(shared_from_this());

    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("RemoteInputModule: Received enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            Debug::Log("RemoteInputModule: Already enabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED, true);
            return;
        }
        instance->Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_DISABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("RemoteSyncModule: Received disable request");
        if (instance->GetModuleState() == ModuleState::Disabled) {
            Debug::Log("RemoteSyncModule: Already disabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED, false);
            return;
        }
        instance->Disable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED, [instance](PC_Package&& package) mutable {
        const bool peerEnabled = package->GetValue<bool>();
        Debug::Log("RemoteInputModule: Peer module state changed: {}", peerEnabled);
        instance->m_peerModuleEnabled.store(peerEnabled);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_INPUT, [instance](PC_Package&& package) mutable {
        if (!IsInputDeliveryState(instance->GetModuleState())) {
            return;
        }

        if (!EnsureRemoteInputPermission(false)) {
            return;
        }

        const Key key = package->GetValue<Key>();
        const InputEventType type = package->GetValue<InputEventType>();

        asio::post(instance->m_moduleStrand, [instance, key, type]() {
            switch (type) {
            case InputEventType::PRESS:
                instance->m_keyboard.PressKey(key);
                break;

            case InputEventType::RELEASE:
                instance->m_keyboard.ReleaseKey(key);
                break;

            case InputEventType::PRESS_AND_RELEASE:
                instance->m_keyboard.PressAndReleaseKey(key);
                break;
            }
        });
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT, [instance](PC_Package&& package) mutable {
        if (!IsInputDeliveryState(instance->GetModuleState())) {
            return;
        }

        if (!EnsureRemoteInputPermission(false)) {
            return;
        }

        const MediaSignal key = package->GetValue<MediaSignal>();
        asio::post(instance->m_moduleStrand, [instance, key]() {
            instance->m_remote.ExecuteSignal(key);
        });
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_REQUEST_MEDIA_INFO, [instance](PC_Package&& package) mutable {
        (void)package;
        if (!IsInputDeliveryState(instance->GetModuleState())) {
            return;
        }

        const MediaInfoSnapshot snapshot = GetMediaInfoSnapshot();
        SendMediaInfoSnapshot(snapshot);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION, [instance](PC_Package&& package) mutable {
        if (!IsInputDeliveryState(instance->GetModuleState())) {
            return;
        }

        if (!EnsureRemoteInputPermission(false)) {
            return;
        }

        const double seconds = std::max(0.0, package->GetValue<double>());
        asio::post(instance->m_moduleStrand, [seconds]() {
            MediaTrackInfo::SetPosition(seconds);
        });
    });
}

void RemoteInputModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_INPUT);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_REQUEST_MEDIA_INFO);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION);
}

void RemoteInputModule::OnInitialize() {}

asio::awaitable<void> RemoteInputModule::OnEnable() {
    if (!EnsureRemoteInputPermission(true, true)) {
        Debug::LogWarning("RemoteInputModule: Accessibility permission not granted; remote input delivery is blocked.");
    }

    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED, true);

    asio::steady_timer timer(m_context.get_executor());
    auto nextPermissionRefresh = std::chrono::steady_clock::now() + PERMISSION_STATE_POLL_INTERVAL;
    while (!ShouldAbortEnable()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        if (std::chrono::steady_clock::now() >= nextPermissionRefresh) {
            EnsureRemoteInputPermission(false);
            nextPermissionRefresh = std::chrono::steady_clock::now() + PERMISSION_STATE_POLL_INTERVAL;
        }

        timer.expires_after(std::chrono::milliseconds(FUTURES_WAIT_DELAY));
        co_await timer.async_wait();
    }

    co_return;
}

asio::awaitable<void> RemoteInputModule::OnDisable() {
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_DISABLE);
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED, false);
    co_return;
}

asio::awaitable<void> RemoteInputModule::OnShutdown() {
    co_return;
}

const char* RemoteInputModule::GetModuleName() const {
    return "RemoteInputModule";
}

ModuleType RemoteInputModule::GetModuleType() const {
    return ModuleType::RemoteInput;
}
