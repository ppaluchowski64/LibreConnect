#include <RemoteInputModule.h>
#include <ConnectionManager.h>

constexpr size_t FUTURES_WAIT_DELAY = 10;

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
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE, [instance](PC_Package&& package) mutable {
        const bool peerEnabled = package->GetValue<bool>();
        Debug::Log("RemoteInputModule: Peer module state changed: {}", peerEnabled);
        instance->m_peerModuleEnabled.store(peerEnabled);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_INPUT, [instance](PC_Package&& package) mutable {
        if (instance->GetModuleState() != ModuleState::Enabled) {
            return;
        }

        const Key key = package->GetValue<Key>();
        const InputEventType type = package->GetValue<InputEventType>();

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
    ConnectionManager::AddResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT, [instance](PC_Package&& package) mutable {
        if (instance->GetModuleState() != ModuleState::Enabled) {
            return;
        }

        const MediaSignal key = package->GetValue<MediaSignal>();
        instance->m_remote.ExecuteSignal(key);
    });
}

void RemoteInputModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_INPUT);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT);
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
