#include <Scanner.h>
#include <SignalReceiver.h>

#include <boost/uuid/uuid_generators.hpp>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/wait.h>
#endif

#include <fstream>

#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>

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
    std::string port_str = std::to_string(port);

#if defined(_WIN32)
    std::string cmd = fmt::format("appLibreConnect_desktop.exe --port {} --address {}", port_str, address);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::vector<char> cmd_buffer(cmd.begin(), cmd.end());
    cmd_buffer.push_back('\0');

    const bool success = CreateProcessA(
        nullptr,
        cmd_buffer.data(),
        nullptr,
        nullptr,
        false,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (success) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        Debug::LogError("Windows failed to start instance. Error: {}", GetLastError());
    }

#else
    pid_t pid = fork();

    if (pid < 0) {
        Debug::LogError("Fork failed!");
        return;
    }

    if (pid == 0) {
        const char* binary = "./appLibreConnect_desktop";
        char* args[] = {
            const_cast<char*>(binary),
            const_cast<char*>("--port"),
            const_cast<char*>(port_str.c_str()),
            const_cast<char*>("--address"),
            const_cast<char*>(address.c_str()),
            nullptr
        };

        execv(binary, args);

        std::cerr << "Failed to execute binary" << std::endl;
        _exit(1);
    } else {
        signal(SIGCHLD, SIG_IGN);
    }
#endif
}

void LibreConnectLogHandler(const QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const QString logMessage = QString("[QT] %1").arg(msg);
    const std::string stdMsg = logMessage.toStdString();

    switch (type) {
    case QtDebugMsg:
        Debug::Log(stdMsg);
        break;
    case QtInfoMsg:
        Debug::Log(stdMsg);
        break;
    case QtWarningMsg:
        Debug::LogWarning(stdMsg);
        break;
    case QtCriticalMsg:
        Debug::LogError(stdMsg);
        break;
    case QtFatalMsg:
        Debug::LogError(stdMsg);
        abort();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("LibreConnect");
    app.setApplicationName("LibreConnect");

    {
        const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/daemon/";
        if (!appDataPath.isEmpty()) {
            QDir().mkpath(appDataPath);
            QDir::setCurrent(appDataPath);

            const Debug::Settings settings{
                .rootPath = appDataPath.toStdString(),
                .maxFileSize = 2 * 1024 * 1024 * 1024ULL,
                .maxLogFilesAmount = 10,
                .deleteLogsAfter = 60 * 60 * 24 * 7
            };

            try {
                Debug::SetSettings(settings);
            } catch (...) {}
        }

        qInstallMessageHandler(LibreConnectLogHandler);
        if (!appDataPath.isEmpty()) {
            std::filesystem::current_path(appDataPath.toStdString());
        }
    }

    LanDeviceScanner::BeginScan(LanDeviceScanner::Options{false, false});
    SignalReceiver::StartReceiving();

    std::vector<uuid> autoConnectDevices{};

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DURATION));

        std::vector<DeviceInfo> devices = LanDeviceScanner::GetDiscoveredDevices();
        std::vector<uuid> connectedDevices = SignalReceiver::GetConnectedDevices();
        LoadDevicesToAutoConnect(autoConnectDevices);

        for (const auto& device : devices) {
            if (std::ranges::find(autoConnectDevices, device.deviceID) == autoConnectDevices.end()) {
                continue;
            }

            if (std::ranges::find(connectedDevices, device.deviceID) != connectedDevices.end()) {
                continue;
            }

            StartInstance(device.deviceAddress, device.deviceAddressPort);
        }
    }
}
