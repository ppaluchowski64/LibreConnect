#include <SmsBridgeModule.h>
#include <SmsBridgeEvents.h>
#include <TransferChannelPool.h>
#include <FileSystemManager.h>

#include <boost/uuid/uuid_generators.hpp>
#include <cstdint>

constexpr size_t FUTURES_WAIT_DELAY = 10;
constexpr const char* MMS_CONTENT_TEMP_CATEGORY = "mms-content";

namespace {
    std::filesystem::path EnsureFileShareTempRoot()
    {
        return FileSystemManager::GetTemporaryStoragePath(MMS_CONTENT_TEMP_CATEGORY);
    }


    std::filesystem::path EnsureFileShareTempCategoryPath(const std::string& category)
    {
        const std::filesystem::path root = EnsureFileShareTempRoot();
        if (root.empty()) {
            return {};
        }

        const std::filesystem::path categoryPath = root / std::filesystem::u8path(category);
        std::error_code ec;
        std::filesystem::create_directories(categoryPath, ec);
        if (ec) {
            return {};
        }

        return categoryPath;
    }

    std::filesystem::path CreateFileShareTempSessionDirectory(const std::string& category)
    {
        const std::filesystem::path categoryPath = EnsureFileShareTempCategoryPath(category);
        if (categoryPath.empty()) {
            return {};
        }

        const std::filesystem::path sessionPath = categoryPath / boost::uuids::to_string(ConnectionManager::GetPeerUUID());
        std::error_code ec;
        std::filesystem::create_directories(sessionPath, ec);
        if (ec) {
            return {};
        }

        return sessionPath;
    }
}

uuid SmsBridgeModule::SendSMS(const std::string& target, const std::string& message) const {
    Debug::Log("SmsBridgeModule: SendSMS requested. Target: {}", target);
    const uuid messageUUID = boost::uuids::random_generator()();
    asio::co_spawn(m_context, SendSMSAwaitable(target, message, messageUUID), asio::detached);
    return messageUUID;
}

void SmsBridgeModule::GetContactList() const {
    Debug::Log("SmsBridgeModule: GetContactList requested.");
    asio::co_spawn(m_context, GetContactListAwaitable(), asio::detached);
}

void SmsBridgeModule::GetTargetMessages(const std::string& target) const {
    Debug::Log("SmsBridgeModule: GetTargetMessages requested. Target: {}", target);
    asio::co_spawn(m_context, GetTargetMessagesAwaitable(target), asio::detached);
}

void SmsBridgeModule::FetchMMSContent(const std::string& target) const {
    Debug::Log("SmsBridgeModule: FetchMMSContent for mms {} requested.", target);
    asio::co_spawn(m_context, FetchMMSContentAwaitable(target), asio::detached);
}

std::optional<std::filesystem::path> SmsBridgeModule::GetMMSContentPath(const std::string& target) const {
    const std::filesystem::path sessionDirectory = CreateFileShareTempSessionDirectory(MMS_CONTENT_TEMP_CATEGORY);
    if (sessionDirectory.empty()) {
        return std::nullopt;
    }

    const std::string targetHash = std::to_string(std::hash<std::string>{}(target));
    const std::filesystem::path hashedDir = sessionDirectory / targetHash;

    if (std::filesystem::exists(hashedDir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(hashedDir, ec)) {
            if (!ec && entry.is_regular_file()) {
                return entry.path();
            }
        }
    }

    return std::nullopt;
}

