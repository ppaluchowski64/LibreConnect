#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <mferror.h>
#include <comdef.h>
#include <sddl.h>
#include <combaseapi.h>
#include "framework.h"
#include "Tools.h"
#include "VCamAPI.h"

#include <iostream>
#include <queue>
#include <mutex>
#include <memory>
#include <string>
#include <map>
#include <vector>

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

    const int size_needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        static_cast<int>(str.size()),
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

UINT static GetFrameSize(const VCamFormat format, const UINT width, const UINT height) {
    switch (format) {
        case VCAM_FORMAT_RGB32: return width * height * 4;
        case VCAM_FORMAT_BGRA: return width * height * 4;
        case VCAM_FORMAT_NV12: return width * height * 3 / 2;
        case VCAM_FORMAT_YUYV: return width * height * 2;
        case VCAM_FORMAT_YUV420: return width * height * 3 / 2;
    }

    return 0;
}

UINT static GetFrameSize(const GUID format, const UINT width, const UINT height) {
    if (format == MFVideoFormat_RGB32)  return width * height * 4;
    if (format == MFVideoFormat_ARGB32) return width * height * 4;
    if (format == MFVideoFormat_NV12)   return width * height * 3 / 2;
    if (format == MFVideoFormat_YUY2)   return width * height * 2;
    if (format == MFVideoFormat_I420)   return width * height * 3 / 2;

    return 0;
}

UUID static GetMfFormat(const VCamFormat format) {
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
        g_lastErrors[handle] = errorText;
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

static bool CreatePermissiveSecurityAttr(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& pSD)
{
    const wchar_t* sddl = L"D:(A;OICI;GA;;;WD)(A;OICI;GA;;;AC)";

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl,
        SDDL_REVISION_1,
        &pSD,
        nullptr))
    {
        return false;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;
    return true;
}

static bool GetFrameSharedMemory(const GUID& clsid, void** sharedFrameMappingPtr) {
    const auto name = L"Global\\LibreConnect_VirtualCameraFrame_" + GUID_ToStringW_Simple(clsid);

    const HANDLE sharedFrameMapping = OpenFileMappingW(
            FILE_MAP_READ,
            FALSE,
            name.c_str()
    );

    if (!sharedFrameMapping) {
        return false;
    }

    const SharedFrameHeader* sharedFrameHeaderView = static_cast<SharedFrameHeader*>(MapViewOfFile(
        sharedFrameMapping,
        FILE_MAP_READ,
        0,
        0,
        sizeof(SharedFrameHeader))
    );

    if (!sharedFrameHeaderView) {
        return false;
    }

    const size_t frameSize = GetFrameSize(sharedFrameHeaderView->format, sharedFrameHeaderView->width, sharedFrameHeaderView->height);
    UnmapViewOfFile(sharedFrameHeaderView);

    void* sharedFrameView = MapViewOfFile(
        sharedFrameMapping,
        FILE_MAP_READ,
        0,
        0,
        frameSize + sizeof(SharedFrameHeader)
    );

    if (!sharedFrameView) {
        return false;
    }

    *sharedFrameMappingPtr = static_cast<uint8_t*>(sharedFrameView);
    return true;
}

static bool SharedMemoryExists(const GUID& clsid, const size_t frameSize, const std::shared_ptr<VCamInstance>& instance) {
    if (instance->sharedFrameMapping && instance->sharedFrameHeader && instance->sharedFrameData && instance->sharedFrameDataSize >= frameSize) {
        return true;
    }

    return false;
}


static bool EnsureSharedMemory(const GUID& clsid, const size_t frameSize, const std::shared_ptr<VCamInstance>& instance) {
    if (SharedMemoryExists(clsid, frameSize, instance)) {
        return true;
    }

    const size_t totalSize = sizeof(SharedFrameHeader) + frameSize;
    const auto name = L"Global\\LibreConnect_VirtualCameraFrame_" + GUID_ToStringW_Simple(clsid);

    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR pSD = nullptr;
    if (!CreatePermissiveSecurityAttr(sa, pSD)) {
        SetError("Failed to create security attributes", instance->handle);
        return false;
    }

    const HANDLE hMap = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        &sa,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(totalSize),
        name.c_str()
    );

    if (pSD) LocalFree(pSD);

    if (!hMap) {
        SetError("Failed to create shared memory for frames", instance->handle);
        return false;
    }

    void* view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);

    if (!view) {
        CloseHandle(hMap);
        SetError("Failed to map shared memory for frames", instance->handle);
        return false;
    }

    instance->sharedFrameMapping = hMap;
    instance->sharedFrameHeader = static_cast<SharedFrameHeader*>(view);
    instance->sharedFrameData = reinterpret_cast<uint8_t*>(instance->sharedFrameHeader + 1);
    instance->sharedFrameDataSize = frameSize;

    instance->sharedFrameHeader->width = 0;
    instance->sharedFrameHeader->height = 0;
    instance->sharedFrameHeader->stride = 0;
    instance->sharedFrameHeader->format = GUID_NULL;
    instance->sharedFrameHeader->frameVersion = 0;

    return true;
}

static bool EnsureSharedMemory(const GUID& clsid, const size_t frameSize, const VCamHandle handle) {
    if (!g_cameras.contains(handle)) {
        return false;
    }

    const std::shared_ptr<VCamInstance> instance = g_cameras[handle];
    return EnsureSharedMemory(clsid, frameSize, instance);
}

