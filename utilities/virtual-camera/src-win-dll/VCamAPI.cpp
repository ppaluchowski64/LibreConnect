#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <mferror.h>
#include <comdef.h>
#include <combaseapi.h>
#include "framework.h"
#include "VCamAPI.h"
#include <queue>
#include <mutex>
#include <memory>
#include <string>
#include <map>
#include <vector>

static void SetError(const wchar_t* msg, VCamHandle handle);
static void SetError(const wchar_t* msg, const VCamHandle* handle);
static void SetError(HRESULT hr, const VCamHandle handle);
static void SetError(HRESULT hr, const VCamHandle* handle);

extern "C" {
    extern GUID CLSID_VCam;
}

inline std::wstring GUID_ToStringW_Simple(const GUID& guid)
{
    wchar_t buf[64];
    swprintf_s(buf, 64, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::wstring(buf);
}

// Forward declaration
struct VCamInstance;

struct PushedFrame
{
    std::vector<BYTE> data;
    GUID format;
    UINT width;
    UINT height;
};

// Global state - maps CLSID string to camera instance
static std::mutex g_camerasMutex;
static std::map<std::wstring, std::shared_ptr<VCamInstance>> g_camerasByClsid;
static std::map<VCamHandle, std::shared_ptr<VCamInstance>> g_cameras;
static std::map<VCamHandle, std::wstring> g_lastErrors;

// Global configuration for MediaStream (since MediaSource is created by Windows)
struct StreamConfig
{
    UINT width;
    UINT height;
    UINT fps;
    bool useExternalFrames;
};
std::map<std::wstring, StreamConfig> g_streamConfigs;
std::mutex g_configMutex;

// VCam instance structure
struct VCamInstance
{
    std::wstring name;
    UINT width;
    UINT height;
    UINT fps;
    GUID clsid;
    wil::com_ptr_nothrow<IMFVirtualCamera> vcam;
    std::mutex frameQueueMutex;
    std::queue<PushedFrame> frameQueue;
    bool useExternalFrames;
    bool initialized;

    VCamInstance() : useExternalFrames(false), initialized(false), width(0), height(0), fps(30)
    {
        // Generate unique CLSID for this instance
        CoCreateGuid(&clsid);
    }

    ~VCamInstance()
    {
        if (vcam)
        {
            vcam->Remove();
            vcam.reset();
        }
    }
};

// Shared memory header for cross-process frame sharing
struct SharedFrameHeader
{
    UINT width;
    UINT height;
    UINT stride;
    GUID format;
    volatile LONG frameVersion; // incremented on each new frame
};

// Per-process shared memory state (one camera / CLSID in this sample)
static HANDLE g_sharedFrameMapping = nullptr;
static SharedFrameHeader* g_sharedFrameHeader = nullptr;
static BYTE* g_sharedFrameData = nullptr;
static size_t g_sharedFrameBufferSize = 0;

static std::wstring GetSharedMemoryName(const GUID& clsid)
{
    // Use a named mapping based on CLSID so both processes can open it
    return L"Local\\VCamFrame_" + GUID_ToStringW_Simple(clsid);
}

// Ensure shared memory is created for the given instance and frame size
static bool EnsureSharedMemory(const GUID& clsid, const size_t frameSize, const VCamHandle handle)
{
    if (g_sharedFrameMapping && g_sharedFrameHeader && g_sharedFrameData && g_sharedFrameBufferSize >= frameSize)
    {
        return true;
    }

    const size_t totalSize = sizeof(SharedFrameHeader) + frameSize;
    const auto name = GetSharedMemoryName(clsid);

    const HANDLE hMap = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(totalSize),
        name.c_str());

    if (!hMap)
    {
        SetError(L"Failed to create shared memory for frames", handle);
        return false;
    }

    void* view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);
    if (!view)
    {
        CloseHandle(hMap);
        SetError(L"Failed to map shared memory for frames", handle);
        return false;
    }

    g_sharedFrameMapping = hMap;
    g_sharedFrameHeader = static_cast<SharedFrameHeader*>(view);
    g_sharedFrameData = reinterpret_cast<BYTE*>(g_sharedFrameHeader + 1);
    g_sharedFrameBufferSize = frameSize;

    // Initialize header
    g_sharedFrameHeader->width = 0;
    g_sharedFrameHeader->height = 0;
    g_sharedFrameHeader->stride = 0;
    ZeroMemory(&g_sharedFrameHeader->format, sizeof(GUID));
    g_sharedFrameHeader->frameVersion = 0;

    return true;
}

