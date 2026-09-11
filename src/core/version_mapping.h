#pragma once

#include <cstdint>

namespace trinity::core
{
    // Returns the modern Crimson Desert title-update label for a confirmed PE
    // revision, or nullptr when the build is not explicitly recognised.
    const char* ModernTitleUpdateForRevision(uint16_t revision);

    // The native inventory transaction and continuous stat-pin contracts are
    // verified only for these explicit post-2.00 revisions. Do not infer this
    // for a newer PE revision without fresh live evidence.
    bool UsesTu201CompatibleRevision(uint16_t revision);

    uintptr_t MoveComponentOwnerOffsetForRevision(uint16_t revision);

    // Broad legacy signatures are a compatibility fallback for older builds.
    // They must never be used for TU 2.01.00 or an unknown newer revision.
    bool MayUseLegacyFuzzySignaturesForRevision(uint16_t revision);

    uintptr_t InventoryCoreGlobalMovOffsetForRevision(uint16_t revision);

    // Per-thread client/server realm selector inside the engine TLS block.
    uintptr_t RealmFlagOffsetForRevision(uint16_t revision);
}
