#include "world.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "offsets.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../mem/hooks.h"
#include "../core/logger.h"
#include "../core/state.h"

namespace trinity::game
{
    using mem::Write8;
    using mem::Write32;
    using mem::Read32;

    namespace
    {
        // Resolved addresses of the fixed-timestep override globals (BSS - zero
        // in the static image, so found via the override block's RIP operands).
        // Zero if the signature did not resolve, in which case Game Speed is
        // inert (Tick no-ops).
        uintptr_t g_flagAddr  = 0; // byte  byte_606B9CE : 1 forces the fixed step
        uintptr_t g_valueAddr = 0; // float dword_615A4F0 : forced seconds-per-frame

        // Whether we currently hold the override on. Lets Tick clear the flag
        // exactly once when the toggle is switched off, restoring the engine's
        // own real-time delta without fighting the game every frame afterwards.
        bool g_applied = false;

        // Master field-clock globals (client / server realm), each the base of
        // a 32-byte int32 time struct (day/hour/min/sec). Zero if the signature
        // did not resolve, in which case Advance Time is inert.
        uintptr_t g_timeClient = 0;
        uintptr_t g_timeServer = 0;

        // --- Freeze the visible SUN: render-manager clamp --------------------
        // Address of the engine-object global (qword_648F688). The render
        // TimeOfDay manager hangs off it and drives the sun independently of
        // the field clock, so the field-time freeze above does not hold it.
        // See offsets.h kSig_TodEngineGlobal. Zero if it did not resolve.
        uintptr_t g_todEngineGlobal = 0;

        // While frozen we pin the manager's lower==upper==g_todTargetHour so
        // the engine clamps the sun there. Captured once on enable; the
        // originals are restored on disable/unload.
        bool  g_todClampApplied = false;
        float g_todOrigLower    = 0.0f;
        float g_todOrigUpper    = 0.0f;
        float g_todTargetHour   = 0.0f; // 0..24; Advance steps this while frozen

        // --- Freeze Time of Day: the field-time tick hook --------------------
        // sub_871360 advances the whole day/night clock by adding the frame
        // delta to its float accumulator ([mgr+0x2C] += delta) before deriving
        // the realm clock globals + the sun from it. Freeze = force that delta
        // to 0 while engaged, so time simply stops accruing - no globals to
        // pin, no cross-thread race (see offsets.h kSig_FieldTimeTick). Only
        // the clock stops; physics, AI and combat keep running.
        //
        // The delta rides in xmm1 as a single float; the prototype declares it
        // so the register survives the trampoline untouched on pass-through.
        using FieldTimeTick_t = void(__fastcall*)(void* mgr, float delta, float d2);
        FieldTimeTick_t oFieldTimeTick = nullptr;
        void* g_fieldTimeTickTarget = nullptr;

        void __fastcall hkFieldTimeTick(void* mgr, float delta, float d2)
        {
            if (State::Get().timeFrozen)
                delta = 0.0f; // clock stops accruing; sun + numeric clock hold
            oFieldTimeTick(mgr, delta, d2);
        }

        // Reinterpret a float as its 32-bit pattern for a raw Write32.
        uint32_t FloatBits(float f)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            return bits;
        }

        float Clamp(float v, float lo, float hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        bool ReadI32(uintptr_t addr, int* out)
        {
            uint32_t bits = 0;
            if (!Read32(addr, &bits)) return false;
            *out = static_cast<int>(bits);
            return true;
        }

        bool ReadF32(uintptr_t addr, float* out)
        {
            uint32_t bits = 0;
            if (!Read32(addr, &bits)) return false;
            std::memcpy(out, &bits, sizeof(*out));
            return true;
        }

        // Resolve the live render TimeOfDay manager: engine = *g_todEngineGlobal,
        // manager = *(engine + 0x2F8). Returns 0 until the engine is up (early
        // load) or if the global did not resolve - callers no-op on 0.
        uintptr_t ResolveTodManager()
        {
            if (!g_todEngineGlobal) return 0;
            uintptr_t engine = 0;
            if (!mem::ReadPtr(g_todEngineGlobal, &engine) || engine < kMinPointer)
                return 0;
            uintptr_t mgr = 0;
            if (!mem::ReadPtr(engine + kOff_Tod_Manager, &mgr) || mgr < kMinPointer)
                return 0;
            return mgr;
        }

        bool WriteI32(uintptr_t addr, int v)
        {
            return Write32(addr, static_cast<uint32_t>(v));
        }

        // Write day/hour to BOTH realm globals (min/sec left untouched). The
        // reading realm depends on the reader thread's TLS selector, so we
        // always write both. Guarded per field.
        void WriteClockDayHour(int day, int hour)
        {
            for (uintptr_t g : { g_timeClient, g_timeServer })
            {
                if (!g) continue;
                WriteI32(g + kOff_FieldTime_Day,  day);
                WriteI32(g + kOff_FieldTime_Hour, hour);
            }
        }

        // --- Master field-clock discovery ------------------------------------
        // One signature over sub_1CA3890's realm-select read yields both realm
        // globals: resolve the RIP operands of the two `vmovups` (server, then
        // client). See offsets.h kSig_FieldTimeRealm.
        bool ResolveFieldTimeGlobals()
        {
            const uintptr_t m = mem::FindPattern(kSig_FieldTimeRealm);
            if (!m) return false;
            g_timeServer = mem::ResolveRipAt(m + kOff_FieldTime_ServerVmovups, kLen_FieldTime_Vmovups);
            g_timeClient = mem::ResolveRipAt(m + kOff_FieldTime_ClientVmovups, kLen_FieldTime_Vmovups);
            if (g_timeClient < kMinPointer || g_timeServer < kMinPointer)
            {
                g_timeClient = g_timeServer = 0;
                return false;
            }
            return true;
        }
    }

