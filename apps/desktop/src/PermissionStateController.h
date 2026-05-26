#pragma once

#include <QObject>
#include <QEvent>
#include <QPointer>

#include <BaseModule.h>

class PermissionStateController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool cameraGranted READ cameraGranted NOTIFY permissionStateChanged)
    Q_PROPERTY(bool notificationsGranted READ notificationsGranted NOTIFY permissionStateChanged)
    Q_PROPERTY(bool fileSystemGranted READ fileSystemGranted NOTIFY permissionStateChanged)
    Q_PROPERTY(bool batteryGranted READ batteryGranted NOTIFY permissionStateChanged)
    Q_PROPERTY(bool smsGranted READ smsGranted NOTIFY permissionStateChanged)
    Q_PROPERTY(bool accessibilityGranted READ accessibilityGranted NOTIFY permissionStateChanged)
    Q_PROPERTY(bool desktopNotificationsGranted READ desktopNotificationsGranted NOTIFY permissionStateChanged)
    Q_PROPERTY(bool microphoneGranted READ microphoneGranted NOTIFY permissionStateChanged)

public:
    explicit PermissionStateController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    bool cameraGranted() const { return m_cameraGranted; }
    bool notificationsGranted() const { return m_notificationsGranted; }
    bool fileSystemGranted() const { return m_fileSystemGranted; }
    bool batteryGranted() const { return m_batteryGranted; }
    bool smsGranted() const { return m_smsGranted; }
    bool accessibilityGranted() const { return m_accessibilityGranted; }
    bool desktopNotificationsGranted() const { return m_desktopNotificationsGranted; }
    bool microphoneGranted() const { return m_microphoneGranted; }

    Q_INVOKABLE bool isGranted(int permissionType) const;
    Q_INVOKABLE void requestPermission(int permissionType);
    Q_INVOKABLE void requestDesktopNotificationPermission();

signals:
    void connectedChanged();
    void permissionStateChanged();

protected:
    bool event(QEvent* event) override;

private:
    void clearPermissionState();
    void requestPermissionSyncSnapshot();
    void setPermissionState(PermissionType permissionType, bool granted);

    bool m_connected = false;
    bool m_cameraGranted = false;
    bool m_notificationsGranted = false;
    bool m_fileSystemGranted = false;
    bool m_batteryGranted = false;
    bool m_smsGranted = false;
    bool m_accessibilityGranted = false;
    bool m_desktopNotificationsGranted = false;
    bool m_microphoneGranted = false;
    int m_syncRequestGeneration = 0;
};
