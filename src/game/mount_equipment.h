#pragma once
#include "equipment_table.h"

namespace trinity::game
{
    // TU 2.02 live: both ServerChildOnlyInGameActor and ClientChildOnlyInGameActor
    // for the player's horse have descriptor tag 5, with local equip tags 0..3.
    // Owner+50 is an entity ID (observed 6), NOT a fixed mount/party slot.
    inline bool IsNativeMountOwner(uintptr_t owner)
    {
        uintptr_t type = 0;
        uint8_t tag = 0;
        return IsEquipmentPointer(owner) && mem::ReadPtr(owner + 0x88, &type) &&
            IsEquipmentPointer(type) && mem::Read8(type + 1, &tag) && tag == 5;
    }

    inline const char* NativeMountSlotName(uint16_t tag)
    {
        switch (tag)
        {
        case 0: return "Chamfron";
        case 1: return "Horse Armor";
        case 2: return "Saddle";
        case 3: return "Stirrups";
        case 4: return "Horseshoes";
        default: return nullptr;
        }
    }

    // Table shape/slot numbers alone cannot distinguish a horse from a human.
    inline uintptr_t NativeMountEquipmentFromRoot(uintptr_t root)
    {
        const uintptr_t comp = NativeEquipmentFromRoot(root);
        uintptr_t owner = 0;
        return comp && mem::ReadPtr(comp + 8, &owner) && IsNativeMountOwner(owner) ? comp : 0;
    }

    struct MountGearSummary
    {
        bool found = false;
        bool positiveInstances = false;
    };

    // Read-only interpretation after the caller validates native table provenance.
    inline MountGearSummary ReadMountGearEntries(const EquipTableDesc& table)
    {
        MountGearSummary result{};
        if (!table.valid || !IsEquipmentPointer(table.array) || table.count > 64 ||
            table.stride != 0xD0 || table.tagOffset != 0xC8) return result;
        for (uint32_t i = 0; i < table.count; ++i)
        {
            const uintptr_t entry = table.array + i * table.stride;
            uint16_t type = 0, tag = 0;
            int64_t quantity = 0, instance = 0;
            if (!mem::Read16(entry + 8, &type) || !type || type == 0xFFFF ||
                !mem::Read16(entry + table.tagOffset, &tag) || !NativeMountSlotName(tag) ||
                !mem::Read64(entry + 0x10, &quantity) || quantity <= 0) continue;
            result.found = true;
            if (mem::Read64(entry, &instance) && instance > 0) result.positiveInstances = true;
        }
        return result;
    }

    inline bool ReadNativeMountGear(uintptr_t comp, bool* hasOwnedItems = nullptr)
    {
        if (hasOwnedItems) *hasOwnedItems = false;
        uintptr_t owner = 0;
        if (!mem::ReadPtr(comp + 8, &owner) || !IsNativeMountOwner(owner)) return false;
        const EquipTableDesc table = ReadNativeEquipmentTable(comp);
        const MountGearSummary summary = ReadMountGearEntries(table);
        if (hasOwnedItems) *hasOwnedItems = summary.positiveInstances;
        return summary.found;
    }
}
