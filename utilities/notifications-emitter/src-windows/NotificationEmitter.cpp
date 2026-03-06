#include <NotificationEmitter.h>
#include <wintoastlib.h>

class NotificationHandler : public WinToastLib::IWinToastHandler {
public:
    void toastActivated() const override {}
    void toastActivated(int actionIndex) const override {}
    void toastActivated(std::wstring response) const override {}
    void toastDismissed(WinToastDismissalReason) const override {}
    void toastFailed() const override {}
};

uint64_t NotificationEmitter::Emit(
    const std::wstring& notificationName,
    const std::wstring& notificationContent,
    const std::optional<std::filesystem::path>& appIconPath,
    const std::optional<std::filesystem::path>& mainImagePath,
    const std::vector<std::wstring>& buttons) {

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

    // Buttons
    for (const auto& btn : buttons) {
        templ.addAction(btn);
    }

    const auto handler = new NotificationHandler();
    const INT64 id = instance->showToast(templ, handler);

    return static_cast<uint64_t>(id);

}

void NotificationEmitter::Remove(const uint64_t id) {
    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();

    if (instance && id > 0) {
        instance->hideToast(static_cast<INT64>(id));
    }
}
