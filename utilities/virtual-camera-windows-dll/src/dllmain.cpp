#include <windows.h>
#include <DebugLog.h>

BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        Debug::Log("DllMain DDL_PROCESS_ATACH {}", GetCommandLine());
        DisableThreadLibraryCalls(hModule);
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_DETACH) {
        Debug::Log("DllMain DLL_PROCESS_DETACH {}", GetCommandLine());
        return TRUE;
    }

    Debug::Log("{}", GetCommandLine());
    return TRUE;
}