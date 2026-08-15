#pragma once

namespace trinity::hooks
{
    // Grabs the DX12 present/queue vtable pointers from a throwaway device and
    // detours them with MinHook. Must be called after MH_Initialize().
    bool InstallDX12Hooks();
    void RemoveDX12Hooks();
}
