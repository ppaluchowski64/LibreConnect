#include "MobileConnectionController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>
#include <QRandomGenerator>
#include <QTimer>
#include <QtMath>

#include <memory>
#include <vector>

#include <boost/uuid/uuid_io.hpp>

#include <AddressResolver.h>
#include <ConnectionManager.h>
#include <DeviceInfo.h>
#include <ModulesManager.h>
#include <SystemInfoShareModule.h>
#include <ThreadPool.h>

#ifdef ANDROID_DEVICE
#include <FindMyBridge.h>
#include <NotificationBridge.h>
#include <PermissionManager.h>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace
{
constexpr auto kFindMyPhoneStopPackage = PC_PackageType::FIND_MY_PHONE_STOP_RINGING;
constexpr auto kFindMyPhoneRingtoneSetting = "findMyPhone/ringtoneUri";
constexpr auto kFindMyPhoneAlertActiveSetting = "findMyPhone/alertActive";

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
            setFindMyPhoneAlertActive(m_settings.value(QString::fromLatin1(kFindMyPhoneAlertActiveSetting), false).toBool());
        });
    }

    refreshPairedDevices();
    refreshLocalIdentity();
    updatePermissionsFromSystem();

#ifdef ANDROID_DEVICE
    NotificationBridge::AddNotificationActionHandler("find_my_phone", "Stop", [this](){
        stopFindMyPhoneAlertInternal(true);
    });
#endif

    m_findMyPhoneRingtoneUri = m_settings.value(QString::fromLatin1(kFindMyPhoneRingtoneSetting), QString()).toString().trimmed();
    ensureSelectedRingtoneOption();
    setFindMyPhoneAlertActive(m_settings.value(QString::fromLatin1(kFindMyPhoneAlertActiveSetting), false).toBool());
}

