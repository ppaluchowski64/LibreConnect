#include <FileShareModule.h>
#include <FileEntry.h>
#include <FileSystemManager.h>
#include <HashHelpers.h>
#include <FileShareEvents.h>
#include <PermissionManager.h>

#include <QDesktopServices>
#include <QUrl>
#include <magic_enum/magic_enum.hpp>

constexpr size_t TRANSFER_CHANNELS_COUNT = 10;
constexpr size_t PROGRESS_EVENT_DELAY_MS = 100;

FileShareModule::FileShareModule() = default;

void FileShareModule::PostEntry(const std::filesystem::path& path, const std::filesystem::path& destination) const {
    asio::co_spawn(m_context, PostEntryAwaitable(path, destination), asio::detached);
}

asio::awaitable<void> FileShareModule::PostEntryAwaitable(const std::filesystem::path path, const std::filesystem::path destination) const {
    Debug::Log("Post entry requested. Source: {}, Destination: {}", path.string(), destination.string());
    if (!std::filesystem::exists(path)) {
        Debug::LogError("File {} does not exist", path.string());
        co_return;
    }

    FileEntry entry(path);

    if (!std::filesystem::is_directory(destination)) {
        Debug::LogError("Destination should be a directory ({})", destination.string());
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
    Debug::Log("Post entry prepared. IsDirectory: {}, Size: {}", isDirectory, totalTransferSize);

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, destination.string(), path.filename().string(), size_t{totalTransferSize}, isDirectory);
    if (!response.has_value()) {
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const uint8_t channelIndex = response.value()->GetValue<uint8_t>();
    Debug::Log("Post entry accepted. Channel: {}", channelIndex);

    if (channelIndex >= TRANSFER_CHANNELS_COUNT) {
        Debug::LogError("Transfer channel index {} is out of range", channelIndex);
        ConnectionManager::Disconnect();
        co_return;
    }

    const std::shared_ptr<TransferChannel> channel = m_transferChannels[channelIndex];

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
    Debug::Log("Post entry transfer finished. Success: {}, Bytes: {}/{}", success, transferred, totalTransferSize);
    const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, success);
    ConnectionManager::SendEvent(event);
}

void FileShareModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

	ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST, [instance, this](PC_Package&& package) mutable {
	    const size_t requestID    = package->GetValue<size_t>();
	    const std::string pathStr = package->GetValue<std::string>();
        Debug::Log("Received directory entries request. RequestID: {}, Path: {}", requestID, pathStr);

	    const std::filesystem::path path(pathStr);
	    auto [entries, success] = FileSystemManager::GetEntries(path);
	    ConnectionManager::SendRequestResponse(requestID, PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_RESPONSE, std::move(entries));
	});
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_FETCH_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID = package->GetValue<size_t>();
        const FileEntry entry  = package->GetValue<FileEntry>();
        Debug::Log("Received transfer fetch request. RequestID: {}", requestID);

        if (!entry.GetPath().has_value() || !entry.GetName().has_value()) {
            Debug::LogError("Missing file path");
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
        Debug::Log("Prepared fetch transfer. RequestID: {}, Path: {}, IsDirectory: {}, Size: {}", requestID, path.string(), isDirectory, totalSize);

        uint8_t transferChannelIndex{};
        asio::steady_timer timer(m_context);

        while (true) {
            for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
                const std::shared_ptr<TransferChannel> channel = m_transferChannels[transferChannelIndex];
                if (!channel->IsUsed(true)) {
                    transferChannelIndex = i;
                    goto FINISH_CHANNEL_SEARCH;
                }
            }

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }

        FINISH_CHANNEL_SEARCH:
        const std::shared_ptr<TransferChannel> channel = m_transferChannels[transferChannelIndex];
        Debug::Log("Selected transfer channel {} for incoming fetch request {}", transferChannelIndex, requestID);

        const auto future = isDirectory ?
            asio::co_spawn(m_context, channel->SendDirectory(path), asio::use_future) :
            asio::co_spawn(m_context, channel->SendFile(path), asio::use_future);

        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::FILE_SHARE_TRANSFER_FETCH_RESPONSE, transferChannelIndex, totalSize);

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
        Debug::Log("Incoming fetch transfer finished. RequestID: {}, Success: {}, Bytes: {}/{}", requestID, success, transferred, totalSize);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, success);
        ConnectionManager::SendEvent(event);
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID         = package->GetValue<size_t>();
        const std::string destination  = package->GetValue<std::string>();
        const std::string fileName     = package->GetValue<std::string>();
        const size_t totalTransferSize = package->GetValue<size_t>();
        const bool isDirectory         = package->GetValue<bool>();
        Debug::Log("Received transfer post request. RequestID: {}, Destination: {}, Name: {}, IsDirectory: {}, Size: {}",
            requestID, destination, fileName, isDirectory, totalTransferSize);

        const std::filesystem::path destinationPath = std::filesystem::path(destination) / fileName;

        uint8_t transferChannelIndex{};
        asio::steady_timer timer(m_context);

        while (true) {
            for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
                const std::shared_ptr<TransferChannel> channel = m_transferChannels[transferChannelIndex];
                if (!channel->IsUsed(true)) {
                    transferChannelIndex = i;
                    goto FINISH_CHANNEL_SEARCH;
                }
            }

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }

        FINISH_CHANNEL_SEARCH:
        std::shared_ptr<TransferChannel> channel = m_transferChannels[transferChannelIndex];
        Debug::Log("Selected transfer channel {} for incoming post request {}", transferChannelIndex, requestID);

        const auto future = isDirectory ?
            asio::co_spawn(m_context, channel->SendDirectory(destinationPath), asio::use_future) :
            asio::co_spawn(m_context, channel->SendFile(destinationPath), asio::use_future);

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
        Debug::Log("Incoming post transfer finished. RequestID: {}, Success: {}, Bytes: {}/{}", requestID, success, transferred, totalTransferSize);
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, success);
        ConnectionManager::SendEvent(event);
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::CONNECTION_CHANNEL_CONNECTION_PORT_INFO, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        co_await EnableAwaitable(true);

        const IPAddress ip  = ConnectionManager::GetPeerAddress();
        const uint16_t port = package->GetValue<uint16_t>();
        const TCPEndpoint endpoint(ip, port);
        const uint8_t index = m_transferChannelInitializationIndex.fetch_add(1);

        const std::shared_ptr<TransferChannel> channel = m_transferChannels[index];
        co_await channel->Connect(endpoint);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE, [instance, this](PC_Package&& package) mutable {
        Disable();
    });
}

void FileShareModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_FETCH_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::CONNECTION_CHANNEL_CONNECTION_PORT_INFO);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::FILE_SHARE_MODULE_DISABLE);
}

void FileShareModule::OnInitialize() {
    m_transferChannels.reserve(TRANSFER_CHANNELS_COUNT);
    std::shared_ptr<SSLContext> sslContext = ConnectionManager::GetSSLContextClient();
    for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
        m_transferChannels.emplace_back(std::make_shared<TransferChannel>(sslContext, m_context));
    }
}

asio::awaitable<void> FileShareModule::OnEnable() {
    if (!co_await PermissionManager::RequestManagingExternalStoragePermission()) {
        Disable();
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::FILE_SHARE_MODULE_ENABLE);
    m_transferChannelInitializationIndex.store(0);
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
