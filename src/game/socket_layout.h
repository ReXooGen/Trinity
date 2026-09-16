#pragma once

#include <cstddef>
#include <cstdint>
#include "../mem/safe_memory.h"

namespace trinity::game
{
    // Native TrItemValue constructor 0x1423507B0: vector at entry+0x60,
    // data/+0, size/+8, capacity/+12, then ONE unlock byte at +0x10.
    // Older layouts must be selected explicitly by the caller (+0x58).
    inline constexpr uint32_t kNativeSocketMax = 5;
    inline constexpr uint32_t kNativeSocketCapacityLimit = 1024;
    struct NativeSocketRecord
    {
        uint16_t gear = 0xFFFF;
        uint16_t durability = 0;
        uint8_t index = 0xFF;
        uint8_t padding = 0; // Not a filled/empty state byte.
    };
    static_assert(sizeof(NativeSocketRecord) == 6);
    static_assert(offsetof(NativeSocketRecord, durability) == 2);
    static_assert(offsetof(NativeSocketRecord, index) == 4);
    static_assert(offsetof(NativeSocketRecord, padding) == 5);

    struct SocketLayout
    {
        uintptr_t entry = 0;
        uintptr_t dataOffset = 0;
        uintptr_t data = 0;
        uintptr_t unlockAddress = 0;
        uint32_t size = 0;
        uint32_t capacity = 0;
        uint8_t unlocked = 0;
        NativeSocketRecord records[kNativeSocketMax]{}; // Owned constructed-range copy.
        bool valid = false;
    };

    inline bool SocketDataRange(uintptr_t address, size_t bytes, bool writable = false)
    {
        if (address < 0x100000000ULL || address > mem::kMaxPointer || !bytes ||
            bytes - 1 > mem::kMaxPointer - address)
            return false;
        const uintptr_t end = address + bytes;
        while (address < end)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi) ||
                mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                return false;
            const DWORD access = mbi.Protect & 0xFF;
            const bool canWrite = access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
                access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
            const bool canRead = canWrite || access == PAGE_READONLY || access == PAGE_EXECUTE_READ;
            if (!canRead || (writable && (!canWrite || mbi.Type == MEM_IMAGE))) return false;
            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (base > address || !mbi.RegionSize || mbi.RegionSize > UINTPTR_MAX - base) return false;
            const uintptr_t next = base + mbi.RegionSize;
            if (next <= address) return false;
            address = next < end ? next : end;
        }
        return true;
    }

    // Readability and writeability are deliberately separate. Neither proves
    // ownership: component callers must also retain native RTTI/backlink checks.
    inline bool ValidateSocketLayoutWrite(const SocketLayout& layout)
    {
        return layout.valid && (layout.dataOffset == 0x58 || layout.dataOffset == 0x60) &&
            layout.size <= kNativeSocketMax && layout.size <= layout.capacity &&
            layout.capacity <= kNativeSocketCapacityLimit && layout.unlocked <= layout.size &&
            SocketDataRange(layout.entry, layout.dataOffset + 0x11) &&
            SocketDataRange(layout.entry + layout.dataOffset, 0x11, true) &&
            (!layout.size || SocketDataRange(layout.data,
                layout.size * sizeof(NativeSocketRecord), true));
    }

    inline SocketLayout ReadSocketLayout(uintptr_t entry, uintptr_t dataOffset, bool writable = false)
    {
        SocketLayout out{};
        if (dataOffset != 0x58 && dataOffset != 0x60) return out;
        if (!SocketDataRange(entry, dataOffset + 0x11)) return out;
        out.entry = entry;
        out.dataOffset = dataOffset;
        out.unlockAddress = entry + dataOffset + 0x10;
        if (!mem::ReadPtr(entry + dataOffset, &out.data) ||
            !mem::Read32(entry + dataOffset + 8, &out.size) ||
            !mem::Read32(entry + dataOffset + 12, &out.capacity) ||
            !mem::Read8(out.unlockAddress, &out.unlocked) ||
            out.size > kNativeSocketMax || out.size > out.capacity ||
            out.capacity > kNativeSocketCapacityLimit || out.unlocked > out.size)
            return {};
        // Capacity is allocation metadata, not the number of constructed records.
        // Do not mask hypothetical capacity flags without native evidence.
        if (!out.capacity)
        {
            if (out.data) return {};
        }
        else if (out.data < 0x100000000ULL || out.data > mem::kMaxPointer)
            return {};
        const size_t bytes = out.size * sizeof(NativeSocketRecord);
        if (bytes && (!SocketDataRange(out.data, bytes) || !mem::ReadBytes(out.data, out.records, bytes)))
            return {};

        // Drop a vector that was resized/replaced while its records were copied.
        uintptr_t data = 0;
        uint32_t size = 0, capacity = 0;
        uint8_t unlocked = 0;
        if (!mem::ReadPtr(entry + dataOffset, &data) || data != out.data ||
            !mem::Read32(entry + dataOffset + 8, &size) || size != out.size ||
            !mem::Read32(entry + dataOffset + 12, &capacity) || capacity != out.capacity ||
            !mem::Read8(out.unlockAddress, &unlocked) || unlocked != out.unlocked)
            return {};
        out.valid = true;
        if (writable && !ValidateSocketLayoutWrite(out)) return {};
        return out;
    }

    inline bool WriteSocketLayoutRecord(uintptr_t entry, uintptr_t dataOffset, int index, uint16_t gear)
    {
        const SocketLayout layout = ReadSocketLayout(entry, dataOffset, true);
        if (!layout.valid || index < 0 || static_cast<uint32_t>(index) >= layout.unlocked) return false;
        const uint16_t target = gear ? gear : uint16_t{0xFFFF};
        if (layout.records[index].gear == target && layout.records[index].index == static_cast<uint8_t>(index)) return true;
        const uintptr_t record = layout.data + static_cast<uintptr_t>(index) * sizeof(NativeSocketRecord);
        // No verified source for the new gear's minimum durability yet: preserve
        // +2 instead of inventing 0xFFFF. Always preserve the padding byte at +5.
        if (!mem::Write8(record + offsetof(NativeSocketRecord, index), static_cast<uint8_t>(index)) ||
            !mem::Write16(record + offsetof(NativeSocketRecord, gear), target)) return false;
        const SocketLayout after = ReadSocketLayout(entry, dataOffset);
        return after.valid && after.data == layout.data && after.size == layout.size && after.unlocked == layout.unlocked &&
            after.records[index].gear == target && after.records[index].index == static_cast<uint8_t>(index);
    }

    inline bool UnlockSocketLayout(uintptr_t entry, uintptr_t dataOffset, int requested)
    {
        const SocketLayout layout = ReadSocketLayout(entry, dataOffset, true);
        if (!layout.valid || !layout.size || requested <= 0 || requested > static_cast<int>(kNativeSocketMax))
            return false;
        const uint8_t target = static_cast<uint8_t>(static_cast<uint32_t>(requested) < layout.size
            ? static_cast<uint32_t>(requested) : layout.size);
        if (target <= layout.unlocked) return true;
        for (uint32_t i = layout.unlocked; i < target; ++i)
            if (!mem::Write8(layout.data + i * sizeof(NativeSocketRecord) + offsetof(NativeSocketRecord, index),
                static_cast<uint8_t>(i))) return false;
        return mem::Write8(layout.unlockAddress, target);
    }
}
