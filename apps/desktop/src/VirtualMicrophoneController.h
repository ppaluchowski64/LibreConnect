#pragma once

#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QString>
#include <QVariantList>

class VirtualMicrophoneController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool createDeviceSupported READ createDeviceSupported CONSTANT)
    Q_PROPERTY(bool driverDownloadRecommended READ driverDownloadRecommended NOTIFY deviceListChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY deviceListChanged)
    Q_PROPERTY(int selectedDeviceIndex READ selectedDeviceIndex WRITE setSelectedDeviceIndex NOTIFY selectedDeviceIndexChanged)
    Q_PROPERTY(QString newDeviceName READ newDeviceName WRITE setNewDeviceName NOTIFY newDeviceNameChanged)

public:
    explicit VirtualMicrophoneController(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    bool createDeviceSupported() const;
    bool driverDownloadRecommended() const;
    QString statusMessage() const { return m_statusMessage; }
    QVariantList audioDevices() const { return m_audioDevices; }
    int selectedDeviceIndex() const { return m_selectedDeviceIndex; }
    QString newDeviceName() const { return m_newDeviceName; }

    void setSelectedDeviceIndex(int selectedDeviceIndex);
    void setNewDeviceName(const QString& newDeviceName);

    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE void createAudioDevice();
    Q_INVOKABLE void setVirtualMicrophoneEnabled(bool enabled);
    Q_INVOKABLE void openVirtualAudioCableDownload();

signals:
    void enabledChanged();
    void busyChanged();
    void statusMessageChanged();
    void deviceListChanged();
    void selectedDeviceIndexChanged();
    void newDeviceNameChanged();

protected:
    bool event(QEvent* event) override;

private:
    void refreshState();
    void setEnabledState(bool enabled);
    void setBusy(bool busy);
    void setStatusMessage(const QString& statusMessage);
    QString selectedDeviceId() const;

    QTimer m_pollTimer;
    QVariantList m_audioDevices;
    bool m_enabled = false;
    bool m_busy = false;
    QString m_statusMessage = QStringLiteral("Waiting for available virtual microphone devices.");
    int m_selectedDeviceIndex = -1;
    QString m_newDeviceName = QStringLiteral("LibreConnect Virtual Microphone");
};
