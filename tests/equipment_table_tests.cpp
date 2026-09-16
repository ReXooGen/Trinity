#include <cstdio>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <vector>

#include "game/equipment_table.h"
#include "game/equipment_logic.h"
#include "game/mount_equipment.h"
#include "game/stat_view.h"
#include "game/actor_registry.h"

namespace
{
    using namespace trinity::game;

    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Buffer
    {
        unsigned char* data = nullptr;
        size_t size;

        explicit Buffer(size_t bytes) : size(bytes)
        {
            data = reinterpret_cast<unsigned char*>(VirtualAlloc(
                nullptr, size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE));
            Check(data != nullptr, "VirtualAlloc failed");
            if (Address() <= 0x100000000ULL || !IsEquipmentPointer(Address()) ||
                !IsEquipmentPointer(Address() + size - 1))
            {
                VirtualFree(data, 0, MEM_RELEASE);
                data = nullptr;
                throw std::runtime_error("Fixture allocation must be above 4 GB");
            }
        }

        ~Buffer() { if (data) VirtualFree(data, 0, MEM_RELEASE); }
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        uintptr_t Address() const { return reinterpret_cast<uintptr_t>(data); }

        template<typename T>
        void Put(size_t offset, T value)
        {
            Check(offset <= size && sizeof(value) <= size - offset,
                  "Fixture write out of bounds");
            std::memcpy(data + offset, &value, sizeof(value));
        }

        DWORD Protect(DWORD protection)
        {
            DWORD previous = 0;
            Check(VirtualProtect(data, size, protection, &previous) != FALSE,
                  "VirtualProtect failed");
            return previous;
        }
    };

    EquipTableDesc ReadUnchanged(uintptr_t component,
                                 std::initializer_list<const Buffer*> buffers,
                                 Buffer* unreadable = nullptr)
    {
        std::vector<std::vector<unsigned char>> before;
        for (const Buffer* buffer : buffers)
            before.emplace_back(buffer->data, buffer->data + buffer->size);

        const DWORD previous = unreadable ? unreadable->Protect(PAGE_NOACCESS) : 0;
        const EquipTableDesc result = ReadEquipmentTable(component);
        if (unreadable) unreadable->Protect(previous);

        size_t index = 0;
        for (const Buffer* buffer : buffers)
        {
            Check(std::memcmp(buffer->data, before[index].data(), buffer->size) == 0,
                  "ReadEquipmentTable mutated an input buffer (including A0/A1)");
            ++index;
        }
        return result;
    }

    struct Table
    {
        Buffer component{0x100};
        Buffer descriptor{0x20};
        Buffer entries{64 * 0xD0};
        uintptr_t stride;
        uint32_t count;

        explicit Table(uintptr_t layout, uint32_t slots = 4) : stride(layout), count(slots)
        {
            Check(stride == 0xD0 || stride == 0xC8, "Unknown fixture layout");
            component.Put(0x90, descriptor.Address());
            descriptor.Put(kOff_EquipTable_Array, entries.Address());
            descriptor.Put(kOff_EquipTable_Count, count);
            component.Put(0xA0, uint8_t{0xA5});
            component.Put(0xA1, uint8_t{0x5A});
            for (uint32_t i = 0; i < 64; ++i)
            {
                const uintptr_t entry = i * stride;
                entries.Put(entry + kOff_InvSlot_TypeId,
                            i % 2 ? kInvSlot_EmptyType : uint16_t{0});
                entries.Put(entry + stride - 8, uint16_t{32});
                entries.Put(entry + 0xA0, uint8_t{0xA5});
                entries.Put(entry + 0xA1, uint8_t{0x5A});
            }

            // Reject the other layout even when slot zero is the only item.
            const uintptr_t otherStride = stride == 0xD0 ? 0xC8 : 0xD0;
            entries.Put(otherStride - 8, uint16_t{32});
            // For sparse tables, poison a false-layout occupied slot without
            // touching any real type, tag, or A0/A1 field in the intended layout.
            const uintptr_t falseEntry = (stride == 0xD0 ? 1 : 2) * otherStride;
            entries.Put(falseEntry + kOff_InvSlot_TypeId, uint16_t{123});
            entries.Put(falseEntry + otherStride - 8, uint16_t{32});
        }

