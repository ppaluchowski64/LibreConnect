#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ConnectionManager.h>
#include <Events.h>
#include <FileShareEvents.h>
#include <FileShareModule.h>
#include <InitialConnection.h>
#include <ModulesManager.h>
#ifndef MACOS_DEVICE
#include <NetworkCameraModule.h>
#endif
#include <NotificationSyncModule.h>
#include <Scanner.h>

namespace {

struct SharedState {
    std::mutex ioMutex;
    std::mutex verificationMutex;
    std::atomic<bool> running{true};
    std::atomic<bool> connected{false};
    std::unique_ptr<ConnectionVerificationEvent> verificationEvent;
};

void PrintLine(SharedState& state, const std::string& line) {
    std::lock_guard<std::mutex> lock(state.ioMutex);
    std::cout << line << std::endl;
}

const char* ModuleStateToString(ModuleState state) {
    switch (state) {
        case ModuleState::Uninitialized: return "Uninitialized";
        case ModuleState::Initializing: return "Initializing";
        case ModuleState::Enabling: return "Enabling";
        case ModuleState::Enabled: return "Enabled";
        case ModuleState::Disabling: return "Disabling";
        case ModuleState::Disabled: return "Disabled";
        default: return "Unknown";
    }
}

InitialConnectionMode ParseMode(const std::string& value, bool& ok) {
    ok = true;
    if (value == "pair" || value == "0") {
        return InitialConnectionMode::PAIR_AND_CONNECT;
    }
    if (value == "paired" || value == "with-pair" || value == "1") {
        return InitialConnectionMode::CONNECT_WITH_PAIR;
    }
    if (value == "nopair" || value == "no-pair" || value == "2") {
        return InitialConnectionMode::CONNECTION_WITHOUT_PAIR;
    }

    ok = false;
    return InitialConnectionMode::CONNECTION_WITHOUT_PAIR;
}

void PrintHelp(SharedState& state) {
    PrintLine(state, "Commands:");
    PrintLine(state, "  help");
    PrintLine(state, "  scan start | scan stop | scan list");
    PrintLine(state, "  devices                 (alias for scan list)");
    PrintLine(state, "  connect <index> [mode]  (mode: pair, paired, nopair)");
    PrintLine(state, "  connect-ip <ip> <port> [mode]");
    PrintLine(state, "  disconnect");
    PrintLine(state, "  paired                  (list paired devices)");
    PrintLine(state, "  modules                 (show module states)");
    PrintLine(state, "  enable <file|camera|notify|all>");
    PrintLine(state, "  disable <file|camera|notify|all>");
    PrintLine(state, "  verify <code>");
    PrintLine(state, "  verify-cancel");
    PrintLine(state, "  exit");
}

class ConsoleEventListener : public QObject {
public:
    explicit ConsoleEventListener(SharedState& state)
        : m_state(state) {}

protected:
    bool event(QEvent* e) override {
        const auto type = e->type();

        if (type == ConnectedEvent::Type) {
            auto* ev = static_cast<ConnectedEvent*>(e);
            const bool ok = (ev->GetResult() == EventResult::SUCCESS);
            m_state.connected.store(ok);
            PrintLine(m_state, ok ? "Connected." : "Connection failed.");
            ClearVerification();
            return true;
        }

        if (type == DisconnectedEvent::Type) {
            auto* ev = static_cast<DisconnectedEvent*>(e);
            m_state.connected.store(false);
            PrintLine(m_state, "Disconnected: " + ev->GetErrorCode().message());
            ClearVerification();
            return true;
        }

        if (type == ScannerErrorEvent::Type) {
            auto* ev = static_cast<ScannerErrorEvent*>(e);
            PrintLine(m_state, "Scanner error: " + ev->GetErrorCode().message());
            return true;
        }

        if (type == ConnectionPendingEvent::Type) {
            auto* ev = static_cast<ConnectionPendingEvent*>(e);
            const DeviceInfo info = ev->GetDeviceInfo();
            PrintLine(m_state, "Incoming connection from: " + info.deviceName);
            ev->AcceptConnection();
            return true;
        }

        if (type == ConnectionVerificationEvent::Type) {
            auto* ev = static_cast<ConnectionVerificationEvent*>(e);
            {
                std::lock_guard<std::mutex> lock(m_state.verificationMutex);
                m_state.verificationEvent.reset(ev->clone());
            }
            PrintLine(m_state, "Verification required. Use: verify <code>");
            return true;
        }

        if (type == ConnectionFailedVerificationEvent::Type) {
            auto* ev = static_cast<ConnectionFailedVerificationEvent*>(e);
            PrintLine(m_state, "Verification failed. Tries left: " + std::to_string(ev->GetLeftTries()));
            return true;
        }

        if (type == FetchDirectoryEntriesResultEvent::Type) {
            auto* ev = static_cast<FetchDirectoryEntriesResultEvent*>(e);
            const auto entries = ev->GetEntries();

            PrintLine(m_state, "Directory entries for: " + ev->GetPath());
            for (const auto& entry : entries) {
                const std::string name = entry.GetName().value_or("<no-name>");
                const std::string path = entry.GetPath().value_or("<no-path>");
                PrintLine(m_state, "  " + path + " / " + name);
            }
            return true;
        }

        if (type == EntryTransferResultEvent::Type) {
            auto* ev = static_cast<EntryTransferResultEvent*>(e);
            const std::string name = ev->GetFileEntry().GetName().value_or("<no-name>");
            PrintLine(m_state, std::string("Transfer result for ") + name + ": " + (ev->Success() ? "success" : "failed"));
            return true;
        }

        return QObject::event(e);
    }

private:
    void ClearVerification() {
        std::lock_guard<std::mutex> lock(m_state.verificationMutex);
        m_state.verificationEvent.reset();
    }

