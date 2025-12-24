#pragma once

#define VCAMAPI_API __declspec(dllexport)

#ifdef __cplusplus
extern "C" {
#endif
    typedef void* VCamHandle;

    // Frame format types
    typedef enum {
        VCAM_FORMAT_RGB32 = 0,
        VCAM_FORMAT_NV12 = 1,
        VCAM_FORMAT_BGRA = 2
    } VCamFormat;

    // Error codes
    typedef enum {
        VCAM_SUCCESS = 1,
        VCAM_ERROR_INVALID_PARAM = -1,
        VCAM_ERROR_INIT_FAILED = -2,
        VCAM_ERROR_CAMERA_EXISTS = -3,
        VCAM_ERROR_CAMERA_NOT_FOUND = -4,
        VCAM_ERROR_FRAME_PUSH_FAILED = -5
    } VCamResult;

    /**
     * Create a virtual camera
     * @param name Camera name (displayed in camera apps)
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param fps Frames per second
     * @param handle Output handle to the created camera
     * @return VCamResult error code
     */
    VCAMAPI_API VCamResult CreateCam(const wchar_t* name, int width, int height, int fps, VCamHandle* handle);

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
     * @param format Frame format (VCAM_FORMAT_RGB32 or VCAM_FORMAT_NV12)
     * @return VCamResult error code
     */
    VCAMAPI_API VCamResult PushCamFrame(VCamHandle handle, const void* data, VCamFormat format);

    /**
     * Get the last error message (if any)
     * @param handle Camera handle
     * @return Error message string (empty if no error)
     */
    VCAMAPI_API const wchar_t* VCamGetLastError(VCamHandle handle);

#ifdef __cplusplus
}
#endif

