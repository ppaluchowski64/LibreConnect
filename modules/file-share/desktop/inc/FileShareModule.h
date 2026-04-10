#ifndef FILE_SHARE_MODULE_H
#define FILE_SHARE_MODULE_H

#include <vector>
#include <memory>
#include <mutex>
#include <unordered_set>

#include <BaseModule.h>
#include <TransferChannel.h>
#include <ConnectionManager.h>

#include <FileSystemManager.h>
#include <FileIconDensity.h>

class FileShareModule final : public BaseModule {
public:
    explicit FileShareModule();

    void FetchDirectoryEntries(const std::string& path) const;
    void FetchDirectoryEntries(const FileEntry& entry) const;
    void FetchEntry(const FileEntry& entry, const std::string& destination) const;
    // Moving entries with std::move is preferred
    void CopyEntriesToClipboard(std::vector<FileEntry> entries) const;
    void PostEntry(const std::filesystem::path& path, const std::filesystem::path& destination) const;
    void PasteEntryFromClipboard(const std::filesystem::path& destination) const;
    void OpenEntry(const FileEntry& entry) const;
    void FetchEntryIcon(const FileEntry& entry, FileIconDensity density) const;

private:
    bool TryBeginDirectoryRequest(const std::string& path) const;
    void EndDirectoryRequest(const std::string& path) const;

    asio::awaitable<void> FetchDirectoryEntriesAwaitable(std::string path) const;
    asio::awaitable<std::vector<FileEntry>> FetchDirectoryEntriesAwaitable(std::string path);
    asio::awaitable<void> FetchEntryAwaitable(FileEntry entry, std::string destination) const;
    asio::awaitable<void> PostEntryAwaitable(std::filesystem::path path, std::filesystem::path destination) const;
    asio::awaitable<void> OpenEntryAwaitable(FileEntry entry) const;
    static asio::awaitable<void> FetchEntryIconAwaitable(FileEntry entry, FileIconDensity density);

    std::vector<std::shared_ptr<TransferChannel>> m_transferChannels;
    mutable std::mutex m_directoryRequestMutex;
    mutable std::unordered_set<std::string> m_inFlightDirectoryRequests;

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;
};

#endif //FILE_SHARE_MODULE_H

