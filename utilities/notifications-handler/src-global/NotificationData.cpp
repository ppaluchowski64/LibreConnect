#include <NotificationData.h>
#include <Packable.h>

void NotificationPacket::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(key, buffer, offset);
    SerializeObject(appName, buffer, offset);
    SerializeObject(title, buffer, offset);
    SerializeObject(content, buffer, offset);
    SerializeObject(timestamp, buffer, offset);
    SerializeObject(dismissable, buffer, offset);
    SerializeObject(buttons, buffer, offset);
    SerializeObject(mainImage, buffer, offset);
    SerializeObject(iconImage, buffer, offset);
}

void NotificationPacket::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(key, buffer, offset);
    DeserializeObject(appName, buffer, offset);
    DeserializeObject(title, buffer, offset);
    DeserializeObject(content, buffer, offset);
    DeserializeObject(timestamp, buffer, offset);
    DeserializeObject(dismissable, buffer, offset);
    DeserializeObject(buttons, buffer, offset);
    DeserializeObject(mainImage, buffer, offset);
    DeserializeObject(iconImage, buffer, offset);
}

size_t NotificationPacket::GetSerializedSize() const {
      return GetObjectSerializedSize(key) + GetObjectSerializedSize(appName) + GetObjectSerializedSize(title) + GetObjectSerializedSize(content) +
          GetObjectSerializedSize(timestamp) + GetObjectSerializedSize(dismissable) + GetObjectSerializedSize(buttons)+ GetObjectSerializedSize(mainImage) +
          GetObjectSerializedSize(iconImage);
}

NotificationData::NotificationData() { }

NotificationData::NotificationData(
    std::string key,
    std::string appName,
    std::string title,
    std::string content,
    size_t timestamp,
    bool dismissable,
    std::vector<std::string> buttons,
    std::vector<uint8_t> smallIcon,
    std::vector<uint8_t> largeIcon,
    std::vector<uint8_t> mainImage
) :
    key(std::move(key)),
    appName(std::move(appName)),
    title(std::move(title)),
    content(std::move(content)),
    timestamp(timestamp),
    dismissable(dismissable),
    buttons(std::move(buttons)),
    smallIcon(std::move(smallIcon)),
    largeIcon(std::move(largeIcon)),
    mainImage(std::move(mainImage))
{ }
