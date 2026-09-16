#include "game/inventory_scan.h"
#include "game/equipment_edit_scan.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace trinity::game;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Memory
{
    unsigned char* data;
    explicit Memory(size_t size) : data(static_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE)))
    { Check(data != nullptr, "allocation failed"); }
    ~Memory() { VirtualFree(data, 0, MEM_RELEASE); }
    uintptr_t address() const { return reinterpret_cast<uintptr_t>(data); }
    template<class T> void put(size_t offset, T value) { memcpy(data + offset, &value, sizeof(value)); }
};
int main()
{
    try
    {
        Memory holder(0x100), buckets(16 * 8), bucket(0x100), slots(700 * 0xD0), replacement(700 * 0xD0);
        holder.put(kOff_InvHolder_Buckets, buckets.address());
        holder.put(kOff_InvHolder_Count, uint32_t{16});
        buckets.put(10 * 8, bucket.address());
        bucket.put(kOff_InvBucket_Slots, slots.address());
        bucket.put(kOff_InvBucket_Count, uint16_t{700});
        InventoryScan scan;
        Check(scan.Bind(holder.address(), 0xD0, 1), "bind failed");
        unsigned visited = 0, completeBuckets = 0;
        std::vector<uintptr_t> addresses;
        auto visit = [&](uintptr_t address) { ++visited; addresses.push_back(address); return true; };
        auto end = [&](const InventoryScan&) { ++completeBuckets; return true; };
        Check(scan.Slice(visit, end) == InventoryScan::Result::Pending && visited == 0,
              "empty bucket budget was not bounded");
        Check(scan.Slice(visit, end) == InventoryScan::Result::Pending && visited == 256 && completeBuckets == 0,
              "partial bucket was published / slot budget exceeded");
        Check(scan.Slice(visit, end) == InventoryScan::Result::Pending && visited == 512, "cursor did not resume");
        Check(scan.Slice(visit, end) == InventoryScan::Result::Complete && visited == 700 && completeBuckets == 1,
              "lap did not complete exactly once");
        for (unsigned i = 0; i < addresses.size(); ++i)
            Check(addresses[i] == slots.address() + i * uintptr_t{0xD0}, "duplicate/skipped slot");
        Check(!scan.Bind(holder.address(), 0xD0, 2), "transaction epoch change accepted");
        Check(!scan.Bind(holder.address(), 0xC8, 1), "stride change accepted");
        scan = {};
        Check(scan.Bind(holder.address(), 0xD0, 3), "rebind failed");
        scan.Slice(visit, end, 1, 16);
        bucket.put(kOff_InvBucket_Slots, replacement.address());
        const unsigned before = visited;
        Check(scan.Slice(visit, end) == InventoryScan::Result::Invalid && visited == before,
              "replaced allocation was traversed with a stale row cursor");
        bucket.put(kOff_InvBucket_Slots, slots.address());
        bucket.put(kOff_InvBucket_Count, uint16_t{0});
        Check(scan.Slice(visit, end) == InventoryScan::Result::Invalid, "shrunken bucket accepted");
        scan = {};
        Check(scan.Bind(holder.address(), 0xD0, 4), "empty rebind failed");
        Check(scan.Slice(visit, end, 256, 16) == InventoryScan::Result::Complete, "empty constructed bucket failed");
        // A deadline yielding in the middle of a bucket preserves its cursor,
        // visits each slot once, and cannot publish that bucket prematurely.
        bucket.put(kOff_InvBucket_Count, uint16_t{700});
        scan = {};
        Check(scan.Bind(holder.address(), 0xD0, 5), "timed scan bind failed");
        visited = completeBuckets = 0;
        addresses.clear();
        Check(scan.SliceWhile(visit, end, [&] { return visited < 17; }, 2048, 16) == InventoryScan::Result::Pending,
              "time budget did not yield");
        Check(visited == 17 && scan.row == 17 && completeBuckets == 0, "deadline discarded partial cursor / published partial bucket");
        Check(scan.SliceWhile(visit, end, [] { return true; }, 2048, 16) == InventoryScan::Result::Complete && visited == 700,
              "timed scan did not resume");
        for (unsigned i = 0; i < addresses.size(); ++i)
            Check(addresses[i] == slots.address() + i * uintptr_t{0xD0}, "deadline skipped/duplicated slot");

        // Reproduce the observed 18 buckets x 1460 constructed records per
        // holder. Even with no matching bag copy, both complete laps are needed.
        Memory largeHolder(0x100), largeBuckets(18*8), headers(18*0x100), largeSlots(18*1460*0xC8);
        largeHolder.put(kOff_InvHolder_Buckets, largeBuckets.address());
        largeHolder.put(kOff_InvHolder_Count, uint32_t{18});
        for (unsigned i = 0; i < 18; ++i)
        {
            largeBuckets.put(i*8, headers.address()+i*0x100);
            headers.put(i*0x100+kOff_InvBucket_Slots, largeSlots.address()+i*1460*0xC8);
            headers.put(i*0x100+kOff_InvBucket_Count, uint16_t{1460});
        }
        EquipmentEditScan realms[2];
        unsigned visits[2]{}, pumps = 0;
        while (!realms[0].complete || !realms[1].complete)
        {
            const unsigned realm = pumps++ % 2;
            Check(pumps < 64, "large holder scan did not make bounded progress");
            auto& state = realms[realm];
            Check(state.Bind(largeHolder.address(), 0xC8, 7), "large holder bind failed");
            const unsigned beforeVisit = visits[realm];
            state.Finish(state.cursor.SliceWhile([&](uintptr_t) { ++visits[realm]; return true; },
                [](const InventoryScan&) { return true; }, [] { return true; }, 2048, 8));
            Check(visits[realm]-beforeVisit <= 2048, "large scan exceeded record budget");
        }
        Check(visits[0] == 18*1460 && visits[1] == 18*1460, "large holder scan missed records");
        Check(pumps == 26, "unexpected large-holder slice count");
        realms[0].matches = 2;
        Check(realms[0].Bind(largeHolder.address(), 0xC8, 8), "mutation did not rebind");
        Check(!realms[0].complete && realms[0].matches == 0 && realms[0].cursor.bucketIndex == 0,
              "old completed evidence survived a mutation");
        realms[1].Finish(InventoryScan::Result::Invalid);
        Check(!realms[1].complete && !realms[1].cursor.started, "invalid scan kept completion");
        Check(!realms[0].Bind(0, 0xC8, 8) && !realms[0].complete, "missing holder retained completion");
        printf("Inventory scan bounds, timed yields, churn and large-holder progress passed (%u slices for two 26280-record laps).\n", pumps);
        return 0;
    }
    catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); return 1; }
}
