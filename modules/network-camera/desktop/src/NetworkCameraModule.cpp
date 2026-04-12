#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <magic_enum/magic_enum.hpp>
#include <asio.hpp>
#include <asio/co_spawn.hpp>
#include <algorithm>
#include <array>
#include <chrono>

extern "C" {
    #include <libavutil/pixdesc.h>
}

namespace {
    constexpr uint64_t kMaxWaitForIdrAfterLossMs = 2000;
    constexpr int64_t kFullHdPixels = 1920 * 1080;
    constexpr uint64_t kBusyDecodeDropLogEvery = 30;

    uint64_t GetMonotonicTimeMs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    AVPixelFormat NormalizeDeprecatedYuvjFormat(const AVPixelFormat format) {
        switch (format) {
            case AV_PIX_FMT_YUVJ420P:
                return AV_PIX_FMT_YUV420P;
            case AV_PIX_FMT_YUVJ422P:
                return AV_PIX_FMT_YUV422P;
            case AV_PIX_FMT_YUVJ444P:
                return AV_PIX_FMT_YUV444P;
            case AV_PIX_FMT_YUVJ440P:
                return AV_PIX_FMT_YUV440P;
            case AV_PIX_FMT_YUVJ411P:
                return AV_PIX_FMT_YUV411P;
            default:
                return format;
        }
    }

    bool IsFullRangeFrame(const AVFrame* frame) {
        if (!frame) {
            return false;
        }

        if (frame->color_range == AVCOL_RANGE_JPEG) {
            return true;
        }

        const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
        return srcFmt == AV_PIX_FMT_YUVJ420P ||
            srcFmt == AV_PIX_FMT_YUVJ422P ||
            srcFmt == AV_PIX_FMT_YUVJ444P ||
            srcFmt == AV_PIX_FMT_YUVJ440P ||
            srcFmt == AV_PIX_FMT_YUVJ411P;
    }

