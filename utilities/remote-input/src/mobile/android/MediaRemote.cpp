#include "MediaRemote.h"

#include <QtCore/QJniObject>
#include <QtCore/QCoreApplication>

namespace {
    enum class AndroidKeyCode {
        MEDIA_PLAY_PAUSE = 85,
        MEDIA_NEXT = 87,
        MEDIA_PREVIOUS = 88,
        VOLUME_UP = 24,
        VOLUME_DOWN = 25,
        VOLUME_MUTE = 164
    };

    int GetAndroidKeyCode(MediaSignal signal) {
        switch (signal) {
            case MediaSignal::PlayPause:
                return static_cast<int>(AndroidKeyCode::MEDIA_PLAY_PAUSE);
            case MediaSignal::NextTrack:
                return static_cast<int>(AndroidKeyCode::MEDIA_NEXT);
            case MediaSignal::PreviousTrack:
                return static_cast<int>(AndroidKeyCode::MEDIA_PREVIOUS);
            case MediaSignal::VolumeUp:
                return static_cast<int>(AndroidKeyCode::VOLUME_UP);
            case MediaSignal::VolumeDown:
                return static_cast<int>(AndroidKeyCode::VOLUME_DOWN);
            case MediaSignal::VolumeMute:
                return static_cast<int>(AndroidKeyCode::VOLUME_MUTE);
            default:
                return -1;
        }
    }
}

MediaRemote::MediaRemote() {}
MediaRemote::~MediaRemote() = default;

void MediaRemote::PlayPause() {
    ExecuteSignal(MediaSignal::PlayPause);
}

void MediaRemote::NextTrack() {
    ExecuteSignal(MediaSignal::NextTrack);
}

void MediaRemote::PreviousTrack() {
    ExecuteSignal(MediaSignal::PreviousTrack);
}

void MediaRemote::VolumeUp() {
    ExecuteSignal(MediaSignal::VolumeUp);
}

void MediaRemote::VolumeDown() {
    ExecuteSignal(MediaSignal::VolumeDown);
}

void MediaRemote::VolumeMute() {
    ExecuteSignal(MediaSignal::VolumeMute);
}

void MediaRemote::ExecuteSignal(MediaSignal signal) {
    int androidKeyCode = GetAndroidKeyCode(signal);

    if (androidKeyCode == -1)
        return;

    const QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (!context.isValid())
        return;

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MediaRemoteBridge",
        "sendMediaKey",
        "(Landroid/content/Context;I)V",
        context.object(),
        static_cast<jint>(androidKeyCode)
    );
}
