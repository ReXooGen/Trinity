#include "worker.h"

#include <cstdint>

#include "offsets.h"
#include "worker_logic.h"
#include "../core/crash_diagnostics.h"
#include "../core/logger.h"
#include "../core/state.h"
#include "../core/version_detect.h"
#include "../mem/safe_memory.h"
#include "../mem/scanner.h"

namespace trinity::game
{
    namespace
    {
        uintptr_t g_patchTarget = 0;
        bool      g_ready = false;
        bool      g_enabled = false;

        bool ReadPatchBytes(uint8_t* out)
        {
            if (!out || !mem::IsValidUserPtr(g_patchTarget) ||
                !mem::IsValidUserPtr(g_patchTarget + kWorkerPatchSize - 1))
                return false;

            for (size_t i = 0; i < kWorkerPatchSize; ++i)
            {
                if (!mem::Read8(g_patchTarget + i, &out[i]))
                    return false;
            }
            return true;
        }

        bool BytesEqual(const uint8_t* left, const uint8_t* right)
        {
            return left && right &&
                   std::memcmp(left, right, kWorkerPatchSize) == 0;
        }
    }

    bool Worker::Install()
    {
        if (g_ready)
            return true;

        g_patchTarget = 0;
        g_enabled = false;

        const int revision = static_cast<int>(core::GetGameVersion().revision);
        if (!WorkerPatchSupportedForRevision(revision))
        {
            LOG_WARN("worker: supplied level/ability patch disabled for unsupported PE revision %d.", revision);
            return false;
        }

        const auto matches = mem::FindAllMatches(kSig_WorkerMaxLevelAndSkills, 2);
        if (matches.size() != 1)
        {
            LOG_WARN("worker: level/ability patch signature expected one match, found %zu; feature disabled.",
                     matches.size());
            return false;
        }

        g_patchTarget = matches[0];
        uint8_t current[kWorkerPatchSize] = {};
        if (!ReadPatchBytes(current))
        {
            LOG_WARN("worker: exact patch target at %p could not be read; feature disabled.",
                     reinterpret_cast<void*>(g_patchTarget));
            g_patchTarget = 0;
            return false;
        }

        if (BytesEqual(current, kWorkerPatchOriginal))
        {
            g_enabled = false;
        }
        else if (BytesEqual(current, kWorkerPatchEnabled))
        {
            g_enabled = true;
        }
        else
        {
            LOG_WARN("worker: target at %p contains unexpected bytes; feature disabled.",
                     reinterpret_cast<void*>(g_patchTarget));
            g_patchTarget = 0;
            return false;
        }

        g_ready = true;
        LOG_OK("worker: level and ability patch target resolved (%s) [OK]",
               g_enabled ? "already enabled" : "original");
        LOG_DEBUG("worker: level and ability patch target resolved @ %p (%s)",
                  reinterpret_cast<void*>(g_patchTarget), g_enabled ? "already enabled" : "original");

        State& state = State::Get();
        if (state.workerMaxLevelAndSkills && !g_enabled && !SetEnabled(true))
        {
            state.workerMaxLevelAndSkills = false;
            LOG_WARN("worker: saved max level/skills state could not be applied; reset to OFF.");
        }
        else if (g_enabled)
        {
            state.workerMaxLevelAndSkills = true;
        }

        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::HookState,
            "hook.worker",
            g_patchTarget,
            static_cast<uint32_t>(kWorkerPatchSize),
            0,
            g_ready);

