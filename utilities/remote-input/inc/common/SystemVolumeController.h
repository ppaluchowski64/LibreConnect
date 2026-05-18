#ifndef SYSTEM_VOLUME_CONTROLLER_H
#define SYSTEM_VOLUME_CONTROLLER_H

class SystemVolumeController {
    public:
        static int GetVolume();
        static void SetVolume(int percentage);
};

#endif // SYSTEM_VOLUME_CONTROLLER_H
