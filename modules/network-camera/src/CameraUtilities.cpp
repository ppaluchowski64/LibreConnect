#include <CameraUtilities.h>

std::vector<CameraSpecification> FetchCamerasSpecification() {
    if (!QGuiApplication::instance()) {
        return {};
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

    return camerasSpecifications;
}
