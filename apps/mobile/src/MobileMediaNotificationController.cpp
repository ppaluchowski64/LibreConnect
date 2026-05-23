#include "MobileMediaNotificationController.h"

#include <RemoteInputModule.h>
#include <DebugLog.h>

MobileMediaNotificationController::MobileMediaNotificationController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"))
{
    m_enabled = m_settings.value(QStringLiteral("mediaNotification/enabled"), true).toBool();
    Debug::Log("Mobile MobileMediaNotificationController created. m_enabled={}", m_enabled);
    applyState();
}

MobileMediaNotificationController::~MobileMediaNotificationController() = default;

void MobileMediaNotificationController::setEnabled(bool enabled)
{
    Debug::Log("Mobile MobileMediaNotificationController::setEnabled({}) called", enabled);
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    m_settings.setValue(QStringLiteral("mediaNotification/enabled"), m_enabled);
    emit enabledChanged();
    applyState();
}

void MobileMediaNotificationController::applyState()
{
    Debug::Log("Mobile MobileMediaNotificationController::applyState(): setting mirroring enabled on RemoteInputModule to {}", m_enabled);
    RemoteInputModule::SetMirroringEnabled(m_enabled);
}
