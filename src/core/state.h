#pragma once

#include <vector>

namespace trinity
{
    // Sentinel bits (outside the real XInput wButtons range) that let the two
    // analog triggers act as held "buttons" in a pad mask - used by the Free
    // Flight binds (flyUpPadMask / flyDownPadMask) and shared between the
    // movement hook that polls them (teleport.cpp) and the Keybinds submenu
    // that captures them (framework.cpp / menu.cpp), so both agree on what
    // "trigger held" means.
    constexpr unsigned int kPadLTrigger = 0x10000u; // Left Trigger
    constexpr unsigned int kPadRTrigger = 0x20000u; // Right Trigger

    // Shared runtime state, accessed from the render thread (menu) and the
    // window thread (input). Simple scalars only - no locking needed for
    // these plain toggles.
    struct State
    {
        bool menuOpen = false;

        // True while a ui::Search row is capturing typed text. The window
        // hook swallows ALL keyboard input from the game while this is set,
        // so typing a search never moves the player.
        bool textCapture = false;

        // Menu open/close binding (framework.cpp PollMenuToggle /
        // PollToggleCombo), rebindable from the SYSTEM tab's Keybinds submenu
        // and persisted in Trinity.ini. openKeyVk is a Win32 virtual-key code
        // (default VK_INSERT); openPadMask is an XInput button mask that must
        // be held in full - a single button or a combo (default LB + D-Pad
        // Down).
        int          openKeyVk  = 0x2D;          // VK_INSERT
        unsigned int openPadMask = 0x0100 | 0x0002; // LB | DPAD_DOWN

        // Transient: a Keybinds-submenu row is listening for the next key /
        // button to bind. While set the window hook swallows all keyboard
        // input, the menu suspends navigation, and the open toggle is ignored,
        // so the press being captured neither moves the player nor navigates
        // nor closes the menu. Never persisted.
        bool rebindCapture = false;

        // Player stat & combat features (player.cpp).
        bool godMode         = false;
        bool oneHitKill      = false;
        bool infDurability   = false;
        bool noFallDamage    = true;
        bool infStamina      = false;
        bool infMountStamina = false;
        bool infSpirit       = false;
        bool easyParry       = false;
        bool easyEvade       = false;
        bool noBounty        = false;

        // Battle-damage multipliers (player.cpp). Applied to the signed HP
        // delta at the damage-apply dispatcher; 1.0 = game behavior.
        float dmgOutMult = 1.0f; // player -> enemy
        float dmgInMult  = 1.0f; // enemy  -> player

        // Movement features (teleport.cpp hkMoveUpdate). Scale the character
        // physics proxy's desired-velocity vector each movement tick: Super Run
        // multiplies horizontal speed, Super Jump multiplies upward (rising)
        // velocity so jumps/launches go higher. 1.0x = game behavior.
        bool  superRun      = false;
        float superRunMult  = 2.0f;
        bool  superJump     = false;
        float superJumpMult = 2.0f;

        // Free Flight (teleport.cpp hkLocoStep). Turns the crow-wing glide -
        // which only ever sinks - into vertical control. While enabled and
        // airborne, HOLD the ascend key to rise or the descend key to sink at
        // flightSpeed units/tick; release and normal physics resume, so jumps
        // and aerial attacks are untouched (no hover clamp). The keys are Win32
        // virtual-key codes; defaults Caps Lock (up) and Ctrl (down). Rebindable
        // (with openKeyVk/openPadMask) from the SYSTEM tab's Keybinds submenu
        // and persisted in Trinity.ini regardless of Auto Save.
        bool  freeFlight   = false;
        float flightSpeed  = 8.0f;
        int   flyUpKeyVk   = 0x14; // VK_CAPITAL (Caps Lock)
        int   flyDownKeyVk = 0x11; // VK_CONTROL (Ctrl)
        // Controller equivalents - XInput button masks that must be held in
        // full (same model as openPadMask). kPadLTrigger/kPadRTrigger (above)
        // stand in for the analog triggers, which aren't wButtons bits.
        // Defaults: RB rises, Right Trigger sinks. 0 disables that direction
        // on the pad. (RB = XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200; 0x0008
        // would be D-Pad Right.)
        unsigned int flyUpPadMask   = 0x0200;  // Right Shoulder (RB)
        unsigned int flyDownPadMask = 0x20000; // Right Trigger (RT)

