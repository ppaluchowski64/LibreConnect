#include <FileShareModule.h>
#include <FileEntry.h>
#include <FileSystemManager.h>
#include <HashHelpers.h>
#include <FileShareEvents.h>
#include <PermissionManager.h>

#include <QDesktopServices>
#include <QUrl>
#include <magic_enum/magic_enum.hpp>

#ifdef ANDROID_DEVICE
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include "FileIconDensity.h"

constexpr size_t TRANSFER_CHANNELS_COUNT = 10;
constexpr size_t PROGRESS_EVENT_DELAY_MS = 100;
constexpr size_t DIRECTORY_REQUEST_WAIT_POLL_MS = 5;

FileShareModule::FileShareModule() = default;

std::shared_future<DirectoryResult> FileShareModule::GetOrCreateDirectoryScanFuture(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_directoryScanMutex);
    const auto it = m_directoryScanFutures.find(path);
    if (it != m_directoryScanFutures.end()) {
        return it->second;
    }

    const std::filesystem::path filesystemPath(path);
    std::future<DirectoryResult> future = ThreadPool::PostFuture([filesystemPath]() {
        return FileSystemManager::GetEntries(filesystemPath);
    });

    std::shared_future<DirectoryResult> sharedFuture = future.share();
    m_directoryScanFutures.insert_or_assign(path, sharedFuture);
    return sharedFuture;
}

void FileShareModule::CleanupDirectoryScanFutureIfReady(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_directoryScanMutex);
    const auto it = m_directoryScanFutures.find(path);
    if (it == m_directoryScanFutures.end()) {
        return;
    }

    if (it->second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        m_directoryScanFutures.erase(it);
    }
}

