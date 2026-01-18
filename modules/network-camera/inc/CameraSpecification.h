#ifndef CAMERA_SPECIFICATION_H
#define CAMERA_SPECIFICATION_H

#include <vector>
#include <Packable.h>
#include <VCamAPI.h>

#include <fmt/core.h>
#include <fmt/format.h>

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

struct CameraFormat {
    int32_t width;
    int32_t height;
    float minFrameRate;
    float maxFrameRate;
    uint8_t pixelFormat;

    CameraFormat() : width(0), height(0), minFrameRate(0), maxFrameRate(0), pixelFormat(0) {}

    CameraFormat(const int32_t w, const int32_t h, const float minFps, const float maxFps, const uint8_t pf)
        : width(w), height(h), minFrameRate(minFps), maxFrameRate(maxFps), pixelFormat(pf) {}

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
            "CameraFormat{{ width={}, height={}, minFPS={}, maxFPS={}, pixelFormat={} }}",
            f.width,
            f.height,
            f.minFrameRate,
            f.maxFrameRate,
            static_cast<uint32_t>(f.pixelFormat)
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