        void Item(uint32_t index, uint16_t tag)
        {
            Check(index < 64, "Fixture item index out of bounds");
            entries.Put(index * stride + kOff_InvSlot_TypeId, uint16_t{123});
            entries.Put(index * stride + stride - 8, tag);
        }

        EquipTableDesc Read(Buffer* unreadable = nullptr) const
        {
            return ReadUnchanged(component.Address(),
                                 {&component, &descriptor, &entries}, unreadable);
        }
    };

    void CheckTable(const EquipTableDesc& actual, const Table& expected)
    {
        Check(actual.valid, "Expected a valid equipment table");
        Check(actual.desc == expected.descriptor.Address(), "Wrong descriptor selected");
        Check(actual.array == expected.entries.Address(), "Wrong entry array selected");
        Check(actual.count == expected.count, "Wrong table count");
        Check(actual.stride == expected.stride, "Wrong layout stride selected");
        Check(actual.tagOffset == expected.stride - 8, "Wrong tag offset selected");
    }

    void CheckInvalid(const EquipTableDesc& actual)
    {
        Check(!actual.valid, "Invalid table was accepted");
        Check(actual.desc == 0 && actual.array == 0 && actual.count == 0,
              "Invalid result exposed a partial descriptor");
    }

    void TestLayout(uintptr_t stride)
    {
        Table table(stride);
        table.Item(0, 0);
        table.Item(1, 7);
        table.Item(3, 31);
        CheckTable(table.Read(), table);
        CheckTable(table.Read(), table);
    }

    void TestSparse(uintptr_t stride)
    {
        Table table(stride, 64);
        CheckInvalid(table.Read());
        table.Item(63, 31);
        CheckTable(table.Read(), table);
    }

    void TestInvalidTags()
    {
        for (uintptr_t stride : {uintptr_t{0xD0}, uintptr_t{0xC8}})
        {
            Table table(stride, 1);
            for (uint16_t tag : {uint16_t{32}, uint16_t{0xFFFF}})
            {
                table.Item(0, tag);
                CheckInvalid(table.Read());
            }

            Table lateInvalid(stride);
            lateInvalid.Item(0, 0);
            lateInvalid.Item(3, 32);
            CheckInvalid(lateInvalid.Read());
        }
    }

    void TestLaterValidDescriptor()
    {
        Table first(0xD0, 1);
        first.Item(0, 32);
        CheckInvalid(first.Read());
        Table later(0xC8, 1);
        later.Item(0, 7);
        first.component.Put(0x88, later.descriptor.Address());
        CheckTable(ReadUnchanged(first.component.Address(),
                   {&first.component, &first.descriptor, &first.entries,
                    &later.component, &later.descriptor, &later.entries}), later);
    }

    void TestBestDescriptor()
    {
        Table first(0xD0, 5);
        Table later(0xC8, 6);
        first.component.Put(0x88, later.descriptor.Address());
        for (uint32_t i = 0; i < first.count; ++i) first.Item(i, 0);
        later.Item(0, 0);
        later.Item(1, 31);
        const auto read = [&]()
        {
            return ReadUnchanged(first.component.Address(),
                                 {&first.component, &first.descriptor, &first.entries,
                                  &later.component, &later.descriptor, &later.entries});
        };

        // Distinct tags dominate item count: 202 beats 105.
        CheckTable(read(), later);
        first.Item(4, 31);
        // With equal diversity, occupied items decide: 205 beats 202.
        CheckTable(read(), first);
        for (uint32_t i = 2; i < later.count; ++i) later.Item(i, 31);
        CheckTable(read(), later); // 206 beats 205, despite the later offset.
        later.entries.Put(5 * later.stride + kOff_InvSlot_TypeId, kInvSlot_EmptyType);
        CheckTable(read(), first); // Equal scores preserve descriptor search order.
    }

