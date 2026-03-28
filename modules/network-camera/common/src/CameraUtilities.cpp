#include <CameraUtilities.h>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/hwcontext.h>
    #include <libswscale/swscale.h>
}

static std::unordered_map<CodecID, std::vector<std::string>> g_encoderPriority = {
    {CodecID::H264, {
        "h264_mediacodec",
        "h264_nvenc",
        "h264_qsv",
        "h264_amf",
        "h264_vaapi",
        "h264_videotoolbox",
        "libx264"
    }},
    {CodecID::H265, {
        "hevc_nvenc",
        "hevc_qsv",
        "hevc_amf",
        "hevc_vaapi",
        "hevc_videotoolbox",
        "libx265"
    }},
    {CodecID::AV1, {
        "av1_nvenc",
        "av1_qsv",
        "av1_amf",
        "av1_vaapi",
        "av1_videotoolbox",
        "libaom-av1",
        "rav1e"
    }}
};

static std::unordered_map<CodecID, std::vector<std::string>> g_decoderPriority = {
    {CodecID::H264, {
        "h264_cuvid",
        "h264_qsv",
        "h264_amf",
        "h264_vaapi",
        "h264_d3d11va",
        "h264_dxva2",
        "h264_videotoolbox",
        "libx264"
    }},
    {CodecID::H265, {
        "hevc_cuvid",
        "hevc_qsv",
        "hevc_amf",
        "hevc_vaapi",
        "hevc_d3d11va",
        "hevc_dxva2",
        "hevc_videotoolbox",
        "libx265"
    }},
    {CodecID::AV1, {
        "av1_cuvid",
        "av1_qsv",
        "av1_amf",
        "av1_vaapi",
        "av1_videotoolbox",
        "libaom-av1",
        "rav1e"
    }}
};
std::vector<CameraSpecification> FetchCamerasSpecification() {
    if (!QGuiApplication::instance()) {
        return {};
    }

    QList<QCameraDevice> cameras;

    if (QThread::currentThread() == QGuiApplication::instance()->thread()) {
        cameras = QMediaDevices::videoInputs();
    } else {
        QMetaObject::invokeMethod(
            QGuiApplication::instance(),
            [&cameras]() {
                cameras = QMediaDevices::videoInputs();
            },
            Qt::BlockingQueuedConnection
        );
    }

    std::vector<CameraSpecification> camerasSpecifications(cameras.size());

    for (int i = 0; i < cameras.size(); ++i) {
        const QCameraDevice& camera = cameras.at(i);
        CameraSpecification& specification = camerasSpecifications.at(i);

        specification.description = camera.description().toStdString();
        specification.id = camera.id().toStdString();

        const QList<QCameraFormat> formats = camera.videoFormats();
        specification.formats.reserve(formats.size());

        for (const auto& format : formats) {
            specification.formats.emplace_back(
                format.resolution().width(),
                format.resolution().height(),
                std::floor(format.maxFrameRate())
            );
        }

        specification.isDefault = camera.isDefault();
    }

    return camerasSpecifications;
}

