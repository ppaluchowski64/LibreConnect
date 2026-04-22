#include <RemoteInputModule.h>
#include <ConnectionManager.h>

#include <asio/post.hpp>

#include <algorithm>
#include <mutex>
#include <optional>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef MACOS_DEVICE
#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef _WIN32
#if defined(__has_include)
    #if __has_include(<winrt/base.h>) && __has_include(<winrt/Windows.Media.Control.h>)
        #include <winrt/base.h>
        #include <winrt/Windows.Foundation.h>
        #include <winrt/Windows.Media.Control.h>
        #define LIBRECONNECT_HAS_WINRT_MEDIA_INFO 1
    #else
        #define LIBRECONNECT_HAS_WINRT_MEDIA_INFO 0
    #endif
#else
    #define LIBRECONNECT_HAS_WINRT_MEDIA_INFO 0
#endif
#endif

namespace {
constexpr size_t FUTURES_WAIT_DELAY = 10;

bool IsInputDeliveryState(const ModuleState state) {
    return state == ModuleState::Enabled || state == ModuleState::Enabling;
}

struct MediaInfoSnapshot {
    std::string title;
    std::string artist;
    std::string collection;
    std::string elapsed;
    bool playing = false;
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

#ifdef _WIN32
std::optional<MediaInfoSnapshot> GetWindowsMediaInfo() {
#if LIBRECONNECT_HAS_WINRT_MEDIA_INFO
    static std::once_flag apartmentInitFlag;
    std::call_once(apartmentInitFlag, []() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    });

    try {
        using namespace winrt::Windows::Media::Control;
        const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (!manager) {
            return std::nullopt;
        }

        const auto session = manager.GetCurrentSession();
        if (!session) {
            return std::nullopt;
        }

        MediaInfoSnapshot snapshot;

        const auto mediaProperties = session.TryGetMediaPropertiesAsync().get();
        if (mediaProperties) {
            snapshot.title = winrt::to_string(mediaProperties.Title());
            snapshot.artist = winrt::to_string(mediaProperties.Artist());
            snapshot.collection = winrt::to_string(mediaProperties.AlbumTitle());
        }

        const auto timeline = session.GetTimelineProperties();
        const long long elapsedSeconds = timeline.Position().count() / 10000000;
        if (elapsedSeconds > 0) {
            snapshot.elapsed = FormatElapsedTime(elapsedSeconds);
        }

        const auto playback = session.GetPlaybackInfo();
        if (playback) {
            snapshot.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        }

        return snapshot;
    } catch (...) {
        return std::nullopt;
    }
#else
    return std::nullopt;
#endif
}
#endif

MediaInfoSnapshot GetMediaInfoSnapshot() {
#ifdef _WIN32
    if (const auto info = GetWindowsMediaInfo(); info.has_value()) {
        return info.value();
    }
#endif

    return {};
}

void SendMediaInfoSnapshot(const MediaInfoSnapshot& snapshot) {
    ConnectionManager::Send(
        PC_PackageType::REMOTE_INPUT_MODULE_MEDIA_INFO_UPDATE,
        snapshot.title,
        snapshot.artist,
        snapshot.collection,
        snapshot.elapsed,
        snapshot.playing
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
}

void RemoteInputModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_INPUT);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_REQUEST_MEDIA_INFO);
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