    void TestReclaimedAddress()
    {
        for (uintptr_t stride : {uintptr_t{0xD0}, uintptr_t{0xC8}})
        {
            Table table(stride, 1);
            const uintptr_t component = table.component.Address();
            const uintptr_t descriptor = table.descriptor.Address();
            const uintptr_t array = table.entries.Address();
            // Simulate object reuse in-place, without relying on allocator reuse.
            table.Item(0, 32);
            CheckInvalid(table.Read());
            table.Item(0, 31);
            CheckTable(table.Read(), table);
            table.Item(0, 32);
            CheckInvalid(table.Read());
            table.Item(0, 0);
            CheckTable(table.Read(), table);
            Check(table.component.Address() == component &&
                  table.descriptor.Address() == descriptor && table.entries.Address() == array,
                  "Reuse test changed an allocation address");
        }
    }

    void TestUnreadableBuffers()
    {
        for (uintptr_t stride : {uintptr_t{0xD0}, uintptr_t{0xC8}})
        {
            Table table(stride, 1);
            table.Item(0, 7);
            for (Buffer* unreadable : {&table.component, &table.descriptor, &table.entries})
            {
                CheckTable(table.Read(), table);
                CheckInvalid(table.Read(unreadable));
                CheckTable(table.Read(), table);
            }
        }
    }

    void TestCounts()
    {
        for (uintptr_t stride : {uintptr_t{0xD0}, uintptr_t{0xC8}})
        {
            Table table(stride, 1);
            table.Item(0, 7);
            CheckTable(table.Read(), table);
            for (uint32_t count : {uint32_t{0}, uint32_t{65}, uint32_t{0xFFFFFFFF}})
            {
                table.descriptor.Put(kOff_EquipTable_Count, count);
                CheckInvalid(table.Read());
            }
        }
    }

    void TestPointers()
    {
        Check(!IsEquipmentPointer(0), "Null pointer accepted");
        Check(!IsEquipmentPointer(0xFFFFFFFFULL), "Pointer below 4 GB accepted");
        Check(IsEquipmentPointer(0x100000000ULL), "Minimum equipment pointer rejected");
        Check(IsEquipmentPointer(0x7FFFFFFFFFFFULL), "Maximum equipment pointer rejected");
        Check(!IsEquipmentPointer(0x800000000000ULL), "Pointer above user range accepted");
        for (uintptr_t pointer : {uintptr_t{0}, uintptr_t{0xFFFFFFFFULL},
                                  uintptr_t{0x800000000000ULL}})
        {
            CheckInvalid(ReadUnchanged(pointer, {}));
            Table table(0xD0, 1);
            table.Item(0, 7);
            table.component.Put(0x90, pointer);
            CheckInvalid(table.Read());
            table.component.Put(0x90, table.descriptor.Address());
            table.descriptor.Put(kOff_EquipTable_Array, pointer);
            CheckInvalid(table.Read());
        }
    }

    void TestEquipmentLogic()
    {
        Buffer owner(0x100), actor(0x100);
        Check(PreferEquipmentOwner(owner.Address(), actor.Address()) == owner.Address(),
              "Owner must take precedence over actor");
        Check(PreferEquipmentOwner(owner.Address(), 0) == owner.Address(),
              "Owner alone must be retained");
        Check(PreferEquipmentOwner(0, actor.Address()) == actor.Address(),
              "Missing owner must fall back to actor");
        Check(PreferEquipmentOwner(0, 0) == 0, "Missing owner and actor must return null");

        for (int selected = 0; selected < 3; ++selected)
        {
            const int other = (selected + 1) % 3;
            Check(AcceptCharacterComponent(selected, selected, other),
                  "Recognized identity must override party ordering");
            Check(AcceptCharacterComponent(selected, selected, -1),
                  "Recognized identity must not require a party fallback");
            Check(!AcceptCharacterComponent(selected, other, selected),
                  "Wrong recognized identity must not use party fallback");
            // A matching party index represents the caller's trusted root;
            // untrusted candidates carry -1, not the selected character index.
            Check(AcceptCharacterComponent(selected, -1, selected),
                  "Unknown identity from a matching trusted root must be accepted");
            Check(!AcceptCharacterComponent(selected, -1, -1),
                  "Unknown identity without a trusted root must be rejected");
            Check(!AcceptCharacterComponent(selected, -1, other),
                  "Unknown identity from another character root must be rejected");
            Check(!AcceptCharacterComponent(selected, 3, selected),
                  "Out-of-range identity must not use party fallback");
            Check(!AcceptCharacterComponent(selected, -1, 3),
                  "Out-of-range party index must be rejected");
        }
        for (int selected : {-1, 3})
        {
            Check(!AcceptCharacterComponent(selected, selected, selected),
                  "Invalid selected index accepted through matching identity");
            Check(!AcceptCharacterComponent(selected, -1, selected),
                  "Invalid selected index accepted through fallback");
        }
    }

