#ifndef NOTIFICATIONLISTENER_KT_NOTIFICATIONEMITTER_H
#define NOTIFICATIONLISTENER_KT_NOTIFICATIONEMITTER_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

class NotificationEmitter {
public:
    struct ButtonAction {
        std::wstring text;
        std::function<void()> action;
    };

    static int64_t Emit(
        const std::wstring& notificationName,
        const std::wstring& notificationContent,
        const std::optional<std::filesystem::path>& appIconPath,
        const std::optional<std::filesystem::path>& mainImagePath,
        std::vector<ButtonAction> buttons
    );

    static void Remove(int64_t id);

};

#endif // NOTIFICATIONLISTENER_KT_NOTIFICATIONEMITTER_H
