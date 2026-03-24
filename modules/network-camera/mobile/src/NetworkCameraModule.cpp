#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <PermissionManager.h>

#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <tuple>

#include <asio.hpp>
#include <asio/co_spawn.hpp>

#include <QVideoSink>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QGuiApplication>
#include <QObject>
#include <QThread>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>

extern "C" {
    #include <libavutil/error.h>
    #include <libavutil/imgutils.h>
}

namespace {
    struct NalSpan {
        const uint8_t* data;
        size_t size;
    };

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
            if (start >= size) break;

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
        if (size < 4) return false;
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

    class InFlightFrameGuard {
    public:
        explicit InFlightFrameGuard(std::atomic<uint32_t>& counter)
            : m_counter(counter) {
            m_counter.fetch_add(1, std::memory_order_relaxed);
        }

        ~InFlightFrameGuard() {
            m_counter.fetch_sub(1, std::memory_order_relaxed);
        }

        InFlightFrameGuard(const InFlightFrameGuard&) = delete;
        InFlightFrameGuard& operator=(const InFlightFrameGuard&) = delete;

    private:
        std::atomic<uint32_t>& m_counter;
    };

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
        context->gop_size = std::max(15, fps);
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

    std::vector<CameraSpecification> FetchCamerasSpecificationForCodec(const AVCodec* codec) {
        if (!QGuiApplication::instance() || !codec) {
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

        std::vector<CameraSpecification> output;
        output.reserve(cameras.size());

        for (const QCameraDevice& camera : cameras) {
            CameraSpecification specification;
            specification.description = camera.description().toStdString();
            specification.id = camera.id().toStdString();
            specification.isDefault = camera.isDefault();

            std::set<std::tuple<int32_t, int32_t, uint16_t>> uniqueFormats;

            for (const QCameraFormat& format : camera.videoFormats()) {
                const int width = format.resolution().width();
                const int height = format.resolution().height();
                const int fps = std::max(1, static_cast<int>(std::floor(format.maxFrameRate())));
                if (!IsCameraFormatSupportedByCodec(codec, format, fps)) {
                    continue;
                }

                uniqueFormats.emplace(
                    static_cast<int32_t>(width),
                    static_cast<int32_t>(height),
                    static_cast<uint16_t>(fps)
                );
            }

            specification.formats.reserve(uniqueFormats.size());
            for (const auto& [w, h, f] : uniqueFormats) {
                specification.formats.emplace_back(w, h, f);
            }

            if (!specification.formats.empty()) {
                output.emplace_back(std::move(specification));
            }
        }

        return output;
    }

}

asio::awaitable<void> NetworkCameraModule::StartStream(const size_t requestID, const std::string cameraID, const CameraFormat requestedFormat) {
    if (!QGuiApplication::instance()) {
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
        ProcessError(ModuleFailReason::InternalError);
        co_return;
    }

    const uint64_t generation = m_streamGeneration.fetch_add(1) + 1;

    Debug::Log(
        "NetworkCameraModule: StartStream requestId={}, cameraId={}, {}x{}@{}",
        requestID,
        cameraID,
        requestedFormat.width,
        requestedFormat.height,
        requestedFormat.framerate
    );

    {
        asio::steady_timer timer(m_context);
        int attempts = 0;
        while (m_portNumber.load() == 0) {
            if (++attempts > 500) { // ~5 seconds at 10ms
                Debug::LogError("SRTP port info not received in time");
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                ProcessError(ModuleFailReason::Timeout);
                co_return;
            }
            timer.expires_after(asio::chrono::milliseconds(10));
            co_await timer.async_wait();
        }
    }

    QMetaObject::invokeMethod(
        QGuiApplication::instance(),
        [=, this]() {
            if (generation != m_streamGeneration.load()) {
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
                ProcessError(ModuleFailReason::InvalidState);
                return;
            }

            m_streamActive.store(false);

            if (m_videoFrameConnection) {
                QObject::disconnect(m_videoFrameConnection);
                m_videoFrameConnection = {};
            }

            if (m_camera) {
                m_camera->stop();
            }

            if (m_captureSession) {
                m_captureSession->setVideoSink(nullptr);
                m_captureSession->setCamera(nullptr);
            }

            m_videoSink.reset();
            m_captureSession.reset();
            m_camera.reset();

            if (m_videoStream) {
                m_videoStream->Close();
                m_videoStream.reset();
            }

            if (m_codecContext) {
                avcodec_free_context(&m_codecContext);
                m_codecContext = nullptr;
            }

            if (m_swsContext) {
                sws_freeContext(m_swsContext);
                m_swsContext = nullptr;
            }

            m_h264ParameterSets.clear();
            m_h264LengthSize = 4;
            m_codecConfigSent = false;

            QList<QCameraDevice> devices = QMediaDevices::videoInputs();
            const QCameraDevice* cameraDevice = nullptr;

            for (auto& device : devices) {
                if (device.id().toStdString() == cameraID) {
                    cameraDevice = &device;
                }
            }

            if (cameraDevice == nullptr) {
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                ProcessError(ModuleFailReason::IncorrectConfig);
                return;
            }

            m_codec = GetEncoderCodec(CodecID::H264);
            if (m_codec == nullptr) {
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
                ProcessError(ModuleFailReason::InternalError);
                return;
            }

            m_camera = std::make_unique<QCamera>(*cameraDevice);

            bool formatFound = false;
            const QList<QCameraFormat> supportedFormats = cameraDevice->videoFormats();
            QCameraFormat format = QCameraFormat();
            QCameraFormat fallbackFormat = QCameraFormat();
            bool fallbackFound = false;

            for (const auto& fm : supportedFormats) {
                if (fm.resolution().width() != requestedFormat.width || fm.resolution().height() != requestedFormat.height) {
                    continue;
                }

                constexpr float diff = 0.01f;
                if (requestedFormat.framerate < (fm.minFrameRate() - diff) ||
                    requestedFormat.framerate > (fm.maxFrameRate() + diff)) {
                    continue;
                }

                if (!IsCameraFormatSupportedByCodec(m_codec, fm, static_cast<int>(requestedFormat.framerate))) {
                    continue;
                }

                if (fm.pixelFormat() == QVideoFrameFormat::Format_NV12) {
                    format = fm;
                    formatFound = true;
                    break;
                }

                if (!fallbackFound) {
                    fallbackFormat = fm;
                    fallbackFound = true;
                }
            }

            if (!formatFound) {
                if (!fallbackFound) {
                    Debug::LogError(
                        "No encoder-compatible camera format for requested {}x{}@{}",
                        requestedFormat.width,
                        requestedFormat.height,
                        requestedFormat.framerate
                    );
                    ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                    ProcessError(ModuleFailReason::IncorrectConfig);
                    return;
                }

                format = fallbackFormat;
            }
            m_camera->setCameraFormat(format);

            m_videoSink = std::make_unique<QVideoSink>();

            m_captureSession = std::make_unique<QMediaCaptureSession>();
            m_captureSession->setCamera(m_camera.get());
            m_captureSession->setVideoSink(m_videoSink.get());

            m_camera->start();

            if (m_codecContext) {
                avcodec_free_context(&m_codecContext);
            }

            m_codecContext = avcodec_alloc_context3(m_codec);
            m_codecContext->width = format.resolution().width();
            m_codecContext->height = format.resolution().height();

            const int fps = std::max(1, static_cast<int>(std::round(format.maxFrameRate())));
            const int64_t pixelRate = static_cast<int64_t>(m_codecContext->width) * m_codecContext->height * fps;
            const int64_t targetBitrate = std::clamp<int64_t>(pixelRate / 8, 1200000, 12000000);

            m_codecContext->bit_rate = static_cast<int>(targetBitrate);
            m_codecContext->rc_min_rate = static_cast<int>(targetBitrate * 3 / 4);
            m_codecContext->rc_max_rate = static_cast<int>(targetBitrate * 5 / 4);
            m_codecContext->bit_rate_tolerance = static_cast<int>(targetBitrate / 2);

            m_codecContext->time_base = {1, fps};
            m_codecContext->framerate = {fps, 1};
            m_codecContext->gop_size = std::max(15, fps);
            m_codecContext->max_b_frames = 0;

            const AVPixelFormat inputPixFmt = ToAVPixelFormat(format.pixelFormat());
            m_codecContext->pix_fmt = PickEncoderPixelFormat(m_codec, inputPixFmt);
            if (m_codecContext->pix_fmt == AV_PIX_FMT_NONE) {
                Debug::LogError("Codec does not support selected camera input format ({})", magic_enum::enum_name(format.pixelFormat()));
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                ProcessError(ModuleFailReason::IncorrectConfig);
                return;
            }

            if (m_codec->name && std::strstr(m_codec->name, "mediacodec")) {
                m_codecContext->max_b_frames = 0;
            }

            Debug::Log(
                "Encoder open params: codec={}, pix_fmt={}, size={}x{}, fps={}, bitrate={}",
                 m_codec->name ? m_codec->name : "unknown",
                av_get_pix_fmt_name(m_codecContext->pix_fmt),
                m_codecContext->width,
                m_codecContext->height,
                fps,
                m_codecContext->bit_rate
            );

            {
                int openRet = avcodec_open2(m_codecContext, m_codec, nullptr);
                if (openRet < 0 && m_codec->name && std::strstr(m_codec->name, "mediacodec") && m_codec->pix_fmts) {
                    constexpr AVPixelFormat candidates[] = { AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P };
                    for (const AVPixelFormat cand : candidates) {
                        if (cand == m_codecContext->pix_fmt) continue;
                        bool supported = false;
                        for (const AVPixelFormat* p = m_codec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
                            if (*p == cand) { supported = true; break; }
                        }
                        if (!supported) continue;

                        m_codecContext->pix_fmt = cand;
                        Debug::Log("Retrying encoder open with pix_fmt={}", av_get_pix_fmt_name(cand));
                        openRet = avcodec_open2(m_codecContext, m_codec, nullptr);
                        if (openRet >= 0) break;
                    }
                }

                if (openRet < 0) {
                    char err[AV_ERROR_MAX_STRING_SIZE]{};
                    av_strerror(openRet, err, sizeof(err));
                    Debug::LogError("Failed to open encoder: {}", err);
                    ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
                    ProcessError(ModuleFailReason::InternalError);
                    return;
                }
            }

            m_h264ParameterSets = ExtractH264ParameterSets(m_codecContext);
            m_h264LengthSize = ExtractAvccNalLengthSize(m_codecContext);
            m_codecConfigSent = m_h264ParameterSets.empty();
            if (!m_h264ParameterSets.empty()) {
                Debug::Log(
                    "Extracted {} H264 parameter sets from encoder extradata (avcc_length_size={})",
                    m_h264ParameterSets.size(),
                    m_h264LengthSize
                );
            } else {
                Debug::LogWarning("No H264 parameter sets in encoder extradata; relying on in-band SPS/PPS");
            }

            m_videoStream = std::make_shared<SRTP::Stream>(m_context, m_localKey, m_remoteKey, requestedFormat.framerate);
            const auto peerAddr = ConnectionManager::GetPeerAddress();
            const auto peerPort = m_portNumber.load();
            Debug::Log("Binding SRTP to {}:{}", peerAddr.to_string(), peerPort);
            m_videoStream->Bind(UDPEndpoint(peerAddr, peerPort));

            if (m_swsContext) {
                sws_freeContext(m_swsContext);
            }

            if (inputPixFmt != m_codecContext->pix_fmt) {
                m_swsContext = sws_getContext(
                    m_codecContext->width,
                    m_codecContext->height,
                    inputPixFmt,
                    m_codecContext->width,
                    m_codecContext->height,
                    m_codecContext->pix_fmt,
                    SWS_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr
                );

                if (!m_swsContext) {
                    Debug::LogError("Could not initialize SwsContext");
                    ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
                    ProcessError(ModuleFailReason::InternalError);
                    return;
                }
            }

            m_ptsCounter = 0;
            m_streamActive.store(true);
            ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::None);
            if (m_videoFrameConnection) {
                QObject::disconnect(m_videoFrameConnection);
            }

            m_videoFrameConnection = QObject::connect(m_videoSink.get(), &QVideoSink::videoFrameChanged, QGuiApplication::instance(), [this, generation](const QVideoFrame &frame) {
                if (!frame.isValid())
                    return;

                if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                    return;
                }

                asio::co_spawn(m_moduleStrand, SendFrame(frame), asio::detached);
            });
        },
        Qt::QueuedConnection
    );
}

