#include <cstring>
#include <mutex>
#include <string>
#include <atomic>
#include <vector>
#include <VMicAPI.h>

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>

constexpr size_t MAX_VMIC_DEVICES = 16;

struct VMicDevice {
    std::string id{};
    std::string name{};
    uint32_t bufferSize{};
    bool isUsed{false};
    bool initialized{false};
};

struct VMicContext {
    std::string deviceId;
    std::string deviceName;
    VMicFormat format{};

    std::vector<uint8_t> ringBuffer;
    std::atomic<size_t> writeHead{0};
    std::atomic<size_t> readHead{0};
    size_t bufferCapacityBytes{0};

    std::string lastError{};

    pw_thread_loop* pwLoop = nullptr;
    pw_context* pwContext = nullptr;
    pw_core* pwCore = nullptr;
    pw_stream* pwStream = nullptr;
    spa_hook pwStreamListener{};
};


static VMicDevice g_devices[MAX_VMIC_DEVICES];
static bool g_isInitialized = false;
static std::mutex g_initializationMutex{};

static void onProcess(void *userdata) {
    VMicContext* context = static_cast<VMicContext*>(userdata);
    pw_buffer* b = pw_stream_dequeue_buffer(context->pwStream);
    if (!b) return;

    const spa_buffer* buf = b->buffer;
    uint8_t* dst = static_cast<uint8_t*>(buf->datas[0].data);
    if (!dst) return;

#if PW_CHECK_VERSION(0, 3, 50)
    const uint32_t reqFrames = b->requested ? b->requested : 1024;
#else
    constexpr uint32_t reqFrames = 1024;
#endif

    const uint32_t bytesPerFrame = context->format.channels * (context->format.bitDepth / 8);
    const uint32_t reqBytes = reqFrames * bytesPerFrame;

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = bytesPerFrame;
    buf->datas[0].chunk->size = reqBytes;

    const size_t writeHead = context->writeHead.load(std::memory_order_acquire);
    const size_t readHead = context->readHead.load(std::memory_order_relaxed);
    const size_t availableBytes = writeHead - readHead;

    if (availableBytes >= reqBytes) {
        const size_t readIndex = readHead % context->bufferCapacityBytes;
        const size_t bytesToEnd = context->bufferCapacityBytes - readIndex;

        if (reqBytes <= bytesToEnd) {
            std::memcpy(dst, &context->ringBuffer[readIndex], reqBytes);
        } else {
            std::memcpy(dst, &context->ringBuffer[readIndex], bytesToEnd);
            std::memcpy(dst + bytesToEnd, &context->ringBuffer[0], reqBytes - bytesToEnd);
        }
        context->readHead.store(readHead + reqBytes, std::memory_order_release);
    } else {
        std::memset(dst, 0, reqBytes);
    }

    pw_stream_queue_buffer(context->pwStream, b);
}

static constexpr pw_stream_events stream_events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = onProcess
};

VMIC_API VMicResult VMic_Initialize() {
    std::lock_guard<std::mutex> lock(g_initializationMutex);
    if (g_isInitialized) {
        return VMIC_ERROR_DEVICE_ALREADY_INITIALIZED;
    }

    for (auto& device : g_devices) {
        device.initialized = false;
        device.isUsed = false;
        device.id.clear();
        device.name.clear();
        device.bufferSize = 0;
    }

    g_isInitialized = true;
    return VMIC_SUCCESS;
}

VMIC_API void VMic_Shutdown() {
    std::lock_guard<std::mutex> lock(g_initializationMutex);
    if (!g_isInitialized) return;

    for (auto& device : g_devices) {
        device.initialized = false;
        device.isUsed = false;
        device.id.clear();
        device.name.clear();
        device.bufferSize = 0;
    }

    g_isInitialized = false;
}

