#include <Scanner.h>
#include <DaemonServer.h>
#include <AddressResolver.h>

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
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
#include <QLockFile>
#include <map>
#include <set>

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
            try {
                devices.push_back(gen(item.get<std::string>()));
            } catch (const std::exception&) {
                continue;
            }
        }
    } catch (...) {
        devices.clear();
    }
}

void StartInstance(const std::string& address, const uint16_t port, const uuid& deviceId) {
    QString error;
    if (!DaemonUtils::StartApplication(QString::fromStdString(address), port,
            QString::fromStdString(boost::uuids::to_string(deviceId)), &error)) {
        Debug::LogError("Failed to start LibreConnect for auto-connect: {}", error.toStdString());
    } else {
        Debug::Log("Started LibreConnect for auto-connect");
    }
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

    QLockFile instanceLock(QDir::current().filePath(QStringLiteral("daemon.lock")));
    if (!instanceLock.tryLock()) {
        return 0;
    }

    auto daemonServer = std::make_shared<DaemonServer>();
    if (!daemonServer->Start()) {
        return 1;
    }
    LanDeviceScanner::BeginScan(LanDeviceScanner::Options{false, false});

    std::vector<uuid> autoConnectDevices{};
    std::map<uuid, std::chrono::steady_clock::time_point> lastAttempts;
    std::set<std::string> networkAddresses;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DURATION));

        std::set<std::string> currentAddresses;
        for (const IPAddress& address : AddressResolver::GetAllPrivateIPv4()) {
            currentAddresses.insert(address.to_string());
        }
        if (currentAddresses != networkAddresses) {
            networkAddresses = std::move(currentAddresses);
            LanDeviceScanner::RestartScan();
        }

        std::vector<DeviceInfo> devices = LanDeviceScanner::GetDiscoveredDevices();
        std::vector<uuid> connectedDevices = daemonServer->GetConnectedDevices();
        LoadDevicesToAutoConnect(autoConnectDevices);

        for (const auto& device : devices) {
            if (std::ranges::find(autoConnectDevices, device.deviceID) == autoConnectDevices.end()) {
                continue;
            }

            if (std::ranges::find(connectedDevices, device.deviceID) != connectedDevices.end()) {
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            const auto attempt = lastAttempts.find(device.deviceID);
            if (attempt != lastAttempts.end() && now - attempt->second < std::chrono::seconds(30)) {
                continue;
            }
            lastAttempts[device.deviceID] = now;
            StartInstance(device.deviceAddress, device.deviceAddressPort, device.deviceID);
        }
    }
}
