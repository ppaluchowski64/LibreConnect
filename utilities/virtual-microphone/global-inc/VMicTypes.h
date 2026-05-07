#pragma once

#include <stdint.h>
#include <stddef.h>

typedef enum {
    VMIC_SUCCESS = 0,
    VMIC_ERROR_GENERIC = -1,
    VMIC_ERROR_DRIVER_NOT_FOUND = -2,
    VMIC_ERROR_UNSUPPORTED_FORMAT = -3,
    VMIC_ERROR_INVALID_HANDLE = -4,
    VMIC_ERROR_BUFFER_FULL = -5,
    VMIC_ERROR_INIT_FAILED = -6,
    VMIC_ERROR_UNSUPPORTED_FUNCTION = -7
} VMicResult;

typedef struct {
    uint32_t sampleRate;
    uint16_t bitDepth;
    uint16_t channels;
} VMicFormat;

typedef struct {
    char id[256];
    char name[256];
} VMicDeviceInfo;

typedef struct VMicContext* VMicHandle;