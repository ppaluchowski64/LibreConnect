#include <CameraUtilities.h>
#include <QThread>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/hwcontext.h>
}

static std::unordered_map<CodecID, std::vector<std::string>> g_encoderPriority = {
    {CodecID::H264, {
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
                format.resolution().height(),
                format.resolution().width(),
                format.minFrameRate(),
                format.maxFrameRate(),
                format.pixelFormat()
            );
        }

        specification.isDefault = camera.isDefault();
    }

    return camerasSpecifications;
}

static bool CanUseEncoder(const AVCodec* codec) {
    if (!codec) return false;
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

    for (const auto& codecName : g_decoderPriority.at(codecID)) {
        const AVCodec* codec = avcodec_find_decoder_by_name(codecName.c_str());
        if (!codec) continue;
        if (CanUseDecoder(codec)) return codec;
    }

    return avcodec_find_decoder(static_cast<AVCodecID>(codecID));
}