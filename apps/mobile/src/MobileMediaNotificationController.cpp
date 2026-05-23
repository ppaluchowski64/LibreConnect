#include "MobileMediaNotificationController.h"

#include <RemoteInputModule.h>
#include <DebugLog.h>

#include <QTimer>

#ifdef ANDROID_DEVICE
#include <jni.h>
#endif

namespace {
    MobileMediaNotificationController* g_instance = nullptr;
    bool g_pendingNavigation = false;
}

MobileMediaNotificationController::MobileMediaNotificationController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"))
{
    g_instance = this;
    m_enabled = m_settings.value(QStringLiteral("mediaNotification/enabled"), true).toBool();
    Debug::Log("Mobile MobileMediaNotificationController created. m_enabled={}", m_enabled);
    applyState();

    if (g_pendingNavigation) {
        g_pendingNavigation = false;
        QTimer::singleShot(0, this, &MobileMediaNotificationController::navigateToMediaRemote);
    }
}

MobileMediaNotificationController::~MobileMediaNotificationController()
{
    if (g_instance == this)
        g_instance = nullptr;
}

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

void MobileMediaNotificationController::triggerNavigation()
{
    if (g_instance) {
        emit g_instance->navigateToMediaRemote();
    } else {
        Debug::Log("MobileMediaNotificationController::triggerNavigation() called, buffering request");
        g_pendingNavigation = true;
    }
}

void MobileMediaNotificationController::applyState()
{
    Debug::Log("Mobile MobileMediaNotificationController::applyState(): setting mirroring enabled on RemoteInputModule to {}", m_enabled);
    RemoteInputModule::SetMirroringEnabled(m_enabled);
}

#ifdef ANDROID_DEVICE
extern "C" JNIEXPORT void JNICALL
Java_org_qtproject_qt_android_bindings_QtActivity_nativeNavigateToMediaRemote(JNIEnv* /*env*/, jclass /*clazz*/) {
    MobileMediaNotificationController::triggerNavigation();
}
#endif
