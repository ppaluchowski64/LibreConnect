#ifndef NT_NOTIFICATION_DATA_H
#define NT_NOTIFICATION_DATA_H

#include <filesystem>
#include <Packable.h>
#include <string>
#include <vector>

struct NotificationPacket {
    std::string key;
    std::string title;
    std::string content;
    size_t timestamp{0};
    std::vector<std::string> buttons;
    std::vector<uint8_t> mainImage;
    std::vector<uint8_t> iconImage;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    size_t GetSerializedSize() const;
};

struct NotificationRecord {
    std::string key;
    std::string title;
    std::string content;
    size_t timestamp{0};
    std::vector<std::string> buttons;

    std::optional<std::filesystem::path> mainImagePath;
    std::optional<std::filesystem::path> iconPath;
};

struct NotificationData {
public:
    NotificationData();
    NotificationData(
        std::string key,
        std::string title,
        std::string content,
        size_t timestamp,
        std::vector<std::string> buttons,
        std::vector<uint8_t> smallIcon,
        std::vector<uint8_t> largeIcon,
        std::vector<uint8_t> mainImage
    );

    std::string key;
    std::string title;
    std::string content;
    size_t timestamp{0};
    std::vector<std::string> buttons;
    std::vector<uint8_t> smallIcon; // PNG compression
    std::vector<uint8_t> largeIcon; // PNG compression
    std::vector<uint8_t> mainImage; // PNG compression
};

#endif // NT_NOTIFICATION_DATA_H