static GUID FormatToGUID(const VCamFormat format)
{
    switch (format)
    {
    case VCAM_FORMAT_RGB32:
        return MFVideoFormat_RGB32;
    case VCAM_FORMAT_NV12:
        return MFVideoFormat_NV12;
    case VCAM_FORMAT_BGRA:
        return MFVideoFormat_RGB32; // BGRA is same as RGB32 in MF
    default:
        return MFVideoFormat_RGB32;
    }
}

static void SetError(const wchar_t* msg, const VCamHandle handle)
{
    g_lastErrors[handle] = msg ? msg : L"";
}

static void SetError(const wchar_t* msg, const VCamHandle* handle) {
    if (handle != nullptr) {
        g_lastErrors[*handle] = msg ? msg : L"";
    }
}

static void SetError(const HRESULT hr, const VCamHandle* handle)
{
    if (handle != nullptr)
    {
        SetError(hr, *handle);
    }
}

static void SetError(const HRESULT hr, const VCamHandle handle)
{
    wchar_t errorText[256];
    if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, 0, errorText, _countof(errorText), nullptr))
    {
        g_lastErrors[handle] = errorText;
    }
    else
    {
        wchar_t buf[64];
        swprintf_s(buf, 64, L"Error 0x%08X", hr);
        g_lastErrors[handle] = buf;
    }
}

