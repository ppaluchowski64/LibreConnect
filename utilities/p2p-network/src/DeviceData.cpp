#include <DeviceData.h>
#include <DebugLog.h>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <filesystem>
#include <fstream>

#include <QStandardPaths>
#include <QDir>

#ifdef ANDROID_DEVICE
#include <AndroidContextProvider.h>
#include <QJniObject>
#endif

namespace
{
#if defined(ANDROID_DEVICE)
QString AndroidFilesDir()
{
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return {};
    }

    const QJniObject filesDir = context.callObjectMethod(
        "getFilesDir",
        "()Ljava/io/File;"
    );
    if (!filesDir.isValid()) {
        return {};
    }

    return filesDir.callObjectMethod(
        "getAbsolutePath",
        "()Ljava/lang/String;"
    ).toString();
}
#endif
}

boost::uuids::uuid DeviceData::GetDeviceUUID() {
    std::string uuidFile = "uuid.bin";

#if defined(MOBILE_DEVICE)
    QString base;
#if defined(ANDROID_DEVICE)
    base = AndroidFilesDir();
#endif
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (!base.isEmpty()) {
        QDir().mkpath(base);
        uuidFile = (base + "/uuid.bin").toStdString();
    }
#endif

    try {
        if (!std::filesystem::exists(uuidFile)) {
            boost::uuids::uuid uuid = boost::uuids::random_generator()();

            std::ofstream output(uuidFile, std::ios::out | std::ios::trunc);
            if (!output.is_open()) {
                Debug::LogError("Failed to open " + uuidFile + " for writing");
                throw std::runtime_error("Failed to open " + uuidFile + " for writing");
            }

            output << boost::uuids::to_string(uuid);
            output.close();
            return uuid;
        }

        std::ifstream input(uuidFile, std::ios::in);
        if (!input.is_open()) {
            Debug::LogError("Failed to open " + uuidFile + " for reading");
            throw std::runtime_error("Failed to open " + uuidFile + " for reading");
        }

        std::string uuidString;
        std::getline(input, uuidString);
        input.close();

        static boost::uuids::string_generator generator;
        return generator(uuidString);

    } catch (const std::exception& e) {
        Debug::LogError(std::string("Error in GetDeviceUUID: ") + e.what());
        throw;
    }
}