        // Map Marker Teleport (teleport.cpp). Teleports to the custom waypoint
        // placed on the world map. Rebindable from the Keybinds submenu (default
        // F10 key) and persisted in Trinity.ini. Fallback height is used when the
        // marker has no elevation (sky insertion).
        int          markerTeleportKeyVk  = 0x79;  // VK_F10
        unsigned int markerTeleportPadMask = 0;     // Disabled on pad by default
        float        markerFallbackHeight = 1200.0f;

        // Trust Multiplier (friendly.cpp). Scales the trust ("Friendly")
        // GAINED when gifting NPCs or feeding/taming animals, at the friendly-
        // apply funnel. Only real interactions are scaled (save-load is not);
        // the value still caps at the game's max (100), so a high multiplier
        // just reaches max / tames in fewer gifts. 1.0x = game behavior.
        bool  trustMult    = false;
        float trustMultVal = 3.0f;

        // World features (world.cpp). Game Speed forces the engine's fixed
        // frame-timestep so the whole simulation runs at gameSpeedMult of the
        // 60-FPS-equivalent rate; the toggle off restores the engine's own
        // real-time delta.
        bool  gameSpeed     = false;
        float gameSpeedMult = 1.0f;

        // Time of Day (world.cpp). Freeze holds BOTH the numeric clock (via the
        // field-time tick hook, delta 0) AND the visible sun (via the render
        // manager's lower==upper clamp) while set, with the rest of the sim
        // untouched - the sun needs its own layer or it keeps moving.
        // Advance is a one-shot action (menu.cpp), not persisted state.
        bool  timeFrozen = false;

        // Weather & Atmosphere controls (world.cpp)
        bool  forceClearSky    = false;
        float rainIntensity    = 0.0f;  // 0.00 .. 1.00
        float snowIntensity    = 0.0f;  // 0.00 .. 1.00
        float dustIntensity    = 0.0f;  // 0.00 .. 1.00
        float windMultiplier   = 1.0f;  // 0.00 .. 5.00
        float windGust         = 1.0f;  // 0.00 .. 3.00
        float windTurbLift     = 1.0f;  // 0.00 .. 3.00
        bool  noWind           = false;

        float cloudThick       = 1.0f;  // 0.00 .. 2.00
        float cloudTop         = 1.0f;  // 0.00 .. 2.00
        float cloudBase        = 1.0f;  // 0.00 .. 2.00
        float cloudScrollSpeed = 1.0f;  // 0.00 .. 5.00

        float fogA             = 1.0f;  // 0.00 .. 2.00
        float fogB             = 1.0f;  // 0.00 .. 2.00
        bool  clearDistantFog  = false;

        int   weatherPreset    = 0;     // 0=Dynamic, 1=Clear, 2=Overcast, 3=Rain, 4=Storm, 5=Fog

        // Inventory-wide overrides (inventory.cpp). Unlike the quantity editor
        // these write into the game's own ItemInfo / InventoryInfo data
        // tables - one number re-stamps every item / storage type at once,
        // and turning the toggle back off restores each row's own original
        // value.
        bool invSlotSize     = false;
        int  invSlotSizeVal  = 700; // 700 is the engine's safe limit (prevents Error 298648703)
        bool invStackSize    = false;
        int  invStackSizeVal = 999999;

        // UI Theme: 0=Crimson Red, 1=Cyber Cyan, 2=Neon Purple, 3=Matrix Emerald, 4=Royal Gold, 5=Sunset Orange
        int themeIndex = 0;
        bool playstationIcons = false;

        // UI Language: "en" (default), "zh", "ko", or any custom code matching Trinity_<code.ini
        char languageCode[16] = "en";
        int  languageIndex = 0;

        // Custom Title Font (Settings UI)
        bool useCustomFont = true;
        int  builtInFontIndex = 0;
        char customFont[64] = "Kirsty Bd.otf";

        // Bookmark / Saved Locations (dynamic unlimited list)
        struct SavedLocation
        {
            char  name[64] = "";
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };
        std::vector<SavedLocation> savedLocations;

        // Overlay extras.
        bool showFps = false;
        bool showConsole = true;
        bool fileLogging = true; // mirror the console to Trinity.log

        // UI customizations
        float menuScale = 1.0f;
        float tooltipImageScale = 1.0f;
        bool  showItemTooltip = true;

        // Persistence (settings.cpp). While on, every feature change is
        // written to Trinity.ini and restored on the next launch. The flag
        // itself always persists so the preference survives sessions.
        bool autoSave = true;

        static State& Get()
        {
            static State s;
            return s;
        }
    };
}
