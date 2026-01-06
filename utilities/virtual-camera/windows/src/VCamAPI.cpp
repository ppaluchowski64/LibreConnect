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
#include "Tools.h"
#include <queue>
#include <mutex>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <fmt/format.h>

static void SetError(const char* msg, VCamHandle handle);
static void SetError(const char* msg, const VCamHandle* handle);
static void SetError(HRESULT hr, VCamHandle handle);
static void SetError(HRESULT hr, const VCamHandle* handle);


inline std::wstring GUID_ToStringW_Simple(const GUID& guid)
{
    wchar_t buf[64];
    swprintf_s(buf, 64, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::wstring(buf);
}


static std::wstring StringToWString(const std::string& str)
{
    if (str.empty())
        return std::wstring();

    int size_needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        (int)str.size(),
        nullptr,
        0
    );

    std::wstring wstr(size_needed, 0);

    MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        (int)str.size(),
        &wstr[0],
        size_needed
    );

    return wstr;
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
static std::vector<bool> g_usedCLSIDs{false};
static std::map<std::wstring, std::shared_ptr<VCamInstance>> g_camerasByClsid;
static std::map<VCamHandle, std::shared_ptr<VCamInstance>> g_cameras;
static std::map<VCamHandle, std::string> g_lastErrors;

struct SharedFrameHeader
{
    UINT width;
    UINT height;
    UINT stride;
    GUID format;
    volatile LONG frameVersion;
};

struct VCamInstance
{
    std::wstring name;
    UINT width;
    UINT height;
    UINT fps;
    GUID clsid;
    wil::com_ptr_nothrow<IMFVirtualCamera> vcam;

    VCamHandle handle;
    HANDLE sharedFrameMapping = nullptr;
    SharedFrameHeader* sharedFrameHeader = nullptr;
    BYTE* sharedFrameData = nullptr;
    size_t sharedFrameDataSize = 0;

    VCamInstance() = delete;
    explicit VCamInstance(const std::wstring& name, const UINT width, const UINT height, const UINT fps, const GUID clsid, VCamHandle handle)
    : name(name), width(width), height(height), fps(fps), clsid(clsid), handle(handle) {}

    ~VCamInstance()
    {
        if (vcam)
        {
            vcam->Remove();
            vcam.reset();
        }

        if (sharedFrameHeader)
        {
            UnmapViewOfFile(sharedFrameHeader);
            sharedFrameHeader = nullptr;
            sharedFrameData = nullptr;
            sharedFrameDataSize = 0;
        }

        if (sharedFrameMapping)
        {
            CloseHandle(sharedFrameMapping);
            sharedFrameMapping = nullptr;
        }
    }
};

static void SetError(const char* msg, const VCamHandle handle)
{
    g_lastErrors[handle] = msg ? msg : "";
}

static void SetError(const char* msg, const VCamHandle* handle) {
    if (handle != nullptr) {
        g_lastErrors[*handle] = msg ? msg : "";
    }
}

static void SetError(const HRESULT hr, const VCamHandle* handle)
{
    if (handle != nullptr)
    {
        SetError(hr, *handle);
    }
}

constexpr UINT static GetFrameSize(const VCamFormat format, const UINT width, const UINT height) {
    switch (format) {
        case VCAM_FORMAT_RGB32: return width * height * 4;
        case VCAM_FORMAT_BGRA: return width * height * 4;
        case VCAM_FORMAT_NV12: return width * height * 3 / 2;
        case VCAM_FORMAT_YUYV: return width * height * 2;
        case VCAM_FORMAT_YUV420: return width * height * 3 / 2;
    }

    return 0;
}

constexpr UINT static GetFrameSize(const GUID format, const UINT width, const UINT height) {
    switch (format) {
        case MFVideoFormat_RGB32: return width * height * 4;
        case MFVideoFormat_ARGB32: return width * height * 4;
        case MFVideoFormat_NV12: return width * height * 3 / 2;
        case MFVideoFormat_YUY2: return width * height * 2;
        case MFVideoFormat_I420: return width * height * 3 / 2;
    }

    return 0;
}

