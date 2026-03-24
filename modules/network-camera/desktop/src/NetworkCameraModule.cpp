#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <magic_enum/magic_enum.hpp>
#include <asio.hpp>
#include <asio/co_spawn.hpp>

namespace {
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
}

std::vector<CameraSpecification> NetworkCameraModule::GetCamerasSpecification() const {
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
    for (const auto& spec : m_camerasSpecification) {
        if (spec.id == m_cameraSettings.id) {
            cameraSpecification = spec;
            cameraFound = true;
            break;
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
            m_camerasSpecification.size()
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

    m_codec = GetDecoderCodec(CodecID::H264);

    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }

    m_codecContext = avcodec_alloc_context3(m_codec);
    m_codecContext->bit_rate = 400000;
    m_codecContext->width = m_cameraSettings.width;
    m_codecContext->height = m_cameraSettings.height;
    m_codecContext->time_base = {1, m_cameraSettings.framerate};
    m_codecContext->framerate = {m_cameraSettings.framerate, 1};
    m_codecContext->gop_size = 10;
    m_codecContext->max_b_frames = 1;
    m_codecContext->pix_fmt = AV_PIX_FMT_NV12;

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

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_seenSps = false;
    m_seenPps = false;
    m_waitForIdrAfterLoss.store(false);
    m_acceptFrames.store(true);

    asio::co_spawn(m_context, ReceiveFrames(), asio::detached);
}

void NetworkCameraModule::ProcessEncodedFrame(const std::vector<uint8_t>& frameBuffer) {
    if (!m_acceptFrames.load() || !m_codecContext || !m_packet || !m_frame) {
        return;
    }

    const bool hasIdr = ContainsH264NalType(frameBuffer, 5);
    if (m_waitForIdrAfterLoss.load()) {
        if (!hasIdr) {
            return;
        }
        Debug::Log("Recovered stream sync on IDR after packet loss");
        m_waitForIdrAfterLoss.store(false);
    }

    m_seenSps = m_seenSps || ContainsH264NalType(frameBuffer, 7);
    m_seenPps = m_seenPps || ContainsH264NalType(frameBuffer, 8);
    if (!m_seenSps || !m_seenPps) {
        return;
    }

    av_packet_unref(m_packet);
    if (av_new_packet(m_packet, static_cast<int>(frameBuffer.size())) < 0) {
        return;
    }
    memcpy(m_packet->data, frameBuffer.data(), frameBuffer.size());

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

        const int targetW = m_cameraSettings.width;
        const int targetH = m_cameraSettings.height;
        constexpr AVPixelFormat targetFmt = AV_PIX_FMT_NV12;

        const AVFrame* srcFrame = m_frame;
        const bool needsConvert = (m_frame->format != targetFmt) ||
            (m_frame->width != targetW) ||
            (m_frame->height != targetH) ||
            (m_frame->linesize[0] != targetW) ||
            (m_frame->linesize[1] != targetW);

        if (needsConvert) {
            if (!m_swsContext ||
                m_frameNv12 == nullptr ||
                m_swsWidth != m_frame->width ||
                m_swsHeight != m_frame->height ||
                m_swsDstWidth != targetW ||
                m_swsDstHeight != targetH ||
                m_swsSrcFormat != static_cast<AVPixelFormat>(m_frame->format)) {
                if (m_swsContext) {
                    sws_freeContext(m_swsContext);
                }
                if (m_frameNv12) {
                    av_frame_free(&m_frameNv12);
                }

                m_swsContext = sws_getContext(
                    m_frame->width,
                    m_frame->height,
                    static_cast<AVPixelFormat>(m_frame->format),
                    targetW,
                    targetH,
                    targetFmt,
                    SWS_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr
                );

                if (!m_swsContext) {
                    Debug::LogError("Failed to create sws context for format {}", m_frame->format);
                    av_frame_unref(m_frame);
                    goto cleanup;
                }

                m_frameNv12 = av_frame_alloc();
                if (!m_frameNv12) {
                    Debug::LogError("Failed to allocate NV12 frame");
                    av_frame_unref(m_frame);
                    goto cleanup;
                }

                m_frameNv12->format = targetFmt;
                m_frameNv12->width = targetW;
                m_frameNv12->height = targetH;

                m_swsSrcFormat = static_cast<AVPixelFormat>(m_frame->format);
                m_swsWidth = m_frame->width;
                m_swsHeight = m_frame->height;
                m_swsDstWidth = targetW;
                m_swsDstHeight = targetH;

                if (av_frame_get_buffer(m_frameNv12, 32) < 0) {
                    Debug::LogError("Failed to allocate NV12 frame buffer");
                    av_frame_unref(m_frame);
                    goto cleanup;
                }
            }

            sws_scale(
                m_swsContext,
                m_frame->data,
                m_frame->linesize,
                0,
                m_frame->height,
                m_frameNv12->data,
                m_frameNv12->linesize
            );

            srcFrame = m_frameNv12;
        }

        const int bufferSize = av_image_get_buffer_size(targetFmt, targetW, targetH, 1);
        if (bufferSize <= 0) {
            Debug::LogError("Invalid buffer size for {}x{} fmt {}", targetW, targetH, magic_enum::enum_name(targetFmt));
            av_frame_unref(m_frame);
            goto cleanup;
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(bufferSize));

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
            av_frame_unref(m_frame);
            goto cleanup;
        }
        av_frame_unref(m_frame);

        m_camera.PushFrame(buffer.data());
    }

