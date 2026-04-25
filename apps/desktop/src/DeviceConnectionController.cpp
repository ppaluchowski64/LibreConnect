#include "DeviceConnectionController.h"
#include <boost/uuid/uuid_io.hpp>
#include <asio/error.hpp>
#include <asio/ssl/error.hpp>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <QCoreApplication>
#include <QMetaObject>
#include <QRegularExpression>
#include <PermissionManager.h>

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

bool IsPendingHandshakeClosureError(const std::error_code& errorCode)
{
    return errorCode == asio::error::eof ||
           errorCode == asio::error::connection_reset ||
           errorCode == asio::error::connection_aborted ||
           errorCode == asio::error::shut_down ||
           errorCode == asio::ssl::error::stream_truncated;
}

std::string CapitalizeErrorMessage(std::string message)
{
    if (!message.empty()) {
        message[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(message[0])));
    }
    return message;
}

constexpr auto kFindMyPhoneStopPackage = PC_PackageType::FIND_MY_PHONE_STOP_RINGING;
}

DeviceConnectionController::DeviceConnectionController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
    ConnectionManager::AddResponseHandler(kFindMyPhoneStopPackage, [weakThis = QPointer<DeviceConnectionController>(this)](PC_Package&&) {
        if (!weakThis || !qApp) {
            return;
        }

        QMetaObject::invokeMethod(qApp, [weakThis]() {
            if (!weakThis) {
                return;
            }

            weakThis->setFindMyPhoneAlertActive(false);
        }, Qt::QueuedConnection);
    });

    refreshPairedDevices();
}

DeviceConnectionController::~DeviceConnectionController()
{
    ConnectionManager::RemoveResponseHandler(kFindMyPhoneStopPackage);
}

bool DeviceConnectionController::localNetworkPermissionGranted() const
{
    return PermissionManager::IsLocalNetworkAccessPermissionGranted();
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

bool DeviceConnectionController::startFindMyPhoneAlert()
{
    if (!m_connected) {
        return false;
    }

    ConnectionManager::Send(PC_PackageType::FIND_MY_PHONE_START_RINGING);
    setFindMyPhoneAlertActive(true);

    return true;
}

void DeviceConnectionController::stopFindMyPhoneAlert()
{
    if (m_connected) {
        ConnectionManager::Send(PC_PackageType::FIND_MY_PHONE_STOP_RINGING);
    }

    setFindMyPhoneAlertActive(false);
}

void DeviceConnectionController::submitVerificationCode(const QString& code)
{
    if (!m_verificationEvent) {
        return;
    }

    const QString trimmedCode = code.trimmed();
    static const QRegularExpression sixDigitPattern(QStringLiteral("^\\d{6}$"));
    if (!sixDigitPattern.match(trimmedCode).hasMatch()) {
        m_verificationError = QStringLiteral("Code must be exactly 6 digits.");
        emit verificationErrorChanged();
        return;
    }

    if (!m_verificationError.isEmpty()) {
        m_verificationError.clear();
        emit verificationErrorChanged();
    }

    const std::string response = trimmedCode.toStdString();
    m_verificationEvent->SendAnswer(response);
}

void DeviceConnectionController::cancelVerification()
{
    if (m_verificationEvent) {
        m_verificationEvent->SendAnswer(std::string{});
    }

    m_verificationEvent.reset();
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

    if (m_pending) {
        m_pending = false;
        emit pendingChanged();
    }

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

    if (type == DeviceNotPairedEvent::Type) {
        const auto* ev = static_cast<DeviceNotPairedEvent*>(e);
        const QString deviceId = QString::fromStdString(ev->GetDeviceID());
        std::string message = "This device is no longer paired with the remote device. Pair the devices again and retry. " + deviceId.toStdString();

        handleError(message, type);
        if (!deviceId.isEmpty()) {
            removePairedDevice(deviceId);
        } else {
            refreshPairedDevices();
        }
        return true;
    }

    if (type == DeviceCooldownEvent::Type) {
        const auto* ev = static_cast<DeviceCooldownEvent*>(e);
        handleError(
            QStringLiteral("Connection temporarily blocked by the remote device. Try again in about %1 seconds.")
                .arg(ev->LeftDuration(), 0, 'f', 1)
                .toStdString(),
            type
        );
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
    setFindMyPhoneAlertActive(false);

    if (pendingChangedValue &&
        !connectedChangedValue &&
        IsPendingHandshakeClosureError(ev->GetErrorCode())) {
        handleError(
            "Connection was closed by the remote device. If this was a paired-mode connect, verify that both devices are still paired.",
            ev->type()
        );
        return;
    }

    if (!IsBenignDisconnectError(ev->GetErrorCode())) {
        handleError(CapitalizeErrorMessage(ev->GetErrorCode().message()), ev->type());
    }
}

void DeviceConnectionController::handleError(const std::string& message)
{
    Debug::LogError("DeviceConnectionController error: {}", message);

    if (m_pending) {
        m_pending = false;
        emit pendingChanged();
    }

    m_lastError = QString::fromStdString(message);

    emit lastErrorChanged();
}

void DeviceConnectionController::handleError(const std::string& message, QEvent::Type type)
{
    Debug::LogError("DeviceConnectionController error from event {}: {}", static_cast<int>(type), message);

    if (m_pending) {
        m_pending = false;
        emit pendingChanged();
    }

    m_lastError = QString::fromStdString(message);

    emit lastErrorChanged();
}

void DeviceConnectionController::handleScannerErrorEvent(ScannerErrorEvent* ev)
{
    if (IsBenignScannerShutdownError(ev->GetErrorCode())) {
        return;
    }

#ifdef MACOS_DEVICE
    if (ev->GetErrorCode() == std::make_error_code(std::errc::permission_denied)) {
        emit localNetworkPermissionGrantedChanged();
        handleError(
            "Local network permission is required to discover devices. "
            "Allow LibreConnect in System Settings > Privacy & Security > Local Network, then try again.",
            ev->type()
        );
        return;
    }
#endif

    handleError(CapitalizeErrorMessage(ev->GetErrorCode().message()), ev->type());
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
    // Ignore stale events after cancel/close.
    if (!m_verificationPending) {
        return;
    }

    m_verificationTriesLeft = std::max(0, ev->GetLeftTries());
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

void DeviceConnectionController::setFindMyPhoneAlertActive(const bool active)
{
    if (m_findMyPhoneAlertActive == active) {
        return;
    }

    m_findMyPhoneAlertActive = active;
    emit findMyPhoneAlertActiveChanged();
}
