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

private:
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
