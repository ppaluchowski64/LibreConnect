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

public:
    explicit PermissionStateController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    bool cameraGranted() const { return m_cameraGranted; }
    bool notificationsGranted() const { return m_notificationsGranted; }
    bool fileSystemGranted() const { return m_fileSystemGranted; }
    bool batteryGranted() const { return m_batteryGranted; }

    Q_INVOKABLE bool isGranted(int permissionType) const;
    Q_INVOKABLE void requestPermission(int permissionType);

signals:
    void connectedChanged();
    void permissionStateChanged();

protected:
    bool event(QEvent* event) override;

private:
    void clearPermissionState();
    void setPermissionState(PermissionType permissionType, bool granted);

    bool m_connected = false;
    bool m_cameraGranted = false;
    bool m_notificationsGranted = false;
    bool m_fileSystemGranted = false;
    bool m_batteryGranted = false;
};

