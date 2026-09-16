#pragma once

#include <cstdint>
#include <cstring>
#include "../mem/safe_memory.h"

namespace trinity::game
{
    struct EquipTableDesc
    {
        uintptr_t desc = 0;
        uintptr_t array = 0;
        uint32_t count = 0;
        uintptr_t stride = 0xD0;
        uintptr_t tagOffset = 0xC8;
        bool valid = false;
    };

    inline bool IsEquipmentPointer(uintptr_t p)
    {
        return p >= 0x100000000ULL && p <= 0x7FFFFFFFFFFFULL;
    }

    enum class NativeEquipKind { Unknown, Client, Server, Common };

    // MSVC x64 RTTI: a primary vtable's preceding qword is its COL.
    // Keep this separate from the table-shape heuristic: arbitrary arrays of
    // pointers can otherwise masquerade as item values and receive +0x0A writes.
    inline NativeEquipKind ReadEquipVtableKind(uintptr_t vt, uintptr_t image, size_t imageSize)
    {
        if (!image || imageSize < 0x1000) return NativeEquipKind::Unknown;
        auto inImage = [=](uintptr_t p, size_t bytes) {
            return p >= image && bytes <= imageSize && p - image <= imageSize - bytes;
        };
        uintptr_t col = 0;
        uint32_t signature = 0, offset = 0, typeRva = 0, selfRva = 0;
        if (vt < 8 || !inImage(vt - 8, 16) ||
            !mem::ReadPtr(vt - 8, &col) || !inImage(col, 24) ||
            !mem::Read32(col, &signature) || signature != 1 ||
            !mem::Read32(col + 4, &offset) || offset != 0 ||
            !mem::Read32(col + 12, &typeRva) || !mem::Read32(col + 20, &selfRva) ||
            selfRva != col - image || !inImage(image + typeRva, 80))
            return NativeEquipKind::Unknown;
        char name[96] = {};
        if (!mem::ReadCString(image + typeRva + 16, name, sizeof(name))) return NativeEquipKind::Unknown;
        NativeEquipKind kind = NativeEquipKind::Unknown;
        if (std::strcmp(name, ".?AVClientEquipSlotActorComponent@pa@@") == 0)
            kind = NativeEquipKind::Client;
        else if (std::strcmp(name, ".?AVServerEquipSlotActorComponent@pa@@") == 0)
            kind = NativeEquipKind::Server;
        else if (std::strcmp(name, ".?AVCommonEquipSlotActorComponent@pa@@") == 0)
            kind = NativeEquipKind::Common;
        return kind;
    }

    inline bool EquipmentOwnerLinked(uintptr_t comp)
    {
        uintptr_t owner = 0, sub = 0, back = 0;
        return mem::ReadPtr(comp + 8, &owner) && IsEquipmentPointer(owner) &&
            mem::ReadPtr(owner + 0x68, &sub) && IsEquipmentPointer(sub) &&
            mem::ReadPtr(sub + 0x38, &back) && back == comp;
    }

    inline NativeEquipKind ReadNativeEquipKind(uintptr_t comp, uintptr_t image, size_t imageSize)
    {
        uintptr_t vt = 0;
        if (!IsEquipmentPointer(comp) || !mem::ReadPtr(comp, &vt)) return NativeEquipKind::Unknown;
        const NativeEquipKind kind = ReadEquipVtableKind(vt, image, imageSize);
        return kind != NativeEquipKind::Unknown && EquipmentOwnerLinked(comp) ? kind : NativeEquipKind::Unknown;
    }

    inline NativeEquipKind GetNativeEquipKind(uintptr_t comp)
    {
        static const uintptr_t image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        static const uint32_t imageSize = [] {
            uint32_t peOffset = 0, size = 0;
            if (mem::Read32(image + 0x3C, &peOffset) && peOffset <= 0x1000)
                mem::Read32(image + peOffset + 24 + 56, &size);
            return size;
        }();
        uintptr_t vt = 0;
        if (!IsEquipmentPointer(comp) || !mem::ReadPtr(comp, &vt) || vt < image || vt-image >= imageSize)
            return NativeEquipKind::Unknown;
        // Only immutable module RTTI is cached. Heap owners are checked every use.
        struct ClassCache { uintptr_t vt; NativeEquipKind kind; };
        static thread_local ClassCache cache[32]{};
        static thread_local unsigned next = 0;
        NativeEquipKind kind = NativeEquipKind::Unknown;
        bool found = false;
        for (const auto& entry : cache)
            if (entry.vt == vt) { kind = entry.kind; found = true; break; }
        if (!found)
        {
            kind = ReadEquipVtableKind(vt, image, imageSize);
            cache[next++ % 32] = { vt, kind };
        }
        return kind != NativeEquipKind::Unknown && EquipmentOwnerLinked(comp) ? kind : NativeEquipKind::Unknown;
    }

