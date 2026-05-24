#include "MediaRemote.h"
#include "NativeMediaMap.h"

#ifdef __APPLE__
    void EmitMacMediaSignal(int macCode);
#endif

MediaRemote::MediaRemote() : VirtualInputDevice("libreconnect-media-remote") {}
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
    if (MediaTrackInfo::ControlPlayback(signal)) {
        return;
    }

    int nativeCode = GetNativeMediaCode(signal);
    if (nativeCode == -1) return;

    #ifdef __APPLE__
        EmitMacMediaSignal(nativeCode);
    #else
        EmitNativeKeyPress(nativeCode);
        EmitNativeKeyRelease(nativeCode);
    #endif
}
