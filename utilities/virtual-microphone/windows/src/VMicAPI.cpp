#include <mutex>
#include <string>
#include <vector>
#include <iostream>

#include "VMicAPI.h"
#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

struct VMicContext {
    VMicFormat format{};
    IAudioClient* pAudioClient{ nullptr };
    IAudioRenderClient* pRenderClient{ nullptr };
    std::string lastError{};
    std::mutex handlerMutex{};
    UINT32 frameSize{ 0 };
};

static std::string WideToUtf8(LPCWSTR wstr) {
    if (!wstr) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], size, nullptr, nullptr);
    str.resize(size - 1);
    return str;
}

static std::wstring Utf8ToWide(const char* str) {
    if (!str) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstr[0], size);
    wstr.resize(size - 1);
    return wstr;
}

static VMicResult SetError(const VMicHandle handle, const VMicResult code, const std::string& errorMsg) {
    if (handle) {
        handle->lastError = errorMsg;
    }

    return code;
}

#ifdef __cplusplus
extern "C" {
#endif

VMicResult VMic_Initialize(void) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return VMIC_ERROR_INIT_FAILED;
    }

    return VMIC_SUCCESS;
}

void VMic_Shutdown(void) {
    CoUninitialize();
}

VMicResult VMic_GetAvailableDevices(VMicDeviceInfo* devices, uint32_t* count) {
    if (!count) return VMIC_ERROR_GENERIC;

    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnumerator));
    if (FAILED(hr)) return VMIC_ERROR_INIT_FAILED;

    IMMDeviceCollection* pCollection = nullptr;
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        pEnumerator->Release();
        return VMIC_ERROR_GENERIC;
    }

    UINT deviceCount = 0;
    pCollection->GetCount(&deviceCount);

    if (devices == nullptr) {
        *count = deviceCount;
        pCollection->Release();
        pEnumerator->Release();
        return VMIC_SUCCESS;
    }

    uint32_t limit = (*count < deviceCount) ? *count : deviceCount;
    *count = limit;

    for (uint32_t i = 0; i < limit; i++) {
        IMMDevice* pDevice = nullptr;
        if (SUCCEEDED(pCollection->Item(i, &pDevice))) {
            LPWSTR pwszID = nullptr;
            if (SUCCEEDED(pDevice->GetId(&pwszID))) {
                std::string utf8Id = WideToUtf8(pwszID);
                strncpy_s(devices[i].id, sizeof(devices[i].id), utf8Id.c_str(), _TRUNCATE);
                CoTaskMemFree(pwszID);
            }

            IPropertyStore* pProps = nullptr;
            if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
                    std::string utf8Name = WideToUtf8(varName.pwszVal);
                    strncpy_s(devices[i].name, sizeof(devices[i].name), utf8Name.c_str(), _TRUNCATE);
                    PropVariantClear(&varName);
                }
                pProps->Release();
            }
            pDevice->Release();
        }
    }

    pCollection->Release();
    pEnumerator->Release();
    return VMIC_SUCCESS;
}

VMicResult VMic_CreateDevice(const char* deviceName, char* deviceId, uint32_t bufferSize) {
    return VMIC_ERROR_UNSUPPORTED_FUNCTION;
}

VMicResult VMic_DestroyDevice(const char* deviceId) {
    return VMIC_ERROR_UNSUPPORTED_FUNCTION;
}

VMicResult VMic_OpenDevice(VMicHandle* handle, const char* deviceId, const VMicFormat* format) {
    if (!handle || !format) return VMIC_ERROR_GENERIC;

    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnumerator));
    if (FAILED(hr)) return VMIC_ERROR_INIT_FAILED;

    IMMDevice* pDevice = nullptr;
    if (deviceId == nullptr || strlen(deviceId) == 0) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    } else {
        const std::wstring wDeviceId = Utf8ToWide(deviceId);
        hr = pEnumerator->GetDevice(wDeviceId.c_str(), &pDevice);
    }
    pEnumerator->Release();

    if (FAILED(hr) || !pDevice) return VMIC_ERROR_DRIVER_NOT_FOUND;

    IAudioClient* pAudioClient = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&pAudioClient));
    pDevice->Release();
    if (FAILED(hr)) return VMIC_ERROR_GENERIC;

    WAVEFORMATEXTENSIBLE wfx = {};
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = format->channels;
    wfx.Format.nSamplesPerSec = format->sampleRate;
    wfx.Format.wBitsPerSample = format->bitDepth;
    wfx.Format.nBlockAlign = (format->channels * format->bitDepth) / 8;
    wfx.Format.nAvgBytesPerSec = format->sampleRate * wfx.Format.nBlockAlign;
    wfx.Format.cbSize = 22;
    wfx.Samples.wValidBitsPerSample = format->bitDepth;
    wfx.dwChannelMask = (format->channels == 2) ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : SPEAKER_FRONT_CENTER;

    wfx.SubFormat = (format->bitDepth == 32) ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;

    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        0,
        10000000,
        0,
        &wfx.Format,
        nullptr
    );

    if (FAILED(hr)) {
        pAudioClient->Release();
        return VMIC_ERROR_UNSUPPORTED_FORMAT;
    }

    IAudioRenderClient* pRenderClient = nullptr;
    hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&pRenderClient));
    if (FAILED(hr)) {
        pAudioClient->Release();
        return VMIC_ERROR_GENERIC;
    }

    VMicContext* ctx = new VMicContext();
    ctx->format = *format;
    ctx->pAudioClient = pAudioClient;
    ctx->pRenderClient = pRenderClient;
    ctx->frameSize = wfx.Format.nBlockAlign;

    *handle = ctx;
    return VMIC_SUCCESS;
}