VMIC_API VMicResult VMic_GetAvailableDevices(VMicDeviceInfo* devices, uint32_t* count) {
    std::lock_guard<std::mutex> lock(g_initializationMutex);

    if (!count) {
        return VMIC_ERROR_INVALID_PARAMETER;
    }

    uint32_t availableCount = 0;
    for (const auto& device : g_devices) {
        if (device.initialized && !device.isUsed) {
            availableCount++;
        }
    }

    if (devices == nullptr) {
        *count = availableCount;
        return VMIC_SUCCESS;
    }

    const uint32_t limit = (*count < availableCount) ? *count : availableCount;
    uint32_t offset = 0;

    for (const auto& device : g_devices) {
        if (offset >= limit) break;

        if (device.initialized && !device.isUsed) {
            std::strncpy(devices[offset].id, device.id.c_str(), sizeof(devices[offset].id) - 1);
            devices[offset].id[sizeof(devices[offset].id) - 1] = '\0';

            std::strncpy(devices[offset].name, device.name.c_str(), sizeof(devices[offset].name) - 1);
            devices[offset].name[sizeof(devices[offset].name) - 1] = '\0';

            offset++;
        }
    }

    *count = offset;
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_CreateDevice(const char* deviceName, char* deviceId, const uint32_t bufferSize) {
    std::lock_guard<std::mutex> lock(g_initializationMutex);

    if (deviceName == nullptr || deviceId == nullptr || bufferSize == 0) {
        return VMIC_ERROR_INVALID_PARAMETER;
    }

    int slot = -1;
    for (int i = 0; i < MAX_VMIC_DEVICES; i++) {
        if (!g_devices[i].initialized) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        return VMIC_ERROR_INIT_FAILED;
    }

    VMicDevice& device = g_devices[slot];
    device.name = deviceName;
    device.bufferSize = bufferSize;
    device.id = "vmic_dev_" + std::to_string(slot);
    device.initialized = true;
    device.isUsed = false;

    std::strncpy(deviceId, device.id.c_str(), 255);
    deviceId[255] = '\0';

    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_DestroyDevice(const char* deviceId) {
    std::lock_guard<std::mutex> lock(g_initializationMutex);

    if (deviceId == nullptr) {
        return VMIC_ERROR_INVALID_PARAMETER;
    }

    for (auto& device : g_devices) {
        if (device.initialized && device.id == deviceId) {
            device.initialized = false;
            device.isUsed = false;
            device.id.clear();
            device.name.clear();
            device.bufferSize = 0;
            return VMIC_SUCCESS;
        }
    }

    return VMIC_ERROR_DEVICE_NOT_FOUND;
}

VMIC_API VMicResult VMic_OpenDevice(VMicHandle* handle, const char* deviceId, const VMicFormat* format) {
    std::lock_guard<std::mutex> lock(g_initializationMutex);

    if (!handle || !deviceId || !format) {
        return VMIC_ERROR_INVALID_PARAMETER;
    }

    VMicDevice* targetDevice = nullptr;
    for (auto& device : g_devices) {
        if (device.initialized && device.id == deviceId) {
            targetDevice = &device;
            break;
        }
    }

    if (!targetDevice) {
        return VMIC_ERROR_DEVICE_NOT_FOUND;
    }

    if (targetDevice->isUsed) {
        return VMIC_ERROR_INIT_FAILED;
    }

    VMicContext* context = new VMicContext();
    context->deviceId = targetDevice->id;
    context->deviceName = targetDevice->name;
    context->format = *format;

    const uint32_t bytesPerSample = format->bitDepth / 8;
    context->bufferCapacityBytes = targetDevice->bufferSize * format->channels * bytesPerSample;
    context->ringBuffer.resize(context->bufferCapacityBytes, 0);

    targetDevice->isUsed = true;
    *handle = context;

    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_Close(VMicHandle handle) {
    std::lock_guard<std::mutex> lock(g_initializationMutex);

    if (!handle) {
        return VMIC_ERROR_INVALID_HANDLE;
    }

    const VMicContext* context = static_cast<VMicContext*>(handle);

    for (auto& device : g_devices) {
        if (device.initialized && device.id == context->deviceId) {
            device.isUsed = false;
            break;
        }
    }

    if (context->pwLoop) {
        VMic_StopStream(handle);
    }

    delete context;
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_StartStream(VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;
    VMicContext* context = static_cast<VMicContext*>(handle);

    pw_init(nullptr, nullptr);

    context->pwLoop = pw_thread_loop_new("vmic_thread", nullptr);
    if (!context->pwLoop) return VMIC_ERROR_INIT_FAILED;

    context->pwContext = pw_context_new(pw_thread_loop_get_loop(context->pwLoop), nullptr, 0);

    pw_thread_loop_lock(context->pwLoop);

    context->pwCore = pw_context_connect(context->pwContext, nullptr, 0);
    if (!context->pwCore) {
        context->lastError = "Failed to connect to PipeWire daemon.";
        pw_thread_loop_unlock(context->pwLoop);
        return VMIC_ERROR_INIT_FAILED;
    }

    pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_NODE_NAME, context->deviceId.c_str(),
        PW_KEY_NODE_DESCRIPTION, context->deviceName.empty() ? "Virtual Microphone" : context->deviceName.c_str(),
        PW_KEY_NODE_NICK, context->deviceName.empty() ? "Virtual Mic" : context->deviceName.c_str(),
        PW_KEY_MEDIA_CLASS, "Audio/Source",
        "node.always-process", "true",
        "node.pause-on-idle", "false",
        "node.passive", "false",
        "node.virtual", "true",
        "media.icon-name", "audio-input-microphone",
        nullptr
    );

    context->pwStream = pw_stream_new(context->pwCore, "vmic_stream", props);
    pw_stream_add_listener(context->pwStream, &context->pwStreamListener, &stream_events, context);

    spa_audio_format spaFmt = SPA_AUDIO_FORMAT_S16;
    if (context->format.bitDepth == 24) spaFmt = SPA_AUDIO_FORMAT_S24;
    else if (context->format.bitDepth == 32) spaFmt = SPA_AUDIO_FORMAT_F32;

    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod* params[1];

    spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(
        .format = spaFmt,
        .rate = context->format.sampleRate,
        .channels = context->format.channels
    );

    params[0] = static_cast<const spa_pod*>(spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info));

    pw_stream_connect(context->pwStream,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    pw_thread_loop_unlock(context->pwLoop);
    pw_thread_loop_start(context->pwLoop);

    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_StopStream(VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;
    VMicContext* context = static_cast<VMicContext*>(handle);

    if (context->pwLoop) {
        pw_thread_loop_stop(context->pwLoop);
        pw_thread_loop_lock(context->pwLoop);

        if (context->pwStream) pw_stream_destroy(context->pwStream);
        if (context->pwCore) pw_core_disconnect(context->pwCore);
        if (context->pwContext) pw_context_destroy(context->pwContext);

        pw_thread_loop_unlock(context->pwLoop);
        pw_thread_loop_destroy(context->pwLoop);

        context->pwStream = nullptr;
        context->pwCore = nullptr;
        context->pwContext = nullptr;
        context->pwLoop = nullptr;

        pw_deinit();
    }
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_GetAvailableSpace(VMicHandle handle, uint32_t* availableSamples) {
    if (!handle || !availableSamples) return VMIC_ERROR_INVALID_PARAMETER;
    const VMicContext* context = static_cast<VMicContext*>(handle);

    const size_t writeHead = context->writeHead.load(std::memory_order_relaxed);
    const size_t readHead = context->readHead.load(std::memory_order_acquire);

    const size_t usedBytes = writeHead - readHead;
    const size_t availableBytes = context->bufferCapacityBytes - usedBytes;

    const uint32_t bytesPerFrame = context->format.channels * (context->format.bitDepth / 8);
    *availableSamples = static_cast<uint32_t>(availableBytes / bytesPerFrame);

    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_PushSamples(VMicHandle handle, const void* samples, const uint32_t sampleCount) {
    if (!handle || !samples) return VMIC_ERROR_INVALID_PARAMETER;
    VMicContext* context = static_cast<VMicContext*>(handle);

    const uint32_t bytesPerFrame = context->format.channels * (context->format.bitDepth / 8);
    const size_t bytesToWrite = sampleCount * bytesPerFrame;

    const size_t writeHead = context->writeHead.load(std::memory_order_relaxed);
    const size_t readHead = context->readHead.load(std::memory_order_acquire);

    const size_t usedBytes = writeHead - readHead;
    const size_t availableBytes = context->bufferCapacityBytes - usedBytes;

    if (bytesToWrite > availableBytes) {
        context->lastError = "Buffer full. Insufficient space to push samples.";
        return VMIC_ERROR_BUFFER_FULL;
    }

    const uint8_t* src = static_cast<const uint8_t*>(samples);
    const size_t writeIndex = writeHead % context->bufferCapacityBytes;
    const size_t bytesToEnd = context->bufferCapacityBytes - writeIndex;

    if (bytesToWrite <= bytesToEnd) {
        std::memcpy(&context->ringBuffer[writeIndex], src, bytesToWrite);
    } else {
        std::memcpy(&context->ringBuffer[writeIndex], src, bytesToEnd);
        std::memcpy(&context->ringBuffer[0], src + bytesToEnd, bytesToWrite - bytesToEnd);
    }

    context->writeHead.store(writeHead + bytesToWrite, std::memory_order_release);
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_Flush(VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;
    VMicContext* context = static_cast<VMicContext*>(handle);

    const size_t currentRead = context->readHead.load(std::memory_order_relaxed);
    context->writeHead.store(currentRead, std::memory_order_release);

    return VMIC_SUCCESS;
}

VMIC_API const char* VMic_GetLastError(VMicHandle handle) {
    if (!handle) return "Invalid handle.";
    const VMicContext* context = static_cast<VMicContext*>(handle);
    return context->lastError.empty() ? "No error." : context->lastError.c_str();
}