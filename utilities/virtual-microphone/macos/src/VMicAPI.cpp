#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <VMicAPI.h>

struct VMicContext {
    AudioUnit audioUnit = nullptr;
    AudioDeviceID deviceID = kAudioObjectUnknown;
    VMicFormat format{};

    std::vector<uint8_t> ringBuffer;
    std::atomic<size_t> writeHead{0};
    std::atomic<size_t> readHead{0};
    size_t bufferCapacityBytes{0};

    std::string lastError;
    bool isStreaming = false;
};

static OSStatus GetDeviceProperty(AudioObjectID objectID, AudioObjectPropertySelector selector, void* data, UInt32* dataSize) {
    AudioObjectPropertyAddress address = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    return AudioObjectGetPropertyData(objectID, &address, 0, nullptr, dataSize, data);
}

static std::string GetDeviceUID(AudioDeviceID deviceID) {
    CFStringRef uid = nullptr;
    UInt32 size = sizeof(uid);
    if (GetDeviceProperty(deviceID, kAudioDevicePropertyDeviceUID, &uid, &size) == noErr && uid) {
        char buf[256];
        if (CFStringGetCString(uid, buf, sizeof(buf), kCFStringEncodingUTF8)) {
            CFRelease(uid);
            return std::string(buf);
        }
        CFRelease(uid);
    }
    return "";
}

static std::string GetDeviceName(AudioDeviceID deviceID) {
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (GetDeviceProperty(deviceID, kAudioDevicePropertyDeviceNameCFString, &name, &size) == noErr && name) {
        char buf[256];
        if (CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8)) {
            CFRelease(name);
            return std::string(buf);
        }
        CFRelease(name);
    }
    return "";
}

static OSStatus RenderCallback(void* inRefCon,
                              AudioUnitRenderActionFlags* ioActionFlags,
                              const AudioTimeStamp* inTimeStamp,
                              UInt32 inBusNumber,
                              UInt32 inNumberFrames,
                              AudioBufferList* ioData) {
    VMicContext* context = static_cast<VMicContext*>(inRefCon);
    if (!context || ioData->mNumberBuffers == 0) return noErr;

    uint32_t bytesPerFrame = context->format.channels * (context->format.bitDepth / 8);
    uint32_t bytesRequested = inNumberFrames * bytesPerFrame;

    size_t currentWrite = context->writeHead.load(std::memory_order_acquire);
    size_t currentRead = context->readHead.load(std::memory_order_relaxed);
    size_t available = currentWrite - currentRead;

    uint8_t* outData = static_cast<uint8_t*>(ioData->mBuffers[0].mData);

    if (available >= bytesRequested) {
        size_t readPos = currentRead % context->bufferCapacityBytes;
        size_t toEnd = context->bufferCapacityBytes - readPos;

        if (bytesRequested <= toEnd) {
            std::memcpy(outData, &context->ringBuffer[readPos], bytesRequested);
        } else {
            std::memcpy(outData, &context->ringBuffer[readPos], toEnd);
            std::memcpy(outData + toEnd, &context->ringBuffer[0], bytesRequested - toEnd);
        }
        context->readHead.store(currentRead + bytesRequested, std::memory_order_release);
    } else {
        std::memset(outData, 0, ioData->mBuffers[0].mDataByteSize);
    }

    return noErr;
}

