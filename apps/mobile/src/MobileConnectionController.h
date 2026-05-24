#pragma once

#include <QObject>
#include <QPointer>
#include <QEvent>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QSettings>
#include <QTimer>
#include <memory>

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
    Q_PROPERTY(bool approvalVisible READ approvalVisible NOTIFY approvalVisibleChanged)
    Q_PROPERTY(QString pendingDeviceName READ pendingDeviceName NOTIFY pendingDeviceNameChanged)
    Q_PROPERTY(QString localDeviceName READ localDeviceName NOTIFY localIdentityChanged)
    Q_PROPERTY(QString localIpAddress READ localIpAddress NOTIFY localIdentityChanged)
    Q_PROPERTY(bool hasPairedDevices READ hasPairedDevices NOTIFY pairedDevicesChanged)
    Q_PROPERTY(QVariantList pairedDevices READ pairedDevices NOTIFY pairedDevicesChanged)
    Q_PROPERTY(bool permissionsOnboardingRequired READ permissionsOnboardingRequired NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool permissionsBusy READ permissionsBusy NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool cameraPermissionGranted READ cameraPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool microphonePermissionGranted READ microphonePermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool notificationSendPermissionGranted READ notificationSendPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool notificationListenerPermissionGranted READ notificationListenerPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool filePermissionGranted READ filePermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool allFilesPermissionGranted READ allFilesPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool batteryPermissionGranted READ batteryPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool smsReceivePermissionGranted READ smsReceivePermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool smsReadPermissionGranted READ smsReadPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool smsSendPermissionGranted READ smsSendPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool contactsPermissionGranted READ contactsPermissionGranted NOTIFY permissionsStateChanged)
    Q_PROPERTY(bool findMyPhoneAlertActive READ findMyPhoneAlertActive NOTIFY findMyPhoneAlertActiveChanged)
    Q_PROPERTY(QVariantList findMyPhoneRingtoneOptions READ findMyPhoneRingtoneOptions NOTIFY findMyPhoneRingtoneOptionsChanged)
    Q_PROPERTY(QString findMyPhoneRingtoneUri READ findMyPhoneRingtoneUri NOTIFY findMyPhoneRingtoneChanged)
    Q_PROPERTY(QString findMyPhoneRingtoneLabel READ findMyPhoneRingtoneLabel NOTIFY findMyPhoneRingtoneChanged)
    Q_PROPERTY(QString defaultDownloadPath READ defaultDownloadPath NOTIFY defaultDownloadPathChanged)
    Q_PROPERTY(QString defaultDownloadPathStatus READ defaultDownloadPathStatus NOTIFY defaultDownloadPathStatusChanged)
    Q_PROPERTY(int batteryPercentage READ batteryPercentage NOTIFY batteryPercentageChanged)
    Q_PROPERTY(bool androidActivityDestroying READ androidActivityDestroying)

