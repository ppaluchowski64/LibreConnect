#ifndef FILE_SHARE_MODULE_H
#define FILE_SHARE_MODULE_H

#include <vector>
#include <memory>
#include <future>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <BaseModule.h>
#include <TransferChannelPool.h>
#include <ConnectionManager.h>
#include <FileIconDensity.h>
#include <FileSystemManager.h>

class FileShareModule final : public BaseModule {
public:
    FileShareModule() = default;
    void PostEntry(const std::filesystem::path& path, const std::filesystem::path& destination, bool notifyTransferProgress = false) const;

private:
    std::shared_future<DirectoryResult> GetOrCreateDirectoryScanFuture(const std::string& path);
    void CleanupDirectoryScanFutureIfReady(const std::string& path);
    void ClearDirectoryScanFutures();

    asio::awaitable<void> PostEntryAwaitable(std::filesystem::path path, std::filesystem::path destination, bool notifyTransferProgress) const;
    std::vector<uint8_t> GetEntryIcon(const std::string& file, FileIconDensity density);

    std::mutex m_directoryScanMutex;
    std::unordered_map<std::string, std::shared_future<DirectoryResult>> m_directoryScanFutures;

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
