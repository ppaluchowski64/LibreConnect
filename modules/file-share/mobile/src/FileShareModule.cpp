#include <FileShareModule.h>
#include <FileEntry.h>
#include <FileSystemManager.h>
#include <HashHelpers.h>
#include <FileShareEvents.h>

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
    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, destination.string(), path.filename().string(), isDirectory);
    if (!response.has_value()) {
        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, false);
        ConnectionManager::SendEvent(event);
        co_return;
    }

    const uint8_t channelIndex = response.value()->GetValue<uint8_t>();
    const size_t totalTransferSize = response.value()->GetValue<size_t>();

    if (channelIndex >= TRANSFER_CHANNELS_COUNT) {
        Debug::LogError("Transfer channel index {} is out of range", channelIndex);
        ConnectionManager::Disconnect();
        co_return;
    }

    TransferChannel* channel = m_transferChannels[channelIndex].get();


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

    const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, totalTransferSize == channel->FetchTransferProgress());
    ConnectionManager::SendEvent(event);
}

void FileShareModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

	ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST, [instance, this](PC_Package&& package) mutable {
	    const size_t requestID    = package->GetValue<size_t>();
	    const std::string pathStr = package->GetValue<std::string>();

	    const std::filesystem::path path(pathStr);
	    auto [entries, success] = FileSystemManager::GetEntries(path);
	    ConnectionManager::SendRequestResponse(requestID, PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_RESPONSE, std::move(entries));
	});
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_FETCH_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID = package->GetValue<size_t>();
        const FileEntry entry  = package->GetValue<FileEntry>();

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

        uint8_t transferChannelIndex{};
        asio::steady_timer timer(m_context);

        while (true) {
            for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
                const TransferChannel* channel = m_transferChannels[i].get();
                if (!channel->IsUsed(true)) {
                    transferChannelIndex = i;
                    goto FINISH_CHANNEL_SEARCH;
                }
            }

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }

        FINISH_CHANNEL_SEARCH:

        TransferChannel* channel = m_transferChannels[transferChannelIndex].get();
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

        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, totalSize == channel->FetchTransferProgress());
        ConnectionManager::SendEvent(event);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::FILE_SHARE_TRANSFER_POST_REQUEST, [instance, this](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID         = package->GetValue<size_t>();
        const std::string destination  = package->GetValue<std::string>();
        const std::string fileName     = package->GetValue<std::string>();
        const size_t totalTransferSize = package->GetValue<size_t>();
        const bool isDirectory         = package->GetValue<bool>();

        const std::filesystem::path destinationPath = std::filesystem::path(destination) / fileName;

        uint8_t transferChannelIndex{};
        asio::steady_timer timer(m_context);

        while (true) {
            for (int i = 0; i < TRANSFER_CHANNELS_COUNT; ++i) {
                const TransferChannel* channel = m_transferChannels[i].get();
                if (!channel->IsUsed(true)) {
                    transferChannelIndex = i;
                    goto FINISH_CHANNEL_SEARCH;
                }
            }

            timer.expires_after(std::chrono::milliseconds(PROGRESS_EVENT_DELAY_MS));
            co_await timer.async_wait();
        }

        FINISH_CHANNEL_SEARCH:
        TransferChannel* channel = m_transferChannels[transferChannelIndex].get();

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

        const std::unique_ptr<QEvent> event = std::make_unique<EntryTransferResultEvent>(entry, totalTransferSize == channel->FetchTransferProgress());
        ConnectionManager::SendEvent(event);
    });
}

void FileShareModule::DisableResponseCallbacks() {

}

void FileShareModule::OnInitialize() {
    m_transferChannels.reserve(TRANSFER_CHANNELS_COUNT);
    std::shared_ptr<SSLContext> sslContext = ConnectionManager::GetSSLContext();
}

asio::awaitable<void> FileShareModule::OnEnable() {
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