VMicResult VMic_Close(const VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;

    {
        std::lock_guard<std::mutex> lock(handle->handlerMutex);

        if (handle->pAudioClient) {
            handle->pAudioClient->Stop();
        }

        if (handle->pRenderClient) {
            handle->pRenderClient->Release();
            handle->pRenderClient = nullptr;
        }

        if (handle->pAudioClient) {
            handle->pAudioClient->Release();
            handle->pAudioClient = nullptr;
        }
    }

    delete handle;
    return VMIC_SUCCESS;
}

VMicResult VMic_StartStream(const VMicHandle handle) {
    if (!handle || !handle->pAudioClient) return VMIC_ERROR_INVALID_HANDLE;

    std::lock_guard<std::mutex> lock(handle->handlerMutex);
    const HRESULT hr = handle->pAudioClient->Start();
    if (FAILED(hr)) return SetError(handle, VMIC_ERROR_GENERIC, "Failed to start WASAPI stream.");

    return VMIC_SUCCESS;
}

VMicResult VMic_StopStream(const VMicHandle handle) {
    if (!handle || !handle->pAudioClient) return VMIC_ERROR_INVALID_HANDLE;

    std::lock_guard<std::mutex> lock(handle->handlerMutex);
    const HRESULT hr = handle->pAudioClient->Stop();
    if (FAILED(hr)) return SetError(handle, VMIC_ERROR_GENERIC, "Failed to stop WASAPI stream.");

    return VMIC_SUCCESS;
}

VMicResult VMic_GetAvailableSpace(const VMicHandle handle, uint32_t* availableSamples) {
    if (!handle || !handle->pAudioClient || !availableSamples) return VMIC_ERROR_INVALID_HANDLE;

    std::lock_guard<std::mutex> lock(handle->handlerMutex);

    UINT32 bufferFrameCount = 0;
    UINT32 numFramesPadding = 0;

    handle->pAudioClient->GetBufferSize(&bufferFrameCount);
    handle->pAudioClient->GetCurrentPadding(&numFramesPadding);

    *availableSamples = bufferFrameCount - numFramesPadding;

    return VMIC_SUCCESS;
}

VMicResult VMic_PushSamples(const VMicHandle handle, const void* samples, const uint32_t sampleCount) {
    if (!handle || !handle->pRenderClient || !samples) return VMIC_ERROR_INVALID_HANDLE;

    std::lock_guard<std::mutex> lock(handle->handlerMutex);

    UINT32 padding = 0;
    UINT32 bufferSize = 0;
    handle->pAudioClient->GetBufferSize(&bufferSize);
    handle->pAudioClient->GetCurrentPadding(&padding);

    const UINT32 availableFrames = bufferSize - padding;
    if (sampleCount > availableFrames) {
        return SetError(handle, VMIC_ERROR_BUFFER_FULL, "Not enough space in WASAPI buffer.");
    }

    BYTE* pData = nullptr;
    HRESULT hr = handle->pRenderClient->GetBuffer(sampleCount, &pData);
    if (FAILED(hr)) return SetError(handle, VMIC_ERROR_GENERIC, "Failed to lock WASAPI buffer.");

    memcpy(pData, samples, static_cast<size_t>(sampleCount) * handle->frameSize);

    hr = handle->pRenderClient->ReleaseBuffer(sampleCount, 0);
    if (FAILED(hr)) return SetError(handle, VMIC_ERROR_GENERIC, "Failed to release WASAPI buffer.");

    return VMIC_SUCCESS;
}

VMicResult VMic_Flush(const VMicHandle handle) {
    if (!handle || !handle->pAudioClient) return VMIC_ERROR_INVALID_HANDLE;

    std::lock_guard<std::mutex> lock(handle->handlerMutex);

    handle->pAudioClient->Stop();
    handle->pAudioClient->Reset();
    handle->pAudioClient->Start();

    return VMIC_SUCCESS;
}

const char* VMic_GetLastError(const VMicHandle handle) {
    if (!handle) return "Invalid handle";
    std::lock_guard<std::mutex> lock(handle->handlerMutex);
    return handle->lastError.c_str();
}

#ifdef __cplusplus
}
#endif
