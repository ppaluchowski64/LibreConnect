#include <DeviceData.h>
#include <DebugLog.h>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <filesystem>
#include <fstream>

std::mutex DeviceData::m_mutex{};
SQLite::Database DeviceData::m_dataBase{"DeviceData.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
std::once_flag DeviceData::m_initFlag{};

boost::uuids::uuid DeviceData::GetDeviceUUID() {
    const std::string uuidFile = "uuid.bin";

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

        try {
            std::string uuidString;
            std::getline(input, uuidString);
            input.close();

            static boost::uuids::string_generator generator;
            return generator(uuidString);
        } catch (const std::exception& e) {
            std::filesystem::remove(uuidFile);
            return GetDeviceUUID();
        }
    }
    catch (const std::exception& e) {
        Debug::LogError(std::string("Error in GetDeviceUUID: ") + e.what());
        throw;
    }
}

bool DeviceData::IsDevicePaired(const boost::uuids::uuid uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::call_once(m_initFlag, []() {
        Init();
    });

    try {
        SQLite::Statement query(m_dataBase, "SELECT COUNT(*) FROM paired_devices WHERE uuid = ?");
        query.bind(1, boost::uuids::to_string(uuid));

        if (query.executeStep()) {
            return query.getColumn(0).getInt() > 0;
        }
    } catch (const std::exception& exception) {
        Debug::LogError("Error in IsDevicePaired: {}", exception.what());
    }

    return false;
}

void DeviceData::AddPairedDevice(const DeviceInfo& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::call_once(m_initFlag, []() {
        Init();
    });

    try {
        SQLite::Statement query(m_dataBase, "INSERT OR REPLACE INTO paired_devices (uuid, name) VALUES (?, ?)");
        query.bind(1, boost::uuids::to_string(data.deviceID));
        query.bind(2, data.deviceName);
        query.exec();
    } catch (const std::exception& exception) {
        Debug::LogError("Error in AddPairedDevice: {}", exception.what());
    }
}

void DeviceData::RemovePairedDevice(const boost::uuids::uuid uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::call_once(m_initFlag, []() {
        Init();
    });

    try {
        SQLite::Statement query(m_dataBase, "DELETE FROM paired_devices WHERE uuid = ?");
        query.bind(1, boost::uuids::to_string(uuid));
        query.exec();
    } catch (const std::exception& exception) {
        Debug::LogError("Error in RemovePairedDevice: {}", exception.what());
    }
}

std::vector<DeviceInfo> DeviceData::GetPairedDevices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::call_once(m_initFlag, []() {
        Init();
    });

    std::vector<DeviceInfo> result;

    try {
        SQLite::Statement query(m_dataBase, "SELECT uuid, name FROM paired_devices");
        static boost::uuids::string_generator generator;

        while (query.executeStep()) {
            DeviceInfo data;
            data.deviceID = generator(query.getColumn(0).getString());
            data.deviceName = query.getColumn(1).getString();
            result.push_back(std::move(data));
        }

        return result;
    } catch (const std::exception& exception) {
        Debug::LogError("Error in GetPairedDevices: {}", exception.what());
    }

    return {};
}

void DeviceData::Init() {
    try {
        m_dataBase.exec("CREATE TABLE IF NOT EXISTS paired_devices (uuid TEXT PRIMARY KEY, name TEXT)");
    } catch (const SQLite::Exception& exception) {
        Debug::LogError("Failed to create table: {}",  exception.what());
    }
}
