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
#include <nlohmann/json.hpp>

winrt::com_array<GUID> Cameras_CLSID;
HMODULE _hModule;

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
	for (auto& clsid : Cameras_CLSID)
	{
		if (clsid == rclsid)
		{
			auto factory = winrt::make_self<ClassFactory>(rclsid);
			return factory->QueryInterface(riid, ppv);
		}
	}

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
	LSTATUS status = RegCreateKeyExW(
		HKEY_LOCAL_MACHINE,
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

	DWORD count = 0;
	DWORD size = sizeof(count);

	RETURN_IF_WIN32_ERROR(RegGetValueW(
	   base.get(),
	   nullptr,
	   L"CameraCount",
	   RRF_RT_REG_DWORD,
	   nullptr,
	   &count,
	   &size));

	Cameras_CLSID = winrt::com_array<GUID>(count);

	for (DWORD i = 0; i < count; ++i)
	{
		std::wstring valueName = L"Camera" + std::to_wstring(i);

		wchar_t guidStr[64]{};
		DWORD guidSize = sizeof(guidStr);

		RETURN_IF_WIN32_ERROR(RegGetValueW(
		   base.get(),
		   nullptr,
		   valueName.c_str(),
		   RRF_RT_REG_SZ,
		   nullptr,
		   guidStr,
		   &guidSize));

		RETURN_IF_FAILED(IIDFromString(guidStr, &Cameras_CLSID[i]));
	}

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

	registry_key base;
	RegWriteKey(HKEY_LOCAL_MACHINE, L"Software\\LibreConnect_VirtualCamera", base.put());
	RegWriteValue(base.get(), L"CameraCount", count);

	const std::wstring exePath = wil::GetModuleFileNameW(_hModule).get();
	WINTRACE(L"DllRegisterServer '%s'", exePath.c_str());

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
	}

	return S_OK;
}

STDAPI DllUnregisterServer()
{
	const std::wstring exePath = wil::GetModuleFileNameW(_hModule).get();
	WINTRACE(L"DllUnregisterServer '%s'", exePath.c_str());

	for (auto& clsid : Cameras_CLSID) {
		auto clsid_wstring = GUID_ToStringW(clsid, false);
		std::wstring path = L"Software\\Classes\\CLSID\\" + clsid_wstring;
		RETURN_IF_WIN32_ERROR(RegDeleteTreeW(HKEY_LOCAL_MACHINE, path.c_str()));
	}

	return S_OK;
}