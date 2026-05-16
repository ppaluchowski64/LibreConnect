#ifndef MICROPHONE_TYPES_H
#define MICROPHONE_TYPES_H

#include <string>

constexpr size_t AUDIO_STREAM_KEY_SIZE = 255;

struct MicrophoneDevice {
    std::string name;
    std::string id;
};

#endif //MICROPHONE_TYPES_H