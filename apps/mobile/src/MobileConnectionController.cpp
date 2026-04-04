#include "MobileConnectionController.h"
#include <QRandomGenerator>

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

void MobileConnectionController::clearChallenge()
{
    if (!m_challengeCode.isEmpty()) {
        m_challengeCode.clear();
        emit challengeCodeChanged();
    }

    if (m_challengeVisible) {
        m_challengeVisible = false;
        emit challengeVisibleChanged();
    }

    if (!m_pendingDeviceName.isEmpty()) {
        m_pendingDeviceName.clear();
        emit pendingDeviceNameChanged();
    }
}

bool MobileConnectionController::event(QEvent* e)
{
    const auto type = e->type();

    if (type == ConnectedEvent::Type) {
        auto* ev = static_cast<ConnectedEvent*>(e);

        bool ok = (ev->GetResult() == EventResult::SUCCESS);
        m_connected = ok;
        emit connectedChanged();

        if (ok) {
            clearChallenge();
        }

        if (!ok)
            setError("Connection failed");

        return true;
    }

    if (type == DisconnectedEvent::Type) {
        auto* ev = static_cast<DisconnectedEvent*>(e);

        m_connected = false;
        emit connectedChanged();

        clearChallenge();

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

        m_pendingDeviceName = QString::fromStdString(info.deviceName);
        emit pendingDeviceNameChanged();

        if (ev->GetInitialConnectionMode() == InitialConnectionMode::CONNECT_WITH_PAIR) {
            clearChallenge();
            ev->AcceptConnection();
            return true;
        }

        m_challengeCode = QString::fromStdString(ev->GetPairingCode());
        if (m_challengeCode.isEmpty()) {
            // Fallback for legacy/invalid payloads.
            const int codeValue = QRandomGenerator::global()->bounded(1000000);
            m_challengeCode = QString("%1").arg(codeValue, 6, 10, QLatin1Char('0'));
        }
        emit challengeCodeChanged();

        if (!m_challengeVisible) {
            m_challengeVisible = true;
            emit challengeVisibleChanged();
        }

        ev->AcceptConnectionIfVerified(m_challengeCode.toStdString());

        return true;
    }

    if (type == ConnectionFailedVerificationEvent::Type) {
        auto* ev = static_cast<ConnectionFailedVerificationEvent*>(e);
        setError(QString("Verification failed (%1 tries left)").arg(ev->GetLeftTries()));
        return true;
    }

    if (type == ConnectionVerificationEvent::Type) {
        auto* ev = static_cast<ConnectionVerificationEvent*>(e);

        ev->SendAnswer(std::string{});
        return true;
    }

    return QObject::event(e);
}
