#pragma once

#include <QObject>
#include <QEvent>
#include <QSettings>
#include <QTimer>

class MobileNotificationSyncController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool desktopPermissionGranted READ desktopPermissionGranted NOTIFY permissionStateChanged)

public:
    explicit MobileNotificationSyncController(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    bool desktopPermissionGranted() const { return m_desktopPermissionGranted; }

    Q_INVOKABLE void setNotificationSyncEnabled(bool enabled);

signals:
    void enabledChanged();
    void busyChanged();
    void statusMessageChanged();
    void permissionStateChanged();

protected:
    bool event(QEvent* event) override;

private:
    void refreshState();
    void setEnabledState(bool enabled);
    void setBusy(bool busy);
    void setStatusMessage(const QString& message);
    void setRequestedEnabled(bool enabled, bool persist);

    QSettings m_settings;
    QTimer m_pollTimer;
    bool m_connected = false;
    bool m_permissionsGranted = false;
    bool m_desktopPermissionGranted = true;
    bool m_requestedEnabled = false;
    bool m_enableAttemptPending = false;
    bool m_disableAttemptPending = false;
    bool m_confirmedEnabled = false;
    bool m_enabled = false;
    bool m_busy = false;
    QString m_statusMessage;
};
