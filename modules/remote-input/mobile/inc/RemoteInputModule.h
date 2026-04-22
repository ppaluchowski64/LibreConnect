#ifndef REMOTEINPUTMODULE_H
#define REMOTEINPUTMODULE_H

#include <BaseModule.h>
#include <InputTypes.h>

class RemoteInputModule : public BaseModule {
public:
    static void SendInput(Key key, InputEventType type);
    static void SendMediaInput(MediaSignal signal);
    static void RequestMediaInfo();
    static void SetMediaPosition(double seconds);

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
