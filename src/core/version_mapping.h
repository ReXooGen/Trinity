#pragma once

#include <cstdint>

namespace trinity::core
{
    enum class LocoStepperContract : uint8_t
    {
        Unsupported,
        Legacy,
        Modern,
        Pe2944,
    };

    // Returns a confirmed, user-visible label for a modern PE revision, or
    // nullptr when the build is not explicitly recognised.  A PE-only label is
    // intentional when the retail title-update name has not been verified.
    const char* ModernTitleUpdateForRevision(uint16_t revision);

    // The native inventory transaction and continuous stat-pin contracts are
    // verified only for these explicit post-2.00 revisions. PE 2944 is based
    // on its live AOB/ABI audit; its Add Item path still requires an in-game
    // semantic test before a release claim. Do not infer this for any newer
    // PE revision without fresh evidence.
    bool UsesTu201CompatibleRevision(uint16_t revision);

    // The old Slot Size implementation changes live bucket metadata and
    // InventoryInfo descriptors. PE 2949's sale/discard path crashes after
    // that mutation, so this legacy implementation must fail closed there.
    bool SlotSizeOverrideSupportedForRevision(uint16_t revision);

    uintptr_t MoveComponentOwnerOffsetForRevision(uint16_t revision);

    // Selects only the locomotion ABI observed for this exact executable
    // family.  Newer, unobserved PE revisions must not inherit a hook target.
    LocoStepperContract LocoStepperContractForRevision(uint16_t revision);

    // Broad legacy signatures are a compatibility fallback for older builds.
    // They must never be used for TU 2.01.00 or an unknown newer revision.
    bool MayUseLegacyFuzzySignaturesForRevision(uint16_t revision);

    // The four-argument crime-event dispatcher was verified only through PE
    // 2850. PE 2944 no longer contains that function contract, so probing its
    // old signature would report an expected incompatibility as an error.
    bool MayProbeLegacyCrimeEventDispatcherForRevision(uint16_t revision);

    uintptr_t InventoryCoreGlobalMovOffsetForRevision(uint16_t revision);

    // Per-thread client/server realm selector inside the engine TLS block.
    uintptr_t RealmFlagOffsetForRevision(uint16_t revision);
}
