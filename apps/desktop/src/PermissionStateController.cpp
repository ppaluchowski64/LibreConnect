#include "PermissionStateController.h"

#include <ConnectionManager.h>
#include <Events.h>

PermissionStateController::PermissionStateController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
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

    ConnectionManager::Send(PC_PackageType::PERMISSION_REQUEST, type);
}

bool PermissionStateController::event(QEvent* event)
{
    const auto type = event->type();

    if (type == ConnectedEvent::Type) {
        auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        const bool isConnected = connectedEvent->GetResult() == EventResult::SUCCESS;
        if (m_connected != isConnected) {
            m_connected = isConnected;
            emit connectedChanged();
        }

        clearPermissionState();
        return true;
    }

    if (type == DisconnectedEvent::Type) {
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
        }

        clearPermissionState();
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
    const bool changed = m_cameraGranted || m_notificationsGranted || m_fileSystemGranted || m_batteryGranted;
    m_cameraGranted = false;
    m_notificationsGranted = false;
    m_fileSystemGranted = false;
    m_batteryGranted = false;

    if (changed) {
        emit permissionStateChanged();
    }
}

void PermissionStateController::setPermissionState(const PermissionType permissionType, const bool granted)
{
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
    case PermissionType::Unknown:
    default:
        break;
    }

    if (changed) {
        emit permissionStateChanged();
    }
}

