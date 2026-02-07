#include <CameraSpecification.h>
#include <QVideoFrameFormat>
#include <fmt/color.h>

void CameraFormat::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(width, buffer, offset);
    SerializeObject(height, buffer, offset);
    SerializeObject(framerate, buffer, offset);
}

void CameraFormat::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(width, buffer, offset);
    DeserializeObject(height, buffer, offset);
    DeserializeObject(framerate, buffer, offset);
}

size_t CameraFormat::GetSerializedSize() const {
    return  GetObjectSerializedSize(width) + GetObjectSerializedSize(height) + GetObjectSerializedSize(framerate);
}

void CameraSpecification::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(description, buffer, offset);
    SerializeObject(formats, buffer, offset);
    SerializeObject(id, buffer, offset);
    SerializeObject(isDefault, buffer, offset);
}

void CameraSpecification::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(description, buffer, offset);
    DeserializeObject(formats, buffer, offset);
    DeserializeObject(id, buffer, offset);
    DeserializeObject(isDefault, buffer, offset);
}

size_t CameraSpecification::GetSerializedSize() const {
    return GetObjectSerializedSize(description) + GetObjectSerializedSize(formats) + GetObjectSerializedSize(id) + GetObjectSerializedSize(isDefault);
}