constexpr UUID static GetMfFormat(const VCamFormat format) {
    switch (format) {
        case VCAM_FORMAT_RGB32: return MFVideoFormat_RGB32;
        case VCAM_FORMAT_BGRA: return MFVideoFormat_ARGB32;
        case VCAM_FORMAT_NV12: return MFVideoFormat_NV12;
        case VCAM_FORMAT_YUYV: return MFVideoFormat_YUY2;
        case VCAM_FORMAT_YUV420: return MFVideoFormat_I420;
    }

    return GUID_NULL;
}

static void SetError(const HRESULT hr, const VCamHandle handle)
{
    char errorText[256];
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, 0, errorText, _countof(errorText), nullptr))
    {
        g_lastErrors[handle] = fmt::format("HRESULT(0x{:08X}): {}", static_cast<uint64_t>(hr), errorText);
    }
    else
    {
        char buf[64];
        sprintf_s(buf, 64, "Error 0x%08X", hr);
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

static bool SharedMemoryExists(const GUID& clsid, const size_t frameSize, const VCamHandle handle) {
    const std::shared_ptr<VCamInstance> instance = g_cameras[handle];

    if (instance->sharedFrameMapping && instance->sharedFrameHeader && instance->sharedFrameData && instance->sharedFrameDataSize >= frameSize) {
        return true;
    }

    return false;
}

static bool EnsureSharedMemory(const GUID& clsid, const size_t frameSize, const VCamHandle handle) {
    const std::shared_ptr<VCamInstance> instance = g_cameras[handle];

    if (SharedMemoryExists(clsid, frameSize, handle)) {
        return true;
    }

    const size_t totalSize = sizeof(SharedFrameHeader) + frameSize;
    const auto name = L"Local\\LibreConnect_VirtualCameraFrame_" + GUID_ToStringW_Simple(clsid);

    const HANDLE hMap = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(totalSize),
        name.c_str()
    );

    if (!hMap) {
        SetError("Failed to create shared memory for frames", handle);
        return false;
    }

    void* view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);

    if (!view) {
        CloseHandle(hMap);
        SetError("Failed to map shared memory for frames", handle);
        return false;
    }

    instance->sharedFrameMapping = hMap;
    instance->sharedFrameHeader = static_cast<SharedFrameHeader*>(view);
    instance->sharedFrameData = reinterpret_cast<BYTE*>(instance->sharedFrameHeader + 1);
    instance->sharedFrameDataSize = frameSize;

    instance->sharedFrameHeader->width = 0;
    instance->sharedFrameHeader->height = 0;
    instance->sharedFrameHeader->stride = 0;
    instance->sharedFrameHeader->format = GUID_NULL;
    instance->sharedFrameHeader->frameVersion = 0;

    return true;
}

