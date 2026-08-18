#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <MinHook.h>

#include "scanner.h"
#include "../core/logger.h"

namespace trinity::mem
{
    // Finds `sig` (warning if it isn't uniquely matched, up to `maxMatches`
    // probes) and installs a MinHook detour at the resolved address - the
    // find/warn/hook/log sequence every game/*.cpp Install() otherwise
    // repeats by hand. `context` prefixes every log line (e.g.
    // "player: stat-accessor") and `consequence` names what's lost on
    // failure (e.g. "God Mode disabled"), so a call site supplies two
    // feature-specific strings instead of three near-duplicate LOG_* calls.
    //
    // On success, *original is set to the trampoline and *target to the
    // hooked address (truthy - keep it to pass to RemoveHook in Remove());
    // both are left null on failure. Returns whether the hook is installed -
    // callers decide whether a false return is fatal for the whole feature
    // or just one sub-feature.
    template <typename Fn>
    bool InstallHook(const char* context, std::string_view sig, const char* consequence,
                      Fn detour, Fn* original, void** target, size_t maxMatches = 8)
    {
        *target = nullptr;
        const uintptr_t addr = FindPattern(sig);
        if (!addr)
        {
            if (consequence && consequence[0] != '\0')
                LOG_ERR("%s signature NOT FOUND - %s.", context, consequence);
            return false;
        }

        const size_t matches = CountMatches(sig, maxMatches);
        if (matches != 1)
            LOG_WARN("%s signature ambiguous (%zu); hooking first.", context, matches);

        void* t = reinterpret_cast<void*>(addr);
        if (MH_CreateHook(t, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original)) != MH_OK ||
            MH_EnableHook(t) != MH_OK)
        {
            LOG_ERR("%s: failed to install hook - %s.", context, consequence);
            *original = nullptr;
            return false;
        }

        *target = t;
        return true;
    }

    // Disables + removes a hook installed via InstallHook and clears
    // *target, so Remove() can call this unconditionally and stays
    // idempotent (a null target is a no-op).
    inline void RemoveHook(void** target)
    {
        if (!*target) return;
        MH_DisableHook(*target);
        MH_RemoveHook(*target);
        *target = nullptr;
    }
}
