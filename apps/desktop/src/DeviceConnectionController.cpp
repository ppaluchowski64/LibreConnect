#include "DeviceConnectionController.h"
#include <boost/uuid/uuid_io.hpp>
#include <asio/error.hpp>
#include <asio/ssl/error.hpp>

namespace
{
QString DeviceTypeToLabel(const DeviceType type)
{
    switch (type) {
    case DeviceType::Linux:
        return QStringLiteral("Linux");
    case DeviceType::macOS:
        return QStringLiteral("macOS");
    case DeviceType::Windows:
        return QStringLiteral("Windows");
    case DeviceType::Android:
        return QStringLiteral("Android");
    case DeviceType::iOS:
        return QStringLiteral("iOS");
    case DeviceType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

bool IsBenignScannerShutdownError(const std::error_code& errorCode)
{
    return errorCode == asio::error::operation_aborted ||
           errorCode == asio::error::not_connected;
}

bool IsBenignDisconnectError(const std::error_code& errorCode)
{
    return !errorCode ||
           errorCode == asio::error::operation_aborted ||
           errorCode == asio::error::eof ||
           errorCode == asio::error::connection_reset ||
           errorCode == asio::error::connection_aborted ||
           errorCode == asio::error::shut_down ||
           errorCode == asio::ssl::error::stream_truncated;
}
}

DeviceConnectionController::DeviceConnectionController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
    refreshPairedDevices();
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

QVariantList DeviceConnectionController::getPairedDevices()
{
    QVariantList entries;
    const std::vector<DeviceInfoLite> devices = ConnectionManager::GetPairedDevices();
    entries.reserve(static_cast<int>(devices.size()));

    for (const auto& device : devices) {
        QVariantMap entry;
        entry.insert(QStringLiteral("deviceId"), QString::fromStdString(boost::uuids::to_string(device.deviceID)));
        entry.insert(QStringLiteral("deviceName"), QString::fromStdString(device.deviceName));
        entry.insert(QStringLiteral("deviceType"), DeviceTypeToLabel(device.deviceType));
        entries.push_back(entry);
    }

    const bool hasDevices = !entries.isEmpty();
    if (m_hasPairedDevices != hasDevices) {
        m_hasPairedDevices = hasDevices;
        emit pairedDevicesChanged();
    }

    return entries;
}

bool DeviceConnectionController::removePairedDevice(const QString& deviceId)
{
    const bool removed = ConnectionManager::RemovePairedDevice(deviceId.toStdString());
    refreshPairedDevices();
    return removed;
}

void DeviceConnectionController::refreshPairedDevices()
{
    const bool hasDevices = !ConnectionManager::GetPairedDevices().empty();
    if (m_hasPairedDevices == hasDevices) {
        return;
    }

    m_hasPairedDevices = hasDevices;
    emit pairedDevicesChanged();
}

void DeviceConnectionController::submitVerificationCode(const QString& code)
{
    if (!m_verificationEvent) {
        return;
    }

    if (!m_verificationError.isEmpty()) {
        m_verificationError.clear();
        emit verificationErrorChanged();
    }

    const std::string response = code.trimmed().toStdString();
    m_verificationEvent->SendAnswer(response);
}

void DeviceConnectionController::cancelVerification()
{
    if (m_verificationEvent) {
        m_verificationEvent->SendAnswer(std::string{});
    }

    m_verificationEvent.reset();
    // Let the backend handle the rejection response and disconnect.
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

    if (type == ModuleErrorEvent::Type) {
        handleModuleErrorEvent(static_cast<ModuleErrorEvent*>(e));
        return true;
    }

    return QObject::event(e);
}

void DeviceConnectionController::handleConnectedEvent(ConnectedEvent* ev)
{
    const bool success = (ev->GetResult() == EventResult::SUCCESS);
    const bool connectedChangedValue = (m_connected != success);
    const bool pendingChangedValue = m_pending;
    m_connected = success;
    m_pending   = false;

    if (connectedChangedValue) {
        emit connectedChanged();
    }

    if (pendingChangedValue) {
        emit pendingChanged();
    }

    if (m_verificationPending) {
        m_verificationPending = false;
        emit verificationPendingChanged();
    }

    m_verificationEvent.reset();

    if (!success) {
        handleError("Connection failed", ev->type());
    }
}

void DeviceConnectionController::handleDisconnectedEvent(DisconnectedEvent* ev)
{
    const bool connectedChangedValue = m_connected;
    const bool pendingChangedValue = m_pending;
    m_connected = false;
    m_pending   = false;

    if (connectedChangedValue) {
        emit connectedChanged();
    }

    if (pendingChangedValue) {
        emit pendingChanged();
    }

    if (m_verificationPending) {
        m_verificationPending = false;
        emit verificationPendingChanged();
    }

    if (m_verificationTriesLeft != 0) {
        m_verificationTriesLeft = 0;
        emit verificationTriesLeftChanged();
    }

    if (!m_verificationError.isEmpty()) {
        m_verificationError.clear();
        emit verificationErrorChanged();
    }

    m_verificationEvent.reset();

    if (!IsBenignDisconnectError(ev->GetErrorCode())) {
        handleError(ev->GetErrorCode().message(), ev->type());
    }
}

void DeviceConnectionController::handleError(const std::string& message)
{
    Debug::LogError("DeviceConnectionController error: {}", message);
    m_lastError = QString::fromStdString(message);

    emit lastErrorChanged();
}

void DeviceConnectionController::handleError(const std::string& message, QEvent::Type type)
{
    Debug::LogError("DeviceConnectionController error from event {}: {}", static_cast<int>(type), message);
    m_lastError = QString::fromStdString(message);

    emit lastErrorChanged();
}

void DeviceConnectionController::handleScannerErrorEvent(ScannerErrorEvent* ev)
{
    if (IsBenignScannerShutdownError(ev->GetErrorCode())) {
        return;
    }

    handleError(ev->GetErrorCode().message(), ev->type());
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
    m_verificationTriesLeft = ev->GetLeftTries();
    emit verificationTriesLeftChanged();

    m_verificationError = QString("Verification failed (%1 tries left)").arg(m_verificationTriesLeft);
    emit verificationErrorChanged();

    emit verificationFailed(m_verificationTriesLeft);
}

void DeviceConnectionController::handleConnectionVerificationEvent(ConnectionVerificationEvent* ev)
{
    if (m_verificationError.length() > 0) {
        m_verificationError.clear();
        emit verificationErrorChanged();
    }

    if (m_verificationTriesLeft != 0) {
        m_verificationTriesLeft = 0;
        emit verificationTriesLeftChanged();
    }

    m_verificationEvent.reset(ev->clone());

    if (!m_verificationPending) {
        m_verificationPending = true;
        emit verificationPendingChanged();
    }
}

void DeviceConnectionController::handleModuleErrorEvent(ModuleErrorEvent* ev)
{
    const QString message = QStringLiteral("%1 module error: %2.")
        .arg(QString::fromLatin1(ModuleTypeToString(ev->GetModuleType())))
        .arg(QString::fromLatin1(ModuleFailReasonToString(ev->GetError())));
    handleError(message.toStdString(), ev->type());
}
