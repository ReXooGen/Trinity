#pragma once
#include <Windows.h>
#include <Xinput.h>

namespace trinity::hooks
{
    // Detours XInputGetState so the pad is neutralised for the GAME while the
    // menu is open - stops nav buttons (A/B, d-pad, RB+X) leaking through. The
    // menu reads the real pad via XInputReadReal, so its own navigation still
    // works while it blocks the game.
    //
    // Safe to call every frame: it hooks whichever xinput module has since
    // loaded (the game often inits its input system after we do) and becomes a
    // no-op once every known module is accounted for.
    void EnsureXInputHooks();
    void RemoveXInputHooks();

    // Real pad state, bypassing the menu-open neutralisation applied to the
    // game. Falls back to the plain export until the hooks are up.
    DWORD XInputReadReal(DWORD userIndex, XINPUT_STATE* state);

    // Returns true if a native DualSense or DS4 controller is currently connected.
    bool IsDualSenseConnected();

    // Returns human-readable model and connection string (e.g. "DualSense (USB)").
    const char* GetDualSenseName();
}
