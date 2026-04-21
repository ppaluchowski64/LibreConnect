#pragma once

#include <QObject>
#include <QPointer>
#include <QEvent>
#include <QString>
#include <QVariantList>
#include <QSettings>

#include <ConnectionManager.h>
#include <Events.h>
#include <BaseModule.h>

class MobileConnectionController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString challengeCode READ challengeCode NOTIFY challengeCodeChanged)
    Q_PROPERTY(bool challengeVisible READ challengeVisible NOTIFY challengeVisibleChanged)
    Q_PROPERTY(QString pendingDeviceName READ pendingDeviceName NOTIFY pendingDeviceNameChanged)
    Q_PROPERTY(QString localDeviceName READ localDeviceName NOTIFY localIdentityChanged)
    Q_PROPERTY(QString localIpAddress READ localIpAddress NOTIFY localIdentityChanged)
    Q_PROPERTY(bool hasPairedDevices READ hasPairedDevices NOTIFY pairedDevicesChanged)
    Q_PROPERTY(QVariantList pairedDevices READ pairedDevices NOTIFY pairedDevicesChanged)
    Q_PROPERTY(bool permissionsOnboardingRequired READ permissionsOnboardingRequired NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool permissionsBusy READ permissionsBusy NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool cameraPermissionGranted READ cameraPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool notificationSendPermissionGranted READ notificationSendPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool notificationListenerPermissionGranted READ notificationListenerPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool filePermissionGranted READ filePermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool allFilesPermissionGranted READ allFilesPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool batteryPermissionGranted READ batteryPermissionGranted NOTIFY permissionsStateChanged)

public:
    explicit MobileConnectionController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    QString lastError() const { return m_lastError; }
    QString challengeCode() const { return m_challengeCode; }
    bool challengeVisible() const { return m_challengeVisible; }
    QString pendingDeviceName() const { return m_pendingDeviceName; }
    QString localDeviceName() const { return m_localDeviceName; }
    QString localIpAddress() const { return m_localIpAddress; }
    bool hasPairedDevices() const { return m_hasPairedDevices; }
    QVariantList pairedDevices() const { return m_pairedDevices; }
    bool permissionsOnboardingRequired() const { return m_connected && !m_permissionsOnboardingCompleted; }
    bool permissionsBusy() const { return m_permissionsBusy; }
    bool cameraPermissionGranted() const { return m_cameraPermissionGranted; }
    bool notificationSendPermissionGranted() const { return m_notificationSendPermissionGranted; }
    bool notificationListenerPermissionGranted() const { return m_notificationListenerPermissionGranted; }
    bool filePermissionGranted() const { return m_filePermissionGranted; }
    bool allFilesPermissionGranted() const { return m_allFilesPermissionGranted; }
    bool batteryPermissionGranted() const { return m_batteryPermissionGranted; }

    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void refreshPairedDevices();
    Q_INVOKABLE bool removePairedDevice(const QString& deviceId);
    Q_INVOKABLE bool unpairCurrentDevice();
    Q_INVOKABLE void refreshLocalIdentity();
    Q_INVOKABLE void refreshPermissionStatuses();
    Q_INVOKABLE void requestCameraPermission();
    Q_INVOKABLE void requestNotificationPermissions();
    Q_INVOKABLE void requestNotificationSendPermission();
    Q_INVOKABLE void requestNotificationListenerPermission();
    Q_INVOKABLE void requestFilePermission();
    Q_INVOKABLE void requestAllFilesPermission();
    Q_INVOKABLE void requestBatteryPermission();
    Q_INVOKABLE void requestAllPermissions();
    Q_INVOKABLE void completePermissionsOnboarding();

signals:
    void connectedChanged();
    void lastErrorChanged();
    void challengeCodeChanged();
    void challengeVisibleChanged();
    void pendingDeviceNameChanged();
    void pairedDevicesChanged();
    void localIdentityChanged();
    void permissionsStateChanged();

    void incomingConnection(QString deviceName);

protected:
    bool event(QEvent* e) override;

private:
    enum class PermissionRequest {
        Camera,
        Notifications,
        NotificationSend,
        NotificationListener,
        FileAccess,
        AllFilesAccess,
        Battery,
        All
    };

    void setError(const QString& e);
    void clearError();
    void clearChallenge();
    void handleModuleErrorEvent(ModuleErrorEvent* ev);
    void updatePermissionsFromSystem();
    void runPermissionRequest(PermissionRequest request);
    void setPermissionsBusy(bool busy);
    void sendPermissionStatusToPeer(PermissionType type, bool granted);
    void sendPermissionSnapshotToPeer();
    void setPermissionSnapshot(
        bool cameraGranted,
        bool notificationSendGranted,
        bool notificationListenerGranted,
        bool fileGranted,
        bool allFilesGranted,
        bool batteryGranted
    );
    bool notificationsPermissionGranted() const;
    QString activePeerDeviceId() const;

private:
    bool m_connected = false;
    QString m_lastError;
    QString m_challengeCode;
    bool m_challengeVisible = false;
    QString m_pendingDeviceName;
    QString m_localDeviceName;
    QString m_localIpAddress;
    QString m_connectedPeerDeviceId;
    bool m_hasPairedDevices = false;
    QVariantList m_pairedDevices;
    bool m_permissionsOnboardingCompleted = false;
    bool m_permissionsBusy = false;
    bool m_cameraPermissionGranted = false;
    bool m_notificationSendPermissionGranted = false;
    bool m_notificationListenerPermissionGranted = false;
    bool m_filePermissionGranted = false;
    bool m_allFilesPermissionGranted = false;
    bool m_batteryPermissionGranted = false;
    QSettings m_settings;
};
