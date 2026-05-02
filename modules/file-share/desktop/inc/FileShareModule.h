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
    std::vector<std::filesystem::path> PrepareEntriesForExternalDrag(std::vector<FileEntry> entries) const;
    void PostEntry(const std::filesystem::path& path, const std::filesystem::path& destination) const;
    void PasteEntryFromClipboard(const std::filesystem::path& destination) const;
    void OpenEntry(const FileEntry& entry) const;
    void FetchEntryIcon(const FileEntry& entry, FileIconDensity density) const;

private:
    struct AcquiredTransferChannel {
        size_t index{};
        std::shared_ptr<TransferChannel> channel;
        std::shared_ptr<void> reservationGuard;
    };

    bool TryBeginDirectoryRequest(const std::string& path) const;
    void EndDirectoryRequest(const std::string& path) const;
    asio::awaitable<AcquiredTransferChannel> AcquireTransferChannel(bool reserveIncomingPost = false) const;

    asio::awaitable<void> FetchDirectoryEntriesAwaitable(std::string path) const;
    asio::awaitable<void> FetchEntryAwaitable(FileEntry entry, std::string destination) const;
    asio::awaitable<void> PostEntryAwaitable(std::filesystem::path path, std::filesystem::path destination) const;
    asio::awaitable<void> OpenEntryAwaitable(FileEntry entry) const;
    asio::awaitable<std::vector<std::filesystem::path>> PrepareEntriesForExternalDragAwaitable(std::vector<FileEntry> entries) const;
    static asio::awaitable<void> FetchEntryIconAwaitable(FileEntry entry, FileIconDensity density);

    std::vector<std::shared_ptr<TransferChannel>> m_transferChannels;
    mutable std::mutex m_directoryRequestMutex;
    mutable std::unordered_set<std::string> m_inFlightDirectoryRequests;
    mutable std::mutex m_incomingPostReservationMutex;
    mutable std::unordered_set<size_t> m_reservedIncomingPostChannels;

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
