#ifndef NETWORK_CAMERA_MODULE_H
#define NETWORK_CAMERA_MODULE_H

#include <BaseModule.h>

class NetworkCameraModule final : public BaseModule {
public:


private:

protected:
    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

};

#endif //NETWORK_CAMERA_MODULE_H