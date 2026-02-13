#include <FileShareModule.h>

#include <QGuiApplication>

constexpr size_t TRANSFER_CHANNELS_COUNT = 10;

FileShareModule::FileShareModule() { }

void FileShareModule::FetchDirectoryEntries(const std::string& path, const std::function<void(std::vector<FileEntry>&&)> callback) const {
    asio::co_spawn(m_context, FetchDirectoryEntriesAwaitable(path, std::move(callback)), asio::detached);
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

// Function fetch progress is available via qt events
void FileShareModule::FetchEntry(const std::string& path, const std::string& destination) {
    
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
