#include <Scanner.h>
#include <SignalReceiver.h>

#include <boost/uuid/uuid_generators.hpp>
#include <nlohmann/json.hpp>

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
    // TODO
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
