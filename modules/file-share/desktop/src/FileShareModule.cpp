#include <FileShareModule.h>

void FileShareModule::EnableResponseCallbacks() {

}

void FileShareModule::DisableResponseCallbacks() {

}

void FileShareModule::OnInitialize() {

}

asio::awaitable<void> FileShareModule::OnEnable() {
    co_return;
}

asio::awaitable<void> FileShareModule::OnDisable() {
    co_return;
}

asio::awaitable<void> FileShareModule::OnShutdown() {
    co_return;
}
