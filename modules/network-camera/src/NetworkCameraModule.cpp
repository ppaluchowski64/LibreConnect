#include <NetworkCameraModule.h>
#include <QMediaDevices>
#include <QCameraDevice>

#if defined(DESKTOP_DEVICE)
std::vector<CameraSpecification> NetworkCameraModule::GetCamerasSpecification() const {
    return m_camerasSpecification;
}
#endif

void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

#if defined(MOBILE_DEVICE)
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY, [instance, this](PC_Package&& package) mutable {
        m_localKey = SRTP::Stream::GenerateKey();

        const size_t requestID = package->GetValue<size_t>();
        package->GetValue(m_remoteKey);

        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY_RESPONSE, std::vector(m_localKey));
        Enable();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST , [instance, this](PC_Package&& package) mutable {
        const size_t requestID = package->GetValue<size_t>();

        if (!QGuiApplication::instance()) {
            ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST_RESPONSE, std::vector<CameraSpecification>());
            return;
        }

        QList<QCameraDevice> cameras;

        QMetaObject::invokeMethod(
            QGuiApplication::instance(),
            [&cameras] {
                cameras = QMediaDevices::videoInputs();
            },
            Qt::BlockingQueuedConnection
        );

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

        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST_RESPONSE, std::move(camerasSpecifications));
    });

#endif

}

void NetworkCameraModule::DisableResponseCallbacks() {
#if defined(MOBILE_DEVICE)
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
#endif
}

void NetworkCameraModule::OnInitialize() {
    AddThreads(1);
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

    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
        if (!response.has_value()) {
            throw std::runtime_error("No response");
        }

        response.value()->GetValue(m_camerasSpecification);
    }



#endif


    m_videoStream = std::make_unique<SRTP::Stream>(m_context, m_localKey, m_remoteKey);



}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_videoStream.reset();

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

    co_return;
}