    inline bool IsNativeClientEquip(uintptr_t comp)
    {
        return GetNativeEquipKind(comp) == NativeEquipKind::Client;
    }

    inline EquipTableDesc ReadNativeEquipmentTableLayout(uintptr_t comp)
    {
        EquipTableDesc out{};
        uintptr_t desc = 0, array = 0;
        uint32_t count = 0;
        if (!mem::ReadPtr(comp + 0x90, &desc) || !IsEquipmentPointer(desc) ||
            !mem::ReadPtr(desc + 8, &array) || !IsEquipmentPointer(array) ||
            !mem::Read32(desc + 0x10, &count) || count == 0 || count > 64)
            return out;
        uint32_t tags = 0;
        unsigned items = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uintptr_t entry = array + i * uintptr_t{0xD0};
            uint16_t type = 0, tag = 0;
            if (!mem::Read16(entry + 8, &type)) return out;
            if (!type || type == 0xFFFF) continue;
            if (!mem::Read16(entry + 0xC8, &tag) || tag >= 32 || (tags & (1u << tag))) return out;
            tags |= 1u << tag;
            ++items;
        }
        if (items) out = { desc, array, count, 0xD0, 0xC8, true };
        return out;
    }

    inline EquipTableDesc ReadNativeEquipmentTable(uintptr_t comp)
    {
        if (GetNativeEquipKind(comp) == NativeEquipKind::Unknown) return {};
        // TU 2.02 native dye/equipment paths use precisely +0x90 / 0xD0 / +0xC8.
        // A different plausible field is not another supported native layout.
        return ReadNativeEquipmentTableLayout(comp);
    }

    // Fixed native ownership routes, also used for read-only mount discovery.
    // Never probe arbitrary subcomponent fields for a table-shaped allocation.
    inline uintptr_t NativeEquipmentFromRoot(uintptr_t root)
    {
        if (!IsEquipmentPointer(root)) return 0;
        if (ReadNativeEquipmentTable(root).valid) return root;
        uintptr_t sub = root;
        for (unsigned depth = 0; depth < 3; ++depth)
        {
            uintptr_t comp = 0;
            if (mem::ReadPtr(sub + 0x38, &comp) && ReadNativeEquipmentTable(comp).valid)
                return comp;
            uintptr_t next = 0;
            if (!mem::ReadPtr(sub + 0x68, &next) || !IsEquipmentPointer(next) || next == sub) break;
            sub = next;
        }
        return 0;
    }

    // Identity, profiles and editors must agree on the same table and layout.
    // A plausible descriptor alone is not enough after object reuse on reload.
    inline EquipTableDesc ReadEquipmentTable(uintptr_t comp)
    {
        EquipTableDesc best{};
        if (!IsEquipmentPointer(comp)) return best;
        int bestScore = 0;
        const uintptr_t tableOffsets[] = { 0x90, 0x88, 0x80, 0x50, 0x78, 0x38, 0x40, 0x48, 0x60, 0x70 };
        const uintptr_t strides[] = { 0xD0, 0xC8 };
        for (uintptr_t offset : tableOffsets)
        {
            uintptr_t desc = 0, array = 0;
            uint32_t count = 0;
            if (!mem::ReadPtr(comp + offset, &desc) || !IsEquipmentPointer(desc) ||
                !mem::ReadPtr(desc + kOff_EquipTable_Array, &array) || !IsEquipmentPointer(array) ||
                !mem::Read32(desc + kOff_EquipTable_Count, &count) || count == 0 || count > 64)
                continue;

            for (uintptr_t stride : strides)
            {
                const uintptr_t tagOffset = stride - 8;
                int items = 0;
                uint32_t tagMask = 0;
                bool valid = true;
                for (uint32_t i = 0; i < count; ++i)
                {
                    const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
                    uint16_t type = 0, tag = 0;
                    if (!mem::Read16(entry + kOff_InvSlot_TypeId, &type))
                    {
                        valid = false;
                        break;
                    }
                    if (type == 0 || type == kInvSlot_EmptyType) continue;
                    if (!mem::Read16(entry + tagOffset, &tag) || tag >= 32)
                    {
                        valid = false;
                        break;
                    }
                    ++items;
                    tagMask |= 1u << tag;
                }
                int distinct = 0;
                for (uint32_t mask = tagMask; mask; mask &= mask - 1) ++distinct;
                const int score = distinct * 100 + items;
                if (valid && items && score > bestScore)
                {
                    bestScore = score;
                    best = { desc, array, count, stride, tagOffset, true };
                }
            }
        }
        return best;
    }
}
