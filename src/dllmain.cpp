#include <Windows.h>
#include "core/mod.h"

static HMODULE g_module = nullptr;

static DWORD WINAPI MainThread(LPVOID)
{
    trinity::Mod::Get().Initialize(g_module);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_module = module;
        DisableThreadLibraryCalls(module);
        // Do real work off the loader lock.
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        trinity::Mod::Get().Shutdown();
        break;
    }
    return TRUE;
}