    int ResolveSwsColorspace(const AVFrame* frame) {
        if (!frame) {
            return SWS_CS_DEFAULT;
        }

        switch (frame->colorspace) {
            case AVCOL_SPC_BT709:
                return SWS_CS_ITU709;
            case AVCOL_SPC_BT470BG:
            case AVCOL_SPC_SMPTE170M:
                return SWS_CS_SMPTE170M;
            case AVCOL_SPC_SMPTE240M:
                return SWS_CS_SMPTE240M;
            default:
                // Prefer BT.709 fallback for HD frames; SD uses SMPTE170M.
                return (frame->width >= 1280 || frame->height > 576) ? SWS_CS_ITU709 : SWS_CS_SMPTE170M;
        }
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

    bool ContainsH264NalType(const std::vector<uint8_t>& frame, const uint8_t nalType) {
        const size_t size = frame.size();
        if (size < 5) {
            return false;
        }

        size_t pos = 0;
        while (pos < size) {
            const size_t start = FindStart(frame.data(), frame.size(), pos);
            if (start >= size) {
                break;
            }

            const size_t scSize = (frame[start + 2] == 0x01) ? 3 : 4;
            const size_t nalStart = start + scSize;
            if (nalStart < size && (frame[nalStart] & 0x1F) == nalType) {
                return true;
            }

            pos = nalStart;
        }

        return false;
    }

    bool ContainsH265NalType(const std::vector<uint8_t>& frame, const uint8_t nalType) {
        const size_t size = frame.size();
        if (size < 6) {
            return false;
        }

        size_t pos = 0;
        while (pos < size) {
            const size_t start = FindStart(frame.data(), frame.size(), pos);
            if (start >= size) {
                break;
            }

            const size_t scSize = (frame[start + 2] == 0x01) ? 3 : 4;
            const size_t nalStart = start + scSize;
            if (nalStart + 1 < size) {
                const uint8_t type = static_cast<uint8_t>((frame[nalStart] >> 1) & 0x3F);
                if (type == nalType) {
                    return true;
                }
            }

            pos = nalStart;
        }

        return false;
    }

    SRTP::VideoCodec ToSrtpVideoCodec(const CodecID codecId) {
        return codecId == CodecID::H265 ? SRTP::VideoCodec::H265 : SRTP::VideoCodec::H264;
    }

    ModuleFailReason ToModuleFailReason(const StreamStartFailReason reason) {
        switch (reason) {
            case StreamStartFailReason::IncorrectConfig:
                return ModuleFailReason::IncorrectConfig;
            case StreamStartFailReason::InternalError:
                return ModuleFailReason::InternalError;
            case StreamStartFailReason::None:
                break;
        }

        return ModuleFailReason::Unknown;
    }

    bool IsHardwarePixelFormat(const AVPixelFormat format) {
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
        return desc && ((desc->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0);
    }
}

std::vector<CameraSpecification> NetworkCameraModule::GetCamerasSpecification() const {
    std::scoped_lock lock(m_camerasSpecificationMutex);
    return m_camerasSpecification;
}

void NetworkCameraModule::SetCameraSettings(CameraSettings settings) {
    asio::post(m_context, [this, settings]() {
        m_cameraSettings = settings;
    });
}

asio::awaitable<void> NetworkCameraModule::StartStream() {
    Debug::Log("NetworkCameraModule: StartStream");
    CameraSpecification cameraSpecification;
    bool cameraFound = false;

    {
        std::scoped_lock lock(m_camerasSpecificationMutex);
        for (const auto& spec : m_camerasSpecification) {
            if (spec.id == m_cameraSettings.id) {
                cameraSpecification = spec;
                cameraFound = true;
                break;
            }
        }
    }

    std::string cameraName;
    if (m_cameraSettings.customCameraNameEnabled && !m_cameraSettings.cameraName.empty()) {
        cameraName = m_cameraSettings.cameraName;
    } else if (cameraFound) {
        cameraName = cameraSpecification.description;
    } else {
        Debug::LogError(
            "NetworkCameraModule::StartStream: Camera id '{}' not found in {} reported camera specifications and no custom name provided",
            m_cameraSettings.id,
            GetCamerasSpecification().size()
        );
        ProcessError(ModuleFailReason::IncorrectConfig);
        co_return;
    }

    if (cameraName.empty()) {
        Debug::LogError(
            "NetworkCameraModule::StartStream: Selected camera id '{}' resolved to empty name",
            m_cameraSettings.id
        );
        ProcessError(ModuleFailReason::IncorrectConfig);
        co_return;
    }

    if (!m_camera.Start(cameraName, m_cameraSettings.pixelFormat, m_cameraSettings.width, m_cameraSettings.height, m_cameraSettings.framerate)) {
        Debug::LogError("NetworkCameraModule::StartStream Failed to start camera");
        ProcessError(ModuleFailReason::InternalError);
        co_return;
    }

    const int64_t pixels = static_cast<int64_t>(m_cameraSettings.width) * static_cast<int64_t>(m_cameraSettings.height);
    const CodecID targetCodecId = (pixels > kFullHdPixels) ? CodecID::H265 : CodecID::H264;
    m_activeCodecId = targetCodecId;

    m_codec = GetDecoderCodec(targetCodecId);
    if (!m_codec) {
        Debug::LogError("Failed to find FFmpeg decoder for requested codec");
        ProcessError(ModuleFailReason::InternalError);
        co_return;
    }

    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }

    m_codecContext = avcodec_alloc_context3(m_codec);
    m_codecContext->width = m_cameraSettings.width;
    m_codecContext->height = m_cameraSettings.height;
    const int decodeFps = std::max(1, static_cast<int>(m_cameraSettings.framerate));
    m_codecContext->framerate = {decodeFps, 1};
    m_codecContext->time_base = {1, decodeFps};
    m_codecContext->pkt_timebase = m_codecContext->time_base;

    m_codecContext->thread_count = 0;
    m_codecContext->thread_type = FF_THREAD_SLICE;

    m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;

    {
        const int openRet = avcodec_open2(m_codecContext, m_codec, nullptr);
        if (openRet < 0) {
            char err[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(openRet, err, sizeof(err));
            Debug::LogError("Failed to open decoder: {}", err);
            ProcessError(ModuleFailReason::InternalError);
            co_return;
        }
    }

    Debug::Log(
        "NetworkCameraModule: Selected decoder '{}' (hardware={})",
        (m_codec && m_codec->name) ? m_codec->name : "unknown",
        (m_codec && (m_codec->capabilities & AV_CODEC_CAP_HARDWARE) ? "true" : "false")
    );

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_seenVps = false;
    m_seenSps = false;
    m_seenPps = false;
    m_waitForIdrAfterLoss.store(false);
    m_waitForIdrStartMs.store(0);
    m_waitForIdrDroppedFrames.store(0);
    m_decodePacketPts.store(0);
    m_acceptFrames.store(true);

    asio::co_spawn(m_context, ReceiveFrames(), asio::detached);
}

void NetworkCameraModule::ProcessEncodedFrame(const std::vector<uint8_t>& frameBuffer) {
    if (!m_acceptFrames.load() || !m_codecContext || !m_packet || !m_frame) {
        return;
    }

    const uint64_t nowMs = GetMonotonicTimeMs();
    const bool usingH265 = (m_activeCodecId == CodecID::H265);
    const bool hasIdr = usingH265
        ? (ContainsH265NalType(frameBuffer, 19) || ContainsH265NalType(frameBuffer, 20) || ContainsH265NalType(frameBuffer, 21))
        : ContainsH264NalType(frameBuffer, 5);

    if (m_waitForIdrAfterLoss.load()) {
        if (!hasIdr) {
            const uint64_t waitStartMs = m_waitForIdrStartMs.load();
            const uint64_t waitMs = (waitStartMs != 0 && nowMs >= waitStartMs) ? (nowMs - waitStartMs) : 0;
            const uint32_t droppedFrames = m_waitForIdrDroppedFrames.fetch_add(1) + 1;

            if (waitMs < kMaxWaitForIdrAfterLossMs) {
                return;
            }

            Debug::LogWarning(
                "IDR wait timeout after packet loss ({} ms, {} dropped frames); resuming decode without IDR",
                waitMs,
                droppedFrames
            );
            m_waitForIdrAfterLoss.store(false);
            m_waitForIdrStartMs.store(0);
            m_waitForIdrDroppedFrames.store(0);
        } else {
            const uint64_t waitStartMs = m_waitForIdrStartMs.load();
            const uint64_t waitMs = (waitStartMs != 0 && nowMs >= waitStartMs) ? (nowMs - waitStartMs) : 0;
            const uint32_t droppedFrames = m_waitForIdrDroppedFrames.exchange(0);
            Debug::Log("Recovered stream sync on IDR after packet loss ({} ms, {} dropped frames)", waitMs, droppedFrames);
            m_waitForIdrAfterLoss.store(false);
            m_waitForIdrStartMs.store(0);
        }
    }

    if (usingH265) {
        m_seenVps = m_seenVps || ContainsH265NalType(frameBuffer, 32);
        m_seenSps = m_seenSps || ContainsH265NalType(frameBuffer, 33);
        m_seenPps = m_seenPps || ContainsH265NalType(frameBuffer, 34);
        if (!m_seenVps || !m_seenSps || !m_seenPps) {
            return;
        }
    } else {
        m_seenSps = m_seenSps || ContainsH264NalType(frameBuffer, 7);
        m_seenPps = m_seenPps || ContainsH264NalType(frameBuffer, 8);
        if (!m_seenSps || !m_seenPps) {
            return;
        }
    }

    if (frameBuffer.empty()) {
        return;
    }

    av_packet_unref(m_packet);
    if (av_new_packet(m_packet, static_cast<int>(frameBuffer.size())) < 0) {
        return;
    }
    memcpy(m_packet->data, frameBuffer.data(), frameBuffer.size());
    const int64_t decodePts = m_decodePacketPts.fetch_add(1, std::memory_order_relaxed);
    m_packet->pts = decodePts;
    m_packet->dts = decodePts;
    m_packet->duration = 1;
    m_packet->time_base = m_codecContext->pkt_timebase;

    int ret = avcodec_send_packet(m_codecContext, m_packet);
    if (ret < 0) goto cleanup;

    while (true) {
        ret = avcodec_receive_frame(m_codecContext, m_frame);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }

        if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            break;
        }

        const AVPixelFormat decodedFormat = static_cast<AVPixelFormat>(m_frame->format);
        const bool decodedWithHardwareFormat = IsHardwarePixelFormat(decodedFormat);
        const AVFrame* decodeFrame = m_frame;

        if (decodedWithHardwareFormat) {
            if (!m_hwTransferFrame) {
                m_hwTransferFrame = av_frame_alloc();
                if (!m_hwTransferFrame) {
                    Debug::LogError("Failed to allocate frame for hardware decode transfer");
                    av_frame_unref(m_frame);
                    goto cleanup;
                }
            }

            av_frame_unref(m_hwTransferFrame);
            const int transferRet = av_hwframe_transfer_data(m_hwTransferFrame, m_frame, 0);
            if (transferRet < 0) {
                char transferErr[AV_ERROR_MAX_STRING_SIZE]{};
                av_strerror(transferRet, transferErr, sizeof(transferErr));
                Debug::LogError("Failed to transfer hardware decoded frame to system memory: {}", transferErr);
                av_frame_unref(m_frame);
                goto cleanup;
            }

            decodeFrame = m_hwTransferFrame;
        }

        auto ReleaseDecodedFrames = [&]() {
            av_frame_unref(m_frame);
            if (decodedWithHardwareFormat && m_hwTransferFrame) {
                av_frame_unref(m_hwTransferFrame);
            }
        };

        const int targetW = m_cameraSettings.width;
        const int targetH = m_cameraSettings.height;
        constexpr AVPixelFormat targetFmt = AV_PIX_FMT_NV12;
        const AVPixelFormat swsSrcFormat = NormalizeDeprecatedYuvjFormat(static_cast<AVPixelFormat>(decodeFrame->format));

        const AVFrame* srcFrame = decodeFrame;
        const bool needsConvert = (decodeFrame->format != targetFmt) ||
            (decodeFrame->width != targetW) ||
            (decodeFrame->height != targetH);

        if (needsConvert) {
            if (!m_swsContext ||
                m_frameNv12 == nullptr ||
                m_swsWidth != decodeFrame->width ||
                m_swsHeight != decodeFrame->height ||
                m_swsDstWidth != targetW ||
                m_swsDstHeight != targetH ||
                m_swsSrcFormat != swsSrcFormat) {

                if (m_swsContext) {
                    sws_freeContext(m_swsContext);
                }
                if (m_frameNv12) {
                    av_frame_free(&m_frameNv12);
                }

                m_swsContext = sws_getContext(
                    decodeFrame->width,
                    decodeFrame->height,
                    swsSrcFormat,
                    targetW,
                    targetH,
                    targetFmt,
                    SWS_FAST_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr
                );

                if (!m_swsContext) {
                    Debug::LogError("Failed to create sws context for format {}", decodeFrame->format);
                    ReleaseDecodedFrames();
                    goto cleanup;
                }

                m_frameNv12 = av_frame_alloc();
                if (!m_frameNv12) {
                    Debug::LogError("Failed to allocate NV12 frame");
                    ReleaseDecodedFrames();
                    goto cleanup;
                }

                m_frameNv12->format = targetFmt;
                m_frameNv12->width = targetW;
                m_frameNv12->height = targetH;

                m_swsSrcFormat = swsSrcFormat;
                m_swsWidth = decodeFrame->width;
                m_swsHeight = decodeFrame->height;
                m_swsDstWidth = targetW;
                m_swsDstHeight = targetH;

                if (av_frame_get_buffer(m_frameNv12, 32) < 0) {
                    Debug::LogError("Failed to allocate NV12 frame buffer");
                    ReleaseDecodedFrames();
                    goto cleanup;
                }
            }

            const int srcRange = IsFullRangeFrame(decodeFrame) ? 1 : 0;
            const int dstRange = 0;
            const int* cs = sws_getCoefficients(ResolveSwsColorspace(decodeFrame));
            if (cs) {
                const int setCsRet = sws_setColorspaceDetails(
                    m_swsContext,
                    cs,
                    srcRange,
                    cs,
                    dstRange,
                    0,
                    1 << 16,
                    1 << 16
                );
                if (setCsRet < 0) {
                    static bool loggedColorRangeSetupError = false;
                    if (!loggedColorRangeSetupError) {
                        Debug::LogWarning("Failed to set sws colorspace/range details");
                        loggedColorRangeSetupError = true;
                    }
                }
            }

            sws_scale(
                m_swsContext,
                decodeFrame->data,
                decodeFrame->linesize,
                0,
                decodeFrame->height,
                m_frameNv12->data,
                m_frameNv12->linesize
            );

            srcFrame = m_frameNv12;
        }

        const int bufferSize = av_image_get_buffer_size(targetFmt, targetW, targetH, 1);
        if (bufferSize <= 0) {
            Debug::LogError("Invalid buffer size for {}x{} fmt {}", targetW, targetH, magic_enum::enum_name(targetFmt));
            ReleaseDecodedFrames();
            goto cleanup;
        }

        std::vector<uint8_t> buffer;
        buffer.resize(static_cast<size_t>(bufferSize));

        const int copyRet = av_image_copy_to_buffer(
            buffer.data(),
            buffer.size(),
            srcFrame->data,
            srcFrame->linesize,
            targetFmt,
            targetW,
            targetH,
            1
        );
        if (copyRet < 0) {
            Debug::LogError("Failed to copy image to buffer");
            ReleaseDecodedFrames();
            goto cleanup;
        }
        ReleaseDecodedFrames();

        m_camera.PushFrame(buffer.data());
    }

cleanup:
    av_packet_unref(m_packet);
}