    void TestNativeProvenance()
    {
        Buffer image(0x2000), owner(0x100), sub(0x100);
        Table table(0xD0, 1);
        table.Item(0, 4);
        // This shape fooled the old validator; a random engine object must
        // never become writable equipment solely because its bytes fit.
        Check(table.Read().valid, "Counterfeit fixture must pass the old shape heuristic");
        Check(ReadNativeEquipKind(table.component.Address(), image.Address(), image.size) == NativeEquipKind::Unknown,
              "Table shape alone was accepted as native equipment");
        Check(!ReadNativeEquipmentTable(table.component.Address()).valid,
              "Production native reader accepted counterfeit component");
        constexpr size_t col = 0x100, vtable = 0x200, type = 0x400;
        image.Put(col, uint32_t{1});
        image.Put(col + 4, uint32_t{0});
        image.Put(col + 12, uint32_t{type});
        image.Put(col + 20, uint32_t{col});
        image.Put(vtable - 8, image.Address() + col);
        table.component.Put(0, image.Address() + vtable);
        table.component.Put(8, owner.Address());
        owner.Put(0x68, sub.Address());
        sub.Put(0x38, table.component.Address());
        const auto kind = [&] { return ReadNativeEquipKind(table.component.Address(), image.Address(), image.size); };
        const struct { const char* name; NativeEquipKind kind; } names[] = {
            {".?AVClientEquipSlotActorComponent@pa@@", NativeEquipKind::Client},
            {".?AVServerEquipSlotActorComponent@pa@@", NativeEquipKind::Server},
            {".?AVCommonEquipSlotActorComponent@pa@@", NativeEquipKind::Common},
            {".?AVPoolServerEquipSlotActorComponent@pa@@", NativeEquipKind::Unknown},
            {".?AVUIGamePlayControlChallengeDescription@uiCommonScript@pa@@", NativeEquipKind::Unknown}
        };
        for (const auto& name : names)
        {
            std::memset(image.data + type + 16, 0, 96);
            std::memcpy(image.data + type + 16, name.name, std::strlen(name.name) + 1);
            Check(kind() == name.kind, "Exact RTTI component classification failed");
        }
        std::memset(image.data + type + 16, 0, 96);
        const char* client = ".?AVClientEquipSlotActorComponent@pa@@";
        std::memcpy(image.data + type + 16, client, std::strlen(client) + 1);
        sub.Put(0x38, uintptr_t{0});
        Check(kind() == NativeEquipKind::Unknown, "Stale owner/component backlink accepted");
        sub.Put(0x38, table.component.Address());
        image.Put(col + 4, uint32_t{8});
        Check(kind() == NativeEquipKind::Unknown, "Secondary subobject accepted as primary component");
        image.Put(col + 4, uint32_t{0});
        image.Put(col + 20, uint32_t{col + 4});
        Check(kind() == NativeEquipKind::Unknown, "Invalid COL self reference accepted");
    }

