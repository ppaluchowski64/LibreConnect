#include "MobileNotificationSyncController.h"

#include <QPointer>

#include <ConnectionManager.h>
#include <Events.h>
#include <ModulesManager.h>
#ifdef ANDROID_DEVICE
#include <PermissionManager.h>
#endif

MobileNotificationSyncController::MobileNotificationSyncController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"))
{
    m_requestedEnabled = m_settings.value(QStringLiteral("notificationSync/enabled"), false).toBool();
    m_enableAttemptPending = m_requestedEnabled;
#ifdef ANDROID_DEVICE
    m_permissionsGranted = PermissionManager::IsNotificationEmitPermissionGranted()
        && PermissionManager::IsNotificationAccessPermissionGranted();
#else
    m_permissionsGranted = true;
#endif
    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    m_pollTimer.setInterval(400);
    connect(&m_pollTimer, &QTimer::timeout, this, &MobileNotificationSyncController::refreshState);
    m_pollTimer.start();

    refreshState();
}

void MobileNotificationSyncController::setNotificationSyncEnabled(const bool enabled)
{
    setRequestedEnabled(enabled, true);
    m_enableAttemptPending = enabled;
    m_disableAttemptPending = !enabled;
    refreshState();
}

bool MobileNotificationSyncController::event(QEvent* event)
{
    if (event->type() == ConnectedEvent::Type) {
        const auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        m_connected = connectedEvent->GetResult() == EventResult::SUCCESS;
        if (m_desktopPermissionGranted != true) {
            m_desktopPermissionGranted = true;
            emit permissionStateChanged();
        }
#ifdef ANDROID_DEVICE
        m_permissionsGranted = PermissionManager::IsNotificationEmitPermissionGranted()
            && PermissionManager::IsNotificationAccessPermissionGranted();
#else
        m_permissionsGranted = true;
#endif
        if (m_connected && m_requestedEnabled) {
            m_enableAttemptPending = true;
        }
        if (!m_connected) {
            m_disableAttemptPending = false;
        }
        refreshState();
        return true;
    }

    if (event->type() == DisconnectedEvent::Type) {
        m_connected = false;
        if (m_desktopPermissionGranted != true) {
            m_desktopPermissionGranted = true;
            emit permissionStateChanged();
        }
#ifdef ANDROID_DEVICE
        m_permissionsGranted = PermissionManager::IsNotificationEmitPermissionGranted()
            && PermissionManager::IsNotificationAccessPermissionGranted();
#else
        m_permissionsGranted = true;
#endif
        refreshState();
        return true;
    }

    if (event->type() == ModuleRequestedPermissionGranted::Type) {
        const auto* grantedEvent = static_cast<ModuleRequestedPermissionGranted*>(event);
        if (grantedEvent->GetPermissionType() == PermissionType::Notifications) {
            m_permissionsGranted = true;
            if (m_connected && m_requestedEnabled) {
                m_enableAttemptPending = true;
                refreshState();
            }
        } else if (grantedEvent->GetPermissionType() == PermissionType::DesktopNotifications) {
            if (m_desktopPermissionGranted != true) {
                m_desktopPermissionGranted = true;
                emit permissionStateChanged();
            }
            refreshState();
        }
        return true;
    }

    if (event->type() == ModuleRequestedPermissionRejected::Type) {
        const auto* rejectedEvent = static_cast<ModuleRequestedPermissionRejected*>(event);
        if (rejectedEvent->GetPermissionType() == PermissionType::Notifications) {
            m_permissionsGranted = false;
            if (m_connected && m_requestedEnabled) {
                setRequestedEnabled(false, true);
                m_enableAttemptPending = false;
                m_disableAttemptPending = false;
                refreshState();
            }
        } else if (rejectedEvent->GetPermissionType() == PermissionType::DesktopNotifications) {
            if (m_desktopPermissionGranted != false) {
                m_desktopPermissionGranted = false;
                emit permissionStateChanged();
            }
            refreshState();
        }
        return true;
    }

    return QObject::event(event);
}