public:
    explicit MobileConnectionController(QObject* parent = nullptr);
    ~MobileConnectionController() override;

    static void handleBackendConnectionPending(const QString& deviceId, const QString& deviceName, int connectionMode, const QString& pairingCode);
    static void handleBackendConnectionApprovalRequested(const QString& deviceId, const QString& deviceName);

    bool connected() const { return m_connected; }
    QString lastError() const { return m_lastError; }
    QString challengeCode() const { return m_challengeCode; }
    bool challengeVisible() const { return m_challengeVisible; }
    bool approvalVisible() const { return m_approvalVisible; }
    QString pendingDeviceName() const { return m_pendingDeviceName; }
    QString localDeviceName() const { return m_localDeviceName; }
    QString localIpAddress() const { return m_localIpAddress; }
    bool hasPairedDevices() const { return m_hasPairedDevices; }
    QVariantList pairedDevices() const { return m_pairedDevices; }
    bool permissionsOnboardingRequired() const { return m_connected && !m_permissionsOnboardingCompleted; }
    bool permissionsBusy() const { return m_permissionsBusy; }
    bool cameraPermissionGranted() const { return m_cameraPermissionGranted; }
    bool microphonePermissionGranted() const { return m_microphonePermissionGranted; }
    bool notificationSendPermissionGranted() const { return m_notificationSendPermissionGranted; }
    bool notificationListenerPermissionGranted() const { return m_notificationListenerPermissionGranted; }
    bool filePermissionGranted() const { return m_filePermissionGranted; }
    bool allFilesPermissionGranted() const { return m_allFilesPermissionGranted; }
    bool batteryPermissionGranted() const { return m_batteryPermissionGranted; }
    bool smsReceivePermissionGranted() const { return m_smsReceivePermissionGranted; }
    bool smsReadPermissionGranted() const { return m_smsReadPermissionGranted; }
    bool smsSendPermissionGranted() const { return m_smsSendPermissionGranted; }
    bool contactsPermissionGranted() const { return m_contactsPermissionGranted; }
    bool findMyPhoneAlertActive() const { return m_findMyPhoneAlertActive; }
    QVariantList findMyPhoneRingtoneOptions() const { return m_findMyPhoneRingtoneOptions; }
    QString findMyPhoneRingtoneUri() const { return m_findMyPhoneRingtoneUri; }
    QString findMyPhoneRingtoneLabel() const;
    QString defaultDownloadPath() const { return m_defaultDownloadPath; }
    QString defaultDownloadPathStatus() const { return m_defaultDownloadPathStatus; }
    int batteryPercentage() const { return m_batteryPercentage; }
    bool androidActivityDestroying() const;

    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void refreshPairedDevices();
    Q_INVOKABLE bool removePairedDevice(const QString& deviceId);
    Q_INVOKABLE bool unpairCurrentDevice();
    Q_INVOKABLE void acceptConnectionApproval();
    Q_INVOKABLE void denyConnectionApproval();
    Q_INVOKABLE void refreshLocalIdentity();
    Q_INVOKABLE void refreshPermissionStatuses();
    Q_INVOKABLE void requestCameraPermission();
    Q_INVOKABLE void requestMicrophonePermission();
    Q_INVOKABLE void requestNotificationPermissions();
    Q_INVOKABLE void requestNotificationSendPermission();
    Q_INVOKABLE void requestNotificationListenerPermission();
    Q_INVOKABLE void requestFilePermission();
    Q_INVOKABLE void requestAllFilesPermission();
    Q_INVOKABLE void requestBatteryPermission();
    Q_INVOKABLE void requestSmsPermissions();
    Q_INVOKABLE void requestSmsReceivePermission();
    Q_INVOKABLE void requestSmsReadPermission();
    Q_INVOKABLE void requestSmsSendPermission();
    Q_INVOKABLE void requestContactsPermission();
    Q_INVOKABLE void requestAllPermissions();
    Q_INVOKABLE void completePermissionsOnboarding();
    Q_INVOKABLE void stopFindMyPhoneAlert();
    Q_INVOKABLE void refreshFindMyPhoneRingtones(bool force = false);
    Q_INVOKABLE void setFindMyPhoneRingtoneUri(const QString& uri);
    Q_INVOKABLE void setFindMyPhoneRingtoneFile(const QUrl& fileUrl);
    Q_INVOKABLE void refreshDefaultDownloadPath();
    Q_INVOKABLE void setDefaultDownloadPath(const QString& path);
    Q_INVOKABLE void exportLogs();
    Q_INVOKABLE void minimizeApp();

signals:
    void connectedChanged();
    void lastErrorChanged();
    void challengeCodeChanged();
    void challengeVisibleChanged();
    void approvalVisibleChanged();
    void pendingDeviceNameChanged();
    void pairedDevicesChanged();
    void localIdentityChanged();
    void permissionsStateChanged();
    void findMyPhoneAlertActiveChanged();
    void findMyPhoneRingtoneOptionsChanged();
    void findMyPhoneRingtoneChanged();
    void defaultDownloadPathChanged();
    void defaultDownloadPathStatusChanged();
    void batteryPercentageChanged();

    void incomingConnection(QString deviceName);

