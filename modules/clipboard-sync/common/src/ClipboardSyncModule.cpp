#include <ClipboardSyncModule.h>
#include <TextClipboard.h>

constexpr size_t FUTURES_WAIT_DELAY = 10;

void ClipboardSyncModule::EnableResponseCallbacks() {
    const std::shared_ptr<ClipboardSyncModule> instance = std::static_pointer_cast<ClipboardSyncModule>(shared_from_this());

    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("ClipboardSyncModule: Received enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            Debug::Log("ClipboardSyncModule: Already enabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE, true);
            return;
        }
        instance->Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_DISABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("ClipboardSyncModule: Received disable request");
        if (instance->GetModuleState() == ModuleState::Disabled) {
            Debug::Log("ClipboardSyncModule: Already disabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE, false);
            return;
        }
        instance->Disable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE, [instance](PC_Package&& package) mutable {
        const bool peerEnabled = package->GetValue<bool>();
        Debug::Log("ClipboardSyncModule: Peer module state changed: {}", peerEnabled);
        instance->m_peerModuleEnabled.store(peerEnabled);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_CLIPBOARD, [instance](PC_Package&& package) mutable {
        const std::string text = package->GetValue<std::string>();
        Debug::Log("ClipboardSyncModule: Received remote clipboard update ({} chars)", text.size());
        TextClipboard::Set(text);
    });
}

void ClipboardSyncModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NOTIFICATION_SYNC_MODULE_STATE_CHANGE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_CLIPBOARD);
}

void ClipboardSyncModule::OnInitialize() {}

asio::awaitable<void> ClipboardSyncModule::OnEnable() {
    {
        std::weak_ptr weakPtr = std::dynamic_pointer_cast<ClipboardSyncModule>(shared_from_this());
        TextClipboard::AddClipboardUpdateListener([weakPtr = std::move(weakPtr)]() mutable {
            if (const auto shared = weakPtr.lock()) {
                if (shared->GetModuleState() != ModuleState::Enabled) {
                    return;
                }

                if (!TextClipboard::Has()) {
                    return;
                }

                std::string text = TextClipboard::Get();
                Debug::Log("ClipboardSyncModule: Sending local clipboard update ({} chars)", text.size());
                ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_CLIPBOARD, std::move(text));
            }
        });
    }

    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE, true);

    asio::steady_timer timer(m_context.get_executor());
    while (!ShouldAbortEnable()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(FUTURES_WAIT_DELAY));
        co_await timer.async_wait();
    }
}

asio::awaitable<void> ClipboardSyncModule::OnDisable() {
    TextClipboard::RemoveClipboardUpdateListener();
    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_DISABLE);
    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE, false);
    co_return;
}

asio::awaitable<void> ClipboardSyncModule::OnShutdown() {
    co_return;
}

const char* ClipboardSyncModule::GetModuleName() const {
    return "ClipboardSyncModule";
}

ModuleType ClipboardSyncModule::GetModuleType() const {
    return ModuleType::ClipboardSync;
}
