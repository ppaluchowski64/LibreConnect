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
    const std::wstring& appName,
    const std::wstring& notificationName,
    const std::wstring& notificationContent,
    const std::optional<std::filesystem::path>& appIconPath,
    const std::optional<std::filesystem::path>& mainImagePath,
    const std::vector<std::string>& buttons) {

    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();
    instance->setAppName(appName);

    if (!instance->initialize()) {
        return 0;
    }

    WinToastLib::WinToastTemplate templ(WinToastLib::WinToastTemplate::ImageAndText02);
    templ.setTextField(std::wstring(notificationName.begin(), notificationName.end()), WinToastLib::WinToastTemplate::FirstLine);
    templ.setTextField(std::wstring(notificationContent.begin(), notificationContent.end()), WinToastLib::WinToastTemplate::SecondLine);

    // App icon
    if (appIconPath) {
        templ.setImagePath(std::wstring(appIconPath->wstring()));
    }

    // Hero image
    if (mainImagePath) {
        templ.setHeroImagePath(std::wstring(mainImagePath->wstring()));
    }

    // Buttons
    for (const auto& btn : buttons) {
        templ.addAction(std::wstring(btn.begin(), btn.end()));
    }

    const auto handler = new NotificationHandler();
    const INT64 id = instance->showToast(templ, handler);

    return static_cast<uint64_t>(id);

}
