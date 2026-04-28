#include <Scanner.h>
#include <SignalReceiver.h>

#include <boost/uuid/uuid_generators.hpp>
#include <nlohmann/json.hpp>

#include <fstream>

constexpr size_t SLEEP_DURATION = 10;

void LoadDevicesToAutoConnect(std::vector<uuid>& devices) {
    static boost::uuids::string_generator gen;
    devices.clear();

    try {
        std::ifstream stream("auto-connect-list.JSON");
        if (!stream.is_open()) return;

        const nlohmann::json json = nlohmann::json::parse(stream, nullptr, false);
        if (json.is_discarded() || !json.is_array()) return;

        for (const auto& item : json) {
            devices.push_back(gen(item.get<std::string>()));
        }
    } catch (...) {
        devices.clear();
    }
}

void StartInstance(const std::string& address, uint16_t port) {
    // TODO
}

int main() {
    LanDeviceScanner::BeginScan();
    SignalReceiver::StartReceiving();

    std::vector<uuid> autoConnectDevices{};

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DURATION));

        std::vector<DeviceInfo> devices = LanDeviceScanner::GetDiscoveredDevices();
        std::vector<uuid> connectedDevices = SignalReceiver::GetConnectedDevices();
        LoadDevicesToAutoConnect(autoConnectDevices);

        for (const auto& device : devices) {
            if (std::ranges::find(autoConnectDevices, device.deviceID) == connectedDevices.end()) {
                continue;
            }

            if (std::ranges::find(connectedDevices, device.deviceID) != autoConnectDevices.end()) {
                continue;
            }

            StartInstance(device.deviceAddress, device.deviceAddressPort);
        }
    }
}