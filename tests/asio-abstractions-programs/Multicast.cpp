#include <Scanner.h>
#include <thread>
#include <ConnectionManager.h>
#include <iostream>

int main() {
    LanDeviceScanner::BeginScan();
    ConnectionManager::Seek(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), 5000));
    std::vector<DeviceInfo> devices;

    while (true) {
        std::cout << "============= COMMANDS =============\n\n"
                     "> rf - refresh devices\n"
                     "> pd - print devices\n"
                     "> cn $deviceID - connect to device\n\n"
                     "====================================\n\n";

        std::string command;
        std::getline(std::cin, command);

        if (command == "rf") {
            devices = LanDeviceScanner::GetDiscoveredDevices();
        } else if (command == "pd") {
            for (int i = 0; i < devices.size(); i++) {
                std::cout << i + 1 << ": name=" << devices[i].deviceName << " uuid=" << boost::uuids::to_string(devices[i].deviceID) << "\n";
            }
        } else if (command.starts_with("cd")) {
            try {
                const size_t spacePosition = command.find(' ', 3);
                std::string portStr = command.substr(3, spacePosition - 3);
                const size_t id = static_cast<size_t>(std::stoi(portStr));

                if (devices.size() > id) {
                    Debug::LogError("Invalid id");
                    continue;
                }

                ConnectionManager::AbortSeek([info = devices[id-1]]() {
                    TCPEndpoint endpoint(asio::ip::make_address_v4(info.deviceAddress), info.deviceAddressPort);
                    ConnectionManager::Connect(std::move(endpoint), [](const bool result) {
                       if (!result) {
                           Debug::LogError("Failed to connect to device");
                       }
                    });
                });

            } catch (const std::invalid_argument& e) {
                Debug::LogError(e.what());
            }
        }

    }
}
