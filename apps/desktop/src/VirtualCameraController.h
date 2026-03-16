#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>

#include <CameraSpecification.h>

class VirtualCameraController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QStringList cameraDescriptions READ cameraDescriptions NOTIFY cameraDescriptionsChanged)
    Q_PROPERTY(QStringList formatDescriptions READ formatDescriptions NOTIFY formatDescriptionsChanged)
    Q_PROPERTY(int selectedCameraIndex READ selectedCameraIndex WRITE setSelectedCameraIndex NOTIFY selectedCameraIndexChanged)
    Q_PROPERTY(int selectedFormatIndex READ selectedFormatIndex WRITE setSelectedFormatIndex NOTIFY selectedFormatIndexChanged)

public:
    explicit VirtualCameraController(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    QStringList cameraDescriptions() const { return m_cameraDescriptions; }
    QStringList formatDescriptions() const { return m_formatDescriptions; }
    int selectedCameraIndex() const { return m_selectedCameraIndex; }
    int selectedFormatIndex() const { return m_selectedFormatIndex; }

    void setSelectedCameraIndex(int selectedCameraIndex);
    void setSelectedFormatIndex(int selectedFormatIndex);

    Q_INVOKABLE void setVirtualCameraEnabled(bool enabled);
    Q_INVOKABLE void refreshAvailableCameras();

signals:
    void enabledChanged();
    void busyChanged();
    void statusMessageChanged();
    void cameraDescriptionsChanged();
    void formatDescriptionsChanged();
    void selectedCameraIndexChanged();
    void selectedFormatIndexChanged();

private:
    void refreshState();
    void rebuildCameraDescriptions();
    void rebuildFormatDescriptions();
    void setEnabledState(bool enabled);
    void setBusy(bool busy);
    void setStatusMessage(const QString& statusMessage);

    QTimer m_pollTimer;
    std::vector<CameraSpecification> m_cameraSpecifications;
    QStringList m_cameraDescriptions;
    QStringList m_formatDescriptions;
    bool m_enabled = false;
    bool m_busy = false;
    QString m_statusMessage = QStringLiteral("Waiting for available cameras from the connected device.");
    int m_selectedCameraIndex = -1;
    int m_selectedFormatIndex = -1;
};
