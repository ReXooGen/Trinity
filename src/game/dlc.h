#pragma once

namespace trinity::game
{
    // Built-in Steamworks DLC entitlement and ownership bypass.
    // Intercepts Steamworks API validation methods (BIsDlcInstalled, BIsSubscribedApp, UserHasLicenseForApp)
    // in-process so the engine treats all Deluxe Edition, Special Edition, and preorder DLC items as
    // legitimately owned, preventing item revocation and save-load reconciliation crashes.
    class DLC
    {
    public:
        // Installs the Steamworks DLC entitlement hooks. Requires MH_Initialize() first.
        // Non-fatal: if steam_api64.dll or export signatures fail to resolve, the mod continues.
        static bool Install();
        static void Remove();

        // True once DLC ownership hooks are active.
        static bool Ready();
    };
}
