#pragma once

#if defined(_WIN32) || defined(_WIN64)
        #define VCAMAPI_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
    #define VCAMAPI_API __attribute__((visibility("default")))
#else
    #define VCAMAPI_API
#endif

#include "VCamTypes.h"

#ifdef __cplusplus
extern "C" {
#endif
    /**
     * Create a virtual camera
     * @param name Camera name (displayed in camera apps)
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param fps Frames per second
     * @param format Frame format (VCAM_FORMAT_RGB32 or VCAM_FORMAT_NV12)
     * @param handle Output handle to the created camera
     * @return VCamResult error code
     */
    VCAMAPI_API VCamResult CreateCam(const char* name, int width, int height, int fps, VCamFormat format, VCamHandle* handle);

    /**
     * Destroy a virtual camera
     * @param handle Camera handle returned from CreateCam
     * @return VCamResult error code
     */
    VCAMAPI_API VCamResult DestroyCam(VCamHandle handle);

    /**
     * Push a frame to the virtual camera
     * @param handle Camera handle
     * @param data Pointer to frame data (RGB32 or NV12 format)
     * @return VCamResult error code
     */
    VCAMAPI_API VCamResult PushCamFrame(VCamHandle handle, const void* data);

    /**
     * Get the last error message (if any)
     * @param handle Camera handle
     * @return Error message string (empty if no error)
     */
    VCAMAPI_API const char* VCamGetLastError(VCamHandle handle);

#ifdef __cplusplus
}
#endif

