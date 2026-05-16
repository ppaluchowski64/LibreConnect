#include "VCamAPI.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cerrno>
#include <set>
#include <unordered_set>
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
    VCamFormat format{};
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

static int RunCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return 1;
    }

    std::vector<char*> execArgs;
    execArgs.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        execArgs.push_back(const_cast<char*>(arg.c_str()));
    }
    execArgs.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        return 1;
    }

    if (pid == 0) {
        execvp(execArgs[0], execArgs.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }
        return 1;
    }

    if (!WIFEXITED(status)) {
        return 1;
    }

    return WEXITSTATUS(status);
}

static bool RemoveLoopbackDevice(const int videoID) {
    return RunCommand({
        "pkexec",
        "/usr/libexec/v4l2loopback-helper",
        "remove",
        std::to_string(videoID)
    }) == 0;
}

static bool TryRemoveLoopbackDeviceWithRetry(const int videoID, const int attempts, const std::chrono::milliseconds delay) {
    for (int i = 0; i < attempts; ++i) {
        if (RemoveLoopbackDevice(videoID)) {
            return true;
        }

        if (i + 1 < attempts) {
            std::this_thread::sleep_for(delay);
        }
    }

    return false;
}

static std::vector<std::string> FindDeviceUsers(const int videoID) {
    std::vector<std::string> users;
    std::unordered_set<int> seenPids;
    const std::string targetPath = fmt::format("/dev/video{}", videoID);

    std::error_code ec;
    for (const auto& procEntry : std::filesystem::directory_iterator("/proc", ec)) {
        if (ec) {
            break;
        }

        if (!procEntry.is_directory()) {
            continue;
        }

        const std::string pidStr = procEntry.path().filename().string();
        if (pidStr.empty() || !std::ranges::all_of(pidStr, [](const unsigned char ch) { return std::isdigit(ch) != 0; })) {
            continue;
        }

        int pid = 0;
        try {
            pid = std::stoi(pidStr);
        } catch (...) {
            continue;
        }

        std::error_code fdEc;
        const auto fdDir = procEntry.path() / "fd";
        for (const auto& fdEntry : std::filesystem::directory_iterator(fdDir, fdEc)) {
            if (fdEc) {
                break;
            }

            std::error_code linkEc;
            const auto linkTarget = std::filesystem::read_symlink(fdEntry.path(), linkEc);
            if (linkEc) {
                continue;
            }

            if (linkTarget.string() != targetPath) {
                continue;
            }

            if (seenPids.contains(pid)) {
                break;
            }

            std::string command = "unknown";
            std::ifstream commFile(procEntry.path() / "comm");
            if (std::getline(commFile, command) && !command.empty()) {
                // Keep parsed command.
            } else {
                command = "unknown";
            }

            users.push_back(fmt::format("{}({})", command, pid));
            seenPids.insert(pid);
            break;
        }
    }

    return users;
}

static std::string JoinStrings(const std::vector<std::string>& values, const std::string_view separator) {
    std::string joined;
    for (size_t i = 0; i < values.size(); ++i) {
        joined += values[i];
        if (i + 1 < values.size()) {
            joined += separator;
        }
    }
    return joined;
}