extern "C" {

VMIC_API VMicResult VMic_Initialize(void) {
    return VMIC_SUCCESS;
}

VMIC_API void VMic_Shutdown(void) {
}

VMIC_API VMicResult VMic_GetAvailableDevices(VMicDeviceInfo* devices, uint32_t* count) {
    if (!count) return VMIC_ERROR_INVALID_PARAMETER;

    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return VMIC_ERROR_GENERIC;
    }

    int deviceCount = size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> deviceIDs(deviceCount);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, deviceIDs.data()) != noErr) {
        return VMIC_ERROR_GENERIC;
    }

    std::vector<VMicDeviceInfo> foundDevices;
    for (AudioDeviceID id : deviceIDs) {
        std::string name = GetDeviceName(id);
        if (name.find("BlackHole") != std::string::npos) {
            VMicDeviceInfo info;
            std::string uid = GetDeviceUID(id);
            std::strncpy(info.id, uid.c_str(), sizeof(info.id) - 1);
            info.id[sizeof(info.id) - 1] = '\0';
            std::strncpy(info.name, name.c_str(), sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';
            foundDevices.push_back(info);
        }
    }

    if (devices == nullptr) {
        *count = (uint32_t)foundDevices.size();
        return VMIC_SUCCESS;
    }

    uint32_t toCopy = std::min(*count, (uint32_t)foundDevices.size());
    for (uint32_t i = 0; i < toCopy; ++i) {
        devices[i] = foundDevices[i];
    }
    *count = toCopy;

    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_CreateDevice(const char* deviceName, char* deviceId, uint32_t bufferSize) {
    return VMIC_ERROR_UNSUPPORTED_FUNCTION;
}

VMIC_API VMicResult VMic_DestroyDevice(const char* deviceId) {
    return VMIC_ERROR_UNSUPPORTED_FUNCTION;
}

VMIC_API VMicResult VMic_OpenDevice(VMicHandle* handle, const char* deviceId, const VMicFormat* format) {
    if (!handle || !deviceId || !format) return VMIC_ERROR_INVALID_PARAMETER;

    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return VMIC_ERROR_GENERIC;
    }

    int deviceCount = size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> deviceIDs(deviceCount);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, deviceIDs.data()) != noErr) {
        return VMIC_ERROR_GENERIC;
    }

    AudioDeviceID targetID = kAudioObjectUnknown;
    for (AudioDeviceID id : deviceIDs) {
        if (GetDeviceUID(id) == deviceId) {
            targetID = id;
            break;
        }
    }

    if (targetID == kAudioObjectUnknown) return VMIC_ERROR_DEVICE_NOT_FOUND;

    VMicContext* context = new VMicContext();
    context->deviceID = targetID;
    context->format = *format;

    uint32_t bufferFrames = 4096;
    uint32_t bytesPerFrame = format->channels * (format->bitDepth / 8);
    context->bufferCapacityBytes = bufferFrames * bytesPerFrame * 10;
    context->ringBuffer.resize(context->bufferCapacityBytes, 0);

    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) {
        delete context;
        return VMIC_ERROR_GENERIC;
    }

    if (AudioComponentInstanceNew(comp, &context->audioUnit) != noErr) {
        delete context;
        return VMIC_ERROR_GENERIC;
    }

    if (AudioUnitSetProperty(context->audioUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &targetID, sizeof(targetID)) != noErr) {
        AudioComponentInstanceDispose(context->audioUnit);
        delete context;
        return VMIC_ERROR_GENERIC;
    }

    AudioStreamBasicDescription asbd;
    asbd.mSampleRate = format->sampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    if (format->bitDepth == 32) {
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    } else {
        asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    }
    asbd.mFramesPerPacket = 1;
    asbd.mChannelsPerFrame = format->channels;
    asbd.mBitsPerChannel = format->bitDepth;
    asbd.mBytesPerPacket = bytesPerFrame;
    asbd.mBytesPerFrame = bytesPerFrame;
    asbd.mReserved = 0;

    if (AudioUnitSetProperty(context->audioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd)) != noErr) {
        AudioComponentInstanceDispose(context->audioUnit);
        delete context;
        return VMIC_ERROR_UNSUPPORTED_FORMAT;
    }

    AURenderCallbackStruct callback;
    callback.inputProc = RenderCallback;
    callback.inputProcRefCon = context;

    if (AudioUnitSetProperty(context->audioUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr) {
        AudioComponentInstanceDispose(context->audioUnit);
        delete context;
        return VMIC_ERROR_GENERIC;
    }

    if (AudioUnitInitialize(context->audioUnit) != noErr) {
        AudioComponentInstanceDispose(context->audioUnit);
        delete context;
        return VMIC_ERROR_INIT_FAILED;
    }

    *handle = context;
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_Close(VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;
    VMicContext* context = static_cast<VMicContext*>(handle);

    if (context->isStreaming) {
        AudioOutputUnitStop(context->audioUnit);
    }

    AudioUnitUninitialize(context->audioUnit);
    AudioComponentInstanceDispose(context->audioUnit);
    delete context;

    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_StartStream(VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;
    VMicContext* context = static_cast<VMicContext*>(handle);

    if (AudioOutputUnitStart(context->audioUnit) != noErr) {
        return VMIC_ERROR_GENERIC;
    }

    context->isStreaming = true;
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_StopStream(VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;
    VMicContext* context = static_cast<VMicContext*>(handle);

    if (AudioOutputUnitStop(context->audioUnit) != noErr) {
        return VMIC_ERROR_GENERIC;
    }

    context->isStreaming = false;
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_GetAvailableSpace(VMicHandle handle, uint32_t* availableSamples) {
    if (!handle || !availableSamples) return VMIC_ERROR_INVALID_PARAMETER;
    VMicContext* context = static_cast<VMicContext*>(handle);

    size_t currentWrite = context->writeHead.load(std::memory_order_relaxed);
    size_t currentRead = context->readHead.load(std::memory_order_acquire);
    size_t used = currentWrite - currentRead;
    size_t availableBytes = context->bufferCapacityBytes - used;

    uint32_t bytesPerFrame = context->format.channels * (context->format.bitDepth / 8);
    *availableSamples = (uint32_t)(availableBytes / bytesPerFrame);

    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_PushSamples(VMicHandle handle, const void* samples, uint32_t sampleCount) {
    if (!handle || !samples) return VMIC_ERROR_INVALID_PARAMETER;
    VMicContext* context = static_cast<VMicContext*>(handle);

    uint32_t bytesPerFrame = context->format.channels * (context->format.bitDepth / 8);
    size_t bytesToWrite = (size_t)sampleCount * bytesPerFrame;

    size_t currentWrite = context->writeHead.load(std::memory_order_relaxed);
    size_t currentRead = context->readHead.load(std::memory_order_acquire);
    size_t used = currentWrite - currentRead;
    size_t availableBytes = context->bufferCapacityBytes - used;

    if (bytesToWrite > availableBytes) {
        context->lastError = "Buffer full";
        return VMIC_ERROR_BUFFER_FULL;
    }

    size_t writePos = currentWrite % context->bufferCapacityBytes;
    size_t toEnd = context->bufferCapacityBytes - writePos;

    const uint8_t* src = static_cast<const uint8_t*>(samples);
    if (bytesToWrite <= toEnd) {
        std::memcpy(&context->ringBuffer[writePos], src, bytesToWrite);
    } else {
        std::memcpy(&context->ringBuffer[writePos], src, toEnd);
        std::memcpy(&context->ringBuffer[0], src + toEnd, bytesToWrite - toEnd);
    }

    context->writeHead.store(currentWrite + bytesToWrite, std::memory_order_release);
    return VMIC_SUCCESS;
}

VMIC_API VMicResult VMic_Flush(VMicHandle handle) {
    if (!handle) return VMIC_ERROR_INVALID_HANDLE;
    VMicContext* context = static_cast<VMicContext*>(handle);

    size_t currentRead = context->readHead.load(std::memory_order_relaxed);
    context->writeHead.store(currentRead, std::memory_order_release);

    return VMIC_SUCCESS;
}

VMIC_API const char* VMic_GetLastError(VMicHandle handle) {
    if (!handle) return "Invalid handle";
    VMicContext* context = static_cast<VMicContext*>(handle);
    return context->lastError.empty() ? "No error" : context->lastError.c_str();
}

}
