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
static std::map<VCamHandle, std::shared_ptr<VCamInstance>> g_cameras;
static std::map<VCamHandle, std::string> g_lastErrors;

struct SharedFrameHeader
{
    volatile LONG frameVersion;
};

struct VCamInstance
{
    std::wstring name;
    UINT width;
    UINT height;
    UINT fps;
    GUID format;
    GUID clsid;
    wil::com_ptr_nothrow<IMFVirtualCamera> vcam;

    VCamHandle handle;
    HANDLE sharedFrameMapping = nullptr;
    HANDLE sharedMutexMapping = nullptr;
    SharedFrameHeader* sharedFrameHeader = nullptr;
    BYTE* sharedFrameData = nullptr;
    size_t sharedFrameDataSize = 0;

    VCamInstance() = delete;
    explicit VCamInstance(const std::wstring& name, const UINT width, const UINT height, const UINT fps, const GUID format, const GUID clsid)
        : name(name), width(width), height(height), fps(fps), format(format), clsid(clsid), handle(nullptr) {
    }

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
        }

        if (sharedFrameMapping)
        {
            CloseHandle(sharedFrameMapping);
        }

        if (sharedMutexMapping) {
            CloseHandle(sharedMutexMapping);
        }

        DestroyCam(handle);
    }
};

static void AddPrefixToError(const char* prefix, const VCamHandle handle) {
    const std::string lastError = VCamGetLastError(handle);
    g_lastErrors[handle] = prefix + lastError;
}

static void ClearError(const VCamHandle handle) {
    g_lastErrors[handle] = "";
}

static void SetError(const char* msg, const VCamHandle handle) {
    g_lastErrors[handle] = msg ? msg : "";
}

static void SetError(const char* msg, const VCamHandle* handle) {
    if (handle != nullptr) {
        g_lastErrors[*handle] = msg ? msg : "";
    }
}

static void SetError(const HRESULT hr, const VCamHandle* handle) {
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
    const std::wstring clsidStr = GUID_ToStringW(clsid);
    const std::wstring regPath = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + clsidStr + L"\\InprocServer32";
    HKEY hKey;
    const LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

static bool CreatePermissiveSecurityAttr(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& pSD)
{
    const wchar_t* sddl =  L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GWGR;;;IU)";

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

static bool OpenSharedMemory(const std::shared_ptr<VCamInstance>& instance) {
    const std::wstring clsid_str = GUID_ToStringW(instance->clsid);
    const size_t frameSize = GetFrameSize(instance->format, instance->width, instance->height);

    const size_t totalSize = sizeof(SharedFrameHeader) + frameSize;
    const auto name = L"Global\\LibreConnect_VirtualCameraFrame_" + clsid_str;
    const auto mutex= L"Global\\LibreConnect_VirtualCameraFrameMutex_" + clsid_str;

    const HANDLE sharedFrameMapping = OpenFileMappingW(
            FILE_MAP_ALL_ACCESS,
            FALSE,
            name.c_str()
    );

    if (!sharedFrameMapping) {
        SetError(fmt::format("Opening shared frame mapping failed ({})", GetLastError()).c_str(), instance->handle);
        return false;
    }

    const SharedFrameHeader* sharedFrameHeaderView = static_cast<SharedFrameHeader*>(MapViewOfFile(
        sharedFrameMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SharedFrameHeader))
    );

    if (!sharedFrameHeaderView) {
        SetError(fmt::format("Opening shared frame header view failed ({})", GetLastError()).c_str(), instance->handle);
        CloseHandle(sharedFrameMapping);
        return false;
    }

    void* sharedFrameView = MapViewOfFile(
        sharedFrameMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        frameSize + sizeof(SharedFrameHeader)
    );

    if (!sharedFrameView) {
        SetError(fmt::format("Opening shared frame view failed ({})", GetLastError()).c_str(), instance->handle);
        CloseHandle(sharedFrameMapping);
        return false;
    }

    const HANDLE mutexMapping = OpenMutexW(
    MUTEX_ALL_ACCESS,
        false,
        mutex.c_str()
    );

    if (!mutexMapping) {
        SetError(fmt::format("Opening shared mutex mapping failed ({})", GetLastError()).c_str(), instance->handle);
        CloseHandle(sharedFrameMapping);
        return false;
    }

    instance->sharedMutexMapping = mutexMapping;
    instance->sharedFrameMapping = sharedFrameMapping;
    instance->sharedFrameHeader = static_cast<SharedFrameHeader*>(sharedFrameView);
    instance->sharedFrameData = static_cast<uint8_t*>(sharedFrameView) + sizeof(SharedFrameHeader);
    instance->sharedFrameDataSize = frameSize;

    return true;
}

