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
#include "crash_diagnostics.h"
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
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "mod.initialize.begin",
            reinterpret_cast<std::uintptr_t>(module),
            0,
            0,
            true);
        LOG("Trinity v%s initializing (built %s).", TRINITY_VERSION, TRINITY_BUILD_TIME);

        // Detect and log game version on startup
        core::GetGameVersion();

        // Initialize localization subsystem (scans Trinity_*.ini beside Trinity.asi)
        loc::Init();

        // Restore last session's feature settings (Trinity.ini) before the
        // feature hooks install, so restored toggles apply from frame one.
        Settings::Load();

        const bool mhOk = (MH_Initialize() == MH_OK);
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "minhook.initialize",
            0,
            0,
            0,
            mhOk);
        if (!mhOk)
        {
            LOG("MinHook initialization failed.");
            return;
        }

        const bool dx12Ok = hooks::InstallDX12Hooks();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "dx12.initialize",
            0,
            0,
            0,
            dx12Ok);
        if (!dx12Ok)
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
        const bool playerOk = game::Player::Install();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "player.initialize", 0, 0, 0, playerOk);

        const bool teleportOk = game::Teleport::Install();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "teleport.initialize", 0, 0, 0, teleportOk);

        const bool inventoryOk = game::Inventory::Install();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "inventory.initialize", 0, 0, 0, inventoryOk);

        const bool worldOk = game::World::Install();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "world.initialize", 0, 0, 0, worldOk);

        const bool equipOk = game::Equipment::Install();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "equipment.initialize", 0, 0, 0, equipOk);

        const bool friendlyOk = game::Friendly::Install();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "friendly.initialize", 0, 0, 0, friendlyOk);
#if defined(TRINITY_EXTENDED)
        const bool dlcOk = game::DLC::Install();
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "dlc.initialize", 0, 0, 0, dlcOk);
#endif

        m_initialized = true;
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "mod.initialize.complete",
            reinterpret_cast<std::uintptr_t>(module),
            0,
            0,
            true);
        LOG_OK("Ready - INSERT (or LB + DOWN on controller) toggles the menu in-game.");
        LOG_OK("If something isn't working or you'd like to leave a comment, visit my blog: https://mul0.com/trainer/crimson-desert-trinity-mod-menu/");
    }

    void Mod::Shutdown()
    {
        if (!m_initialized)
            return;

        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "mod.shutdown.begin", 0, 0, 0, true);

        // Menu changes already save as they happen; this catches anything
        // mutated outside the menu since the last write. In the launcher this
        // is inert - Save() only writes for the process that owns the file.
        if (State::Get().autoSave)
            Settings::Save();

#if defined(TRINITY_EXTENDED)
        game::DLC::Remove();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "dlc.shutdown", 0, 0, 0, true);
#endif
        game::Friendly::Remove();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "friendly.shutdown", 0, 0, 0, true);

        game::Equipment::Remove();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "equipment.shutdown", 0, 0, 0, true);

        game::World::Remove();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "world.shutdown", 0, 0, 0, true);

        game::Inventory::Remove();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "inventory.shutdown", 0, 0, 0, true);

        game::Teleport::Remove();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "teleport.shutdown", 0, 0, 0, true);

        game::Player::Remove();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "player.shutdown", 0, 0, 0, true);

        hooks::RemoveDX12Hooks();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "dx12.shutdown", 0, 0, 0, true);

        MH_Uninitialize();
        core::CrashDiagnostics::Record(core::diag::BreadcrumbKind::Operation, "minhook.shutdown", 0, 0, 0, true);

        Logger::Shutdown();
        m_initialized = false;
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::Operation,
            "mod.shutdown.complete", 0, 0, 0, true);
    }
}

