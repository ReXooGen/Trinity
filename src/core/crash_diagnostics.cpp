#include "crash_diagnostics.h"

#include "state.h"
#include "build_timestamp.h"

#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace trinity::core::CrashDiagnostics {
namespace {

diag::BreadcrumbRing g_breadcrumbs;
HMODULE g_module = nullptr;
SessionIdentity g_session{};
std::atomic<std::uint64_t> g_featureRevision{0};

struct AtomicFeatureSnapshot {
    std::atomic<std::uint64_t> revision{0};
    std::atomic<std::uint64_t> enabledBits{0};
    std::atomic<std::int32_t> walkSpeedMilli{0};
    std::atomic<std::int32_t> sprintSpeedMilli{0};
    std::atomic<std::int32_t> jumpHeightMilli{0};
    std::atomic<std::int32_t> slotSize{0};
};

AtomicFeatureSnapshot g_featureSnapshots[2];
std::atomic<unsigned> g_activeFeatureSnapshot{0};
thread_local MutationScope* g_activeMutationScope = nullptr;

void CopyWide(wchar_t* destination, std::size_t capacity, const wchar_t* source) noexcept
{
    if (!destination || capacity == 0) return;
    destination[0] = L'\0';
    if (!source) return;
    wcsncpy_s(destination, capacity, source, _TRUNCATE);
}

void CopyNarrow(char* destination, std::size_t capacity, const char* source) noexcept
{
    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    if (!source) return;
    strncpy_s(destination, capacity, source, _TRUNCATE);
}

bool HashFileSha256(const wchar_t* path, char (&hex)[65]) noexcept
{
    CopyNarrow(hex, sizeof(hex), "UNAVAILABLE");
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                                &resultLength, 0) >= 0 &&
              BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                                &resultLength, 0) >= 0 && hashLength == 32;
    std::vector<UCHAR> object(ok ? objectLength : 0);
    std::vector<UCHAR> digest(ok ? hashLength : 0);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                                  nullptr, 0, 0) >= 0;

    UCHAR buffer[64 * 1024]{};
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) break;
        ok = BCryptHashData(hash, buffer, read, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), hashLength, 0) >= 0;
    if (ok) {
        static constexpr char kHex[] = "0123456789ABCDEF";
        for (DWORD i = 0; i < hashLength; ++i) {
            hex[i * 2] = kHex[digest[i] >> 4];
            hex[i * 2 + 1] = kHex[digest[i] & 0x0F];
        }
        hex[64] = '\0';
    }

    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok;
}

std::size_t ModuleImageSize(HMODULE module) noexcept
{
    if (!module) return 0;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void ParentDirectory(const wchar_t* path, wchar_t (&directory)[MAX_PATH]) noexcept
{
    CopyWide(directory, MAX_PATH, path);
    wchar_t* slash = wcsrchr(directory, L'\\');
    if (slash) *slash = L'\0';
}

struct RetentionFile {
    diag::CrashBundleFile diagnostic{};
    wchar_t name[MAX_PATH]{};
};

void PruneOldBundles(const wchar_t* directory) noexcept
{
    wchar_t pattern[MAX_PATH]{};
    _snwprintf_s(pattern, _TRUNCATE, L"%s\\Trinity_Crash_*.*", directory);
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;

    std::vector<RetentionFile> files;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const wchar_t* extension = wcsrchr(data.cFileName, L'.');
        const bool isText = extension && _wcsicmp(extension, L".txt") == 0;
        const bool isDump = extension && _wcsicmp(extension, L".dmp") == 0;
        if (!isText && !isDump) continue;

        const std::size_t stemLength = static_cast<std::size_t>(extension - data.cFileName);
        if (stemLength == 0 || stemLength >= 64) continue;
        RetentionFile file{};
        CopyWide(file.name, MAX_PATH, data.cFileName);
        for (std::size_t i = 0; i < stemLength; ++i) {
            if (data.cFileName[i] > 0x7F) {
                file.diagnostic.stem[0] = '\0';
                break;
            }
            file.diagnostic.stem[i] = static_cast<char>(data.cFileName[i]);
            file.diagnostic.stem[i + 1] = '\0';
        }
        if (file.diagnostic.stem[0] == '\0') continue;
        file.diagnostic.isText = isText;
        files.push_back(file);
    } while (FindNextFileW(find, &data));
    FindClose(find);

    std::vector<diag::CrashBundleFile> diagnosticFiles;
    diagnosticFiles.reserve(files.size());
    for (const RetentionFile& file : files) diagnosticFiles.push_back(file.diagnostic);
    if (files.empty()) return;
    std::unique_ptr<bool[]> flags(new (std::nothrow) bool[files.size()]{});
    if (!flags) return;
    diag::SelectCrashFilesToPrune(diagnosticFiles.data(), diagnosticFiles.size(), 3, flags.get());
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!flags[i]) continue;
        wchar_t path[MAX_PATH]{};
        _snwprintf_s(path, _TRUNCATE, L"%s\\%s", directory, files[i].name);
        DeleteFileW(path);
    }
}

