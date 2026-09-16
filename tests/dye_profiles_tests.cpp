#include "game/dye_profiles.h"
#include "game/dye_record.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace trinity::game;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct TestFile
{
    std::wstring path;
    TestFile()
    {
        wchar_t cwd[MAX_PATH]{}, name[MAX_PATH]{};
        Check(GetCurrentDirectoryW(MAX_PATH, cwd) != 0, "working directory unavailable");
        Check(GetTempFileNameW(cwd, L"dyp", 0, name) != 0, "test fixture creation failed");
        path = name;
        DeleteFileW(path.c_str());
    }
    ~TestFile() { DeleteFileW(path.c_str()); }
};
static std::vector<uint8_t> Read(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(h != INVALID_HANDLE_VALUE, "fixture read open failed");
    DWORD size = GetFileSize(h, nullptr), read = 0;
    std::vector<uint8_t> bytes(size);
    const bool ok = ReadFile(h, bytes.data(), size, &read, nullptr) && read == size;
    CloseHandle(h); Check(ok, "fixture read failed"); return bytes;
}
static void Write(const std::wstring& path, const std::vector<uint8_t>& bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    Check(h != INVALID_HANDLE_VALUE, "fixture write open failed");
    DWORD written = 0;
    const bool ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(h); Check(ok, "fixture write failed");
}
static DyeProfile Look()
{
    DyeProfile p{};
    p.revision = 2850; p.typeId = 5925; p.mode = 1;
    strcpy_s(p.name, "White & Gold"); strcpy_s(p.itemName, "Chamfron");
    p.mask = (1u << 0) | (1u << 3) | (1u << 11);
    for (unsigned ch : {0u, 3u, 11u})
    {
        auto& r = p.records[ch];
        r[0] = static_cast<uint8_t>(ch + 1); r[3] = 0xF0;
        r[4] = static_cast<uint8_t>(ch + 2); r[5] = 0xAB; // raw material > UI range must survive
        r[6] = static_cast<uint8_t>(ch); r[7] = 255; r[8] = static_cast<uint8_t>(ch * 7);
        r[9] = 19; r[10] = 255; r[11] = 63; r[12] = 7;
        r[13] = 0xDE; r[14] = 0xAD; r[15] = 0xEF; // storage tail not serialized
    }
    return p;
}
static void RoundTrip()
{
    TestFile file;
    DyeProfileStore store;
    Check(store.Load(file.path) && store.List().empty(), "fresh library failed");
    DyeProfile p = Look(); uint32_t id = 0;
    Check(store.Save(p, 0, &id) && id, "save failed");
    DyeProfileStore reload;
    Check(reload.Load(file.path), "round-trip load failed");
    DyeProfile loaded{};
    Check(reload.Get(id, loaded) && loaded.mask == p.mask && loaded.revision == 2850 &&
          loaded.typeId == p.typeId && loaded.mode == p.mode && !strcmp(p.name, loaded.name), "metadata lost");
    for (unsigned ch = 0; ch < 12; ++ch)
    {
        uint8_t rec[16]{}; DyeProfileRecord(loaded, ch, rec);
        if (p.mask & (1u << ch)) Check(SameDyePayload(rec, p.records[ch]), "zone payload lost/normalized");
        else Check(rec[4] == 255 && rec[5] == 255 && rec[6] == ch && !rec[7] && rec[11] == 255,
                   "unsaved zone retained later dye instead of clearing");
        Check(rec[13] == 0 && rec[14] == 0 && rec[15] == 0, "storage padding persisted");
    }
    Check(DyeProfileCompatible(loaded, 2850, p.typeId, 1), "matching equipment rejected");
    Check(!DyeProfileCompatible(loaded, 2851, p.typeId, 1) && !DyeProfileCompatible(loaded, 2850, p.typeId, 0) &&
          !DyeProfileCompatible(loaded, 2850, p.typeId + 1, 1), "incompatible profile allowed");
    const auto original = Read(file.path);
    DyeProfile bad = p; bad.typeId++;
    Check(!reload.Save(bad, id) && Read(file.path) == original, "overwrite changed equipment identity");
    bad = p; bad.records[3][6] = 8;
    Check(!reload.Save(bad, 0), "mismatched channel accepted");
    bad = p; bad.mask = 0x1000;
    Check(!reload.Save(bad, 0), "invalid mask accepted");
    Check(!reload.Rename(id, "  "), "blank name accepted");
    Check(reload.Rename(id, "My restored look") && reload.Get(id, loaded) && !strcmp(loaded.name, "My restored look"), "rename failed");
    p.mask = 0; strcpy_s(p.name, "Natural");
    Check(reload.Save(p, id), "overwrite with natural state failed");
    Check(store.Load(file.path) && store.Get(id, loaded) && loaded.mask == 0, "natural state round-trip failed");
    Check(store.Erase(id) && reload.Load(file.path) && reload.List().empty(), "delete did not persist");
}
static void FailurePreservesFile()
{
    TestFile file; DyeProfileStore store; Check(store.Load(file.path), "load failed");
    auto p = Look(); uint32_t id = 0; Check(store.Save(p, 0, &id), "save failed");
    const auto bytes = Read(file.path);
    HANDLE blocker = CreateFileW(file.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(blocker != INVALID_HANDLE_VALUE, "blocker open failed");
    const bool saved = store.Rename(id, "Must not publish");
    CloseHandle(blocker);
    DyeProfile unchanged{};
    Check(!saved && Read(file.path) == bytes && store.Get(id, unchanged) && !strcmp(unchanged.name, p.name),
          "failed replacement published or destroyed prior data");
    for (unsigned mode = 0; mode < 4; ++mode)
    {
        auto corrupt = bytes;
        if (mode == 0) corrupt[100] ^= 1;
        if (mode == 1) corrupt.resize(corrupt.size() - 1);
        if (mode == 2) corrupt[8] = 255;
        if (mode == 3) corrupt[7] = '9';
        Write(file.path, corrupt);
        DyeProfileStore invalid;
        Check(!invalid.Load(file.path) && !invalid.Save(p, 0) && Read(file.path) == corrupt,
              "invalid/unsupported file replaced by empty library");
    }
}
static void Capacity()
{
    TestFile file; DyeProfileStore store; Check(store.Load(file.path), "load failed");
    auto p = Look(); uint32_t id = 0;
    for (size_t i = 0; i < DyeProfileStore::kLimit; ++i)
        if (!store.Save(p, 0, &id)) throw std::runtime_error("save " + std::to_string(i) + ": " + store.Error());
    const auto bytes = Read(file.path);
    Check(!store.Save(p, 0) && Read(file.path) == bytes, "library limit lost data");
    Check(store.Save(p, id), "overwrite at capacity failed");
    DyeProfileStore reload; Check(reload.Load(file.path) && reload.List().size() == DyeProfileStore::kLimit, "full library reload failed");
}
int main()
{
    try { RoundTrip(); FailurePreservesFile(); Capacity(); puts("Dye profile round-trip, natural-zone restore, CRUD, limits and failed-write preservation passed."); return 0; }
    catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); return 1; }
}
