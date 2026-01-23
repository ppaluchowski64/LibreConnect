#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <magic_enum/magic_enum.hpp>
#include <QVideoSink>

#if defined(DESKTOP_DEVICE)
std::vector<CameraSpecification> NetworkCameraModule::GetCamerasSpecification() const {
    return m_camerasSpecification;
}

void NetworkCameraModule::StartStream(const size_t requestID, const std::string cameraID, const CameraFormat requestedFormat) {
    if (!QGuiApplication::instance()) {
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
        return;
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

            for (const auto& fm : supportedFormats) {
                if (fm.resolution().width() != requestedFormat.width || fm.resolution().height() != requestedFormat.height) {
                    continue;
                }

                constexpr float diff = 0.01f;
                if (std::abs(fm.minFrameRate() - requestedFormat.minFrameRate) > diff ||
                    std::abs(fm.maxFrameRate() - requestedFormat.maxFrameRate) > diff) {
                    continue;
                }

                if (fm.pixelFormat() != static_cast<QVideoFrameFormat::PixelFormat>(requestedFormat.pixelFormat)) {
                    continue;
                }

                m_camera->setCameraFormat(fm);
                formatFound = true;
                break;
            }

            if (!formatFound) {
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                return;
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

            m_codecContext = avcodec_alloc_context3(m_codec);
            m_codecContext->bit_rate = 400000;
            m_codecContext->width = requestedFormat.width;
            m_codecContext->height = requestedFormat.height;
            m_codecContext->time_base = {1, static_cast<int>(requestedFormat.maxFrameRate)};
            m_codecContext->framerate = {static_cast<int>(requestedFormat.maxFrameRate), 1};
            m_codecContext->gop_size = 10;
            m_codecContext->max_b_frames = 1;
            m_codecContext->pix_fmt = requestedFormat.GetFormat();

            QGuiApplication::instance()->connect(m_videoSink.get(), &QVideoSink::videoFrameChanged, [&](const QVideoFrame &frame) {
                if (!frame.isValid())
                    return;

                asio::co_spawn(m_context, SendFrame(frame), asio::detached);
            });
        },
        Qt::QueuedConnection
    );
}

asio::awaitable<void> NetworkCameraModule::SendFrame(QVideoFrame frame) {
    if (!frame.map(QVideoFrame::ReadOnly))
        co_return;

    AVFrame* avFrame = av_frame_alloc();
    avFrame->format = m_codecContext->pix_fmt;
    avFrame->width  = m_codecContext->width;
    avFrame->height = m_codecContext->height;

    if (av_frame_get_buffer(avFrame, 32) < 0) {
        Debug::LogError("Could not allocate frame data");
        co_return;
    }


    AVPacket *pkt = av_packet_alloc();

    int ret = avcodec_send_frame(m_codecContext, avFrame);
    if (ret < 0) {
        Debug::LogError("Error sending frame to encoder");
        co_return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codecContext, pkt);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            Debug::LogError("Error during encoding");
            co_return;
        }



        av_packet_unref(pkt);
    }
}

asio::awaitable<void> NetworkCameraModule::UpdateCamerasSpecificationList() {
    constexpr size_t UPDATE_DELAY = 5;

    while (GetModuleState() != ModuleState::Disabled && GetModuleState() != ModuleState::Uninitialized) {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
        if (!response.has_value()) {
            Debug::LogWarning("NetworkCameraModule::UpdateCamerasSpecificationList: No response");
            continue;
        }

        response.value()->GetValue(m_camerasSpecification);

        asio::steady_timer timer(m_context);
        timer.expires_after(std::chrono::seconds(UPDATE_DELAY));
        co_await timer.async_wait(asio::use_awaitable);
    }
}

#endif

//#define MOBILE_DEVICE


void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

#if defined(MOBILE_DEVICE)
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY, [instance, this](PC_Package&& package) mutable {
        if (GetModuleState() != ModuleState::Disabled) {
            return;
        }

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
        const size_t requestID = package->GetValue<size_t>();
        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST_RESPONSE,
            FetchCamerasSpecification()
        );
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM, [instance, this](PC_Package&& package) mutable {
        const size_t requestID = package->GetValue<size_t>();
        const std::string deviceID = package->GetValue<std::string>();
        const CameraFormat format = package->GetValue<CameraFormat>();

        asio::co_spawn(m_context, StartStream(requestID,deviceID, format), asio::detached);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM, [instance, this](PC_Package&& package) mutable {
        Disable();
    });

#endif
}

void NetworkCameraModule::DisableResponseCallbacks() {
#if defined(MOBILE_DEVICE)
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);
#endif
}

void NetworkCameraModule::OnInitialize() {
    AddThreads(1);

#if defined(DESKTOP_DEVICE)
    asio::co_spawn(m_context, UpdateCamerasSpecificationList(), asio::detached);
#endif
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

#if defined(DESKTOP_DEVICE)
    m_localKey = SRTP::Stream::GenerateKey();

    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY, std::vector(m_localKey));
        if (!response.has_value()) {
            throw std::runtime_error("No response");
        }

        response.value()->GetValue(m_remoteKey);
    }
#endif

    m_videoStream = std::make_unique<SRTP::Stream>(m_context, m_localKey, m_remoteKey);

#if defined(DESKTOP_DEVICE)
    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM);
        if (!response.has_value()) {
            throw std::runtime_error("No response");
        }

        const StreamStartFailReason reason = response.value()->GetValue<StreamStartFailReason>();
        if (reason != StreamStartFailReason::None) {
            throw std::runtime_error(fmt::format("Failed to start stream: {}", magic_enum::enum_name(reason)));
        }
    }
#endif


}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_videoStream.reset();

#if defined(DESKTOP_DEVICE)
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);
#endif

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

    co_return;
}
