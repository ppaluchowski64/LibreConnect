#ifdef IOS_DEVICE

#include <NetworkCameraModule.h>
#include <magic_enum/magic_enum.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <set>

extern "C" {
    #include <libavutil/error.h>
    #include <libavutil/imgutils.h>
}

std::vector<CameraSpecification> NetworkCameraModule::FetchCamerasSpecificationForCodec(const AVCodec* codec) {
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
            if (codec && !CameraUtilitiesLC::IsCameraFormatSupportedByCodec(codec, format, fps)) {
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

asio::awaitable<void> NetworkCameraModule::StopStream_IOS() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

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

    if (m_reusableSendFrame) {
        av_frame_free(&m_reusableSendFrame);
    }

    if (m_reusableAndroidConvertFrame) {
        av_frame_free(&m_reusableAndroidConvertFrame);
    }

    if (m_reusableEncodePacket) {
        av_packet_free(&m_reusableEncodePacket);
    }
}

void NetworkCameraModule::StartStream_IOS(const size_t requestID, const std::string& cameraID, const CameraFormat requestedFormat) {
    if (!QGuiApplication::instance()) {
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
        ProcessError(ModuleFailReason::InternalError);
        return;
    }

    const uint64_t generation = m_streamGeneration.fetch_add(1) + 1;

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

                if (m_reusableSendFrame) {
                    av_frame_free(&m_reusableSendFrame);
                }

                if (m_reusableAndroidConvertFrame) {
                    av_frame_free(&m_reusableAndroidConvertFrame);
                }

                if (m_reusableEncodePacket) {
                    av_packet_free(&m_reusableEncodePacket);
                }

                m_h264ParameterSets.clear();
                m_h264LengthSize = 4;
                m_codecConfigSent = false;
                m_droppedFramesBackpressure.store(0, std::memory_order_relaxed);

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

                    if (!CameraUtilitiesLC::IsCameraFormatSupportedByCodec(m_codec, fm, static_cast<int>(requestedFormat.framerate))) {
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
                m_codecContext->gop_size = std::max(8, fps / 2);
                m_codecContext->max_b_frames = 0;

                const AVPixelFormat inputPixFmt = CameraUtilitiesLC::ToAVPixelFormat(format.pixelFormat());
                m_codecContext->pix_fmt = CameraUtilitiesLC::PickEncoderPixelFormat(m_codec, inputPixFmt);
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

                m_h264ParameterSets = CameraUtilitiesLC::ExtractH264ParameterSets(m_codecContext);
                m_h264LengthSize = CameraUtilitiesLC::ExtractAvccNalLengthSize(m_codecContext);
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

                    if (!TryReserveFrameSlot("qt")) {
                        return;
                    }

                    asio::co_spawn(m_moduleStrand, SendFrame_IOS(frame), asio::detached);
                }
            );
        },
    Qt::QueuedConnection
    );
}

asio::awaitable<void> NetworkCameraModule::SendFrame_IOS(QVideoFrame frame) {
    const uint64_t generation = m_streamGeneration.load();

    const auto slotDeleter = [this](const int* p) {
        (void)p;
        ReleaseFrameSlot();
    };

    std::unique_ptr<int, decltype(slotDeleter)> slotGuard(reinterpret_cast<int*>(1), slotDeleter);

    if (!m_streamActive.load()) {
        co_return;
    }

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

    if (!CameraUtilitiesLC::EnsureReusableFrameBuffer(
        m_reusableSendFrame,
        m_codecContext->pix_fmt,
        m_codecContext->width,
        m_codecContext->height
    )) {
        Debug::LogError("NetworkCameraModule: Could not prepare reusable send frame");
        frame.unmap();
        co_return;
    }
    AVFrame* avFrame = m_reusableSendFrame;
    avFrame->pts = m_ptsCounter++;

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

    co_await EncodeAndSendFrame(avFrame, generation);
}

asio::awaitable<void> NetworkCameraModule::EncodeAndSendFrame(const AVFrame* avFrame, const uint64_t generation) {
    if (!avFrame) {
        co_return;
    }

    if (!m_codecContext) {
        co_return;
    }

    const std::shared_ptr<SRTP::Stream> stream = m_videoStream;
    if (!stream) {
        co_return;
    }

    if (!m_reusableEncodePacket) {
        m_reusableEncodePacket = av_packet_alloc();
    }
    AVPacket* pkt = m_reusableEncodePacket;
    if (!pkt) {
        Debug::LogError("NetworkCameraModule: Failed to allocate reusable encode packet");
        co_return;
    }
    av_packet_unref(pkt);

    int ret = avcodec_send_frame(m_codecContext, avFrame);

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            co_return;
        }

        char err[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(ret, err, sizeof(err));
        Debug::LogError("Error sending frame to encoder: {}", err);
        co_return;
    }

    while (true) {
        ret = avcodec_receive_packet(m_codecContext, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            Debug::LogError("Error during encoding");
            break;
        }

        size_t newExtraDataSize = 0;
        const uint8_t* newExtradata = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &newExtraDataSize);
        if (newExtradata && newExtraDataSize > 0) {
            const bool updated = CameraUtilitiesLC::UpdateH264ParameterSetsFromExtradata(
                m_h264ParameterSets,
                m_h264LengthSize,
                newExtradata,
                newExtraDataSize
            );
            if (updated) {
                Debug::Log("Updated H264 parameter sets from packet side-data (count={})", m_h264ParameterSets.size());
            }
        }

        std::vector<CameraUtilitiesLC::NalSpan> nal_spans;
        if (CameraUtilitiesLC::IsAnnexB(pkt->data, pkt->size)) {
            CameraUtilitiesLC::SplitAnnexB(pkt->data, pkt->size, nal_spans);
        } else {
            CameraUtilitiesLC::SplitAvccAuto(pkt->data, pkt->size, m_h264LengthSize, nal_spans);
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

        const bool inBandUpdated = CameraUtilitiesLC::UpdateH264ParameterSetsFromNalSpans(m_h264ParameterSets, nal_spans);
        if (inBandUpdated) {
            Debug::Log("Captured H264 parameter sets from in-band NALs (count={})", m_h264ParameterSets.size());
        }

        const uint32_t ts = stream->NextTimestamp();
        const bool isKeyPacket = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        if (m_waitForKeyframeAfterDrop.load(std::memory_order_relaxed) && !isKeyPacket) {
            av_packet_unref(pkt);
            continue;
        }
        if (isKeyPacket) {
            m_waitForKeyframeAfterDrop.store(false, std::memory_order_relaxed);
        }
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
}

#endif
