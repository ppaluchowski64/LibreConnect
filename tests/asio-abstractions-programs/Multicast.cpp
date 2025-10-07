#include <Scanner.h>
#include <thread>
#include <ConnectionManager.h>
#include <CryptographicIdentityManager.h>
#include <iostream>

int main() {
    LanDeviceScanner::BeginScan();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::vector<DeviceInfo> devices = LanDeviceScanner::GetDiscoveredDevices();

        if (devices.empty()) {
            std::cout << "\nNo devices found\n" << std::endl;
            continue;
        }

        std::cout << "======== DISCOVERED DEVICES ========\n\n";
        for (const auto& device : devices) {
            std::cout<<device.deviceName<<" , "<<device.deviceID<<'\n';
        }
        std::cout << "\n======== END DISCOVERED DEVICES ========\n\n";\
    }
}
