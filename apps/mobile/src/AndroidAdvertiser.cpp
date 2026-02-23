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
    releaseMulticastLock();
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
    m_hasLock = false;
#endif
}
