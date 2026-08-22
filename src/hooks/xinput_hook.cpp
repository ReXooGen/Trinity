#include "xinput_hook.h"

#include <MinHook.h>

#include "../core/state.h"

#pragma comment(lib, "xinput9_1_0.lib")

namespace trinity::hooks
{
    using XInputGetState_t = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

    // One trampoline per known module. The game and our own overlay may link
    // different xinput versions, and each hook must forward to its own original.
    static XInputGetState_t o_1_4   = nullptr;
    static XInputGetState_t o_1_3   = nullptr;
    static XInputGetState_t o_9_1_0 = nullptr;

    // Any real-state trampoline, used by the menu to read the pad it's blocking.
    static XInputGetState_t g_read = nullptr;

    static void Neutralize(XINPUT_STATE* s)
    {
        // Only strip the buttons the menu itself uses (d-pad, A/B/X, and the
        // LB/RB section switchers). The thumbsticks, triggers and every other
        // button stay live so the player can still move and look around with
        // the menu open.
        constexpr WORD kMenuButtons =
            XINPUT_GAMEPAD_DPAD_UP  | XINPUT_GAMEPAD_DPAD_DOWN |
            XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT |
            XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X |
            XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;

        if (s->Gamepad.wButtons & kMenuButtons)
        {
            s->Gamepad.wButtons &= ~kMenuButtons;
            ++s->dwPacketNumber; // bump so the game registers the state as changed
        }
    }

    // A distinct detour per module so each can call the matching trampoline
    // (MinHook can't tell us which target a shared detour was invoked for).
    #define TRINITY_XINPUT_DETOUR(NAME, ORIG)                              \
        static DWORD WINAPI NAME(DWORD i, XINPUT_STATE* s)                 \
        {                                                                  \
            const DWORD r = ORIG(i, s);                                    \
            if (r == ERROR_SUCCESS && s && State::Get().menuOpen)         \
                Neutralize(s);                                             \
            return r;                                                      \
        }
    TRINITY_XINPUT_DETOUR(hk_1_4,   o_1_4)
    TRINITY_XINPUT_DETOUR(hk_1_3,   o_1_3)
    TRINITY_XINPUT_DETOUR(hk_9_1_0, o_9_1_0)
    #undef TRINITY_XINPUT_DETOUR

    struct Target
    {
        const wchar_t*    dll;
        void*             detour;
        XInputGetState_t* original;
        bool              done; // hooked, or confirmed nothing to hook here
    };

    static Target g_targets[] = {
        { L"xinput1_4.dll",   reinterpret_cast<void*>(&hk_1_4),   &o_1_4,   false },
        { L"xinput1_3.dll",   reinterpret_cast<void*>(&hk_1_3),   &o_1_3,   false },
        { L"xinput9_1_0.dll", reinterpret_cast<void*>(&hk_9_1_0), &o_9_1_0, false },
    };

    void EnsureXInputHooks()
    {
        static bool s_allDone = false;
        if (s_allDone)
            return;

        bool anyPending = false;
        for (auto& t : g_targets)
        {
            if (t.done)
                continue;

            HMODULE mod = GetModuleHandleW(t.dll);
            if (!mod)
            {
                anyPending = true; // may still load (game inits input after us)
                continue;
            }

            FARPROC proc = GetProcAddress(mod, "XInputGetState");
            if (!proc)
            {
                t.done = true; // module present but no export - never retry it
                continue;
            }

            if (MH_CreateHook(reinterpret_cast<void*>(proc), t.detour,
                              reinterpret_cast<void**>(t.original)) != MH_OK)
            {
                anyPending = true; // transient; try again next frame
                continue;
            }

            // Publish the reader before enabling so a concurrent XInputReadReal
            // never falls through to a now-patched plain export.
            if (!g_read)
                g_read = *t.original;

            if (MH_EnableHook(reinterpret_cast<void*>(proc)) == MH_OK)
            {
                t.done = true;
            }
            else
            {
                anyPending = true;
            }
        }

        if (!anyPending)
            s_allDone = true;
    }

    void RemoveXInputHooks()
    {
        // The detours are torn down by MH_DisableHook(MH_ALL_HOOKS) /
        // MH_Uninitialize during shutdown; just reset our bookkeeping.
        for (auto& t : g_targets)
        {
            t.done      = false;
            *t.original = nullptr;
        }
        g_read = nullptr;
    }

    DWORD XInputReadReal(DWORD userIndex, XINPUT_STATE* state)
    {
        if (g_read)
            return g_read(userIndex, state);
        return XInputGetState(userIndex, state); // hooks not up yet
    }
}
