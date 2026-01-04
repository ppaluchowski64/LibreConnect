#include "VCamAPI.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <fmt/format.h>
#include <thread>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>

struct VCamInstance {
    int v4l2Device{};
    int videoID{};
    int width{};
    int height{};
    std::string lastError;
};

static std::mutex g_mutex;
static std::map<VCamHandle, std::shared_ptr<VCamInstance>> g_instances;

static void SetError(const std::string_view str, const VCamHandle handle) {
    if (!g_instances.contains(handle)) {
        return;
    }

    g_instances.at(handle)->lastError = str;
}

static bool IsProcessAlive(const pid_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
}

std::vector<std::string> ListVideoDevices() {
    std::vector<std::string> devices;
    for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
        if (entry.path().filename().string().starts_with("video")) {
            devices.push_back(entry.path());
        }
    }
    return devices;
}

static std::string GetVideoLabel(const std::string& device) {
    int fd = open(device.c_str(), O_RDONLY);
    if (fd < 0)
        return {};

    v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        close(fd);
        return {};
    }

    close(fd);
    return reinterpret_cast<char*>(cap.card);
}

static std::string FindDeviceByLabel(const std::string& label) {
    for (const auto& dev : ListVideoDevices()) {
        if (GetVideoLabel(dev) == label) {
            return dev;
        }
    }
    return {};
}

static std::string WaitForDeviceByLabel(const std::string& label, const int timeoutMs = 50) {
    for (int i = 0; i < 20; i++) {
        auto dev = FindDeviceByLabel(label);
        if (!dev.empty())
            return dev;
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    }
    return {};
}

void StartWatcher(int videoID, int fd, pid_t parentPid);

extern "C" {
    VCAMAPI_API VCamResult CreateCam(const char* name, int width, int height, int fps, VCamFormat format, VCamHandle* handle) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::shared_ptr<VCamInstance> instance = std::make_shared<VCamInstance>();

        {
            int fd = shm_open("/video_vcam_counter", O_RDWR | O_CREAT, 0666);
            ftruncate(fd, sizeof(std::atomic<uint16_t>));
            uint16_t nextHandle = static_cast<std::atomic<uint16_t> *>(mmap(nullptr, sizeof(std::atomic<uint32_t>),PROT_READ | PROT_WRITE, MAP_SHARED, fd,0))->fetch_add(1);
            *handle = reinterpret_cast<void*>(static_cast<uint64_t>(nextHandle));
        }

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

        instance->width = width;
        instance->height = height;

        int deviceID = -1;
        std::string deviceName = fmt::format("{}: {}", reinterpret_cast<long long>(*handle), name);

        {
            const std::string cmd = fmt::format("pkexec /usr/libexec/v4l2loopback-helper add \"{}\"", deviceName);

            if (std::system(cmd.c_str()) != 0) {
                SetError("Failed to add v4l2loopback device", handle);
                return VCAM_ERROR_INIT_FAILED;
            }
        }

        {
            const std::string result = WaitForDeviceByLabel(deviceName);

            int i = 0;
            for (; i < result.size(); ++i) {
                if (result[i] >= '0' && result[i] <= '9') { break; }
            }

            std::string parsed = result.substr(i, result.size() - 1);
            deviceID = std::stoi(parsed);
        }

        const std::string device = fmt::format("/dev/video{}", deviceID);
        instance->v4l2Device = open(device.c_str(), O_WRONLY);
        instance->videoID = deviceID;

        StartWatcher(deviceID, instance->v4l2Device, getpid());

        if (instance->v4l2Device < 0) {
            SetError("Failed to open device", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        v4l2_format v4l2_format{};
        v4l2_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        v4l2_format.fmt.pix.width = width;
        v4l2_format.fmt.pix.height = height;

        switch (format) {
            case VCAM_FORMAT_RGB32: v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32; break;
            case VCAM_FORMAT_NV12:  v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;  break;
            case VCAM_FORMAT_BGRA:  v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR32; break;
            case VCAM_FORMAT_YUYV:  v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; break;
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
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_instances.contains(handle)) {
            return VCAM_ERROR_INVALID_PARAM;
        }

        const std::shared_ptr<VCamInstance>& instance = g_instances.at(handle);
        close(instance->v4l2Device);

        {
            const std::string cmd = fmt::format("pkexec /usr/libexec/v4l2loopback-helper remove {}", instance->videoID);

            if (std::system(cmd.c_str()) != 0) {
                SetError("Failed to delete v4l2loopback device", handle);
                return VCAM_ERROR_CAMERA_DESTRUCTION_FAILED;
            }
        }

        const int pid = getpid();
        g_instances.erase(handle);

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(VCamHandle handle, const void* data, const VCamFormat format) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_instances.contains(handle)) {
            return VCAM_ERROR_INVALID_PARAM;
        }

        const std::shared_ptr<VCamInstance>& instance = g_instances.at(handle);

        int size = 0;
        switch (format) {
            case VCAM_FORMAT_RGB32:
                size = instance->width * instance->height * 4;
                break;
            case VCAM_FORMAT_BGRA:
                size = instance->width * instance->height * 4;
                break;
            case VCAM_FORMAT_NV12:
                size = instance->width * instance->height * 3 / 2;
                break;
            case VCAM_FORMAT_YUYV:
                size = instance->width * instance->height * 2;
        }

        if (write(instance->v4l2Device, data, size) < 0) {
            SetError("Failed to push frame", handle);
            return VCAM_ERROR_FRAME_PUSH_FAILED;
        }

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

void StartWatcher(const int videoID, const int fd, const pid_t parentPid) {
    const pid_t pid = fork();

    if (pid != 0) {
        return;
    }

    setsid();

    while (true) {
        if (kill(parentPid, 0) == -1) {
            close(fd);
            const std::string cmd = fmt::format("pkexec /usr/libexec/v4l2loopback-helper remove {}", videoID);
            std::system(cmd.c_str());
            _exit(0);
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}