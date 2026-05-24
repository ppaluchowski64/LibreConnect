#include "MobileClipboardSyncController.h"

#include <QPointer>

#include <ConnectionManager.h>
#include <Events.h>
#include <ModulesManager.h>
#include <ClipboardSyncModule.h>
#include <TextClipboard.h>

#ifdef ANDROID_DEVICE
#include "BackendBridge.h"
#endif

MobileClipboardSyncController::MobileClipboardSyncController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"))
{
    m_requestedAutoSync = m_settings.value(QStringLiteral("clipboardSync/autoSync"), false).toBool();
    m_autoSyncEnabled = m_requestedAutoSync;
    m_enableAttemptPending = m_requestedAutoSync;
#ifndef ANDROID_DEVICE
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
#endif

#ifdef ANDROID_DEVICE
    const QJsonObject snapshot = BackendBridge::ReadStateSnapshot();
    m_lastAppliedRemoteClipboard = snapshot.value(QStringLiteral("lastRemoteClipboard")).toString();
    m_lastSentLocalClipboard = QString::fromStdString(TextClipboard::Get());
#endif

    m_pollTimer.setInterval(400);
    connect(&m_pollTimer, &QTimer::timeout, this, &MobileClipboardSyncController::refreshState);
    m_pollTimer.start();

    refreshState();
}

void MobileClipboardSyncController::setClipboardAutoSyncEnabled(const bool enabled)
{
    setRequestedAutoSync(enabled, true);
    m_enableAttemptPending = enabled;
    m_disableAttemptPending = !enabled;
#ifdef ANDROID_DEVICE
    BackendBridge::SendAction(BackendBridge::kActionToggleClipboardSync, BackendBridge::kExtraEnabled, enabled);
#endif
    refreshState();
}

void MobileClipboardSyncController::syncClipboard()
{
#ifdef ANDROID_DEVICE
    BackendBridge::SendAction(BackendBridge::kActionSyncClipboard);
    setStatusMessage(QStringLiteral("Sync request sent."));
    return;
#endif

    if (!m_connected) {
        setStatusMessage(QStringLiteral("Connect to a desktop device to sync clipboard."));
        return;
    }

    auto& module = ModulesManager::GetModuleReference<ClipboardSyncModule>();
    module->RequestSyncWithPeer();
    setStatusMessage(QStringLiteral("Sync request sent. Clipboard will update when the desktop responds."));
}

bool MobileClipboardSyncController::event(QEvent* event)
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

void MobileClipboardSyncController::refreshState()
{
#ifdef ANDROID_DEVICE
    const QJsonObject snapshot = BackendBridge::ReadStateSnapshot();
    m_connected = snapshot.value(QStringLiteral("connected")).toBool(false);
    const bool backendEnabled = snapshot.value(QStringLiteral("clipboardSyncEnabled")).toBool(false);
    setAutoSyncEnabledState(backendEnabled);
    setBusy(false);

    if (m_requestedAutoSync != backendEnabled) {
        setRequestedAutoSync(backendEnabled, true);
    }

    if (!m_connected) {
        setStatusMessage(QStringLiteral("Connect to a desktop device to sync clipboard."));
    } else {
        setStatusMessage(backendEnabled
            ? QStringLiteral("Clipboard auto sync is enabled for the connected desktop device.")
            : QStringLiteral("Clipboard auto sync is disabled."));

        if (backendEnabled) {
            const QString remoteClipboard = snapshot.value(QStringLiteral("lastRemoteClipboard")).toString();
            if (!remoteClipboard.isEmpty() && remoteClipboard != m_lastAppliedRemoteClipboard) {
                m_lastAppliedRemoteClipboard = remoteClipboard;
                m_lastSentLocalClipboard = remoteClipboard;
                TextClipboard::Set(remoteClipboard.toStdString());
            }

            const QString localClipboard = QString::fromStdString(TextClipboard::Get());
            if (!localClipboard.isEmpty() &&
                localClipboard != m_lastAppliedRemoteClipboard &&
                localClipboard != m_lastSentLocalClipboard) {
                m_lastSentLocalClipboard = localClipboard;
                BackendBridge::SendAction(
                    BackendBridge::kActionSendLocalClipboard,
                    BackendBridge::kExtraClipboardText,
                    localClipboard
                );
            }
        }
    }
    return;
#endif

    auto& module = ModulesManager::GetModuleReference<ClipboardSyncModule>();
    const ModuleState state = module->GetModuleState();

    if (!m_connected) {
        m_confirmedEnabled = false;
        setAutoSyncEnabledState(m_requestedAutoSync);
        setBusy(false);
        setStatusMessage(m_requestedAutoSync
            ? QStringLiteral("Clipboard auto sync will start after connecting to the desktop device.")
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

        if (m_disableAttemptPending) {
            setAutoSyncEnabledState(false);
            setBusy(true);
            setStatusMessage(QStringLiteral("Stopping clipboard auto sync..."));
            m_disableAttemptPending = false;
            module->Disable(true);
            return;
        }

        setAutoSyncEnabledState(true);
        setBusy(false);
        setStatusMessage(QStringLiteral("Clipboard auto sync is enabled for the connected desktop device."));
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
            ? QStringLiteral("Clipboard auto sync is waiting for the connected desktop device.")
            : QStringLiteral("Clipboard auto sync is disabled. Use Sync Clipboard to sync manually."));
        return;
    }

    setAutoSyncEnabledState(m_requestedAutoSync);
    setBusy(false);
    setStatusMessage(m_requestedAutoSync
        ? QStringLiteral("Clipboard auto sync is waiting for the connected desktop device.")
        : QStringLiteral("Clipboard auto sync is disabled. Use Sync Clipboard to sync manually."));
}

void MobileClipboardSyncController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void MobileClipboardSyncController::setAutoSyncEnabledState(const bool enabled)
{
    if (m_autoSyncEnabled == enabled) {
        return;
    }

    m_autoSyncEnabled = enabled;
    emit autoSyncEnabledChanged();
}

void MobileClipboardSyncController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

void MobileClipboardSyncController::setRequestedAutoSync(const bool enabled, const bool persist)
{
    if (m_requestedAutoSync != enabled) {
        m_requestedAutoSync = enabled;
    }

    if (persist) {
        m_settings.setValue(QStringLiteral("clipboardSync/autoSync"), enabled);
    }
}
