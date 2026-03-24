#include "VirtualCameraController.h"

#include <ModulesManager.h>

VirtualCameraController::VirtualCameraController(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &VirtualCameraController::refreshAvailableCameras);
    m_pollTimer.start();

    refreshAvailableCameras();
}

void VirtualCameraController::setSelectedCameraIndex(const int selectedCameraIndex)
{
    if (m_selectedCameraIndex == selectedCameraIndex) {
        return;
    }

    m_selectedCameraIndex = selectedCameraIndex;
    emit selectedCameraIndexChanged();

    rebuildFormatDescriptions();
}

void VirtualCameraController::setSelectedFormatIndex(const int selectedFormatIndex)
{
    if (m_selectedFormatIndex == selectedFormatIndex) {
        return;
    }

    m_selectedFormatIndex = selectedFormatIndex;
    emit selectedFormatIndexChanged();
}

void VirtualCameraController::setVirtualCameraEnabled(const bool enabled)
{
    auto& module = ModulesManager::GetModuleReference<NetworkCameraModule>();

    if (!enabled) {
        setEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping virtual camera..."));
        module->Disable(true);
        return;
    }

    if (m_selectedCameraIndex < 0 || m_selectedCameraIndex >= static_cast<int>(m_cameraSpecifications.size())) {
        setStatusMessage(QStringLiteral("Select a camera before enabling the virtual camera."));
        return;
    }

    const CameraSpecification& camera = m_cameraSpecifications.at(m_selectedCameraIndex);
    if (m_selectedFormatIndex < 0 || m_selectedFormatIndex >= static_cast<int>(camera.formats.size())) {
        setStatusMessage(QStringLiteral("Select a format before enabling the virtual camera."));
        return;
    }

    const CameraFormat& format = camera.formats.at(m_selectedFormatIndex);
    CameraSettings settings(
        camera.description,
        true,
        format.width,
        format.height,
        format.framerate,
        VCAM_FORMAT_NV12,
        camera.id
    );

    module->SetCameraSettings(settings);
    setEnabledState(true);
    setBusy(true);
    setStatusMessage(QStringLiteral("Starting virtual camera..."));
    module->Enable(true);
}

void VirtualCameraController::refreshAvailableCameras()
{
    auto& module = ModulesManager::GetModuleReference<NetworkCameraModule>();
    m_cameraSpecifications = module->GetCamerasSpecification();

    rebuildCameraDescriptions();
    refreshState();
}

void VirtualCameraController::refreshState()
{
    const auto& module = ModulesManager::GetModuleReference<NetworkCameraModule>();
    const ModuleState state = module->GetModuleState();

    if (state == ModuleState::Enabled) {
        setEnabledState(true);
        setBusy(false);
        setStatusMessage(QStringLiteral("Virtual camera is enabled."));
        return;
    }

    if (state == ModuleState::Enabling) {
        setEnabledState(true);
        setBusy(true);
        setStatusMessage(QStringLiteral("Starting virtual camera..."));
        return;
    }

    if (state == ModuleState::Disabling) {
        setEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping virtual camera..."));
        return;
    }

    if (!m_cameraDescriptions.isEmpty()) {
        if (state == ModuleState::Disabled) {
            setEnabledState(false);
            setBusy(false);
            setStatusMessage(QStringLiteral("Select a camera and format, then enable the virtual camera."));
        }
        return;
    }

    if (state == ModuleState::Disabled) {
        setEnabledState(false);
        setBusy(false);
        setStatusMessage(QStringLiteral("No cameras have been reported by the connected device yet."));
    }
}

void VirtualCameraController::rebuildCameraDescriptions()
{
    QStringList descriptions;
    descriptions.reserve(static_cast<qsizetype>(m_cameraSpecifications.size()));

    int defaultIndex = -1;
    for (int i = 0; i < static_cast<int>(m_cameraSpecifications.size()); ++i) {
        const CameraSpecification& specification = m_cameraSpecifications.at(i);
        QString description = QString::fromStdString(specification.description);
        if (specification.isDefault) {
            description += QStringLiteral(" (Default)");
            if (defaultIndex < 0) {
                defaultIndex = i;
            }
        }

        descriptions.append(description);
    }

    if (m_cameraDescriptions != descriptions) {
        m_cameraDescriptions = descriptions;
        emit cameraDescriptionsChanged();
    }

    if (m_cameraSpecifications.empty()) {
        if (m_selectedCameraIndex != -1) {
            m_selectedCameraIndex = -1;
            emit selectedCameraIndexChanged();
        }
        rebuildFormatDescriptions();
        return;
    }

    if (m_selectedCameraIndex < 0 || m_selectedCameraIndex >= static_cast<int>(m_cameraSpecifications.size())) {
        m_selectedCameraIndex = defaultIndex >= 0 ? defaultIndex : 0;
        emit selectedCameraIndexChanged();
    }

    rebuildFormatDescriptions();
}

void VirtualCameraController::rebuildFormatDescriptions()
{
    QStringList descriptions;

    if (m_selectedCameraIndex >= 0 && m_selectedCameraIndex < static_cast<int>(m_cameraSpecifications.size())) {
        const std::vector<CameraFormat>& formats = m_cameraSpecifications.at(m_selectedCameraIndex).formats;
        descriptions.reserve(static_cast<qsizetype>(formats.size()));

        for (const CameraFormat& format : formats) {
            descriptions.append(QStringLiteral("%1 x %2 @ %3 fps")
                .arg(format.width)
                .arg(format.height)
                .arg(format.framerate));
        }
    }

    if (m_formatDescriptions != descriptions) {
        m_formatDescriptions = descriptions;
        emit formatDescriptionsChanged();
    }

    const int newIndex = descriptions.isEmpty() ? -1 : 0;
    if (m_selectedFormatIndex < 0 || m_selectedFormatIndex >= descriptions.size()) {
        if (m_selectedFormatIndex != newIndex) {
            m_selectedFormatIndex = newIndex;
            emit selectedFormatIndexChanged();
        }
    }
}

void VirtualCameraController::setEnabledState(const bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    emit enabledChanged();
}

void VirtualCameraController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void VirtualCameraController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}
