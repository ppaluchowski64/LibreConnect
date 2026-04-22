#include "MobileConnectionController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QTimer>

#include <vector>

#include <boost/uuid/uuid_io.hpp>

#include <AddressResolver.h>
#include <ConnectionManager.h>
#include <DeviceInfo.h>
#include <ThreadPool.h>

#ifdef ANDROID_DEVICE
#include <PermissionManager.h>
#endif

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
}

MobileConnectionController::MobileConnectionController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"))
{
    m_permissionsOnboardingCompleted = m_settings.value(
        QStringLiteral("permissions/onboardingCompleted"),
        false
    ).toBool();

    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    if (auto* guiApp = qobject_cast<QGuiApplication*>(qApp)) {
        QObject::connect(guiApp, &QGuiApplication::applicationStateChanged, this, [this](const Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) {
                return;
            }

            updatePermissionsFromSystem();
            if (m_connected) {
                sendPermissionSnapshotToPeer();
            }
        });
    }

    refreshPairedDevices();
    refreshLocalIdentity();
    updatePermissionsFromSystem();
}

void MobileConnectionController::disconnect()
{
    ConnectionManager::Disconnect();
}

void MobileConnectionController::refreshPairedDevices()
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
    const bool hasChanged = m_pairedDevices != entries || m_hasPairedDevices != hasDevices;
    m_pairedDevices = entries;
    m_hasPairedDevices = hasDevices;

    if (hasChanged) {
        emit pairedDevicesChanged();
    }
}

bool MobileConnectionController::removePairedDevice(const QString& deviceId)
{
    const bool removed = ConnectionManager::RemovePairedDevice(deviceId.toStdString());
    if (!removed) {
        return false;
    }

    if (m_connected && deviceId == activePeerDeviceId()) {
        ConnectionManager::Disconnect();
    }

    refreshPairedDevices();
    return true;
}

bool MobileConnectionController::unpairCurrentDevice()
{
    const QString peerId = activePeerDeviceId();
    if (peerId.isEmpty()) {
        return false;
    }

    return removePairedDevice(peerId);
}

void MobileConnectionController::refreshLocalIdentity()
{
    const DeviceInfo localInfo = DeviceInfo::GetThisDeviceInfo();
    QString resolvedIpAddress = QStringLiteral("Unavailable");

    std::vector<IPAddress> privateAddresses = AddressResolver::GetAllPrivateIPv4();
    if (privateAddresses.empty()) {
        privateAddresses = AddressResolver::GetAllIPv4();
    }

    if (!privateAddresses.empty()) {
        resolvedIpAddress = QString::fromStdString(privateAddresses.front().to_string());
    }

    const QString resolvedDeviceName = QString::fromStdString(localInfo.deviceName);
    if (m_localDeviceName == resolvedDeviceName && m_localIpAddress == resolvedIpAddress) {
        return;
    }

    m_localDeviceName = resolvedDeviceName;
    m_localIpAddress = resolvedIpAddress;
    emit localIdentityChanged();
}

void MobileConnectionController::refreshPermissionStatuses()
{
    updatePermissionsFromSystem();
    if (m_connected) {
        sendPermissionSnapshotToPeer();
    }
}

void MobileConnectionController::requestCameraPermission()
{
    runPermissionRequest(PermissionRequest::Camera);
}

void MobileConnectionController::requestNotificationSendPermission()
{
    runPermissionRequest(PermissionRequest::NotificationSend);
}

void MobileConnectionController::requestNotificationListenerPermission()
{
    runPermissionRequest(PermissionRequest::NotificationListener);
}

void MobileConnectionController::requestNotificationPermissions()
{
    runPermissionRequest(PermissionRequest::Notifications);
}

void MobileConnectionController::requestFilePermission()
{
    runPermissionRequest(PermissionRequest::FileAccess);
}

void MobileConnectionController::requestAllFilesPermission()
{
    runPermissionRequest(PermissionRequest::AllFilesAccess);
}

void MobileConnectionController::requestBatteryPermission()
{
    runPermissionRequest(PermissionRequest::Battery);
}

void MobileConnectionController::requestSmsPermissions()
{
    runPermissionRequest(PermissionRequest::Sms);
}

