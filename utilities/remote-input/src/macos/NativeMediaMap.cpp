#include "NativeMediaMap.h"

#include "InputTypes.h"

#include <IOKit/hidsystem/ev_keymap.h>

int GetNativeMediaCode(MediaSignal signal) {
    switch(signal) {
        case MediaSignal::PlayPause: return NX_KEYTYPE_PLAY;
        case MediaSignal::NextTrack: return NX_KEYTYPE_NEXT;
        case MediaSignal::PreviousTrack: return NX_KEYTYPE_PREVIOUS;
        case MediaSignal::VolumeUp: return NX_KEYTYPE_SOUND_UP;
        case MediaSignal::VolumeDown: return NX_KEYTYPE_SOUND_DOWN;
        case MediaSignal::VolumeMute: return NX_KEYTYPE_MUTE;
        default: return -1;
    }
}