extern "C" {
    VCAMAPI_API VCamResult CreateCam(const char* name, const int width, const int height, const int fps, VCamFormat format, VCamHandle* handle)
    {
        if (!name || !handle || width <= 0 || height <= 0 || fps <= 0)
        {
            SetError("Invalid parameters", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> lock(g_camerasMutex);

        GUID clsid = GUID_NULL;
        for (int i = 0; i < Cameras_CLSID.size(); i++) {
            if (g_usedCLSIDs[i]) {
                continue;
            }

            clsid = Cameras_CLSID[i];
        }

        if (clsid == GUID_NULL) {
            SetError("Instance limit reached.", handle);
            return VCAM_ERROR_INIT_FAILED;
        }

        // Check if CLSID is registered before attempting to create camera
        if (!IsCLSIDRegistered(clsid))
        {
            SetError("CLSID is not registered in registry.", handle);
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
            const auto instance = std::make_shared<VCamInstance>(StringToWString(name), width, height, fps, clsid, handle);
            const auto clsid_str = GUID_ToStringW_Simple(instance->clsid);

            {
                const std::wstring regPath = L"SOFTWARE\\LibreConnect_VirtualCamera_Configs\\" + clsid_str;
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, nullptr,
                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
                {
                    RegSetValueExW(hKey, L"Width", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&width), sizeof(width));
                    RegSetValueExW(hKey, L"Height", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&height), sizeof(height));
                    RegSetValueExW(hKey, L"Fps", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&fps), sizeof(fps));
                    RegCloseKey(hKey);
                }
            }

            // Create virtual camera
            HRESULT hr = MFCreateVirtualCamera(
                MFVirtualCameraType_SoftwareCameraSource,
                MFVirtualCameraLifetime_Session,
                MFVirtualCameraAccess_CurrentUser,
                instance->name.c_str(),
                clsid_str.c_str(),
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

            *handle = instance.get();
            g_cameras[*handle] = instance;
            g_camerasByClsid[clsid_str] = instance;

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
            const std::string msg = ex.what();
            SetError(msg.c_str(), handle);
            return VCAM_ERROR_INIT_FAILED;
        }
        catch (...)
        {
            SetError("Unknown exception during camera creation", handle);
            return VCAM_ERROR_INIT_FAILED;
        }
    }

    VCAMAPI_API VCamResult DestroyCam(const VCamHandle handle)
    {
        if (!handle)
        {
            SetError("Invalid handle", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> lock(g_camerasMutex);

        if (!g_cameras.contains(handle))
        {
            SetError("Camera not found", handle);
            return VCAM_ERROR_CAMERA_NOT_FOUND;
        }

        const auto instance = g_cameras.at(handle);

        // Remove virtual camera
        if (instance->vcam)
        {
            instance->vcam->Remove();
            instance->vcam.reset();
        }

        // Remove from both maps
        const auto id = GUID_ToStringW_Simple(instance->clsid);
        g_camerasByClsid.erase(id);
        g_cameras.erase(handle);
        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(const VCamHandle handle, const void* data, const VCamFormat format)
    {
        if (!handle || !data)
        {
            SetError("Invalid parameters", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> lock(g_camerasMutex);

        if (!g_cameras.contains(handle))
        {
            SetError("Camera not found", handle);
            return VCAM_ERROR_CAMERA_NOT_FOUND;
        }

        const auto instance = g_cameras.at(handle);

        const size_t frameSize = GetFrameSize(format, instance->width, instance->height);
        const GUID mfFormat = GetMfFormat(format);

        if (mfFormat == GUID_NULL)
        {
            SetError("Unsupported format", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        // Ensure shared memory for this camera (cross-process)
        if (!EnsureSharedMemory(instance->clsid, frameSize, handle))
        {
            return VCAM_ERROR_INIT_FAILED;
        }

        instance->sharedFrameHeader->width = instance->width;
        instance->sharedFrameHeader->height = instance->height;
        instance->sharedFrameHeader->stride = (mfFormat == MFVideoFormat_RGB32) ? (instance->width * 4) : instance->width;
        instance->sharedFrameHeader->format = mfFormat;

        if (frameSize <= instance->sharedFrameDataSize)
        {
            memcpy(instance->sharedFrameData, data, frameSize);
            InterlockedIncrement(&instance->sharedFrameHeader->frameVersion);
        }
        else
        {
            SetError("Shared frame buffer too small", handle);
            return VCAM_ERROR_INVALID_PARAM;
        }

        return VCAM_SUCCESS;
    }

    VCAMAPI_API const char* VCamGetLastError(const VCamHandle handle)
    {
        if (!g_lastErrors.contains(handle)) {
            return "";
        }

        return g_lastErrors.at(handle).c_str();
    }
}

extern "C++"
{
    __declspec(dllexport) bool VCamAPI_HasExternalFrame(const GUID& clsid)
    {
        const std::wstring clsid_str = GUID_ToStringW(clsid);
        if (!g_camerasByClsid.contains(clsid_str)) {
            return false;
        }

        const std::shared_ptr<VCamInstance> instance = g_camerasByClsid.at(clsid_str);
        if (!SharedMemoryExists(clsid, GetFrameSize(instance->sharedFrameHeader->format, instance->width, instance->height), instance->handle)) {
            return false;
        }

        const bool hasFrames = (instance->sharedFrameHeader->frameVersion != 0);
        return hasFrames;
    }

    __declspec(dllexport) bool GetExternalFrame(const GUID& clsid, PushedFrame& frame)
    {
        const std::wstring clsid_str = GUID_ToStringW(clsid);
        if (!g_camerasByClsid.contains(clsid_str)) {
            return false;
        }

        const std::shared_ptr<VCamInstance> instance = g_camerasByClsid.at(clsid_str);
        frame.data.resize(instance->sharedFrameDataSize);
        memcpy(frame.data.data(), instance->sharedFrameData, GetFrameSize(instance->sharedFrameHeader->format, instance->width, instance->height));

        frame.width = instance->sharedFrameHeader->width;
        frame.height = instance->sharedFrameHeader->height;
        frame.format = instance->sharedFrameHeader->format;

        return true;
    }
}