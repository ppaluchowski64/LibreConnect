#include <NotificationEmitter.h>
#include <ThreadPool.h>
#include <wintoastlib.h>
#include <DebugLog.h>
#include <magic_enum/magic_enum.hpp>
#include <mutex>

namespace {
std::mutex g_toastMutex;
std::once_flag g_toastInitFlag;
bool g_toastInitialized = false;

bool EnsureToastInitialized() {
    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();
    std::call_once(g_toastInitFlag, [instance]() {
        constexpr const wchar_t* APPNAME = L"LibreConnect";
        instance->setAppName(APPNAME);

        const auto aumi = WinToastLib::WinToast::configureAUMI(
            L"Default",
            APPNAME,
            L"main",
            L"1.0"
        );

        instance->setAppUserModelId(aumi);
        g_toastInitialized = instance->initialize();
        if (!g_toastInitialized) {
            Debug::LogError("NotificationEmitter: WinToast initialization failed");
        }
    });

    return g_toastInitialized;
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
    std::lock_guard lock(g_toastMutex);

    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();
    if (!EnsureToastInitialized()) {
        return -1;
    }

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

    const auto handler = new NotificationHandler(buttons);
    const int64_t toastID = instance->showToast(templ, handler);
    if (toastID < 0) {
        Debug::LogWarning("NotificationEmitter: WinToast showToast failed with id {}", toastID);
    }

    return toastID;
}

void NotificationEmitter::Remove(const int64_t id) {
    std::lock_guard lock(g_toastMutex);
    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();

    if (instance && EnsureToastInitialized() && id >= 0) {
        instance->hideToast(static_cast<INT64>(id));
    }
}