using registry_key = winrt::handle_type<registry_traits>;

static bool IsProcessAlive(DWORD pid)
{
    const HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        pid
    );

    if (!hProcess)
        return false;

    DWORD exitCode = 0;
    const bool alive =
        GetExitCodeProcess(hProcess, &exitCode) &&
        exitCode == STILL_ACTIVE;

    CloseHandle(hProcess);
    return alive;
}

static void SetCameraActive(const GUID& clsid, const DWORD value) {
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
            const auto mfFormat = GetMfFormat(format);
            const auto instance = std::make_shared<VCamInstance>(StringToWString(name), width, height, fps, mfFormat, clsid);
            const auto clsid_str = GUID_ToStringW_Simple(instance->clsid);
            const auto format_str = GUID_ToStringW_Simple(mfFormat);

            {
                const std::wstring regPath = L"SOFTWARE\\LibreConnect_VirtualCamera_Configs\\" + clsid_str;
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, nullptr,
                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
                {
                    RegSetValueExW(hKey, L"Width", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&width), sizeof(width));
                    RegSetValueExW(hKey, L"Height", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&height), sizeof(height));
                    RegSetValueExW(hKey, L"Fps", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&fps), sizeof(fps));
                    RegSetValueExW(hKey, L"Format", 0, REG_SZ, reinterpret_cast<const BYTE*>(&format_str[0]), (format_str.size() + 1) * sizeof(wchar_t));
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
            instance->handle = *handle;
            g_cameras[*handle] = instance;
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

        const auto id = GUID_ToStringW_Simple(instance->clsid);

        if (g_cameras.contains(handle)) {
            g_cameras.erase(handle);
        }

        return VCAM_SUCCESS;
    }

    VCAMAPI_API VCamResult PushCamFrame(const VCamHandle handle, const void* data)
    {
        ClearError(handle);

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
        const size_t frameSize = GetFrameSize(instance->format, instance->width, instance->height);

        if (!OpenSharedMemory(instance))
        {
            AddPrefixToError("Failed to open shared memory: ", handle);
            return VCAM_ERROR_FRAME_PUSH_FAILED;
        }

        if (frameSize > instance->sharedFrameDataSize) {
            SetError("Shared frame buffer too small", handle);
            return VCAM_ERROR_FRAME_PUSH_FAILED;
        }

        const DWORD waitResult = WaitForSingleObject(instance->sharedMutexMapping, 100);

        if (waitResult != WAIT_OBJECT_0) {
            return VCAM_ERROR_FRAME_PUSH_FAILED;
        }

        memcpy(instance->sharedFrameData, data, frameSize);
        ReleaseMutex(instance->sharedMutexMapping);
        InterlockedIncrement(&instance->sharedFrameHeader->frameVersion);

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
    struct VCamInstanceMinimal {
        UINT frameSize;
        GUID clsid;
        HANDLE sharedFrameMapping = nullptr;
        HANDLE sharedMutexMapping = nullptr;
        SharedFrameHeader* sharedFrameHeader = nullptr;
        BYTE* sharedFrameData = nullptr;
        size_t sharedFrameDataSize = 0;

        ~VCamInstanceMinimal() {
            if (sharedFrameHeader)
            {
                UnmapViewOfFile(sharedFrameHeader);
            }

            if (sharedFrameMapping)
            {
                CloseHandle(sharedFrameMapping);
            }

            if (sharedMutexMapping) {
                CloseHandle(sharedMutexMapping);
            }
        }
    };

    static std::map<std::wstring, std::shared_ptr<VCamInstanceMinimal>> g_cameraInstancesExtern;
    static std::mutex g_cameraInstancesExternMutex;

    VCAMAPI_API bool InitializeCameraInstance(const GUID& clsid, const UINT width, const UINT height, const GUID format) {
        std::lock_guard<std::mutex> lock(g_cameraInstancesExternMutex);
        const auto clsid_str = GUID_ToStringW_Simple(clsid);

        OutputDebugStringA(std::to_string(GetCurrentProcessId()).c_str());

        if (g_cameraInstancesExtern.contains(clsid_str)) {
            return true;
        }

        OutputDebugStringA("Called");

        const size_t frameSize = GetFrameSize(format, width, height);
        const size_t totalSize = sizeof(SharedFrameHeader) + frameSize;
        const auto name = L"Global\\LibreConnect_VirtualCameraFrame_" + GUID_ToStringW_Simple(clsid);
        const auto mutex= L"Global\\LibreConnect_VirtualCameraFrameMutex_" + GUID_ToStringW_Simple(clsid);

        const std::shared_ptr<VCamInstanceMinimal> instance = std::make_shared<VCamInstanceMinimal>();
        g_cameraInstancesExtern[clsid_str] = instance;

        SECURITY_ATTRIBUTES sa;
        PSECURITY_DESCRIPTOR pSD = nullptr;
        if (!CreatePermissiveSecurityAttr(sa, pSD)) {
            OutputDebugStringA(fmt::format("[InitializeCameraInstance] Create permissive security attribute failed ({})", GetLastError()).c_str());
            return false;
        }

        const HANDLE frameMapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            &sa,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(totalSize),
            name.c_str()
        );

        if (pSD) LocalFree(pSD);

        if (!frameMapping) {
            OutputDebugStringA(fmt::format("[InitializeCameraInstance] Frame mapping failed ({})", GetLastError()).c_str());
            return false;
        }

        void* view = MapViewOfFile(frameMapping, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);

        if (!view) {
            OutputDebugStringA(fmt::format("[InitializeCameraInstance] Frame mapping view failed ({})", GetLastError()).c_str());
            return false;
        }

        pSD = nullptr;
        if (!CreatePermissiveSecurityAttr(sa, pSD)) {
            return false;
        }

        const HANDLE mutexMapping = CreateMutexW(
            &sa,
            FALSE,
            mutex.c_str()
        );

        if (pSD) LocalFree(pSD);

        if (!mutexMapping) {
            OutputDebugStringA(fmt::format("[InitializeCameraInstance] Mutex mapping failed ({})", GetLastError()).c_str());
            return false;
        }

        instance->sharedFrameMapping              = frameMapping;
        instance->sharedFrameHeader               = static_cast<SharedFrameHeader*>(view);
        instance->sharedFrameHeader->frameVersion = 0;
        instance->sharedFrameData                 = static_cast<uint8_t*>(frameMapping) + sizeof(SharedFrameHeader);
        instance->sharedFrameDataSize             = frameSize;
        instance->sharedMutexMapping              = mutexMapping;
        instance->frameSize                       = frameSize;

        return true;
    }

    VCAMAPI_API bool HasCameraPendingExternalFrame(const GUID& clsid) {
        const std::wstring clsid_str = GUID_ToStringW(clsid);
        std::shared_ptr<VCamInstanceMinimal> instance;

        {
            std::lock_guard<std::mutex> lock(g_cameraInstancesExternMutex);
            if (!g_cameraInstancesExtern.contains(clsid_str)) {
                return false;
            }

            instance = g_cameraInstancesExtern.at(clsid_str);
        }

        return InterlockedAdd(&instance->sharedFrameHeader->frameVersion, 0) != 0;
    }

    VCAMAPI_API bool HasCameraPendingExternalFrame(const std::shared_ptr<VCamInstanceMinimal>& instance) {
        return InterlockedAdd(&instance->sharedFrameHeader->frameVersion, 0) != 0;
    }

    VCAMAPI_API bool GetCameraExternalFrame(const GUID& clsid, PushedFrame& frame) {
        const std::wstring clsid_str = GUID_ToStringW(clsid);
        std::lock_guard<std::mutex> lock(g_cameraInstancesExternMutex);

        if (!g_cameraInstancesExtern.contains(clsid_str)) {
            return false;
        }

        const std::shared_ptr<VCamInstanceMinimal> instance = g_cameraInstancesExtern.at(clsid_str);

        if (!HasCameraPendingExternalFrame(instance)) {
            return false;
        }

        frame.data.resize(instance->frameSize);

        const DWORD waitResult = WaitForSingleObject(instance->sharedMutexMapping, 100);
        if (waitResult != WAIT_OBJECT_0) {
            return false;
        }

        std::memcpy(frame.data.data(), instance->sharedFrameData, instance->frameSize);
        ReleaseMutex(instance->sharedMutexMapping);

        return true;
    }
}