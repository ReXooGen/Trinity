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

        static uintptr_t g_fieldTimeMgr = 0;

        void __fastcall hkFieldTimeTick(void* mgr, float delta, float d2)
        {
            g_fieldTimeMgr = reinterpret_cast<uintptr_t>(mgr);
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

        // --- Dynamic Weather Intensity Hooks (Rain, Snow, Dust) with bulletproof SEH protection ---
        using GetWeatherIntensity_t = __m128(__fastcall*)(void* ws);
        static GetWeatherIntensity_t oGetRainIntensity = nullptr;
        static GetWeatherIntensity_t oGetSnowIntensity = nullptr;
        static GetWeatherIntensity_t oGetDustIntensity = nullptr;
        static void* g_rainIntensityTarget = nullptr;
        static void* g_snowIntensityTarget = nullptr;
        static void* g_dustIntensityTarget = nullptr;

        __m128 __fastcall hkGetRainIntensity(void* ws)
        {
            if (!ws || reinterpret_cast<uintptr_t>(ws) < kMinPointer)
                return _mm_set_ss(0.0f);

            const State& st = State::Get();
            if (st.forceClearSky) return _mm_set_ss(0.0f);
            if (st.rainIntensity > 0.001f) return _mm_set_ss(st.rainIntensity);
            if (st.weatherPreset == 1) return _mm_set_ss(0.0f);
            if (st.weatherPreset == 3) return _mm_set_ss(0.60f);
            if (st.weatherPreset == 4) return _mm_set_ss(1.00f);

            if (oGetRainIntensity)
            {
                __try
                {
                    return oGetRainIntensity(ws);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return _mm_set_ss(0.0f);
                }
            }
            return _mm_set_ss(0.0f);
        }

        __m128 __fastcall hkGetSnowIntensity(void* ws)
        {
            if (!ws || reinterpret_cast<uintptr_t>(ws) < kMinPointer)
                return _mm_set_ss(0.0f);

            const State& st = State::Get();
            if (st.forceClearSky) return _mm_set_ss(0.0f);
            if (st.snowIntensity > 0.001f) return _mm_set_ss(st.snowIntensity);

            if (oGetSnowIntensity)
            {
                __try
                {
                    return oGetSnowIntensity(ws);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return _mm_set_ss(0.0f);
                }
            }
            return _mm_set_ss(0.0f);
        }

        __m128 __fastcall hkGetDustIntensity(void* ws)
        {
            if (!ws || reinterpret_cast<uintptr_t>(ws) < kMinPointer)
                return _mm_set_ss(0.0f);

            const State& st = State::Get();
            if (st.forceClearSky || st.noWind) return _mm_set_ss(0.0f);
            if (st.dustIntensity > 0.001f)
            {
                float mul = (st.windMultiplier > 0.01f ? st.windMultiplier : 1.0f);
                return _mm_set_ss(st.dustIntensity * 15.0f * mul);
            }

            if (oGetDustIntensity)
            {
                __try
                {
                    __m128 orig = oGetDustIntensity(ws);
                    float val = _mm_cvtss_f32(orig);
                    if (st.windMultiplier > 0.01f && st.windMultiplier != 1.0f)
                    {
                        val *= st.windMultiplier;
                    }
                    return _mm_set_ss(val);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return _mm_set_ss(0.0f);
                }
            }
            return _mm_set_ss(0.0f);
        }

        // --- Wind & Cloud/Fog Pack Hook (Shader pipeline parameters) ---
        using WindPack_t = void(__fastcall*)(void* windNodePtr, float* packedOut);
        static WindPack_t oWindPack = nullptr;
        static void* g_windPackTarget = nullptr;

        void __fastcall hkWindPack(void* windNodePtr, float* packedOut)
        {
            if (oWindPack)
            {
                __try
                {
                    oWindPack(windNodePtr, packedOut);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return;
                }
            }

            if (!packedOut || reinterpret_cast<uintptr_t>(packedOut) < kMinPointer)
                return;

            __try
            {
                const State& st = State::Get();
                if (st.forceClearSky)
                {
                    packedOut[0x1B] = 0.0f; // Cloud amount
                    packedOut[0x11] = 0.0f; // Fog density
                    packedOut[0x17] = 0.0f;
                    return;
                }

                if (st.clearDistantFog)
                {
                    packedOut[0x11] = 0.0f;
                    packedOut[0x17] = 0.0f;
                }
                else
                {
                    if (st.fogA != 1.0f) packedOut[0x11] *= st.fogA;
                    if (st.fogB != 1.0f) packedOut[0x17] *= st.fogB;
                }

                if (st.cloudThick != 1.0f)
                {
                    packedOut[0x1B] *= st.cloudThick;
                    packedOut[0x32] *= st.cloudThick;
                }

                if (st.cloudTop != 1.0f)
                {
                    packedOut[0x2F] *= st.cloudTop;
                }

                if (st.cloudBase != 1.0f)
                {
                    packedOut[0x30] *= st.cloudBase;
                }

                if (st.cloudScrollSpeed != 1.0f)
                {
                    packedOut[0x23] *= st.cloudScrollSpeed;
                    packedOut[0x24] *= st.cloudScrollSpeed;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        // --- Safe Weather & Atmosphere discovery (Non-hooking field controller) ---
        static uintptr_t g_pEnvManager = 0;

        struct ResolvedWeatherEnv
        {
            uintptr_t entity       = 0;
            uintptr_t weatherState = 0;
            uintptr_t cloudNode    = 0;
            uintptr_t windNode     = 0;
            bool      valid        = false;
        };

        ResolvedWeatherEnv ResolveWeatherEnv()
        {
            ResolvedWeatherEnv env{};
            if (!g_pEnvManager) return env;

            __try
            {
                uintptr_t envMgr = 0;
                if (!mem::ReadPtr(g_pEnvManager, &envMgr) || envMgr < kMinPointer)
                    return env;

                uintptr_t* vt = *reinterpret_cast<uintptr_t**>(envMgr);
                if (!vt || reinterpret_cast<uintptr_t>(vt) < kMinPointer)
                    return env;

                auto getEntity = reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(vt[0x40 / 8]);
                if (!getEntity || reinterpret_cast<uintptr_t>(getEntity) < kMinPointer)
                    return env;

                env.entity = getEntity(envMgr);
                if (env.entity < kMinPointer)
                    return env;

                uintptr_t ws = 0;
                if (!mem::ReadPtr(env.entity + 0xEF0, &ws) || ws < kMinPointer)
                {
                    if (!mem::ReadPtr(env.entity + 0xEE0, &ws) || ws < kMinPointer)
                        mem::ReadPtr(env.entity + 0xED8, &ws);
                }
                if (ws < kMinPointer) return env;

                env.weatherState = ws;

                uintptr_t result = 0;
                if (!mem::ReadPtr(ws + 0x60, &result) || result < kMinPointer)
                {
                    if (!mem::ReadPtr(ws + 0x50, &result) || result < kMinPointer)
                        mem::ReadPtr(ws + 0x20, &result);
                }

                if (result >= kMinPointer)
                {
                    mem::ReadPtr(result + 0x18, &env.cloudNode);
                    mem::ReadPtr(result + 0x20, &env.windNode);
                }

                env.valid = (env.cloudNode >= kMinPointer || env.weatherState >= kMinPointer);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                env = ResolvedWeatherEnv{};
            }
            return env;
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

        // Dynamic weather intensity hooks (Rain, Snow, Dust)
        mem::InstallHook("world: rain intensity", kSig_WeatherRain,
                         "Rain control disabled", hkGetRainIntensity,
                         &oGetRainIntensity, &g_rainIntensityTarget);
        mem::InstallHook("world: snow intensity", kSig_WeatherSnow,
                         "Snow control disabled", hkGetSnowIntensity,
                         &oGetSnowIntensity, &g_snowIntensityTarget);
        mem::InstallHook("world: dust intensity", kSig_WeatherDust,
                         "Dust control disabled", hkGetDustIntensity,
                         &oGetDustIntensity, &g_dustIntensityTarget);
        mem::InstallHook("world: wind pack", kSig_WindPack,
                         "Cloud and Fog control disabled", hkWindPack,
                         &oWindPack, &g_windPackTarget);

        // Safe EnvManager pointer resolution for Atmosphere & Weather (Zero hooks)
        {
            const uintptr_t envSig = mem::FindPattern(kSig_EnvManager);
            if (envSig)
            {
                g_pEnvManager = mem::ResolveRipAt(envSig + kOff_EnvManager_Mov, kLen_EnvManager_Mov);
                if (g_pEnvManager >= kMinPointer)
                {
                    LOG("world: safe EnvManager pointer resolved: 0x%llX", g_pEnvManager);
                }
                else
                {
                    g_pEnvManager = 0;
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

        // Live Weather & Atmosphere parameter injection (Zero hooks, 100% crash-safe)
        const ResolvedWeatherEnv env = ResolveWeatherEnv();
        if (env.valid)
        {
            __try
            {
                if (env.cloudNode)
                {
                    if (st.clearDistantFog)
                    {
                        Write32(env.cloudNode + CN::FOG_A, FloatBits(0.0f));
                        Write32(env.cloudNode + CN::FOG_B, FloatBits(0.0f));
                    }
                    else
                    {
                        if (st.fogA != 1.0f) Write32(env.cloudNode + CN::FOG_A, FloatBits(st.fogA));
                        if (st.fogB != 1.0f) Write32(env.cloudNode + CN::FOG_B, FloatBits(st.fogB));
                    }

                    if (st.forceClearSky)
                    {
                        Write32(env.cloudNode + CN::FOG_A, FloatBits(0.0f));
                        Write32(env.cloudNode + CN::FOG_B, FloatBits(0.0f));
                        Write32(env.cloudNode + CN::STORM_THRESH, FloatBits(0.0f));
                        Write32(env.cloudNode + CN::DUST_THRESH, FloatBits(0.0f));
                        Write32(env.cloudNode + CN::CLOUD_THICK, FloatBits(0.0f));
                    }
                    else
                    {
                        if (st.rainIntensity > 0.001f)
                        {
                            Write32(env.cloudNode + CN::STORM_THRESH, FloatBits(st.rainIntensity));
                            Write32(env.cloudNode + CN::CLOUD_THICK, FloatBits(Clamp(st.rainIntensity * 1.5f, 0.4f, 1.0f)));
                        }
                        if (st.dustIntensity > 0.001f)
                        {
                            Write32(env.cloudNode + CN::DUST_BASE, FloatBits(st.dustIntensity * 5.0f));
                            Write32(env.cloudNode + CN::DUST_THRESH, FloatBits(st.dustIntensity));
                        }
                        if (st.cloudThick != 1.0f && st.rainIntensity <= 0.001f)
                        {
                            Write32(env.cloudNode + CN::CLOUD_THICK, FloatBits(st.cloudThick));
                        }
                        if (st.cloudTop != 1.0f)
                        {
                            Write32(env.cloudNode + CN::CLOUD_TOP, FloatBits(st.cloudTop * 0.001f));
                        }
                        if (st.cloudBase != 1.0f)
                        {
                            Write32(env.cloudNode + CN::CLOUD_BASE, FloatBits(st.cloudBase * 0.001f));
                        }
                    }
                }

                if (env.windNode)
                {
                    if (st.noWind)
                    {
                        Write32(env.windNode + WN::SPEED, FloatBits(0.0f));
                        Write32(env.windNode + WN::GUST, FloatBits(0.0f));
                        Write32(env.windNode + WN::TURB_LIFT, FloatBits(0.0f));
                        if (env.cloudNode) Write32(env.cloudNode + CN::DUST_WIND_SCALE, FloatBits(0.0f));
                    }
                    else
                    {
                        if (st.windMultiplier != 1.0f)
                        {
                            Write32(env.windNode + WN::SPEED, FloatBits(st.windMultiplier * 2.0f));
                            if (env.cloudNode) Write32(env.cloudNode + CN::DUST_WIND_SCALE, FloatBits(st.windMultiplier));
                        }
                        if (st.windGust != 1.0f)
                        {
                            Write32(env.windNode + WN::GUST, FloatBits(st.windGust * 1.5f));
                        }
                        if (st.windTurbLift != 1.0f)
                        {
                            Write32(env.windNode + WN::TURB_LIFT, FloatBits(st.windTurbLift));
                        }
                        if (st.cloudScrollSpeed != 1.0f)
                        {
                            Write32(env.windNode + WN::CLOUD_SCROLL_X, FloatBits(st.cloudScrollSpeed));
                            Write32(env.windNode + WN::CLOUD_SCROLL_Z, FloatBits(st.cloudScrollSpeed));
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
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

        // Unhook dynamic weather intensity hooks
        mem::RemoveHook(&g_rainIntensityTarget);
        mem::RemoveHook(&g_snowIntensityTarget);
        mem::RemoveHook(&g_dustIntensityTarget);
        mem::RemoveHook(&g_windPackTarget);
        oGetRainIntensity = nullptr;
        oGetSnowIntensity = nullptr;
        oGetDustIntensity = nullptr;
        oWindPack = nullptr;

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

        if (g_fieldTimeMgr)
        {
            Write32(g_fieldTimeMgr + 0x2C, FloatBits(static_cast<float>(newHour * 3600)));
        }

        const uintptr_t renderMgr = ResolveTodManager();
        if (renderMgr)
        {
            const float h = static_cast<float>(newHour);
            Write32(renderMgr + kOff_Tod_CurrentHour, FloatBits(h));
            if (g_todClampApplied)
            {
                g_todTargetHour = h;
                Write32(renderMgr + kOff_Tod_LowerLimit, FloatBits(h));
                Write32(renderMgr + kOff_Tod_UpperLimit, FloatBits(h));
            }
        }
        else if (g_todClampApplied)
        {
            g_todTargetHour = std::fmod(g_todTargetHour + static_cast<float>(hours), 24.0f);
            if (g_todTargetHour < 0.0f) g_todTargetHour += 24.0f;
        }
        return true;
    }

    bool World::SetTimeOfDay(int targetHour)
    {
        if (!g_timeClient) return false;

        int day = 0;
        if (!ReadI32(g_timeClient + kOff_FieldTime_Day, &day)) return false;

        targetHour = targetHour % 24;
        if (targetHour < 0) targetHour += 24;

        WriteClockDayHour(day, targetHour);

        if (g_fieldTimeMgr)
        {
            Write32(g_fieldTimeMgr + 0x2C, FloatBits(static_cast<float>(targetHour * 3600)));
        }

        const uintptr_t renderMgr = ResolveTodManager();
        if (renderMgr)
        {
            const float h = static_cast<float>(targetHour);
            Write32(renderMgr + kOff_Tod_CurrentHour, FloatBits(h));
            if (g_todClampApplied)
            {
                g_todTargetHour = h;
                Write32(renderMgr + kOff_Tod_LowerLimit, FloatBits(h));
                Write32(renderMgr + kOff_Tod_UpperLimit, FloatBits(h));
            }
        }
        return true;
    }

    bool World::GetCurrentTimeOfDay(int* outDay, int* outHour, int* outMinute)
    {
        if (!g_timeClient) return false;
        int d = 0, h = 0, m = 0;
        if (!ReadI32(g_timeClient + kOff_FieldTime_Day, &d)) return false;
        if (!ReadI32(g_timeClient + kOff_FieldTime_Hour, &h)) return false;
        ReadI32(g_timeClient + 0x08, &m);
        if (outDay) *outDay = d;
        if (outHour) *outHour = h;
        if (outMinute) *outMinute = m;
        return true;
    }

    bool World::SetWeatherPreset(int presetId)
    {
        State& st = State::Get();
        st.weatherPreset = presetId;

        switch (presetId)
        {
        case 1: // Clear Sky (Sunny)
            st.forceClearSky   = true;
            st.clearDistantFog = true;
            st.rainIntensity   = 0.0f;
            st.dustIntensity   = 0.0f;
            st.cloudThick      = 0.0f;
            st.windMultiplier  = 1.0f;
            st.noWind          = false;
            break;
        case 2: // Overcast (Cloudy)
            st.forceClearSky   = false;
            st.clearDistantFog = false;
            st.rainIntensity   = 0.0f;
            st.dustIntensity   = 0.0f;
            st.cloudThick      = 1.80f;
            st.fogA            = 1.20f;
            st.fogB            = 1.10f;
            st.windMultiplier  = 1.20f;
            st.noWind          = false;
            break;
        case 3: // Rainy (Light Rain)
            st.forceClearSky   = false;
            st.clearDistantFog = false;
            st.rainIntensity   = 0.50f;
            st.dustIntensity   = 0.0f;
            st.cloudThick      = 1.60f;
            st.fogA            = 1.30f;
            st.fogB            = 1.20f;
            st.windMultiplier  = 1.50f;
            st.noWind          = false;
            break;
        case 4: // Thunderstorm (Storm)
            st.forceClearSky   = false;
            st.clearDistantFog = false;
            st.rainIntensity   = 1.00f;
            st.dustIntensity   = 0.0f;
            st.cloudThick      = 2.00f;
            st.fogA            = 1.50f;
            st.fogB            = 1.40f;
            st.windMultiplier  = 2.50f;
            st.windGust        = 2.00f;
            st.noWind          = false;
            break;
        case 5: // Dense Fog / Mist
            st.forceClearSky   = false;
            st.clearDistantFog = false;
            st.rainIntensity   = 0.0f;
            st.dustIntensity   = 0.0f;
            st.cloudThick      = 1.50f;
            st.fogA            = 2.00f;
            st.fogB            = 2.00f;
            st.windMultiplier  = 0.50f;
            st.noWind          = false;
            break;
        default: // 0 = Dynamic (Game Default)
            st.forceClearSky   = false;
            st.clearDistantFog = false;
            st.rainIntensity   = 0.0f;
            st.dustIntensity   = 0.0f;
            st.cloudThick      = 1.0f;
            st.fogA            = 1.0f;
            st.fogB            = 1.0f;
            st.windMultiplier  = 1.0f;
            st.windGust        = 1.0f;
            st.noWind          = false;
            break;
        }

        LOG("world: weather preset set to %d", presetId);
        return true;
    }

    bool World::SetClearDistantFog(bool enabled)
    {
        State::Get().clearDistantFog = enabled;
        LOG("world: clear distant fog set to %s", enabled ? "ENABLED" : "DISABLED");
        return true;
    }
}
