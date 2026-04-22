#include "SystemVolumeController.h"

#include <CoreAudio/CoreAudio.h>

int SystemVolumeController::GetVolume() {
    AudioDeviceID defaultDevice = 0;
    UInt32 size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress defaultAddr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, nullptr, &size, &defaultDevice);

    Float32 volume = 0.0f;
    size = sizeof(Float32);
    AudioObjectPropertyAddress volumeAddr = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };

    AudioObjectGetPropertyData(defaultDevice, &volumeAddr, 0, nullptr, &size, &volume);

    return static_cast<int>(volume * 100.0f);
}

void SystemVolumeController::SetVolume(int percentage) {
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    Float32 newVolume = percentage / 100.0f;

    AudioDeviceID defaultDevice = 0;
    UInt32 size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress defaultAddr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, nullptr, &size, &defaultDevice);

    size = sizeof(Float32);
    AudioObjectPropertyAddress volumeAddr = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };

    AudioObjectSetPropertyData(defaultDevice, &volumeAddr, 0, nullptr, size, &newVolume);
}
