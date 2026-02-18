#include <FileShareModule.h>
#include <FileEntry.h>
#include <QGuiApplication>
#include <FileSystemManager.h>
#include <HashHelpers.h>

constexpr size_t TRANSFER_CHANNELS_COUNT = 10;
constexpr size_t PROGRESS_EVENT_DELAY_MS = 100;

FileShareModule::FileShareModule() = default;

void FileShareModule::FetchDirectoryEntries(const FileEntry& entry) const {
    const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();

    if (path.empty()) {
        return;
    }

    asio::co_spawn(m_context, FetchDirectoryEntriesAwaitable(std::move(path)), asio::detached);
}

asio::awaitable<void> FileShareModule::FetchDirectoryEntriesAwaitable(std::string path) const {
    const std::shared_ptr<const BaseModule> instance = shared_from_this();

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST, std::move(path));
    std::vector<FileEntry> entries;

    if (response) {
        response.value()->GetValue(entries);
    }

    // TODO: Send event with entries
}

asio::awaitable<std::vector<FileEntry>> FileShareModule::FetchDirectoryEntriesAwaitable(std::string path) {
    const std::shared_ptr<const BaseModule> instance = shared_from_this();

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST, std::move(path));
    std::vector<FileEntry> entries;

    if (response) {
        response.value()->GetValue(entries);
    }

    co_return entries;
}

// Function fetch progress is available via qt events
void FileShareModule::FetchEntry(const FileEntry& entry, const std::string& destination) const {
    asio::co_spawn(m_context, FetchEntryAwaitable(entry, destination), asio::detached);
}

asio::awaitable<void> FileShareModule::FetchEntryAwaitable(FileEntry entry, std::string destination) const {
    const std::string entryPath = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    if (entryPath.empty()) {
        // TODO: THROW SOME EVENT
        co_return;
    }

    std::filesystem::path filePath(destination);
    if (!std::filesystem::is_directory(filePath)) {
        // TODO: THROW SOME EVENT
        co_return;
    }

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_TRANSFER_ENTRY_REQUEST, entry);
    if (response) {
        // TODO: THROW SOME EVENT
        co_return;
    }

    const uint8_t channelIndex = response.value()->GetValue<uint8_t>();

    if (m_transferChannels.size() >= channelIndex) {
        Debug::LogError("Transfer channel {} doesn't exists", channelIndex);
        // TODO: SEND SOME ERROR
        co_return;
    }

    TransferChannel* channel = m_transferChannels[channelIndex].get();
    if (channel->IsUsed(false)) {
        Debug::LogError("Transfer channel {} is in use", channelIndex);
        // TODO: SEND SOME ERROR
        co_return;
    }

    const FileType type = entry.GetType() ? entry.GetType().value() : FileType::Unknown;
    const std::filesystem::path path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    if (type == FileType::Directory) {
        filePath /= path.lexically_normal().filename();
        std::filesystem::create_directories(filePath);
    } else {
        filePath /= path.filename();
    }

    std::filesystem::create_directories(filePath);

    const auto future = type == FileType::Directory ?
        asio::co_spawn(m_context, channel->ReceiveDirectory(filePath), asio::use_future) :
        asio::co_spawn(m_context, channel->ReceiveFile(filePath), asio::use_future);

    asio::steady_timer timer(m_context);

    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        // TODO: Send progress event

        timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
        co_await timer.async_wait();
    }

    // TODO: Send transfer result event
}


void FileShareModule::CopyEntriesToClipboard(const std::vector<FileEntry>& entries) const {
    asio::co_spawn(m_context, CopyEntriesToClipboardAwaitable(entries), asio::detached);
}

asio::awaitable<void> FileShareModule::CopyEntriesToClipboardAwaitable(std::vector<FileEntry> entries) const {
    std::vector<asio::detail::promise_async_result<void(std::exception_ptr), std::allocator<void>>::return_type> futures;
    std::vector<std::filesystem::path> paths;

    futures.reserve(entries.size());
    paths.reserve(entries.size());

    for (const auto& entry : entries) {
        const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
        const std::string name = entry.GetName().has_value() ? entry.GetName().value() : std::string();

        // TODO: Add device specific temp

        const size_t hash = HashString(std::filesystem::path(path).parent_path().string());
        std::filesystem::path entryDestination = std::filesystem::temp_directory_path() / std::to_string(hash) / name;

        paths.push_back(entryDestination);
        futures.push_back(asio::co_spawn(m_context, FetchEntryAwaitable(entry, entryDestination.string()), asio::use_future));
    }

    for (auto& future : futures) {
        future.get();
    }

    FileSystemManager::CopyToClipboard(paths);

    // TODO: Send event with copy result

    co_return;
}

