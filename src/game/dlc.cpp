#include "dlc.h"

#include <Windows.h>
#include <cstdint>
#include <MinHook.h>

#include "../core/logger.h"
#include "../mem/hooks.h"

namespace trinity::game
{
    namespace
    {
        // Steamworks API Flat Function Prototypes
        // Calling convention on x64 Windows is standard x64 __cdecl / __fastcall
        using SteamAPI_ISteamApps_BIsDlcInstalled_t = bool(__cdecl*)(void* self, uint32_t appID);
        using SteamAPI_ISteamApps_BIsSubscribedApp_t = bool(__cdecl*)(void* self, uint32_t appID);
        using SteamAPI_ISteamUser_UserHasLicenseForApp_t = int(__cdecl*)(void* self, uint64_t steamID, uint32_t appID);

        SteamAPI_ISteamApps_BIsDlcInstalled_t oBIsDlcInstalled = nullptr;
        SteamAPI_ISteamApps_BIsSubscribedApp_t oBIsSubscribedApp = nullptr;
        SteamAPI_ISteamUser_UserHasLicenseForApp_t oUserHasLicenseForApp = nullptr;

        void* g_targetBIsDlcInstalled = nullptr;
        void* g_targetBIsSubscribedApp = nullptr;
        void* g_targetUserHasLicenseForApp = nullptr;

        // Detour: always report DLC as installed
        bool __cdecl hkSteamAPI_ISteamApps_BIsDlcInstalled(void* self, uint32_t appID)
        {
            return true;
        }

        // Detour: always report user is subscribed to the DLC/App ID
        bool __cdecl hkSteamAPI_ISteamApps_BIsSubscribedApp(void* self, uint32_t appID)
        {
            return true;
        }

        // Detour: always return k_EUserHasLicenseResultHasLicense (1)
        int __cdecl hkSteamAPI_ISteamUser_UserHasLicenseForApp(void* self, uint64_t steamID, uint32_t appID)
        {
            return 1; // k_EUserHasLicenseResultHasLicense
        }
    }

    bool DLC::Install()
    {
        HMODULE hSteam = GetModuleHandleA("steam_api64.dll");
        if (!hSteam)
        {
            hSteam = GetModuleHandleA("steam_api64");
        }
        if (!hSteam)
        {
            LOG_WARN("dlc: steam_api64.dll not found in process - DLC entitlement hook skipped.");
            return false;
        }

        int hookCount = 0;

        auto HookFunc = [&](const char* name, void* detour, void** original, void** target) {
            FARPROC proc = GetProcAddress(hSteam, name);
            if (!proc)
            {
                LOG_WARN("dlc: Export '%s' not found in steam_api64.dll.", name);
                return false;
            }

            if (MH_CreateHook(reinterpret_cast<void*>(proc), detour, original) != MH_OK ||
                MH_EnableHook(reinterpret_cast<void*>(proc)) != MH_OK)
            {
                LOG_ERR("dlc: Failed to hook '%s'.", name);
                *original = nullptr;
                return false;
            }

            *target = reinterpret_cast<void*>(proc);
            hookCount++;
            return true;
        };

        HookFunc("SteamAPI_ISteamApps_BIsDlcInstalled",
                 reinterpret_cast<void*>(&hkSteamAPI_ISteamApps_BIsDlcInstalled),
                 reinterpret_cast<void**>(&oBIsDlcInstalled),
                 &g_targetBIsDlcInstalled);

        HookFunc("SteamAPI_ISteamApps_BIsSubscribedApp",
                 reinterpret_cast<void*>(&hkSteamAPI_ISteamApps_BIsSubscribedApp),
                 reinterpret_cast<void**>(&oBIsSubscribedApp),
                 &g_targetBIsSubscribedApp);

        HookFunc("SteamAPI_ISteamUser_UserHasLicenseForApp",
                 reinterpret_cast<void*>(&hkSteamAPI_ISteamUser_UserHasLicenseForApp),
                 reinterpret_cast<void**>(&oUserHasLicenseForApp),
                 &g_targetUserHasLicenseForApp);

        if (hookCount > 0)
        {
            LOG_OK("dlc: Steamworks DLC entitlement hooks installed (%d functions hooked in steam_api64.dll at 0x%p).",
                   hookCount, static_cast<void*>(hSteam));
            return true;
        }

        LOG_WARN("dlc: No Steamworks API functions were hooked.");
        return false;
    }

    void DLC::Remove()
    {
        mem::RemoveHook(&g_targetBIsDlcInstalled);
        mem::RemoveHook(&g_targetBIsSubscribedApp);
        mem::RemoveHook(&g_targetUserHasLicenseForApp);
        oBIsDlcInstalled = nullptr;
        oBIsSubscribedApp = nullptr;
        oUserHasLicenseForApp = nullptr;
    }

    bool DLC::Ready()
    {
        return g_targetBIsDlcInstalled != nullptr ||
               g_targetBIsSubscribedApp != nullptr ||
               g_targetUserHasLicenseForApp != nullptr;
    }
}
