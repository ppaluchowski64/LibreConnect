#include "VCamAPI.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>
#include <DebugLog.h>
#include <nlohmann/json.hpp>

#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <csignal>
#include <sys/stat.h>
#include <linux/videodev2.h>

struct VCamInstance {
    int v4l2Device{};
    std::string lastError;
};

static std::mutex g_mutex;
static std::map<VCamHandle, std::shared_ptr<VCamInstance>> g_instances;
static std::atomic<uint64_t> g_nextHandle{1};

static void SetError(const std::string_view str, const VCamHandle handle) {
    if (!g_instances.contains(handle)) {
        return;
    }

    g_instances.at(handle)->lastError = str;
}

struct VirtualCameraData {
    std::string name;
    uint16_t cameraID;
    bool locked;
};

static bool IsProcessAlive(const pid_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
}

static bool videoExists(const int number) {
    const std::string path = "/dev/video" + std::to_string(number);
    struct stat buffer{};
    return (stat(path.c_str(), &buffer) == 0);
}

static int findFreeVideoNr(const int start = 10, const int max = 1000) {
    for (int i = start; i <= max; ++i) {
        if (!videoExists(i))
            return i;
    }
    return -1;
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
    VCAMAPI_API VCamResult CreateCam(const char* name, int width, int height, int fps, VCamFormat format, VCamHandle* handle) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::shared_ptr<VCamInstance> instance = std::make_shared<VCamInstance>();

        *handle = reinterpret_cast<void*>(g_nextHandle++);
        g_instances[*handle] = instance;

        if (fps <= 0 || fps > 240) {
            SetError("Invalid FPS value", *handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        if (width <= 0 || width > 8192) {
            SetError("Invalid width value", *handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        if (height <= 0 || height > 8192) {
            SetError("Invalid height value", *handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        const std::vector<VirtualCameraData> configs = LoadConfig();
        bool createNewCamera = true;
        int deviceID = -1;

        for (const auto& config : configs) {
            if (config.name != name) {
                continue;
            }

            if (config.locked) {
                SetError("Camera with that name already exists", *handle);
                return VCAM_ERROR_CAMERA_EXISTS;
            }

            createNewCamera = false;
            deviceID = config.cameraID;

            break;
        }

        if (createNewCamera) {
            deviceID = findFreeVideoNr(10, 50000);
            const std::string command = fmt::format("modprobe v4l2loopback devices=1 video_nr={} card_label=\"{}\" exclusive_caps=1", deviceID, name);
            const int status = system(command.c_str());

            if (status == -1) {
                SetError("Failed to execute system()", *handle);
                return VCAM_ERROR_INIT_FAILED;
            }

            const int exitCode = WEXITSTATUS(status);
            if (exitCode != 0) {
                SetError(fmt::format("Modprobe failed with code: {}", exitCode), *handle);
                return VCAM_ERROR_INIT_FAILED;
            }

            nlohmann::json config;
            config["name"] = name;
            config["cameraID"] = deviceID;

            const std::filesystem::path configDir{fmt::format("VirtualCameraConfigs/config_{}", deviceID)};
            std::ofstream configFileStream(configDir);

            if (!configFileStream) {
                SetError("Failed to open config file", *handle);
                return VCAM_ERROR_INIT_FAILED;
            }

            configFileStream << config;
        }

        if (!videoExists(deviceID)) {
            SetError("v4l2loopback did not create device", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        const std::string device = fmt::format("/dev/video{}", deviceID);
        instance->v4l2Device = open(device.c_str(), O_RDWR | O_CLOEXEC);

        if (instance->v4l2Device < 0) {
            SetError("Failed to open device", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        v4l2_format v4l2_format{};
        v4l2_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        v4l2_format.fmt.pix.field = V4L2_FIELD_NONE;
        v4l2_format.fmt.pix.width = width;
        v4l2_format.fmt.pix.height = height;

        switch (format) {
            case VCAM_FORMAT_RGB32: v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32; break;
            case VCAM_FORMAT_NV12:  v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;  break;
            case VCAM_FORMAT_BGRA:  v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR32; break;
        }

        if (ioctl(instance->v4l2Device, VIDIOC_S_FMT, &v4l2_format) < 0) {
            close(instance->v4l2Device);
            SetError("Failed to set format", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        if (ioctl(instance->v4l2Device, VIDIOC_G_FMT, &v4l2_format) < 0) {
            close(instance->v4l2Device);
            SetError("Failed to set format", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

        if (ioctl(instance->v4l2Device, VIDIOC_G_PARM, &parm) < 0) {
            close(instance->v4l2Device);
             SetError("Failed to set parms", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        parm.parm.output.timeperframe.numerator = 1;
        parm.parm.output.timeperframe.denominator = fps;

        if (ioctl(instance->v4l2Device, VIDIOC_S_PARM, &parm) < 0) {
            close(instance->v4l2Device);
            SetError("Failed to set parms", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult DestroyCam(VCamHandle handle) {

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(VCamHandle handle, const void* data, const VCamFormat format) {

        return VCAM_SUCCESS;
    }

    VCAMAPI_API const char* VCamGetLastError(VCamHandle handle) {
        std::lock_guard<std::mutex> guard(g_mutex);
        if (!g_instances.contains(handle)) {
            return "";
        }

        return g_instances.at(handle)->lastError.c_str();
    }
}