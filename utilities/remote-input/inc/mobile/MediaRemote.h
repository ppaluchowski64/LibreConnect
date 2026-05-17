#ifndef MEDIA_REMOTE_H
#define MEDIA_REMOTE_H

#include "InputTypes.h"
#include "MediaTrackInfo.h"
#include "SystemVolumeController.h"

class MediaRemote {
    public:
        MediaRemote();
        ~MediaRemote();

        void PlayPause();
        void NextTrack();
        void PreviousTrack();
        void VolumeUp();
        void VolumeDown();
        void VolumeMute();

        void ExecuteSignal(MediaSignal signal);
};

#endif // MEDIA_REMOTE_H
