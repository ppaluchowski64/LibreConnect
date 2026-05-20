#include <FileShareModule.h>
#include <FileEntry.h>
#include <FileSystemManager.h>
#include <ExternalFileOpener.h>
#include <HashHelpers.h>
#include <FileShareEvents.h>

#include <QSettings>
#include <QStandardPaths>

#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>

namespace
{
constexpr const char* FILE_SHARE_TEMP_CATEGORY = "file-share";
constexpr const char* CLIPBOARD_TEMP_CATEGORY = "clipboard";
constexpr const char* DRAG_TEMP_CATEGORY = "drag";
constexpr const char* OPEN_TEMP_CATEGORY = "open";
constexpr const char* ICON_TEMP_CATEGORY = "icons";
std::mutex g_incomingPostDirectoryMutex;
std::filesystem::path g_incomingPostDirectory;

std::filesystem::path EnsureFileShareTempRoot()
{
    return FileSystemManager::GetTemporaryStoragePath(FILE_SHARE_TEMP_CATEGORY);
}

std::filesystem::path EnsureFileShareTempCategoryPath(const std::string& category)
{
    const std::filesystem::path root = EnsureFileShareTempRoot();
    if (root.empty()) {
        return {};
    }

    const std::filesystem::path categoryPath = root / std::filesystem::path(reinterpret_cast<const char8_t*>(category.data()));
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

    const std::filesystem::path sessionPath = categoryPath /
        std::filesystem::path(boost::uuids::to_string(boost::uuids::random_generator()()));
    std::error_code ec;
    std::filesystem::create_directories(sessionPath, ec);
    if (ec) {
        return {};
    }

    return sessionPath;
}

std::filesystem::path CreateHashedSubdirectory(const std::filesystem::path& baseDirectory, const std::string& sourcePath)
{
    if (baseDirectory.empty()) {
        return {};
    }

    const size_t hash = HashString(std::filesystem::path(sourcePath).parent_path().string());
    const std::filesystem::path hashedDirectory = baseDirectory / std::to_string(hash);
    std::error_code ec;
    std::filesystem::create_directories(hashedDirectory, ec);
    if (ec) {
        return {};
    }

    return hashedDirectory;
}

std::filesystem::path DefaultIncomingPostDirectory()
{
    QSettings settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnect"));
    const QString savedDownloadDir = settings.value(QStringLiteral("fileManager/localDownloadDirectory")).toString();
    if (!savedDownloadDir.isEmpty()) {
        return savedDownloadDir.toStdString();
    }

    const QString defaultDownloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!defaultDownloadDir.isEmpty()) {
        return defaultDownloadDir.toStdString();
    }

    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
}

std::filesystem::path CurrentIncomingPostDirectory()
{
    std::lock_guard<std::mutex> lock(g_incomingPostDirectoryMutex);
    if (g_incomingPostDirectory.empty()) {
        g_incomingPostDirectory = DefaultIncomingPostDirectory();
    }

    return g_incomingPostDirectory;
}

}

constexpr size_t TRANSFER_CHANNELS_COUNT = 10;
constexpr size_t PROGRESS_EVENT_DELAY_MS = 100;

void FileShareModule::SetIncomingPostDirectory(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(g_incomingPostDirectoryMutex);
    g_incomingPostDirectory = path;
}

bool FileShareModule::TryBeginDirectoryRequest(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
    const auto [_, inserted] = m_inFlightDirectoryRequests.insert(path);
    return inserted;
}

void FileShareModule::EndDirectoryRequest(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
    m_inFlightDirectoryRequests.erase(path);
}

void FileShareModule::FetchDirectoryEntries(const std::string& path) const {
    if (path.empty()) {
        Debug::LogWarning("FileShareModule: Fetch directory entries skipped: empty path");
        return;
    }

    if (!TryBeginDirectoryRequest(path)) {
        Debug::LogWarning("FileShareModule: Fetch directory entries skipped: request already in-flight for {}", path);
        return;
    }

    Debug::Log("FileShareModule: Fetching directory entries for {}", path);
    asio::co_spawn(m_context, FetchDirectoryEntriesAwaitable(path), asio::detached);
}