void MobileNotificationSyncController::refreshState()
{
    auto& module = ModulesManager::GetModuleReference<NotificationSyncModule>();
    const ModuleState state = module->GetModuleState();

    if (!m_connected) {
        setEnabledState(m_requestedEnabled);
        setBusy(false);
        setStatusMessage(m_requestedEnabled
            ? QStringLiteral("Notification sync will start after connecting to the desktop device.")
            : QStringLiteral("Notification sync is disabled."));
        return;
    }

    if (state == ModuleState::Enabling) {
        setEnabledState(true);
        setBusy(true);
        setStatusMessage(QStringLiteral("Starting notification sync..."));
        return;
    }

    if (state == ModuleState::Disabling) {
        setEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping notification sync..."));
        return;
    }

    if (state == ModuleState::Enabled) {
        if (!m_permissionsGranted || !m_desktopPermissionGranted) {
            m_enableAttemptPending = false;
            m_disableAttemptPending = false;
            if (m_requestedEnabled) {
                setRequestedEnabled(false, true);
            }

            setEnabledState(false);
            setBusy(true);
            setStatusMessage(!m_permissionsGranted
                ? QStringLiteral("Notification sync is waiting for notification permissions on this device.")
                : QStringLiteral("Notification sync is waiting for the connected desktop device to allow LibreConnect notifications."));
            module->Disable(true);
            return;
        }

        m_enableAttemptPending = false;
        if (!m_requestedEnabled) {
            setRequestedEnabled(true, true);
        }

        if (m_disableAttemptPending) {
            setEnabledState(false);
            setBusy(true);
            setStatusMessage(QStringLiteral("Stopping notification sync..."));
            module->Disable(true);
            return;
        }

        setEnabledState(true);
        setBusy(false);
        setStatusMessage(QStringLiteral("Notification sync is enabled for the connected desktop device."));
        return;
    }

    if (state == ModuleState::Disabled) {
        if (m_requestedEnabled && !m_enableAttemptPending && !m_disableAttemptPending) {
            setRequestedEnabled(false, true);
        }

        if (m_disableAttemptPending) {
            m_disableAttemptPending = false;
            if (m_requestedEnabled) {
                setRequestedEnabled(false, true);
            }
        }

        if (m_requestedEnabled && m_enableAttemptPending) {
            if (!m_permissionsGranted) {
                setEnabledState(m_requestedEnabled);
                setBusy(false);
                setStatusMessage(QStringLiteral("Notification sync is waiting for notification permissions on this device."));
                return;
            }

            setEnabledState(true);
            setBusy(true);
            setStatusMessage(QStringLiteral("Starting notification sync..."));
            m_enableAttemptPending = false;
            module->Enable(true);
            return;
        }

        setEnabledState(m_requestedEnabled);
        setBusy(false);
        setStatusMessage(!m_permissionsGranted
            ? QStringLiteral("Notification sync is waiting for notification permissions on this device.")
            : (!m_desktopPermissionGranted
                ? QStringLiteral(
                    "Notification sync is waiting for the connected desktop device to allow LibreConnect notifications."
                )
                : QStringLiteral("Notification sync is disabled.")));
        return;
    }

    setEnabledState(m_requestedEnabled);
    setBusy(false);
    setStatusMessage(!m_permissionsGranted
        ? QStringLiteral("Notification sync is waiting for notification permissions on this device.")
        : (!m_desktopPermissionGranted
            ? QStringLiteral(
                "Notification sync is waiting for the connected desktop device to allow LibreConnect notifications."
            )
            : QStringLiteral("Notification sync is disabled.")));
}

void MobileNotificationSyncController::setEnabledState(const bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    emit enabledChanged();
}

void MobileNotificationSyncController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void MobileNotificationSyncController::setStatusMessage(const QString& message)
{
    if (m_statusMessage == message) {
        return;
    }

    m_statusMessage = message;
    emit statusMessageChanged();
}

void MobileNotificationSyncController::setRequestedEnabled(const bool enabled, const bool persist)
{
    if (m_requestedEnabled != enabled) {
        m_requestedEnabled = enabled;
    }

    if (persist) {
        m_settings.setValue(QStringLiteral("notificationSync/enabled"), enabled);
    }
}
