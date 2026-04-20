#include <FileShareModule.h>
#include <FileEntry.h>
#include <FileSystemManager.h>
#include <HashHelpers.h>
#include <FileShareEvents.h>

#include <QDesktopServices>
#include <QUrl>

#include <chrono>
#include <future>
#include <stdexcept>

namespace
{
QString PathToQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

}

constexpr size_t TRANSFER_CHANNELS_COUNT = 10;
constexpr size_t PROGRESS_EVENT_DELAY_MS = 100;

FileShareModule::FileShareModule() = default;

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

    if (response) {
        response.value()->GetValue(entries);
    } else {
        Debug::LogError("FileShareModule: Directory entries request failed");
        ProcessError(ModuleFailReason::Timeout);
    }

    const std::unique_ptr<QEvent> event = std::make_unique<FetchDirectoryEntriesResultEvent>(std::move(path), std::move(entries));
    ConnectionManager::SendEvent(event);
}

asio::awaitable<std::vector<FileEntry>> FileShareModule::FetchDirectoryEntriesAwaitable(std::string path) {
    const std::shared_ptr<const BaseModule> instance = shared_from_this();
    std::string requestPath = path;

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(
        PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST,
        std::move(requestPath)
    );
    std::vector<FileEntry> entries;

    if (response) {
        response.value()->GetValue(entries);
    } else {
        ProcessError(ModuleFailReason::Timeout);
    }

    co_return entries;
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

    if (channelIndex >= m_transferChannels.size()) {
        Debug::LogError("FileShareModule: Transfer channel {} doesn't exists", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const std::shared_ptr<TransferChannel> channel = m_transferChannels[channelIndex];
    if (channel->IsUsed(false)) {
        Debug::LogError("FileShareModule: Transfer channel {} is in use", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const FileType type = entry.GetType() ? entry.GetType().value() : FileType::Unknown;
    const std::filesystem::path path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    const std::filesystem::path entryNamePath = std::filesystem::u8path(entryName);
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

        futures.reserve(entries.size());
        paths.reserve(entries.size());

        for (const auto& entry : entries) {
            const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
            const std::string name = entry.GetName().has_value() ? entry.GetName().value() : std::string();

            // TODO: Add device specific temp

            const size_t hash = HashString(std::filesystem::path(path).parent_path().string());
            std::filesystem::path destinationDirectory = std::filesystem::temp_directory_path() / std::to_string(hash);
            std::filesystem::create_directories(destinationDirectory);
            std::filesystem::path entryDestination = destinationDirectory / std::filesystem::u8path(name);

            paths.push_back(entryDestination);
            futures.push_back(asio::co_spawn(m_context, FetchEntryAwaitable(entry, destinationDirectory.string()), asio::use_future));
        }

        for (auto& future : futures) {
            future.get();
        }

        bool result = FileSystemManager::CopyToClipboard(paths);
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

    preparedPaths.reserve(entries.size());

    for (const FileEntry& entry : entries) {
        const std::string sourcePath = entry.GetPath().value_or(std::string());
        const std::string name = entry.GetName().value_or(std::string());
        if (sourcePath.empty() || name.empty()) {
            continue;
        }

        const size_t hash = HashString(std::filesystem::path(sourcePath).parent_path().string());
        std::filesystem::path destinationDirectory = std::filesystem::temp_directory_path() / std::to_string(hash);
        std::filesystem::create_directories(destinationDirectory);
        const std::filesystem::path expectedPath = destinationDirectory / std::filesystem::u8path(name);

        try {
            co_await FetchEntryAwaitable(entry, destinationDirectory.string());
            if (std::filesystem::exists(expectedPath)) {
                preparedPaths.push_back(expectedPath);
            }
        } catch (...) {
            // Skip failed entries and continue preparing remaining ones.
        }
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

    if (channelIndex >= TRANSFER_CHANNELS_COUNT) {
        Debug::LogError("FileShareModule: Transfer channel index {} is out of range", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const std::shared_ptr<TransferChannel> channel = m_transferChannels[channelIndex];

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
    const std::filesystem::path destinationDirectory =
        std::filesystem::temp_directory_path() /
        std::filesystem::path(boost::uuids::to_string(boost::uuids::random_generator()()));
    std::filesystem::create_directories(destinationDirectory);

    co_await FetchEntryAwaitable(entry, destinationDirectory.string());

    const std::filesystem::path openedPath = destinationDirectory /
        std::filesystem::u8path(entry.GetName().value_or(std::string()));
    QDesktopServices::openUrl(QUrl::fromLocalFile(PathToQString(openedPath)));
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

    // TODO: Add device specific temp

    const size_t hash = HashString(std::filesystem::path(path).parent_path().string());
    std::filesystem::path entryDestination = std::filesystem::temp_directory_path() / std::to_string(hash) / fmt::format("{}.png", name);

    {
        std::ofstream stream(entryDestination, std::ios::binary);
        if (!stream.good()) {
            Debug::LogError("FileShareModule: Failed to open icon destination for writing: {}", entryDestination.string());
            const std::unique_ptr<QEvent> event = std::make_unique<FetchEntryIconResultEvent>(entry, std::filesystem::path{}, false);
            ConnectionManager::SendEvent(event);
            co_return;
        }

        if (!iconBuffer.empty()) {
            stream.write(reinterpret_cast<const char*>(iconBuffer.data()), static_cast<std::streamsize>(iconBuffer.size()));
        }

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

void FileShareModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_ENABLE, [this, instance](PC_Package&& package) mutable {
        m_peerModuleEnabled.store(true);
        Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE, [this, instance](PC_Package&& package) mutable {
        m_peerModuleEnabled.store(false);
        Disable(true);
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, [this, instance](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID         = package->GetValue<size_t>();
        const std::string destination  = package->GetValue<std::string>();
        const std::string fileName     = package->GetValue<std::string>();
        const size_t totalTransferSize = package->GetValue<size_t>();
        const bool isDirectory         = package->GetValue<bool>();
        Debug::Log("FileShareModule: Received transfer post request. RequestID: {}, Destination: {}, Name: {}, IsDirectory: {}, Size: {}",
            requestID, destination, fileName, isDirectory, totalTransferSize);

        const std::filesystem::path destinationPath = std::filesystem::path(destination) / fileName;

        uint8_t transferChannelIndex{};
        asio::steady_timer timer(m_context);

        while (true) {
            {
                std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
                for (size_t i = 0; i < m_transferChannels.size(); ++i) {
                    const std::shared_ptr<TransferChannel> channel = m_transferChannels[i];
                    if (m_reservedIncomingPostChannels.find(i) != m_reservedIncomingPostChannels.end()) {
                        continue;
                    }

                    if (!channel->IsUsed(false)) {
                        transferChannelIndex = static_cast<uint8_t>(i);
                        m_reservedIncomingPostChannels.insert(i);
                        goto FINISH_CHANNEL_SEARCH;
                    }
                }
            }

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }

        FINISH_CHANNEL_SEARCH:
        std::shared_ptr<TransferChannel> channel = m_transferChannels[transferChannelIndex];
        const auto reservationGuard = std::shared_ptr<void>(nullptr, [this, transferChannelIndex](void*) {
            std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
            m_reservedIncomingPostChannels.erase(static_cast<size_t>(transferChannelIndex));
        });
        (void)reservationGuard;
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
}

void FileShareModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST);
}

void FileShareModule::OnInitialize() {
    m_peerModuleEnabled.store(false);
    {
        std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
        m_inFlightDirectoryRequests.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
        m_reservedIncomingPostChannels.clear();
    }
    m_transferChannels.clear();
    m_transferChannels.reserve(TRANSFER_CHANNELS_COUNT);

    std::shared_ptr<SSLContext_> sslContext = ConnectionManager::GetSSLContextServer();

    for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
        m_transferChannels.emplace_back(
            std::make_shared<TransferChannel>()
        );
    }

    const std::filesystem::path applicationDataPath = FileSystemManager::GetAppDataPath(APPLICATION_NAME) / "temp/";
    std::filesystem::create_directories(applicationDataPath);
}

asio::awaitable<void> FileShareModule::OnEnable() {
    // Always require a fresh peer ack for the current primary connection.
    m_peerModuleEnabled.store(false);

    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_ENABLE);
    std::vector<std::pair<std::unique_ptr<AwaitableFlag>, uint16_t>> ports;
    ports.reserve(m_transferChannels.size());

    for (int i = 0; i < m_transferChannels.size(); ++i) {
        ports.emplace_back(
            std::make_unique<AwaitableFlag>(m_context.get_executor()), 0
        );

        asio::co_spawn(
            m_context,
            m_transferChannels[i]->Seek(*ports[i].first, ports[i].second),
            asio::detached
        );
    }

    asio::steady_timer timer(m_context);
    const auto peerEnableDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!m_peerModuleEnabled.load()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        if (std::chrono::steady_clock::now() >= peerEnableDeadline) {
            throw std::runtime_error("FileShareModule enable timed out waiting for peer module acknowledgement");
        }

        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }

    for (int i = 0; i < m_transferChannels.size(); ++i) {
        co_await ports[i].first->Wait();
        if (ShouldAbortEnable()) {
            co_return;
        }
        ConnectionManager::Send(PC_PackageType::CONNECTION_CHANNEL_CONNECTION_PORT_INFO, ports[i].second);
    }

    const auto channelsDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (true) {
        const ModuleState state = GetModuleState();
        if (state != ModuleState::Enabling && state != ModuleState::Enabled) {
            co_return;
        }

        for (int i = 0; i < m_transferChannels.size(); ++i) {
            if (m_transferChannels[i]->GetConnectionState() != ConnectionState::CONNECTED) {
                goto WaitForChannels;
            }
        }

        break;

        WaitForChannels:
        if (std::chrono::steady_clock::now() >= channelsDeadline) {
            throw std::runtime_error("FileShareModule enable timed out waiting for transfer channels");
        }

        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }

    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, true);
}

asio::awaitable<void> FileShareModule::OnDisable() {
    m_peerModuleEnabled.store(false);
    {
        std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
        m_inFlightDirectoryRequests.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
        m_reservedIncomingPostChannels.clear();
    }
    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_DISABLE);
    for (size_t i = 0; i < m_transferChannels.size(); ++i) {
        asio::co_spawn(m_context, m_transferChannels[i]->Disconnect(), asio::detached);
    }

    co_return;
}

asio::awaitable<void> FileShareModule::OnShutdown() {
    m_peerModuleEnabled.store(false);
    {
        std::lock_guard<std::mutex> lock(m_directoryRequestMutex);
        m_inFlightDirectoryRequests.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
        m_reservedIncomingPostChannels.clear();
    }
    m_transferChannels.clear();
    co_return;
}

const char* FileShareModule::GetModuleName() const {
    return "FileShareModule";
}

ModuleType FileShareModule::GetModuleType() const {
    return ModuleType::NetworkFileSystem;
}
