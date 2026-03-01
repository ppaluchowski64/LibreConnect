#ifndef NT_NOTIFICATION_DATA_H
#define NT_NOTIFICATION_DATA_H

#include <Packable.h>
#include <string>
#include <vector>

struct NotificationData {
public:
    NotificationData();
    NotificationData(std::string key, std::string title, std::string content, size_t timestamp, std::vector<uint8_t> smallIcon, std::vector<uint8_t> largeIcon);

    std::string key;
    std::string title;
    std::string content;
    size_t timestamp{0};
    std::vector<uint8_t> smallIcon; // PNG compression
    std::vector<uint8_t> largeIcon; // PNG compression
    std::vector<uint8_t> mainImage; // PNG compression

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    size_t GetSerializedSize() const;
};

#endif // NT_NOTIFICATION_DATA_H