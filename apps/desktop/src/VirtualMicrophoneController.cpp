#include "VirtualMicrophoneController.h"

#include <QDesktopServices>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>

#include <ModulesManager.h>
#include <NetworkMicrophoneEvents.h>
#include <NetworkMicrophoneModule.h>

VirtualMicrophoneController::VirtualMicrophoneController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &VirtualMicrophoneController::refreshAudioDevices);
    m_pollTimer.start();

    refreshAudioDevices();
}

bool VirtualMicrophoneController::createDeviceSupported() const
{
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool VirtualMicrophoneController::driverDownloadRecommended() const
{
#ifdef _WIN32
    return m_audioDevices.isEmpty();
#else
    return false;
#endif
}

void VirtualMicrophoneController::setSelectedDeviceIndex(const int selectedDeviceIndex)
{
    if (m_selectedDeviceIndex == selectedDeviceIndex) {
        return;
    }

    m_selectedDeviceIndex = selectedDeviceIndex;
    emit selectedDeviceIndexChanged();
}

void VirtualMicrophoneController::setNewDeviceName(const QString& newDeviceName)
{
    if (m_newDeviceName == newDeviceName) {
        return;
    }

    m_newDeviceName = newDeviceName;
    emit newDeviceNameChanged();
}

void VirtualMicrophoneController::refreshAudioDevices()
{
    auto& module = ModulesManager::GetModuleReference<NetworkMicrophoneModule>();
    module->GetAudioDeviceList();
    refreshState();
}

void VirtualMicrophoneController::createAudioDevice()
{
    if (!createDeviceSupported()) {
        setStatusMessage(QStringLiteral("Creating virtual microphone devices is only supported on Linux."));
        return;
    }

    const QString trimmedName = m_newDeviceName.trimmed();
    if (trimmedName.isEmpty()) {
        setStatusMessage(QStringLiteral("Enter a device name before creating a virtual microphone."));
        return;
    }

    setStatusMessage(QStringLiteral("Creating virtual microphone device..."));
    auto& module = ModulesManager::GetModuleReference<NetworkMicrophoneModule>();
    module->CreateAudioDevice(trimmedName.toStdString());
}

void VirtualMicrophoneController::setVirtualMicrophoneEnabled(const bool enabled)
{
    auto& module = ModulesManager::GetModuleReference<NetworkMicrophoneModule>();

    if (!enabled) {
        setEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping virtual microphone..."));
        module->Disable(true);
        return;
    }

    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty()) {
        setStatusMessage(QStringLiteral("Select a virtual audio device before enabling the microphone stream."));
        return;
    }

    module->SelectDevice(deviceId.toStdString());
    setEnabledState(true);
    setBusy(true);
    setStatusMessage(QStringLiteral("Starting virtual microphone..."));
    module->Enable(true);
}

void VirtualMicrophoneController::openVirtualAudioCableDownload()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://vb-audio.com/Cable/")));
}

bool VirtualMicrophoneController::event(QEvent* event)
{
    if (event->type() == AudioDeviceListEvent::Type) {
        const auto* deviceListEvent = static_cast<AudioDeviceListEvent*>(event);
        QVariantList devices;
        const std::vector<MicrophoneDevice> audioDevices = deviceListEvent->GetAudioDeviceList();
        devices.reserve(static_cast<qsizetype>(audioDevices.size()));

        for (const MicrophoneDevice& device : audioDevices) {
            QVariantMap map;
            map.insert(QStringLiteral("id"), QString::fromStdString(device.id));
            map.insert(QStringLiteral("name"), QString::fromStdString(device.name));
            devices.append(map);
        }

        if (m_audioDevices != devices) {
            m_audioDevices = devices;
            emit deviceListChanged();
        }

        if (m_audioDevices.isEmpty()) {
            if (m_selectedDeviceIndex != -1) {
                m_selectedDeviceIndex = -1;
                emit selectedDeviceIndexChanged();
            }
        } else if (m_selectedDeviceIndex < 0 || m_selectedDeviceIndex >= m_audioDevices.size()) {
            m_selectedDeviceIndex = 0;
            emit selectedDeviceIndexChanged();
        }

        refreshState();
        return true;
    }

    if (event->type() == VirtualMicrophoneErrorEvent::Type) {
        const auto* errorEvent = static_cast<VirtualMicrophoneErrorEvent*>(event);
        setBusy(false);
        setStatusMessage(QStringLiteral("Virtual microphone error: %1").arg(static_cast<int>(errorEvent->GetError())));
        return true;
    }

    return QObject::event(event);
}

void VirtualMicrophoneController::refreshState()
{
    const auto& module = ModulesManager::GetModuleReference<NetworkMicrophoneModule>();
    const ModuleState state = module->GetModuleState();

    if (state == ModuleState::Enabled) {
        setEnabledState(true);
        setBusy(false);
        setStatusMessage(QStringLiteral("Virtual microphone is enabled."));
        return;
    }

    if (state == ModuleState::Enabling) {
        setEnabledState(true);
        setBusy(true);
        setStatusMessage(QStringLiteral("Starting virtual microphone..."));
        return;
    }

    if (state == ModuleState::Disabling) {
        setEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping virtual microphone..."));
        return;
    }

    if (state == ModuleState::Disabled) {
        setEnabledState(false);
        setBusy(false);
        setStatusMessage(
            m_audioDevices.isEmpty()
                ? QStringLiteral("No virtual microphone devices have been detected.")
                : QStringLiteral("Select a virtual audio device, then enable the microphone stream.")
        );
    }
}

void VirtualMicrophoneController::setEnabledState(const bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    emit enabledChanged();
}

void VirtualMicrophoneController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void VirtualMicrophoneController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

QString VirtualMicrophoneController::selectedDeviceId() const
{
    if (m_selectedDeviceIndex < 0 || m_selectedDeviceIndex >= m_audioDevices.size()) {
        return QString();
    }

    return m_audioDevices.at(m_selectedDeviceIndex).toMap().value(QStringLiteral("id")).toString();
}