asio::awaitable<void> NetworkCameraModule::ReceiveFrames() {
    struct ReceivePipelineState {
        std::array<std::vector<uint8_t>, 2> frameBuffers;
        std::vector<uint8_t> pendingLatestFrame;
        std::mutex pendingLatestFrameMutex;
        std::atomic<bool> hasPendingLatest{false};
        std::atomic<bool> isProcessing{false};
    };

    auto pipelineState = std::make_shared<ReceivePipelineState>();
    for (auto& buffer : pipelineState->frameBuffers) {
        buffer.reserve(1024 * 1024 * 2); // 2 MiB
    }
    pipelineState->pendingLatestFrame.reserve(1024 * 1024 * 2); // 2 MiB
    size_t receiveBufferIndex = 0;
    bool pendingDecoderFlush = false;
    uint64_t receivedEncodedFrames = 0;
    uint64_t queuedForDecodeFrames = 0;
    uint64_t droppedBusyFrames = 0;
    uint64_t stashedLatestFrames = 0;

    m_receiveFramesRunning.store(true);

    Debug::Log("NetworkCameraModule: ReceiveFrames started");
    asio::steady_timer timer(m_context);

    while (GetModuleState() == ModuleState::Enabling) {
        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait(asio::use_awaitable);
    }

    while (GetModuleState() == ModuleState::Enabled && m_acceptFrames.load()) {
        const std::shared_ptr<SRTP::Stream> stream = m_videoStream;
        if (!stream) {
            break;
        }

        co_await stream->AsyncReceive(pipelineState->frameBuffers[receiveBufferIndex]);
        if (GetModuleState() != ModuleState::Enabled || !m_acceptFrames.load()) {
            break;
        }

        if (stream->ConsumeReceiveLossSignal()) {
            bool expected = false;
            if (m_waitForIdrAfterLoss.compare_exchange_strong(expected, true)) {
                Debug::LogWarning("RTP loss detected, waiting for next IDR frame");
                m_waitForIdrStartMs.store(GetMonotonicTimeMs());
                m_waitForIdrDroppedFrames.store(0);
                if (pipelineState->isProcessing.load()) {
                    pendingDecoderFlush = true;
                } else if (m_codecContext) {
                    avcodec_flush_buffers(m_codecContext);
                }
            }
        }

        if (pipelineState->frameBuffers[receiveBufferIndex].empty()) {
            continue;
        }

        ++receivedEncodedFrames;

        if (pendingDecoderFlush && !pipelineState->isProcessing.load()) {
            if (m_codecContext) {
                avcodec_flush_buffers(m_codecContext);
                Debug::Log("NetworkCameraModule: Flushed decoder buffers after receive-path drop");
            }
            pendingDecoderFlush = false;
        }

        if (pipelineState->isProcessing.exchange(true)) {
            ++droppedBusyFrames;
            ++stashedLatestFrames;
            {
                std::scoped_lock pendingLock(pipelineState->pendingLatestFrameMutex);
                pipelineState->pendingLatestFrame = pipelineState->frameBuffers[receiveBufferIndex];
            }
            pipelineState->hasPendingLatest.store(true, std::memory_order_release);
            if ((droppedBusyFrames % kBusyDecodeDropLogEvery) == 1) {
                Debug::LogWarning(
                    "NetworkCameraModule: Dropping frame (decoder busy / codec pipeline saturated). dropped_busy={}, stashed_latest={}, received={}, queued_for_decode={}, codec={}, wait_for_idr={}, pending_decoder_flush={}",
                    droppedBusyFrames,
                    stashedLatestFrames,
                    receivedEncodedFrames,
                    queuedForDecodeFrames,
                    (m_activeCodecId == CodecID::H265 ? "H265" : "H264"),
                    m_waitForIdrAfterLoss.load(),
                    pendingDecoderFlush
                );
            }
            continue;
        }

        const size_t processBufferIndex = receiveBufferIndex;
        receiveBufferIndex = (receiveBufferIndex + 1) % pipelineState->frameBuffers.size();
        ++queuedForDecodeFrames;

        asio::post(m_context, [this, pipelineState, processBufferIndex]() mutable {
            ProcessEncodedFrame(pipelineState->frameBuffers[processBufferIndex]);

            while (m_acceptFrames.load(std::memory_order_relaxed)) {
                if (!pipelineState->hasPendingLatest.exchange(false, std::memory_order_acq_rel)) {
                    break;
                }

                std::vector<uint8_t> latestFrame;
                {
                    std::scoped_lock pendingLock(pipelineState->pendingLatestFrameMutex);
                    latestFrame.swap(pipelineState->pendingLatestFrame);
                }

                if (latestFrame.empty()) {
                    continue;
                }

                ProcessEncodedFrame(latestFrame);
            }

            pipelineState->isProcessing.store(false);
        });
    }

    while (pipelineState->isProcessing.load()) {
        timer.expires_after(std::chrono::milliseconds(1));
        co_await timer.async_wait(asio::use_awaitable);
    }

    if (droppedBusyFrames > 0) {
        Debug::LogWarning(
            "NetworkCameraModule: ReceiveFrames summary: dropped_busy={}, stashed_latest={}, received={}, queued_for_decode={}",
            droppedBusyFrames,
            stashedLatestFrames,
            receivedEncodedFrames,
            queuedForDecodeFrames
        );
    }

    m_receiveFramesRunning.store(false);
}

