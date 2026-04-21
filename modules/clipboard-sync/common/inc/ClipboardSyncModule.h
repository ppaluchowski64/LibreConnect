#ifndef CLIPBOARDSYNCMODULE_H
#define CLIPBOARDSYNCMODULE_H

#include <BaseModule.h>



class ClipboardSyncModule : public BaseModule {
public:
    void RequestSyncWithPeer();

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;

private:
    void SendLocalClipboardSnapshot() const;
};

#endif // CLIPBOARDSYNCMODULE_H
