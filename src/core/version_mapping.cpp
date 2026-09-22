#include "version_mapping.h"

namespace trinity::core
{
    const char* ModernTitleUpdateForRevision(uint16_t revision)
    {
        switch (revision)
        {
        // The game's public TU number was not inferred from the PE resource.
        // Keep the exact executable identity visible until a retail label is
        // independently confirmed.
        case 2949: return "2.03.01";
        case 2944: return "PE 2944";
        case 2850: return "2.02.00";
        case 2760: return "2.01.00";
        case 2692: return "2.00.02";
        case 2658: return "2.00.01";
        case 2625: return "2.00.00";
        default:   return nullptr;
        }
    }

    bool UsesTu201CompatibleRevision(uint16_t revision)
    {
        // PE 2944 and PE 2949 retain the modern item ctor, placement
        // commit/free, and transaction-commit ABI. Their changed holder-insert
        // frame uses a dedicated exact AOB rather than inheriting PE 2850's
        // pattern.
        return revision == 2760 || revision == 2850 || revision == 2944 || revision == 2949;
    }

    bool SlotSizeOverrideSupportedForRevision(uint16_t revision)
    {
        // PE 2949 uses its separately audited four-argument native expansion
        // setter. No InventoryInfo or bucket metadata is patched directly.
        return true;
    }

    uintptr_t MoveComponentOwnerOffsetForRevision(uint16_t revision)
    {
        switch (LocoStepperContractForRevision(revision))
        {
        case LocoStepperContract::Pe2944:
            // PE 2944 live evidence: the installed stepper logged the local
            // move-owner at [component+0x2C0].  +0x2B8 is not that pointer.
            return 0x2C0;
        case LocoStepperContract::Modern:
            return 0x2B8;
        case LocoStepperContract::Legacy:
            return 0x298;
        case LocoStepperContract::Unsupported:
        default:
            return 0;
        }
    }

    LocoStepperContract LocoStepperContractForRevision(uint16_t revision)
    {
        switch (revision)
        {
        case 2944:
        case 2949: return LocoStepperContract::Pe2944;
        case 2760:
        case 2850: return LocoStepperContract::Modern;
        case 2625:
        case 2658:
        case 2692: return LocoStepperContract::Legacy;
        default:   return LocoStepperContract::Unsupported;
        }
    }

    bool MayUseLegacyFuzzySignaturesForRevision(uint16_t revision)
    {
        return !UsesTu201CompatibleRevision(revision) && revision < 2760;
    }

    bool MayProbeLegacyCrimeEventDispatcherForRevision(uint16_t revision)
    {
        return revision <= 2850;
    }

    uintptr_t InventoryCoreGlobalMovOffsetForRevision(uint16_t revision)
    {
        return UsesTu201CompatibleRevision(revision) ? 0 : 0x15;
    }

    uintptr_t RealmFlagOffsetForRevision(uint16_t revision)
    {
        // Per-revision TLS realm flag offsets, confirmed from live binary analysis:
        //   PE 2850 (TU 2.02.00): realm selector reads tls+0x1EC.
        //     Audit (2026-09-11) confirmed this gave server=1 client=1 for Add Item.
        //     0x1FD is a non-boolean byte at that offset — writing it silently
        //     leaves the planner in client-realm, causing err=-771604600 rejections.
        //   PE 2760 (TU 2.01.00): realm selector reads tls+0x1FD.
        //   Pre-TU 2.01: reads tls+0x1F2.
        // PE 2944's live realm selector remains `mov edx, 0x1EC` before the
        // TLS byte read (verified at 0x1420E55A4 on 2026-09-19). PE 2949
        // retains that exact unique selector sequence.
        if (revision == 2850 || revision == 2944 || revision == 2949) return 0x1EC;
        return (revision == 2760) ? 0x1FD : 0x1F2;
    }
}
