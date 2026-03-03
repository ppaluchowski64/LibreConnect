#pragma once

#include <QObject>
#include <QPointer>
#include <QEvent>
#include <QString>
#include <memory>

#include <ConnectionManager.h>
#include <Events.h>
#include <InitialConnection.h>

class DeviceConnectionController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool pending   READ pending   NOTIFY pendingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool verificationPending READ verificationPending NOTIFY verificationPendingChanged)
    Q_PROPERTY(int verificationTriesLeft READ verificationTriesLeft NOTIFY verificationTriesLeftChanged)
    Q_PROPERTY(QString verificationError READ verificationError NOTIFY verificationErrorChanged)

public:
    explicit DeviceConnectionController(QObject* parent = nullptr);

    bool connected()  const { return m_connected; }
    bool pending()    const { return m_pending; }
    QString lastError() const { return m_lastError; }
    bool verificationPending() const { return m_verificationPending; }
    int verificationTriesLeft() const { return m_verificationTriesLeft; }
    QString verificationError() const { return m_verificationError; }

    Q_INVOKABLE void connectTo(const QString& ipAddress,
                               quint16 port,
                               int mode);

    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void submitVerificationCode(const QString& code);
    Q_INVOKABLE void cancelVerification();

    signals:
        void connectedChanged();
    void pendingChanged();
    void lastErrorChanged();
    void verificationPendingChanged();
    void verificationTriesLeftChanged();
    void verificationErrorChanged();

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

    void handleError(const std::string& message);
    void handleError(const std::string& message, QEvent::Type eventType);

    bool m_connected = false;
    bool m_pending   = false;
    QString m_lastError;
    bool m_verificationPending = false;
    int m_verificationTriesLeft = 0;
    QString m_verificationError;
    std::unique_ptr<ConnectionVerificationEvent> m_verificationEvent;
};
