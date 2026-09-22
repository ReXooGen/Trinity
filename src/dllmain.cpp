#include <Windows.h>

#include "core/crash_diagnostics.h"
#include "core/mod.h"

namespace {

DWORD WINAPI MainThread(LPVOID parameter)
{
    HMODULE module = static_cast<HMODULE>(parameter);
    trinity::core::CrashDiagnostics::InitializeSession(module);
    trinity::Mod::Get().Initialize(module);
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(module);
        trinity::core::CrashDiagnostics::InstallUnhandledFilter(module);
        HANDLE thread = CreateThread(nullptr, 0, MainThread, module, 0, nullptr);
        if (thread) CloseHandle(thread);
        break;
    }
    case DLL_PROCESS_DETACH:
        trinity::Mod::Get().Shutdown();
        trinity::core::CrashDiagnostics::Shutdown();
        break;
    }
    return TRUE;
}
