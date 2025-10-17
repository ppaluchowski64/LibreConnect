#include "DeviceTypeDetector.h"

#ifdef __APPLE__
    #include <TargetConditionals.h>
#endif

DeviceType DeviceTypeDetector::GetDeviceType() {
    #ifdef __ANDROID__
        return DeviceType::Android;
    #elif defined(__APPLE__)
        #if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
            return DeviceType::iOS;
        #else
            return DeviceType::macOS;
        #endif
    #elif defined(__linux__)
        return DeviceType::Linux;
    #elif defined(_WIN32)
        return DeviceType::Windows;
    #else
        return DeviceType::Unknown;
    #endif
}
