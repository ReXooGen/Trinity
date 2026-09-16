#include <cstdio>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <stdexcept>

#include "game/socket_layout.h"

namespace
{
    using namespace trinity::game;

    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Buffer
    {
        unsigned char* data;
        size_t size;

        explicit Buffer(size_t bytes = 0x1000) : size(bytes)
        {
            data = static_cast<unsigned char*>(VirtualAlloc(nullptr, size,
                MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE));
            Check(data != nullptr, "VirtualAlloc failed");
            if (Address() < 0x100000000ULL || Address() + size - 1 > trinity::mem::kMaxPointer)
            {
                VirtualFree(data, 0, MEM_RELEASE);
                data = nullptr;
                throw std::runtime_error("Fixture requires a canonical allocation above 4 GB");
            }
        }
        ~Buffer() { if (data) VirtualFree(data, 0, MEM_RELEASE); }
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        uintptr_t Address(size_t offset = 0) const { return reinterpret_cast<uintptr_t>(data) + offset; }

        template<typename T> void Put(size_t offset, T value)
        {
            Check(offset <= size && sizeof(value) <= size - offset, "Fixture write out of bounds");
            std::memcpy(data + offset, &value, sizeof(value));
        }

        DWORD Protect(DWORD protection, size_t offset = 0, size_t bytes = 0)
        {
            DWORD old = 0;
            Check(VirtualProtect(data + offset, bytes ? bytes : size, protection, &old) != FALSE,
                "VirtualProtect failed");
            return old;
        }
    };

    struct Fixture
    {
        Buffer entry;
        Buffer records;
        uintptr_t offset;
        explicit Fixture(uintptr_t dataOffset = 0x60) : offset(dataOffset)
        {
            entry.Put(offset, records.Address());
            entry.Put(offset + 8, uint32_t{5});
            entry.Put(offset + 12, uint32_t{8});
            entry.Put(offset + 16, uint8_t{2});
            // The old 32-bit unlock read combined these bytes into a huge count.
            entry.Put(offset + 17, uint8_t{0xA5});
            entry.Put(offset + 18, uint8_t{0x5A});
            entry.Put(offset + 19, uint8_t{0xC3});
            for (uint32_t i = 0; i < 8; ++i)
            {
                records.Put(i * 6, uint16_t(i == 1 ? 0 : 100 + i));
                records.Put(i * 6 + 2, uint16_t{937});
                records.Put(i * 6 + 4, uint8_t(i < 2 ? i : 0xFF));
                records.Put(i * 6 + 5, uint8_t(0xA0 + i));
            }
        }
        SocketLayout Read(bool writable = false) const { return ReadSocketLayout(entry.Address(), offset, writable); }
    };

    void TestExplicitLayoutsAndOwnedCopy()
    {
        for (uintptr_t offset : {uintptr_t{0x58}, uintptr_t{0x60}})
        {
            Fixture f(offset);
            unsigned char beforeEntry[0xD0], beforeRecords[48];
            std::memcpy(beforeEntry, f.entry.data, sizeof(beforeEntry));
            std::memcpy(beforeRecords, f.records.data, sizeof(beforeRecords));
            const SocketLayout copy = f.Read();
            Check(copy.valid && copy.size == 5 && copy.capacity == 8 && copy.unlocked == 2,
                "Capacity eight or byte-sized unlock rejected");
            Check(copy.unlockAddress == f.entry.Address(offset + 16), "Wrong unlock address");
            Check(copy.records[4].gear == 104 && copy.records[4].durability == 937 &&
                copy.records[4].index == 0xFF && copy.records[4].padding == 0xA4,
                "Constructed locked record was hidden or decoded incorrectly");
            Check(std::memcmp(beforeEntry, f.entry.data, sizeof(beforeEntry)) == 0 &&
                std::memcmp(beforeRecords, f.records.data, sizeof(beforeRecords)) == 0,
                "Reader mutated native storage");
            f.records.Put(24, uint16_t{555});
            Check(copy.records[4].gear == 104, "Snapshot does not own its record copy");
            f.records.Protect(PAGE_NOACCESS);
            Check(copy.records[4].gear == 104 && !f.Read().valid,
                "Copied records depended on the live allocation");
        }
        Fixture modern;
        Check(!ReadSocketLayout(modern.entry.Address(), 0x50).valid, "Guessed layout accepted");
        // Both explicit descriptors are readable and empty; a reader must not
        // search a neighboring layout for a more interesting nonempty vector.
        modern.entry.Put(0x60, uintptr_t{0});
        modern.entry.Put(0x68, uint32_t{0});
        modern.entry.Put(0x6C, uint32_t{0});
        modern.entry.Put(0x70, uint8_t{0});
        Check(modern.Read().valid && modern.Read().size == 0, "Empty modern layout was rejected");
    }