void FileShareModule::PostEntry(const std::filesystem::path& path, const std::filesystem::path& destination) const {
    asio::co_spawn(m_context, PostEntryAwaitable(path, destination), asio::detached);
}

asio::awaitable<void> FileShareModule::PostEntryAwaitable(const std::filesystem::path path, const std::filesystem::path destination) const {
    if (!std::filesystem::exists(path)) {
        Debug::LogError("File {} does not exist", path.string());
        co_return;
    }

    if (!std::filesystem::is_directory(destination)) {
        Debug::LogError("Destination should be a directory ({})", destination.string());
        co_return;
    }

    const bool isDirectory = std::filesystem::is_directory(path);
    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, destination.string(), path.filename().string(), isDirectory);
    if (!response.has_value()) {
        // TODO: Send status event

        co_return;
    }

    const uint8_t channelIndex = response.value()->GetValue<uint8_t>();
    if (channelIndex >= TRANSFER_CHANNELS_COUNT) {
        // TODO: Send event
    }

    TransferChannel* channel = m_transferChannels[channelIndex].get();

    const auto future = isDirectory ?
        asio::co_spawn(m_context, channel->SendDirectory(path), asio::use_future) :
        asio::co_spawn(m_context, channel->SendFile(path), asio::use_future);


    asio::steady_timer timer(m_context);

    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        // TODO: Send progress event

        timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
        co_await timer.async_wait();
    }

    // TODO: Send transfer result event
}

void FileShareModule::PasteEntryFromClipboard(const std::filesystem::path& destination) const {
    const std::filesystem::path path{}; // TODO: Replace it with actual implementation
    asio::co_spawn(m_context, PostEntryAwaitable(path, destination), asio::detached);
}

void FileShareModule::OpenEntry(const FileEntry& entry) {

}

void FileShareModule::EnableResponseCallbacks() {

}

void FileShareModule::DisableResponseCallbacks() {

}

void FileShareModule::OnInitialize() {
    m_transferChannels.reserve(TRANSFER_CHANNELS_COUNT);

    std::shared_ptr<SSLContext> sslContext = ConnectionManager::GetSSLContext();

    for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
        m_transferChannels.emplace_back(
            std::make_shared<TransferChannel>(sslContext, m_context)
        );
    }

    const std::filesystem::path applicationDataPath = FileSystemManager::GetAppDataPath(APPLICATION_NAME) / "temp/";
    std::filesystem::create_directories(applicationDataPath);
}

asio::awaitable<void> FileShareModule::OnEnable() {
    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_ENABLE);

    std::vector<std::pair<std::unique_ptr<AwaitableFlag>, uint16_t>> ports;
    ports.reserve(TRANSFER_CHANNELS_COUNT);

    for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
        ports.emplace_back(
            std::make_unique<AwaitableFlag>(m_context.get_executor()), 0
        );

        asio::co_spawn(
            m_context,
            m_transferChannels[i]->Seek(*ports[i].first, ports[i].second),
            asio::detached
        );
    }

    for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
        co_await ports[i].first->Wait();
        ConnectionManager::Send(PC_PackageType::CONNECTION_CHANNEL_CONNECTION_PORT_INFO, ports[i].second);
    }

    asio::steady_timer timer(m_context);
    while (true) {
        for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
            if (m_transferChannels[i]->GetConnectionState() != ConnectionState::CONNECTED) {
                goto WaitForChannels;
            }
        }

        break;

        WaitForChannels:
        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }

    co_return;
}

asio::awaitable<void> FileShareModule::OnDisable() {
    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_DISABLE);
    for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
        asio::co_spawn(m_context, m_transferChannels[i]->Disconnect(), asio::detached);
    }

    co_return;
}

asio::awaitable<void> FileShareModule::OnShutdown() {
    m_transferChannels.clear();
    co_return;
}
