#pragma once
#include <cstdint>
#include <cstring>
#include "../mem/safe_memory.h"

namespace trinity::game
{
    // TU 2.02 native DyeVisualSet 140917122..12E obtains CharacterInfo from
    // owner+68 -> sub+20 -> status+30 (u16 row), NOT from worn item TypeIDs.
    // Native getter 140383820 reads table+8 count and table+58 row pointers.
    // Read resident data only: calling that getter can trigger lazy-load writes.
    inline int ReadNativeCharacterIdentity(uintptr_t owner, uintptr_t tableGlobal)
    {
        using namespace trinity::mem;
        uintptr_t sub = 0, status = 0, back = 0, table = 0, defs = 0;
        uintptr_t def = 0, textObject = 0, text = 0;
        uint16_t row = 0;
        uint32_t count = 0;
        if (!IsValidUserPtr(owner) || !IsValidUserPtr(tableGlobal) ||
            !ReadPtr(owner + 0x68, &sub) || !IsValidUserPtr(sub) ||
            !ReadPtr(sub + 0x20, &status) || !IsValidUserPtr(status) ||
            !ReadPtr(status + 8, &back) || back != owner ||
            !Read16(status + 0x30, &row) ||
            !ReadPtr(tableGlobal, &table) || !IsValidUserPtr(table) ||
            !Read32(table + 8, &count) || !count || count > 65536 || row >= count ||
            !ReadPtr(table + 0x58, &defs) || !IsValidUserPtr(defs) ||
            !ReadPtr(defs + uintptr_t{row} * 8, &def) || !IsValidUserPtr(def) ||
            !ReadPtr(def + 8, &textObject) || !IsValidUserPtr(textObject) ||
            !ReadPtr(textObject, &text) || !IsValidUserPtr(text)) return -1;

        char key[64]{};
        if (!ReadCString(text, key, sizeof(key))) return -1;
        const int identity = std::strcmp(key, "Kliff") == 0 ? 0 :
            std::strcmp(key, "Damian") == 0 ? 1 : std::strcmp(key, "Oongka") == 0 ? 2 : -1;
        if (identity < 0) return -1; // NPCs/clones/unknown never fall back to gear.

        uintptr_t current = 0;
        uint16_t currentRow = 0;
        uint32_t currentCount = 0;
        if (!ReadPtr(owner + 0x68, &current) || current != sub ||
            !ReadPtr(sub + 0x20, &current) || current != status ||
            !ReadPtr(status + 8, &current) || current != owner ||
            !Read16(status + 0x30, &currentRow) || currentRow != row ||
            !ReadPtr(tableGlobal, &current) || current != table ||
            !Read32(table + 8, &currentCount) || currentCount != count ||
            !ReadPtr(table + 0x58, &current) || current != defs ||
            !ReadPtr(defs + uintptr_t{row} * 8, &current) || current != def ||
            !ReadPtr(def + 8, &current) || current != textObject ||
            !ReadPtr(textObject, &current) || current != text) return -1;
        return identity;
    }
}
