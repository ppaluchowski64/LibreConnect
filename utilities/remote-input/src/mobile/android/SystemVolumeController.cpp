#include "SystemVolumeController.h"

#include <QtCore/QJniObject>
#include <QtCore/QCoreApplication>

int SystemVolumeController::GetVolume() {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (!context.isValid())
        return 0;

    return QJniObject::callStaticMethod<jint>(
        "com/LibreConnect/mobile/MediaRemoteBridge",
        "getVolume",
        "(Landroid/content/Context;)I",
        context.object()
    );
}

void SystemVolumeController::SetVolume(int percentage) {
    if (percentage < 0)
        percentage = 0;

    if (percentage > 100)
        percentage = 100;

    const QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (!context.isValid())
        return;

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MediaRemoteBridge",
        "setVolume",
        "(Landroid/content/Context;I)V",
        context.object(),
        static_cast<jint>(percentage)
    );
}
