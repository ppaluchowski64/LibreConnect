#ifndef CAMERA_SPECIFICATION_H
#define CAMERA_SPECIFICATION_H

#include <vector>
#include <Packable.h>
#include <VCamAPI.h>

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

struct CameraFormat {
    uint32_t width;
    uint32_t height;
    float minFrameRate;
    float maxFrameRate;
    uint8_t pixelFormat;

    VCamFormat GetFormat() const;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    NO_DISCARD size_t GetSerializedSize() const;
};

struct CameraSpecification {
    std::string description;
    std::vector<CameraFormat> formats;
    std::string id;
    bool isDefault;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    NO_DISCARD size_t GetSerializedSize() const;
};

#endif //CAMERA_SPECIFICATION_H