// Helper function to check if CLSID is registered
static bool IsCLSIDRegistered(const GUID& clsid)
{
    wchar_t clsidStr[64];
    swprintf_s(clsidStr, 64, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        clsid.Data1, clsid.Data2, clsid.Data3,
        clsid.Data4[0], clsid.Data4[1], clsid.Data4[2], clsid.Data4[3],
        clsid.Data4[4], clsid.Data4[5], clsid.Data4[6], clsid.Data4[7]);

    std::wstring regPath = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + clsidStr + L"\\InprocServer32";
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

extern "C" {
    VCAMAPI_API VCamResult CreateCam(const wchar_t* name, const int width, const int height, const int fps, VCamHandle* handle)
    {
        if (!name || !handle || width <= 0 || height <= 0 || fps <= 0)
        {
            SetError(L"Invalid parameters", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> lock(g_camerasMutex);

        // Check if CLSID is registered before attempting to create camera
        if (!IsCLSIDRegistered(CLSID_VCam))
        {
            SetError(L"CLSID_VCam is not registered in registry.", handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        static bool mfInitialized = false;
        if (!mfInitialized)
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            {
                SetError(hr, handle);
                return VCAM_ERROR_INIT_FAILED;
            }
            hr = MFStartup(MF_VERSION);
            if (FAILED(hr))
            {
                SetError(hr, handle);
                return VCAM_ERROR_INIT_FAILED;
            }
            mfInitialized = true;
        }

        try
        {
            const auto instance = std::make_shared<VCamInstance>();
            instance->name = name;
            instance->width = width;
            instance->height = height;
            instance->fps = fps;
            instance->useExternalFrames = true;

            // Use the registered CLSID_VCam
            instance->clsid = CLSID_VCam;
            const auto clsidStr = GUID_ToStringW_Simple(CLSID_VCam);

            {
                std::lock_guard<std::mutex> configLock(g_configMutex);
                StreamConfig config;
                config.width = width;
                config.height = height;
                config.fps = fps;
                config.useExternalFrames = true;
                g_streamConfigs[clsidStr] = config;

                const std::wstring regPath = L"SOFTWARE\\VCamSample\\" + clsidStr;
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, nullptr,
                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
                {
                    RegSetValueExW(hKey, L"Width", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&width), sizeof(width));
                    RegSetValueExW(hKey, L"Height", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&height), sizeof(height));
                    RegSetValueExW(hKey, L"Fps", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&fps), sizeof(fps));
                    constexpr DWORD useExternal = 1;
                    RegSetValueExW(hKey, L"UseExternalFrames", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&useExternal), sizeof(useExternal));
                    RegCloseKey(hKey);
                }

                const DWORD processId = GetCurrentProcessId();
                wchar_t debugMsg[512];
                swprintf_s(debugMsg, L"[VCamAPI] CreateCam: Setting config BEFORE camera creation: %dx%d @ %d fps, useExternalFrames=%d (Process ID=%lu, map size=%zu, map addr=%p)\n",
                    width, height, fps, config.useExternalFrames, processId, g_streamConfigs.size(), &g_streamConfigs);
                OutputDebugStringW(debugMsg);
            }

            // Create virtual camera
            HRESULT hr = MFCreateVirtualCamera(
                MFVirtualCameraType_SoftwareCameraSource,
                MFVirtualCameraLifetime_Session,
                MFVirtualCameraAccess_CurrentUser,
                name,
                clsidStr.c_str(),
                nullptr,
                0,
                &instance->vcam);

            if (FAILED(hr))
            {
                SetError(hr, handle);
                return VCAM_ERROR_INIT_FAILED;
            }

            try
            {
                hr = instance->vcam->Start(nullptr);

                if (FAILED(hr))
                {
                    SetError(hr, handle);
                    instance->vcam->Remove();
                    return VCAM_ERROR_INIT_FAILED;
                }
            }
            catch (const winrt::hresult_error& ex)
            {
                HRESULT exHr = ex.code();
                SetError(hr, handle);

                if (instance->vcam)
                {
                    instance->vcam->Remove();
                }

                return VCAM_ERROR_INIT_FAILED;
            }

            instance->initialized = true;
            *handle = instance.get();
            g_cameras[*handle] = instance;
            g_camerasByClsid[clsidStr] = instance;

            {
                std::lock_guard<std::mutex> configLock(g_configMutex);
                auto it = g_streamConfigs.find(clsidStr);
                if (it != g_streamConfigs.end())
                {
                    wchar_t debugMsg[256];
                    swprintf_s(debugMsg, L"[VCamAPI] CreateCam: Config verified after Start: %dx%d @ %d fps, useExternalFrames=%d\n",
                        it->second.width, it->second.height, it->second.fps, it->second.useExternalFrames);
                    OutputDebugStringW(debugMsg);
                }
                else
                {
                    OutputDebugStringW(L"[VCamAPI] CreateCam: WARNING - Config lost after Start()!\n");
                    StreamConfig config;
                    config.width = width;
                    config.height = height;
                    config.fps = fps;
                    config.useExternalFrames = true;
                    g_streamConfigs[clsidStr] = config;
                }
            }

            return VCAM_SUCCESS;
        }
        catch (const winrt::hresult_error& ex)
        {
            const HRESULT hr = ex.code();
            SetError(hr, handle);

            return VCAM_ERROR_INIT_FAILED;
        }
        catch (const std::exception& ex)
        {
            std::string msg = ex.what();
            std::wstring wmsg(msg.begin(), msg.end());
            SetError(wmsg.c_str(), handle);
            return VCAM_ERROR_INIT_FAILED;
        }
        catch (...)
        {
            SetError(L"Unknown exception during camera creation", handle);
            return VCAM_ERROR_INIT_FAILED;
        }
    }

    VCAMAPI_API VCamResult DestroyCam(const VCamHandle handle)
    {
        if (!handle)
        {
            SetError(L"Invalid handle", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> lock(g_camerasMutex);

        auto it = g_cameras.find(handle);
        if (it == g_cameras.end())
        {
            SetError(L"Camera not found", handle);
            return VCAM_ERROR_CAMERA_NOT_FOUND;
        }

        auto instance = it->second;

        // Remove virtual camera
        if (instance->vcam)
        {
            instance->vcam->Remove();
            instance->vcam.reset();
        }

        // Clear frame queue
        {
            std::lock_guard<std::mutex> frameLock(instance->frameQueueMutex);
            while (!instance->frameQueue.empty())
            {
                instance->frameQueue.pop();
            }
        }

        // Remove from both maps
        auto clsidStr = GUID_ToStringW_Simple(instance->clsid);
        g_camerasByClsid.erase(clsidStr);
        g_cameras.erase(it);
        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(const VCamHandle handle, const void* data, const VCamFormat format)
    {
        if (!handle || !data)
        {
            SetError(L"Invalid parameters", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> lock(g_camerasMutex);

        const auto it = g_cameras.find(handle);
        if (it == g_cameras.end())
        {
            SetError(L"Camera not found", handle);
            return VCAM_ERROR_CAMERA_NOT_FOUND;
        }

        const auto instance = it->second;
        if (!instance->initialized)
        {
            SetError(L"Camera not initialized", handle);
            return VCAM_ERROR_CAMERA_NOT_FOUND;
        }

        size_t frameSize = 0;
        UINT bytesPerPixel = 0;
        GUID mfFormat = GUID_NULL;

        if (format == VCAM_FORMAT_RGB32 || format == VCAM_FORMAT_BGRA)
        {
            bytesPerPixel = 4;
            frameSize = instance->width * instance->height * bytesPerPixel;
            mfFormat = MFVideoFormat_RGB32;
        }
        else if (format == VCAM_FORMAT_NV12)
        {
            bytesPerPixel = 1;
            frameSize = instance->width * instance->height * 3 / 2;
            mfFormat = MFVideoFormat_NV12;
        }
        else
        {
            SetError(L"Unsupported format", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        // Ensure shared memory for this camera (cross-process)
        if (!EnsureSharedMemory(instance->clsid, frameSize, handle))
        {
            return VCAM_ERROR_INIT_FAILED;
        }

        // Write frame into shared memory
        g_sharedFrameHeader->width = instance->width;
        g_sharedFrameHeader->height = instance->height;
        g_sharedFrameHeader->stride = (mfFormat == MFVideoFormat_RGB32) ? (instance->width * 4) : instance->width;
        g_sharedFrameHeader->format = mfFormat;

        if (frameSize <= g_sharedFrameBufferSize)
        {
            memcpy(g_sharedFrameData, data, frameSize);
            const LONG newVersion = InterlockedIncrement(&g_sharedFrameHeader->frameVersion);
        }
        else
        {
            SetError(L"Shared frame buffer too small", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        return VCAM_SUCCESS;
    }

    VCAMAPI_API const wchar_t* VCamGetLastError(const VCamHandle handle)
    {
        if (!g_lastErrors.contains(handle)) {
            return L"";
        }

        return g_lastErrors.at(handle).c_str();
    }
}

extern "C++"
{
    __declspec(dllexport) bool VCamAPI_HasExternalFrame(const GUID& clsid)
    {
        UNREFERENCED_PARAMETER(clsid);

        if (!g_sharedFrameHeader)
        {
            const auto name = GetSharedMemoryName(CLSID_VCam);

            const HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
            if (!hMap)
            {
                return false;
            }

            // Map just header first to check if data exists
            void* headerView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(SharedFrameHeader));
            if (!headerView)
            {
                CloseHandle(hMap);
                return false;
            }

            const SharedFrameHeader* tempHeader = static_cast<SharedFrameHeader*>(headerView);

            if (tempHeader->frameVersion == 0 || tempHeader->width == 0 || tempHeader->height == 0)
            {
                UnmapViewOfFile(headerView);
                CloseHandle(hMap);
                return false;
            }

            // Calculate frame size
            size_t frameSize = 0;
            if (tempHeader->format == MFVideoFormat_RGB32)
            {
                frameSize = static_cast<size_t>(tempHeader->width) * static_cast<size_t>(tempHeader->height) * 4;
            }
            else if (tempHeader->format == MFVideoFormat_NV12)
            {
                frameSize = static_cast<size_t>(tempHeader->width) * static_cast<size_t>(tempHeader->height) * 3 / 2;
            }
            else
            {
                UnmapViewOfFile(headerView);
                CloseHandle(hMap);
                return false;
            }

            UnmapViewOfFile(headerView);

            const size_t totalSize = sizeof(SharedFrameHeader) + frameSize;
            void* view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, totalSize);
            if (!view)
            {
                CloseHandle(hMap);
                return false;
            }

            g_sharedFrameMapping = hMap;
            g_sharedFrameHeader = static_cast<SharedFrameHeader*>(view);
            g_sharedFrameData = reinterpret_cast<BYTE*>(g_sharedFrameHeader + 1);
            g_sharedFrameBufferSize = frameSize;
        }

        const bool hasFrames = (g_sharedFrameHeader->frameVersion != 0);
        return hasFrames;
    }

    __declspec(dllexport) bool GetExternalFrame(const GUID& clsid, PushedFrame& frame)
    {
        UNREFERENCED_PARAMETER(clsid);

        if (!g_sharedFrameHeader)
        {
            const auto name = GetSharedMemoryName(CLSID_VCam);

            const HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
            if (!hMap)
            {
                return false;
            }

            // First, map just the header to read dimensions
            void* headerView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(SharedFrameHeader));
            if (!headerView)
            {
                CloseHandle(hMap);
                return false;
            }

            const SharedFrameHeader* tempHeader = static_cast<SharedFrameHeader*>(headerView);
            const UINT width = tempHeader->width;
            const UINT height = tempHeader->height;
            const GUID fmt = tempHeader->format;

            // Calculate frame size
            size_t frameSize = 0;
            if (fmt == MFVideoFormat_RGB32)
            {
                frameSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
            }
            else if (fmt == MFVideoFormat_NV12)
            {
                frameSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2;
            }
            else
            {
                UnmapViewOfFile(headerView);
                CloseHandle(hMap);
                return false;
            }

            // Unmap header view and remap full region
            UnmapViewOfFile(headerView);

            const size_t totalSize = sizeof(SharedFrameHeader) + frameSize;
            void* view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, totalSize);
            if (!view)
            {
                CloseHandle(hMap);
                return false;
            }

            g_sharedFrameMapping = hMap;
            g_sharedFrameHeader = static_cast<SharedFrameHeader*>(view);
            g_sharedFrameData = reinterpret_cast<BYTE*>(g_sharedFrameHeader + 1);
            g_sharedFrameBufferSize = frameSize;
        }

        static LONG lastVersion = 0;
        const LONG version = g_sharedFrameHeader->frameVersion;

        if (version == 0)
        {
            return false;
        }

        if (version == lastVersion && lastVersion != 0)
        {
            return false;
        }

        // Determine frame size from header
        const UINT width = g_sharedFrameHeader->width;
        const UINT height = g_sharedFrameHeader->height;
        const GUID fmt = g_sharedFrameHeader->format;

        size_t frameSize = 0;
        if (fmt == MFVideoFormat_RGB32)
        {
            frameSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        }
        else if (fmt == MFVideoFormat_NV12)
        {
            frameSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2;
        }
        else
        {
            return false;
        }

        frame.data.resize(frameSize);
        memcpy(frame.data.data(), g_sharedFrameData, frameSize);
        frame.width = width;
        frame.height = height;
        frame.format = fmt;

        lastVersion = version;
        return true;
    }
}