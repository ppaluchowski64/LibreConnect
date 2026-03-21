#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <magic_enum/magic_enum.hpp>
#include <asio.hpp>
#include <asio/co_spawn.hpp>

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
    for (const auto& spec : m_camerasSpecification) {
        if (spec.id == m_cameraSettings.id) {
            cameraSpecification = spec;
        }
    }

    const std::string& cameraName = m_cameraSettings.customCameraNameEnabled ? m_cameraSettings.cameraName : cameraSpecification.description;
    if (!m_camera.Start(cameraName, m_cameraSettings.pixelFormat, m_cameraSettings.width, m_cameraSettings.height, m_cameraSettings.framerate)) {
        Debug::LogError("NetworkCameraModule::StartStream Failed to start camera");
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
            co_return;
        }
    }

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();

    asio::co_spawn(m_context, ReceiveFrames(), asio::detached);
}

void NetworkCameraModule::ProcessEncodedFrame(const std::vector<uint8_t>& frameBuffer) {
    av_new_packet(m_packet, frameBuffer.size());
    memcpy(m_packet->data, frameBuffer.data(), frameBuffer.size());

    int ret = avcodec_send_packet(m_codecContext, m_packet);
    if (ret < 0) return;

    while (ret >= 0) {
        ret = avcodec_receive_frame(m_codecContext, m_frame);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        } else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            return;
        }

        const int targetW = m_cameraSettings.width;
        const int targetH = m_cameraSettings.height;
        const AVPixelFormat targetFmt = AV_PIX_FMT_NV12;

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
                    return;
                }

                m_frameNv12 = av_frame_alloc();
                if (!m_frameNv12) {
                    Debug::LogError("Failed to allocate NV12 frame");
                    av_frame_unref(m_frame);
                    return;
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
                    return;
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
            return;
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
            return;
        }
        av_frame_unref(m_frame);

        m_camera.PushFrame(buffer.data());
    }

    av_packet_unref(m_packet);
}

asio::awaitable<void> NetworkCameraModule::ReceiveFrames() {
    std::vector<uint8_t> frameBuffer;
    frameBuffer.reserve(1024 * 1024 * 2); // 2 MiB

    Debug::Log("NetworkCameraModule: ReceiveFrames started");
    Debug::Log("State {}", magic_enum::enum_name(GetModuleState()));

    asio::steady_timer timer(m_context);

    while (GetModuleState() == ModuleState::Enabling) {
        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait(asio::use_awaitable);
    }

    while (GetModuleState() == ModuleState::Enabled) {
        co_await m_videoStream->AsyncReceive(frameBuffer);
        ProcessEncodedFrame(frameBuffer);
    }
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
            co_return;
        }

        response.value()->GetValue(m_remoteKey);
    }

    m_videoStream = std::make_unique<SRTP::Stream>(m_context, m_localKey, m_remoteKey, m_cameraSettings.framerate);
    const UDPEndpoint endpoint = m_videoStream->Bind();
    Debug::Log("SRTP local bind port: {}", endpoint.port());
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_PORT_INFO, endpoint.port());

    {
        CameraFormat cameraFormat(m_cameraSettings.width, m_cameraSettings.height, m_cameraSettings.framerate);
        std::string cameraID = m_cameraSettings.id;

        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM, std::move(cameraID), std::move(cameraFormat));
        if (!response.has_value()) {
            Debug::LogError("No response");
            co_return;
        }

        const StreamStartFailReason reason = response.value()->GetValue<StreamStartFailReason>();
        if (reason != StreamStartFailReason::None) {
            Debug::LogError("Failed to start stream: {}", magic_enum::enum_name(reason));
            co_return;
        }
    }

    co_await StartStream();
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    Debug::Log("NetworkCameraModule: OnDisable");
    m_videoStream.reset();

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

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    Debug::Log("NetworkCameraModule: OnShutdown");
    co_return;
}
