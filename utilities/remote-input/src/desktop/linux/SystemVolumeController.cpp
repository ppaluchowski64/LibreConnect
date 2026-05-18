#include "SystemVolumeController.h"

#include <alsa/asoundlib.h>

#include <cmath>

int SystemVolumeController::GetVolume() {
    long min = 0, max = 0, volume = 0;

    snd_mixer_t* handle = nullptr;

    if (snd_mixer_open(&handle, 0) < 0)
        return 0;

    if (snd_mixer_attach(handle, "default") < 0) {
        snd_mixer_close(handle);
        return 0;
    }

    if (snd_mixer_selem_register(handle, nullptr, nullptr) < 0 || snd_mixer_load(handle) < 0) {
        snd_mixer_close(handle);
        return 0;
    }

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");

    if (snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid)) {
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &volume);
    }

    snd_mixer_close(handle);

    if (max == 0) return 0;
    return static_cast<int>(std::round((static_cast<double>(volume) * 100.0) / static_cast<double>(max)));
}

void SystemVolumeController::SetVolume(int percentage) {
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    snd_mixer_t* handle = nullptr;

    if (snd_mixer_open(&handle, 0) < 0)
        return;

    if (snd_mixer_attach(handle, "default") < 0) {
        snd_mixer_close(handle);
        return;
    }

    if (snd_mixer_selem_register(handle, nullptr, nullptr) < 0 || snd_mixer_load(handle) < 0) {
        snd_mixer_close(handle);
        return;
    }

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");

    if (snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid)) {
        long min = 0, max = 0;
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);

        long newVolume = static_cast<long>(std::round((static_cast<double>(percentage) * static_cast<double>(max)) / 100.0));
        snd_mixer_selem_set_playback_volume_all(elem, newVolume);
    }

    snd_mixer_close(handle);
}