protected:
    bool event(QEvent* e) override;

private:
    enum class PermissionRequest {
        Camera,
        Microphone,
        Notifications,
        NotificationSend,
        NotificationListener,
        FileAccess,
        AllFilesAccess,
        Battery,
        Sms,
        SmsReceive,
        SmsRead,
        SmsSend,
        Contacts,
        All
    };

    void setError(const QString& e);
    void clearError();
    void clearChallenge();
    void clearApproval();
    void handleModuleErrorEvent(ModuleErrorEvent* ev);
    void updatePermissionsFromSystem();
    void runPermissionRequest(PermissionRequest request);
    void setPermissionsBusy(bool busy);
    void sendPermissionStatusToPeer(PermissionType type, bool granted);
    void sendPermissionSnapshotToPeer();
    void setPermissionSnapshot(
        bool cameraGranted,
        bool microphoneGranted,
        bool notificationSendGranted,
        bool notificationListenerGranted,
        bool fileGranted,
        bool allFilesGranted,
        bool batteryGranted,
        bool smsReceiveGranted,
        bool smsReadGranted,
        bool smsSendGranted,
        bool contactsGranted
    );
    bool notificationsPermissionGranted() const;
    bool smsPermissionsGranted() const;
    QString activePeerDeviceId() const;
    void setFindMyPhoneAlertActive(bool active);
    void stopFindMyPhoneAlertInternal(bool notifyPeer);
    QVariantList queryFindMyPhoneRingtoneOptions() const;
    void ensureSelectedRingtoneOption();
    void setFindMyPhoneRingtoneUriInternal(const QString& uri, bool persist);
    QString resolveFindMyPhoneRingtoneLabel(const QString& uri) const;
    void setDefaultDownloadPathInternal(const QString& path);
    void setDefaultDownloadPathStatus(const QString& status);
    void setBatteryPercentage(int percentage);
    void refreshBackendStateSnapshot();
    void applyBackendConnectionPending(const QString& deviceId, const QString& deviceName, int connectionMode, const QString& pairingCode);
    void applyBackendConnectionApprovalRequested(const QString& deviceId, const QString& deviceName);

private:
    bool m_connected = false;
    QString m_lastError;
    QString m_challengeCode;
    bool m_challengeVisible = false;
    bool m_approvalVisible = false;
    QString m_pendingDeviceName;
    QString m_localDeviceName;
    QString m_localIpAddress;
    QString m_connectedPeerDeviceId;
    bool m_hasPairedDevices = false;
    QVariantList m_pairedDevices;
    bool m_permissionsOnboardingCompleted = false;
    bool m_permissionsBusy = false;
    bool m_cameraPermissionGranted = false;
    bool m_microphonePermissionGranted = false;
    bool m_notificationSendPermissionGranted = false;
    bool m_notificationListenerPermissionGranted = false;
    bool m_filePermissionGranted = false;
    bool m_allFilesPermissionGranted = false;
    bool m_batteryPermissionGranted = false;
    bool m_smsReceivePermissionGranted = false;
    bool m_smsReadPermissionGranted = false;
    bool m_smsSendPermissionGranted = false;
    bool m_contactsPermissionGranted = false;
    bool m_findMyPhoneAlertActive = false;
    QVariantList m_findMyPhoneRingtoneOptions;
    QString m_findMyPhoneRingtoneUri;
    bool m_findMyPhoneRingtonesLoaded = false;
    QString m_defaultDownloadPath;
    QString m_defaultDownloadPathStatus;
    int m_batteryPercentage = -1;
    bool m_backendApprovalPending = false;
    QTimer m_backendStatePollTimer;
    QSettings m_settings;
    std::unique_ptr<ConnectionApprovalRequestedEvent> m_approvalEvent;
};
