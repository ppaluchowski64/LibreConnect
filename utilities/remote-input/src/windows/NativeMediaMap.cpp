#include "NativeMediaMap.h"

#include "InputTypes.h"

#include "Windows.h"

int GetNativeMediaCode(MediaSignal signal) {
    switch(signal) {
        case MediaSignal::PlayPause: return VK_MEDIA_PLAY_PAUSE;
        case MediaSignal::NextTrack: return VK_MEDIA_NEXT_TRACK;
        case MediaSignal::PreviousTrack: return VK_MEDIA_PREV_TRACK;
        case MediaSignal::VolumeUp: return VK_VOLUME_UP;
        case MediaSignal::VolumeDown: return VK_VOLUME_DOWN;
        case MediaSignal::VolumeMute: return VK_VOLUME_MUTE;
        default: return -1;
    }
}
