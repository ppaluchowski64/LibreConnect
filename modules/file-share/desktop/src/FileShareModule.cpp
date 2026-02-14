#include <stack>

#include <FileShareModule.h>
#include <FileEntry.h>
#include <QGuiApplication>
#include <TransferInfo.h>

constexpr size_t TRANSFER_CHANNELS_COUNT = 10;

FileShareModule::FileShareModule() { }

void FileShareModule::FetchDirectoryEntries(const FileEntry& entry, std::function<void(std::vector<FileEntry>&&)> callback) const {
    const std::string path = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();

    if (path.empty()) {
        QMetaObject::invokeMethod(
            QGuiApplication::instance(),
            [callback = std::move(callback)]() mutable {
                callback(std::vector<FileEntry>{});
            },
            Qt::QueuedConnection
        );

        return;
    }

    asio::co_spawn(m_context, FetchDirectoryEntriesAwaitable(std::move(path), std::move(callback)), asio::detached);
}

asio::awaitable<void> FileShareModule::FetchDirectoryEntriesAwaitable(std::string path, std::function<void(std::vector<FileEntry>&&)> callback) const {
    const std::shared_ptr<const BaseModule> instance = shared_from_this();

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_DIRECTORY_ENTRIES_REQUEST, std::move(path));
    std::vector<FileEntry> entries;

    if (response) {
        response.value()->GetValue(entries);
    }

    QMetaObject::invokeMethod(
        QGuiApplication::instance(),
        [callback = std::move(callback), entries = std::move(entries)]() mutable {
            callback(std::move(entries));
        },
        Qt::QueuedConnection
    );
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
void FileShareModule::FetchEntry(const FileEntry& entry, const std::string& destination) {
    asio::co_spawn(m_context, FetchEntryAwaitable(entry, destination), asio::detached);
}

asio::awaitable<void> FileShareModule::FetchEntryAwaitable(FileEntry entry, std::string destination) {
    const std::string entryPath = entry.GetPath().has_value() ? entry.GetPath().value() : std::string();
    if (entryPath.empty()) {
        // TODO
        // THROW SOME EVENT
        co_return;
    }

    std::filesystem::path filePath(destination);
    if (std::filesystem::is_directory(filePath)) {
        // TODO
        // THROW SOME EVENT
        co_return;
    }

    const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::FILE_SHARE_TRANSFER_ENTRY_REQUEST, entry);
    if (response) {
        // TODO
        // THROW SOME EVENT
        co_return;
    }

    const TransferInfo transferInfo = response.value()->GetValue<TransferInfo>();
    if (m_transferChannels.size() >= transferInfo.channel) {
        Debug::LogError("Transfer channel {} doesn't exists", transferInfo.channel);
        // TODO
        // SEND SOME ERROR
        co_return;
    }

    TransferChannel* channel = m_transferChannels[transferInfo.channel].get();
    if (channel->IsUsed(false)) {
        Debug::LogError("Transfer channel {} is in use", transferInfo.channel);
        // TODO
        // SEND SOME ERROR
        co_return;
    }

    uuid tempFileName = boost::uuids::random_generator()();
    std::filesystem::path tempFile = std::filesystem::temp_directory_path() / boost::uuids::to_string(tempFileName);

    co_await channel->Receive(tempFile, transferInfo.size);

}

void FileShareModule::CopyEntryToClipboard(const std::string& path) {

}

void FileShareModule::PostEntry(const std::string& path, const std::string& destination) {

}

void FileShareModule::PasteEntryFromClipboard(const std::string& path, const std::string& destination) {

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