    void TestReadOnlyAndFreshWriteValidation()
    {
        Fixture f;
        Check(f.Read(true).valid, "Writable fixture rejected");
        f.records.Protect(PAGE_READONLY);
        Check(f.Read().valid && !f.Read(true).valid, "Read-only records require writable pages to display");
        Check(!WriteSocketLayoutRecord(f.entry.Address(), f.offset, 0, 600),
            "Record write used stale permission validation");
        f.records.Protect(PAGE_READWRITE);
        f.entry.Protect(PAGE_READONLY);
        Check(f.Read().valid && !f.Read(true).valid, "Read-only entry handling failed");
        Check(!UnlockSocketLayout(f.entry.Address(), f.offset, 5), "Unlock accepted a read-only entry");
        f.entry.Protect(PAGE_READWRITE);
        f.entry.Put(f.offset + 8, uint32_t{1});
        f.entry.Put(f.offset + 16, uint8_t{1});
        Check(!WriteSocketLayoutRecord(f.entry.Address(), f.offset, 1, 600),
            "Writer reused an old constructed size");
        f.entry.Protect(PAGE_NOACCESS);
        Check(!f.Read().valid, "Unreadable descriptor accepted");
    }

    void TestConstructedRangeOnly()
    {
        Buffer entry(0x2000), records(0x2000);
        const size_t entryStart = 0x1000 - 0x71;
        const size_t recordStart = 0x1000 - 5 * sizeof(NativeSocketRecord);
        entry.Put(entryStart + 0x60, records.Address(recordStart));
        entry.Put(entryStart + 0x68, uint32_t{5});
        entry.Put(entryStart + 0x6C, uint32_t{8});
        entry.Put(entryStart + 0x70, uint8_t{3});
        records.Put(recordStart + 24, uint16_t{700});
        entry.Protect(PAGE_NOACCESS, 0x1000, 0x1000);
        records.Protect(PAGE_NOACCESS, 0x1000, 0x1000);
        const SocketLayout layout = ReadSocketLayout(entry.Address(entryStart), 0x60);
        Check(layout.valid && layout.records[4].gear == 700,
            "Reader touched unlock padding or unconstructed capacity tail");
        Check(UnlockSocketLayout(entry.Address(entryStart), 0x60, 5),
            "Writer validated or modified bytes beyond constructed records/unlock byte");
        records.Protect(PAGE_NOACCESS, 0, 0x1000);
        Check(!ReadSocketLayout(entry.Address(entryStart), 0x60).valid,
            "Unreadable constructed records accepted");
    }

    void TestBoundsAndEmptyVectors()
    {
        Fixture f;
        for (uint32_t capacity : {uint32_t{0}, uint32_t{4}, uint32_t{1025},
            uint32_t{0x80000008}, uint32_t{0xFFFFFFFF}})
        {
            f.entry.Put(f.offset + 12, capacity);
            Check(!f.Read().valid, "Invalid/unverified flagged capacity accepted");
        }
        for (uint32_t capacity : {uint32_t{5}, uint32_t{8}, uint32_t{1024}})
        {
            f.entry.Put(f.offset + 12, capacity);
            Check(f.Read().valid, "Bounded allocation capacity rejected");
        }
        f.entry.Put(f.offset + 8, uint32_t{6});
        Check(!f.Read().valid, "More than five constructed records accepted");
        f.entry.Put(f.offset + 8, uint32_t{5});
        f.entry.Put(f.offset + 16, uint8_t{6});
        Check(!f.Read().valid, "Unlock count beyond size accepted");
        f.entry.Put(f.offset + 8, uint32_t{0});
        f.entry.Put(f.offset + 16, uint8_t{0});
        f.records.Protect(PAGE_NOACCESS);
        Check(f.Read().valid, "Empty reserved vector read unconstructed storage");
        Check(!UnlockSocketLayout(f.entry.Address(), f.offset, 5), "Empty reserved vector was expanded");
        f.entry.Put(f.offset + 12, uint32_t{0});
        Check(!f.Read().valid, "Non-null pointer with zero capacity accepted");
        f.entry.Put(f.offset, uintptr_t{0});
        Check(f.Read().valid, "Default empty vector rejected");
        Check(!WriteSocketLayoutRecord(f.entry.Address(), f.offset, 0, 100), "Empty vector received a record");
        for (uintptr_t pointer : {uintptr_t{0}, uintptr_t{0xFFFFFFFF}, uintptr_t{0x800000000000ULL}, UINTPTR_MAX})
            Check(!ReadSocketLayout(pointer, 0x60).valid, "Invalid entry pointer accepted");
        f.entry.Put(f.offset + 8, uint32_t{1});
        f.entry.Put(f.offset + 12, uint32_t{8});
        for (uintptr_t pointer : {uintptr_t{0}, uintptr_t{0xFFFFFFFF}, uintptr_t{0x800000000000ULL}})
        {
            f.entry.Put(f.offset, pointer);
            Check(!f.Read().valid, "Invalid record pointer accepted");
        }
    }