void FileShareModule::ClearDirectoryScanFutures() {
    std::lock_guard<std::mutex> lock(m_directoryScanMutex);
    m_directoryScanFutures.clear();
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

    if (!std::filesystem::is_directory(destination)) {
        Debug::LogError("FileShareModule: Destination should be a directory ({})", destination.string());
        ProcessError(ModuleFailReason::IncorrectConfig);
        co_return;
    }

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

    if (channelIndex >= m_transferChannels.size()) {
        Debug::LogError("FileShareModule: Transfer channel index {} is out of range", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        ConnectionManager::Disconnect();
        co_return;
    }

    const std::shared_ptr<TransferChannel> channel = m_transferChannels[channelIndex];
    if (channel->GetConnectionState() != ConnectionState::CONNECTED) {
        Debug::LogError("FileShareModule: Transfer channel {} is not connected", channelIndex);
        ProcessError(ModuleFailReason::InternalError);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    {
        const auto future = isDirectory ?
            asio::co_spawn(m_context, channel->SendDirectory(path), asio::use_future) :
            asio::co_spawn(m_context, channel->SendFile(path), asio::use_future);

        asio::steady_timer timer(m_context);
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

std::vector<uint8_t> FileShareModule::GetEntryIcon(const std::string& file, const FileIconDensity density) {
#ifdef ANDROID_DEVICE
    if (file.empty()) {
        Debug::LogWarning("FileShareModule: GetEntryIcon skipped: empty file path");
        return {};
    }
    Debug::Log("FileShareModule: GetEntryIcon request. Path: {}, Density: {}", file, static_cast<int>(density));

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        Debug::LogWarning("FileShareModule: GetEntryIcon failed: Android context is invalid");
        return {};
    }

    const QJniObject filePath = QJniObject::fromString(QString::fromStdString(file));
    const QJniObject response = QJniObject::callStaticObjectMethod(
        "com/LibreConnect/mobile/FileSystemUtils",
        "getFileIconAsPngBytes",
        "(Landroid/content/Context;Ljava/lang/String;I)[B",
        context.object<jobject>(),
        filePath.object<jstring>(),
        static_cast<jint>(density)
    );

    const QJniEnvironment env;
    if (!response.isValid()) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        Debug::LogWarning("FileShareModule: GetEntryIcon failed: JNI response is invalid");
        return {};
    }

    const jbyteArray bytes = response.object<jbyteArray>();
    if (!bytes) {
        Debug::LogWarning("FileShareModule: GetEntryIcon failed: JNI byte array is null");
        return {};
    }

    const jsize length = env->GetArrayLength(bytes);
    if (length <= 0) {
        Debug::LogWarning("FileShareModule: GetEntryIcon returned empty icon bytes");
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(length));
    env->GetByteArrayRegion(
        bytes,
        0,
        length,
        reinterpret_cast<jbyte*>(buffer.data())
    );

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        Debug::LogWarning("FileShareModule: GetEntryIcon failed while reading JNI byte array");
        return {};
    }

    Debug::Log("FileShareModule: GetEntryIcon success. Path: {}, Bytes: {}", file, buffer.size());
    return buffer;
#else
    (void)file;
    (void)density;
    Debug::LogWarning("FileShareModule: GetEntryIcon is unavailable on non-Android platform");
    return {};
#endif
}

void FileShareModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

	ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_ENABLE, [instance, this](PC_Package&& package) mutable {
           const ModuleState state = GetModuleState();
           if (state == ModuleState::Enabled) {
               // Reconnect flow: peer requested enable while we are already enabled.
               m_peerModuleEnabled.store(true);
               m_transferChannelInitializationIndex.store(0);
               ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_ENABLE);
               ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, true);
               return;
           }

           m_transferChannelInitializationIndex.store(0);
	       Enable(true);
	    });
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE, [instance, this](PC_Package&& package) mutable {
       Disable(true);
    });
	ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
	    const size_t requestID    = package->GetValue<size_t>();
	    const std::string pathStr = package->GetValue<std::string>();
        Debug::Log("FileShareModule: Received directory entries request. RequestID: {}, Path: {}", requestID, pathStr);

        const std::shared_future<DirectoryResult> scanFuture = GetOrCreateDirectoryScanFuture(pathStr);

        asio::steady_timer timer(m_context);
        while (scanFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            timer.expires_after(std::chrono::milliseconds(DIRECTORY_REQUEST_WAIT_POLL_MS));
            co_await timer.async_wait();
        }

        DirectoryResult result;
        try {
            result = scanFuture.get();
        } catch (const std::exception& exc) {
            Debug::LogError(
                "FileShareModule: Directory scan failed with exception. RequestID: {}, Path: {}, Error: {}",
                requestID,
                pathStr,
                exc.what()
            );
            ProcessError(ModuleFailReason::InternalError);
            ConnectionManager::SendRequestResponse(
                requestID,
                PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_RESPONSE,
                std::vector<FileEntry>{}
            );
            CleanupDirectoryScanFutureIfReady(pathStr);
            co_return;
        }

        CleanupDirectoryScanFutureIfReady(pathStr);

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_RESPONSE,
            std::move(result.entries)
        );
	});
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_FETCH_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID = package->GetValue<size_t>();
        const FileEntry entry  = package->GetValue<FileEntry>();
        Debug::Log("FileShareModule: Received transfer fetch request. RequestID: {}", requestID);

        if (!entry.GetPath().has_value() || !entry.GetName().has_value()) {
            Debug::LogError("FileShareModule: Missing file path");
            ProcessError(ModuleFailReason::IncorrectConfig);
            ConnectionManager::Disconnect();
            co_return;
        }

        const std::filesystem::path path = std::filesystem::path(entry.GetPath().value()) / entry.GetName().value();
        const bool isDirectory = std::filesystem::is_directory(path);
        size_t totalSize = 0;

        if (isDirectory) {
            for (const auto& it : std::filesystem::recursive_directory_iterator(path)) {
                if (it.is_regular_file()) {
                    totalSize += it.file_size();
                }
            }

        } else {
            totalSize = std::filesystem::file_size(path);
        }
        Debug::Log("FileShareModule: Prepared fetch transfer. RequestID: {}, Path: {}, IsDirectory: {}, Size: {}", requestID, path.string(), isDirectory, totalSize);

        size_t transferChannelIndex{};
        asio::steady_timer timer(m_context);

        while (true) {
            for (size_t i = 0; i < m_transferChannels.size(); ++i) {
                const std::shared_ptr<TransferChannel> channel = m_transferChannels[i];
                if (channel->GetConnectionState() == ConnectionState::CONNECTED && !channel->IsUsed(false)) {
                    transferChannelIndex = i;
                    goto FINISH_CHANNEL_SEARCH;
                }
            }

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }

        FINISH_CHANNEL_SEARCH:
        const std::shared_ptr<TransferChannel> channel = m_transferChannels[transferChannelIndex];
        Debug::Log("FileShareModule: Selected transfer channel {} for incoming fetch request {}", transferChannelIndex, requestID);

        const auto future = isDirectory ?
            asio::co_spawn(m_context, channel->SendDirectory(path), asio::use_future) :
            asio::co_spawn(m_context, channel->SendFile(path), asio::use_future);

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::FILE_SHARE_TRANSFER_FETCH_RESPONSE,
            static_cast<uint8_t>(transferChannelIndex),
            totalSize
        );

        {
            const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferProgressEvent>(entry, totalSize, 0, TransferOperation::Fetch);

            while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                const size_t progress = channel->FetchTransferProgress();
                reinterpret_cast<EntryTransferProgressEvent*>(event.get())->SetBytesTransferred(progress);
                ConnectionManager::SendEvent(event);

                timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
                co_await timer.async_wait();
            }
        }

        const size_t transferred = channel->FetchTransferProgress();
        const bool success = totalSize == transferred;
        Debug::Log("FileShareModule: Incoming fetch transfer finished. RequestID: {}, Success: {}, Bytes: {}/{}", requestID, success, transferred, totalSize);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, success);
        ConnectionManager::SendEvent(event);
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID         = package->GetValue<size_t>();
        const std::string destination  = package->GetValue<std::string>();
        const std::string fileName     = package->GetValue<std::string>();
        const size_t totalTransferSize = package->GetValue<size_t>();
        const bool isDirectory         = package->GetValue<bool>();
        Debug::Log("FileShareModule: Received transfer post request. RequestID: {}, Destination: {}, Name: {}, IsDirectory: {}, Size: {}",
            requestID, destination, fileName, isDirectory, totalTransferSize);

        const std::filesystem::path destinationPath = std::filesystem::path(destination) / fileName;

        size_t transferChannelIndex{};
        asio::steady_timer timer(m_context);

        while (true) {
            {
                std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
                for (size_t i = 0; i < m_transferChannels.size(); ++i) {
                    const std::shared_ptr<TransferChannel> channel = m_transferChannels[i];
                    if (m_reservedIncomingPostChannels.find(i) != m_reservedIncomingPostChannels.end()) {
                        continue;
                    }

                    if (channel->GetConnectionState() == ConnectionState::CONNECTED && !channel->IsUsed(false)) {
                        transferChannelIndex = i;
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
            m_reservedIncomingPostChannels.erase(transferChannelIndex);
        });
        (void)reservationGuard;
        Debug::Log("FileShareModule: Selected transfer channel {} for incoming post request {}", transferChannelIndex, requestID);

        const auto future = isDirectory ?
            asio::co_spawn(m_context, channel->ReceiveDirectory(destinationPath), asio::use_future) :
            asio::co_spawn(m_context, channel->ReceiveFile(destinationPath), asio::use_future);

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::FILE_SHARE_TRANSFER_POST_RESPONSE,
            static_cast<uint8_t>(transferChannelIndex)
        );
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
	    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::CONNECTION_CHANNEL_CONNECTION_PORT_INFO, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
	        const ModuleState state = GetModuleState();
	        if (state != ModuleState::Enabling && state != ModuleState::Enabled) {
	            Debug::LogWarning(
	                "FileShareModule: Ignoring transfer port info in state {}",
	                static_cast<int>(state)
	            );
	            co_return;
        }

        const IPAddress ip  = ConnectionManager::GetPeerAddress();
        const uint16_t port = package->GetValue<uint16_t>();
        const TCPEndpoint endpoint(ip, port);
        const size_t index = m_transferChannelInitializationIndex.fetch_add(1);

        if (index >= m_transferChannels.size()) {
            Debug::LogWarning(
                "FileShareModule: Received extra transfer port info for index {} (channels: {}), ignoring",
                index,
                m_transferChannels.size()
            );
            co_return;
        }

        const std::shared_ptr<TransferChannel> channel = m_transferChannels[index];

        co_await channel->Connect(endpoint);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED, [instance, this](PC_Package&& package) mutable {
        const bool newState = package->GetValue<bool>();
        m_peerModuleEnabled.store(newState);
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_FETCH_ENTRY_ICON_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID        = package->GetValue<size_t>();
        const FileEntry entry         = package->GetValue<FileEntry>();
        const FileIconDensity density = package->GetValue<FileIconDensity>();

        const std::optional<std::string> name = entry.GetName();
        const std::optional<std::string> path = entry.GetPath();

        if (!path || !name) {
            Debug::LogError("FileShareModule: Missing file path/name for icon request. RequestID: {}", requestID);
            ProcessError(ModuleFailReason::IncorrectConfig);
            co_return;
        }

        const std::filesystem::path filePath = std::filesystem::path(path.value()) / name.value();
        Debug::Log(
            "FileShareModule: Processing entry icon request. RequestID: {}, Path: {}, Density: {}",
            requestID,
            filePath.string(),
            static_cast<int>(density)
        );
        std::vector<uint8_t> icon = GetEntryIcon(filePath.string(), density);
        const size_t iconSize = icon.size();

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::FILE_SHARE_FETCH_ENTRY_ICON_RESPONSE,
            std::move(icon)
        );
        Debug::Log("FileShareModule: Sent entry icon response. RequestID: {}, Bytes: {}", requestID, iconSize);

        co_return;
    });
}

void FileShareModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_FETCH_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::CONNECTION_CHANNEL_CONNECTION_PORT_INFO);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_FETCH_ENTRY_ICON_REQUEST);
}

void FileShareModule::OnInitialize() {
    ClearDirectoryScanFutures();
    {
        std::lock_guard<std::mutex> lock(m_incomingPostReservationMutex);
        m_reservedIncomingPostChannels.clear();
    }
    m_transferChannels.reserve(TRANSFER_CHANNELS_COUNT);
    std::shared_ptr<SSLContext_> sslContext = ConnectionManager::GetSSLContextClient();
    for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
        m_transferChannels.emplace_back(std::make_shared<TransferChannel>());
    }
}

asio::awaitable<void> FileShareModule::OnEnable() {
    m_peerModuleEnabled.store(false);

    ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::FileSystem);
    if (!co_await PermissionManager::RequestManagingExternalStoragePermission()) {
        ConnectionManager::Send(PC_PackageType::PERMISSION_REJECTED, PermissionType::FileSystem);
        Debug::LogWarning("FileShareModule: External storage permission denied; disabling module");
        Disable();
        co_return;
    }
    ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::FileSystem);

    if (ShouldAbortEnable()) {
        co_return;
    }

    m_transferChannelInitializationIndex.store(0);
    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_ENABLE);

    asio::steady_timer timer(m_context);
    while (!m_peerModuleEnabled.load()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }
}

asio::awaitable<void> FileShareModule::OnDisable() {
    m_peerModuleEnabled.store(false);
    ClearDirectoryScanFutures();
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
    ClearDirectoryScanFutures();
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
