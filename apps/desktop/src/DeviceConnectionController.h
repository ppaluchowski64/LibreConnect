#pragma once

#include <QObject>
#include <QPointer>
#include <QEvent>
#include <QString>

#include <ConnectionManager.h>
#include <Events.h>
#include <InitialConnection.h>

class DeviceConnectionController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool pending   READ pending   NOTIFY pendingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit DeviceConnectionController(QObject* parent = nullptr);

    bool connected()  const { return m_connected; }
    bool pending()    const { return m_pending; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void connectTo(const QString& ipAddress,
                               quint16 port,
                               int mode);

    Q_INVOKABLE void disconnect();

    signals:
        void connectedChanged();
    void pendingChanged();
    void lastErrorChanged();

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

    bool m_connected = false;
    bool m_pending   = false;
    QString m_lastError;
};