    void TestNativeWritesPreserveNeighbors()
    {
        for (uintptr_t offset : {uintptr_t{0x58}, uintptr_t{0x60}})
        {
            Fixture f(offset);
            unsigned char expectedEntry[0xD0], expectedRecords[48];
            std::memcpy(expectedEntry, f.entry.data, sizeof(expectedEntry));
            std::memcpy(expectedRecords, f.records.data, sizeof(expectedRecords));
            Check(!WriteSocketLayoutRecord(f.entry.Address(), offset, -1, 777), "Negative index accepted");
            Check(!WriteSocketLayoutRecord(f.entry.Address(), offset, 2, 777), "Locked index accepted");
            Check(!UnlockSocketLayout(f.entry.Address(), offset, 6), "Invalid unlock request accepted");
            Check(UnlockSocketLayout(f.entry.Address(), offset, 5), "Unlock failed");
            expectedEntry[offset + 16] = 5;
            for (unsigned i = 2; i < 5; ++i) expectedRecords[i * 6 + 4] = static_cast<unsigned char>(i);
            Check(std::memcmp(expectedEntry, f.entry.data, sizeof(expectedEntry)) == 0 &&
                std::memcmp(expectedRecords, f.records.data, sizeof(expectedRecords)) == 0,
                "Unlock changed allocation metadata, durability, padding, or neighbors");

            for (uint16_t gear : {uint16_t{777}, uint16_t{0xFFFF}, uint16_t{0}})
            {
                Check(WriteSocketLayoutRecord(f.entry.Address(), offset, 4, gear), "Record write failed");
                const uint16_t expectedGear = gear ? gear : uint16_t{0xFFFF};
                std::memcpy(expectedRecords + 24, &expectedGear, sizeof(expectedGear));
                Check(std::memcmp(expectedEntry, f.entry.data, sizeof(expectedEntry)) == 0 &&
                    std::memcmp(expectedRecords, f.records.data, sizeof(expectedRecords)) == 0,
                    "Add/clear changed durability, padding, metadata, or capacity tail");
            }
            f.entry.Put(offset + 8, uint32_t{3});
            f.entry.Put(offset + 16, uint8_t{1});
            Check(UnlockSocketLayout(f.entry.Address(), offset, 5) && f.Read().unlocked == 3,
                "Unlock was not limited to constructed size");
        }
    }
}

int main()
{
    const struct Test { const char* name; void (*run)(); } tests[] = {
        {"explicit layouts, byte unlock and owned records", TestExplicitLayoutsAndOwnedCopy},
        {"read-only UI and fresh write validation", TestReadOnlyAndFreshWriteValidation},
        {"constructed range and one-byte boundary", TestConstructedRangeOnly},
        {"bounds, pointers and empty vectors", TestBoundsAndEmptyVectors},
        {"native writes preserve neighboring bytes", TestNativeWritesPreserveNeighbors},
    };
    int failures = 0;
    for (const auto& test : tests)
    {
        try { test.run(); std::printf("PASS: %s\n", test.name); }
        catch (const std::exception& e)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s: %s\n", test.name, e.what());
        }
    }
    return failures ? 1 : 0;
}
