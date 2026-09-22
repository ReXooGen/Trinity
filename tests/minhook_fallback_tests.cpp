#include <Windows.h>
#include <MinHook.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    using TargetFn = int(__fastcall*)();
    TargetFn g_original = nullptr;

    // This is intentionally a normal compiled function: MinHook must accept
    // the detour exactly as it does in Trinity, while the synthetic target is
    // isolated in a deliberately reservation-fragmented address range.
    __declspec(noinline) int __fastcall Detour()
    {
        return g_original ? g_original() + 1 : -1;
    }

    bool Expect(bool condition, const char* message)
    {
        if (condition) return true;
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
}

int main()
{
#if !defined(_M_X64) && !defined(__x86_64__)
    std::puts("MinHook absolute fallback test is x64-only");
    return 0;
#else
    // Reserve MinHook's whole +/-512 MiB search window around our target.
    // The single committed page is executable target code, but every 64 KiB
    // candidate where upstream MinHook could place a rel32 relay is reserved.
    // A successful hook therefore proves the 14-byte absolute fallback path.
    constexpr SIZE_T kNearSearchRange = 0x40000000ull;
    constexpr SIZE_T kPageSize = 0x1000;
    auto* const reservation = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, kNearSearchRange, MEM_RESERVE, PAGE_NOACCESS));
    if (!Expect(reservation != nullptr, "could not reserve the synthetic near-hook window"))
        return 1;

    auto* const targetPage = static_cast<uint8_t*>(VirtualAlloc(
        reservation + kNearSearchRange / 2, kPageSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!Expect(targetPage != nullptr, "could not commit the synthetic hook target"))
    {
        VirtualFree(reservation, 0, MEM_RELEASE);
        return 1;
    }

    // mov eax,7; ten NOPs; ret.  The 14-byte patch replaces whole
    // instructions, leaving one NOP before ret for the trampoline's return.
    const uint8_t code[] = {
        0xB8, 0x07, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0xC3,
    };
    std::memcpy(targetPage, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), targetPage, sizeof(code));
    const auto target = reinterpret_cast<TargetFn>(targetPage);

    bool ok = Expect(target() == 7, "synthetic target must run before hooking");
    if (ok)
        ok = Expect(MH_Initialize() == MH_OK, "MinHook must initialize");
    if (ok)
        ok = Expect(MH_CreateHook(reinterpret_cast<void*>(target),
                                  reinterpret_cast<void*>(&Detour),
                                  reinterpret_cast<void**>(&g_original)) == MH_OK,
                    "fragmented near window must use the absolute-hook fallback");
    if (ok)
        ok = Expect(MH_EnableHook(reinterpret_cast<void*>(target)) == MH_OK,
                    "absolute-hook fallback must enable");
    if (ok)
        ok = Expect(target() == 8 && g_original != nullptr,
                    "absolute hook must reach the detour and its trampoline");
    if (g_original)
    {
        if (MH_DisableHook(reinterpret_cast<void*>(target)) != MH_OK) ok = false;
        if (ok)
            ok = Expect(target() == 7, "disabling the absolute hook must restore all 14 bytes");
        if (MH_RemoveHook(reinterpret_cast<void*>(target)) != MH_OK) ok = false;
    }
    MH_Uninitialize();
    VirtualFree(reservation, 0, MEM_RELEASE);
    return ok ? 0 : 1;
#endif
}
