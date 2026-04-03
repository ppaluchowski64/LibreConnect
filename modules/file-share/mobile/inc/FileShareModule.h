#ifndef FILE_SHARE_MODULE_H
#define FILE_SHARE_MODULE_H

#include <vector>
#include <memory>

#include <BaseModule.h>
#include <TransferChannel.h>
#include <ConnectionManager.h>

class FileShareModule final : public BaseModule {
public:
    explicit FileShareModule();
    void PostEntry(const std::filesystem::path& path, const std::filesystem::path& destination) const;

private:
    asio::awaitable<void> PostEntryAwaitable(std::filesystem::path path, std::filesystem::path destination) const;

    std::atomic_size_t m_transferChannelInitializationIndex;
    std::vector<std::shared_ptr<TransferChannel>> m_transferChannels;

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

