#pragma once

namespace trinity::game
{
    // World features: Game Speed (a global time-dilation control built on the
    // engine's fixed-timestep override) and Time of Day (freeze / advance the
    // day-night clock). Both resolve independently at Install() - either can
    // be unavailable (signature drift after a patch) without disabling the
    // other; each has its own Ready() check.
    //
    // Game Speed - see offsets.h kSig_GameSpeed for the full mechanism: the
    // engine's per-frame timing update carries a fixed-timestep override
    // (its own video/demo-capture path) that, when a flag byte is set,
    // replaces the measured frame delta with a fixed value. That master
    // delta is what the whole simulation - animation, physics, AI, ability
    // timers - advances by, so overriding it dilates game time uniformly.
    // We resolve the flag byte + value float from the override block's RIP
    // operands at load, then each game tick write value = mult/60 and hold
    // the flag on while enabled; turning it off clears the flag once, so the
    // engine returns to its own real-time delta.
    //
    //  NOTE: this dilates CLIENT simulation time. Server-authoritative systems
    //  (some cooldowns, inventory reconcile) do not scale with it, so very high
    //  multipliers can desync those; the exposed range is deliberately modest.
    //
    // Time of Day - see offsets.h kSig_FieldTimeRealm for the discovery recipe
    // and field map. The real day/night clock is two BSS globals (client /
    // server realm), each a 32-byte int struct (day/hour/min/sec) that the
    // sun/sky update reads every frame. (The "TimeOfDayManager" +0x3D0 float
    // and the engine timeScale were both dead ends - see offsets.h.)
    //  - Freeze works in TWO layers, because the numeric clock and the visible
    //    sun are driven separately. (1) The field-time tick hook
    //    (kSig_FieldTimeTick) forces its frame delta to 0, holding the NUMERIC
    //    clock. (2) The visible SUN rides the render manager's own accumulator,
    //    which the tick hook does NOT stop - so we also clamp that manager's
    //    lower==upper==the captured hour every Tick (kSig_TodEngineGlobal), the
    //    same lever the engine's own TimeOfDayForward/Backward debug commands
    //    and /settimeofday{lower,upper}limit console commands use. Without layer
    //    (2), Freeze "only froze the clock" while the sun kept moving. Physics,
    //    AI and combat keep running throughout.
    //  - Advance adds whole hours to the clock (carrying into the day so the
    //    two stay consistent); the change sticks and the clock keeps flowing.
    class World
    {
    public:
        // Resolves both Game Speed and Time of Day globals. Non-fatal per
        // feature: whichever signature does not resolve leaves that feature
        // inert (Tick/Ready reflect it) without affecting the other.
        static bool Install();
        static void Remove();

        // Per-frame, game-thread apply for both features. Cheap and
        // idempotent; a no-op for any sub-feature that did not resolve.
        // Driven by the movement-update tick (see teleport.cpp), same as the
        // player resolve.
        static void Tick();

        // True once the Game Speed globals resolved (the feature is available).
        static bool Ready();

        // True once the field-clock globals resolved (the feature is available).
        static bool TimeOfDayReady();

        // Adds `hours` to the in-game clock (carrying into the day). Returns
        // false if Time of Day did not resolve or the clock can't be read right
        // now (e.g. not yet in-world). While frozen, advances the pinned time.
        static bool AdvanceTimeOfDayHours(int hours);

        // Sets the clock to an exact hour (0..23) instantly.
        static bool SetTimeOfDay(int targetHour);

        // Reads the current clock state.
        static bool GetCurrentTimeOfDay(int* outDay, int* outHour, int* outMinute);

        // Weather & Atmosphere controls
        static bool SetWeatherPreset(int presetId);
        static bool SetClearDistantFog(bool enabled);
    };
}
