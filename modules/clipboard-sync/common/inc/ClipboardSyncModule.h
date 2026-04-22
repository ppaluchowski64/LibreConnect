#ifndef CLIPBOARDSYNCMODULE_H
#define CLIPBOARDSYNCMODULE_H

#include <BaseModule.h>
#include <chrono>
#include <mutex>
#include <string>



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
    static std::string NormalizeClipboardText(std::string text);
    void SendLocalClipboardSnapshot() const;

    mutable std::mutex m_clipboardStateMutex;
    mutable std::string m_lastLocalClipboardSent;
    mutable std::string m_lastRemoteClipboardApplied;
    mutable std::chrono::steady_clock::time_point m_lastRemoteClipboardAppliedAt{};
};

#endif // CLIPBOARDSYNCMODULE_H