asio::awaitable<void> NetworkCameraModule::SendFrame(QVideoFrame frame) {
    InFlightFrameGuard inFlightGuard(m_inFlightSendFrames);

    if (!m_streamActive.load()) {
        co_return;
    }
    const uint64_t generation = m_streamGeneration.load();

    if (!m_codecContext) {
        co_return;
    }

    if (!frame.map(QVideoFrame::ReadOnly))
        co_return;

    const AVPixelFormat inputFmt = GetFormat(frame.pixelFormat());

    if (inputFmt == AV_PIX_FMT_NONE) {
        Debug::LogError("Unsupported input pixel format ({})", magic_enum::enum_name(frame.pixelFormat()));
        frame.unmap();
        co_return;
    }

    const std::shared_ptr<SRTP::Stream> stream = m_videoStream;
    if (!stream) {
        frame.unmap();
        co_return;
    }

    AVFrame* avFrame = av_frame_alloc();
    if (!avFrame) { frame.unmap(); co_return; }

    avFrame->format = m_codecContext->pix_fmt;
    avFrame->width  = m_codecContext->width;
    avFrame->height = m_codecContext->height;
    avFrame->pts = m_ptsCounter++;

    if (av_frame_get_buffer(avFrame, 32) < 0) {
        Debug::LogError("Could not allocate frame data");
        av_frame_free(&avFrame);
        frame.unmap();
        co_return;
    }

    auto buildSourcePlanes = [&](const AVPixelFormat pixelFormat, const int width, const int height, const uint8_t* srcSlice[4], int srcStride[4]) -> bool {
        for (int i = 0; i < 4; ++i) {
            srcSlice[i] = nullptr;
            srcStride[i] = 0;
        }

        const int planeCount = frame.planeCount();
        for (int i = 0; i < planeCount && i < 4; ++i) {
            srcSlice[i] = frame.bits(i);
            srcStride[i] = frame.bytesPerLine(i);
        }

        if (pixelFormat == AV_PIX_FMT_NV12 && planeCount == 1) {
            static bool loggedPackedNV12 = false;
            if (!loggedPackedNV12) {
                Debug::LogWarning("NetworkCameraModule: QVideoFrame NV12 exposed as packed single-plane; synthesizing UV plane");
                loggedPackedNV12 = true;
            }

            const uint8_t* base = frame.bits(0);
            const int yStride = frame.bytesPerLine(0);
            if (!base || yStride <= 0 || width <= 0 || height <= 0) {
                return false;
            }

            const size_t yBytes = static_cast<size_t>(yStride) * static_cast<size_t>(height);
            srcSlice[1] = base + yBytes;
            srcStride[1] = yStride;
        }

        if (!srcSlice[0]) {
            return false;
        }

        if (pixelFormat == AV_PIX_FMT_NV12 && !srcSlice[1]) {
            return false;
        }

        return true;
    };

    const bool sameSize = frame.width() == m_codecContext->width && frame.height() == m_codecContext->height;
    const bool requiresScaleOrConvert = (inputFmt != m_codecContext->pix_fmt) || !sameSize;

    const uint8_t* srcSlice[4] = {};
    int srcStride[4] = {};
    if (!buildSourcePlanes(inputFmt, frame.width(), frame.height(), srcSlice, srcStride)) {
        Debug::LogError(
            "NetworkCameraModule: Could not map source planes (fmt={}, planes={}, size={}x{}, stride0={})",
            av_get_pix_fmt_name(inputFmt),
            frame.planeCount(),
            frame.width(),
            frame.height(),
            frame.planeCount() > 0 ? frame.bytesPerLine(0) : 0
        );
        av_frame_free(&avFrame);
        frame.unmap();
        co_return;
    }

    if (requiresScaleOrConvert) {
        m_swsContext = sws_getCachedContext(
            m_swsContext,
            frame.width(),
            frame.height(),
            inputFmt,
            m_codecContext->width,
            m_codecContext->height,
            m_codecContext->pix_fmt,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr
        );

        if (!m_swsContext) {
            Debug::LogError("Could not initialize SwsContext");
            av_frame_free(&avFrame);
            frame.unmap();
            co_return;
        }

        sws_scale(m_swsContext, srcSlice, srcStride, 0, frame.height(), avFrame->data, avFrame->linesize);
    } else {
        av_image_copy(
            avFrame->data,
            avFrame->linesize,
            srcSlice,
            srcStride,
            m_codecContext->pix_fmt,
            m_codecContext->width,
            m_codecContext->height
        );
    }

    frame.unmap();

    AVPacket *pkt = av_packet_alloc();
    int ret = avcodec_send_frame(m_codecContext, avFrame);

    av_frame_free(&avFrame);

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            av_packet_free(&pkt);
            co_return;
        }

        char err[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(ret, err, sizeof(err));
        Debug::LogError("Error sending frame to encoder: {}", err);
        av_packet_free(&pkt);
        co_return;
    }

    while (true) {
        ret = avcodec_receive_packet(m_codecContext, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            Debug::LogError("Error during encoding");
            break;
        }

        size_t newExtradataSize = 0;
        const uint8_t* newExtradata = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &newExtradataSize);
        if (newExtradata && newExtradataSize > 0) {
            const bool updated = UpdateH264ParameterSetsFromExtradata(
                m_h264ParameterSets,
                m_h264LengthSize,
                newExtradata,
                newExtradataSize
            );
            if (updated) {
                Debug::Log("Updated H264 parameter sets from packet side-data (count={})", m_h264ParameterSets.size());
            }
        }

        std::vector<NalSpan> nal_spans;
        if (IsAnnexB(pkt->data, pkt->size)) {
            SplitAnnexB(pkt->data, pkt->size, nal_spans);
        } else {
            SplitAvccAuto(pkt->data, pkt->size, m_h264LengthSize, nal_spans);
        }

        if (nal_spans.empty()) {
            static bool loggedH264SplitWarning = false;
            if (!loggedH264SplitWarning) {
                Debug::LogWarning(
                    "Could not split encoded H264 packet (size={}) as Annex-B or AVCC; sending raw payload",
                    pkt->size
                );
                loggedH264SplitWarning = true;
            }
            nal_spans.push_back({pkt->data, static_cast<size_t>(pkt->size)});
        }

        const bool inBandUpdated = UpdateH264ParameterSetsFromNalSpans(m_h264ParameterSets, nal_spans);
        if (inBandUpdated) {
            Debug::Log("Captured H264 parameter sets from in-band NALs (count={})", m_h264ParameterSets.size());
        }

        const uint32_t ts = stream->NextTimestamp();
        const bool isKeyPacket = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        const bool shouldSendCodecConfig = (!m_codecConfigSent || isKeyPacket) && !m_h264ParameterSets.empty();
        const std::vector<std::vector<uint8_t>> codecConfigSnapshot = shouldSendCodecConfig ? m_h264ParameterSets : std::vector<std::vector<uint8_t>>{};
        if (shouldSendCodecConfig) {
            bool sentAllConfig = true;
            for (const auto& nal : codecConfigSnapshot) {
                if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                    sentAllConfig = false;
                    break;
                }
                co_await stream->AsyncSendNal(nal.data(), nal.size(), ts, false);
                if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                    sentAllConfig = false;
                    break;
                }
            }

            if (sentAllConfig && generation == m_streamGeneration.load()) {
                m_codecConfigSent = true;
            }
        }

        for (size_t i = 0; i < nal_spans.size(); ++i) {
            if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                break;
            }
            const bool marker = (i + 1 == nal_spans.size());
            co_await stream->AsyncSendNal(nal_spans[i].data, nal_spans[i].size, ts, marker);
            if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                break;
            }
        }

        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_REMOTE_KEY");
        m_localKey = SRTP::Stream::GenerateKey();

        const size_t requestID = package->GetValue<size_t>();
        package->GetValue(m_remoteKey);

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY_RESPONSE,
            std::vector(m_localKey)
        );

        Enable();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST , [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_CAMERAS_SPECIFICATION_LIST");
        const size_t requestID = package->GetValue<size_t>();
        const AVCodec* codec = GetEncoderCodec(CodecID::H264);
        std::vector<CameraSpecification> specifications = FetchCamerasSpecificationForCodec(codec);
        if (specifications.empty()) {
            Debug::LogWarning("Encoder-filtered camera format list is empty, falling back to raw camera formats");
            specifications = FetchCamerasSpecification();
        }

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST_RESPONSE,
            std::move(specifications)
        );
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_PORT_INFO, [instance, this](PC_Package&& package) mutable {
        const uint16_t port = package->GetValue<uint16_t>();
        Debug::Log("Received SRTP port info: {}", port);
        m_portNumber.store(port);
        if (m_portNumber.load() == 0) {
            Disable();
        }
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_START_STREAM");
        const size_t requestID = package->GetValue<size_t>();
        const std::string deviceID = package->GetValue<std::string>();
        const CameraFormat format = package->GetValue<CameraFormat>();

        asio::co_spawn(m_context, StartStream(requestID,deviceID, format), asio::detached);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_STOP_STREAM");
        Disable(true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: ENABLE");
        Enable(true);
    });
}

