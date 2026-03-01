#include <NotificationData.h>

NotificationData::NotificationData() { }

NotificationData::NotificationData(std::string key, std::string title, std::string content, size_t timestamp, std::vector<uint8_t> smallIcon, std::vector<uint8_t> largeIcon)
:   key(std::move(key)), title(std::move(title)), content(std::move(content)),
    timestamp(timestamp), smallIcon(std::move(smallIcon)), largeIcon(std::move(largeIcon)) { }

void NotificationData::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(key, buffer, offset);
    SerializeObject(title, buffer, offset);
    SerializeObject(content, buffer, offset);
    SerializeObject(timestamp, buffer, offset);
    SerializeObject(smallIcon, buffer, offset);
    SerializeObject(largeIcon, buffer, offset);
}

void NotificationData::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(key, buffer, offset);
    DeserializeObject(title, buffer, offset);
    DeserializeObject(content, buffer, offset);
    DeserializeObject(timestamp, buffer, offset);
    DeserializeObject(smallIcon, buffer, offset);
    DeserializeObject(largeIcon, buffer, offset);
}

size_t NotificationData::GetSerializedSize() const {
    return GetObjectSerializedSize(key) + GetObjectSerializedSize(title) + GetObjectSerializedSize(content) +
        GetObjectSerializedSize(timestamp) + GetObjectSerializedSize(smallIcon) + GetObjectSerializedSize(largeIcon);
}
