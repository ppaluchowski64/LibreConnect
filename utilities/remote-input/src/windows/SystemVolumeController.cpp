#include "SystemVolumeController.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

int SystemVolumeController::GetVolume() {
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* defaultDevice = nullptr;
    IAudioEndpointVolume* endpointVolume = nullptr;
    float currentVolume = 0.0f;

    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
            if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(&endpointVolume)))) {
                endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        enumerator->Release();
    }

    if (SUCCEEDED(hrInit)) {
        CoUninitialize();
    }

    return static_cast<int>(currentVolume * 100.0f);
}

void SystemVolumeController::SetVolume(int percentage) {
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    float newVolume = static_cast<float>(percentage) / 100.0f;

    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* defaultDevice = nullptr;
    IAudioEndpointVolume* endpointVolume = nullptr;

    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
            if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(&endpointVolume)))) {
                endpointVolume->SetMasterVolumeLevelScalar(newVolume, nullptr);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        enumerator->Release();
    }

    if (SUCCEEDED(hrInit)) {
        CoUninitialize();
    }
}