asio::awaitable<void> NetworkCameraModule::UpdateCamerasSpecificationList() {
    constexpr size_t UPDATE_DELAY = 20;

    while (GetModuleState() != ModuleState::Uninitialized) {
        if (GetModuleState() == ModuleState::Disabled) {
            const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
            if (!response.has_value()) {
                Debug::LogWarning("NetworkCameraModule::UpdateCamerasSpecificationList: No response");
            } else {
                std::vector<CameraSpecification> updatedSpecifications;
                response.value()->GetValue(updatedSpecifications);
                {
                    std::scoped_lock lock(m_camerasSpecificationMutex);
                    m_camerasSpecification = std::move(updatedSpecifications);
                }
            }
        }

        asio::steady_timer timer(m_context);
        timer.expires_after(std::chrono::seconds(UPDATE_DELAY));
        co_await timer.async_wait(asio::use_awaitable);
    }
}


void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE, [instance, this](PC_Package&&) mutable {
        if (GetModuleState() == ModuleState::Enabled) {
            ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, true);
            return;
        }
        Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, [instance, this](PC_Package&& package) mutable {
       m_peerModuleEnabled.store(package->GetValue<bool>());
    });

}

void NetworkCameraModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED);
}

void NetworkCameraModule::OnInitialize() {
    asio::co_spawn(m_context, UpdateCamerasSpecificationList(), asio::detached);
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);

    Debug::Log(
        "NetworkCameraModule: Enabled, cameraId={}, {}x{}@{}",
        m_cameraSettings.id,
        m_cameraSettings.width,
        m_cameraSettings.height,
        m_cameraSettings.framerate
    );

    m_localKey = SRTP::Stream::GenerateKey();

    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY, std::vector(m_localKey));
        if (ShouldAbortEnable()) {
            co_return;
        }

        if (!response.has_value()) {
            Debug::LogError("No response");
            ProcessError(ModuleFailReason::Timeout);
            co_return;
        }

        response.value()->GetValue(m_remoteKey);
    }

    const int64_t requestedPixels = static_cast<int64_t>(m_cameraSettings.width) * static_cast<int64_t>(m_cameraSettings.height);
    m_activeCodecId = (requestedPixels > kFullHdPixels) ? CodecID::H265 : CodecID::H264;
    m_videoStream = std::make_shared<SRTP::Stream>(
        m_context,
        m_localKey,
        m_remoteKey,
        m_cameraSettings.framerate,
        ToSrtpVideoCodec(m_activeCodecId)
    );
    const UDPEndpoint endpoint = m_videoStream->Bind();
    Debug::Log("SRTP local bind port: {}", endpoint.port());
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_PORT_INFO, endpoint.port());

    {
        CameraFormat cameraFormat(m_cameraSettings.width, m_cameraSettings.height, m_cameraSettings.framerate);
        std::string cameraID = m_cameraSettings.id;

        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM, std::move(cameraID), std::move(cameraFormat));
        if (ShouldAbortEnable()) {
            co_return;
        }

        if (!response.has_value()) {
            Debug::LogError("No response");
            ProcessError(ModuleFailReason::Timeout);
            co_return;
        }

        const StreamStartFailReason reason = response.value()->GetValue<StreamStartFailReason>();
        if (reason != StreamStartFailReason::None) {
            Debug::LogError("Failed to start stream: {}", magic_enum::enum_name(reason));
            ProcessError(ToModuleFailReason(reason));
            co_return;
        }
    }

    asio::steady_timer timer(m_context);
    while (!m_peerModuleEnabled.load()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }

    co_await StartStream();
    if (ShouldAbortEnable()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, true);
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, false);
    m_acceptFrames.store(false);
    m_waitForIdrAfterLoss.store(false);
    m_waitForIdrStartMs.store(0);
    m_waitForIdrDroppedFrames.store(0);
    m_decodePacketPts.store(0);

    const std::shared_ptr<SRTP::Stream> stream = m_videoStream;
    m_videoStream.reset();
    if (stream) {
        stream->Close();
    }

    asio::steady_timer stopWait(m_context);
    for (int i = 0; i < 100 && m_receiveFramesRunning.load(); ++i) {
        stopWait.expires_after(std::chrono::milliseconds(10));
        co_await stopWait.async_wait(asio::use_awaitable);
    }

    m_camera.Stop();
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);

    if (m_packet) {
        av_packet_free(&m_packet);
    }

    if (m_frame) {
        av_frame_free(&m_frame);
    }

    if (m_hwTransferFrame) {
        av_frame_free(&m_hwTransferFrame);
    }

    if (m_frameNv12) {
        av_frame_free(&m_frameNv12);
    }

    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
        m_swsSrcFormat = AV_PIX_FMT_NONE;
        m_swsWidth = 0;
        m_swsHeight = 0;
    }

    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    m_seenVps = false;
    m_seenSps = false;
    m_seenPps = false;
    m_waitForIdrStartMs.store(0);
    m_waitForIdrDroppedFrames.store(0);
    m_decodePacketPts.store(0);

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_acceptFrames.store(false);
    if (m_videoStream) {
        m_videoStream->Close();
        m_videoStream.reset();
    }
    m_camera.Stop();
    co_return;
}

const char* NetworkCameraModule::GetModuleName() const {
    return "NetworkCameraModule";
}

ModuleType NetworkCameraModule::GetModuleType() const {
    return ModuleType::NetworkCamera;
}

