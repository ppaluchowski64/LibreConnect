#include <ClipboardSyncModule.h>
#include <TextClipboard.h>

#include <algorithm>
#include <chrono>

namespace {
constexpr auto REMOTE_ECHO_SUPPRESSION_WINDOW = std::chrono::milliseconds(1500);
}

std::string ClipboardSyncModule::NormalizeClipboardText(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

void ClipboardSyncModule::SendLocalClipboardSnapshot() const {
    const std::string text = TextClipboard::Get();
    if (text.empty()) {
        return;
    }

    const std::string normalized = NormalizeClipboardText(text);
    const auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard lock(m_clipboardStateMutex);
        if (normalized == m_lastLocalClipboardSent) {
            return;
        }

        if (normalized == m_lastRemoteClipboardApplied &&
            (now - m_lastRemoteClipboardAppliedAt) <= REMOTE_ECHO_SUPPRESSION_WINDOW) {
            m_lastLocalClipboardSent = normalized;
            return;
        }

        m_lastLocalClipboardSent = normalized;
    }

    Debug::Log("ClipboardSyncModule: Sending local clipboard update ({} chars)", text.size());
    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_CLIPBOARD, text);
}

void ClipboardSyncModule::RequestSyncWithPeer() {
    SendLocalClipboardSnapshot();
    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_REQUEST_SYNC);
}

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
        const std::string normalized = NormalizeClipboardText(text);
        const auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard lock(instance->m_clipboardStateMutex);
            instance->m_lastRemoteClipboardApplied = normalized;
            instance->m_lastRemoteClipboardAppliedAt = now;
            instance->m_lastLocalClipboardSent = normalized;
        }

        Debug::Log("ClipboardSyncModule: Received remote clipboard update ({} chars)", text.size());
        TextClipboard::Set(text);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_REQUEST_SYNC, [instance](PC_Package&& package) mutable {
        (void)package;
        Debug::Log("ClipboardSyncModule: Received manual sync request");
        instance->SendLocalClipboardSnapshot();
    });
}

void ClipboardSyncModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_CLIPBOARD);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_REQUEST_SYNC);
}

void ClipboardSyncModule::OnInitialize() {}

asio::awaitable<void> ClipboardSyncModule::OnEnable() {
    m_peerModuleEnabled.store(false);
    {
        std::lock_guard lock(m_clipboardStateMutex);
        m_lastLocalClipboardSent.clear();
        m_lastRemoteClipboardApplied.clear();
        m_lastRemoteClipboardAppliedAt = std::chrono::steady_clock::time_point{};
    }

    {
        std::weak_ptr weakPtr = std::dynamic_pointer_cast<ClipboardSyncModule>(shared_from_this());
        TextClipboard::AddClipboardUpdateListener([weakPtr = std::move(weakPtr)]() mutable {
            if (const auto shared = weakPtr.lock()) {
                if (shared->GetModuleState() != ModuleState::Enabled) {
                    return;
                }

                shared->SendLocalClipboardSnapshot();
            }
        });
    }

    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE, true);
    co_return;
}

asio::awaitable<void> ClipboardSyncModule::OnDisable() {
    TextClipboard::RemoveClipboardUpdateListener();
    m_peerModuleEnabled.store(false);
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