void MobileConnectionController::requestSmsReceivePermission()
{
    runPermissionRequest(PermissionRequest::SmsReceive);
}

void MobileConnectionController::requestSmsReadPermission()
{
    runPermissionRequest(PermissionRequest::SmsRead);
}

void MobileConnectionController::requestSmsSendPermission()
{
    runPermissionRequest(PermissionRequest::SmsSend);
}

void MobileConnectionController::requestContactsPermission()
{
    runPermissionRequest(PermissionRequest::Contacts);
}

void MobileConnectionController::requestAllPermissions()
{
    runPermissionRequest(PermissionRequest::All);
}

void MobileConnectionController::completePermissionsOnboarding()
{
    if (m_permissionsOnboardingCompleted) {
        return;
    }

    m_permissionsOnboardingCompleted = true;
    m_settings.setValue(QStringLiteral("permissions/onboardingCompleted"), true);
    emit permissionsStateChanged();
}

void MobileConnectionController::setError(const QString& e)
{
    if (m_lastError == e) {
        return;
    }

    m_lastError = e;
    emit lastErrorChanged();
}

void MobileConnectionController::clearError()
{
    if (m_lastError.isEmpty()) {
        return;
    }

    m_lastError.clear();
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

void MobileConnectionController::handleModuleErrorEvent(ModuleErrorEvent* ev)
{
    const QString message = QStringLiteral("%1 module error: %2.")
        .arg(QString::fromLatin1(ModuleTypeToString(ev->GetModuleType())))
        .arg(QString::fromLatin1(ModuleFailReasonToString(ev->GetError())));
    setError(message);
}

void MobileConnectionController::updatePermissionsFromSystem()
{
#ifdef ANDROID_DEVICE
    setPermissionSnapshot(
        PermissionManager::IsCameraAccessPermissionGranted(),
        PermissionManager::IsNotificationEmitPermissionGranted(),
        PermissionManager::IsNotificationAccessPermissionGranted(),
        PermissionManager::IsFileAccessPermissionGranted(),
        PermissionManager::IsManagingExternalStoragePermissionGranted(),
        PermissionManager::IsBatteryOptimizationIgnored(),
        PermissionManager::IsReceiveSmsPermissionGranted(),
        PermissionManager::IsReadSmsPermissionGranted(),
        PermissionManager::IsSendSmsPermissionGranted(),
        PermissionManager::IsReadContactsPermissionGranted()
    );
#else
    setPermissionSnapshot(true, true, true, true, true, true, true, true, true, true);
#endif
}

void MobileConnectionController::runPermissionRequest(const PermissionRequest request)
{
    if (m_permissionsBusy) {
        return;
    }

    setPermissionsBusy(true);

#ifdef ANDROID_DEVICE
    QPointer<MobileConnectionController> weakThis(this);
    asio::co_spawn(ThreadPool::GetContext(), [weakThis, request]() -> asio::awaitable<void> {
        if (!weakThis) {
            co_return;
        }

        auto requestNotifications = []() -> asio::awaitable<void> {
            co_await PermissionManager::RequestNotificationEmitPermission();
            co_await PermissionManager::RequestNotificationAccessPermission();
            co_return;
        };

        auto requestSms = []() -> asio::awaitable<void> {
            co_await PermissionManager::RequestReceiveSmsPermission();
            co_await PermissionManager::RequestReadSmsPermission();
            co_await PermissionManager::RequestSendSmsPermission();
            co_await PermissionManager::RequestReadContactsPermission();
            co_return;
        };

        switch (request) {
        case PermissionRequest::Camera:
            co_await PermissionManager::RequestCameraAccessPermission();
            break;
        case PermissionRequest::Notifications:
            co_await requestNotifications();
            break;
        case PermissionRequest::NotificationSend:
            co_await PermissionManager::RequestNotificationEmitPermission();
            break;
        case PermissionRequest::NotificationListener:
            co_await PermissionManager::RequestNotificationAccessPermission();
            break;
        case PermissionRequest::FileAccess:
            co_await PermissionManager::RequestFileAccessPermission();
            break;
        case PermissionRequest::AllFilesAccess:
            co_await PermissionManager::RequestManagingExternalStoragePermission();
            break;
        case PermissionRequest::Battery:
            co_await PermissionManager::RequestDisablingBatteryOptimizations();
            break;
        case PermissionRequest::Sms:
            co_await requestSms();
            break;
        case PermissionRequest::SmsReceive:
            co_await PermissionManager::RequestReceiveSmsPermission();
            break;
        case PermissionRequest::SmsRead:
            co_await PermissionManager::RequestReadSmsPermission();
            break;
        case PermissionRequest::SmsSend:
            co_await PermissionManager::RequestSendSmsPermission();
            break;
        case PermissionRequest::Contacts:
            co_await PermissionManager::RequestReadContactsPermission();
            break;
        case PermissionRequest::All:
            co_await PermissionManager::RequestCameraAccessPermission();
            co_await requestNotifications();
            co_await PermissionManager::RequestFileAccessPermission();
            co_await PermissionManager::RequestManagingExternalStoragePermission();
            co_await PermissionManager::RequestDisablingBatteryOptimizations();
            co_await requestSms();
            break;
        default:
            break;
        }

        const bool cameraGranted = PermissionManager::IsCameraAccessPermissionGranted();
        const bool notificationSendGranted = PermissionManager::IsNotificationEmitPermissionGranted();
        const bool notificationListenerGranted = PermissionManager::IsNotificationAccessPermissionGranted();
        const bool fileGranted = PermissionManager::IsFileAccessPermissionGranted();
        const bool allFilesGranted = PermissionManager::IsManagingExternalStoragePermissionGranted();
        const bool batteryGranted = PermissionManager::IsBatteryOptimizationIgnored();
        const bool smsReceiveGranted = PermissionManager::IsReceiveSmsPermissionGranted();
        const bool smsReadGranted = PermissionManager::IsReadSmsPermissionGranted();
        const bool smsSendGranted = PermissionManager::IsSendSmsPermissionGranted();
        const bool contactsGranted = PermissionManager::IsReadContactsPermissionGranted();

        QMetaObject::invokeMethod(
            qApp,
            [weakThis,
             cameraGranted,
             notificationSendGranted,
             notificationListenerGranted,
             fileGranted,
             allFilesGranted,
             batteryGranted,
             smsReceiveGranted,
             smsReadGranted,
             smsSendGranted,
             contactsGranted]() {
                if (!weakThis) {
                    return;
                }

                weakThis->setPermissionSnapshot(
                    cameraGranted,
                    notificationSendGranted,
                    notificationListenerGranted,
                    fileGranted,
                    allFilesGranted,
                    batteryGranted,
                    smsReceiveGranted,
                    smsReadGranted,
                    smsSendGranted,
                    contactsGranted
                );
                weakThis->setPermissionsBusy(false);
                if (weakThis->connected()) {
                    weakThis->sendPermissionSnapshotToPeer();
                }
            },
            Qt::QueuedConnection
        );

        co_return;
    }, asio::detached);
#else
    updatePermissionsFromSystem();
    setPermissionsBusy(false);
#endif
}

void MobileConnectionController::setPermissionsBusy(const bool busy)
{
    if (m_permissionsBusy == busy) {
        return;
    }

    m_permissionsBusy = busy;
    emit permissionsStateChanged();
}

void MobileConnectionController::sendPermissionStatusToPeer(const PermissionType type, const bool granted)
{
    if (!m_connected) {
        return;
    }

    ConnectionManager::Send(
        granted ? PC_PackageType::PERMISSION_GRANTED : PC_PackageType::PERMISSION_REJECTED,
        type
    );
}

void MobileConnectionController::sendPermissionSnapshotToPeer()
{
    sendPermissionStatusToPeer(PermissionType::Camera, m_cameraPermissionGranted);
    sendPermissionStatusToPeer(PermissionType::Notifications, notificationsPermissionGranted());
    sendPermissionStatusToPeer(PermissionType::FileSystem, m_filePermissionGranted && m_allFilesPermissionGranted);
    sendPermissionStatusToPeer(PermissionType::Battery, m_batteryPermissionGranted);
}

void MobileConnectionController::setPermissionSnapshot(
    const bool cameraGranted,
    const bool notificationSendGranted,
    const bool notificationListenerGranted,
    const bool fileGranted,
    const bool allFilesGranted,
    const bool batteryGranted,
    const bool smsReceiveGranted,
    const bool smsReadGranted,
    const bool smsSendGranted,
    const bool contactsGranted
)
{
    const bool changed =
        m_cameraPermissionGranted != cameraGranted ||
        m_notificationSendPermissionGranted != notificationSendGranted ||
        m_notificationListenerPermissionGranted != notificationListenerGranted ||
        m_filePermissionGranted != fileGranted ||
        m_allFilesPermissionGranted != allFilesGranted ||
        m_batteryPermissionGranted != batteryGranted ||
        m_smsReceivePermissionGranted != smsReceiveGranted ||
        m_smsReadPermissionGranted != smsReadGranted ||
        m_smsSendPermissionGranted != smsSendGranted ||
        m_contactsPermissionGranted != contactsGranted;

    m_cameraPermissionGranted = cameraGranted;
    m_notificationSendPermissionGranted = notificationSendGranted;
    m_notificationListenerPermissionGranted = notificationListenerGranted;
    m_filePermissionGranted = fileGranted;
    m_allFilesPermissionGranted = allFilesGranted;
    m_batteryPermissionGranted = batteryGranted;
    m_smsReceivePermissionGranted = smsReceiveGranted;
    m_smsReadPermissionGranted = smsReadGranted;
    m_smsSendPermissionGranted = smsSendGranted;
    m_contactsPermissionGranted = contactsGranted;

    if (changed) {
        emit permissionsStateChanged();
    }
}

bool MobileConnectionController::notificationsPermissionGranted() const
{
    return m_notificationSendPermissionGranted && m_notificationListenerPermissionGranted;
}

QString MobileConnectionController::activePeerDeviceId() const
{
    return m_connectedPeerDeviceId;
}

bool MobileConnectionController::event(QEvent* e)
{
    const auto type = e->type();

    if (type == ConnectedEvent::Type) {
        auto* ev = static_cast<ConnectedEvent*>(e);

        const bool ok = (ev->GetResult() == EventResult::SUCCESS);
        if (m_connected != ok) {
            m_connected = ok;
            emit connectedChanged();
            emit permissionsStateChanged();
        }

        if (ok) {
            clearError();
            clearChallenge();
            updatePermissionsFromSystem();
            sendPermissionSnapshotToPeer();
            QTimer::singleShot(750, this, [this]() {
                if (m_connected) {
                    sendPermissionSnapshotToPeer();
                }
            });
        } else {
            m_connectedPeerDeviceId.clear();
            setError(QStringLiteral("Connection failed"));
        }

        refreshPairedDevices();
        return true;
    }

    if (type == DisconnectedEvent::Type) {
        auto* ev = static_cast<DisconnectedEvent*>(e);

        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
            emit permissionsStateChanged();
        }

        clearChallenge();
        m_connectedPeerDeviceId.clear();
        setError(QString::fromStdString(ev->GetErrorCode().message()));
        refreshPairedDevices();
        return true;
    }

    if (type == ScannerErrorEvent::Type) {
        auto* ev = static_cast<ScannerErrorEvent*>(e);
        setError(QString::fromStdString(ev->GetErrorCode().message()));
        return true;
    }

    if (type == ConnectionPendingEvent::Type) {
        auto* ev = static_cast<ConnectionPendingEvent*>(e);

        const DeviceInfo info = ev->GetDeviceInfo();
        m_connectedPeerDeviceId = QString::fromStdString(boost::uuids::to_string(info.deviceID));
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
        setError(QStringLiteral("Verification failed (%1 tries left)").arg(ev->GetLeftTries()));
        return true;
    }

    if (type == ConnectionVerificationEvent::Type) {
        auto* ev = static_cast<ConnectionVerificationEvent*>(e);
        ev->SendAnswer(std::string{});
        return true;
    }

    if (type == ModuleErrorEvent::Type) {
        handleModuleErrorEvent(static_cast<ModuleErrorEvent*>(e));
        return true;
    }

    if (type == ModuleRequestedPermission::Type ||
        type == ModuleRequestedPermissionGranted::Type ||
        type == ModuleRequestedPermissionRejected::Type) {
        updatePermissionsFromSystem();
        if (m_connected) {
            sendPermissionSnapshotToPeer();
        }
        return true;
    }

    return QObject::event(e);
}
