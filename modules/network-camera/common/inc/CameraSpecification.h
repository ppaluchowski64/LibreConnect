#ifndef CAMERA_SPECIFICATION_H
#define CAMERA_SPECIFICATION_H

#include <vector>
#include <QVideoFrameFormat>

#include <VCamTypes.h>
#include <Packable.h>
#include <DebugLog.h>

#include <fmt/core.h>
#include <fmt/format.h>

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}


struct CameraFormat {
    int32_t width;
    int32_t height;
    uint16_t framerate;

    CameraFormat() : width(0), height(0), framerate(0) {}
    CameraFormat(const int32_t w, const int32_t h, const uint16_t f)
        : width(w), height(h), framerate(f) {}

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    NO_DISCARD size_t GetSerializedSize() const;
};

struct CameraSettings {
    std::string cameraName;
    bool customCameraNameEnabled;
    int32_t width;
    int32_t height;
    uint16_t framerate;
    VCamFormat pixelFormat;
    std::string id;

    CameraSettings() : cameraName(""), customCameraNameEnabled(false), width(0), height(0), framerate(0), pixelFormat(VCAM_FORMAT_NV12), id("") {}
    CameraSettings(const std::string_view cameraName, const bool customCameraNameEnabled, const int32_t w, const int32_t h, const uint16_t f, const VCamFormat pf, const std::string& id)
        : cameraName(cameraName), customCameraNameEnabled(customCameraNameEnabled), width(w), height(h), framerate(f), pixelFormat(pf), id(id) {}
};

AVPixelFormat GetFormat(QVideoFrameFormat::PixelFormat format);

// ReSharper disable once CppPassValueParameterByConstReference
inline AVPixelFormat GetFormat(QVideoFrameFormat::PixelFormat format) {
    switch (format) {
        case QVideoFrameFormat::Format_RGBA8888: return AV_PIX_FMT_RGBA;
        case QVideoFrameFormat::Format_BGRA8888: return AV_PIX_FMT_BGRA;
        case QVideoFrameFormat::Format_YUYV: return AV_PIX_FMT_YUYV422;
        case QVideoFrameFormat::Format_NV12: return AV_PIX_FMT_NV12;
        case QVideoFrameFormat::Format_NV21: return AV_PIX_FMT_NV21;
        case QVideoFrameFormat::Format_YUV420P: return AV_PIX_FMT_YUV420P;
        default: Debug::LogError("Unsupported pixel format"); return AV_PIX_FMT_NONE;
    }
}

struct CameraSpecification {
    std::string description;
    std::vector<CameraFormat> formats;
    std::string id;
    bool isDefault;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    NO_DISCARD size_t GetSerializedSize() const;
};

template <>
struct fmt::formatter<CameraFormat>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const CameraFormat& f, FormatContext& ctx) const
    {
        return fmt::format_to(
            ctx.out(),
            "CameraFormat{{ width={}, height={}, framerate{}}}",
            f.width,
            f.height,
            f.framerate
        );
    }
};

template <>
struct fmt::formatter<CameraSpecification>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const CameraSpecification& s, FormatContext& ctx) const
    {
        auto out = ctx.out();

        out = fmt::format_to(
            out,
            "CameraSpecification{{ description=\"{}\", id=\"{}\", isDefault={}, formats=[",
            s.description,
            s.id,
            s.isDefault
        );

        for (size_t i = 0; i < s.formats.size(); ++i)
        {
            out = fmt::format_to(out, "\n{},", s.formats[i]);
        }

        out = fmt::format_to(out, "] }}");
        return out;
    }
};

#endif //CAMERA_SPECIFICATION_H