MobileConnectionController::~MobileConnectionController()
{
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

void MobileConnectionController::acceptConnectionApproval()
{
    if (!m_approvalEvent) {
        return;
    }

    m_approvalEvent->AcceptConnection();
    clearApproval();
}

void MobileConnectionController::denyConnectionApproval()
{
    if (!m_approvalEvent) {
        ConnectionManager::Disconnect();
        return;
    }

    m_approvalEvent->DenyConnection();
    clearApproval();
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

void MobileConnectionController::requestMicrophonePermission()
{
    runPermissionRequest(PermissionRequest::Microphone);
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

QString MobileConnectionController::findMyPhoneRingtoneLabel() const
{
    return resolveFindMyPhoneRingtoneLabel(m_findMyPhoneRingtoneUri);
}

void MobileConnectionController::stopFindMyPhoneAlert()
{
    stopFindMyPhoneAlertInternal(true);
}

void MobileConnectionController::refreshFindMyPhoneRingtones(const bool force)
{
    if (m_findMyPhoneRingtonesLoaded && !force) {
        return;
    }

    const QVariantList options = queryFindMyPhoneRingtoneOptions();
    m_findMyPhoneRingtonesLoaded = true;
    if (m_findMyPhoneRingtoneOptions != options) {
        m_findMyPhoneRingtoneOptions = options;
        emit findMyPhoneRingtoneOptionsChanged();
    }

    ensureSelectedRingtoneOption();
    emit findMyPhoneRingtoneChanged();
}

void MobileConnectionController::setFindMyPhoneRingtoneUri(const QString& uri)
{
    setFindMyPhoneRingtoneUriInternal(uri, true);
}

void MobileConnectionController::setFindMyPhoneRingtoneFile(const QUrl& fileUrl)
{
    const QString uri = fileUrl.toString();
    setFindMyPhoneRingtoneUriInternal(uri, true);
}

void MobileConnectionController::refreshDefaultDownloadPath()
{
    if (!m_connected) {
        setDefaultDownloadPathStatus(QStringLiteral("Connect to a desktop device to load the default download path."));
        return;
    }

    setDefaultDownloadPathStatus(QStringLiteral("Loading default download path..."));
    QPointer<MobileConnectionController> weakThis(this);
    asio::co_spawn(ThreadPool::GetContext(), [weakThis]() -> asio::awaitable<void> {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(
            PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_GET_REQUEST
        );

        QString path;
        QString status;
        if (response.has_value()) {
            path = QString::fromStdString(response.value()->GetValue<std::string>());
            status = QStringLiteral("Default download path loaded.");
        } else {
            status = QStringLiteral("Could not load the desktop default download path.");
        }

        QMetaObject::invokeMethod(qApp, [weakThis, path, status]() {
            if (!weakThis) {
                return;
            }

            if (!path.isEmpty()) {
                weakThis->setDefaultDownloadPathInternal(path);
            }
            weakThis->setDefaultDownloadPathStatus(status);
        }, Qt::QueuedConnection);
    }, asio::detached);
}

void MobileConnectionController::setDefaultDownloadPath(const QString& path)
{
    const QString normalizedPath = path.trimmed();
    if (!m_connected) {
        setDefaultDownloadPathStatus(QStringLiteral("Connect to a desktop device before changing the download path."));
        return;
    }

    if (normalizedPath.isEmpty()) {
        setDefaultDownloadPathStatus(QStringLiteral("Enter a desktop folder path."));
        return;
    }

    setDefaultDownloadPathStatus(QStringLiteral("Saving default download path..."));
    QPointer<MobileConnectionController> weakThis(this);
    asio::co_spawn(ThreadPool::GetContext(), [weakThis, normalizedPath]() -> asio::awaitable<void> {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(
            PC_PackageType::FILE_SHARE_INCOMING_DIRECTORY_SET_REQUEST,
            normalizedPath.toStdString()
        );

        bool success = false;
        QString resolvedPath = normalizedPath;
        QString message = QStringLiteral("Could not save the desktop default download path.");
        if (response.has_value()) {
            success = response.value()->GetValue<bool>();
            resolvedPath = QString::fromStdString(response.value()->GetValue<std::string>());
            message = QString::fromStdString(response.value()->GetValue<std::string>());
        }

        QMetaObject::invokeMethod(qApp, [weakThis, success, resolvedPath, message]() {
            if (!weakThis) {
                return;
            }

            if (success) {
                weakThis->setDefaultDownloadPathInternal(resolvedPath);
            }
            weakThis->setDefaultDownloadPathStatus(message);
        }, Qt::QueuedConnection);
    }, asio::detached);
}

void MobileConnectionController::exportLogs()
{
#ifdef ANDROID_DEVICE
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/FileSystemUtils",
        "shareLogs",
        "(Landroid/content/Context;)V",
        context.object<jobject>()
    );
#endif
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

void MobileConnectionController::clearApproval()
{
    m_approvalEvent.reset();

    if (m_approvalVisible) {
        m_approvalVisible = false;
        emit approvalVisibleChanged();
    }
}

void MobileConnectionController::handleModuleErrorEvent(ModuleErrorEvent* ev)
{
    const QString message = QStringLiteral("%1 module error: %2.")
        .arg(QString::fromLatin1(ModuleTypeToString(ev->GetModuleType())))
        .arg(QString::fromLatin1(ModuleFailReasonToString(ev->GetError())));
    setError(message);
}

void MobileConnectionController::setFindMyPhoneAlertActive(const bool active)
{
    if (m_findMyPhoneAlertActive == active) {
        return;
    }

    m_findMyPhoneAlertActive = active;
    emit findMyPhoneAlertActiveChanged();
}

void MobileConnectionController::stopFindMyPhoneAlertInternal(const bool notifyPeer)
{
#ifdef ANDROID_DEVICE
    FindMyBridge::StopAlert();
#endif

    if (notifyPeer && m_connected) {
        ConnectionManager::Send(kFindMyPhoneStopPackage);
    }

    m_settings.setValue(QString::fromLatin1(kFindMyPhoneAlertActiveSetting), false);
    setFindMyPhoneAlertActive(false);
}

QVariantList MobileConnectionController::queryFindMyPhoneRingtoneOptions() const
{
    QVariantList options;
    QSet<QString> seenUris;

    auto addOption = [&options, &seenUris](const QString& uri, const QString& label) {
        const QString normalizedUri = uri.trimmed();
        if (seenUris.contains(normalizedUri)) {
            return;
        }

        seenUris.insert(normalizedUri);
        QVariantMap option;
        option.insert(QStringLiteral("value"), normalizedUri);
        option.insert(QStringLiteral("label"), label);
        options.push_back(option);
    };

    addOption(QString(), QStringLiteral("System Default Alarm"));

#ifdef ANDROID_DEVICE
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        const QJniObject jsonResult = QJniObject::callStaticObjectMethod(
            "com/LibreConnect/mobile/FindMyPhone",
            "getAvailableRingtones",
            "(Landroid/content/Context;)Ljava/lang/String;",
            context.object<jobject>()
        );

        const QJsonDocument document = QJsonDocument::fromJson(jsonResult.toString().toUtf8());
        if (document.isArray()) {
            const QJsonArray items = document.array();
            for (const QJsonValue& value : items) {
                if (!value.isObject()) {
                    continue;
                }

                const QJsonObject object = value.toObject();
                const QString uri = object.value(QStringLiteral("uri")).toString().trimmed();
                const QString label = object.value(QStringLiteral("label")).toString().trimmed();
                if (uri.isEmpty() || label.isEmpty()) {
                    continue;
                }

                addOption(uri, label);
            }
        }
    }
#endif

    return options;
}

void MobileConnectionController::ensureSelectedRingtoneOption()
{
    const QString selectedUri = m_findMyPhoneRingtoneUri.trimmed();
    for (const QVariant& optionValue : m_findMyPhoneRingtoneOptions) {
        const QVariantMap option = optionValue.toMap();
        if (option.value(QStringLiteral("value")).toString().trimmed() == selectedUri) {
            return;
        }
    }

    QVariantMap customOption;
    customOption.insert(QStringLiteral("value"), selectedUri);
    customOption.insert(QStringLiteral("label"), resolveFindMyPhoneRingtoneLabel(selectedUri));
    m_findMyPhoneRingtoneOptions.push_back(customOption);
    emit findMyPhoneRingtoneOptionsChanged();
}

void MobileConnectionController::setFindMyPhoneRingtoneUriInternal(const QString& uri, const bool persist)
{
    const QString normalizedUri = uri.trimmed();
    if (m_findMyPhoneRingtoneUri == normalizedUri) {
        return;
    }

    m_findMyPhoneRingtoneUri = normalizedUri;

    if (persist) {
        m_settings.setValue(QString::fromLatin1(kFindMyPhoneRingtoneSetting), m_findMyPhoneRingtoneUri);
    }

    ensureSelectedRingtoneOption();
    emit findMyPhoneRingtoneChanged();
}

QString MobileConnectionController::resolveFindMyPhoneRingtoneLabel(const QString& uri) const
{
    const QString normalizedUri = uri.trimmed();
    if (normalizedUri.isEmpty()) {
        return QStringLiteral("System Default Alarm");
    }

    for (const QVariant& optionValue : m_findMyPhoneRingtoneOptions) {
        const QVariantMap option = optionValue.toMap();
        if (option.value(QStringLiteral("value")).toString().trimmed() == normalizedUri) {
            return option.value(QStringLiteral("label")).toString();
        }
    }

#ifdef ANDROID_DEVICE
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        const QJniObject label = QJniObject::callStaticObjectMethod(
            "com/LibreConnect/mobile/FindMyPhone",
            "getRingtoneTitle",
            "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
            context.object<jobject>(),
            QJniObject::fromString(normalizedUri).object<jstring>()
        );

        const QString title = label.toString().trimmed();
        if (!title.isEmpty()) {
            return title;
        }
    }
#endif

    return QStringLiteral("Custom ringtone");
}

void MobileConnectionController::setDefaultDownloadPathInternal(const QString& path)
{
    if (m_defaultDownloadPath == path) {
        return;
    }

    m_defaultDownloadPath = path;
    emit defaultDownloadPathChanged();
}

void MobileConnectionController::setDefaultDownloadPathStatus(const QString& status)
{
    if (m_defaultDownloadPathStatus == status) {
        return;
    }

    m_defaultDownloadPathStatus = status;
    emit defaultDownloadPathStatusChanged();
}

void MobileConnectionController::setBatteryPercentage(const int percentage)
{
    if (m_batteryPercentage == percentage) {
        return;
    }

    m_batteryPercentage = percentage;
    emit batteryPercentageChanged();
}

void MobileConnectionController::updatePermissionsFromSystem()
{
#ifdef ANDROID_DEVICE
    setPermissionSnapshot(
        PermissionManager::IsCameraAccessPermissionGranted(),
        PermissionManager::IsMicrophoneAccessPermissionGranted(),
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
    setPermissionSnapshot(true, true, true, true, true, true, true, true, true, true, true);
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
        case PermissionRequest::Microphone:
            co_await PermissionManager::RequestMicrophoneAccessPermission();
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
            co_await PermissionManager::RequestMicrophoneAccessPermission();
            co_await requestNotifications();
            co_await PermissionManager::RequestManagingExternalStoragePermission();
            co_await PermissionManager::RequestDisablingBatteryOptimizations();
            co_await requestSms();
            break;
        default:
            break;
        }

        const bool cameraGranted = PermissionManager::IsCameraAccessPermissionGranted();
        const bool microphoneGranted = PermissionManager::IsMicrophoneAccessPermissionGranted();
        const bool notificationSendGranted = PermissionManager::IsNotificationEmitPermissionGranted();
        const bool notificationListenerGranted = PermissionManager::IsNotificationAccessPermissionGranted();
        const bool fileGranted = PermissionManager::IsFileAccessPermissionGranted();
        const bool allFilesGranted = PermissionManager::IsManagingExternalStoragePermissionGranted();
        const bool batteryGranted = PermissionManager::IsBatteryOptimizationIgnored();
        const bool smsReceiveGranted = PermissionManager::IsReceiveSmsPermissionGranted();
        const bool smsReadGranted = PermissionManager::IsReadSmsPermissionGranted();
        const bool smsSendGranted = PermissionManager::IsSendSmsPermissionGranted();
        const bool contactsGranted = PermissionManager::IsReadContactsPermissionGranted();
        const bool smsPermissionsGranted =
            smsReceiveGranted &&
            smsReadGranted &&
            smsSendGranted &&
            contactsGranted;
        const bool notificationPermissionsGranted = notificationSendGranted && notificationListenerGranted;
        bool requestAffectsNotificationPermissions = false;
        bool requestAffectsSmsPermissions = false;
        switch (request) {
        case PermissionRequest::Notifications:
        case PermissionRequest::NotificationSend:
        case PermissionRequest::NotificationListener:
        case PermissionRequest::All:
            requestAffectsNotificationPermissions = true;
            break;
        default:
            break;
        }

        switch (request) {
        case PermissionRequest::Sms:
        case PermissionRequest::SmsReceive:
        case PermissionRequest::SmsRead:
        case PermissionRequest::SmsSend:
        case PermissionRequest::Contacts:
        case PermissionRequest::All:
            requestAffectsSmsPermissions = true;
            break;
        default:
            break;
        }

        QMetaObject::invokeMethod(
            qApp,
            [weakThis,
             cameraGranted,
             microphoneGranted,
             notificationSendGranted,
             notificationListenerGranted,
             fileGranted,
             allFilesGranted,
             batteryGranted,
             smsReceiveGranted,
             smsReadGranted,
             smsSendGranted,
             contactsGranted,
             notificationPermissionsGranted,
             requestAffectsNotificationPermissions,
             smsPermissionsGranted,
             requestAffectsSmsPermissions]() {
                if (!weakThis) {
                    return;
                }

                weakThis->setPermissionSnapshot(
                    cameraGranted,
                    microphoneGranted,
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
                if (requestAffectsNotificationPermissions) {
                    std::unique_ptr<QEvent> event;
                    if (notificationPermissionsGranted) {
                        event = std::make_unique<ModuleRequestedPermissionGranted>(PermissionType::Notifications);
                    } else {
                        event = std::make_unique<ModuleRequestedPermissionRejected>(PermissionType::Notifications);
                    }
                    ConnectionManager::SendEvent(event);
                }
                if (requestAffectsSmsPermissions) {
                    std::unique_ptr<QEvent> event;
                    if (smsPermissionsGranted) {
                        event = std::make_unique<ModuleRequestedPermissionGranted>(PermissionType::Sms);
                    } else {
                        event = std::make_unique<ModuleRequestedPermissionRejected>(PermissionType::Sms);
                    }
                    ConnectionManager::SendEvent(event);
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
    sendPermissionStatusToPeer(PermissionType::Microphone, m_microphonePermissionGranted);
    sendPermissionStatusToPeer(PermissionType::Notifications, notificationsPermissionGranted());
    sendPermissionStatusToPeer(PermissionType::FileSystem, m_filePermissionGranted && m_allFilesPermissionGranted);
    sendPermissionStatusToPeer(PermissionType::Battery, m_batteryPermissionGranted);
    sendPermissionStatusToPeer(PermissionType::Sms, smsPermissionsGranted());
}

void MobileConnectionController::setPermissionSnapshot(
    const bool cameraGranted,
    const bool microphoneGranted,
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
        m_microphonePermissionGranted != microphoneGranted ||
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
    m_microphonePermissionGranted = microphoneGranted;
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

bool MobileConnectionController::smsPermissionsGranted() const
{
    return m_smsReceivePermissionGranted &&
        m_smsReadPermissionGranted &&
        m_smsSendPermissionGranted &&
        m_contactsPermissionGranted;
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
            clearApproval();
            updatePermissionsFromSystem();
            sendPermissionSnapshotToPeer();
            refreshDefaultDownloadPath();
            auto& systemInfoModule = ModulesManager::GetModuleReference<SystemInfoShareModule>();
            systemInfoModule->Enable(true);
            QTimer::singleShot(750, this, [this]() {
                if (m_connected) {
                    sendPermissionSnapshotToPeer();
                }
            });
        } else {
            stopFindMyPhoneAlertInternal(false);
            m_connectedPeerDeviceId.clear();
            setBatteryPercentage(-1);
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
        clearApproval();
        m_connectedPeerDeviceId.clear();
        stopFindMyPhoneAlertInternal(false);
        setBatteryPercentage(-1);
        auto& systemInfoModule = ModulesManager::GetModuleReference<SystemInfoShareModule>();
        systemInfoModule->Disable(true);
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

    if (type == ConnectionApprovalRequestedEvent::Type) {
        auto* ev = static_cast<ConnectionApprovalRequestedEvent*>(e);
        const DeviceInfo info = ev->GetDeviceInfo();
        const QString deviceName = QString::fromStdString(info.deviceName);

        clearChallenge();

        if (!deviceName.isEmpty() && m_pendingDeviceName != deviceName) {
            m_pendingDeviceName = deviceName;
            emit pendingDeviceNameChanged();
        }

        m_approvalEvent.reset(ev->clone());
        if (!m_approvalVisible) {
            m_approvalVisible = true;
            emit approvalVisibleChanged();
        }
        return true;
    }

    if (type == ConnectionApprovalDeniedEvent::Type) {
        setError(QStringLiteral("Connection denied on the remote device."));
        clearApproval();
        return true;
    }

    if (type == DeviceNotPairedEvent::Type) {
        const auto* ev = static_cast<DeviceNotPairedEvent*>(e);
        const QString deviceId = QString::fromStdString(ev->GetDeviceID());
        if (!deviceId.isEmpty()) {
            removePairedDevice(deviceId);
        } else {
            refreshPairedDevices();
        }
        setError(QStringLiteral("This device is no longer paired with the remote device. Pair the devices again and retry."));
        return true;
    }

    if (type == DeviceCooldownEvent::Type) {
        const auto* ev = static_cast<DeviceCooldownEvent*>(e);
        setError(
            QStringLiteral("Connection temporarily blocked by the remote device. Try again in about %1 seconds.")
                .arg(ev->LeftDuration(), 0, 'f', 1)
        );
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

    if (type == FindMyPhoneAlertStateEvent::Type) {
        const auto* alertStateEvent = static_cast<FindMyPhoneAlertStateEvent*>(e);
        m_settings.setValue(QString::fromLatin1(kFindMyPhoneAlertActiveSetting), alertStateEvent->IsActive());
        setFindMyPhoneAlertActive(alertStateEvent->IsActive());
        return true;
    }

    if (type == PeerBatteryLevelUpdateEvent::Type) {
        const auto* batteryEvent = static_cast<PeerBatteryLevelUpdateEvent*>(e);
        const int percentage = batteryEvent->GetBatteryLevel() < 0
            ? -1
            : std::clamp(static_cast<int>(std::lround(batteryEvent->GetBatteryLevel())), 0, 100);
        setBatteryPercentage(percentage);
        return true;
    }

    return QObject::event(e);
}
