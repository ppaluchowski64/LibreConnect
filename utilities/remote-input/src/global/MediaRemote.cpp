#include "MediaRemote.h"

int GetNativeMediaSignal(MediaSignal signal);

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
    int nativeKeyCode = GetNativeMediaSignal(signal);
    if (nativeKeyCode == -1) return;
    EmitNativeKeyPress(nativeKeyCode);
    EmitNativeKeyRelease(nativeKeyCode);
}
