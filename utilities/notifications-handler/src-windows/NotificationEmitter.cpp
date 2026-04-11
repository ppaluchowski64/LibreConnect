#include <NotificationEmitter.h>
#include <ThreadPool.h>
#include <wintoastlib.h>
#include <DebugLog.h>
#include <magic_enum/magic_enum.hpp>
#include <mutex>

namespace {
    std::mutex g_toastMutex;
    std::atomic<bool> g_toastInitialized{ false };
    std::atomic<bool> g_toastInitAttempted{ false };

    bool EnsureToastInitialized() {
        if (g_toastInitialized.load(std::memory_order_acquire))
            return true;

        if (g_toastInitAttempted.exchange(true, std::memory_order_acq_rel))
            return g_toastInitialized.load(std::memory_order_acquire);

        WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();

        constexpr const wchar_t* APPNAME = L"LibreConnect";
        instance->setAppName(APPNAME);
        const auto aumi = WinToastLib::WinToast::configureAUMI(
            L"Default", APPNAME, L"main", L"1.0");
        instance->setAppUserModelId(aumi);

        const bool ok = instance->initialize();
        if (!ok) {
            Debug::LogError("NotificationEmitter: WinToast initialization failed");
        }

        g_toastInitialized.store(ok, std::memory_order_release);
        return ok;
    }
} // namespace

class NotificationHandler : public WinToastLib::IWinToastHandler {
public:
    explicit NotificationHandler(std::vector<NotificationEmitter::ButtonAction> buttons) : actions(std::move(buttons)) {}

    void toastActivated(const int actionIndex) const override {
        if (actionIndex < 0 || static_cast<size_t>(actionIndex) >= actions.size()) {
            return;
        }

        auto action = actions[static_cast<size_t>(actionIndex)].action;
        if (!action) {
            return;
        }

        ThreadPool::Post([action = std::move(action)]() mutable {
            action();
        });
    }

    void toastActivated() const override {}
    void toastActivated(std::wstring response) const override {}
    void toastDismissed(WinToastDismissalReason reason) const override {}
    void toastFailed() const override {}

private:
    std::vector<NotificationEmitter::ButtonAction> actions;

};

int64_t NotificationEmitter::Emit(
    const std::wstring& notificationName,
    const std::wstring& notificationContent,
    const std::optional<std::filesystem::path>& appIconPath,
    const std::optional<std::filesystem::path>& mainImagePath,
    const std::vector<ButtonAction>& buttons) {
    if (!EnsureToastInitialized()) {
        return -1;
    }

    std::lock_guard lock(g_toastMutex);
    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();

    const WinToastLib::WinToastTemplate::WinToastTemplateType templateType = appIconPath
        ? WinToastLib::WinToastTemplate::ImageAndText02
        : WinToastLib::WinToastTemplate::Text02;

    WinToastLib::WinToastTemplate templ(templateType);

    templ.setTextField(notificationName, WinToastLib::WinToastTemplate::FirstLine);
    templ.setTextField(notificationContent, WinToastLib::WinToastTemplate::SecondLine);

    if (appIconPath) {
        templ.setImagePath(appIconPath->wstring());
    }

    if (mainImagePath) {
        templ.setHeroImagePath(mainImagePath->wstring());
    }

    for (const auto& [name, _] : buttons) {
        templ.addAction(name);
    }

    const int64_t toastID = instance->showToast(templ, new NotificationHandler(buttons));
    if (toastID < 0) {
        Debug::LogWarning("NotificationEmitter: showToast failed with id {}", toastID);
    }

    return toastID;
}

void NotificationEmitter::Remove(const int64_t id) {
    if (!EnsureToastInitialized() || id < 0) {
        return;
    }

    std::lock_guard lock(g_toastMutex);
    WinToastLib::WinToast::instance()->hideToast(static_cast<INT64>(id));
}
