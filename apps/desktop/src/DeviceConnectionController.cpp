#include "DeviceConnectionController.h"

DeviceConnectionController::DeviceConnectionController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
}

void DeviceConnectionController::connectTo(const QString& ipAddress,
                                           quint16 port,
                                           int mode)
{
    m_pending = true;
    emit pendingChanged();

    // Clear old error
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }

    const std::string addr = ipAddress.toStdString();
    const auto connectionMode = static_cast<InitialConnectionMode>(mode);

    ConnectionManager::Connect(addr, static_cast<uint16_t>(port), connectionMode);
}

void DeviceConnectionController::disconnect()
{
    ConnectionManager::Disconnect();
}

bool DeviceConnectionController::event(QEvent* e)
{
    const QEvent::Type type = e->type();

    if (type == ConnectedEvent::Type) {
        handleConnectedEvent(static_cast<ConnectedEvent*>(e));
        return true;
    }

    if (type == DisconnectedEvent::Type) {
        handleDisconnectedEvent(static_cast<DisconnectedEvent*>(e));
        return true;
    }

    if (type == ScannerErrorEvent::Type) {
        handleScannerErrorEvent(static_cast<ScannerErrorEvent*>(e));
        return true;
    }

    if (type == ConnectionPendingEvent::Type) {
        handleConnectionPendingEvent(static_cast<ConnectionPendingEvent*>(e));
        return true;
    }

    if (type == ConnectionVerificationEvent::Type) {
        handleConnectionVerificationEvent(static_cast<ConnectionVerificationEvent*>(e));
        return true;
    }

    if (type == ConnectionFailedVerificationEvent::Type) {
        handleConnectionFailedVerificationEvent(static_cast<ConnectionFailedVerificationEvent*>(e));
        return true;
    }

    return QObject::event(e);
}

void DeviceConnectionController::handleConnectedEvent(ConnectedEvent* ev)
{
    const bool success = (ev->GetResult() == EventResult::SUCCESS);
    m_connected = success;
    m_pending   = false;

    emit connectedChanged();
    emit pendingChanged();

    if (!success) {
        m_lastError = QStringLiteral("Connection failed");
        emit lastErrorChanged();
    }
}

void DeviceConnectionController::handleDisconnectedEvent(DisconnectedEvent* ev)
{
    m_connected = false;
    m_pending   = false;

    emit connectedChanged();
    emit pendingChanged();

    m_lastError = QString::fromStdString(ev->GetErrorCode().message());
    emit lastErrorChanged();
}

void DeviceConnectionController::handleScannerErrorEvent(ScannerErrorEvent* ev)
{
    m_lastError = QString::fromStdString(ev->GetErrorCode().message());
    emit lastErrorChanged();
}

void DeviceConnectionController::handleConnectionPendingEvent(ConnectionPendingEvent* ev)
{
    const DeviceInfo info = ev->GetDeviceInfo();

    m_pending = true;
    emit pendingChanged();

    emit incomingConnectionRequested(QString::fromStdString(info.deviceName));

    // jeszcze nie mamy na PIN
    ev->AcceptConnection();
}

void DeviceConnectionController::handleConnectionFailedVerificationEvent(ConnectionFailedVerificationEvent* ev)
{
    emit verificationFailed(ev->GetLeftTries());
}

void DeviceConnectionController::handleConnectionVerificationEvent(ConnectionVerificationEvent* ev)
{
    // nie mamy jeszcze wgl pinu
    ev->SendAnswer(std::string{});
}
