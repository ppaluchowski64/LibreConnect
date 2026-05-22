#include "ClipboardSyncController.h"

#include <ModulesManager.h>
#include <ConnectionManager.h>
#include <Events.h>
#include <QPointer>

ClipboardSyncController::ClipboardSyncController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnect"))
{
    m_requestedAutoSync = m_settings.value(QStringLiteral("clipboardSync/autoSync"), false).toBool();
    m_autoSyncEnabled = m_requestedAutoSync;
    m_enableAttemptPending = m_requestedAutoSync;

    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    m_pollTimer.setInterval(400);
    connect(&m_pollTimer, &QTimer::timeout, this, &ClipboardSyncController::refreshState);
    m_pollTimer.start();

    refreshState();
}

void ClipboardSyncController::setClipboardAutoSyncEnabled(const bool enabled)
{
    setRequestedAutoSync(enabled, true);
    m_enableAttemptPending = enabled;
    m_disableAttemptPending = !enabled;
    refreshState();
}

void ClipboardSyncController::syncClipboard()
{
    if (!m_connected) {
        setStatusMessage(QStringLiteral("Connect to a device to sync clipboard."));
        return;
    }

    auto& module = ModulesManager::GetModuleReference<ClipboardSyncModule>();
    module->RequestSyncWithPeer();
    setStatusMessage(QStringLiteral("Sync request sent. Clipboard will update when the peer responds."));
}

void ClipboardSyncController::refreshState()
{
    auto& module = ModulesManager::GetModuleReference<ClipboardSyncModule>();
    const ModuleState state = module->GetModuleState();

    if (!m_connected) {
        m_confirmedEnabled = false;
        setAutoSyncEnabledState(m_requestedAutoSync);
        setBusy(false);
        setStatusMessage(m_requestedAutoSync
            ? QStringLiteral("Clipboard auto sync will start after connecting to the device.")
            : QStringLiteral("Clipboard auto sync is disabled. Use Sync Clipboard to sync manually."));
        return;
    }

    if (state == ModuleState::Enabling) {
        setAutoSyncEnabledState(true);
        setBusy(true);
        setStatusMessage(QStringLiteral("Starting clipboard auto sync..."));
        return;
    }

    if (state == ModuleState::Disabling) {
        setAutoSyncEnabledState(false);
        setBusy(true);
        setStatusMessage(QStringLiteral("Stopping clipboard auto sync..."));
        return;
    }

    if (state == ModuleState::Enabled) {
        m_confirmedEnabled = true;
        m_enableAttemptPending = false;
        if (!m_requestedAutoSync) {
            setRequestedAutoSync(true, true);
        }

        if (m_disableAttemptPending || !m_requestedAutoSync) {
            setAutoSyncEnabledState(false);
            setBusy(true);
            setStatusMessage(QStringLiteral("Stopping clipboard auto sync..."));
            m_disableAttemptPending = false;
            module->Disable(true);
            return;
        }

        setAutoSyncEnabledState(true);
        setBusy(false);
        setStatusMessage(QStringLiteral("Clipboard auto sync is enabled on the connected device."));
        return;
    }

    if (state == ModuleState::Disabled) {
        if (m_confirmedEnabled && !m_disableAttemptPending) {
            m_confirmedEnabled = false;
            if (m_requestedAutoSync) {
                setRequestedAutoSync(false, true);
            }
            m_enableAttemptPending = false;
        }

        if (m_disableAttemptPending) {
            m_confirmedEnabled = false;
            m_disableAttemptPending = false;
            if (m_requestedAutoSync) {
                setRequestedAutoSync(false, true);
            }
        }

        if (m_requestedAutoSync && m_enableAttemptPending) {
            setAutoSyncEnabledState(true);
            setBusy(true);
            setStatusMessage(QStringLiteral("Starting clipboard auto sync..."));
            m_enableAttemptPending = false;
            module->Enable(true);
            return;
        }

        setAutoSyncEnabledState(m_requestedAutoSync);
        setBusy(false);
        setStatusMessage(m_requestedAutoSync
            ? QStringLiteral("Clipboard auto sync is waiting for the connected device.")
            : QStringLiteral("Clipboard auto sync is disabled. Use Sync Clipboard to sync manually."));
        return;
    }

    setAutoSyncEnabledState(m_requestedAutoSync);
    setBusy(false);
    setStatusMessage(m_requestedAutoSync
        ? QStringLiteral("Clipboard auto sync is waiting for the connected device.")
        : QStringLiteral("Clipboard auto sync is disabled. Use Sync Clipboard to sync manually."));
}

bool ClipboardSyncController::event(QEvent* event)
{
    if (event->type() == ConnectedEvent::Type) {
        const auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        m_connected = connectedEvent->GetResult() == EventResult::SUCCESS;
        if (m_connected && m_requestedAutoSync) {
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
        refreshState();
        return true;
    }

    return QObject::event(event);
}

void ClipboardSyncController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void ClipboardSyncController::setAutoSyncEnabledState(const bool enabled)
{
    if (m_autoSyncEnabled == enabled) {
        return;
    }

    m_autoSyncEnabled = enabled;
    emit autoSyncEnabledChanged();
}

void ClipboardSyncController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

void ClipboardSyncController::setRequestedAutoSync(const bool enabled, const bool persist)
{
    if (m_requestedAutoSync != enabled) {
        m_requestedAutoSync = enabled;
    }

    if (persist) {
        m_settings.setValue(QStringLiteral("clipboardSync/autoSync"), enabled);
    }
}