    SharedState& m_state;
};

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    SharedState state;

    ConsoleEventListener listener(state);
    ConnectionManager::AddEventListener(QPointer<QObject>(&listener));

    ConnectionManager::StartAcceptingConnections();
    LanDeviceScanner::BeginScan();

    auto& fileShareModule = ModulesManager::GetModuleReference<FileShareModule>();
#ifndef MACOS_DEVICE
    auto& cameraModule = ModulesManager::GetModuleReference<NetworkCameraModule>();
#endif
    auto& notificationModule = ModulesManager::GetModuleReference<NotificationSyncModule>();

    std::thread inputThread([&]() {
        std::vector<DeviceInfo> devices;
        PrintHelp(state);

        while (state.running.load()) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                break;
            }

            std::istringstream iss(line);
            std::vector<std::string> tokens;
            for (std::string token; iss >> token; ) {
                tokens.push_back(token);
            }

            if (tokens.empty()) {
                continue;
            }

            const std::string& cmd = tokens[0];

            if (cmd == "help") {
                PrintHelp(state);
                continue;
            }

            if (cmd == "scan") {
                if (tokens.size() < 2) {
                    PrintLine(state, "Usage: scan start|stop|list");
                    continue;
                }

                if (tokens[1] == "start") {
                    LanDeviceScanner::BeginScan();
                    PrintLine(state, "Scan started.");
                } else if (tokens[1] == "stop") {
                    LanDeviceScanner::EndScan();
                    PrintLine(state, "Scan stopped.");
                } else if (tokens[1] == "list") {
                    devices = LanDeviceScanner::GetDiscoveredDevices();
                    if (devices.empty()) {
                        PrintLine(state, "No devices discovered.");
                    } else {
                        for (size_t i = 0; i < devices.size(); ++i) {
                            const auto& dev = devices[i];
                            PrintLine(state,
                                std::to_string(i + 1) + ". " + dev.deviceName + " " +
                                dev.deviceAddress + ":" + std::to_string(dev.deviceAddressPort));
                        }
                    }
                }
                continue;
            }

            if (cmd == "devices") {
                devices = LanDeviceScanner::GetDiscoveredDevices();
                if (devices.empty()) {
                    PrintLine(state, "No devices discovered.");
                } else {
                    for (size_t i = 0; i < devices.size(); ++i) {
                        const auto& dev = devices[i];
                        PrintLine(state,
                            std::to_string(i + 1) + ". " + dev.deviceName + " " +
                            dev.deviceAddress + ":" + std::to_string(dev.deviceAddressPort));
                    }
                }
                continue;
            }

            if (cmd == "connect") {
                if (tokens.size() < 2) {
                    PrintLine(state, "Usage: connect <index> [mode]");
                    continue;
                }

                size_t index = 0;
                try {
                    index = static_cast<size_t>(std::stoul(tokens[1]));
                } catch (const std::exception&) {
                    PrintLine(state, "Invalid index.");
                    continue;
                }
                if (index == 0 || index > devices.size()) {
                    PrintLine(state, "Invalid index.");
                    continue;
                }

                InitialConnectionMode mode = InitialConnectionMode::CONNECTION_WITHOUT_PAIR;
                if (tokens.size() >= 3) {
                    bool ok = false;
                    mode = ParseMode(tokens[2], ok);
                    if (!ok) {
                        PrintLine(state, "Unknown mode. Use: pair, paired, nopair.");
                        continue;
                    }
                }

                const auto& dev = devices[index - 1];
                ConnectionManager::Connect(dev.deviceAddress, dev.deviceAddressPort, mode);
                PrintLine(state, "Connecting to " + dev.deviceAddress + ":" + std::to_string(dev.deviceAddressPort));
                continue;
            }

            if (cmd == "connect-ip") {
                if (tokens.size() < 3) {
                    PrintLine(state, "Usage: connect-ip <ip> <port> [mode]");
                    continue;
                }

                const std::string ip = tokens[1];
                uint16_t port = 0;
                try {
                    port = static_cast<uint16_t>(std::stoul(tokens[2]));
                } catch (const std::exception&) {
                    PrintLine(state, "Invalid port.");
                    continue;
                }
                InitialConnectionMode mode = InitialConnectionMode::CONNECTION_WITHOUT_PAIR;
                if (tokens.size() >= 4) {
                    bool ok = false;
                    mode = ParseMode(tokens[3], ok);
                    if (!ok) {
                        PrintLine(state, "Unknown mode. Use: pair, paired, nopair.");
                        continue;
                    }
                }

                ConnectionManager::Connect(ip, port, mode);
                PrintLine(state, "Connecting to " + ip + ":" + std::to_string(port));
                continue;
            }

            if (cmd == "disconnect") {
                ConnectionManager::Disconnect();
                PrintLine(state, "Disconnect requested.");
                continue;
            }

            if (cmd == "paired") {
                const auto paired = ConnectionManager::GetPairedDevices();
                if (paired.empty()) {
                    PrintLine(state, "No paired devices.");
                } else {
                    for (const auto& dev : paired) {
                        PrintLine(state, dev.deviceName);
                    }
                }
                continue;
            }

            if (cmd == "modules") {
                PrintLine(state, std::string("FileShare: ") + ModuleStateToString(fileShareModule->GetModuleState()));
#ifndef MACOS_DEVICE
                PrintLine(state, std::string("NetworkCamera: ") + ModuleStateToString(cameraModule->GetModuleState()));
#else
                PrintLine(state, "NetworkCamera: Unsupported on macOS");
#endif
                PrintLine(state, std::string("NotificationSync: ") + ModuleStateToString(notificationModule->GetModuleState()));
                continue;
            }

            if (cmd == "enable" || cmd == "disable") {
                if (tokens.size() < 2) {
                    PrintLine(state, "Usage: " + cmd + " <file|camera|notify|all>");
                    continue;
                }

                const bool enable = (cmd == "enable");
                const std::string& target = tokens[1];

                auto runAction = [&](const std::shared_ptr<BaseModule>& module, const std::string& name) {
                    if (enable) {
                        module->Enable();
                        PrintLine(state, "Enable requested: " + name);
                    } else {
                        module->Disable();
                        PrintLine(state, "Disable requested: " + name);
                    }
                };

                if (target == "file" || target == "files" || target == "file-share") {
                    runAction(fileShareModule, "file-share");
                } else if (target == "camera") {
#ifndef MACOS_DEVICE
                    runAction(cameraModule, "network-camera");
#else
                    PrintLine(state, "network-camera is unsupported on macOS.");
#endif
                } else if (target == "notify" || target == "notification") {
                    runAction(notificationModule, "notification-sync");
                } else if (target == "all") {
                    runAction(fileShareModule, "file-share");
#ifndef MACOS_DEVICE
                    runAction(cameraModule, "network-camera");
#endif
                    runAction(notificationModule, "notification-sync");
                } else {
                    PrintLine(state, "Unknown module: " + target);
                }
                continue;
            }

            if (cmd == "verify") {
                if (tokens.size() < 2) {
                    PrintLine(state, "Usage: verify <code>");
                    continue;
                }

                std::unique_ptr<ConnectionVerificationEvent> ev;
                {
                    std::lock_guard<std::mutex> lock(state.verificationMutex);
                    ev = std::move(state.verificationEvent);
                }

                if (!ev) {
                    PrintLine(state, "No verification pending.");
                    continue;
                }

                ev->SendAnswer(tokens[1]);
                PrintLine(state, "Verification code sent.");
                continue;
            }

            if (cmd == "verify-cancel") {
                std::unique_ptr<ConnectionVerificationEvent> ev;
                {
                    std::lock_guard<std::mutex> lock(state.verificationMutex);
                    ev = std::move(state.verificationEvent);
                }

                if (!ev) {
                    PrintLine(state, "No verification pending.");
                    continue;
                }

                ev->SendAnswer(std::string{});
                PrintLine(state, "Verification cancelled.");
                continue;
            }

            if (cmd == "exit" || cmd == "quit") {
                state.running.store(false);
                QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
                break;
            }

            PrintLine(state, "Unknown command. Type 'help' for usage.");
        }
    });

    const int result = app.exec();

    state.running.store(false);
    LanDeviceScanner::EndScan();
    ConnectionManager::StopAcceptingConnections();

    if (inputThread.joinable()) {
        inputThread.join();
    }

    return result;
}
