#pragma once

#include <cstdint>

namespace trinity::core
{
    enum class GameTU : uint32_t
    {
        Unknown = 0,
        TU_1_13,
        TU_1_14,
        TU_1_15,
        TU_1_16,
        TU_1_17,
        TU_1_18,
        TU_1_18_01_Plus,
    };

    struct GameVersionInfo
    {
        uint16_t major = 0;
        uint16_t minor = 0;
        uint16_t build = 0;
        uint16_t revision = 0;
        GameTU   tu = GameTU::Unknown;
        char     rawVersionStr[64] = "Unknown";
        char     displayStr[96]    = "Crimson Desert (Auto-Detecting)";
        bool     isSupported       = true;
    };

    // Detects and caches the Crimson Desert game executable version.
    const GameVersionInfo& GetGameVersion();

    // Returns a friendly display string e.g. "Crimson Desert TU 1.18.01" or "TU 1.17.00"
    const char* GetGameVersionDisplay();

    // True if running on TU 1.18 or newer
    bool IsTU118OrNewer();

    // True if running on legacy TU 1.14 - 1.16
    bool IsLegacyTU();

    uintptr_t GetPlacementStride();
    uintptr_t GetPlacementSlotIdxOffset();
    uintptr_t GetSlotStride();
    uintptr_t GetItemValSocketOffset();
    uintptr_t GetItemDefBucketTypeOffset();
    uintptr_t GetItemValWorkingSize();
}
