#pragma once
#include "equipment_table.h"

namespace trinity::game
{
    // Native client registry (same contract used by the dye-ack dispatcher).
    // Lookup is data-only; each positive is bound to its actual map pair/node.
    struct ActorRegistryView
    {
        uintptr_t root = 0, registry = 0, map = 0, buckets = 0, nodes = 0;
        uint32_t bucketCount = 0;
        bool Same(const ActorRegistryView& b) const
        {
            return root == b.root && registry == b.registry && map == b.map &&
                buckets == b.buckets && nodes == b.nodes && bucketCount == b.bucketCount && bucketCount;
        }
    };
    inline ActorRegistryView ReadActorRegistry(uintptr_t global)
    {
        ActorRegistryView v{};
        if (!mem::ReadPtr(global, &v.root) || !IsEquipmentPointer(v.root) ||
            !mem::ReadPtr(v.root + 0x38, &v.registry) || !IsEquipmentPointer(v.registry) ||
            !mem::ReadPtr(v.registry + 8, &v.map) || !IsEquipmentPointer(v.map) ||
            !mem::Read32(v.map + 0x88, &v.bucketCount) || !v.bucketCount || v.bucketCount > 65536 ||
            !mem::ReadPtr(v.map + 0x98, &v.buckets) || !IsEquipmentPointer(v.buckets) ||
            !mem::ReadPtr(v.map + 0xA0, &v.nodes) || !IsEquipmentPointer(v.nodes)) return {};
        return v;
    }
    struct RegistryActor
    {
        uintptr_t pair = 0, node = 0, owner = 0;
        uint32_t key = 0, index = 0;
    };
    inline RegistryActor ReadRegistryActor(const ActorRegistryView& v, uint32_t bucket, uint32_t row)
    {
        RegistryActor a{};
        uint32_t count = 0, key = 0;
        if (!v.bucketCount || bucket >= v.bucketCount || row >= 31) return {};
        const uintptr_t base = v.buckets + uintptr_t{bucket} * 0x100;
        if (!mem::Read32(base, &count) || count > 31 || row >= count) return {};
        a.pair = base + 8 + uintptr_t{row} * 8;
        if (!mem::Read32(a.pair, &a.key) || !a.key || a.key % v.bucketCount != bucket ||
            !mem::Read32(a.pair + 4, &a.index) || a.index >= 65536 ||
            !mem::ReadPtr(v.nodes + uintptr_t{a.index} * 8, &a.node) || !IsEquipmentPointer(a.node) ||
            !mem::Read32(a.node + 4, &key) || key != a.key ||
            !mem::ReadPtr(a.node + 8, &a.owner) || !IsEquipmentPointer(a.owner)) return {};
        return a;
    }
    inline bool RegistryActorCurrent(const ActorRegistryView& v, const RegistryActor& a)
    {
        if (!v.bucketCount || !a.owner) return false;
        const uintptr_t first = v.buckets + uintptr_t{a.key % v.bucketCount} * 0x100 + 8;
        if (a.pair < first || (a.pair - first) % 8 || (a.pair - first) / 8 >= 31) return false;
        const auto now = ReadRegistryActor(v, a.key % v.bucketCount, static_cast<uint32_t>((a.pair - first) / 8));
        return now.owner == a.owner && now.node == a.node && now.key == a.key && now.index == a.index;
    }
}
