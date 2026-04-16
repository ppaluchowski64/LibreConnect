#include "InputTypes.h"

#include "linux/input-event-codes.h"

int GetNativeMediaSignal(MediaSignal signal) {
    switch(signal) {
        case MediaSignal::PlayPause: return KEY_PLAYPAUSE;
        case MediaSignal::NextTrack: return KEY_NEXTSONG;
        case MediaSignal::PreviousTrack: return KEY_PREVIOUSSONG;
        case MediaSignal::VolumeUp: return KEY_VOLUMEUP;
        case MediaSignal::VolumeDown: return KEY_VOLUMEDOWN;
        case MediaSignal::VolumeMute: return KEY_MUTE;
        default: return -1;
    }
}