diag::FeatureSnapshot LoadFeatureSnapshot(unsigned index) noexcept
{
    const AtomicFeatureSnapshot& source = g_featureSnapshots[index & 1u];
    diag::FeatureSnapshot snapshot{};
    snapshot.revision = source.revision.load(std::memory_order_acquire);
    snapshot.enabledBits = source.enabledBits.load(std::memory_order_relaxed);
    snapshot.walkSpeedMilli = source.walkSpeedMilli.load(std::memory_order_relaxed);
    snapshot.sprintSpeedMilli = source.sprintSpeedMilli.load(std::memory_order_relaxed);
    snapshot.jumpHeightMilli = source.jumpHeightMilli.load(std::memory_order_relaxed);
    snapshot.slotSize = source.slotSize.load(std::memory_order_relaxed);
    return snapshot;
}

void StoreFeatureSnapshot(unsigned index, const diag::FeatureSnapshot& snapshot) noexcept
{
    AtomicFeatureSnapshot& destination = g_featureSnapshots[index & 1u];
    destination.enabledBits.store(snapshot.enabledBits, std::memory_order_relaxed);
    destination.walkSpeedMilli.store(snapshot.walkSpeedMilli, std::memory_order_relaxed);
    destination.sprintSpeedMilli.store(snapshot.sprintSpeedMilli, std::memory_order_relaxed);
    destination.jumpHeightMilli.store(snapshot.jumpHeightMilli, std::memory_order_relaxed);
    destination.slotSize.store(snapshot.slotSize, std::memory_order_relaxed);
    destination.revision.store(snapshot.revision, std::memory_order_release);
}

}  // namespace

void InstallUnhandledFilter(HMODULE module) noexcept
{
    g_module = module;
}

bool InitializeSession(HMODULE module, const wchar_t* outputDirectoryOverride) noexcept
{
    g_module = module;
    g_session = {};
    g_session.processId = GetCurrentProcessId();
    g_session.startedTickMs = GetTickCount64();
    g_session.trinityBase = reinterpret_cast<std::uintptr_t>(module);
    g_session.trinitySize = ModuleImageSize(module);
    GetModuleFileNameW(nullptr, g_session.executablePath, MAX_PATH);
    GetModuleFileNameW(module, g_session.trinityPath, MAX_PATH);
    if (outputDirectoryOverride && outputDirectoryOverride[0] != L'\0') {
        CopyWide(g_session.outputDirectory, MAX_PATH, outputDirectoryOverride);
        CreateDirectoryW(g_session.outputDirectory, nullptr);
    } else {
        ParentDirectory(g_session.trinityPath, g_session.outputDirectory);
    }
    CopyNarrow(g_session.buildTimestamp, sizeof(g_session.buildTimestamp), TRINITY_BUILD_TIME);
    HashFileSha256(g_session.executablePath, g_session.executableSha256);
    HashFileSha256(g_session.trinityPath, g_session.trinitySha256);
    PruneOldBundles(g_session.outputDirectory);
    return module != nullptr && g_session.outputDirectory[0] != L'\0';
}

void Shutdown() noexcept
{
}

void Record(diag::BreadcrumbKind kind,
            const char* label,
            std::uintptr_t address,
            std::uint32_t size,
            std::int64_t detail,
            bool success) noexcept
{
    diag::BreadcrumbInput input{};
    input.kind = kind;
    input.label = label;
    input.tickMs = GetTickCount64();
    input.threadId = GetCurrentThreadId();
    input.address = address;
    input.size = size;
    input.detail = detail;
    input.success = success;
    g_breadcrumbs.Record(input);
}

void PublishFeatureSnapshot(const State& state) noexcept
{
    const std::uint64_t revision = g_featureRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    const diag::FeatureSnapshot next = diag::BuildFeatureSnapshot(state, revision);
    const unsigned active = g_activeFeatureSnapshot.load(std::memory_order_acquire) & 1u;
    const diag::FeatureSnapshot current = LoadFeatureSnapshot(active);
    if (current.revision != 0 && diag::FeatureSnapshotsEqualIgnoringRevision(current, next)) return;

    const unsigned inactive = active ^ 1u;
    StoreFeatureSnapshot(inactive, next);
    g_activeFeatureSnapshot.store(inactive, std::memory_order_release);
    Record(diag::BreadcrumbKind::FeatureState,
           "feature.snapshot",
           0,
           0,
           static_cast<std::int64_t>(next.enabledBits),
           true);
}

MutationScope::MutationScope(const char* label) noexcept
    : accumulator_(label), previous_(g_activeMutationScope)
{
    g_activeMutationScope = this;
}

MutationScope::~MutationScope() noexcept
{
    if (g_activeMutationScope == this) g_activeMutationScope = previous_;
    const diag::MutationSummary summary = accumulator_.Finish();
    if (summary.writes == 0) return;

    const std::uintptr_t extent = summary.lastAddress >= summary.firstAddress
        ? summary.lastAddress - summary.firstAddress
        : 0;
    const std::uint32_t size = extent > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(extent);
    const std::int64_t detail =
        (static_cast<std::int64_t>(summary.writes) << 32) | summary.failures;
    Record(diag::BreadcrumbKind::Mutation,
           summary.label,
           summary.firstAddress,
           size,
           detail,
           summary.failures == 0);
}

void NoteMemoryWrite(std::uintptr_t address, std::uint32_t size, bool success) noexcept
{
    if (g_activeMutationScope) {
        g_activeMutationScope->accumulator_.Note(address, size, success);
    } else if (!success) {
        Record(diag::BreadcrumbKind::SafetyFailure,
               "memory.write",
               address,
               size,
               0,
               false);
    }
}

}  // namespace trinity::core::CrashDiagnostics
