#include <DeviceData.h>
#include <DebugLog.h>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <filesystem>
#include <fstream>

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