static bool CanUseEncoder(const AVCodec* codec) {
    if (!codec) return false;

#ifdef ANDROID_DEVICE
    if (codec->name && std::strstr(codec->name, "mediacodec")) {
        const AVPixelFormat preferredPixFmts[] = {
            AV_PIX_FMT_NV12,
            AV_PIX_FMT_YUV420P,
            AV_PIX_FMT_NONE
        };

        auto TryOpen = [&](const AVPixelFormat pixFmt) -> bool {
            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            if (!ctx) {
                return false;
            }

            ctx->pix_fmt = pixFmt;
            ctx->width = 1280;
            ctx->height = 720;
            ctx->time_base = {1, 30};
            ctx->framerate = {30, 1};
            ctx->gop_size = 15;
            ctx->max_b_frames = 0;
            ctx->bit_rate = 2000000;

            const int ret = avcodec_open2(ctx, codec, nullptr);
            avcodec_free_context(&ctx);
            return ret == 0;
        };

        if (codec->pix_fmts) {
            for (const AVPixelFormat* p = codec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
                if (TryOpen(*p)) {
                    return true;
                }
            }
        }

        for (const AVPixelFormat* p = preferredPixFmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (TryOpen(*p)) {
                return true;
            }
        }

        return false;
    }
#endif

    if (!(codec->capabilities & AV_CODEC_CAP_HARDWARE)) return false;

    AVHWDeviceType deviceType = AV_HWDEVICE_TYPE_NONE;
    for (int i = 0;; i++) {
        const AVCodecHWConfig* hwcfg = avcodec_get_hw_config(codec, i);
        if (!hwcfg)
            break;

        if (hwcfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            deviceType = hwcfg->device_type;
            break;
        }
    }

    AVBufferRef* hw_device = nullptr;
    if (av_hwdevice_ctx_create(&hw_device, deviceType, nullptr, nullptr, 0) < 0) {
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        av_buffer_unref(&hw_device);
        return false;
    }

    ctx->hw_device_ctx = av_buffer_ref(hw_device);

    if (codec->pix_fmts) {
        ctx->pix_fmt = codec->pix_fmts[0];
    } else {
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    ctx->width = 1920;
    ctx->height = 1080;

    ctx->time_base = {1, 30};
    ctx->framerate = {30, 1};
    ctx->bit_rate = 500000;

    const int ret = avcodec_open2(ctx, codec, nullptr);

    av_buffer_unref(&hw_device);
    avcodec_free_context(&ctx);

    return ret == 0;
}

static bool CanUseDecoder(const AVCodec* codec) {
    if (!codec) return false;
    if (!(codec->capabilities & AV_CODEC_CAP_HARDWARE)) return false;

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    const bool ok = (avcodec_open2(ctx, codec, nullptr) == 0);

    avcodec_free_context(&ctx);
    return ok;
}


const AVCodec* GetEncoderCodec(const CodecID codecID) {
    if (!g_encoderPriority.contains(codecID)) return nullptr;

    for (const auto& codecName : g_encoderPriority.at(codecID)) {
        const AVCodec* codec = avcodec_find_encoder_by_name(codecName.c_str());
        if (!codec) continue;
        if (CanUseEncoder(codec)) return codec;
    }

  return avcodec_find_encoder(static_cast<AVCodecID>(codecID));
}

const AVCodec* GetDecoderCodec(const CodecID codecID) {
    if (!g_decoderPriority.contains(codecID)) return nullptr;

    // Prefer software decoding for now to keep frames in system memory.
    // Hardware decoders (e.g., cuvid/d3d11va) often output GPU frames that
    // require explicit hwframe transfer before sws_scale / CPU access.
    //
    // If you want to re-enable HW decoding, restore this block and add
    // proper hwframe transfer in the desktop decode path:
    //
    // for (const auto& codecName : g_decoderPriority.at(codecID)) {
    //     const AVCodec* codec = avcodec_find_decoder_by_name(codecName.c_str());
    //     if (!codec) continue;
    //     if (CanUseDecoder(codec)) return codec;
    // }

    return avcodec_find_decoder(static_cast<AVCodecID>(codecID));
}


namespace CameraUtilitiesLC {
    AVPixelFormat ToAVPixelFormat(const QVideoFrameFormat::PixelFormat format) {
        switch (format) {
            case QVideoFrameFormat::Format_RGBA8888:
                return AV_PIX_FMT_RGBA;
            case QVideoFrameFormat::Format_BGRA8888:
                return AV_PIX_FMT_BGRA;
            case QVideoFrameFormat::Format_YUYV:
                return AV_PIX_FMT_YUYV422;
            case QVideoFrameFormat::Format_NV12:
                return AV_PIX_FMT_NV12;
            case QVideoFrameFormat::Format_NV21:
                return AV_PIX_FMT_NV21;
            case QVideoFrameFormat::Format_YUV420P:
                return AV_PIX_FMT_YUV420P;
            default:
                return AV_PIX_FMT_NONE;
        }
    }

    bool CodecSupportsPixelFormat(const AVCodec* codec, const AVPixelFormat format) {
        if (!codec || !codec->pix_fmts || format == AV_PIX_FMT_NONE) {
            return false;
        }

        for (const AVPixelFormat* p = codec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == format) {
                return true;
            }
        }

        return false;
    }

    AVPixelFormat PickEncoderPixelFormat(const AVCodec* codec, const AVPixelFormat inputFormat) {
        if (!codec || inputFormat == AV_PIX_FMT_NONE) {
            return AV_PIX_FMT_NONE;
        }

        if (!codec->pix_fmts) {
            return inputFormat;
        }

        if (CodecSupportsPixelFormat(codec, inputFormat)) {
            return inputFormat;
        }

        if (!sws_isSupportedInput(inputFormat)) {
            return AV_PIX_FMT_NONE;
        }

        if (CodecSupportsPixelFormat(codec, AV_PIX_FMT_YUV420P) && sws_isSupportedOutput(AV_PIX_FMT_YUV420P)) {
            return AV_PIX_FMT_YUV420P;
        }

        for (const AVPixelFormat* p = codec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (sws_isSupportedOutput(*p)) {
                return *p;
            }
        }

        return AV_PIX_FMT_NONE;
    }

    bool CanOpenEncoderWithFormat(
        const AVCodec* codec,
        const int width,
        const int height,
        const int fps,
        const AVPixelFormat encoderPixelFormat
    ) {
        if (!codec || width <= 0 || height <= 0 || fps <= 0 || encoderPixelFormat == AV_PIX_FMT_NONE) {
            return false;
        }

        AVCodecContext* context = avcodec_alloc_context3(codec);
        if (!context) {
            return false;
        }

        context->width = width;
        context->height = height;
        context->pix_fmt = encoderPixelFormat;
        context->time_base = {1, fps};
        context->framerate = {fps, 1};
        context->gop_size = std::max(8, fps / 2);
        context->max_b_frames = 0;

        const int64_t pixelRate = static_cast<int64_t>(width) * height * fps;
        const int64_t targetBitrate = std::clamp<int64_t>(pixelRate / 8, 1200000, 12000000);
        context->bit_rate = static_cast<int>(targetBitrate);
        context->rc_min_rate = static_cast<int>(targetBitrate * 3 / 4);
        context->rc_max_rate = static_cast<int>(targetBitrate * 5 / 4);
        context->bit_rate_tolerance = static_cast<int>(targetBitrate / 2);

        const int openResult = avcodec_open2(context, codec, nullptr);
        avcodec_free_context(&context);
        return openResult >= 0;
    }

    bool IsCameraFormatSupportedByCodec(const AVCodec* codec, const QCameraFormat& cameraFormat, const int requestedFps) {
        if (!codec) {
            return true;
        }

        const AVPixelFormat inputFormat = ToAVPixelFormat(cameraFormat.pixelFormat());
        const AVPixelFormat encoderPixelFormat = PickEncoderPixelFormat(codec, inputFormat);
        if (encoderPixelFormat == AV_PIX_FMT_NONE) {
            return false;
        }

        return CanOpenEncoderWithFormat(
            codec,
            cameraFormat.resolution().width(),
            cameraFormat.resolution().height(),
            std::max(1, requestedFps),
            encoderPixelFormat
        );
    }

    bool IsH264ParameterSetNal(const uint8_t nalHeader) {
        const uint8_t nalType = nalHeader & 0x1F;
        return nalType == 7 || nalType == 8;
    }

    bool IsSupportedH264NalType(const uint8_t nalHeader) {
        const uint8_t nalType = nalHeader & 0x1F;
        return nalType > 0 && nalType <= 31;
    }

    size_t FindStart(const uint8_t* data, const size_t size, const size_t from) {
        for (size_t j = from; j + 3 < size; ++j) {
            if (data[j] == 0x00 && data[j + 1] == 0x00 &&
                (data[j + 2] == 0x01 || (data[j + 2] == 0x00 && data[j + 3] == 0x01))) {
                return j;
                }
        }
        return size;
    }

    void SplitAnnexB(const uint8_t* data, const size_t size, std::vector<NalSpan>& out) {
        size_t i = 0;
        while (i < size) {
            const size_t start = FindStart(data, size, i);
            if (start >= size) {
                break;
            }

            const size_t scSize = (data[start + 2] == 0x01) ? 3 : 4;
            const size_t nalStart = start + scSize;
            const size_t next = FindStart(data, size, nalStart);
            const size_t nalEnd = (next < size) ? next : size;

            if (nalEnd > nalStart) {
                out.push_back({data + nalStart, nalEnd - nalStart});
            }

            i = nalEnd;
        }
    }

    bool SplitAvcc(const uint8_t* data, const size_t size, const uint8_t nalLengthSize, std::vector<NalSpan>& out) {
        if (!data || nalLengthSize < 1 || nalLengthSize > 4) {
            return false;
        }

        size_t offset = 0;
        while (offset + nalLengthSize <= size) {
            uint32_t n = 0;
            for (uint8_t i = 0; i < nalLengthSize; ++i) {
                n = (n << 8) | data[offset + i];
            }
            offset += nalLengthSize;

            if (n == 0 || offset + n > size) {
                return false;
            }

            if (!IsSupportedH264NalType(data[offset])) {
                return false;
            }

            out.push_back({data + offset, n});
            offset += n;
        }

        return !out.empty() && offset == size;
    }

    bool SplitAvccAuto(const uint8_t* data, const size_t size, const uint8_t preferredNalLengthSize, std::vector<NalSpan>& out) {
        std::vector<NalSpan> parsed;
        if (SplitAvcc(data, size, preferredNalLengthSize, parsed)) {
            out = std::move(parsed);
            return true;
        }

        constexpr uint8_t candidates[] = {4, 2, 1};
        for (const uint8_t candidate : candidates) {
            if (candidate == preferredNalLengthSize) {
                continue;
            }

            parsed.clear();
            if (SplitAvcc(data, size, candidate, parsed)) {
                out = std::move(parsed);
                return true;
            }
        }

        return false;
    }

    bool IsAnnexB(const uint8_t* data, const size_t size) {
        if (size < 4) {
            return false;
        }
        return (data[0] == 0x00 && data[1] == 0x00 && ((data[2] == 0x01) || (data[2] == 0x00 && data[3] == 0x01)));
    }

    std::vector<std::vector<uint8_t>> ParseAnnexBH264ParameterSets(const uint8_t* data, const size_t size) {
        std::vector<std::vector<uint8_t>> out;
        if (!data || size < 4) {
            return out;
        }

        size_t i = 0;
        while (i < size) {
            const size_t start = FindStart(data, size, i);
            if (start >= size) {
                break;
            }

            const size_t scSize = (data[start + 2] == 0x01) ? 3 : 4;
            const size_t nalStart = start + scSize;
            const size_t next = FindStart(data, size, nalStart);
            const size_t nalEnd = (next < size) ? next : size;

            if (nalEnd > nalStart) {
                const uint8_t nalType = data[nalStart] & 0x1F;
                if (nalType == 7 || nalType == 8) {
                    out.emplace_back(data + nalStart, data + nalEnd);
                }
            }

            i = nalEnd;
        }

        return out;
    }

    std::vector<std::vector<uint8_t>> ParseAvccH264ParameterSets(const uint8_t* data, const size_t size) {
        std::vector<std::vector<uint8_t>> out;
        if (!data || size < 7 || data[0] != 1) {
            return out;
        }

        size_t offset = 6;
        const uint8_t numSps = data[5] & 0x1F;
        for (uint8_t i = 0; i < numSps; ++i) {
            if (offset + 2 > size) {
                return out;
            }

            const uint16_t n = (static_cast<uint16_t>(data[offset]) << 8) |
                               static_cast<uint16_t>(data[offset + 1]);
            offset += 2;

            if (n == 0 || offset + n > size) {
                return out;
            }

            out.emplace_back(data + offset, data + offset + n);
            offset += n;
        }

        if (offset >= size) {
            return out;
        }

        const uint8_t numPps = data[offset++];
        for (uint8_t i = 0; i < numPps; ++i) {
            if (offset + 2 > size) {
                return out;
            }

            const uint16_t n = (static_cast<uint16_t>(data[offset]) << 8) |
                               static_cast<uint16_t>(data[offset + 1]);
            offset += 2;

            if (n == 0 || offset + n > size) {
                return out;
            }

            out.emplace_back(data + offset, data + offset + n);
            offset += n;
        }

        return out;
    }

    std::vector<std::vector<uint8_t>> ExtractH264ParameterSets(const AVCodecContext* codecContext) {
        if (!codecContext || !codecContext->extradata || codecContext->extradata_size <= 0) {
            return {};
        }

        const uint8_t* data = codecContext->extradata;
        const size_t size = static_cast<size_t>(codecContext->extradata_size);

        std::vector<std::vector<uint8_t>> out = ParseAvccH264ParameterSets(data, size);
        if (out.empty()) {
            out = ParseAnnexBH264ParameterSets(data, size);
        }

        return out;
    }

    uint8_t ExtractAvccNalLengthSize(const AVCodecContext* codecContext) {
        if (!codecContext || !codecContext->extradata || codecContext->extradata_size < 5) {
            return 4;
        }

        const uint8_t* data = codecContext->extradata;
        if (data[0] != 1) {
            return 4;
        }

        return static_cast<uint8_t>((data[4] & 0x03) + 1);
    }

    bool UpsertH264ParameterSet(std::vector<std::vector<uint8_t>>& parameterSets, const uint8_t* data, const size_t size) {
        if (!data || size == 0 || !IsH264ParameterSetNal(data[0])) {
            return false;
        }

        const uint8_t nalType = data[0] & 0x1F;
        for (auto& existing : parameterSets) {
            if (!existing.empty() && (existing[0] & 0x1F) == nalType) {
                if (existing.size() == size && std::memcmp(existing.data(), data, size) == 0) {
                    return false;
                }

                existing.assign(data, data + size);
                return true;
            }
        }

        parameterSets.emplace_back(data, data + size);
        return true;
    }

    bool UpdateH264ParameterSetsFromNalSpans(std::vector<std::vector<uint8_t>>& parameterSets, const std::vector<NalSpan>& nals) {
        bool updated = false;
        for (const auto& nal : nals) {
            updated = UpsertH264ParameterSet(parameterSets, nal.data, nal.size) || updated;
        }
        return updated;
    }

    bool UpdateH264ParameterSetsFromExtradata(std::vector<std::vector<uint8_t>>& parameterSets, uint8_t& nalLengthSize, const uint8_t* data, const size_t size) {
        if (!data || size == 0) {
            return false;
        }

        std::vector<std::vector<uint8_t>> extracted = ParseAvccH264ParameterSets(data, size);
        if (extracted.empty()) {
            extracted = ParseAnnexBH264ParameterSets(data, size);
        } else if (size >= 5 && data[0] == 1) {
            nalLengthSize = static_cast<uint8_t>((data[4] & 0x03) + 1);
        }

        bool updated = false;
        for (const auto& nal : extracted) {
            updated = UpsertH264ParameterSet(parameterSets, nal.data(), nal.size()) || updated;
        }

        return updated;
    }

    bool EnsureReusableFrameBuffer(AVFrame*& frame, const AVPixelFormat format, const int width, const int height) {
        if (width <= 0 || height <= 0 || format == AV_PIX_FMT_NONE) {
            return false;
        }

        if (!frame) {
            frame = av_frame_alloc();
            if (!frame) {
                return false;
            }
        }

        const bool needsReallocate =
            frame->format != static_cast<int>(format) ||
            frame->width != width ||
            frame->height != height ||
            frame->buf[0] == nullptr;

        if (needsReallocate) {
            av_frame_unref(frame);
            frame->format = static_cast<int>(format);
            frame->width = width;
            frame->height = height;
            if (av_frame_get_buffer(frame, 32) < 0) {
                av_frame_unref(frame);
                return false;
            }
            return true;
        }

        return av_frame_make_writable(frame) >= 0;
    }
}
