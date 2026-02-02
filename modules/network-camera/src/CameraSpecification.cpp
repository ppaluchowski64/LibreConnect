#include <CameraSpecification.h>
#include <QVideoFrameFormat>
#include <fmt/color.h>

AVPixelFormat CameraFormat::GetFormat() const {
    switch (static_cast<QVideoFrameFormat::PixelFormat>(pixelFormat)) {
        case QVideoFrameFormat::Format_RGBA8888: return AV_PIX_FMT_RGBA;
        case QVideoFrameFormat::Format_BGRA8888: return AV_PIX_FMT_BGRA;
        case QVideoFrameFormat::Format_YUYV: return AV_PIX_FMT_YUYV422;
        case QVideoFrameFormat::Format_NV12: return AV_PIX_FMT_NV12;
        case QVideoFrameFormat::Format_YUV420P: return AV_PIX_FMT_YUV420P;
        default: Debug::LogError("Unsupported pixel format"); return AV_PIX_FMT_NONE;
    }
}


void CameraFormat::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(width, buffer, offset);
    SerializeObject(height, buffer, offset);
    SerializeObject(minFrameRate, buffer, offset);
    SerializeObject(maxFrameRate, buffer, offset);
    SerializeObject(pixelFormat, buffer, offset);
}

void CameraFormat::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(width, buffer, offset);
    DeserializeObject(height, buffer, offset);
    DeserializeObject(minFrameRate, buffer, offset);
    DeserializeObject(maxFrameRate, buffer, offset);
    DeserializeObject(pixelFormat, buffer, offset);
}

size_t CameraFormat::GetSerializedSize() const {
    return  GetObjectSerializedSize(width) + GetObjectSerializedSize(height) +
            GetObjectSerializedSize(minFrameRate) + GetObjectSerializedSize(maxFrameRate) +
            GetObjectSerializedSize(pixelFormat);
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
