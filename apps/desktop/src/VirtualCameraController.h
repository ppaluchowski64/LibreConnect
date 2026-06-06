#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <CameraSpecification.h>

class VirtualCameraController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString unavailableReason READ unavailableReason CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QStringList cameraDescriptions READ cameraDescriptions NOTIFY cameraDescriptionsChanged)
    Q_PROPERTY(QVariantList formatList READ formatList NOTIFY formatListChanged)
    Q_PROPERTY(int selectedCameraIndex READ selectedCameraIndex WRITE setSelectedCameraIndex NOTIFY selectedCameraIndexChanged)
    Q_PROPERTY(int selectedFormatIndex READ selectedFormatIndex WRITE setSelectedFormatIndex NOTIFY selectedFormatIndexChanged)
    Q_PROPERTY(bool cameraFlipped READ cameraFlipped NOTIFY cameraFlippedChanged)

public:
    explicit VirtualCameraController(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    bool available() const { return m_available; }
    QString unavailableReason() const { return m_unavailableReason; }
    QString statusMessage() const { return m_statusMessage; }
    QStringList cameraDescriptions() const { return m_cameraDescriptions; }
    QVariantList formatList() const { return m_formatList; }
    int selectedCameraIndex() const { return m_selectedCameraIndex; }
    int selectedFormatIndex() const { return m_selectedFormatIndex; }
    bool cameraFlipped() const;

    void setSelectedCameraIndex(int selectedCameraIndex);
    void setSelectedFormatIndex(int selectedFormatIndex);

    Q_INVOKABLE void setVirtualCameraEnabled(bool enabled);
    Q_INVOKABLE void refreshAvailableCameras();
    Q_INVOKABLE void flipCamera();

signals:
    void enabledChanged();
    void busyChanged();
    void statusMessageChanged();
    void cameraDescriptionsChanged();
    void formatListChanged();
    void selectedCameraIndexChanged();
    void selectedFormatIndexChanged();
    void cameraFlippedChanged();

private:
    void refreshState();
    void rebuildCameraDescriptions();
    void rebuildFormatList();
    void setEnabledState(bool enabled);
    void setBusy(bool busy);
    void setStatusMessage(const QString& statusMessage);

    QTimer m_pollTimer;
    std::vector<CameraSpecification> m_cameraSpecifications;
    QStringList m_cameraDescriptions;
    QVariantList m_formatList;
    bool m_available = true;
    bool m_enabled = false;
    bool m_busy = false;
    QString m_unavailableReason;
    QString m_statusMessage = QStringLiteral("Waiting for available cameras from the connected device.");
    int m_selectedCameraIndex = -1;
    int m_selectedFormatIndex = -1;
};
