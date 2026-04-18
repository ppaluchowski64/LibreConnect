#ifndef REMOTEINPUTMODULE_H
#define REMOTEINPUTMODULE_H

#include <BaseModule.h>
#include <Keyboard.h>
#include <MediaRemote.h>

class RemoteInputModule : public BaseModule {
private:
    Keyboard m_keyboard{};
    MediaRemote m_remote{};

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

#endif // REMOTEINPUTMODULE_H
