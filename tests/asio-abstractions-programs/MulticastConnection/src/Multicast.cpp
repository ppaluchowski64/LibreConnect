#include <Scanner.h>
#include <ConnectionManager.h>
#include <InitialConnection.h>
#include <iostream>
#include <QCoreApplication>

int main() {
    ConnectionManager::StartAcceptingConnections();
    LanDeviceScanner::BeginScan();

    std::vector<DeviceInfo> devices;

    ConnectionManager::AddResponseHandler(PC_PackageType::NONE, [](std::unique_ptr<Package<PC_PackageType>>&& package) {
        std::string value;
        package->GetValue(value);
        Debug::Log(value);
    });

    std::atomic<bool> connected = false;

    while (true) {
        while (connected.load()) {
            Debug::Log("Send message");
            std::string text;
            std::getline(std::cin, text);
            ConnectionManager::Send(PC_PackageType::NONE, std::move(text));
        }

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

                if (devices.size() < id || id == 0) {
                    Debug::LogError("Invalid id");
                    continue;
                }

                Debug::Log("{}:{}", devices[id-1].deviceAddress, devices[id-1].deviceAddressPort);

                ConnectionManager::Connect(devices[id-1].deviceAddress, devices[id-1].deviceAddressPort, InitialConnectionMode::CONNECTION_WITHOUT_PAIR);
                connected = true;

            } catch (const std::invalid_argument& e) {
                Debug::LogError(std::string(e.what()));
            }
        }
    }
}