void FileShareModule::FetchDirectoryEntries(const FileEntry& entry) const {
    const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();

    if (path.empty()) {
        Debug::LogWarning("FileShareModule: Fetch directory entries skipped: empty path");
        return;
    }

    if (!TryBeginDirectoryRequest(path)) {
        Debug::LogWarning("FileShareModule: Fetch directory entries skipped: request already in-flight for {}", path);
        return;
    }

    Debug::Log("FileShareModule: Fetching directory entries for {}", path);
    asio::co_spawn(m_context, FetchDirectoryEntriesAwaitable(std::move(path)), asio::detached);
}

asio::awaitable<void> FileShareModule::FetchDirectoryEntriesAwaitable(std::string path) const {
    const std::shared_ptr<const BaseModule> instance = shared_from_this();
    std::string requestPath = path;
    const auto guard = std::shared_ptr<void>(nullptr, [this, path](void*) {
        EndDirectoryRequest(path);
    });
    (void)guard;

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(
        PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST,
        std::move(requestPath)
    );

    std::vector<FileEntry> entries;
    size_t index;

    if (!response) {
        Debug::LogError("FileShareModule: Directory entries request failed");
        ProcessError(ModuleFailReason::Timeout);
    }

    response.value()->GetValue(index);
    const auto opt = TransferChannelPool::Get(index);

    if (opt.has_value()) {
        const auto& channel = opt.value();
        co_await channel->ReceiveDirectoryEntries(entries);
    } else {
        Debug::LogError("FileShareModule: Transfer channel {} doesn't exists", index);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const std::unique_ptr<QEvent> event = std::make_unique<FetchDirectoryEntriesResultEvent>(std::move(path), std::move(entries));
    ConnectionManager::SendEvent(event);
}

void FileShareModule::FetchEntry(const FileEntry& entry, const std::string& destination) const {
    asio::co_spawn(m_context, FetchEntryAwaitable(entry, destination), asio::detached);
}

asio::awaitable<void> FileShareModule::FetchEntryAwaitable(FileEntry entry, std::string destination) const {
    const std::string entryPath = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    const std::string entryName = entry.GetName().has_value() ? entry.GetName().value() : std::string();
    Debug::Log("FileShareModule: Fetch entry requested. Source: {}, Destination: {}", entryPath, destination);
    if (entryPath.empty()) {
        Debug::LogError("FileShareModule: Fetch entry failed: empty source path");
        ProcessError(ModuleFailReason::IncorrectConfig);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }
    if (entryName.empty()) {
        Debug::LogError("FileShareModule: Fetch entry failed: empty source name");
        ProcessError(ModuleFailReason::IncorrectConfig);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    std::filesystem::path filePath(destination);
    if (!std::filesystem::is_directory(filePath)) {
        Debug::LogError("FileShareModule: Fetch entry failed: destination is not a directory ({})", destination);
        ProcessError(ModuleFailReason::IncorrectConfig);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_TRANSFER_FETCH_REQUEST, entry);
    if (!response.has_value()) {
        Debug::LogError("FileShareModule: Fetch entry failed: fetch request rejected for {}", entryPath);
        ProcessError(ModuleFailReason::Timeout);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const uint8_t channelIndex = response.value()->GetValue<uint8_t>();
    const size_t totalTransferSize = response.value()->GetValue<size_t>();
    Debug::Log("FileShareModule: Fetch entry accepted. Channel: {}, Size: {}", channelIndex, totalTransferSize);

    const auto opt = TransferChannelPool::Get(channelIndex);
    if (!opt.has_value()) {
        Debug::LogError("FileShareModule: Transfer channel {} doesn't exists", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const auto& channel = opt.value();
    if (channel->GetConnectionState() != ConnectionState::CONNECTED) {
        Debug::LogError("FileShareModule: Transfer channel {} is not connected", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }
    if (channel->IsUsed(false)) {
        Debug::LogError("FileShareModule: Transfer channel {} is in use", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const FileType type = entry.GetType() ? entry.GetType().value() : FileType::Unknown;
    const std::filesystem::path path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    const std::filesystem::path entryNamePath = std::filesystem::path(reinterpret_cast<const char8_t*>(entryName.data()));
    if (type == FileType::Directory) {
        filePath /= entryNamePath;
        std::filesystem::create_directories(filePath);
    } else {
        filePath /= entryNamePath;
        std::filesystem::create_directories(filePath.parent_path());
    }
    Debug::Log("FileShareModule: Fetch entry transfer started to {}", filePath.string());

    const auto future = type == FileType::Directory ?
        asio::co_spawn(m_context, channel->ReceiveDirectory(filePath), asio::use_future) :
        asio::co_spawn(m_context, channel->ReceiveFile(filePath), asio::use_future);

    asio::steady_timer timer(m_context);

    {
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferProgressEvent>(entry, totalTransferSize, 0, TransferOperation::Fetch);

        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            const size_t progress = channel->FetchTransferProgress();
            reinterpret_cast<EntryTransferProgressEvent*>(event.get())->SetBytesTransferred(progress);
            ConnectionManager::SendEvent(event);

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }
    }

    const size_t transferred = channel->FetchTransferProgress();
    const bool success = totalTransferSize == transferred;
    Debug::Log("FileShareModule: Fetch entry transfer finished. Success: {}, Bytes: {}/{}", success, transferred, totalTransferSize);
    const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, success);
    ConnectionManager::SendEvent(event);
}


void FileShareModule::CopyEntriesToClipboard(std::vector<FileEntry> entries) const {
    asio::post(m_context, [this, entries = std::move(entries)]() {
        std::vector<asio::detail::promise_async_result<void(std::exception_ptr), std::allocator<void>>::return_type> futures;
        std::vector<std::filesystem::path> paths;
        const std::filesystem::path operationDirectory = CreateFileShareTempSessionDirectory(CLIPBOARD_TEMP_CATEGORY);
        if (operationDirectory.empty()) {
            const std::unique_ptr<QEvent> event = std::make_unique<EntriesCopyResultEvent>(std::move(entries), false);
            ConnectionManager::SendEvent(event);
            return;
        }

        futures.reserve(entries.size());
        paths.reserve(entries.size());

        for (const auto& entry : entries) {
            const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
            const std::string name = entry.GetName().has_value() ? entry.GetName().value() : std::string();
            std::filesystem::path destinationDirectory = CreateHashedSubdirectory(operationDirectory, path);
            if (destinationDirectory.empty()) {
                continue;
            }
            std::filesystem::path entryDestination = destinationDirectory / std::filesystem::path(reinterpret_cast<const char8_t*>(name.data()));

            paths.push_back(entryDestination);
            futures.push_back(asio::co_spawn(m_context, FetchEntryAwaitable(entry, destinationDirectory.string()), asio::use_future));
        }

        for (auto& future : futures) {
            future.get();
        }

        bool result = FileSystemManager::CopyToClipboard(paths, { operationDirectory });
        if (!result) {
            std::error_code ec;
            std::filesystem::remove_all(operationDirectory, ec);
        }
        const std::unique_ptr<QEvent> event = std::make_unique<EntriesCopyResultEvent>(std::move(entries), result);
        ConnectionManager::SendEvent(event);
    });
}

std::vector<std::filesystem::path> FileShareModule::PrepareEntriesForExternalDrag(std::vector<FileEntry> entries) const {
    if (entries.empty()) {
        return {};
    }

    try {
        return asio::co_spawn(m_context, PrepareEntriesForExternalDragAwaitable(std::move(entries)), asio::use_future).get();
    } catch (...) {
        return {};
    }
}

asio::awaitable<std::vector<std::filesystem::path>> FileShareModule::PrepareEntriesForExternalDragAwaitable(std::vector<FileEntry> entries) const {
    std::vector<std::filesystem::path> preparedPaths;
    if (entries.empty()) {
        co_return preparedPaths;
    }

    const std::filesystem::path operationDirectory = CreateFileShareTempSessionDirectory(DRAG_TEMP_CATEGORY);
    if (operationDirectory.empty()) {
        co_return preparedPaths;
    }

    preparedPaths.reserve(entries.size());

    for (const FileEntry& entry : entries) {
        const std::string sourcePath = entry.GetPath().value_or(std::string());
        const std::string name = entry.GetName().value_or(std::string());
        if (sourcePath.empty() || name.empty()) {
            continue;
        }

        std::filesystem::path destinationDirectory = CreateHashedSubdirectory(operationDirectory, sourcePath);
        if (destinationDirectory.empty()) {
            continue;
        }
        const std::filesystem::path expectedPath = destinationDirectory / std::filesystem::path(reinterpret_cast<const char8_t*>(name.data()));

        try {
            co_await FetchEntryAwaitable(entry, destinationDirectory.string());
            if (std::filesystem::exists(expectedPath)) {
                preparedPaths.push_back(expectedPath);
            }
        } catch (...) {
            // Skip failed entries and continue preparing remaining ones.
        }
    }

    if (preparedPaths.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(operationDirectory, ec);
    }

    co_return preparedPaths;
}

void FileShareModule::PostEntry(const std::filesystem::path& path, const std::filesystem::path& destination) const {
    asio::co_spawn(m_context, PostEntryAwaitable(path, destination), asio::detached);
}

asio::awaitable<void> FileShareModule::PostEntryAwaitable(const std::filesystem::path path, const std::filesystem::path destination) const {
    Debug::Log("FileShareModule: Post entry requested. Source: {}, Destination: {}", path.string(), destination.string());
    if (!std::filesystem::exists(path)) {
        Debug::LogError("FileShareModule: File {} does not exist", path.string());
        ProcessError(ModuleFailReason::IncorrectConfig);
        co_return;
    }

    FileEntry entry(path);
    const bool isDirectory = std::filesystem::is_directory(path);
    size_t totalTransferSize = 0;

    if (isDirectory) {
        for (const auto& it : std::filesystem::recursive_directory_iterator(path)) {
            if (it.is_regular_file()) {
                totalTransferSize += it.file_size();
            }
        }

    } else {
        totalTransferSize += std::filesystem::file_size(path);
    }
    Debug::Log("FileShareModule: Post entry prepared. IsDirectory: {}, Size: {}", isDirectory, totalTransferSize);

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, destination.string(), path.filename().string(), size_t{totalTransferSize}, isDirectory);
    if (!response.has_value()) {
        ProcessError(ModuleFailReason::Timeout);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const uint8_t channelIndex = response.value()->GetValue<uint8_t>();
    Debug::Log("FileShareModule: Post entry accepted. Channel: {}", channelIndex);

    const auto opt = TransferChannelPool::Get(channelIndex);
    if (!opt.has_value()) {
        Debug::LogError("FileShareModule: Transfer channel index {} is out of range", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const auto& channel = opt.value();

    if (channel->GetConnectionState() != ConnectionState::CONNECTED) {
        Debug::LogError("FileShareModule: Transfer channel {} is not connected", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const auto future = isDirectory ?
        asio::co_spawn(m_context, channel->SendDirectory(path), asio::use_future) :
        asio::co_spawn(m_context, channel->SendFile(path), asio::use_future);


    asio::steady_timer timer(m_context);

    {
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferProgressEvent>(entry, totalTransferSize, 0, TransferOperation::Post);

        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            const size_t progress = channel->FetchTransferProgress();
            reinterpret_cast<EntryTransferProgressEvent*>(event.get())->SetBytesTransferred(progress);
            ConnectionManager::SendEvent(event);

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }
    }

    const size_t transferred = channel->FetchTransferProgress();
    const bool success = totalTransferSize == transferred;
    Debug::Log("FileShareModule: Post entry transfer finished. Success: {}, Bytes: {}/{}", success, transferred, totalTransferSize);
    const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, success);
    ConnectionManager::SendEvent(event);
}

void FileShareModule::PasteEntryFromClipboard(const std::filesystem::path& destination) const {
    const std::filesystem::path path{}; // TODO: Replace it with actual implementation
    asio::co_spawn(m_context, PostEntryAwaitable(path, destination), asio::detached);
}

void FileShareModule::OpenEntry(const FileEntry& entry) const {
    asio::co_spawn(m_context, OpenEntryAwaitable(entry), asio::detached);
}

void FileShareModule::FetchEntryIcon(const FileEntry& entry, const FileIconDensity density) const {
    const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    const std::string name = entry.GetName().has_value() ? entry.GetName().value() : std::string();
    Debug::Log(
        "FileShareModule: FetchEntryIcon requested. Path: {}, Name: {}, Density: {}",
        path,
        name,
        static_cast<int>(density)
    );
    asio::co_spawn(m_context, FetchEntryIconAwaitable(entry, density), asio::detached);
}

asio::awaitable<void> FileShareModule::OpenEntryAwaitable(const FileEntry entry) const {
    const std::filesystem::path destinationDirectory = CreateFileShareTempSessionDirectory(OPEN_TEMP_CATEGORY);
    if (destinationDirectory.empty()) {
        co_return;
    }

    co_await FetchEntryAwaitable(entry, destinationDirectory.string());

    const std::string entryName = entry.GetName().value_or(std::string());
    const std::filesystem::path openedPath = destinationDirectory /
        std::filesystem::path(reinterpret_cast<const char8_t*>(entryName.data()));
    if (!ExternalFileOpener::OpenLocalFile(openedPath)) {
        Debug::LogError("FileShareModule: Failed to open fetched entry {}", openedPath.string());
    }
}

asio::awaitable<void> FileShareModule::FetchEntryIconAwaitable(const FileEntry entry, const FileIconDensity density) {
    const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    const std::string name = entry.GetName().has_value() ? entry.GetName().value() : std::string();
    Debug::Log(
        "FileShareModule: FetchEntryIconAwaitable start. Path: {}, Name: {}, Density: {}",
        path,
        name,
        static_cast<int>(density)
    );

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_FETCH_ENTRY_ICON_REQUEST, FileEntry(entry), density);
    if (!response) {
        Debug::LogError("FileShareModule: FetchEntryIcon request failed (no response)");
        const std::unique_ptr<QEvent> event = std::make_unique<FetchEntryIconResultEvent>(entry, std::filesystem::path{}, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const std::vector<uint8_t> iconBuffer = response.value()->GetValue<std::vector<uint8_t>>();
    Debug::Log("FileShareModule: FetchEntryIcon response received. Bytes: {}", iconBuffer.size());
    if (iconBuffer.empty()) {
        Debug::LogWarning("FileShareModule: FetchEntryIcon returned no icon data");
        const std::unique_ptr<QEvent> event = std::make_unique<FetchEntryIconResultEvent>(entry, std::filesystem::path{}, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const std::filesystem::path iconBaseDirectory = EnsureFileShareTempCategoryPath(ICON_TEMP_CATEGORY);
    const std::filesystem::path iconDirectory = CreateHashedSubdirectory(iconBaseDirectory, path);
    if (iconDirectory.empty()) {
        const std::unique_ptr<QEvent> event = std::make_unique<FetchEntryIconResultEvent>(entry, std::filesystem::path{}, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    std::filesystem::path entryDestination = iconDirectory / fmt::format("{}.png", name);

    {
        std::ofstream stream(entryDestination, std::ios::binary);
        if (!stream.good()) {
            Debug::LogError("FileShareModule: Failed to open icon destination for writing: {}", entryDestination.string());
            const std::unique_ptr<QEvent> event = std::make_unique<FetchEntryIconResultEvent>(entry, std::filesystem::path{}, false);
            ConnectionManager::SendEvent(event);
            co_return;
        }

        stream.write(reinterpret_cast<const char*>(iconBuffer.data()), static_cast<std::streamsize>(iconBuffer.size()));

        if (!stream.good()) {
            Debug::LogError("FileShareModule: Failed while writing icon bytes to {}", entryDestination.string());
            const std::unique_ptr<QEvent> event = std::make_unique<FetchEntryIconResultEvent>(entry, std::filesystem::path{}, false);
            ConnectionManager::SendEvent(event);
            co_return;
        }
    }

    Debug::Log("FileShareModule: FetchEntryIcon saved icon to {}", entryDestination.string());
    const std::unique_ptr<QEvent> event = std::make_unique<FetchEntryIconResultEvent>(entry, entryDestination, true);
    ConnectionManager::SendEvent(event);
}

void FileShareModule::DeleteEntries(std::vector<FileEntry> entries) const {
    asio::co_spawn(m_context, DeleteEntriesAwaitable(std::move(entries)), asio::detached);
}

asio::awaitable<void> FileShareModule::DeleteEntriesAwaitable(std::vector<FileEntry> entries) const {
    Debug::Log("FileShareModule: Delete entries requested. Count: {}", entries.size());
    if (entries.empty()) {
        const std::unique_ptr<QEvent> event = std::make_unique<EntriesDeleteResultEvent>(std::move(entries), true);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_DELETE_ENTRIES_REQUEST, entries);
    bool success = false;

    if (!response.has_value()) {
        Debug::LogError("FileShareModule: Delete entries request failed (timeout or rejected)");
        ProcessError(ModuleFailReason::Timeout);
    } else {
        response.value()->GetValue(success);
        Debug::Log("FileShareModule: Delete entries request completed. Success: {}", success);
    }

    const std::unique_ptr<QEvent> event = std::make_unique<EntriesDeleteResultEvent>(std::move(entries), success);
    ConnectionManager::SendEvent(event);
}

void FileShareModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_ENABLE, [this, instance](PC_Package&& package) mutable {
        (void)package;
        m_peerModuleEnabled.store(true);
        if (GetModuleState() == ModuleState::Enabled) {
            ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, true);
            return;
        }

        Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE, [this, instance](PC_Package&& package) mutable {
        (void)package;
        m_peerModuleEnabled.store(false);
        if (GetModuleState() == ModuleState::Disabled) {
            ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, false);
            return;
        }

        Disable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, [this, instance](PC_Package&& package) mutable {
        m_peerModuleEnabled.store(package->GetValue<bool>());
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, [this, instance](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID         = package->GetValue<size_t>();
        const std::string destination  = package->GetValue<std::string>();
        const std::string fileName     = package->GetValue<std::string>();
        const size_t totalTransferSize = package->GetValue<size_t>();
        const bool isDirectory         = package->GetValue<bool>();
        Debug::Log("FileShareModule: Received transfer post request. RequestID: {}, Destination: {}, Name: {}, IsDirectory: {}, Size: {}",
            requestID, destination, fileName, isDirectory, totalTransferSize);

        std::filesystem::path destinationDirectory(destination);
        if (destinationDirectory.empty()) {
            std::lock_guard<std::mutex> lock(g_incomingPostDirectoryMutex);
            destinationDirectory = g_incomingPostDirectory;
        }

        if (destinationDirectory.empty()) {
            Debug::LogError("FileShareModule: Incoming post request has no destination directory");
            co_return;
        }

        std::error_code createError;
        std::filesystem::create_directories(destinationDirectory, createError);
        if (createError || !std::filesystem::is_directory(destinationDirectory)) {
            Debug::LogError("FileShareModule: Incoming post destination is invalid: {}", destinationDirectory.string());
            co_return;
        }

        const std::filesystem::path destinationPath = destinationDirectory / fileName;

        const BorrowedTransferChannel acquiredChannel = co_await TransferChannelPool::BorrowTransferChannel(true);
        const uint8_t transferChannelIndex = static_cast<uint8_t>(acquiredChannel.index);
        const std::shared_ptr<TransferChannel> channel = acquiredChannel.channel;
        asio::steady_timer timer(m_context);
        Debug::Log("FileShareModule: Selected transfer channel {} for incoming post request {}", transferChannelIndex, requestID);

        const auto future = isDirectory ?
            asio::co_spawn(m_context, channel->ReceiveDirectory(destinationPath), asio::use_future) :
            asio::co_spawn(m_context, channel->ReceiveFile(destinationPath), asio::use_future);

        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::FILE_SHARE_TRANSFER_POST_RESPONSE, transferChannelIndex);
        FileEntry entry(destinationPath);

        {
            const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferProgressEvent>(entry, totalTransferSize, 0, TransferOperation::Post);

            while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                const size_t progress = channel->FetchTransferProgress();
                reinterpret_cast<EntryTransferProgressEvent*>(event.get())->SetBytesTransferred(progress);
                ConnectionManager::SendEvent(event);

                timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
                co_await timer.async_wait();
            }
        }

        const size_t transferred = channel->FetchTransferProgress();
        const bool success = totalTransferSize == transferred;
        Debug::Log("FileShareModule: Incoming post transfer finished. RequestID: {}, Success: {}, Bytes: {}/{}", requestID, success, transferred, totalTransferSize);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, success);
        ConnectionManager::SendEvent(event);
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_GET_REQUEST, [](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID = package->GetValue<size_t>();
        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_GET_RESPONSE,
            CurrentIncomingPostDirectory().string()
        );
        co_return;
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_SET_REQUEST, [](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID = package->GetValue<size_t>();
        const std::string requestedPath = package->GetValue<std::string>();
        const std::filesystem::path candidate(requestedPath);

        std::error_code ec;
        const bool valid = !candidate.empty() &&
            std::filesystem::exists(candidate, ec) &&
            !ec &&
            std::filesystem::is_directory(candidate, ec) &&
            !ec;

        if (!valid) {
            ConnectionManager::SendRequestResponse(
                requestID,
                PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_SET_RESPONSE,
                false,
                requestedPath,
                std::string("That desktop folder does not exist. Modify the path and try again.")
            );
            co_return;
        }

        {
            std::lock_guard<std::mutex> lock(g_incomingPostDirectoryMutex);
            g_incomingPostDirectory = candidate;
        }

        QSettings settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnect"));
        settings.setValue(QStringLiteral("fileManager/localDownloadDirectory"), QString::fromStdString(candidate.string()));

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_SET_RESPONSE,
            true,
            candidate.string(),
            std::string("Default download path saved.")
        );
        co_return;
    });
}

void FileShareModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_GET_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_SET_REQUEST);
}

void FileShareModule::OnInitialize() {
    m_peerModuleEnabled.store(false);
    {
        std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
        m_inFlightDirectoryRequests.clear();
    }

    const std::filesystem::path tempPath = EnsureFileShareTempRoot();
    if (!tempPath.empty()) {
        std::filesystem::create_directories(tempPath);
    }

    {
        std::lock_guard<std::mutex> lock(g_incomingPostDirectoryMutex);
        if (g_incomingPostDirectory.empty()) {
            g_incomingPostDirectory = DefaultIncomingPostDirectory();
        }
    }
}

asio::awaitable<void> FileShareModule::OnEnable() {
    asio::steady_timer timer(m_context);

    ConnectionState state = TransferChannelPool::GetConnectionState();

    while (state != ConnectionState::CONNECTED) {
        if (state != ConnectionState::CONNECTING) {
            Disable();
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
        co_await timer.async_wait();

        state = TransferChannelPool::GetConnectionState();
    }

    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, true);
}

asio::awaitable<void> FileShareModule::OnDisable() {
    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_DISABLE);
    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, false);
    {
        std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
        m_inFlightDirectoryRequests.clear();
    }

    co_return;
}

asio::awaitable<void> FileShareModule::OnShutdown() {
    m_peerModuleEnabled.store(false);
    {
        std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
        m_inFlightDirectoryRequests.clear();
    }

    co_return;
}

const char* FileShareModule::GetModuleName() const {
    return "FileShareModule";
}

ModuleType FileShareModule::GetModuleType() const {
    return ModuleType::NetworkFileSystem;
}
