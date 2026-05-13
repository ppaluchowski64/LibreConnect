#include "NotificationSyncController.h"

#include <NotificationSyncModule.h>
#include <ModulesManager.h>
#include <ConnectionManager.h>
#include <Events.h>
#include <NotificationEmitter.h>
#include <QPointer>
#include <QUrl>
#include <QDateTime>
#include <algorithm>

NotificationSyncController::NotificationSyncController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnect"))
{
    m_requestedEnabled = m_settings.value(QStringLiteral("notificationSync/enabled"), false).toBool();
    m_enableAttemptPending = m_requestedEnabled;
    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    m_pollTimer.setInterval(400);
    connect(&m_pollTimer, &QTimer::timeout, this, &NotificationSyncController::refreshState);
    m_pollTimer.start();

    refreshLocalPermissionState(false, true);
    refreshState();
}

QString NotificationSyncController::permissionMessage() const
{
    if (!m_permissionsGranted && !m_localPermissionGranted) {
        return QStringLiteral(
            "Notification sync needs notification permissions on the mobile device and permission to show notifications on this Mac."
        );
    }

    if (!m_permissionsGranted) {
        return QStringLiteral("Notification sync is unavailable until notification permissions are granted on the mobile device.");
    }

    if (!m_localPermissionGranted) {
        return QStringLiteral(
            "Notification sync is unavailable until LibreConnect is allowed to show notifications on this Mac. "
            "Allow it in System Settings > Notifications."
        );
    }

    return QString();
}