void NetworkCameraModule::DisableResponseCallbacks() {
    Debug::Log("NetworkCameraModule: DisableResponseCallbacks");
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_PORT_INFO);
}

void NetworkCameraModule::OnInitialize() {
    Debug::Log("NetworkCameraModule: OnInitialize");
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);

    m_portNumber.store(0);

    if (!co_await PermissionManager::RequestCameraAccessPermission()) {
        Disable();
        co_return;
    }

    Debug::Log(
        "NetworkCameraModule: Enabled, waiting for SRTP port, camera permission granted"
    );

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    Debug::Log("NetworkCameraModule: OnDisable");
    m_streamActive.store(false);
    m_streamGeneration.fetch_add(1);

    if (m_videoStream) {
        m_videoStream->Close();
        m_videoStream.reset();
    }

    if (QGuiApplication::instance()) {
        m_qtPipelineStopped.store(false, std::memory_order_release);
        const bool queued = QMetaObject::invokeMethod(
            QGuiApplication::instance(),
            [this, instance]() {
                if (m_videoFrameConnection) {
                    QObject::disconnect(m_videoFrameConnection);
                    m_videoFrameConnection = {};
                }

                if (m_camera) {
                    m_camera->stop();
                }

                if (m_captureSession) {
                    m_captureSession->setVideoSink(nullptr);
                    m_captureSession->setCamera(nullptr);
                }

                m_videoSink.reset();
                m_captureSession.reset();
                m_camera.reset();
                m_qtPipelineStopped.store(true, std::memory_order_release);
            },
            Qt::QueuedConnection
        );

        if (!queued) {
            Debug::LogWarning("NetworkCameraModule: Failed to queue Qt pipeline stop");
            m_qtPipelineStopped.store(true, std::memory_order_release);
        }
    } else {
        m_qtPipelineStopped.store(true, std::memory_order_release);
    }

    asio::steady_timer timer(m_context);
    int waitedMs = 0;
    constexpr int waitStepMs = 10;
    constexpr int waitTimeoutMs = 2000;

    while (
        (
            !m_qtPipelineStopped.load(std::memory_order_acquire) ||
            m_inFlightSendFrames.load(std::memory_order_acquire) != 0
        ) && waitedMs < waitTimeoutMs
    ) {
        timer.expires_after(asio::chrono::milliseconds(waitStepMs));
        std::error_code ec;
        co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        waitedMs += waitStepMs;
    }

    if (!m_qtPipelineStopped.load(std::memory_order_acquire) ||
        m_inFlightSendFrames.load(std::memory_order_acquire) != 0) {
        Debug::LogWarning(
            "NetworkCameraModule: OnDisable timeout waiting for shutdown (qt_stopped={}, in_flight={})",
            m_qtPipelineStopped.load(std::memory_order_acquire),
            m_inFlightSendFrames.load(std::memory_order_acquire)
        );
    }

    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }

    m_h264ParameterSets.clear();
    m_h264LengthSize = 4;
    m_codecConfigSent = false;

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    Debug::Log("NetworkCameraModule: OnShutdown");
    co_await OnDisable();
    co_return;
}

const char* NetworkCameraModule::GetModuleName() const {
    return "NetworkCameraModule";
}

ModuleType NetworkCameraModule::GetModuleType() const {
    return ModuleType::NetworkCamera;
}

