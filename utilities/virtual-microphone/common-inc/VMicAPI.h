#ifndef VMIC_API_H
#define VMIC_API_H

#include <VMicTypes.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #define VMIC_API __declspec(dllexport)
#else
    #if __GNUC__ >= 4
        #define VMIC_API __attribute__((visibility("default")))
    #else
        #define VMIC_API
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

VMIC_API VMicResult VMic_Initialize(void);
VMIC_API void VMic_Shutdown(void);

VMIC_API VMicResult VMic_GetAvailableDevices(VMicDeviceInfo* devices, uint32_t* count);

VMIC_API VMicResult VMic_CreateDevice(const char* deviceName, char* deviceId, uint32_t bufferSize);
VMIC_API VMicResult VMic_DestroyDevice(const char* deviceId);

VMIC_API VMicResult VMic_OpenDevice(VMicHandle* handle, const char* deviceId, const VMicFormat* format);
VMIC_API VMicResult VMic_Close(VMicHandle handle);

VMIC_API VMicResult VMic_StartStream(VMicHandle handle);
VMIC_API VMicResult VMic_StopStream(VMicHandle handle);

VMIC_API VMicResult VMic_GetAvailableSpace(VMicHandle handle, uint32_t* availableSamples);
VMIC_API VMicResult VMic_PushSamples(VMicHandle handle, const void* samples, uint32_t sampleCount);
VMIC_API VMicResult VMic_Flush(VMicHandle handle);

VMIC_API const char* VMic_GetLastError(VMicHandle handle);

#ifdef __cplusplus
}
#endif

#endif // VMIC_API_H