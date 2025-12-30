#include "VCamAPI.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <DebugLog.h>
#include <nlohmann/json.hpp>

#include <unistd.h>
#include <csignal>

struct VirtualCameraData {
    std::string name;
    uint16_t cameraID;
    bool locked;
};

static bool IsProcessAlive(const pid_t pid) {
    return kill(pid, 0) == 0;
}

static std::vector<VirtualCameraData> LoadConfig() {
    const std::filesystem::path configDir{"VirtualCameraConfigs"};
    const std::string lockedFilePrefix{"lock_"};

    std::error_code ec{};
    std::filesystem::create_directories(configDir, ec);

    if (ec) {
        Debug::LogError("VirtualCamera::LoadConfig create directories error: {}", ec.message());
        return {};
    }

    std::vector<VirtualCameraData> configs;

    for (const auto& entry : std::filesystem::directory_iterator(configDir, ec)) {
        if (ec) {
            Debug::LogError("VirtualCamera::LoadConfig Directory iteration error: {}", ec.message());
            break;
        }

        if (entry.is_directory()) {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file) {
            Debug::LogError("VirtualCamera::LoadConfig Failed to open {}", entry.path().string());
            continue;
        }

        const std::string filename = entry.path().filename();

        if (filename.starts_with(lockedFilePrefix)) {
            continue;
        }

        nlohmann::json config;
        try {
            file >> config;
        } catch (const std::exception& e) {
            Debug::LogError("VirtualCamera::LoadConfig JSON parse failed for {}: {}", entry.path().string(), e.what());
            continue;
        }

        bool valid =
            config.contains("name") &&
            config["name"].is_string() &&
            config.contains("cameraID") &&
            config["cameraID"].is_number_unsigned();

        if (!valid) {
            std::filesystem::remove(entry.path(), ec);
            if (ec) {
                Debug::LogError("VirtualCamera::LoadConfig file remove error: {}", ec.message());
            }

            continue;
        }

        const std::filesystem::path lockPath = entry.path().parent_path() / (lockedFilePrefix + filename);
        bool locked = std::filesystem::exists(lockPath);
        if (locked) {
            std::ifstream lockFile(lockPath);
            if (!lockFile) {
                Debug::LogError("VirtualCamera::LoadConfig Failed to open {}", lockPath.string());
                continue;
            }

            pid_t pid{};
            lockFile >> pid;

            locked = IsProcessAlive(pid);
        }

        configs.emplace_back(
            config.at("name").get<std::string>(),
            config.at("cameraID").get<uint16_t>(),
            locked
        );
    }

    return configs;
}

extern "C" {
    VCAMAPI_API VCamResult CreateCam(const char* name, int width, int height, int fps, VCamHandle* handle) {

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult DestroyCam(VCamHandle handle) {

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(const VCamHandle handle, const void* data, const VCamFormat format) {

        return VCAM_SUCCESS;
    }

    VCAMAPI_API const char* VCamGetLastError(VCamHandle handle) {

        return "";
    }
}