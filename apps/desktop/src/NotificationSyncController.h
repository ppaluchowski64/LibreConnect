#pragma once

#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QString>
#include <QSettings>
#include <QVariantList>
#include <QVector>

class NotificationSyncController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)

public:
    explicit NotificationSyncController(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    QVariantList notifications() const;

    Q_INVOKABLE void setNotificationSyncEnabled(bool enabled);
    Q_INVOKABLE bool dismissNotification(const QString& key);

signals:
    void enabledChanged();
    void busyChanged();
    void statusMessageChanged();
    void notificationsChanged();

private:
    struct NotificationItem {
        QString key;
        QString appName;
        QString title;
        QString content;
        qint64 timestamp = 0;
        bool dismissable = true;
        QString iconPath;
    };

    bool event(QEvent* event) override;
    void refreshState();
    void upsertNotification(const NotificationItem& item);
    void removeNotificationByKey(const QString& key);
    void clearNotifications();
    void setBusy(bool busy);
    void setEnabledState(bool enabled);
    void setStatusMessage(const QString& statusMessage);
    void setRequestedEnabled(bool enabled, bool persist);

    QSettings m_settings;
    QTimer m_pollTimer;
    bool m_connected = false;
    bool m_enabled = false;
    bool m_busy = false;
    bool m_requestedEnabled = false;
    bool m_enableAttemptPending = false;
    bool m_disableAttemptPending = false;
    QString m_statusMessage;
    QVector<NotificationItem> m_notifications;
};
