#ifndef CLIPBOARDSYNCMODULE_H
#define CLIPBOARDSYNCMODULE_H

#include <BaseModule.h>
#include <chrono>
#include <mutex>
#include <string>

class ClipboardSyncModule : public BaseModule {
public:
    void RequestSyncWithPeer() const;
    void RequestSyncWithPeer(std::string localClipboardText) const;

    void SendLocalClipboard() const;
    void SendLocalClipboard(std::string localClipboardText) const;

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
    void SendClipboardText(std::string text) const;
    void SendLocalClipboardSnapshot() const;

    asio::awaitable<void> SendClipboardTextAwaitable(std::string text) const;

    mutable std::mutex m_clipboardStateMutex;
    mutable std::string m_lastLocalClipboardSent;
    mutable std::string m_lastRemoteClipboardApplied;
    mutable std::chrono::steady_clock::time_point m_lastRemoteClipboardAppliedAt{};

    std::string m_buffer{};
    std::atomic<size_t> m_leftFragments{0};
    IOContextStrand m_clipboardModificationStrand{ThreadPool::GetContext().get_executor()};
};

#endif // CLIPBOARDSYNCMODULE_H
