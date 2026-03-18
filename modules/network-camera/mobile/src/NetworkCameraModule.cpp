#include <NetworkCameraModule.h>
#include <CameraUtilities.h>

#include <magic_enum/magic_enum.hpp>

#include <asio.hpp>
#include <asio/co_spawn.hpp>

#include <QVideoSink>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>

asio::awaitable<void> NetworkCameraModule::StartStream(const size_t requestID, const std::string cameraID, const CameraFormat requestedFormat) {
    if (!QGuiApplication::instance()) {
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
        co_return;
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
            m_codecContext->bit_rate = 400000;
            m_codecContext->width = format.resolution().width();
            m_codecContext->height = format.resolution().height();
            m_codecContext->time_base = {1, static_cast<int>(format.maxFrameRate())};
            m_codecContext->framerate = {static_cast<int>(format.maxFrameRate()), 1};
            m_codecContext->gop_size = 10;
            m_codecContext->max_b_frames = 1;
            m_codecContext->pix_fmt = AV_PIX_FMT_NV12;

            m_videoStream = std::make_shared<SRTP::Stream>(m_context, m_localKey, m_remoteKey, requestedFormat.framerate);

            if (m_swsContext) {
                sws_freeContext(m_swsContext);
            }

            if (format.pixelFormat() != QVideoFrameFormat::Format_NV12) {
                m_swsContext = sws_getContext(
                    m_codecContext->width,
                    m_codecContext->height,
                    GetFormat(format.pixelFormat()),
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

                asio::co_spawn(m_context, SendFrame(frame), asio::detached);
            });
        },
        Qt::QueuedConnection
    );
}

asio::awaitable<void> NetworkCameraModule::SendFrame(QVideoFrame frame) {
    if (!frame.map(QVideoFrame::ReadOnly))
        co_return;

    const AVPixelFormat inputFmt = GetFormat(frame.pixelFormat());

    if (inputFmt == AV_PIX_FMT_NONE) {
        Debug::LogError("Unsupported input pixel format");
        frame.unmap();
        co_return;
    }

    const std::shared_ptr<SRTP::Stream> stream = m_videoStream;

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

    if (frame.pixelFormat() != QVideoFrameFormat::Format_NV12) {
        const uint8_t* srcSlice[4] = {};
        int srcStride[4] = {};

        for (int i = 0; i < frame.planeCount(); ++i) {
            srcSlice[i]  = frame.bits(i);
            srcStride[i] = frame.bytesPerLine(i);
        }

        sws_scale(m_swsContext, srcSlice, srcStride, 0, frame.height(), avFrame->data, avFrame->linesize);
    } else {
        std::memcpy(avFrame->data[0], frame.bits(0), frame.bytesPerLine(0) * frame.height());
        std::memcpy(avFrame->data[1], frame.bits(1), frame.bytesPerLine(1) * frame.height() / 2);
    }

    frame.unmap();

    AVPacket *pkt = av_packet_alloc();
    int ret = avcodec_send_frame(m_codecContext, avFrame);

    av_frame_free(&avFrame);

    if (ret < 0) {
        Debug::LogError("Error sending frame to encoder");
        av_packet_free(&pkt);
        co_return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codecContext, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            Debug::LogError("Error during encoding");
            break;
        }

        co_await stream->AsyncSend(pkt->data, pkt->size);

        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

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
}

void NetworkCameraModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);
}

void NetworkCameraModule::OnInitialize() {}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_videoStream.reset();

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

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
