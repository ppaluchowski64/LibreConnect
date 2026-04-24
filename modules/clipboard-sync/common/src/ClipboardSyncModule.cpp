#include <ClipboardSyncModule.h>
#include <TextClipboard.h>

#include <algorithm>
#include <chrono>

namespace {
constexpr auto REMOTE_ECHO_SUPPRESSION_WINDOW = std::chrono::milliseconds(1500);
}

std::string ClipboardSyncModule::NormalizeClipboardText(std::string text) {
    text.erase(std::ranges::remove(text, '\r').begin(), text.end());
    return text;
}

void ClipboardSyncModule::SendClipboardText(std::string text) const {
    asio::co_spawn(m_clipboardModificationStrand, SendClipboardTextAwaitable(std::move(text)), asio::detached);
}

void ClipboardSyncModule::SendLocalClipboardSnapshot() const {
    SendClipboardText(TextClipboard::Get());
}

asio::awaitable<void> ClipboardSyncModule::SendClipboardTextAwaitable(std::string text) const {
    if (text.empty()) {
        co_return;
    }

    const std::string normalized = NormalizeClipboardText(text);
    const auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard lock(m_clipboardStateMutex);
        if (normalized == m_lastLocalClipboardSent) {
            co_return;
        }

        if (normalized == m_lastRemoteClipboardApplied &&
            (now - m_lastRemoteClipboardAppliedAt) <= REMOTE_ECHO_SUPPRESSION_WINDOW) {
            m_lastLocalClipboardSent = normalized;
            co_return;
            }

        m_lastLocalClipboardSent = normalized;
    }


    Debug::Log("ClipboardSyncModule: Sending local clipboard update ({} chars)", text.size());

    constexpr size_t MAX_FRAG_SIZE = MAX_PACKAGE_SIZE - 128;

    if (text.size() > MAX_PACKAGE_SIZE) {
        const size_t fragmentCount = std::ceil(static_cast<double>(text.size()) / MAX_FRAG_SIZE);

        Debug::Log("ClipboardSyncModule: Package too large, fragmentation is required (frag: {})", fragmentCount);
        co_await ConnectionManager::SendRequest(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_INFO_REQUEST, text.size(), fragmentCount);

        size_t offset{0};
        while (offset < text.size()) {
            const size_t fragmentSize = std::min(text.size() - offset, MAX_FRAG_SIZE);

            std::string fragment = text.substr(offset, fragmentSize);
            ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_FRAGMENTED_CLIPBOARD_UPDATE, offset, fragment);
            offset += fragmentSize;
        }

    } else {
        ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_CLIPBOARD, text);
    }

    co_await ConnectionManager::SendRequest(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_FINISHED_REQUEST);
}

void ClipboardSyncModule::RequestSyncWithPeer() const {
    SendLocalClipboardSnapshot();
    ConnectionManager::Send(PC_PackageType::CLIPBOARD_SYNC_MODULE_REQUEST_SYNC);
}

void ClipboardSyncModule::RequestSyncWithPeer(std::string localClipboardText) const {
    SendClipboardText(std::move(localClipboardText));
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

        asio::post(instance->m_clipboardModificationStrand, [instance, text]() {
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
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_REQUEST_SYNC, [instance](PC_Package&& package) mutable {
        (void)package;
        Debug::Log("ClipboardSyncModule: Received manual sync request");
        instance->SendLocalClipboardSnapshot();
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_INFO_REQUEST, [instance](PC_Package&& package) mutable {
        const size_t requestID = package->GetValue<size_t>();
        const size_t size = package->GetValue<size_t>();
        const size_t fragmentCount = package->GetValue<size_t>();

        instance->m_buffer.resize(size);
        instance->m_leftFragments.store(fragmentCount);

        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_INFO_RESPONSE);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_FRAGMENTED_CLIPBOARD_UPDATE, [instance](PC_Package&& package) mutable {
        const std::size_t offset = package->GetValue<std::size_t>();
        const std::string fragment = package->GetValue<std::string>();

        instance->m_leftFragments.fetch_add(-1);
        std::memcpy(&instance->m_buffer[offset], fragment.c_str(), fragment.size());
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_FINISHED_REQUEST, [instance](PC_Package&& package) mutable -> asio::awaitable<void> {
        const std::size_t requestID = package->GetValue<size_t>();
        asio::steady_timer timer(ThreadPool::GetContext().get_executor());

        while (instance->m_leftFragments.load() > 0) {
            timer.expires_after(std::chrono::milliseconds(10));
            co_await timer.async_wait();
        }

        asio::post(instance->m_clipboardModificationStrand, [instance]() {
            const std::string text = instance->m_buffer;
            const std::string normalized = NormalizeClipboardText(text);
            const auto now = std::chrono::steady_clock::now();

            {
                std::lock_guard lock(instance->m_clipboardStateMutex);
                instance->m_lastRemoteClipboardApplied = normalized;
                instance->m_lastRemoteClipboardAppliedAt = now;
                instance->m_lastLocalClipboardSent = normalized;
            }

            Debug::Log("ClipboardSyncModule: Received remote fragmented clipboard update ({} chars)", text.size());
            TextClipboard::Set(text);
        });

        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_FINISHED_RESPONSE);
    });
}

void ClipboardSyncModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_STATE_CHANGE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_CLIPBOARD);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_REQUEST_SYNC);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_FINISHED_REQUEST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::CLIPBOARD_SYNC_MODULE_UPDATE_INFO_REQUEST);
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
