#include <NotificationEmitter.h>
#include <ThreadPool.h>
#include <wintoastlib.h>
#include <DebugLog.h>
#include <magic_enum/magic_enum.hpp>

class NotificationHandler : public WinToastLib::IWinToastHandler {
public:
    explicit NotificationHandler(std::vector<NotificationEmitter::ButtonAction>&& buttons) : actions(std::move(buttons)) {}

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
    std::vector<ButtonAction> buttons) {
    const std::wstring APPNAME = L"LibreConnect";

    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();
    instance->setAppName(APPNAME);

    // TEMP
    const auto aumi = WinToastLib::WinToast::configureAUMI(
        L"Default",
        APPNAME,
        L"main",
        L"1.0"
    );

    instance->setAppUserModelId(aumi);

    if (!instance->initialize()) {
        return 0;
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

    const auto handler = new NotificationHandler(std::move(buttons));
    return instance->showToast(templ, handler);
}

void NotificationEmitter::Remove(const int64_t id) {
    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();

    if (instance && id >= 0) {
        instance->hideToast(static_cast<INT64>(id));
    }
}
