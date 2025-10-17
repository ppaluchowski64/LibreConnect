#ifndef DEVICE_TYPE_DETECTOR_H
#define DEVICE_TYPE_DETECTOR_H

enum class DeviceType {
    Linux,
    macOS,
    Windows,
    Android,
    iOS,
    Unknown,
};

class DeviceTypeDetector {
    public:
        static DeviceType GetDeviceType();
};

#endif // DEVICE_TYPE_DETECTOR_H
