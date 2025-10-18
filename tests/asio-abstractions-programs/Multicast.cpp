#include <Scanner.h>
#include <thread>
#include <ConnectionManager.h>
#include <iostream>

int main() {
    LanDeviceScanner::BeginScan();
    ConnectionManager::Seek(TCPEndpoint(asio::ip::tcp::v4(), 5000));
    std::vector<DeviceInfo> devices;

    ConnectionManager::AddResponseHandler(PC_PackageType::MESSAGE, [](std::unique_ptr<Package<PC_PackageType>>&& package) {
        std::string value;
        package->GetValue(value);
        Debug::Log(value);
    });

    std::atomic<bool> connected = false;

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
        } else if (command.starts_with("cn")) {
            try {
                command += ' ';
                const size_t spacePosition = command.find(' ', 3);
                std::string portStr = command.substr(3, spacePosition - 3);
                const size_t id = static_cast<size_t>(std::stoi(portStr));

                if (devices.size() > id) {
                    Debug::LogError("Invalid id");
                    continue;
                }

                ConnectionManager::Disconnect([info = devices[id-1], &connected]() {
                    TCPEndpoint endpoint(asio::ip::make_address_v4(info.deviceAddress), info.deviceAddressPort);
                    Debug::Log("dd");
                    ConnectionManager::Connect(std::move(endpoint), [&connected](const bool result) {
                        if (!result) {
                            Debug::LogError("Failed to connect to device");
                        } else {
                            connected.store(true);
                        }
                    });
                });

            } catch (const std::invalid_argument& e) {
                Debug::LogError(std::string(e.what()));
            }
        }

        while (connected.load()) {
            std::string text;
            std::getline(std::cin, text);

            ConnectionManager::Send(PC_PackageType::MESSAGE, std::move(text));
        }
    }
}
