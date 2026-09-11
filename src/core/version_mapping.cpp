#include "version_mapping.h"

namespace trinity::core
{
    const char* ModernTitleUpdateForRevision(uint16_t revision)
    {
        switch (revision)
        {
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
        return revision == 2760 || revision == 2850;
    }

    uintptr_t MoveComponentOwnerOffsetForRevision(uint16_t revision)
    {
        // PE 2850's live locomotion stepper still loads its move owner from
        // [rcx+0x2B8] before consuming r8's drive-velocity vector.
        return (revision == 2760 || revision == 2850) ? 0x2B8 : 0x298;
    }

    bool MayUseLegacyFuzzySignaturesForRevision(uint16_t revision)
    {
        return !UsesTu201CompatibleRevision(revision) && revision < 2760;
    }

    uintptr_t InventoryCoreGlobalMovOffsetForRevision(uint16_t revision)
    {
        return UsesTu201CompatibleRevision(revision) ? 0 : 0x15;
    }

    uintptr_t RealmFlagOffsetForRevision(uint16_t revision)
    {
        // PE 2850's live realm selector at 0x14084F30D is `mov edx, 0x1fd` followed by
        // `mov rax, gs:[0x58]; mov rcx, [rax]; cmp byte ptr [rdx+rcx], 0`.
        return UsesTu201CompatibleRevision(revision) ? 0x1FD : 0x1F2;
    }
}
