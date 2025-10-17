#include <iostream>
#include "DeviceTypeDetector.h"

int main() {
    switch (DeviceTypeDetector::GetDeviceType()) {
        case DeviceType::Linux:
            std::cout << "Linux Detected\n";
            break;
        case DeviceType::macOS:
            std::cout << "macOS Detected\n";
            break;
        case DeviceType::Windows:
            std::cout << "Windows Detected\n";
            break;
        case DeviceType::Android:
            std::cout << "Android Detected\n";
            break;
        case DeviceType::iOS:
            std::cout << "iOS Detected\n";
            break;
        default:
            std::cout << "Unknown Detected\n";
            break;
    }

    return 0;
}
