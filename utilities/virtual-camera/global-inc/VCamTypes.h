#pragma once

typedef void* VCamHandle;

// Frame format types
typedef enum {
    VCAM_FORMAT_RGB32 = 0,
    VCAM_FORMAT_NV12 = 1,
    VCAM_FORMAT_BGRA = 2,
    VCAM_FORMAT_YUYV = 3,
    VCAM_FORMAT_YUV420 = 4
} VCamFormat;

// Error codes
typedef enum {
    VCAM_SUCCESS = 1,
    VCAM_ERROR_INVALID_PARAM = -1,
    VCAM_ERROR_INIT_FAILED = -2,
    VCAM_ERROR_CAMERA_EXISTS = -3,
    VCAM_ERROR_CAMERA_NOT_FOUND = -4,
    VCAM_ERROR_FRAME_PUSH_FAILED = -5,
    VCAM_ERROR_CAMERA_DESTRUCTION_FAILED = -6
} VCamResult;