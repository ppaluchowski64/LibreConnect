#include <NotificationData.h>

NotificationData::NotificationData() { }

NotificationData::NotificationData(
    std::string key,
    std::string title,
    std::string content,
    size_t timestamp,
    std::vector<std::string> buttons,
    std::vector<uint8_t> smallIcon,
    std::vector<uint8_t> largeIcon,
    std::vector<uint8_t> mainImage
) :
    key(std::move(key)),
    title(std::move(title)),
    content(std::move(content)),
    timestamp(timestamp),
    buttons(std::move(buttons)),
    smallIcon(std::move(smallIcon)),
    largeIcon(std::move(largeIcon)),
    mainImage(std::move(mainImage))
{ }

void NotificationData::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(key, buffer, offset);
    SerializeObject(title, buffer, offset);
    SerializeObject(content, buffer, offset);
    SerializeObject(timestamp, buffer, offset);
    SerializeObject(buttons, buffer, offset);
    SerializeObject(smallIcon, buffer, offset);
    SerializeObject(largeIcon, buffer, offset);
    SerializeObject(mainImage, buffer, offset);
}

void NotificationData::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(key, buffer, offset);
    DeserializeObject(title, buffer, offset);
    DeserializeObject(content, buffer, offset);
    DeserializeObject(timestamp, buffer, offset);
    DeserializeObject(buttons, buffer, offset);
    DeserializeObject(smallIcon, buffer, offset);
    DeserializeObject(largeIcon, buffer, offset);
    DeserializeObject(mainImage, buffer, offset);
}

size_t NotificationData::GetSerializedSize() const {
    return GetObjectSerializedSize(key) + GetObjectSerializedSize(title) + GetObjectSerializedSize(content) +
        GetObjectSerializedSize(timestamp) + GetObjectSerializedSize(buttons) + GetObjectSerializedSize(smallIcon) +
        GetObjectSerializedSize(largeIcon) + GetObjectSerializedSize(mainImage);
}