    void TestNativeLayout()
    {
        Table table(0xD0, 2);
        table.Item(0, 4);
        table.Item(1, 5);
        CheckTable(ReadNativeEquipmentTableLayout(table.component.Address()), table);
        table.Item(1, 4);
        CheckInvalid(ReadNativeEquipmentTableLayout(table.component.Address()));
        table.Item(1, 5);
        table.component.Put(0x88, table.descriptor.Address());
        table.component.Put(0x90, uintptr_t{0});
        Check(table.Read().valid, "Heuristic alternate descriptor fixture invalid");
        CheckInvalid(ReadNativeEquipmentTableLayout(table.component.Address()));
        Table other(0xC8, 2);
        other.Item(0, 4);
        other.Item(1, 5);
        CheckInvalid(ReadNativeEquipmentTableLayout(other.component.Address()));
    }

    void TestMountLocalSlots()
    {
        Buffer owner(0x100), type(0x100);
        owner.Put(0x88, type.Address());
        owner.Put(0x50, uint32_t{6}); // live mount entity ID, not the old fixed 5
        type.Put(1, uint8_t{5});
        Check(IsNativeMountOwner(owner.Address()), "Tag-5 mount with entity ID 6 rejected");
        owner.Put(0x50, uint32_t{2872});
        Check(IsNativeMountOwner(owner.Address()), "Mount identity incorrectly depends on entity ID");
        type.Put(1, uint8_t{1});
        owner.Put(0x50, uint32_t{5});
        Check(!IsNativeMountOwner(owner.Address()), "Player with entity ID 5 mistaken for mount");
        type.Put(1, uint8_t{4});
        Check(!IsNativeMountOwner(owner.Address()), "Companion mistaken for mount");

        Table table(0xD0, 4);
        const uint16_t types[] = {5925, 5926, 5927, 5928};
        const int64_t instances[] = {1000366, 1000367, 1000368, 1000369};
        const char* labels[] = {"Chamfron", "Horse Armor", "Saddle", "Stirrups"};
        for (unsigned i = 0; i < 4; ++i)
        {
            table.Item(i, static_cast<uint16_t>(i));
            table.entries.Put(i * 0xD0 + 8, types[i]);
            table.entries.Put(i * 0xD0, instances[i]);
            table.entries.Put(i * 0xD0 + 0x10, int64_t{1});
            Check(std::strcmp(NativeMountSlotName(static_cast<uint16_t>(i)), labels[i]) == 0,
                  "Wrong live mount slot label");
        }
        const auto layout = ReadNativeEquipmentTableLayout(table.component.Address());
        const auto owned = ReadMountGearEntries(layout);
        Check(owned.found && owned.positiveInstances, "Live four-item mount fixture not recognized");
        for (unsigned i = 0; i < 4; ++i) table.entries.Put(i * 0xD0, int64_t{-1});
        const auto ambient = ReadMountGearEntries(layout);
        Check(ambient.found && !ambient.positiveInstances, "Ambient default gear marked positive-owned");
        for (unsigned i = 0; i < 4; ++i) table.entries.Put(i * 0xD0 + 0x10, int64_t{0});
        Check(!ReadMountGearEntries(layout).found, "Zero-quantity mount gear accepted");
        Check(!NativeMountSlotName(14) && !NativeMountSlotName(22) && NativeMountSlotName(4),
              "Player/global slot namespace mixed with mount-local slots");
        // A mount tag still cannot authorize a counterfeit equipment component.
        type.Put(1, uint8_t{5});
        table.component.Put(8, owner.Address());
        Check(!ReadNativeMountGear(table.component.Address()), "Mount owner bypassed native component RTTI");
    }

    void TestStatReload()
    {
        Buffer owner(0x100), actor(0x100), marker(0x100), root(0x100), first(64 * 0x90), second(64 * 0x90);
        owner.Put(0x68, actor.Address()); actor.Put(0x20, marker.Address());
        marker.Put(0x18, root.Address()); root.Put(0, marker.Address());
        root.Put(0x58, first.Address()); root.Put(0x60, uint32_t{20});
        const auto before = ReadStatView(owner.Address());
        Check(before.count == 20 && before.Contains(first.Address() + 12 * 0x90), "Native stat array rejected");
        Check(!before.Contains(first.Address() + 20 * 0x90), "Read past native stat count");
        Check(!before.Contains(first.Address() + 12 * 0x90 + 1), "Misaligned stat accepted");
        root.Put(0x58, second.Address());
        const auto after = ReadStatView(owner.Address());
        Check(!before.Same(after) && !after.Contains(first.Address() + 12 * 0x90) &&
              after.Contains(second.Address() + 12 * 0x90), "Reload retained old stat allocation");
        root.Put(0x60, uint32_t{0}); Check(!ReadStatView(owner.Address()).count, "Empty stat vector accepted");
        root.Put(0x60, uint32_t{257}); Check(!ReadStatView(owner.Address()).count, "Unbounded stat vector accepted");
        root.Put(0x60, uint32_t{20}); root.Put(0, uintptr_t{0});
        Check(!ReadStatView(owner.Address()).count, "Broken status backlink accepted");
    }

