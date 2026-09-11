#include "mod.h"
#include <MinHook.h>
#include <iterator>
#include "logger.h"
#include "settings.h"
#include "state.h"
#include "version.h"
#include "build_timestamp.h"
#include "localization.h"
#include "version_detect.h"
#include "readiness.h"
#include "../hooks/dx12_hook.h"
#include "../mem/scanner.h"
#include "../game/offsets.h"
#include "../game/player.h"
#include "../game/teleport.h"
#include "../game/inventory.h"
#include "../game/world.h"
#include "../game/equipment.h"
#include "../game/friendly.h"
#if defined(TRINITY_EXTENDED)
#include "../game/dlc.h"
#endif

namespace
{
    bool GameplayCodeReady()
    {
        using namespace trinity::game;

        // PE 2760 removed several TU 2.00.02 functions. Waiting for those old
        // signatures would guarantee a three-minute timeout, so use only the
        // independently confirmed 2.01.00 sentinels on that revision.
        const char* const currentRequired[] = {
            kSig_DamageApply_Alt,
            kSig_CombatTimingEval,
            kSig_MoveUpdate,
            kSig_InvGetItemQty,
            kSig_EvaluateCrimeWantedState,
            kSig_TodEngineGlobal,
            kSig_WeatherRain,
        };
        const char* const legacyRequired[] = {
            kCharMgrAnchors[0].sig,
            kSig_StatCommit,
            kSig_DamageApply_Alt,
            kSig_CombatTimingEval,
            kSig_MoveUpdate,
            kSig_InvGetItemQty,
            kSig_EvaluateCrimeWantedState,
            kSig_FrameTimerBody,
            kSig_FieldTimeTick,
            kSig_TodEngineGlobal,
            kSig_WeatherRain,
            kSig_EquipEffectRefresh,
        };

        const auto profile = trinity::core::ReadinessProfileForRevision(
            trinity::core::GetGameVersion().revision);
        const char* const* required = profile == trinity::core::ReadinessProfile::Tu201KnownCompatible
            ? currentRequired : legacyRequired;
        const size_t requiredCount = profile == trinity::core::ReadinessProfile::Tu201KnownCompatible
            ? std::size(currentRequired) : std::size(legacyRequired);

        // Startup probing only needs presence. The actual installers retain
        // their stricter uniqueness/consensus checks. Stopping at the first
        // hit keeps polling light while the packed image is materialising.
        for (size_t i = 0; i < requiredCount; ++i)
            if (!trinity::mem::FindPattern(required[i]))
                return false;
        return true;
    }
}

namespace trinity
{
    void Mod::Initialize(HMODULE module)
    {
        if (m_initialized)
            return;

        m_module = module;
        LOG("Trinity v%s initializing (built %s).", TRINITY_VERSION, TRINITY_BUILD_TIME);

        // Detect and log game version on startup
        core::GetGameVersion();

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

        // Modern builds materialise large gameplay-code regions after the
        // ASI loader starts us. A single early scan therefore produced dozens
        // of false NOT FOUND results even though the exact AOBs appeared a few
        // seconds later. Keep the render hook responsive and wait on this
        // worker thread until every gameplay subsystem is actually scannable.
        LOG("Waiting for %s gameplay code to become ready...", core::GetGameVersionDisplay());
        const bool codeReady = core::WaitForReadiness(
            &GameplayCodeReady,
            [] { return GetTickCount64(); },
            [](uint32_t ms) { Sleep(ms); },
            180000,
            2000);
        if (codeReady)
            LOG_OK("Gameplay code ready - installing feature hooks.");
        else
            LOG_WARN("Gameplay-code readiness timed out after 180 seconds; installing available hooks only.");

        // Gameplay features. Non-fatal: if a signature ever fails to resolve
        // the overlay still runs, the feature is just disabled and logged.
        game::Player::Install();    // God Mode / Infinite Stamina
        game::Teleport::Install();  // Live position tracking / Fast Travel
        game::Inventory::Install(); // Item browser / quantity editor
        game::World::Install();     // Game Speed / Time of Day (Freeze, Advance)
        game::Equipment::Install(); // Abyss-gear socket editor
        game::Friendly::Install();  // Trust Multiplier (gift/feed/tame)
#if defined(TRINITY_EXTENDED)
        game::DLC::Install();
#endif

        m_initialized = true;
        LOG_OK("Ready - INSERT (or LB + DOWN on controller) toggles the menu in-game.");
        LOG_OK("If something isn't working or you'd like to leave a comment, visit my blog: https://mul0.com/trainer/crimson-desert-trinity-mod-menu/");
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
        game::Equipment::Remove();
        game::Friendly::Remove();
#if defined(TRINITY_EXTENDED)
        game::DLC::Remove();
#endif
        hooks::RemoveDX12Hooks();
        MH_Uninitialize();
        Logger::Shutdown();
        m_initialized = false;
    }
}