QVariantList NotificationSyncController::notifications() const
{
    QVariantList result;
    result.reserve(m_notifications.size());

    for (const NotificationItem& notification : m_notifications) {
        QVariantMap map;
        map.insert(QStringLiteral("key"), notification.key);
        map.insert(QStringLiteral("appName"), notification.appName);
        map.insert(QStringLiteral("title"), notification.title);
        map.insert(QStringLiteral("content"), notification.content);
        map.insert(QStringLiteral("timestamp"), notification.timestamp);
        map.insert(QStringLiteral("timestampText"),
            notification.timestamp > 0
                ? QDateTime::fromMSecsSinceEpoch(notification.timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QStringLiteral("Unknown time"));
        map.insert(QStringLiteral("dismissable"), notification.dismissable);
        map.insert(QStringLiteral("icon"), notification.iconPath);
        result.push_back(map);
    }

    return result;
}

void NotificationSyncController::setNotificationSyncEnabled(const bool enabled)
{
    setRequestedEnabled(enabled, true);
    m_enableAttemptPending = enabled;
    m_disableAttemptPending = !enabled;
    refreshState();
}

bool NotificationSyncController::dismissNotification(const QString& key)
{
    const QString normalized = key.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    for (const NotificationItem& item : m_notifications) {
        if (item.key == normalized) {
            if (!item.dismissable) {
                return false;
            }
            break;
        }
    }

    auto& module = ModulesManager::GetModuleReference<NotificationSyncModule>();
    return module->DismissNotificationByKey(normalized.toStdString());
}

void NotificationSyncController::refreshState()
{
    refreshLocalPermissionState(m_connected);

    auto& module = ModulesManager::GetModuleReference<NotificationSyncModule>();
    const ModuleState state = module->GetModuleState();

    if (!m_connected) {
        setEnabledState(m_requestedEnabled);
        setBusy(false);
        clearNotifications();
        setStatusMessage(m_requestedEnabled
            ? QStringLiteral("Notification sync will start after connecting to the device.")
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
        if (!m_permissionsGranted || !m_localPermissionGranted) {
            m_enableAttemptPending = false;
            m_disableAttemptPending = false;
            if (m_requestedEnabled) {
                setRequestedEnabled(false, true);
            }

            setEnabledState(false);
            setBusy(true);
            setStatusMessage(permissionMessage());
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
        setStatusMessage(QStringLiteral("Notification sync is enabled on the connected device."));
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
                setStatusMessage(QStringLiteral("Notification sync is waiting for notification permissions on the connected device."));
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
        if (!m_requestedEnabled) {
            clearNotifications();
        }
        setStatusMessage(!m_permissionsGranted || !m_localPermissionGranted
            ? permissionMessage()
            : QStringLiteral("Notification sync is disabled."));
        return;
    }

    setEnabledState(m_requestedEnabled);
    setBusy(false);
    setStatusMessage(!m_permissionsGranted || !m_localPermissionGranted
        ? permissionMessage()
        : QStringLiteral("Notification sync is disabled."));
}

bool NotificationSyncController::event(QEvent* event)
{
    if (event->type() == ConnectedEvent::Type) {
        const auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        m_connected = connectedEvent->GetResult() == EventResult::SUCCESS;
        m_permissionsGranted = false;
        m_lastReportedLocalPermissionState.reset();
        refreshLocalPermissionState(m_connected, true);
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
        m_permissionsGranted = false;
        m_lastReportedLocalPermissionState.reset();
        clearNotifications();
        refreshState();
        return true;
    }

    if (event->type() == NotificationReceivedEvent::Type) {
        if (!m_connected) {
            return true;
        }

        const auto* notificationEvent = static_cast<NotificationReceivedEvent*>(event);
        const NotificationRecord& record = notificationEvent->GetNotification();

        NotificationItem item;
        item.key = QString::fromStdString(record.key);
        item.appName = QString::fromStdString(record.appName);
        item.title = QString::fromStdString(record.title);
        item.content = QString::fromStdString(record.content);
        item.timestamp = static_cast<qint64>(record.timestamp);
        item.dismissable = record.dismissable;
        if (record.iconPath.has_value()) {
            item.iconPath = QUrl::fromLocalFile(QString::fromStdString(record.iconPath->string())).toString();
        }

        upsertNotification(item);
        return true;
    }

    if (event->type() == NotificationRemovedEvent::Type) {
        if (!m_connected) {
            return true;
        }

        const auto* notificationEvent = static_cast<NotificationRemovedEvent*>(event);
        removeNotificationByKey(QString::fromStdString(notificationEvent->GetKey()));
        return true;
    }

    if (event->type() == ModuleRequestedPermissionGranted::Type) {
        const auto* grantedEvent = static_cast<ModuleRequestedPermissionGranted*>(event);
        if (grantedEvent->GetPermissionType() == PermissionType::Notifications) {
            if (m_permissionsGranted != true) {
                m_permissionsGranted = true;
                emit permissionStateChanged();
            }
            if (m_connected && m_requestedEnabled) {
                m_enableAttemptPending = true;
                refreshState();
            }
        }
        return true;
    }

    if (event->type() == ModuleRequestedPermissionRejected::Type) {
        const auto* rejectedEvent = static_cast<ModuleRequestedPermissionRejected*>(event);
        if (rejectedEvent->GetPermissionType() == PermissionType::Notifications) {
            if (m_permissionsGranted != false) {
                m_permissionsGranted = false;
                emit permissionStateChanged();
            }
            if (m_connected && m_requestedEnabled) {
                setRequestedEnabled(false, true);
                m_enableAttemptPending = false;
                m_disableAttemptPending = false;
                refreshState();
            }
        }
        return true;
    }

    return QObject::event(event);
}

void NotificationSyncController::upsertNotification(const NotificationItem& item)
{
    auto it = std::find_if(m_notifications.begin(), m_notifications.end(), [&item](const NotificationItem& existing) {
        return existing.key == item.key;
    });

    bool changed = false;
    if (it != m_notifications.end()) {
        if (it->appName != item.appName ||
            it->title != item.title ||
            it->content != item.content ||
            it->timestamp != item.timestamp ||
            it->dismissable != item.dismissable ||
            it->iconPath != item.iconPath) {
            *it = item;
            changed = true;
        }
    } else {
        m_notifications.push_back(item);
        changed = true;
    }

    if (changed) {
        std::sort(m_notifications.begin(), m_notifications.end(), [](const NotificationItem& lhs, const NotificationItem& rhs) {
            return lhs.timestamp > rhs.timestamp;
        });
        emit notificationsChanged();
    }
}

void NotificationSyncController::removeNotificationByKey(const QString& key)
{
    const auto newEnd = std::remove_if(m_notifications.begin(), m_notifications.end(), [&key](const NotificationItem& item) {
        return item.key == key;
    });

    if (newEnd == m_notifications.end()) {
        return;
    }

    m_notifications.erase(newEnd, m_notifications.end());
    emit notificationsChanged();
}

void NotificationSyncController::clearNotifications()
{
    if (m_notifications.isEmpty()) {
        return;
    }

    m_notifications.clear();
    emit notificationsChanged();
}

void NotificationSyncController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void NotificationSyncController::setEnabledState(const bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    emit enabledChanged();
}

void NotificationSyncController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

void NotificationSyncController::setRequestedEnabled(const bool enabled, const bool persist)
{
    if (m_requestedEnabled != enabled) {
        m_requestedEnabled = enabled;
    }

    if (persist) {
        m_settings.setValue(QStringLiteral("notificationSync/enabled"), enabled);
    }
}

void NotificationSyncController::refreshLocalPermissionState(const bool reportToPeer, const bool forceReport)
{
#ifdef MACOS_DEVICE
    const bool granted = NotificationEmitter::IsPermissionGranted();
#else
    const bool granted = true;
#endif

    if (m_localPermissionGranted != granted) {
        m_localPermissionGranted = granted;
        emit permissionStateChanged();
    }

    if (reportToPeer && m_connected) {
        reportLocalPermissionStateToPeer(forceReport);
    }
}

void NotificationSyncController::reportLocalPermissionStateToPeer(const bool forceReport)
{
    if (!m_connected) {
        return;
    }

    if (!forceReport &&
        m_lastReportedLocalPermissionState.has_value() &&
        m_lastReportedLocalPermissionState.value() == m_localPermissionGranted) {
        return;
    }

    ConnectionManager::Send(
        m_localPermissionGranted ? PC_PackageType::PERMISSION_GRANTED : PC_PackageType::PERMISSION_REJECTED,
        PermissionType::DesktopNotifications
    );
    m_lastReportedLocalPermissionState = m_localPermissionGranted;
}
