#ifndef NOTIFICATIONLISTENER_KT_NOTIFICATIONEMITTER_H
#define NOTIFICATIONLISTENER_KT_NOTIFICATIONEMITTER_H

#include <filesystem>
#include <string>

class NotificationEmitter {
public:
    static uint64_t Emit(
        const std::wstring& appName,
        const std::wstring& notificationName,
        const std::wstring& notificationContent,
        const std::optional<std::filesystem::path>& appIconPath,
        const std::optional<std::filesystem::path>& mainImagePath,
        const std::vector<std::string>& buttons
    );

    static void Remove(uint64_t id);
};

#endif // NOTIFICATIONLISTENER_KT_NOTIFICATIONEMITTER_H