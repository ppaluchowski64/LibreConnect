#ifndef FILE_SHARE_MODULE_H
#define FILE_SHARE_MODULE_H

#include <vector>
#include <memory>

#include <BaseModule.h>
#include <TransferChannel.h>
#include <ConnectionManager.h>

#include <FileSystemManager.h>

class FileShareModule final : public BaseModule {
public:
    explicit FileShareModule();

    void FetchDirectoryEntries(const FileEntry& entry, std::function<void(std::vector<FileEntry>&&)> callback) const;
    void FetchEntry(const FileEntry& entry, const std::string& destination);
    void CopyEntryToClipboard(const std::string& path);

    void PostEntry(const std::string& path, const std::string& destination);
    void PasteEntryFromClipboard(const std::string& path, const std::string& destination);

private:
    asio::awaitable<void> FetchDirectoryEntriesAwaitable(std::string path, std::function<void(std::vector<FileEntry>&&)> callback) const;
    asio::awaitable<std::vector<FileEntry>> FetchDirectoryEntriesAwaitable(std::string path);

    asio::awaitable<void> FetchEntryAwaitable(FileEntry entry, std::string destination);

    std::vector<std::shared_ptr<TransferChannel>> m_transferChannels;

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;
};

#endif //FILE_SHARE_MODULE_H
