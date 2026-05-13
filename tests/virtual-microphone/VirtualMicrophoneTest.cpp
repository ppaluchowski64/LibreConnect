#include <VMicAPI.h>
#include <DebugLog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr uint32_t kSampleRate = 48000;
constexpr uint16_t kBitDepth = 16;
constexpr uint16_t kChannels = 2;
constexpr uint32_t kChunkFrames = 480;
constexpr double kToneFrequencyHz = 440.0;
constexpr double kPi = 3.14159265358979323846;
}

int main(const int argc, char** argv) {
    const VMicResult initResult = VMic_Initialize();
    if (initResult != VMIC_SUCCESS) {
        Debug::LogError("Virtual microphone init failed with code {}", static_cast<int>(initResult));
        return -1;
    }

#if !defined(WIN32) && !defined(__APPLE__)
    {
        char id[255];
        const VMicResult createDevice = VMic_CreateDevice("TestDevice2", id, kSampleRate);
    }
#endif

    uint32_t deviceCount = 0;
    const VMicResult countResult = VMic_GetAvailableDevices(nullptr, &deviceCount);
    if (countResult != VMIC_SUCCESS) {
        Debug::LogError("Failed to enumerate virtual microphone devices: {}", static_cast<int>(countResult));
        VMic_Shutdown();
        return -1;
    }

    std::vector<VMicDeviceInfo> devices(deviceCount);
    uint32_t listedCount = 0;
    if (deviceCount > 0) {
        listedCount = deviceCount;
        const VMicResult listResult = VMic_GetAvailableDevices(devices.data(), &listedCount);
        if (listResult != VMIC_SUCCESS) {
            Debug::LogError("Failed to fetch virtual microphone device details: {}", static_cast<int>(listResult));
            VMic_Shutdown();
            return -1;
        }

        Debug::Log("Enumerated {} virtual microphone output device(s)", listedCount);
        for (uint32_t i = 0; i < listedCount; ++i) {
            Debug::Log("  [{}] {} ({})", i, devices[i].name, devices[i].id);
        }
    } else {
        Debug::LogWarning("No active render devices found; default device open may fail");
    }

    if (argc > 1 && std::string(argv[1]) == "--list") {
        VMic_Shutdown();
        return 0;
    }

    std::string selectedDeviceId;
    if (listedCount > 0) {
        long selectedIndex = -1;

        if (argc > 1) {
            char* end = nullptr;
            selectedIndex = std::strtol(argv[1], &end, 10);
            if (end == argv[1] || *end != '\0') {
                Debug::LogError("Invalid device index '{}'. Use --list to inspect devices first.", argv[1]);
                VMic_Shutdown();
                return -1;
            }
        } else {
            std::cout << "Select device index (-1 for default): ";
            std::cin >> selectedIndex;
            if (!std::cin) {
                Debug::LogError("Failed to read device selection from stdin");
                VMic_Shutdown();
                return -1;
            }
        }

        if (selectedIndex >= 0) {
            if (selectedIndex >= static_cast<long>(listedCount)) {
                Debug::LogError("Device index {} out of range 0..{}", selectedIndex, listedCount - 1);
                VMic_Shutdown();
                return -1;
            }

            selectedDeviceId = devices[static_cast<size_t>(selectedIndex)].id;
            Debug::Log("Selected device [{}] {} ({})",
                selectedIndex,
                devices[static_cast<size_t>(selectedIndex)].name,
                selectedDeviceId);
        } else {
            Debug::Log("Using default render device");
        }
    }

    VMicHandle handle = nullptr;
    constexpr VMicFormat format{
        .sampleRate = kSampleRate,
        .bitDepth = kBitDepth,
        .channels = kChannels
    };

    const char* deviceId = selectedDeviceId.empty() ? nullptr : selectedDeviceId.c_str();
    const VMicResult openResult = VMic_OpenDevice(&handle, deviceId, &format);
    if (openResult != VMIC_SUCCESS || handle == nullptr) {
        Debug::LogError("Failed to open virtual microphone device: {}", static_cast<int>(openResult));
        VMic_Shutdown();
        return -1;
    }

    const VMicResult startResult = VMic_StartStream(handle);
    if (startResult != VMIC_SUCCESS) {
        Debug::LogError("Failed to start virtual microphone stream: {}", VMic_GetLastError(handle));
        VMic_Close(handle);
        VMic_Shutdown();
        return -1;
    }

    Debug::Log("Virtual microphone stream started");

    constexpr uint32_t totalFramesToPush = kSampleRate * 30;
    uint32_t pushedFrames = 0;
    double phase = 0.0;
    constexpr double phaseStep = (2.0 * kPi * kToneFrequencyHz) / static_cast<double>(kSampleRate);
    std::vector<int16_t> samples(static_cast<size_t>(kChunkFrames) * kChannels);

    while (pushedFrames < totalFramesToPush) {
        uint32_t availableFrames = 0;
        const VMicResult spaceResult = VMic_GetAvailableSpace(handle, &availableFrames);
        if (spaceResult != VMIC_SUCCESS) {
            Debug::LogError("Failed to query virtual microphone buffer space: {}", VMic_GetLastError(handle));
            break;
        }

        if (availableFrames == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        const uint32_t framesToWrite = std::min({availableFrames, kChunkFrames, totalFramesToPush - pushedFrames});
        for (uint32_t frame = 0; frame < framesToWrite; ++frame) {
            const int16_t sample = static_cast<int16_t>(std::sin(phase) * 12000.0);
            for (uint16_t channel = 0; channel < kChannels; ++channel) {
                samples[static_cast<size_t>(frame) * kChannels + channel] = sample;
            }

            phase += phaseStep;
            if (phase >= 2.0 * kPi) {
                phase -= 2.0 * kPi;
            }
        }

        const VMicResult pushResult = VMic_PushSamples(handle, samples.data(), framesToWrite);
        if (pushResult == VMIC_ERROR_BUFFER_FULL) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (pushResult != VMIC_SUCCESS) {
            Debug::LogError("Failed to push audio samples: {}", VMic_GetLastError(handle));
            break;
        }

        pushedFrames += framesToWrite;
    }

    VMic_StopStream(handle);
    VMic_Close(handle);
    VMic_Shutdown();

    Debug::Log("Virtual microphone stream stopped after pushing {} frame(s)", pushedFrames);
    return pushedFrames == totalFramesToPush ? 0 : -1;
}