    void TestRegistryReload()
    {
        Buffer global(8), world(0x80), registry(0x20), map(0xB0), buckets(2 * 0x100), nodes(16), node(0x20), owner(0x100);
        global.Put(0, world.Address()); world.Put(0x38, registry.Address()); registry.Put(8, map.Address());
        map.Put(0x88, uint32_t{2}); map.Put(0x98, buckets.Address()); map.Put(0xA0, nodes.Address());
        // Odd key belongs to bucket 1. Model exact pair/node and owner binding.
        buckets.Put(0x100, uint32_t{1}); buckets.Put(0x108, uint32_t{0xB0100007}); buckets.Put(0x10C, uint32_t{0});
        nodes.Put(0, node.Address()); node.Put(4, uint32_t{0xB0100007}); node.Put(8, owner.Address());
        const auto view = ReadActorRegistry(global.Address());
        const auto target = ReadRegistryActor(view, 1, 0);
        Check(target.owner == owner.Address() && RegistryActorCurrent(view, target), "Registry binding failed");
        node.Put(8, owner.Address() + 16);
        Check(!RegistryActorCurrent(view, target), "Replaced owner retained");
        node.Put(8, owner.Address()); buckets.Put(0x100, uint32_t{0});
        Check(!RegistryActorCurrent(view, target), "Removed row retained just because memory readable");
        buckets.Put(0x100, uint32_t{1}); node.Put(4, uint32_t{7});
        Check(!ReadRegistryActor(view, 1, 0).owner, "Mismatched native key accepted");
        node.Put(4, uint32_t{0xB0100007}); map.Put(0x98, buckets.Address() + 0x100);
        Check(!view.Same(ReadActorRegistry(global.Address())), "Map reallocation retained old view");
    }
}

int main()
{
    const struct Test { const char* name; void (*run)(); } tests[] = {
        {"D0 layout and pure reads", [] { TestLayout(0xD0); }},
        {"C8 layout and pure reads", [] { TestLayout(0xC8); }},
        {"D0 sparse item at slot 63", [] { TestSparse(0xD0); }},
        {"C8 sparse item at slot 63", [] { TestSparse(0xC8); }},
        {"invalid tags in all layouts", TestInvalidTags},
        {"invalid descriptor before valid 0x88", TestLaterValidDescriptor},
        {"best valid descriptor score", TestBestDescriptor},
        {"reclaimed address revalidation", TestReclaimedAddress},
        {"unreadable buffers fail closed", TestUnreadableBuffers},
        {"descriptor count bounds", TestCounts},
        {"null and invalid pointers", TestPointers},
        {"owner and character selection", TestEquipmentLogic},
        {"native RTTI and owner provenance", TestNativeProvenance},
        {"exact native table layout", TestNativeLayout},
        {"mount local slots and entity identity", TestMountLocalSlots},
        {"native stat bounds and reload", TestStatReload},
        {"client registry row lifetime", TestRegistryReload},
    };

    int failures = 0;
    for (const Test& test : tests)
    {
        try
        {
            test.run();
            std::printf("PASS: %s\n", test.name);
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s: %s\n", test.name, error.what());
        }
        catch (...)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s: unknown exception\n", test.name);
        }
    }
    if (failures) std::fprintf(stderr, "%d equipment test(s) failed\n", failures);
    return failures ? 1 : 0;
}
