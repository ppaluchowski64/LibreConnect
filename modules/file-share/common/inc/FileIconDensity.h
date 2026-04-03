#ifndef FILEICONDENSITY_H
#define FILEICONDENSITY_H

#include <cstdint>

enum class FileIconDensity : int32_t {
    DEFAULT = 0,
    LOW     = 120,     // LDPI (~36x36px)
    MEDIUM  = 160,     // MDPI (~48x48px)
    HIGH    = 240,     // HDPI (~72x72px)
    XHIGH   = 320,     // XHDPI (~96x96px)
    XXHIGH  = 480,     // XXHDPI (~144x144px)
    XXXHIGH = 640      // XXXHDPI (~192x192px)
};

#endif //FILEICONDENSITY_H
