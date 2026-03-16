#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

class NotificationSyncController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit NotificationSyncController(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE void setNotificationSyncEnabled(bool enabled);

signals:
    void enabledChanged();
    void busyChanged();
    void statusMessageChanged();

private:
    void refreshState();
    void setBusy(bool busy);
    void setEnabledState(bool enabled);
    void setStatusMessage(const QString& statusMessage);

    QTimer m_pollTimer;
    bool m_enabled = false;
    bool m_busy = false;
    bool m_requestedEnabled = false;
    QString m_statusMessage;
};
