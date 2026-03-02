#include "MobileConnectionController.h"

MobileConnectionController::MobileConnectionController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
}

void MobileConnectionController::setError(const QString& e)
{
    if (m_lastError == e)
        return;

    m_lastError = e;
    emit lastErrorChanged();
}

bool MobileConnectionController::event(QEvent* e)
{
    const auto type = e->type();

    if (type == ConnectedEvent::Type) {
        auto* ev = static_cast<ConnectedEvent*>(e);

        bool ok = (ev->GetResult() == EventResult::SUCCESS);
        m_connected = ok;
        emit connectedChanged();

        if (!ok)
            setError("Connection failed");

        return true;
    }

    if (type == DisconnectedEvent::Type) {
        auto* ev = static_cast<DisconnectedEvent*>(e);

        m_connected = false;
        emit connectedChanged();

        setError(QString::fromStdString(ev->GetErrorCode().message()));
        return true;
    }

    if (type == ScannerErrorEvent::Type) {
        auto* ev = static_cast<ScannerErrorEvent*>(e);
        setError(QString::fromStdString(ev->GetErrorCode().message()));
        return true;
    }

    if (type == ConnectionPendingEvent::Type) {
        auto* ev = static_cast<ConnectionPendingEvent*>(e);

        DeviceInfo info = ev->GetDeviceInfo();
        emit incomingConnection(QString::fromStdString(info.deviceName));

        ev->AcceptConnection();

        return true;
    }

    if (type == ConnectionFailedVerificationEvent::Type) {
        auto* ev = static_cast<ConnectionFailedVerificationEvent*>(e);
        setError(QString("Verification failed (%1 tries left)").arg(ev->GetLeftTries()));
        return true;
    }

    if (type == ConnectionVerificationEvent::Type) {
        auto* ev = static_cast<ConnectionVerificationEvent*>(e);

        // Later: show PIN UI
        ev->SendAnswer(std::string{});
        return true;
    }

    return QObject::event(e);
}