using registry_key = winrt::handle_type<registry_traits>;

static bool IsProcessAlive(DWORD pid)
{
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, // minimal rights
        FALSE,
        pid
    );

    if (!hProcess)
        return false; // no such process or no access

    DWORD exitCode = 0;
    bool alive =
        GetExitCodeProcess(hProcess, &exitCode) &&
        exitCode == STILL_ACTIVE;

    CloseHandle(hProcess);
    return alive;
}

static void SetCameraActive(const GUID& clsid, DWORD value) {
    const std::wstring str = GUID_ToStringW(clsid);
    const std::wstring valueName = L"IsActive_" + str;

    registry_key base;
    RegWriteKey(HKEY_CURRENT_USER, L"Software\\LibreConnect_VirtualCamera", base.put());
    RegWriteValue(base.get(), valueName.c_str(), value);
}

static void SetCameraProcess(const GUID& clsid, const DWORD value) {
    const std::wstring str = GUID_ToStringW(clsid);
    const std::wstring valueName = L"Process_" + str;

    registry_key base;
    RegWriteKey(HKEY_CURRENT_USER, L"Software\\LibreConnect_VirtualCamera", base.put());
    RegWriteValue(base.get(), valueName.c_str(), value);
}

static bool IsCameraActive(const GUID& clsid) {
    const std::wstring str = GUID_ToStringW(clsid);

    registry_key base;
    const LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\LibreConnect_VirtualCamera",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_READ,
        nullptr,
        base.put(),
        nullptr
    );

    if (status != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(status);
    }

    DWORD isActive = 0;
    DWORD byProcess = 0;

    DWORD dwordSize = sizeof(DWORD);

    LSTATUS result = RegGetValueW(
       base.get(),
       nullptr,
       std::wstring(L"IsActive_" + str).c_str(),
       RRF_RT_REG_DWORD,
       nullptr,
       &isActive,
       &dwordSize
    );

    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        return HRESULT_FROM_WIN32(result);
    }

    if (isActive == 0) {
        return false;
    }

    result = RegGetValueW(
       base.get(),
       nullptr,
       std::wstring(L"Process_" + str).c_str(),
       RRF_RT_REG_DWORD,
       nullptr,
       &byProcess,
       &dwordSize
    );

    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        return HRESULT_FROM_WIN32(result);
    }

    if (IsProcessAlive(byProcess)) {
        return true;
    }

    return false;
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
            if (IsCameraActive(Cameras_CLSID[i])) {
                continue;
            }

            clsid = Cameras_CLSID[i];
            break;
        }

        if (clsid == GUID_NULL) {
            SetError(std::to_string(Cameras_CLSID.size()).c_str(), handle);
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

            {
                const std::size_t frameSize = GetFrameSize(format, width, height);
                if (!EnsureSharedMemory(instance->clsid, frameSize, instance))
                {
                    SetError("Failed to initialize shared memory", *handle);
                    return VCAM_ERROR_INIT_FAILED;
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

        SetCameraActive(clsid, 1);
        SetCameraProcess(clsid, GetCurrentProcessId());

        return VCAM_SUCCESS;
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

        SetCameraActive(instance->clsid, 0);
        SetCameraProcess(instance->clsid, 0);

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
    static std::map<std::wstring, void*> g_FrameSharedMemoryMap;

    __declspec(dllexport) bool VCamAPI_HasExternalFrame(const GUID& clsid)
    {
        const std::wstring clsid_str = GUID_ToStringW(clsid);
        void* frameMapping = nullptr;

        if (g_FrameSharedMemoryMap.contains(clsid_str)) {
            frameMapping = g_FrameSharedMemoryMap.at(clsid_str);
            goto validate_frame;
        }

        if (!GetFrameSharedMemory(clsid, &frameMapping)) {
            return false;
        }

        g_FrameSharedMemoryMap[clsid_str] = frameMapping;

        validate_frame:
        const SharedFrameHeader* frameHeader = static_cast<SharedFrameHeader*>(frameMapping);
        OutputDebugStringA(std::string("Frame: " + std::to_string(frameHeader->frameVersion)).c_str());
        return frameHeader->frameVersion != 0;
    }

    __declspec(dllexport) bool GetExternalFrame(const GUID& clsid, PushedFrame& frame)
    {
        if (!VCamAPI_HasExternalFrame(clsid)) {
            return false;
        }

        const std::wstring clsid_str = GUID_ToStringW(clsid);
        void* frameMapping = g_FrameSharedMemoryMap.at(clsid_str);

        const SharedFrameHeader* frameSharedHeader = static_cast<SharedFrameHeader*>(frameMapping);
        const void* frameBuffer = static_cast<uint8_t*>(frameMapping) + sizeof(SharedFrameHeader);

        const size_t frameSize = GetFrameSize(frameSharedHeader->format, frameSharedHeader->width, frameSharedHeader->height);
        frame.data.resize(frameSize);

        std::memcpy(frame.data.data(), frameBuffer, frameSize);
        frame.width = frameSharedHeader->width;
        frame.height = frameSharedHeader->height;
        frame.format = frameSharedHeader->format;

        return true;
    }
}