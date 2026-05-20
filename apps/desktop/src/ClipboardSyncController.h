#pragma once

#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QString>
#include <QSettings>

class ClipboardSyncController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool autoSyncEnabled READ autoSyncEnabled NOTIFY autoSyncEnabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit ClipboardSyncController(QObject* parent = nullptr);

    bool autoSyncEnabled() const { return m_autoSyncEnabled; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE void setClipboardAutoSyncEnabled(bool enabled);
    Q_INVOKABLE void syncClipboard();

signals:
    void autoSyncEnabledChanged();
    void busyChanged();
    void statusMessageChanged();

private:
    bool event(QEvent* event) override;
    void refreshState();
    void setBusy(bool busy);
    void setAutoSyncEnabledState(bool enabled);
    void setStatusMessage(const QString& statusMessage);
    void setRequestedAutoSync(bool enabled, bool persist);

    QSettings m_settings;
    QTimer m_pollTimer;
    bool m_connected = false;
    bool m_autoSyncEnabled = false;
    bool m_busy = false;
    bool m_requestedAutoSync = false;
    bool m_enableAttemptPending = false;
    bool m_disableAttemptPending = false;
    bool m_confirmedEnabled = false;
    QString m_statusMessage;
};
