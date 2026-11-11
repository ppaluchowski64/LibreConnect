#include "PermissionStateController.h"

#include <ConnectionManager.h>
#include <Events.h>
#include <ModulesManager.h>
#include <QTimer>
#include <array>
#include <NotificationEmitter.h>

PermissionStateController::PermissionStateController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
    m_connected = ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED;
    if (m_connected) {
        m_deviceName = QString::fromStdString(ConnectionManager::GetPeerDeviceName());
        if (m_deviceName.isEmpty()) {
            m_deviceName = tr("Device");
        }
        ModulesManager::Initialize();
        requestPermissionSyncSnapshot();
    }
}

bool PermissionStateController::isGranted(const int permissionType) const
{
    switch (static_cast<PermissionType>(permissionType)) {
    case PermissionType::Camera:
        return m_cameraGranted;
    case PermissionType::Notifications:
        return m_notificationsGranted;
    case PermissionType::FileSystem:
        return m_fileSystemGranted;
    case PermissionType::Battery:
        return m_batteryGranted;
    case PermissionType::Sms:
        return m_smsGranted;
    case PermissionType::Accessibility:
        return m_accessibilityGranted;
    case PermissionType::DesktopNotifications:
        return m_desktopNotificationsGranted;
    case PermissionType::Microphone:
        return m_microphoneGranted;
    case PermissionType::Unknown:
    default:
        return false;
    }
}

void PermissionStateController::requestPermission(const int permissionType)
{
    if (!m_connected) {
        return;
    }

    const PermissionType type = static_cast<PermissionType>(permissionType);
    if (type == PermissionType::Unknown) {
        return;
    }

    ModulesManager::Initialize();
    m_requestedPermissions.insert(permissionType);
    ConnectionManager::Send(PC_PackageType::PERMISSION_REQUEST, type);
    requestPermissionSyncSnapshot();
}

void PermissionStateController::requestDesktopNotificationPermission()
{
#ifdef MACOS_DEVICE
    const bool granted = NotificationEmitter::RequestPermission();
    setPermissionState(PermissionType::DesktopNotifications, granted);
#else
    requestPermission(static_cast<int>(PermissionType::DesktopNotifications));
#endif
}

bool PermissionStateController::event(QEvent* event)
{
    const auto type = event->type();

    if (type == ConnectedEvent::Type) {
        auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        const bool isConnected = connectedEvent->GetResult() == EventResult::SUCCESS;
        const bool newlyConnected = isConnected && !m_connected;
        if (m_connected != isConnected) {
            m_connected = isConnected;
            emit connectedChanged();
        }

        clearPermissionState();
        if (isConnected) {
            m_deviceName = QString::fromStdString(ConnectionManager::GetPeerDeviceName());
            if (m_deviceName.isEmpty()) {
                m_deviceName = tr("Device");
            }
            if (newlyConnected) {
                notify(tr("Device connected"), tr("%1 is connected.").arg(m_deviceName));
            }
            ModulesManager::Initialize();
            requestPermissionSyncSnapshot();
        } else {
            ++m_syncRequestGeneration;
        }
        return true;
    }

    if (type == DisconnectedEvent::Type) {
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
            notify(tr("Device disconnected"), tr("%1 disconnected.").arg(m_deviceName));
        }

        clearPermissionState();
        ++m_syncRequestGeneration;
        return true;
    }

    if (type == ModuleRequestedPermissionGranted::Type) {
        auto* grantedEvent = static_cast<ModuleRequestedPermissionGranted*>(event);
        setPermissionState(grantedEvent->GetPermissionType(), true);
        return true;
    }

    if (type == ModuleRequestedPermissionRejected::Type) {
        auto* rejectedEvent = static_cast<ModuleRequestedPermissionRejected*>(event);
        setPermissionState(rejectedEvent->GetPermissionType(), false);
        return true;
    }

    return QObject::event(event);
}

void PermissionStateController::clearPermissionState()
{
    const bool changed = m_cameraGranted || m_notificationsGranted || m_fileSystemGranted || m_batteryGranted ||
                         m_smsGranted || m_accessibilityGranted || m_desktopNotificationsGranted ||
                         m_microphoneGranted;
    m_cameraGranted = false;
    m_notificationsGranted = false;
    m_fileSystemGranted = false;
    m_batteryGranted = false;
    m_smsGranted = false;
    m_accessibilityGranted = false;
    m_desktopNotificationsGranted = false;
    m_microphoneGranted = false;
    m_knownPermissions.clear();
    m_requestedPermissions.clear();

    if (changed) {
        emit permissionStateChanged();
    }
}

void PermissionStateController::requestPermissionSyncSnapshot()
{
    if (!m_connected) {
        return;
    }

    const int generation = ++m_syncRequestGeneration;
    constexpr std::array delaysMs{0, 500, 1500, 3000, 6000, 10000, 20000, 40000};
    for (const int delayMs : delaysMs) {
        QTimer::singleShot(delayMs, this, [this, generation] {
            if (generation != m_syncRequestGeneration || !m_connected) {
                return;
            }

            ConnectionManager::Send(PC_PackageType::PERMISSION_SYNC_REQUEST);
        });
    }
}

void PermissionStateController::setPermissionState(const PermissionType permissionType, const bool granted)
{
    const int permissionId = static_cast<int>(permissionType);
    const bool wasKnown = m_knownPermissions.contains(permissionId);
    const bool wasGranted = isGranted(permissionId);
    const bool requested = m_requestedPermissions.contains(permissionId);
    bool changed = false;

    switch (permissionType) {
    case PermissionType::Camera:
        if (m_cameraGranted != granted) {
            m_cameraGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::Notifications:
        if (m_notificationsGranted != granted) {
            m_notificationsGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::FileSystem:
        if (m_fileSystemGranted != granted) {
            m_fileSystemGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::Battery:
        if (m_batteryGranted != granted) {
            m_batteryGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::Sms:
        if (m_smsGranted != granted) {
            m_smsGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::Accessibility:
        if (m_accessibilityGranted != granted) {
            m_accessibilityGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::DesktopNotifications:
        if (m_desktopNotificationsGranted != granted) {
            m_desktopNotificationsGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::Microphone:
        if (m_microphoneGranted != granted) {
            m_microphoneGranted = granted;
            changed = true;
        }
        break;
    case PermissionType::Unknown:
    default:
        break;
    }

    if (changed) {
        emit permissionStateChanged();
    }

    if (permissionType != PermissionType::Unknown) {
        m_knownPermissions.insert(permissionId);
        if (granted && !wasGranted && (wasKnown || requested) && m_connected) {
            const QString name = permissionName(permissionType);
            if (!name.isEmpty()) {
                notify(tr("Permission granted"), tr("%1 granted %2 access.").arg(m_deviceName, name));
            }
        }
        if (granted) {
            m_requestedPermissions.remove(permissionId);
        }
    }
}

QString PermissionStateController::permissionName(const PermissionType permissionType) const
{
    switch (permissionType) {
    case PermissionType::Camera: return tr("camera");
    case PermissionType::Notifications: return tr("notifications");
    case PermissionType::FileSystem: return tr("files");
    case PermissionType::Battery: return tr("battery information");
    case PermissionType::Sms: return tr("messages");
    case PermissionType::Accessibility: return tr("accessibility");
    case PermissionType::Microphone: return tr("microphone");
    default: return {};
    }
}

void PermissionStateController::notify(const QString& title, const QString& message) const
{
    NotificationEmitter::Emit(title.toStdWString(), message.toStdWString(), std::nullopt, std::nullopt, {});
}
