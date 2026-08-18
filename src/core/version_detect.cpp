#include "version_detect.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include "../core/logger.h"
#include "../mem/scanner.h"
#include "../game/offsets.h"

#pragma comment(lib, "version.lib")

namespace trinity::core
{
    namespace
    {
        GameVersionInfo g_versionInfo{};
        bool            g_detected = false;

        void DetectVersion()
        {
            if (g_detected) return;
            g_detected = true;

            wchar_t exePath[MAX_PATH] = { 0 };
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH))
            {
                DWORD handle = 0;
                DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
                if (size > 0)
                {
                    BYTE* buffer = new BYTE[size];
                    if (GetFileVersionInfoW(exePath, handle, size, buffer))
                    {
                        VS_FIXEDFILEINFO* pFileInfo = nullptr;
                        UINT len = 0;
                        if (VerQueryValueW(buffer, L"\\", reinterpret_cast<LPVOID*>(&pFileInfo), &len) && pFileInfo && len >= sizeof(VS_FIXEDFILEINFO))
                        {
                            g_versionInfo.major    = static_cast<uint16_t>(HIWORD(pFileInfo->dwFileVersionMS));
                            g_versionInfo.minor    = static_cast<uint16_t>(LOWORD(pFileInfo->dwFileVersionMS));
                            g_versionInfo.build    = static_cast<uint16_t>(HIWORD(pFileInfo->dwFileVersionLS));
                            g_versionInfo.revision = static_cast<uint16_t>(LOWORD(pFileInfo->dwFileVersionLS));

                            snprintf(g_versionInfo.rawVersionStr, sizeof(g_versionInfo.rawVersionStr),
                                     "%u.%u.%u.%u",
                                     g_versionInfo.major, g_versionInfo.minor,
                                     g_versionInfo.build, g_versionInfo.revision);
                        }
                    }
                    delete[] buffer;
                }
            }

            // In-Memory Binary Fingerprinting:
            // Pearl Abyss keeps the PE resource version static (1.0.0.2474) across multiple Steam updates.
            // We inspect the live machine code signatures in game memory to determine the exact Title Update.
            const bool hasModernDyeBatch = (mem::FindPattern(game::kSig_DyeApplyBatch) != 0);
            const bool hasLegacyDyeBatch = (mem::FindPattern(game::kSig_DyeApplyBatch_Legacy) != 0);

            if (hasModernDyeBatch)
            {
                g_versionInfo.tu = GameTU::TU_1_18_01_Plus;
                snprintf(g_versionInfo.displayStr, sizeof(g_versionInfo.displayStr),
                         "Crimson Desert TU 1.18.02 (Active)");
            }
            else if (hasLegacyDyeBatch)
            {
                g_versionInfo.tu = GameTU::TU_1_14;
                snprintf(g_versionInfo.displayStr, sizeof(g_versionInfo.displayStr),
                         "Crimson Desert TU 1.14 - 1.15 (Legacy Compatible)");
            }
            else
            {
                // Fallback default
                g_versionInfo.tu = GameTU::TU_1_18_01_Plus;
                snprintf(g_versionInfo.displayStr, sizeof(g_versionInfo.displayStr),
                         "Crimson Desert TU 1.18.02 (Active)");
            }

            g_versionInfo.isSupported = true;
            LOG_OK("version: Game version detected: %s [PE: %s]", g_versionInfo.displayStr, g_versionInfo.rawVersionStr);
        }
    }

    const GameVersionInfo& GetGameVersion()
    {
        DetectVersion();
        return g_versionInfo;
    }

    const char* GetGameVersionDisplay()
    {
        return GetGameVersion().displayStr;
    }

    bool IsTU118OrNewer()
    {
        const GameVersionInfo& info = GetGameVersion();
        return info.tu == GameTU::TU_1_18 || info.tu == GameTU::TU_1_18_01_Plus || info.tu == GameTU::Unknown;
    }

    bool IsLegacyTU()
    {
        const GameVersionInfo& info = GetGameVersion();
        return info.tu == GameTU::TU_1_14 || info.tu == GameTU::TU_1_15 || info.tu == GameTU::TU_1_16;
    }

    uintptr_t GetPlacementStride()
    {
        const GameVersionInfo& info = GetGameVersion();
        if (info.tu == GameTU::TU_1_13 || info.tu == GameTU::TU_1_14 || info.tu == GameTU::TU_1_15)
            return 216; // 216 (0xD8) in TU 1.13 - 1.15
        return 0xE0;    // 224 (0xE0) in TU 1.16+
    }

    uintptr_t GetPlacementSlotIdxOffset()
    {
        const GameVersionInfo& info = GetGameVersion();
        if (info.tu == GameTU::TU_1_13 || info.tu == GameTU::TU_1_14 || info.tu == GameTU::TU_1_15)
            return 208; // 208 (0xD0) in TU 1.13 - 1.15
        return 0xD8;    // 216 (0xD8) in TU 1.16+
    }

    uintptr_t GetSlotStride()
    {
        const GameVersionInfo& info = GetGameVersion();
        if (info.tu == GameTU::TU_1_13 || info.tu == GameTU::TU_1_14 || info.tu == GameTU::TU_1_15)
            return 0xC0; // 192 bytes in TU 1.13 - 1.15
        return 0xC8;     // 200 bytes in TU 1.16+
    }

    uintptr_t GetItemValSocketOffset()
    {
        const GameVersionInfo& info = GetGameVersion();
        if (info.tu == GameTU::TU_1_13 || info.tu == GameTU::TU_1_14 || info.tu == GameTU::TU_1_15 || info.tu == GameTU::TU_1_16)
            return 0x58; // Legacy offset in TU <= 1.16
        return 0x60;     // Modern offset in TU 1.17+
    }

    uintptr_t GetItemDefBucketTypeOffset()
    {
        const GameVersionInfo& info = GetGameVersion();
        if (info.tu == GameTU::TU_1_13 || info.tu == GameTU::TU_1_14 || info.tu == GameTU::TU_1_15 || info.tu == GameTU::TU_1_16)
            return 66; // Legacy bucket type offset 0x42 (66)
        return 0x418;  // Modern bucket type offset in TU 1.17+
    }

    uintptr_t GetItemValWorkingSize()
    {
        const GameVersionInfo& info = GetGameVersion();
        if (info.tu == GameTU::TU_1_13 || info.tu == GameTU::TU_1_14 || info.tu == GameTU::TU_1_15)
            return 0xC0;  // 192 bytes buffer in 1.15
        return 0x108;     // 264 bytes buffer in 1.16+
    }
}