    bool World::Install()
    {
        bool ok = true;

        const uintptr_t m = mem::FindPattern(kSig_GameSpeed);
        if (!m)
        {
            LOG_ERR("world: game-speed signature NOT FOUND - Game Speed disabled.");
            ok = false;
        }
        else
        {
            if (mem::CountMatches(kSig_GameSpeed, 2) != 1)
                LOG_WARN("world: game-speed signature ambiguous; using first match.");

            // Flag: disp32 of `cmp cs:byte_606B9CE, 1` (an imm follows the disp,
            // so resolve from the explicit disp/next-instr rather than ResolveRipAt).
            g_flagAddr = mem::ResolveRip(m + kOff_GameSpeed_FlagDisp, m + kOff_GameSpeed_FlagEnd);
            // Value: `vmovss xmm0, cs:dword_615A4F0` - a standard RIP instr (disp
            // at its tail), so ResolveRipAt handles it.
            g_valueAddr = mem::ResolveRipAt(m + kOff_GameSpeed_ValueVmovss, kLen_GameSpeed_Vmovss);

            if (g_flagAddr < kMinPointer || g_valueAddr < kMinPointer)
            {
                LOG_ERR("world: game-speed globals resolved out of range - Game Speed disabled.");
                g_flagAddr = g_valueAddr = 0;
                ok = false;
            }
        }

        // Time of Day resolves independently - Game Speed still works if this
        // signature drifts, and vice versa. Advance needs the clock globals...
        if (!ResolveFieldTimeGlobals())
        {
            LOG_WARN("world: field-clock signature NOT FOUND - Advance Time disabled.");
            g_timeClient = g_timeServer = 0;
            ok = false;
        }

        // ...Freeze needs the field-time tick hook (zeroes the clock's delta,
        // which holds the numeric clock)...
        // Independent of both the globals above and Game Speed - each can drift
        // without disabling the others.
        if (!mem::InstallHook("world: field-time tick", kSig_FieldTimeTick,
                              "Freeze Time of Day disabled", hkFieldTimeTick,
                              &oFieldTimeTick, &g_fieldTimeTickTarget))
            ok = false;

        // ...and the render-manager clamp holds the visible SUN (the field-time
        // tick alone does not - the sun rides its own accumulator). Resolve the
        // engine-object global here; the manager itself is read live each Tick.
        {
            const uintptr_t g = mem::FindPattern(kSig_TodEngineGlobal);
            if (!g)
            {
                LOG_WARN("world: TOD engine-global signature NOT FOUND - sun freeze disabled.");
                ok = false;
            }
            else if (mem::CountMatches(kSig_TodEngineGlobal, 2) != 1)
            {
                LOG_WARN("world: TOD engine-global signature ambiguous - sun freeze disabled.");
                g_todEngineGlobal = 0;
                ok = false;
            }
            else
            {
                g_todEngineGlobal = mem::ResolveRipAt(g + kOff_TodEngineGlobal_Mov,
                                                      kLen_TodEngineGlobal_Mov);
                if (g_todEngineGlobal < kMinPointer)
                {
                    LOG_ERR("world: TOD engine-global resolved out of range - sun freeze disabled.");
                    g_todEngineGlobal = 0;
                    ok = false;
                }
            }
        }

        return ok;
    }