cleanup:
    av_packet_unref(m_packet);
}

asio::awaitable<void> NetworkCameraModule::ReceiveFrames() {
    std::vector<uint8_t> frameBuffer;
    frameBuffer.reserve(1024 * 1024 * 2); // 2 MiB
    m_receiveFramesRunning.store(true);

    Debug::Log("NetworkCameraModule: ReceiveFrames started");
    Debug::Log("State {}", magic_enum::enum_name(GetModuleState()));

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

        co_await stream->AsyncReceive(frameBuffer);
        if (GetModuleState() != ModuleState::Enabled || !m_acceptFrames.load()) {
            break;
        }

        if (stream->ConsumeReceiveLossSignal()) {
            if (!m_waitForIdrAfterLoss.load()) {
                Debug::LogWarning("RTP loss detected, waiting for next IDR frame");
            }
            m_waitForIdrAfterLoss.store(true);
        }

        if (frameBuffer.empty()) {
            continue;
        }

        ProcessEncodedFrame(frameBuffer);
    }

    m_receiveFramesRunning.store(false);
}

asio::awaitable<void> NetworkCameraModule::UpdateCamerasSpecificationList() {
    constexpr size_t UPDATE_DELAY = 20;

    while (GetModuleState() != ModuleState::Uninitialized) {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
        if (!response.has_value()) {
            Debug::LogWarning("NetworkCameraModule::UpdateCamerasSpecificationList: No response");
        } else {
            response.value()->GetValue(m_camerasSpecification);
        }

        asio::steady_timer timer(m_context);
        timer.expires_after(std::chrono::seconds(UPDATE_DELAY));
        co_await timer.async_wait(asio::use_awaitable);
    }
}


void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE, [instance, this](PC_Package&&) mutable {
        Enable(true);
    });

}

void NetworkCameraModule::DisableResponseCallbacks() {
    Debug::Log("NetworkCameraModule: DisableResponseCallbacks");
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);
}

void NetworkCameraModule::OnInitialize() {
    Debug::Log("NetworkCameraModule: OnInitialize");
    asio::co_spawn(m_context, UpdateCamerasSpecificationList(), asio::detached);
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
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
        if (!response.has_value()) {
            Debug::LogError("No response");
            ProcessError(ModuleFailReason::Timeout);
            co_return;
        }

        response.value()->GetValue(m_remoteKey);
    }

    m_videoStream = std::make_shared<SRTP::Stream>(m_context, m_localKey, m_remoteKey, m_cameraSettings.framerate);
    const UDPEndpoint endpoint = m_videoStream->Bind();
    Debug::Log("SRTP local bind port: {}", endpoint.port());
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_PORT_INFO, endpoint.port());

    {
        CameraFormat cameraFormat(m_cameraSettings.width, m_cameraSettings.height, m_cameraSettings.framerate);
        std::string cameraID = m_cameraSettings.id;

        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM, std::move(cameraID), std::move(cameraFormat));
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

    co_await StartStream();
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    Debug::Log("NetworkCameraModule: OnDisable");
    m_acceptFrames.store(false);
    m_waitForIdrAfterLoss.store(false);

    std::shared_ptr<SRTP::Stream> stream = m_videoStream;
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

    m_seenSps = false;
    m_seenPps = false;

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    Debug::Log("NetworkCameraModule: OnShutdown");
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

