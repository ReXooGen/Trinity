#include "mod.h"
#include <MinHook.h>
#include "logger.h"
#include "settings.h"
#include "state.h"
#include "version.h"
#include "localization.h"
#include "../hooks/dx12_hook.h"
#include "../game/player.h"
#include "../game/teleport.h"
#include "../game/inventory.h"
#include "../game/world.h"
#include "../game/dye.h"
#include "../game/equipment.h"
#include "../game/friendly.h"

namespace trinity
{
    void Mod::Initialize(HMODULE module)
    {
        if (m_initialized)
            return;

        m_module = module;
        // Console is created lazily from the render path, so only the process
        // that actually presents the game gets one. Early logs buffer until then.
        LOG("Trinity v%s initializing (built %s %s).", TRINITY_VERSION, __DATE__, __TIME__);

        // Initialize localization subsystem (scans Trinity_*.ini beside Trinity.asi)
        loc::Init();

        // Restore last session's feature settings (Trinity.ini) before the
        // feature hooks install, so restored toggles apply from frame one.
        Settings::Load();

        if (MH_Initialize() != MH_OK)
        {
            LOG("MinHook initialization failed.");
            return;
        }

        if (!hooks::InstallDX12Hooks())
        {
            LOG("Failed to install DX12 hooks.");
            MH_Uninitialize();
            return;
        }

        // Gameplay features. Non-fatal: if a signature ever fails to resolve
        // the overlay still runs, the feature is just disabled and logged.
        game::Player::Install();    // God Mode / Infinite Stamina
        game::Teleport::Install();  // Live position tracking / Fast Travel
        game::Inventory::Install(); // Item browser / quantity editor
        game::World::Install();     // Game Speed / Time of Day (Freeze, Advance)
        game::Dye::Install();       // Armor dye / material / repair look
        game::Equipment::Install(); // Abyss-gear socket editor
        game::Friendly::Install();  // Trust Multiplier (gift/feed/tame)

        m_initialized = true;
        LOG_OK("Ready - INSERT (or LB + DOWN on controller) toggles the menu in-game.");
    }

    void Mod::Shutdown()
    {
        if (!m_initialized)
            return;

        // Menu changes already save as they happen; this catches anything
        // mutated outside the menu since the last write. In the launcher this
        // is inert - Save() only writes for the process that owns the file.
        if (State::Get().autoSave)
            Settings::Save();

        game::Player::Remove();
        game::Teleport::Remove();
        game::Inventory::Remove();
        game::World::Remove();
        game::Dye::Remove();
        game::Equipment::Remove();
        game::Friendly::Remove();
        hooks::RemoveDX12Hooks();
        MH_Uninitialize();
        Logger::Shutdown();
        m_initialized = false;
    }
}
