#ifndef MEDIA_REMOTE_H
#define MEDIA_REMOTE_H

#include "InputTypes.h"
#include "VirtualInputDevice.h"

class MediaRemote : public VirtualInputDevice {
    public:
        MediaRemote();
        ~MediaRemote() override;

        void PlayPause();
        void NextTrack();
        void PreviousTrack();
        void VolumeUp();
        void VolumeDown();
        void VolumeMute();

        void ExecuteSignal(MediaSignal signal);
};

#endif // MEDIA_REMOTE_H
