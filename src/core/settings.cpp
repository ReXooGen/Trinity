#include "settings.h"

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
            else if (!strcmp(key, "autoSave"))            vals.autoSave            = atoi(val) != 0;
            else if (!strcmp(key, "godMode"))             vals.godMode             = atoi(val) != 0;
            else if (!strcmp(key, "infStamina"))          vals.infStamina          = atoi(val) != 0;
            else if (!strcmp(key, "infSpirit"))           vals.infSpirit           = atoi(val) != 0;
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
            else if (!strcmp(key, "invSlotSize"))         vals.invSlotSize         = atoi(val) != 0;
            else if (!strcmp(key, "invSlotSizeVal"))      vals.invSlotSizeVal      = atoi(val);
            else if (!strcmp(key, "invStackSize"))        vals.invStackSize        = atoi(val) != 0;
            else if (!strcmp(key, "invStackSizeVal"))     vals.invStackSizeVal     = atoi(val);
            else if (!strcmp(key, "clearDistantFog"))     vals.clearDistantFog     = atoi(val) != 0;
            else if (!strcmp(key, "weatherPreset"))       vals.weatherPreset       = atoi(val);
            else if (!strcmp(key, "showFps"))             vals.showFps             = atoi(val) != 0;
            else if (!strcmp(key, "fileLogging"))         vals.fileLogging         = atoi(val) != 0;
            else if (!strcmp(key, "themeIndex"))          vals.themeIndex          = atoi(val);
            else if (!strcmp(key, "language"))
            {
                snprintf(vals.languageCode, sizeof(vals.languageCode), "%s", val);
                size_t len = strlen(vals.languageCode);
                while (len > 0 && (vals.languageCode[len - 1] == '\r' || vals.languageCode[len - 1] == '\n' || vals.languageCode[len - 1] == ' '))
                    vals.languageCode[--len] = '\0';
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
        st.autoSave = vals.autoSave;
        st.fileLogging = vals.fileLogging;
        st.themeIndex = vals.themeIndex;
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

        if (!st.autoSave)
            return; // remembered the preference, but features start clean

        // Clamp the floats to the same ranges the menu rows enforce, in case
        // the file was hand-edited.
        st.godMode       = vals.godMode;
        st.infStamina    = vals.infStamina;
        st.infSpirit     = vals.infSpirit;
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
        st.invSlotSize     = vals.invSlotSize;
        st.invSlotSizeVal  = ClampI(vals.invSlotSizeVal, 1, 9999);
        st.invStackSize    = vals.invStackSize;
        st.invStackSizeVal = ClampI(vals.invStackSizeVal, 1, 999999999);
        st.showFps       = vals.showFps;

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
                "autoSave=%d\n"
                "godMode=%d\n"
                "infStamina=%d\n"
                "infSpirit=%d\n"
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
                "invSlotSize=%d\n"
                "invSlotSizeVal=%d\n"
                "invStackSize=%d\n"
                "invStackSizeVal=%d\n"
                "clearDistantFog=%d\n"
                "weatherPreset=%d\n"
                "showFps=%d\n"
                "fileLogging=%d\n"
                "themeIndex=%d\n"
                "language=%s\n",
                st.openKeyVk,
                st.openPadMask,
                st.flyUpKeyVk,
                st.flyDownKeyVk,
                st.flyUpPadMask,
                st.flyDownPadMask,
                st.markerTeleportKeyVk,
                st.markerTeleportPadMask,
                st.markerFallbackHeight,
                st.autoSave ? 1 : 0,
                st.godMode ? 1 : 0,
                st.infStamina ? 1 : 0,
                st.infSpirit ? 1 : 0,
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
                st.invSlotSize ? 1 : 0,
                st.invSlotSizeVal,
                st.invStackSize ? 1 : 0,
                st.invStackSizeVal,
                st.clearDistantFog ? 1 : 0,
                st.weatherPreset,
                st.showFps ? 1 : 0,
                st.fileLogging ? 1 : 0,
                st.themeIndex,
                loc::GetLanguageCode(st.languageIndex));

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

        if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING))
        {
            LOG_WARN("Could not update %s - feature settings not saved.", path);
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
        st.infStamina           = def.infStamina;
        st.infSpirit            = def.infSpirit;
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
        st.invSlotSize          = def.invSlotSize;
        st.invSlotSizeVal       = def.invSlotSizeVal;
        st.invStackSize         = def.invStackSize;
        st.invStackSizeVal      = def.invStackSizeVal;
        st.showFps              = def.showFps;
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
    }
}
