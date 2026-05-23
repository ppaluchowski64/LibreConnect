#include <RemoteInputModule.h>
#include <ConnectionManager.h>
#include <RemoteInputEvents.h>
#include <MediaTrackInfo.h>
#include <asio/post.hpp>

#include <memory>
#include <vector>
#include <atomic>

constexpr size_t FUTURES_WAIT_DELAY = 10;

namespace {
    std::atomic<bool> g_mirroringEnabled{false};
}

void RemoteInputModule::SendInput(const Key key, const InputEventType type) {
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_SEND_INPUT, key, type);
}

void RemoteInputModule::SendMediaInput(const MediaSignal signal) {
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT, signal);
}

void RemoteInputModule::RequestMediaInfo() {
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_REQUEST_MEDIA_INFO);
}

void RemoteInputModule::SetMediaPosition(const double seconds) {
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION, seconds);
}

void RemoteInputModule::SendMediaInfoUpdate(
    const std::string& title,
    const std::string& artist,
    const std::string& collection,
    const std::string& elapsed,
    const bool playing,
    const double positionSeconds,
    const double durationSeconds,
    const std::vector<uint8_t>& coverBytes
) {
    ConnectionManager::Send(
        PC_PackageType::REMOTE_INPUT_MODULE_MEDIA_INFO_UPDATE,
        title,
        artist,
        collection,
        elapsed,
        playing,
        positionSeconds,
        durationSeconds,
        coverBytes
    );
}

void RemoteInputModule::SetMirroringEnabled(bool enabled) {
    g_mirroringEnabled.store(enabled);
}

bool RemoteInputModule::IsMirroringEnabled() {
    return g_mirroringEnabled.load();
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
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_MEDIA_INFO_UPDATE, [instance](PC_Package&& package) mutable {
        (void)instance;
        const std::string title = package->GetValue<std::string>();
        const std::string artist = package->GetValue<std::string>();
        const std::string collection = package->GetValue<std::string>();
        const std::string elapsed = package->GetValue<std::string>();
        const bool playing = package->GetValue<bool>();
        double positionSeconds = 0.0;
        double durationSeconds = 0.0;
        std::vector<uint8_t> coverBytes;

        // Keep compatibility with peers that still send the old 5-field payload.
        try {
            positionSeconds = package->GetValue<double>();
            durationSeconds = package->GetValue<double>();
            coverBytes = package->GetValue<std::vector<uint8_t>>();
        } catch (...) {}

        const std::unique_ptr<QEvent> event = std::make_unique<RemoteMediaInfoEvent>(
            title,
            artist,
            collection,
            elapsed,
            playing,
            positionSeconds,
            durationSeconds,
            std::move(coverBytes)
        );
        ConnectionManager::SendEvent(event);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT, [instance](PC_Package&& package) mutable {
        const MediaSignal key = package->GetValue<MediaSignal>();
        Debug::Log("Mobile RemoteInputModule: Received REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT package: key={}", static_cast<int>(key));
        instance->m_remote.ExecuteSignal(key);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION, [instance](PC_Package&& package) mutable {
        const double seconds = std::max(0.0, package->GetValue<double>());
        Debug::Log("Mobile RemoteInputModule: Received REMOTE_INPUT_MODULE_SET_MEDIA_POSITION package: position={:.2f}s", seconds);
        MediaTrackInfo::SetPosition(seconds);
    });
}

void RemoteInputModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_MEDIA_INFO_UPDATE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION);
}

void RemoteInputModule::OnInitialize() {}

asio::awaitable<void> RemoteInputModule::OnEnable() {
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED, true);

    const std::shared_ptr<RemoteInputModule> instance = std::static_pointer_cast<RemoteInputModule>(shared_from_this());
    MediaTrackInfo::SetTrackCallback([instance](const TrackMetadata& metadata) {
        const bool mirroring = IsMirroringEnabled();
        Debug::Log("Mobile RemoteInputModule: MediaTrackInfo track update callback triggered. title='{}', playing={}, mirroringEnabled={}",
                   metadata.title, metadata.playing, mirroring);
        if (!mirroring) {
            return;
        }
        asio::post(instance->m_context.get_executor(), [instance, metadata]() {
            Debug::Log("Mobile RemoteInputModule: Posting SendMediaInfoUpdate package to desktop");
            SendMediaInfoUpdate(
                metadata.title,
                metadata.artist,
                metadata.album,
                "", // elapsed
                metadata.playing,
                metadata.position,
                metadata.duration,
                metadata.cover
            );
        });
    });

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
    MediaTrackInfo::SetTrackCallback(nullptr);
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
