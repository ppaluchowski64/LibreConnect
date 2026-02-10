#ifndef FILE_SHARE_MODULE_H
#define FILE_SHARE_MODULE_H

#include <BaseModule.h>

class FileShareModule final : public BaseModule {
public:


private:


protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;
};

#endif //FILE_SHARE_MODULE_H
