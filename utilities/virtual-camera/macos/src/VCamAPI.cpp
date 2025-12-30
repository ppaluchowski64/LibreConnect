#include "VCamAPI.h"

extern "C" {
    VCAMAPI_API VCamResult CreateCam(const char* name, int width, int height, int fps, VCamHandle* handle) {
        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult DestroyCam(VCamHandle handle) {
        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(const VCamHandle handle, const void* data, const VCamFormat format) {
        return VCAM_SUCCESS;
    }

    VCAMAPI_API const char* VCamGetLastError(VCamHandle handle) {
        return "";
    }
}