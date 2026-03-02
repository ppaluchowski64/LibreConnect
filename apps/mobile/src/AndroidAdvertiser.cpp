#include "AndroidAdvertiser.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

AndroidAdvertiser::AndroidAdvertiser(QObject* parent)
    : QObject(parent)
{
}

AndroidAdvertiser::~AndroidAdvertiser()
{
    stop();
}

void AndroidAdvertiser::start()
{
    if (m_running)
        return;

    m_running = true;
    emit runningChanged();

    acquireMulticastLock();
    ConnectionManager::StartAcceptingConnections();
    LanDeviceScanner::BeginScan();
}

void AndroidAdvertiser::stop()
{
    if (!m_running)
        return;

    LanDeviceScanner::EndScan();
    ConnectionManager::StopAcceptingConnections();
    releaseMulticastLock();

    m_running = false;
    emit runningChanged();
}

void AndroidAdvertiser::acquireMulticastLock()
{
#ifndef Q_OS_ANDROID
    return;
#else
    if (m_hasLock)
        return;
    QJniObject activity =
        QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative",
            "activity",
            "()Landroid/app/Activity;"
        );

    if (!activity.isValid())
        return;

    QJniObject wifiService = activity.callObjectMethod(
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        QJniObject::fromString("wifi").object<jstring>()
    );

    if (!wifiService.isValid())
        return;

    QJniObject lock = wifiService.callObjectMethod(
        "createMulticastLock",
        "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
        QJniObject::fromString("LibreConnectMulticastLock").object<jstring>()
    );

    if (!lock.isValid())
        return;

    lock.callMethod<void>("setReferenceCounted", "(Z)V", false);
    lock.callMethod<void>("acquire");

    m_multicastLock = lock;
    m_hasLock = true;
#endif
}

void AndroidAdvertiser::releaseMulticastLock()
{
#ifndef Q_OS_ANDROID
    return;
#else
    if (!m_hasLock)
        return;
    if (m_multicastLock.isValid()) {
        m_multicastLock.callMethod<void>("release");
    }
    m_multicastLock = QJniObject();
    m_hasLock = false;
#endif
}
