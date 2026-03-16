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

void NetworkCameraModule::StartStream() {
    asio::post(m_context, [this]() {
        CameraSpecification cameraSpecification;
        for (const auto& spec : m_camerasSpecification) {
            if (spec.id == m_cameraSettings.id) {
                cameraSpecification = spec;
            }
        }

        const std::string& cameraName = m_cameraSettings.customCameraNameEnabled ? m_cameraSettings.cameraName : cameraSpecification.description;
        if (!m_camera.Start(cameraName, m_cameraSettings.pixelFormat, m_cameraSettings.width, m_cameraSettings.height, m_cameraSettings.framerate)) {
            Debug::LogError("NetworkCameraModule::StartStream Failed to start camera");
            return;
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

        m_packet = av_packet_alloc();
        m_frame = av_frame_alloc();

        asio::co_spawn(m_context, ReceiveFrames(), asio::detached);
    });
}

void NetworkCameraModule::ProcessEncodedFrame(const std::vector<uint8_t>& frameBuffer) const {

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

        const size_t y_size = m_frame->width * m_frame->height;
        const size_t uv_size = m_frame->width * (m_frame->height / 2);
        std::vector<uint8_t> buffer(y_size + uv_size);

        av_image_copy_to_buffer(buffer.data(), buffer.size(), m_frame->data, m_frame->linesize, static_cast<AVPixelFormat>(m_frame->format), m_frame->width, m_frame->height, 1);
        av_frame_unref(m_frame);

        m_camera.PushFrame(buffer.data());
    }

    av_packet_unref(m_packet);
}

asio::awaitable<void> NetworkCameraModule::ReceiveFrames() const {
    std::vector<uint8_t> frameBuffer;
    frameBuffer.reserve(1024 * 1024 * 2); // 2 MiB

    while (GetModuleState() == ModuleState::Enabled) {
        co_await m_videoStream->AsyncReceive(frameBuffer);
        ProcessEncodedFrame(frameBuffer);
    }
}

asio::awaitable<void> NetworkCameraModule::UpdateCamerasSpecificationList() {
    constexpr size_t UPDATE_DELAY = 5;

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

}

void NetworkCameraModule::DisableResponseCallbacks() {

}

void NetworkCameraModule::OnInitialize() {
    asio::co_spawn(m_context, UpdateCamerasSpecificationList(), asio::detached);
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
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

    StartStream();
    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_videoStream.reset();

    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    co_return;
}