        return true;
    }

    void Worker::Remove()
    {
        if (g_ready && g_patchTarget)
        {
            uint8_t current[kWorkerPatchSize] = {};
            if (ReadPatchBytes(current) && BytesEqual(current, kWorkerPatchEnabled))
            {
                if (!mem::PatchMemory(g_patchTarget, kWorkerPatchOriginal, kWorkerPatchSize))
                    LOG_WARN("worker: could not restore original patch bytes at shutdown @ %p.",
                             reinterpret_cast<void*>(g_patchTarget));
            }
            else if (!BytesEqual(current, kWorkerPatchOriginal))
            {
                LOG_WARN("worker: shutdown found unexpected bytes at %p; left target untouched.",
                         reinterpret_cast<void*>(g_patchTarget));
            }
        }

        g_patchTarget = 0;
        g_ready = false;
        g_enabled = false;
    }

    bool Worker::Ready()
    {
        return g_ready;
    }

    bool Worker::Enabled()
    {
        return g_enabled;
    }

    bool Worker::SetEnabled(bool enabled)
    {
        if (!g_ready || !g_patchTarget)
            return false;

        core::CrashDiagnostics::MutationScope scope("worker.job-time");

        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::PatchState,
            "patch.worker.job-time",
            g_patchTarget,
            static_cast<uint32_t>(kWorkerPatchSize),
            enabled ? 1 : 0,
            true);

        uint8_t current[kWorkerPatchSize] = {};
        if (!ReadPatchBytes(current))
        {
            core::CrashDiagnostics::Record(
                core::diag::BreadcrumbKind::PatchState,
                "patch.worker.job-time",
                g_patchTarget,
                static_cast<uint32_t>(kWorkerPatchSize),
                enabled ? 1 : 0,
                false);
            return false;
        }

        if (enabled)
        {
            if (BytesEqual(current, kWorkerPatchEnabled))
            {
                g_enabled = true;
                return true;
            }

            if (!CanTransitionWorkerPatch(WorkerPatchState::Original,
                                          WorkerPatchState::Patched,
                                          current, kWorkerPatchOriginal,
                                          kWorkerPatchSize))
            {
                core::CrashDiagnostics::Record(
                    core::diag::BreadcrumbKind::PatchState,
                    "patch.worker.job-time",
                    g_patchTarget,
                    static_cast<uint32_t>(kWorkerPatchSize),
                    enabled ? 1 : 0,
                    false);
                return false;
            }

            if (!mem::PatchMemory(g_patchTarget, kWorkerPatchEnabled, kWorkerPatchSize))
            {
                core::CrashDiagnostics::Record(
                    core::diag::BreadcrumbKind::PatchState,
                    "patch.worker.job-time",
                    g_patchTarget,
                    static_cast<uint32_t>(kWorkerPatchSize),
                    enabled ? 1 : 0,
                    false);
                return false;
            }

            g_enabled = true;
            core::CrashDiagnostics::Record(
                core::diag::BreadcrumbKind::PatchState,
                "patch.worker.job-time",
                g_patchTarget,
                static_cast<uint32_t>(kWorkerPatchSize),
                enabled ? 1 : 0,
                true);
            LOG_OK("worker: max level and all abilities patch enabled [OK]");
            LOG_DEBUG("worker: max level and all abilities patch enabled @ %p",
                      reinterpret_cast<void*>(g_patchTarget));
            return true;
        }

        if (BytesEqual(current, kWorkerPatchOriginal))
        {
            g_enabled = false;
            return true;
        }

        if (!CanTransitionWorkerPatch(WorkerPatchState::Patched,
                                      WorkerPatchState::Original,
                                      current, kWorkerPatchEnabled,
                                      kWorkerPatchSize))
        {
            core::CrashDiagnostics::Record(
                core::diag::BreadcrumbKind::PatchState,
                "patch.worker.job-time",
                g_patchTarget,
                static_cast<uint32_t>(kWorkerPatchSize),
                enabled ? 1 : 0,
                false);
            return false;
        }

        if (!mem::PatchMemory(g_patchTarget, kWorkerPatchOriginal, kWorkerPatchSize))
        {
            core::CrashDiagnostics::Record(
                core::diag::BreadcrumbKind::PatchState,
                "patch.worker.job-time",
                g_patchTarget,
                static_cast<uint32_t>(kWorkerPatchSize),
                enabled ? 1 : 0,
                false);
            return false;
        }

        g_enabled = false;
        core::CrashDiagnostics::Record(
            core::diag::BreadcrumbKind::PatchState,
            "patch.worker.job-time",
            g_patchTarget,
            static_cast<uint32_t>(kWorkerPatchSize),
            enabled ? 1 : 0,
            true);
        LOG_OK("worker: max level and all abilities patch disabled [OK]");
        LOG_DEBUG("worker: max level and all abilities patch disabled @ %p",
                  reinterpret_cast<void*>(g_patchTarget));
        return true;
    }
}
