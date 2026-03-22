#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <PermissionManager.h>

#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <asio.hpp>
#include <asio/co_spawn.hpp>

#include <QVideoSink>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QGuiApplication>
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
}

asio::awaitable<void> NetworkCameraModule::StartStream(const size_t requestID, const std::string cameraID, const CameraFormat requestedFormat) {
    if (!QGuiApplication::instance()) {
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
        co_return;
    }

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
                co_return;
            }
            timer.expires_after(asio::chrono::milliseconds(10));
            co_await timer.async_wait();
        }
    }

    QMetaObject::invokeMethod(
        QGuiApplication::instance(),
        [=, this]() {
            QList<QCameraDevice> devices = QMediaDevices::videoInputs();
            const QCameraDevice* cameraDevice = nullptr;

            for (auto& device : devices) {
                if (device.id().toStdString() == cameraID) {
                    cameraDevice = &device;
                }
            }

            if (cameraDevice == nullptr) {
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                return;
            }

            m_camera = std::make_unique<QCamera>(*cameraDevice);

            bool formatFound = false;
            const QList<QCameraFormat> supportedFormats = cameraDevice->videoFormats();
            QCameraFormat format = QCameraFormat();

            for (const auto& fm : supportedFormats) {
                if (fm.resolution().width() != requestedFormat.width || fm.resolution().height() != requestedFormat.height) {
                    continue;
                }

                constexpr float diff = 0.01f;
                if (requestedFormat.framerate < (fm.minFrameRate() - diff) ||
                    requestedFormat.framerate > (fm.maxFrameRate() + diff)) {
                    continue;
                }

                format = fm;

                if (fm.pixelFormat() != QVideoFrameFormat::Format_NV12) {
                    continue;
                }

                m_camera->setCameraFormat(fm);
                formatFound = true;
                break;
            }

            if (!formatFound) {
                if (format.isNull()) {
                    ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                    return;
                }

                m_camera->setCameraFormat(format);
            }

            m_videoSink = std::make_unique<QVideoSink>();

            m_captureSession = std::make_unique<QMediaCaptureSession>();
            m_captureSession->setCamera(m_camera.get());
            m_captureSession->setVideoSink(m_videoSink.get());

            m_camera->start();
            m_codec = GetEncoderCodec(CodecID::H264);

            if (m_codec == nullptr) {
                return;
            }

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

            const AVPixelFormat inputPixFmt = GetFormat(format.pixelFormat());
            const auto pickPixFmt = [](const AVCodec* codec, AVPixelFormat preferred) {
                if (!codec || !codec->pix_fmts) return preferred;
                for (const AVPixelFormat* p = codec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
                    if (*p == preferred) return *p;
                }
                for (const AVPixelFormat* p = codec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
                    if (*p == AV_PIX_FMT_YUV420P) return *p;
                }
                return codec->pix_fmts[0];
            };

            m_codecContext->pix_fmt = pickPixFmt(m_codec, inputPixFmt == AV_PIX_FMT_NONE ? AV_PIX_FMT_NV12 : inputPixFmt);

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

            if (inputPixFmt == AV_PIX_FMT_NONE) {
                Debug::LogError("Unsupported input pixel format ({})", magic_enum::enum_name(format.pixelFormat()));
                return;
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
                    return;
                }
            }

            m_ptsCounter = 0;
            ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::None);
            QGuiApplication::instance()->connect(m_videoSink.get(), &QVideoSink::videoFrameChanged, [&](const QVideoFrame &frame) {
                if (!frame.isValid())
                    return;

                asio::co_spawn(m_moduleStrand, SendFrame(frame), asio::detached);
            });
        },
        Qt::QueuedConnection
    );
}

asio::awaitable<void> NetworkCameraModule::SendFrame(QVideoFrame frame) {
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
        Debug::LogError("SRTP stream is null");
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

    if (inputFmt != m_codecContext->pix_fmt) {
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
            frame.unmap();
            co_return;
        }

        const uint8_t* srcSlice[4] = {};
        int srcStride[4] = {};

        for (int i = 0; i < frame.planeCount(); ++i) {
            srcSlice[i]  = frame.bits(i);
            srcStride[i] = frame.bytesPerLine(i);
        }

        sws_scale(m_swsContext, srcSlice, srcStride, 0, frame.height(), avFrame->data, avFrame->linesize);
    } else {
        const uint8_t* srcSlice[4] = {};
        int srcStride[4] = {};

        for (int i = 0; i < frame.planeCount(); ++i) {
            srcSlice[i]  = frame.bits(i);
            srcStride[i] = frame.bytesPerLine(i);
        }

        av_image_copy(
            avFrame->data,
            avFrame->linesize,
            srcSlice,
            srcStride,
            m_codecContext->pix_fmt,
            frame.width(),
            frame.height()
        );
    }

    frame.unmap();

    AVPacket *pkt = av_packet_alloc();
    int ret = avcodec_send_frame(m_codecContext, avFrame);

    av_frame_free(&avFrame);

    if (ret < 0) {
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
        if ((!m_codecConfigSent || isKeyPacket) && !m_h264ParameterSets.empty()) {
            for (const auto& nal : m_h264ParameterSets) {
                co_await stream->AsyncSendNal(nal.data(), nal.size(), ts, false);
            }
            m_codecConfigSent = true;
        }

        for (size_t i = 0; i < nal_spans.size(); ++i) {
            const bool marker = (i + 1 == nal_spans.size());
            co_await stream->AsyncSendNal(nal_spans[i].data, nal_spans[i].size, ts, marker);
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
        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST_RESPONSE,
            FetchCamerasSpecification()
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
    m_videoStream.reset();
    m_h264ParameterSets.clear();
    m_h264LengthSize = 4;
    m_codecConfigSent = false;

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    Debug::Log("NetworkCameraModule: OnShutdown");

    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }

    co_return;
}
