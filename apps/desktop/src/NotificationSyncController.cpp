#include "NotificationSyncController.h"

#include <ModulesManager.h>

NotificationSyncController::NotificationSyncController(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(400);
    connect(&m_pollTimer, &QTimer::timeout, this, &NotificationSyncController::refreshState);
    m_pollTimer.start();

    refreshState();
}

void NotificationSyncController::setNotificationSyncEnabled(const bool enabled)
{
    auto& module = ModulesManager::GetModuleReference<NotificationSyncModule>();
    m_requestedEnabled = enabled;

    if (enabled) {
        setEnabledState(true);
        setBusy(true);
        setStatusMessage(QStringLiteral("Starting notification sync..."));
        module->Enable(true);
    } else {
        setEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping notification sync..."));
        module->Disable(true);
    }
}

void NotificationSyncController::refreshState()
{
    const auto& module = ModulesManager::GetModuleReference<NotificationSyncModule>();
    const ModuleState state = module->GetModuleState();

    if (state == ModuleState::Enabled) {
        setEnabledState(true);
        setBusy(false);
        setStatusMessage(QStringLiteral("Notification sync is enabled on the connected devices."));
    } else if (state == ModuleState::Enabling) {
        setEnabledState(true);
        setBusy(true);
        setStatusMessage(QStringLiteral("Starting notification sync..."));
    } else if (state == ModuleState::Disabling) {
        setEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping notification sync..."));
    } else if (state == ModuleState::Disabled) {
        setEnabledState(false);
        setBusy(false);
        setStatusMessage(QStringLiteral("Notification sync is disabled."));
    }
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