asio::awaitable<void> SmsBridgeModule::FetchMMSContentAwaitable(std::string target) const {
    const std::filesystem::path sessionDirectory = CreateFileShareTempSessionDirectory(MMS_CONTENT_TEMP_CATEGORY);
    if (sessionDirectory.empty()) {
        Debug::LogError("SmsBridgeModule: FetchMMSContentAwaitable failed to create destination directory. Target: {}", target);
        const std::unique_ptr<QEvent> event = std::make_unique<MMSContentReceivedEvent>(target, std::filesystem::path{});
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const std::string targetHash = std::to_string(std::hash<std::string>{}(target));
    const std::filesystem::path hashedDir = sessionDirectory / targetHash;

    if (std::filesystem::exists(hashedDir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(hashedDir, ec)) {
            if (!ec && entry.is_regular_file()) {
                Debug::Log("SmsBridgeModule: Found cached MMS attachment: {}", entry.path().string());
                const std::unique_ptr<QEvent> event = std::make_unique<MMSContentReceivedEvent>(target, entry.path());
                ConnectionManager::SendEvent(event);
                co_return;
            }
        }
    }

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::SMS_BRIDGE_MODULE_MMS_FILE_CONTENT_REQUEST, target);
    if (!response.has_value()) {
        Debug::LogError("SmsBridgeModule: FetchMMSContentAwaitable failed (timeout). Target: {}", target);
        ProcessError(ModuleFailReason::Timeout);
        const std::unique_ptr<QEvent> event = std::make_unique<MMSContentReceivedEvent>(target, std::filesystem::path{});
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const bool success = response.value()->GetValue<bool>();
    const size_t index = response.value()->GetValue<size_t>();
    const std::string fileName = response.value()->GetValue<std::string>();
    if (!success || fileName.empty()) {
        Debug::LogError("SmsBridgeModule: FetchMMSContentAwaitable failed. Target: {}", target);
        const std::unique_ptr<QEvent> event = std::make_unique<MMSContentReceivedEvent>(target, std::filesystem::path{});
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const auto opt = TransferChannelPool::Get(index);
    std::filesystem::path destinationFile;

    if (opt.has_value()) {
        const auto& channel = opt.value();
        std::error_code ec;
        std::filesystem::create_directories(hashedDir, ec);
        destinationFile = hashedDir / std::filesystem::path(fileName).filename();
        co_await channel->ReceiveFile(destinationFile);
    } else {
        Debug::LogError("FileShareModule: Transfer channel {} doesn't exists", index);
        ProcessError(ModuleFailReason::InternalError);
        const std::unique_ptr<QEvent> event = std::make_unique<MMSContentReceivedEvent>(target, std::filesystem::path{});
        ConnectionManager::SendEvent(event);
        ConnectionManager::Disconnect();
        co_return;
    }

    if (!std::filesystem::exists(destinationFile)) {
        Debug::LogError("SmsBridgeModule: MMS content transfer did not create destination file. Target: {}", target);
        destinationFile.clear();
    }

    const std::unique_ptr<QEvent> event = std::make_unique<MMSContentReceivedEvent>(target, destinationFile);
    ConnectionManager::SendEvent(event);
}

asio::awaitable<void> SmsBridgeModule::SendSMSAwaitable(std::string target, std::string message, uuid messageUUID) const {
    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::SMS_BRIDGE_MODULE_SEND_SMS_REQUEST, std::string(target), std::string(message));
    if (!response.has_value()) {
        Debug::LogError("SmsBridgeModule: SendSMS failed (timeout). Target: {}", target);
        ProcessError(ModuleFailReason::Timeout);
        co_return;
    }

    const bool result = response.value()->GetValue<bool>();
    Debug::Log("SmsBridgeModule: SendSMS response received. Target: {}, Success: {}", target, result);

    const std::unique_ptr<QEvent> event = std::make_unique<SendSmsResultEvent>(result, messageUUID);
    ConnectionManager::SendEvent(event);
}

asio::awaitable<void> SmsBridgeModule::GetContactListAwaitable() const {
    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_CONTACTS_REQUEST);
    if (!response.has_value()) {
        Debug::LogError("SmsBridgeModule: GetContactList failed (timeout).");
        ProcessError(ModuleFailReason::Timeout);
        co_return;
    }

    std::vector<std::pair<std::string, std::string>> contacts;
    response.value()->GetValue(contacts);
    Debug::Log("SmsBridgeModule: GetContactList response received. Contacts count: {}", contacts.size());

    const std::unique_ptr<QEvent> event = std::make_unique<FetchContactListResultEvent>(contacts);
    ConnectionManager::SendEvent(event);
}

asio::awaitable<void> SmsBridgeModule::GetTargetMessagesAwaitable(std::string target) const {
    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_MESSAGES_REQUEST, target);
    if (!response.has_value()) {
        Debug::LogError("SmsBridgeModule: GetTargetMessages failed (timeout). Target: {}", target);
        ProcessError(ModuleFailReason::Timeout);
        co_return;
    }

    std::vector<std::string> messages;
    response.value()->GetValue(messages);
    Debug::Log("SmsBridgeModule: GetTargetMessages response received. Target: {}, Messages count: {}", target, messages.size());

    const std::unique_ptr<QEvent> event = std::make_unique<FetchMessageListResultEvent>(messages, target);
    ConnectionManager::SendEvent(event);
}

void SmsBridgeModule::EnableResponseCallbacks() {
    const std::shared_ptr<SmsBridgeModule> instance = std::static_pointer_cast<SmsBridgeModule>(shared_from_this());

    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("SmsBridgeModule: Received enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            Debug::Log("SmsBridgeModule: Already enabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, true);
            return;
        }
        instance->Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_DISABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("SmsBridgeModule: Received disable request");
        if (instance->GetModuleState() == ModuleState::Disabled) {
            Debug::Log("SmsBridgeModule: Already disabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, false);
            return;
        }
        instance->Disable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, [instance](PC_Package&& package) mutable {
        const bool peerEnabled = package->GetValue<bool>();
        Debug::Log("SmsBridgeModule: Peer module state changed: {}", peerEnabled);
        instance->m_peerModuleEnabled.store(peerEnabled);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_NEW_SMS_RECEIVED, [instance](PC_Package&& package) mutable {
        (void)instance;
        const std::string sender = package->GetValue<std::string>();
        const std::string body = package->GetValue<std::string>();
        const int64_t timestamp = package->GetValue<int64_t>();
        Debug::Log("SmsBridgeModule: New SMS received from {}", sender);
        const std::unique_ptr<QEvent> event = std::make_unique<NewSmsReceivedEvent>(sender, body, timestamp);
        ConnectionManager::SendEvent(event);
    });
}

void SmsBridgeModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_NEW_SMS_RECEIVED);
}

void SmsBridgeModule::OnInitialize() {}

asio::awaitable<void> SmsBridgeModule::OnEnable() {
    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, true);

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

asio::awaitable<void> SmsBridgeModule::OnDisable() {
    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_DISABLE);
    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, false);
    co_return;
}

asio::awaitable<void> SmsBridgeModule::OnShutdown() {
    co_return;
}

const char* SmsBridgeModule::GetModuleName() const {
    return "SmsBridgeModule";
}

ModuleType SmsBridgeModule::GetModuleType() const {
    return ModuleType::SmsBridge;
}
