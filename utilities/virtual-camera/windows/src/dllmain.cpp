#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"
#include "MediaStream.h"
#include "MediaSource.h"
#include "Activator.h"
#include <fstream>
#include <iostream>
#include <sddl.h>
#include <mutex>
#include <nlohmann/json.hpp>

winrt::com_array<GUID> Cameras_CLSID;
HMODULE _hModule;
static std::mutex g_clsidMutex;
static bool g_clsidsLoaded = false;

static HRESULT LoadCamerasCLSIDs();

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		{
			_hModule = hModule;
			WinTraceRegister();
			WINTRACE(L"DllMain DLL_PROCESS_ATTACH '%s'", GetCommandLine());
			DisableThreadLibraryCalls(hModule);
			// Best-effort load; will be retried in DllGetClassObject if it fails.
			LoadCamerasCLSIDs();

			wil::SetResultLoggingCallback([](wil::FailureInfo const& failure) noexcept
				{
					wchar_t str[2048];
					if (SUCCEEDED(wil::GetFailureLogString(str, _countof(str), failure)))
					{
						WinTrace(2, 0, str); // 2 => error
					}
				});
			break;
		}

	case DLL_PROCESS_DETACH:
		WINTRACE(L"DllMain DLL_PROCESS_DETACH '%s'", GetCommandLine());
		WinTraceUnregister();
		break;
	}
	return TRUE;
}

struct ClassFactory : winrt::implements<ClassFactory, IClassFactory>
{
	GUID _cameraClsid;
	explicit ClassFactory(const GUID clsid) : _cameraClsid(clsid) {}

	STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** result) noexcept override {
		if (outer) return CLASS_E_NOAGGREGATION;

		auto act = winrt::make_self<Activator>();
		act->Initialize(_cameraClsid);
		return act->QueryInterface(riid, result);
	}

	STDMETHODIMP LockServer(BOOL) noexcept final
	{
		return S_OK;
	}
};

__control_entrypoint(DllExport)
STDAPI DllCanUnloadNow()
{
	if (winrt::get_module_lock())
	{
		WINTRACE(L"DllCanUnloadNow S_FALSE");
		return S_FALSE;
	}

	winrt::clear_factory_cache();
	WINTRACE(L"DllCanUnloadNow S_OK");
	return S_OK;
}

_Check_return_
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
	// Retry CLSID loading if DllMain's attempt failed (e.g., Frame Server
	// loaded us before the registry keys were fully committed).
	{
		std::lock_guard<std::mutex> lock(g_clsidMutex);
		if (!g_clsidsLoaded || Cameras_CLSID.size() == 0)
		{
			OutputDebugStringA("[VCam] DllGetClassObject: CLSIDs not loaded, retrying LoadCamerasCLSIDs\n");
			LoadCamerasCLSIDs();
		}
	}

	for (auto& clsid : Cameras_CLSID)
	{
		if (clsid == rclsid)
		{
			auto factory = winrt::make_self<ClassFactory>(rclsid);
			return factory->QueryInterface(riid, ppv);
		}
	}

	wchar_t requestedStr[64]{};
	StringFromGUID2(rclsid, requestedStr, _countof(requestedStr));
	OutputDebugStringW((std::wstring(L"[VCam] DllGetClassObject: CLSID not found: ") + requestedStr
		+ L", loaded count: " + std::to_wstring(Cameras_CLSID.size()) + L"\n").c_str());

	return CLASS_E_CLASSNOTAVAILABLE;
}

using registry_key = winrt::handle_type<registry_traits>;

static uint8_t ReadConfigCount() {
	std::ifstream settingsStream("VirtualCameraSettings.json");

	if (!settingsStream.is_open()) {
		return 16;
	}

	nlohmann::json json;

	try {
		json = nlohmann::json::parse(settingsStream);
	} catch (const std::exception& ex) {
		return 16;
	}

	if (!json.contains("VirtualCamera_MaxCount") || !json["VirtualCamera_MaxCount"].is_number_unsigned()) {
		return 16;
	}

	return json["VirtualCamera_MaxCount"].get<uint8_t>();
}

