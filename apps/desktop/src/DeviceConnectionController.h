#pragma once

#include <QObject>
#include <QPointer>
#include <QEvent>
#include <QString>
#include <QVariantList>
#include <memory>

#include <ConnectionManager.h>
#include <Events.h>
#include <InitialConnection.h>
#include <BaseModule.h>

class DeviceConnectionController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool pending   READ pending   NOTIFY pendingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool hasPairedDevices READ hasPairedDevices NOTIFY pairedDevicesChanged)
    Q_PROPERTY(bool localNetworkPermissionGranted READ localNetworkPermissionGranted NOTIFY localNetworkPermissionGrantedChanged)
    Q_PROPERTY(bool localNetworkPermissionCheckPending READ localNetworkPermissionCheckPending NOTIFY localNetworkPermissionCheckPendingChanged)
    Q_PROPERTY(bool verificationPending READ verificationPending NOTIFY verificationPendingChanged)
    Q_PROPERTY(int verificationTriesLeft READ verificationTriesLeft NOTIFY verificationTriesLeftChanged)
    Q_PROPERTY(QString verificationError READ verificationError NOTIFY verificationErrorChanged)
    Q_PROPERTY(QString verificationStatus READ verificationStatus NOTIFY verificationStatusChanged)
    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY approvalPendingChanged)
    Q_PROPERTY(bool findMyPhoneAlertActive READ findMyPhoneAlertActive NOTIFY findMyPhoneAlertActiveChanged)
    Q_PROPERTY(int batteryPercentage READ batteryPercentage NOTIFY batteryPercentageChanged)

public:
    explicit DeviceConnectionController(QObject* parent = nullptr);
    ~DeviceConnectionController() override;

    bool connected()  const { return m_connected; }
    bool pending()    const { return m_pending; }
    QString lastError() const { return m_lastError; }
    bool hasPairedDevices() const { return m_hasPairedDevices; }
    bool localNetworkPermissionGranted() const;
    bool localNetworkPermissionCheckPending() const { return m_localNetworkPermissionCheckPending; }
    bool verificationPending() const { return m_verificationPending; }
    int verificationTriesLeft() const { return m_verificationTriesLeft; }
    QString verificationError() const { return m_verificationError; }
    QString verificationStatus() const { return m_verificationStatus; }
    bool approvalPending() const { return m_approvalPending; }
    bool findMyPhoneAlertActive() const { return m_findMyPhoneAlertActive; }
    int batteryPercentage() const { return m_batteryPercentage; }

    Q_INVOKABLE void connectTo(const QString& ipAddress,
                               quint16 port,
                               int mode);

    Q_INVOKABLE void disconnect();
    Q_INVOKABLE QVariantList getPairedDevices();
    Q_INVOKABLE bool removePairedDevice(const QString& deviceId);
    Q_INVOKABLE void submitVerificationCode(const QString& code);
    Q_INVOKABLE void cancelVerification();
    Q_INVOKABLE void refreshPairedDevices();
    Q_INVOKABLE bool startFindMyPhoneAlert();
    Q_INVOKABLE void stopFindMyPhoneAlert();
    Q_INVOKABLE void checkLocalNetworkPermission();

    signals:
        void connectedChanged();
    void pendingChanged();
    void lastErrorChanged();
    void pairedDevicesChanged();
    void localNetworkPermissionGrantedChanged();
    void localNetworkPermissionCheckPendingChanged();
    void localNetworkPermissionCheckFinished(bool granted);
    void verificationPendingChanged();
    void verificationTriesLeftChanged();
    void verificationErrorChanged();
    void verificationStatusChanged();
    void approvalPendingChanged();
    void findMyPhoneAlertActiveChanged();
    void batteryPercentageChanged();

    void incomingConnectionRequested(QString deviceName);
    void verificationFailed(int triesLeft);

protected:
    bool event(QEvent* e) override;

private:
    void handleConnectedEvent(ConnectedEvent* ev);
    void handleDisconnectedEvent(DisconnectedEvent* ev);
    void handleScannerErrorEvent(ScannerErrorEvent* ev);
    void handleConnectionPendingEvent(ConnectionPendingEvent* ev);
    void handleConnectionFailedVerificationEvent(ConnectionFailedVerificationEvent* ev);
    void handleConnectionVerificationEvent(ConnectionVerificationEvent* ev);
    void handleConnectionApprovalRequestedEvent(ConnectionApprovalRequestedEvent* ev);
    void handleConnectionApprovalDeniedEvent(ConnectionApprovalDeniedEvent* ev);
    void handleModuleErrorEvent(ModuleErrorEvent* ev);
    void setBatteryPercentage(int percentage);

    void handleError(const std::string& message);
    void handleError(const std::string& message, QEvent::Type eventType);
    void setFindMyPhoneAlertActive(bool active);
    void setApprovalPending(bool pending);

    bool m_connected = false;
    bool m_pending   = false;
    QString m_lastError;
    bool m_hasPairedDevices = false;
    bool m_localNetworkPermissionCheckPending = false;
    bool m_verificationPending = false;
    int m_verificationTriesLeft = 0;
    QString m_verificationError;
    QString m_verificationStatus;
    bool m_approvalPending = false;
    bool m_findMyPhoneAlertActive = false;
    int m_batteryPercentage = -1;
    std::unique_ptr<ConnectionVerificationEvent> m_verificationEvent;
};
