#include "settings.h"
#include "tick_metrics.h"

#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "logger.h"
#include "mod.h"
#include "state.h"
#include "localization.h"

namespace trinity
{
    // Set once by ClaimOwnership() in the process that presents the game. See
    // settings.h - only that process may write Trinity.ini.
    static bool g_owner = false;

    // Trinity.ini lives next to Trinity.asi so the whole install stays one
    // folder that can be copied or deleted as a unit. `suffix` appends to the
    // file name for the temp file Save() writes through.
    static bool IniPath(char* buf, size_t cap, const char* suffix = "")
    {
        const DWORD n = GetModuleFileNameA(Mod::Get().Module(), buf, static_cast<DWORD>(cap));
        if (n == 0 || n >= cap)
            return false;

        char* slash = strrchr(buf, '\\');
        if (!slash)
            return false;

        const size_t left = cap - static_cast<size_t>(slash + 1 - buf);
        return snprintf(slash + 1, left, "Trinity.ini%s", suffix) < static_cast<int>(left);
    }

    static float ClampF(float v, float lo, float hi)
    {
        return v < lo ? lo : v > hi ? hi : v;
    }

    static int ClampI(int v, int lo, int hi)
    {
        return v < lo ? lo : v > hi ? hi : v;
    }

    void Settings::Load()
    {
        char path[MAX_PATH];
        if (!IniPath(path, sizeof(path)))
            return;

        FILE* f = fopen(path, "r");
        if (!f)
            return; // first run - nothing saved yet

        // Parse onto a default-constructed State so missing/garbled keys keep
        // their defaults, then apply in one step below.
        State vals;
        char  line[128];
        while (fgets(line, sizeof(line), f))
        {
            char* eq = strchr(line, '=');
            if (!eq)
                continue;
            *eq = 0;
            const char* key = line;
            const char* val = eq + 1;

            if      (!strcmp(key, "openKeyVk"))            vals.openKeyVk           = atoi(val);
            else if (!strcmp(key, "openPadMask"))          vals.openPadMask         = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "flyUpKeyVk"))           vals.flyUpKeyVk          = atoi(val);
            else if (!strcmp(key, "flyDownKeyVk"))         vals.flyDownKeyVk        = atoi(val);
            else if (!strcmp(key, "flyUpPadMask"))         vals.flyUpPadMask        = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "flyDownPadMask"))       vals.flyDownPadMask      = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "markerTeleportKeyVk"))  vals.markerTeleportKeyVk = atoi(val);
            else if (!strcmp(key, "markerTeleportPadMask"))vals.markerTeleportPadMask= static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "markerFallbackHeight")) vals.markerFallbackHeight = strtof(val, nullptr);
            else if (!strcmp(key, "navSelectPadMask"))    vals.navSelectPadMask    = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navBackPadMask"))      vals.navBackPadMask      = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navClearPadMask"))     vals.navClearPadMask     = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navPrevTabPadMask"))   vals.navPrevTabPadMask   = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navNextTabPadMask"))   vals.navNextTabPadMask   = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navUpPadMask"))        vals.navUpPadMask        = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navDownPadMask"))      vals.navDownPadMask      = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navLeftPadMask"))      vals.navLeftPadMask      = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navRightPadMask"))     vals.navRightPadMask     = static_cast<unsigned int>(strtoul(val, nullptr, 0));
            else if (!strcmp(key, "navSelectKeyVk"))      vals.navSelectKeyVk      = atoi(val);
            else if (!strcmp(key, "navBackKeyVk"))        vals.navBackKeyVk        = atoi(val);
            else if (!strcmp(key, "navClearKeyVk"))       vals.navClearKeyVk       = atoi(val);
            else if (!strcmp(key, "navPrevTabKeyVk"))     vals.navPrevTabKeyVk     = atoi(val);
            else if (!strcmp(key, "navNextTabKeyVk"))     vals.navNextTabKeyVk     = atoi(val);
            else if (!strcmp(key, "navUpKeyVk"))          vals.navUpKeyVk          = atoi(val);
            else if (!strcmp(key, "navDownKeyVk"))        vals.navDownKeyVk        = atoi(val);
            else if (!strcmp(key, "navLeftKeyVk"))        vals.navLeftKeyVk        = atoi(val);
            else if (!strcmp(key, "navRightKeyVk"))       vals.navRightKeyVk       = atoi(val);
            else if (!strcmp(key, "autoSave"))            vals.autoSave            = atoi(val) != 0;
            else if (!strcmp(key, "godMode"))             vals.godMode             = atoi(val) != 0;
            else if (!strcmp(key, "oneHitKill"))           vals.oneHitKill           = atoi(val) != 0;
            else if (!strcmp(key, "infDurability"))        vals.infDurability        = atoi(val) != 0;
            else if (!strcmp(key, "noFallDamage"))        vals.noFallDamage        = atoi(val) != 0;
            else if (!strcmp(key, "infStamina"))          vals.infStamina          = atoi(val) != 0;
            else if (!strcmp(key, "infMountStamina"))     vals.infMountStamina     = atoi(val) != 0;
            else if (!strcmp(key, "infSpirit"))           vals.infSpirit           = atoi(val) != 0;
            else if (!strcmp(key, "easyParry"))           vals.easyParry           = atoi(val) != 0;
            else if (!strcmp(key, "easyEvade"))           vals.easyEvade           = atoi(val) != 0;
            else if (!strcmp(key, "noBounty"))            vals.noBounty            = atoi(val) != 0;
            else if (!strcmp(key, "superGlideCompat"))    vals.superGlideCompat    = atoi(val) != 0;
            else if (!strcmp(key, "bypassStatCommit"))    vals.bypassStatCommit    = atoi(val) != 0;
            else if (!strcmp(key, "dmgOutMult"))          vals.dmgOutMult          = strtof(val, nullptr);
            else if (!strcmp(key, "dmgInMult"))           vals.dmgInMult           = strtof(val, nullptr);
            else if (!strcmp(key, "gameSpeed"))           vals.gameSpeed           = atoi(val) != 0;
            else if (!strcmp(key, "gameSpeedMult"))       vals.gameSpeedMult       = strtof(val, nullptr);
            else if (!strcmp(key, "timeFrozen"))          vals.timeFrozen          = atoi(val) != 0;
            else if (!strcmp(key, "superRun"))            vals.superRun            = atoi(val) != 0;
            else if (!strcmp(key, "superRunMult"))        vals.superRunMult        = strtof(val, nullptr);
            else if (!strcmp(key, "superJump"))           vals.superJump           = atoi(val) != 0;
            else if (!strcmp(key, "superJumpMult"))       vals.superJumpMult       = strtof(val, nullptr);
            else if (!strcmp(key, "freeFlight"))          vals.freeFlight          = atoi(val) != 0;
            else if (!strcmp(key, "flightSpeed"))         vals.flightSpeed         = strtof(val, nullptr);
            else if (!strcmp(key, "trustMult"))           vals.trustMult           = atoi(val) != 0;
            else if (!strcmp(key, "trustMultVal"))        vals.trustMultVal        = strtof(val, nullptr);
            else if (!strcmp(key, "petSlotLimit"))        vals.petSlotLimit        = atoi(val) != 0;
            else if (!strcmp(key, "petSlotLimitVal"))     vals.petSlotLimitVal     = atoi(val);
            else if (!strcmp(key, "invSlotSize"))         vals.invSlotSize         = atoi(val) != 0;
            else if (!strcmp(key, "invSlotSizeVal"))      vals.invSlotSizeVal      = atoi(val);
            else if (!strcmp(key, "invStackSize"))        vals.invStackSize        = atoi(val) != 0;
            else if (!strcmp(key, "invStackSizeVal"))     vals.invStackSizeVal     = atoi(val);
            else if (!strcmp(key, "forceClearSky"))       vals.forceClearSky       = atoi(val) != 0;
            else if (!strcmp(key, "rainIntensity"))       vals.rainIntensity       = strtof(val, nullptr);
            else if (!strcmp(key, "snowIntensity"))       vals.snowIntensity       = strtof(val, nullptr);
            else if (!strcmp(key, "dustIntensity"))       vals.dustIntensity       = strtof(val, nullptr);
            else if (!strcmp(key, "windMultiplier"))      vals.windMultiplier      = strtof(val, nullptr);
            else if (!strcmp(key, "windGust"))            vals.windGust            = strtof(val, nullptr);
            else if (!strcmp(key, "windTurbLift"))        vals.windTurbLift        = strtof(val, nullptr);
            else if (!strcmp(key, "noWind"))              vals.noWind              = atoi(val) != 0;
            else if (!strcmp(key, "cloudThick"))          vals.cloudThick          = strtof(val, nullptr);
            else if (!strcmp(key, "cloudTop"))            vals.cloudTop            = strtof(val, nullptr);
            else if (!strcmp(key, "cloudBase"))           vals.cloudBase           = strtof(val, nullptr);
            else if (!strcmp(key, "cloudScrollSpeed"))    vals.cloudScrollSpeed    = strtof(val, nullptr);
            else if (!strcmp(key, "fogA"))                vals.fogA                = strtof(val, nullptr);
            else if (!strcmp(key, "fogB"))                vals.fogB                = strtof(val, nullptr);
            else if (!strcmp(key, "clearDistantFog"))     vals.clearDistantFog     = atoi(val) != 0;
            else if (!strcmp(key, "weatherPreset"))       vals.weatherPreset       = atoi(val);
            else if (!strcmp(key, "showFps"))             vals.showFps             = atoi(val) != 0;
            else if (!strcmp(key, "showConsole"))         vals.showConsole         = atoi(val) != 0;
            else if (!strcmp(key, "menuScale"))           vals.menuScale           = strtof(val, nullptr);
            else if (!strcmp(key, "tooltipImageScale"))    vals.tooltipImageScale   = strtof(val, nullptr);
            else if (!strcmp(key, "showItemTooltip"))     vals.showItemTooltip     = atoi(val) != 0;
            else if (!strcmp(key, "fileLogging"))         vals.fileLogging         = atoi(val) != 0;
            else if (!strcmp(key, "perfLogging"))         vals.perfLogging         = atoi(val) != 0;
            else if (!strcmp(key, "themeIndex"))          vals.themeIndex          = atoi(val);
            else if (!strcmp(key, "playstationIcons"))    vals.playstationIcons    = atoi(val) != 0;
            else if (!strcmp(key, "language"))
            {
                snprintf(vals.languageCode, sizeof(vals.languageCode), "%s", val);
                size_t len = strlen(vals.languageCode);
                while (len > 0 && (vals.languageCode[len - 1] == '\r' || vals.languageCode[len - 1] == '\n' || vals.languageCode[len - 1] == ' '))
                    vals.languageCode[--len] = '\0';
            }

            else if (!strcmp(key, "useCustomFont"))       vals.useCustomFont       = atoi(val) != 0;
            else if (!strcmp(key, "builtInFontIndex"))    vals.builtInFontIndex    = atoi(val);
            else if (!strcmp(key, "customFont"))
            {
                snprintf(vals.customFont, sizeof(vals.customFont), "%s", val);
                size_t len = strlen(vals.customFont);
                while (len > 0 && (vals.customFont[len - 1] == '\r' || vals.customFont[len - 1] == '\n' || vals.customFont[len - 1] == ' '))
                    vals.customFont[--len] = '\0';
            }
            else if (!strncmp(key, "loc_", 4))
            {
                int idx = -1;
                char field[32] = "";
                if (sscanf(key, "loc_%[a-z]_%d", field, &idx) == 2 && idx >= 0 && idx < 500)
                {
                    if (idx >= static_cast<int>(vals.savedLocations.size()))
                        vals.savedLocations.resize(idx + 1);
                    if (!strcmp(field, "name"))
                    {
                        snprintf(vals.savedLocations[idx].name, sizeof(vals.savedLocations[idx].name), "%s", val);
                        size_t len = strlen(vals.savedLocations[idx].name);
                        while (len > 0 && (vals.savedLocations[idx].name[len - 1] == '\r' || vals.savedLocations[idx].name[len - 1] == '\n'))
                            vals.savedLocations[idx].name[--len] = '\0';
                    }
                    else if (!strcmp(field, "x")) vals.savedLocations[idx].x = strtof(val, nullptr);
                    else if (!strcmp(field, "y")) vals.savedLocations[idx].y = strtof(val, nullptr);
                    else if (!strcmp(field, "z")) vals.savedLocations[idx].z = strtof(val, nullptr);
                }
            }
            else if (!strncmp(key, "loc", 3) && key[3] >= '0' && key[3] <= '9')
            {
                int idx = key[3] - '0';
                if (idx >= 0 && idx < 500)
                {
                    if (idx >= static_cast<int>(vals.savedLocations.size()))
                        vals.savedLocations.resize(idx + 1);
                    if (!strcmp(key + 4, "_name"))
                    {
                        snprintf(vals.savedLocations[idx].name, sizeof(vals.savedLocations[idx].name), "%s", val);
                        size_t len = strlen(vals.savedLocations[idx].name);
                        while (len > 0 && (vals.savedLocations[idx].name[len - 1] == '\r' || vals.savedLocations[idx].name[len - 1] == '\n'))
                            vals.savedLocations[idx].name[--len] = '\0';
                    }
                    else if (!strcmp(key + 4, "_x")) vals.savedLocations[idx].x = strtof(val, nullptr);
                    else if (!strcmp(key + 4, "_y")) vals.savedLocations[idx].y = strtof(val, nullptr);
                    else if (!strcmp(key + 4, "_z")) vals.savedLocations[idx].z = strtof(val, nullptr);
                }
            }
        }
        fclose(f);

        State& st  = State::Get();
        st.autoSave      = vals.autoSave;
        st.fileLogging   = vals.fileLogging;
        st.perfLogging   = vals.perfLogging;
        core::TickMetrics::SetEnabled(st.perfLogging);
        st.themeIndex    = vals.themeIndex;
        st.playstationIcons = vals.playstationIcons;
        st.useCustomFont = vals.useCustomFont;
        st.builtInFontIndex = vals.builtInFontIndex;
        st.superGlideCompat = vals.superGlideCompat;
        st.bypassStatCommit = vals.bypassStatCommit;
        snprintf(st.customFont, sizeof(st.customFont), "%s", vals.customFont);

        st.savedLocations.clear();
        for (const auto& loc : vals.savedLocations)
        {
            if (loc.name[0] != '\0' || loc.x != 0.0f || loc.y != 0.0f || loc.z != 0.0f)
                st.savedLocations.push_back(loc);
        }

        // Every key/pad bind persists regardless of Auto Save - a rebind you
        // can't keep between sessions is a bug, not a "feature value". A garbled
        // key falls back to the default; a 0 pad mask legitimately means "no
        // controller bind", so it is honoured as-is (fly binds use 0 to mean
        // "that direction disabled on the pad").
        if (vals.openKeyVk > 0 && vals.openKeyVk <= 0xFF)
            st.openKeyVk = vals.openKeyVk;
        st.openPadMask = vals.openPadMask & 0xFFFF;
        if (vals.flyUpKeyVk >= 0 && vals.flyUpKeyVk <= 0xFF)
            st.flyUpKeyVk = vals.flyUpKeyVk;
        if (vals.flyDownKeyVk >= 0 && vals.flyDownKeyVk <= 0xFF)
            st.flyDownKeyVk = vals.flyDownKeyVk;
        st.flyUpPadMask   = vals.flyUpPadMask   & 0x3FFFF;
        st.flyDownPadMask = vals.flyDownPadMask & 0x3FFFF;
        if (vals.markerTeleportKeyVk >= 0 && vals.markerTeleportKeyVk <= 0xFF)
            st.markerTeleportKeyVk = vals.markerTeleportKeyVk;
        st.markerTeleportPadMask = vals.markerTeleportPadMask & 0x3FFFF;
        if (vals.markerFallbackHeight >= 50.0f && vals.markerFallbackHeight <= 5000.0f)
            st.markerFallbackHeight = vals.markerFallbackHeight;

        st.navSelectPadMask   = vals.navSelectPadMask   & 0x3FFFF;
        st.navBackPadMask     = vals.navBackPadMask     & 0x3FFFF;
        st.navClearPadMask    = vals.navClearPadMask    & 0x3FFFF;
        st.navPrevTabPadMask  = vals.navPrevTabPadMask  & 0x3FFFF;
        st.navNextTabPadMask  = vals.navNextTabPadMask  & 0x3FFFF;
        st.navUpPadMask       = vals.navUpPadMask       & 0x3FFFF;
        st.navDownPadMask     = vals.navDownPadMask     & 0x3FFFF;
        st.navLeftPadMask     = vals.navLeftPadMask     & 0x3FFFF;
        st.navRightPadMask    = vals.navRightPadMask    & 0x3FFFF;

        if (vals.navSelectKeyVk >= 0 && vals.navSelectKeyVk <= 0xFF)   st.navSelectKeyVk   = vals.navSelectKeyVk;
        if (vals.navBackKeyVk >= 0 && vals.navBackKeyVk <= 0xFF)       st.navBackKeyVk     = vals.navBackKeyVk;
        if (vals.navClearKeyVk >= 0 && vals.navClearKeyVk <= 0xFF)     st.navClearKeyVk    = vals.navClearKeyVk;
        if (vals.navPrevTabKeyVk >= 0 && vals.navPrevTabKeyVk <= 0xFF) st.navPrevTabKeyVk  = vals.navPrevTabKeyVk;
        if (vals.navNextTabKeyVk >= 0 && vals.navNextTabKeyVk <= 0xFF) st.navNextTabKeyVk  = vals.navNextTabKeyVk;
        if (vals.navUpKeyVk >= 0 && vals.navUpKeyVk <= 0xFF)           st.navUpKeyVk       = vals.navUpKeyVk;
        if (vals.navDownKeyVk >= 0 && vals.navDownKeyVk <= 0xFF)       st.navDownKeyVk     = vals.navDownKeyVk;
        if (vals.navLeftKeyVk >= 0 && vals.navLeftKeyVk <= 0xFF)       st.navLeftKeyVk     = vals.navLeftKeyVk;
        if (vals.navRightKeyVk >= 0 && vals.navRightKeyVk <= 0xFF)     st.navRightKeyVk    = vals.navRightKeyVk;

        if (!st.autoSave)
            return; // remembered the preference, but features start clean

        // Clamp the floats to the same ranges the menu rows enforce, in case
        // the file was hand-edited.
        st.godMode         = vals.godMode;
        st.oneHitKill      = vals.oneHitKill;
        st.infDurability   = vals.infDurability;
        st.noFallDamage    = vals.noFallDamage;
        st.infStamina      = vals.infStamina;
        st.infMountStamina = vals.infMountStamina || vals.infStamina;
        st.infSpirit       = vals.infSpirit;
        st.easyParry       = vals.easyParry;
        st.easyEvade       = vals.easyEvade;
        st.noBounty        = vals.noBounty;
        st.dmgOutMult    = ClampF(vals.dmgOutMult, 0.0f, 20.0f);
        st.dmgInMult     = ClampF(vals.dmgInMult, 0.0f, 10.0f);
        st.gameSpeed     = vals.gameSpeed;
        st.gameSpeedMult = ClampF(vals.gameSpeedMult, 0.1f, 5.0f);
        st.timeFrozen    = vals.timeFrozen;
        st.superRun      = vals.superRun;
        st.superRunMult  = ClampF(vals.superRunMult, 1.0f, 10.0f);
        st.superJump     = vals.superJump;
        st.superJumpMult = ClampF(vals.superJumpMult, 1.0f, 10.0f);
        st.freeFlight    = vals.freeFlight;
        st.flightSpeed   = ClampF(vals.flightSpeed, 1.0f, 40.0f);
        st.trustMult     = vals.trustMult;
        st.trustMultVal  = ClampF(vals.trustMultVal, 1.0f, 25.0f);
        st.petSlotLimit    = vals.petSlotLimit;
        st.petSlotLimitVal = ClampI(vals.petSlotLimitVal, 30, 999);
        st.invSlotSize     = vals.invSlotSize;
        st.invSlotSizeVal  = ClampI(vals.invSlotSizeVal, 1, 700); // 240 vanilla / 700 modded cap
        st.invStackSize    = vals.invStackSize;
        st.invStackSizeVal = ClampI(vals.invStackSizeVal, 1, 999999999);
        st.showFps       = vals.showFps;
        st.showConsole   = vals.showConsole;
        st.menuScale     = ClampF(vals.menuScale, 0.5f, 2.5f);
        st.tooltipImageScale = vals.tooltipImageScale > 0.1f ? vals.tooltipImageScale : 1.0f;
        st.showItemTooltip = vals.showItemTooltip;

        // Apply language setting
        snprintf(st.languageCode, sizeof(st.languageCode), "%s", vals.languageCode);
        loc::SetLanguageByCode(st.languageCode);
        st.languageIndex = loc::GetCurrentLanguageIndex();

        LOG_OK("Trinity.ini loaded - restored feature settings from last session.");
    }

    void Settings::ClaimOwnership()
    {
        g_owner = true;
    }

    void Settings::Save()
    {
        // Never let a process without a menu write its startup snapshot back.
        if (!g_owner)
            return;

        char path[MAX_PATH];
        char tmp[MAX_PATH];
        if (!IniPath(path, sizeof(path)) || !IniPath(tmp, sizeof(tmp), ".tmp"))
            return;

        // Write through a temp file and swap it in, so an interrupted save (the
        // shutdown one runs while the process is already tearing down) can never
        // leave a truncated Trinity.ini behind - the old file survives instead.
        const State& st = State::Get();
        FILE* f = fopen(tmp, "w");
        if (!f)
        {
            LOG_WARN("Could not write %s - feature settings not saved.", tmp);
            return;
        }

        fprintf(f,
                "; Trinity feature settings - managed from the in-game SYSTEM tab.\n"
                "; *KeyVk = Win32 virtual-key code; *PadMask = XInput button mask.\n"
                "openKeyVk=%d\n"
                "openPadMask=%u\n"
                "flyUpKeyVk=%d\n"
                "flyDownKeyVk=%d\n"
                "flyUpPadMask=%u\n"
                "flyDownPadMask=%u\n"
                "markerTeleportKeyVk=%d\n"
                "markerTeleportPadMask=%u\n"
                "markerFallbackHeight=%.3f\n"
                "navSelectPadMask=%u\n"
                "navBackPadMask=%u\n"
                "navClearPadMask=%u\n"
                "navPrevTabPadMask=%u\n"
                "navNextTabPadMask=%u\n"
                "navUpPadMask=%u\n"
                "navDownPadMask=%u\n"
                "navLeftPadMask=%u\n"
                "navRightPadMask=%u\n"
                "navSelectKeyVk=%d\n"
                "navBackKeyVk=%d\n"
                "navClearKeyVk=%d\n"
                "navPrevTabKeyVk=%d\n"
                "navNextTabKeyVk=%d\n"
                "navUpKeyVk=%d\n"
                "navDownKeyVk=%d\n"
                "navLeftKeyVk=%d\n"
                "navRightKeyVk=%d\n"
                "autoSave=%d\n"
                "godMode=%d\n"
                "oneHitKill=%d\n"
                "infDurability=%d\n"
                "noFallDamage=%d\n"
                "infStamina=%d\n"
                "infMountStamina=%d\n"
                "infSpirit=%d\n"
                "easyParry=%d\n"
                "easyEvade=%d\n"
                "noBounty=%d\n"
                "superGlideCompat=%d\n"
                "bypassStatCommit=%d\n"
                "dmgOutMult=%.3f\n"
                "dmgInMult=%.3f\n"
                "gameSpeed=%d\n"
                "gameSpeedMult=%.3f\n"
                "timeFrozen=%d\n"
                "superRun=%d\n"
                "superRunMult=%.3f\n"
                "superJump=%d\n"
                "superJumpMult=%.3f\n"
                "freeFlight=%d\n"
                "flightSpeed=%.3f\n"
                "trustMult=%d\n"
                "trustMultVal=%.3f\n"
                "petSlotLimit=%d\n"
                "petSlotLimitVal=%d\n"
                "invSlotSize=%d\n"
                "invSlotSizeVal=%d\n"
                "invStackSize=%d\n"
                "invStackSizeVal=%d\n"
                "forceClearSky=%d\n"
                "rainIntensity=%.3f\n"
                "snowIntensity=%.3f\n"
                "dustIntensity=%.3f\n"
                "windMultiplier=%.3f\n"
                "windGust=%.3f\n"
                "windTurbLift=%.3f\n"
                "noWind=%d\n"
                "cloudThick=%.3f\n"
                "cloudTop=%.3f\n"
                "cloudBase=%.3f\n"
                "cloudScrollSpeed=%.3f\n"
                "fogA=%.3f\n"
                "fogB=%.3f\n"
                "clearDistantFog=%d\n"
                "weatherPreset=%d\n"
                "showFps=%d\n"
                "showConsole=%d\n"
                "menuScale=%.3f\n"
                "tooltipImageScale=%.3f\n"
                "showItemTooltip=%d\n"
                "fileLogging=%d\n"
                "perfLogging=%d\n"
                "themeIndex=%d\n"
                "playstationIcons=%d\n"
                "language=%s\n"
                "useCustomFont=%d\n"
                "builtInFontIndex=%d\n"
                "customFont=%s\n",
                st.openKeyVk,
                st.openPadMask,
                st.flyUpKeyVk,
                st.flyDownKeyVk,
                st.flyUpPadMask,
                st.flyDownPadMask,
                st.markerTeleportKeyVk,
                st.markerTeleportPadMask,
                st.markerFallbackHeight,
                st.navSelectPadMask,
                st.navBackPadMask,
                st.navClearPadMask,
                st.navPrevTabPadMask,
                st.navNextTabPadMask,
                st.navUpPadMask,
                st.navDownPadMask,
                st.navLeftPadMask,
                st.navRightPadMask,
                st.navSelectKeyVk,
                st.navBackKeyVk,
                st.navClearKeyVk,
                st.navPrevTabKeyVk,
                st.navNextTabKeyVk,
                st.navUpKeyVk,
                st.navDownKeyVk,
                st.navLeftKeyVk,
                st.navRightKeyVk,
                st.autoSave ? 1 : 0,
                st.godMode ? 1 : 0,
                st.oneHitKill ? 1 : 0,
                st.infDurability ? 1 : 0,
                st.noFallDamage ? 1 : 0,
                st.infStamina ? 1 : 0,
                st.infMountStamina ? 1 : 0,
                st.infSpirit ? 1 : 0,
                st.easyParry ? 1 : 0,
                st.easyEvade ? 1 : 0,
                st.noBounty ? 1 : 0,
                st.superGlideCompat ? 1 : 0,
                st.bypassStatCommit ? 1 : 0,
                st.dmgOutMult,
                st.dmgInMult,
                st.gameSpeed ? 1 : 0,
                st.gameSpeedMult,
                st.timeFrozen ? 1 : 0,
                st.superRun ? 1 : 0,
                st.superRunMult,
                st.superJump ? 1 : 0,
                st.superJumpMult,
                st.freeFlight ? 1 : 0,
                st.flightSpeed,
                st.trustMult ? 1 : 0,
                st.trustMultVal,
                st.petSlotLimit ? 1 : 0,
                st.petSlotLimitVal,
                st.invSlotSize ? 1 : 0,
                st.invSlotSizeVal,
                st.invStackSize ? 1 : 0,
                st.invStackSizeVal,
                st.forceClearSky ? 1 : 0,
                st.rainIntensity,
                st.snowIntensity,
                st.dustIntensity,
                st.windMultiplier,
                st.windGust,
                st.windTurbLift,
                st.noWind ? 1 : 0,
                st.cloudThick,
                st.cloudTop,
                st.cloudBase,
                st.cloudScrollSpeed,
                st.fogA,
                st.fogB,
                st.clearDistantFog ? 1 : 0,
                st.weatherPreset,
                st.showFps ? 1 : 0,
                st.showConsole ? 1 : 0,
                st.menuScale,
                st.tooltipImageScale,
                st.showItemTooltip ? 1 : 0,
                st.fileLogging ? 1 : 0,
                st.perfLogging ? 1 : 0,
                st.themeIndex,
                st.playstationIcons ? 1 : 0,
                loc::GetLanguageCode(st.languageIndex),
                st.useCustomFont ? 1 : 0,
                st.builtInFontIndex,
                st.customFont);

        for (size_t i = 0; i < st.savedLocations.size(); ++i)
        {
            const auto& loc = st.savedLocations[i];
            if (loc.name[0] != '\0' || loc.x != 0.0f || loc.y != 0.0f || loc.z != 0.0f)
            {
                fprintf(f, "loc_name_%zu=%s\nloc_x_%zu=%.3f\nloc_y_%zu=%.3f\nloc_z_%zu=%.3f\n",
                        i, loc.name[0] ? loc.name : "Saved Spot", i, loc.x, i, loc.y, i, loc.z);
            }
        }
        const bool ok = fflush(f) == 0;
        fclose(f);

        if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            if (!CopyFileA(tmp, path, FALSE))
            {
                LOG_WARN("Could not update %s - feature settings not saved.", path);
            }
            DeleteFileA(tmp);
        }
    }

    void Settings::ResetFeatures()
    {
        // Copy the defaults straight off a fresh State so this can never
        // drift from the initializers in state.h. Menu/session state
        // (menuOpen, textCapture, autoSave) is deliberately left alone.
        const State def;
        State&      st = State::Get();
        st.godMode              = def.godMode;
        st.oneHitKill           = def.oneHitKill;
        st.infDurability        = def.infDurability;
        st.noFallDamage         = def.noFallDamage;
        st.infStamina           = def.infStamina;
        st.infMountStamina      = def.infMountStamina;
        st.infSpirit            = def.infSpirit;
        st.easyParry            = def.easyParry;
        st.easyEvade            = def.easyEvade;
        st.noBounty             = def.noBounty;
        st.dmgOutMult           = def.dmgOutMult;
        st.dmgInMult            = def.dmgInMult;
        st.gameSpeed            = def.gameSpeed;
        st.gameSpeedMult        = def.gameSpeedMult;
        st.timeFrozen           = def.timeFrozen;
        st.superRun             = def.superRun;
        st.superRunMult         = def.superRunMult;
        st.superJump            = def.superJump;
        st.superJumpMult        = def.superJumpMult;
        st.freeFlight           = def.freeFlight;
        st.flightSpeed          = def.flightSpeed;
        st.markerFallbackHeight = def.markerFallbackHeight;
        st.trustMult            = def.trustMult;
        st.trustMultVal         = def.trustMultVal;
        st.petSlotLimit         = def.petSlotLimit;
        st.petSlotLimitVal      = def.petSlotLimitVal;
        st.invSlotSize          = def.invSlotSize;
        st.invSlotSizeVal       = def.invSlotSizeVal;
        st.invStackSize         = def.invStackSize;
        st.invStackSizeVal      = def.invStackSizeVal;
        st.showFps              = def.showFps;
        st.perfLogging          = def.perfLogging;
        core::TickMetrics::SetEnabled(st.perfLogging);
    }

    void Settings::ResetBinds()
    {
        const State def;
        State&      st = State::Get();
        st.openKeyVk              = def.openKeyVk;
        st.openPadMask            = def.openPadMask;
        st.flyUpKeyVk             = def.flyUpKeyVk;
        st.flyDownKeyVk           = def.flyDownKeyVk;
        st.flyUpPadMask           = def.flyUpPadMask;
        st.flyDownPadMask         = def.flyDownPadMask;
        st.markerTeleportKeyVk    = def.markerTeleportKeyVk;
        st.markerTeleportPadMask  = def.markerTeleportPadMask;
        st.navSelectPadMask       = def.navSelectPadMask;
        st.navBackPadMask         = def.navBackPadMask;
        st.navClearPadMask        = def.navClearPadMask;
        st.navPrevTabPadMask      = def.navPrevTabPadMask;
        st.navNextTabPadMask      = def.navNextTabPadMask;
        st.navUpPadMask           = def.navUpPadMask;
        st.navDownPadMask         = def.navDownPadMask;
        st.navLeftPadMask         = def.navLeftPadMask;
        st.navRightPadMask        = def.navRightPadMask;
        st.navSelectKeyVk         = def.navSelectKeyVk;
        st.navBackKeyVk           = def.navBackKeyVk;
        st.navClearKeyVk          = def.navClearKeyVk;
        st.navPrevTabKeyVk        = def.navPrevTabKeyVk;
        st.navNextTabKeyVk        = def.navNextTabKeyVk;
        st.navUpKeyVk             = def.navUpKeyVk;
        st.navDownKeyVk           = def.navDownKeyVk;
        st.navLeftKeyVk           = def.navLeftKeyVk;
        st.navRightKeyVk          = def.navRightKeyVk;
    }
}