static HRESULT LoadCamerasCLSIDs()
{
	registry_key base;
	LSTATUS status = RegOpenKeyExW(
		HKEY_LOCAL_MACHINE,
		L"Software\\LibreConnect_VirtualCamera",
		0,
		KEY_READ,
		base.put()
	);

	if (status != ERROR_SUCCESS)
	{
		OutputDebugStringA("[VCam] LoadCamerasCLSIDs: Failed to open registry key Software\\LibreConnect_VirtualCamera\n");
		g_clsidsLoaded = false;
		return HRESULT_FROM_WIN32(status);
	}

	DWORD count = 0;
	DWORD size = sizeof(count);

	status = RegGetValueW(
		base.get(),
		nullptr,
		L"CameraCount",
		RRF_RT_REG_DWORD,
		nullptr,
		&count,
		&size);

	if (status != ERROR_SUCCESS)
	{
		OutputDebugStringA("[VCam] LoadCamerasCLSIDs: Failed to read CameraCount\n");
		g_clsidsLoaded = false;
		return HRESULT_FROM_WIN32(status);
	}

	winrt::com_array<GUID> loadedClsids(count);

	for (DWORD i = 0; i < count; ++i)
	{
		std::wstring valueName = L"Camera" + std::to_wstring(i);

		wchar_t guidStr[64]{};
		DWORD guidSize = sizeof(guidStr);

		status = RegGetValueW(
			base.get(),
			nullptr,
			valueName.c_str(),
			RRF_RT_REG_SZ,
			nullptr,
			guidStr,
			&guidSize);

		if (status != ERROR_SUCCESS)
		{
			OutputDebugStringA("[VCam] LoadCamerasCLSIDs: Failed to read Camera GUID from registry\n");
			g_clsidsLoaded = false;
			return HRESULT_FROM_WIN32(status);
		}

		HRESULT hr = IIDFromString(guidStr, &loadedClsids[i]);
		if (FAILED(hr))
		{
			OutputDebugStringA("[VCam] LoadCamerasCLSIDs: Failed to parse Camera GUID string\n");
			g_clsidsLoaded = false;
			return hr;
		}
	}

	Cameras_CLSID = std::move(loadedClsids);
	g_clsidsLoaded = true;
	OutputDebugStringA("[VCam] LoadCamerasCLSIDs: Successfully loaded CLSIDs\n");
	return S_OK;
}

STDAPI DllRegisterServer()
{
	const uint32_t count = ReadConfigCount();

	const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\LibreConnect_VirtualCamera");
	if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
	{
		return HRESULT_FROM_WIN32(status);
	}

	const std::wstring exePath = wil::GetModuleFileNameW(_hModule).get();
	WINTRACE(L"DllRegisterServer '%s'", exePath.c_str());

	const LPCWSTR sddl =
		   L"D:P"
		   L"(A;;KA;;;SY)"   // SYSTEM full
		   L"(A;;KA;;;BA)"   // Administrators full
		   L"(A;;KRKW;;;BU)" // Users read/write
		   L"(A;;KR;;;LS)"   // Local Service read
		   L"(A;;KR;;;AC)";  // All Application Packages read

	PSECURITY_DESCRIPTOR sd = nullptr;
	ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr);

	registry_key base;
	LSTATUS createStatus = RegCreateKeyExW(
		HKEY_LOCAL_MACHINE,
		L"Software\\LibreConnect_VirtualCamera",
		0, nullptr, REG_OPTION_NON_VOLATILE,
		KEY_ALL_ACCESS | WRITE_DAC,
		nullptr, base.put(), nullptr
	);

	if (createStatus == ERROR_SUCCESS) {
		RegSetKeySecurity(base.get(), DACL_SECURITY_INFORMATION, sd);
	}

	RegWriteValue(base.get(), L"CameraCount", count);

	for (uint32_t i = 0; i < count; ++i)
	{
		GUID clsid;
		CoCreateGuid(&clsid);

		std::wstring valueName = L"Camera" + std::to_wstring(i);
		RegWriteValue(base.get(), valueName.c_str(), GUID_ToStringW(clsid));

		auto clsid_wstring = GUID_ToStringW(clsid, false);
		std::wstring path = L"Software\\Classes\\CLSID\\" + clsid_wstring + L"\\InprocServer32";

		registry_key key;
		RETURN_IF_WIN32_ERROR(RegWriteKey(HKEY_LOCAL_MACHINE, path.c_str(), key.put()));
		RETURN_IF_WIN32_ERROR(RegWriteValue(key.get(), nullptr, exePath));
		RETURN_IF_WIN32_ERROR(RegWriteValue(key.get(), L"ThreadingModel", L"Both"));

		std::wstring configPath = L"SOFTWARE\\LibreConnect_VirtualCamera_Configs\\" + clsid_wstring;
		HKEY configKey;

		RETURN_IF_WIN32_ERROR(RegCreateKeyExW(HKEY_LOCAL_MACHINE, configPath.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | WRITE_DAC, nullptr, &configKey, nullptr));
		RETURN_IF_WIN32_ERROR(RegSetKeySecurity(configKey, DACL_SECURITY_INFORMATION, sd));
		RegCloseKey(configKey);
	}

	LocalFree(sd);

	return S_OK;
}

STDAPI DllUnregisterServer()
{
	const std::wstring exePath = wil::GetModuleFileNameW(_hModule).get();
	WINTRACE(L"DllUnregisterServer '%s'", exePath.c_str());

	// Ensure CLSIDs are loaded so we can clean up their InprocServer32 entries.
	if (Cameras_CLSID.size() == 0)
	{
		LoadCamerasCLSIDs();
	}

	for (auto& clsid : Cameras_CLSID) {
		auto clsid_wstring = GUID_ToStringW(clsid, false);

		// Remove InprocServer32 registration
		std::wstring clsidPath = L"Software\\Classes\\CLSID\\" + clsid_wstring;
		RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidPath.c_str());

		// Remove config key
		std::wstring configPath = L"SOFTWARE\\LibreConnect_VirtualCamera_Configs\\" + clsid_wstring;
		RegDeleteTreeW(HKEY_LOCAL_MACHINE, configPath.c_str());
	}

	// Remove the main CLSID list key
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\LibreConnect_VirtualCamera");

	return S_OK;
}