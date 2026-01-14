#include <NetworkCameraModule.h>

void NetworkCameraModule::OnInitialize() {
    AddThreads(1);
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {

}

asio::awaitable<void> NetworkCameraModule::OnDisable() {

}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {

}