static void ScheduleDeferredRemove(const int videoID) {
    std::thread([videoID]() {
        (void)TryRemoveLoopbackDeviceWithRetry(videoID, 150, std::chrono::seconds(2));
    }).detach();
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
    const int fd = open(device.c_str(), O_RDONLY);
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

        instance->format = format;
        instance->width = width;
        instance->height = height;

        int deviceID = -1;
        std::string deviceName(name);

        {
            constexpr size_t MAX_DEVICE_NAME_LENGTH = 128;
            if (deviceName.empty() || deviceName.size() > MAX_DEVICE_NAME_LENGTH) {
                SetError("Invalid device name length", *handle);
                return VCAM_ERROR_INIT_FAILED;
            }

            const std::set<char> whiteList = {
                'a','b','c','d','e','f','g','h','i','j','k','l','m',
                'n','o','p','q','r','s','t','u','v','w','x','y','z',
                'A','B','C','D','E','F','G','H','I','J','K','L','M',
                'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
                '0','1','2','3','4','5','6','7','8','9',
                ' ', '-', '_', ':', '.', '(', ')', '\''
            };

            for (char c : deviceName) {
                if (!whiteList.contains(c)) {
                    SetError(fmt::format("Invalid character in device name (char: '{}')", c), *handle);
                    return VCAM_ERROR_INIT_FAILED;
                }
            }
        }

        {
            if (RunCommand({"pkexec", "/usr/libexec/v4l2loopback-helper", "add", deviceName}) != 0) {
                SetError("Failed to add v4l2loopback device", *handle);
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

        v4l2_format.fmt.pix.field = V4L2_FIELD_NONE;
        v4l2_format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

        v4l2_format.fmt.pix.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
        v4l2_format.fmt.pix.quantization = V4L2_QUANTIZATION_DEFAULT;
        v4l2_format.fmt.pix.xfer_func = V4L2_XFER_FUNC_DEFAULT;

        switch (format) {
            case VCAM_FORMAT_RGB32:
                v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
                v4l2_format.fmt.pix.bytesperline = width * 4;
                v4l2_format.fmt.pix.sizeimage = width * height * 4;
                break;
            case VCAM_FORMAT_NV12:
                v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
                v4l2_format.fmt.pix.bytesperline = width;
                v4l2_format.fmt.pix.sizeimage = width * height * 3 / 2;
                break;
            case VCAM_FORMAT_BGRA:
                v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR32;
                v4l2_format.fmt.pix.bytesperline = width * 4;
                v4l2_format.fmt.pix.sizeimage = width * height * 4;
                break;
            case VCAM_FORMAT_YUYV:
                v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
                v4l2_format.fmt.pix.bytesperline = width * 2;
                v4l2_format.fmt.pix.sizeimage = width * height * 2;
                break;
            case VCAM_FORMAT_YUV420:
                v4l2_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
                v4l2_format.fmt.pix.bytesperline = width;
                v4l2_format.fmt.pix.sizeimage = width * height * 3 / 2;
                break;
        }

        if (ioctl(instance->v4l2Device, VIDIOC_S_FMT, &v4l2_format) < 0) {
            close(instance->v4l2Device);
            SetError("Failed to set format", *handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        // v4l2_std_id stdid = V4L2_STD_625_50;
        // ioctl(instance->v4l2Device, VIDIOC_S_STD, &stdid);
        //
        // int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        // ioctl(instance->v4l2Device, VIDIOC_STREAMON, &type);
        // ioctl(instance->v4l2Device, VIDIOC_STREAMOFF, &type);

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

        // std::vector<uint8_t> blackFrame(v4l2_format.fmt.pix.sizeimage, 0);
        // write(instance->v4l2Device, blackFrame.data(), blackFrame.size());

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult DestroyCam(const VCamHandle handle) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_instances.contains(handle)) {
            return VCAM_ERROR_INVALID_PARAM;
        }

        const std::shared_ptr<VCamInstance>& instance = g_instances.at(handle);
        const int videoID = instance->videoID;
        close(instance->v4l2Device);

        if (!TryRemoveLoopbackDeviceWithRetry(videoID, 10, std::chrono::milliseconds(200))) {
            const std::vector<std::string> users = FindDeviceUsers(videoID);

            std::string message = fmt::format(
                "v4l2loopback device /dev/video{} is busy; deferred removal scheduled",
                videoID
            );

            if (!users.empty()) {
                message += fmt::format(" (close camera in: {})", JoinStrings(users, ", "));
            }

            SetError(message, handle);
            std::cerr << message << '\n';
            ScheduleDeferredRemove(videoID);
        }

        g_instances.erase(handle);

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(const VCamHandle handle, const void* data) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_instances.contains(handle)) {
            return VCAM_ERROR_INVALID_PARAM;
        }

        const std::shared_ptr<VCamInstance>& instance = g_instances.at(handle);

        int size = 0;
        switch (instance->format) {
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
                break;
            case VCAM_FORMAT_YUV420:
                size = instance->width * instance->height * 3 / 2;
                break;
        }

        if (write(instance->v4l2Device, data, size) < 0) {
            SetError("Failed to push frame", handle);
            return VCAM_ERROR_FRAME_PUSH_FAILED;
        }

        return VCAM_SUCCESS;
    }

    VCAMAPI_API const char* VCamGetLastError(const VCamHandle handle) {
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
            (void)RemoveLoopbackDevice(videoID);
            _exit(0);
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
