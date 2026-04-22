#include <RemoteInputModule.h>
#include <ConnectionManager.h>
#include <MediaTrackInfo.h>

#include <asio/post.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef MACOS_DEVICE
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace {
constexpr size_t FUTURES_WAIT_DELAY = 10;
constexpr size_t MAX_MEDIA_COVER_BYTES = 512 * 1024;

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
#ifdef _WIN32
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
#endif

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

bool EnsureRemoteInputPermission() {
    static bool s_reportedGranted = false;

    const bool trusted = IsAccessibilityTrusted(true);
    if (!trusted) {
        s_reportedGranted = false;
        ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::Accessibility);
        ConnectionManager::Send(PC_PackageType::PERMISSION_REJECTED, PermissionType::Accessibility);
        return false;
    }

    if (!s_reportedGranted) {
        s_reportedGranted = true;
        ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::Accessibility);
    }

    return true;
}
#else
bool EnsureRemoteInputPermission() {
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

        if (!EnsureRemoteInputPermission()) {
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

        if (!EnsureRemoteInputPermission()) {
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

        if (!EnsureRemoteInputPermission()) {
            return;
        }

        const double seconds = std::max(0.0, package->GetValue<double>());
        asio::post(instance->m_moduleStrand, [seconds]() {
#ifdef _WIN32
            MediaTrackInfo::SetPosition(seconds);
#else
            (void)seconds;
#endif
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
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED, true);

    asio::steady_timer timer(m_context.get_executor());
    while (!ShouldAbortEnable()) {
        if (ShouldAbortEnable()) {
            co_return;
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