    void World::Tick()
    {
        const State& st = State::Get();

        // Game Speed - only if its globals resolved; independent of the sun
        // freeze below, so a Game Speed signature drift never disables Freeze.
        if ((g_flagAddr && g_valueAddr) && st.gameSpeed)
        {
            // Forced frame delta = mult / 60: the engine's own fixed-timestep
            // reference is the 60-FPS step (1/60 s), so 1.00x reproduces it and
            // the multiplier scales sim time from there. Clamp both the factor
            // (to the slider's range) and the resulting delta (defensively, so a
            // bad value can never feed the sim an absurd timestep).
            const float mult  = Clamp(st.gameSpeedMult, 0.1f, 5.0f);
            const float delta = Clamp(mult / kGameSpeed_BaselineFps, 1.0e-5f, 1.0f);

            // Value first, then arm the flag, so the timing update never reads a
            // stale delta on the frame we switch it on. Re-armed every tick so
            // it self-heals if the game's capture path clears the flag.
            Write32(g_valueAddr, FloatBits(delta));
            Write8(g_flagAddr, 1);
            g_applied = true;
        }
        else if (g_applied)
        {
            // Back to the engine's own measured real-time delta.
            Write8(g_flagAddr, 0);
            g_applied = false;
        }

        // Freeze Time of Day: the field-time tick hook (hkFieldTimeTick) holds
        // the NUMERIC clock, but the visible SUN rides the render manager's own
        // accumulator - so pin its clamp here to hold the sun too. Force
        // lower == upper == the captured hour every tick while frozen; the
        // engine clamps currentTimeOfDay to it (real time keeps flowing).
        const uintptr_t mgr = ResolveTodManager();
        if (st.timeFrozen && mgr)
        {
            if (!g_todClampApplied)
            {
                // Capture the originals + the hour to hold, once on enable.
                if (!ReadF32(mgr + kOff_Tod_LowerLimit, &g_todOrigLower)) g_todOrigLower = 0.0f;
                if (!ReadF32(mgr + kOff_Tod_UpperLimit, &g_todOrigUpper)) g_todOrigUpper = 24.0f;
                float cur = 0.0f;
                if (ReadF32(mgr + kOff_Tod_CurrentHour, &cur) && cur >= 0.0f && cur <= 24.0f)
                    g_todTargetHour = cur;
                g_todClampApplied = true;
            }
            const uint32_t bits = FloatBits(g_todTargetHour);
            Write32(mgr + kOff_Tod_LowerLimit, bits);
            Write32(mgr + kOff_Tod_UpperLimit, bits);
        }
        else if (g_todClampApplied)
        {
            // Disabled (or manager lost): restore the engine's own limits once.
            if (mgr)
            {
                Write32(mgr + kOff_Tod_LowerLimit, FloatBits(g_todOrigLower));
                Write32(mgr + kOff_Tod_UpperLimit, FloatBits(g_todOrigUpper));
            }
            g_todClampApplied = false;
        }
    }

    void World::Remove()
    {
        // Leave the game at normal speed on unload.
        if (g_applied && g_flagAddr) Write8(g_flagAddr, 0);
        g_applied  = false;
        g_flagAddr = g_valueAddr = 0;

        // Restore the render manager's time-of-day limits if we were holding
        // the sun, then forget the engine global.
        if (g_todClampApplied)
        {
            const uintptr_t mgr = ResolveTodManager();
            if (mgr)
            {
                Write32(mgr + kOff_Tod_LowerLimit, FloatBits(g_todOrigLower));
                Write32(mgr + kOff_Tod_UpperLimit, FloatBits(g_todOrigUpper));
            }
            g_todClampApplied = false;
        }
        g_todEngineGlobal = 0;

        // Unhook the field-time tick (freeze) and forget the clock globals.
        mem::RemoveHook(&g_fieldTimeTickTarget);
        oFieldTimeTick = nullptr;
        g_timeClient = g_timeServer = 0;
    }

    bool World::Ready()
    {
        return g_flagAddr >= kMinPointer && g_valueAddr >= kMinPointer;
    }

    bool World::TimeOfDayReady()
    {
        return g_timeClient >= kMinPointer && g_timeServer >= kMinPointer;
    }

    bool World::AdvanceTimeOfDayHours(int hours)
    {
        if (!g_timeClient) return false;

        int day = 0, hour = 0;
        if (!ReadI32(g_timeClient + kOff_FieldTime_Day,  &day)) return false;
        if (!ReadI32(g_timeClient + kOff_FieldTime_Hour, &hour)) return false;

        // Carry the hour into the day so day/hour stay consistent (writing the
        // hour alone and letting it wrap past 24 desyncs the day and the game
        // corrects it back - the flicker seen in testing).
        int total = day * 24 + hour + hours;
        if (total < 0) total = 0;
        const int newDay  = total / 24;
        const int newHour = total % 24;

        WriteClockDayHour(newDay, newHour);

        // While frozen the tick's delta is 0, so nothing rewrites the globals -
        // the advanced numeric time simply sticks until Freeze is turned off.
        // But the visible SUN is held by the render-manager clamp, so step its
        // target hour too - otherwise the clamp would pin the sun in place and
        // Advance would move only the numbers, not the daylight.
        if (g_todClampApplied)
        {
            g_todTargetHour = std::fmod(g_todTargetHour + static_cast<float>(hours), 24.0f);
            if (g_todTargetHour < 0.0f) g_todTargetHour += 24.0f;
            // Next Tick re-pins lower==upper==g_todTargetHour.
        }
        return true;
    }
}
