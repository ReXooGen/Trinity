#include "crash_diagnostics.h"

#include "state.h"
#include "build_timestamp.h"
#include "version.h"

#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
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
volatile LONG g_crashHandling = 0;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousCrashHandler = nullptr;
diag::Breadcrumb g_crashBreadcrumbs[diag::BreadcrumbRing::kCapacity]{};
char g_crashReport[256 * 1024]{};

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

void ToUtf8(const wchar_t* source, char* destination, std::size_t capacity) noexcept
{
    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    if (!source) return;
    WideCharToMultiByte(CP_UTF8, 0, source, -1, destination,
                        static_cast<int>(capacity), nullptr, nullptr);
    destination[capacity - 1] = '\0';
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

bool InModule(std::uintptr_t address, std::uintptr_t base, std::size_t size) noexcept
{
    return size != 0 && address >= base && address - base < size;
}

const char* ExceptionName(DWORD code) noexcept
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    default: return "UNKNOWN_EXCEPTION";
    }
}

const char* BreadcrumbKindName(diag::BreadcrumbKind kind) noexcept
{
    switch (kind) {
    case diag::BreadcrumbKind::FeatureState: return "FEATURE";
    case diag::BreadcrumbKind::HookState: return "HOOK";
    case diag::BreadcrumbKind::PatchState: return "PATCH";
    case diag::BreadcrumbKind::Identity: return "IDENTITY";
    case diag::BreadcrumbKind::Mutation: return "MUTATION";
    case diag::BreadcrumbKind::Operation: return "OPERATION";
    case diag::BreadcrumbKind::SafetyFailure: return "SAFETY_FAILURE";
    }
    return "UNKNOWN";
}

class FixedText final {
public:
    FixedText(char* data, std::size_t capacity) noexcept : data_(data), capacity_(capacity)
    {
        if (capacity_) data_[0] = '\0';
    }

    void Append(const char* format, ...) noexcept
    {
        if (!format || used_ >= capacity_) return;
        va_list arguments;
        va_start(arguments, format);
        const int result = _vsnprintf_s(data_ + used_, capacity_ - used_, _TRUNCATE,
                                        format, arguments);
        va_end(arguments);
        if (result < 0) {
            used_ = capacity_ ? capacity_ - 1 : 0;
            if (capacity_) data_[used_] = '\0';
        } else {
            used_ += static_cast<std::size_t>(result);
        }
    }

    const char* Data() const noexcept { return data_; }
    DWORD Size() const noexcept
    {
        return used_ > MAXDWORD ? MAXDWORD : static_cast<DWORD>(used_);
    }

private:
    char* data_{};
    std::size_t capacity_{};
    std::size_t used_{};
};

struct CapturedStack {
    std::uintptr_t frames[32]{};
    std::size_t count{};
    bool containsTrinity{};
};

CapturedStack CaptureFaultStack(const CONTEXT& source) noexcept
{
    CapturedStack captured{};
    CONTEXT context = source;
    for (std::size_t i = 0; i < 32 && context.Rip != 0; ++i) {
        captured.frames[captured.count++] = static_cast<std::uintptr_t>(context.Rip);
        if (InModule(context.Rip, g_session.trinityBase, g_session.trinitySize))
            captured.containsTrinity = true;

        const DWORD64 previousRsp = context.Rsp;
        __try {
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (function) {
                PVOID handlerData = nullptr;
                DWORD64 establisherFrame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function,
                                 &context, &handlerData, &establisherFrame, nullptr);
            } else {
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += sizeof(DWORD64);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (context.Rsp <= previousRsp) break;
    }
    return captured;
}

bool BuildCrashPath(const wchar_t* stem,
                    const wchar_t* extension,
                    wchar_t (&path)[MAX_PATH]) noexcept
{
    if (g_session.outputDirectory[0] == L'\0') return false;
    const int result = _snwprintf_s(path, _TRUNCATE, L"%s\\%s%s",
                                    g_session.outputDirectory, stem, extension);
    return result > 0;
}

void DescribeAddress(FixedText& text, std::uintptr_t address) noexcept
{
    if (InModule(address, g_session.trinityBase, g_session.trinitySize)) {
        text.Append("Trinity.asi+0x%llX", static_cast<unsigned long long>(address - g_session.trinityBase));
    } else if (InModule(address, g_session.executableBase, g_session.executableSize)) {
        text.Append("CrimsonDesert.exe+0x%llX", static_cast<unsigned long long>(address - g_session.executableBase));
    } else {
        text.Append("external/unknown@0x%016llX", static_cast<unsigned long long>(address));
    }
}

void WriteCrashBundle(EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    if (!exceptionPointers || !exceptionPointers->ExceptionRecord ||
        !exceptionPointers->ContextRecord || g_session.outputDirectory[0] == L'\0') return;

    SYSTEMTIME systemTime{};
    GetLocalTime(&systemTime);
    const diag::CrashTimestamp timestamp{
        systemTime.wYear, systemTime.wMonth, systemTime.wDay,
        systemTime.wHour, systemTime.wMinute, systemTime.wSecond};
    wchar_t stem[80]{};
    if (!diag::FormatCrashStem(timestamp, g_session.processId, stem, 80)) return;

    wchar_t textPath[MAX_PATH]{};
    wchar_t dumpPath[MAX_PATH]{};
    if (!BuildCrashPath(stem, L".txt", textPath) ||
        !BuildCrashPath(stem, L".dmp", dumpPath)) return;

    HANDLE textFile = CreateFileW(textPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD textCreateError = textFile == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    HANDLE dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD dumpError = dumpFile == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    bool dumpSucceeded = false;
    if (dumpFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = GetCurrentThreadId();
        exceptionInfo.ExceptionPointers = exceptionPointers;
        exceptionInfo.ClientPointers = FALSE;
        dumpSucceeded = MiniDumpWriteDump(GetCurrentProcess(), g_session.processId,
                                          dumpFile, kDiagnosticDumpType,
                                          &exceptionInfo, nullptr, nullptr) != FALSE;
        if (!dumpSucceeded) dumpError = GetLastError();
        FlushFileBuffers(dumpFile);
        CloseHandle(dumpFile);
    }

    const EXCEPTION_RECORD& exception = *exceptionPointers->ExceptionRecord;
    const CONTEXT& context = *exceptionPointers->ContextRecord;
    const std::uintptr_t instructionPointer = static_cast<std::uintptr_t>(context.Rip);
    const std::uintptr_t targetAddress = exception.NumberParameters > 1
        ? static_cast<std::uintptr_t>(exception.ExceptionInformation[1]) : 0;
    const ULONG_PTR accessType = exception.NumberParameters > 0
        ? exception.ExceptionInformation[0] : 0;
    std::uint64_t dropped = 0;
    const std::size_t breadcrumbCount = g_breadcrumbs.Snapshot(
        g_crashBreadcrumbs, diag::BreadcrumbRing::kCapacity, &dropped);
    const unsigned featureIndex = g_activeFeatureSnapshot.load(std::memory_order_acquire);
    const diag::FeatureSnapshot features = LoadFeatureSnapshot(featureIndex);
    const CapturedStack stack = CaptureFaultStack(context);

    diag::AttributionInput attributionInput{};
    attributionInput.instructionPointer = instructionPointer;
    attributionInput.targetAddress = targetAddress;
    attributionInput.trinityBase = g_session.trinityBase;
    attributionInput.trinitySize = g_session.trinitySize;
    attributionInput.faultModuleIsGameOrDriver =
        InModule(instructionPointer, g_session.executableBase, g_session.executableSize);
    attributionInput.stackContainsTrinity = stack.containsTrinity;
    attributionInput.nowMs = GetTickCount64();
    attributionInput.breadcrumbs = g_crashBreadcrumbs;
    attributionInput.breadcrumbCount = breadcrumbCount;
    const diag::Attribution attribution = diag::Classify(attributionInput);

    FixedText report(g_crashReport, sizeof(g_crashReport));
    report.Append("TRINITY TERMINAL CRASH DIAGNOSTICS\r\n\r\n");
    report.Append("[Session]\r\nTimestamp: %04u-%02u-%02u %02u:%02u:%02u\r\n",
                  timestamp.year, timestamp.month, timestamp.day,
                  timestamp.hour, timestamp.minute, timestamp.second);
    report.Append("ProcessId: %lu\r\nThreadId: %lu\r\nUptimeMs: %llu\r\n",
                  g_session.processId, GetCurrentThreadId(),
                  static_cast<unsigned long long>(attributionInput.nowMs - g_session.startedTickMs));
    report.Append("TrinityVersion: %s\r\nBuildTimestamp: %s\r\n",
                  TRINITY_VERSION, g_session.buildTimestamp);
    report.Append("Executable: %s\r\nExecutableBase: 0x%016llX\r\nExecutableSize: 0x%llX\r\nExecutableSHA256: %s\r\n",
                  g_session.executablePathUtf8,
                  static_cast<unsigned long long>(g_session.executableBase),
                  static_cast<unsigned long long>(g_session.executableSize),
                  g_session.executableSha256);
    report.Append("TrinityModule: %s\r\nTrinityBase: 0x%016llX\r\nTrinitySize: 0x%llX\r\nTrinitySHA256: %s\r\n\r\n",
                  g_session.trinityPathUtf8,
                  static_cast<unsigned long long>(g_session.trinityBase),
                  static_cast<unsigned long long>(g_session.trinitySize),
                  g_session.trinitySha256);

    report.Append("[Exception]\r\nCode: 0x%08lX (%s)\r\nFlags: 0x%08lX\r\n",
                  exception.ExceptionCode, ExceptionName(exception.ExceptionCode), exception.ExceptionFlags);
    report.Append("Instruction: ");
    DescribeAddress(report, instructionPointer);
    report.Append("\r\nTargetAddress: 0x%016llX\r\nAccessType: %llu\r\n\r\n",
                  static_cast<unsigned long long>(targetAddress),
                  static_cast<unsigned long long>(accessType));

    report.Append("[Registers]\r\nRAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\r\n",
                  context.Rax, context.Rbx, context.Rcx, context.Rdx);
    report.Append("RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX\r\n",
                  context.Rsi, context.Rdi, context.Rbp, context.Rsp);
    report.Append("R8 =%016llX R9 =%016llX R10=%016llX R11=%016llX\r\n",
                  context.R8, context.R9, context.R10, context.R11);
    report.Append("R12=%016llX R13=%016llX R14=%016llX R15=%016llX\r\n",
                  context.R12, context.R13, context.R14, context.R15);
    report.Append("RIP=%016llX EFLAGS=%08lX\r\n\r\n", context.Rip, context.EFlags);

    report.Append("[Fault Module]\r\n");
    DescribeAddress(report, instructionPointer);
    report.Append("\r\nResolutionLimit: only startup-captured executable and Trinity ranges are named in-process.\r\n\r\n");

    report.Append("[Stack]\r\n");
    for (std::size_t i = 0; i < stack.count; ++i) {
        report.Append("#%02zu ", i);
        DescribeAddress(report, stack.frames[i]);
        report.Append("\r\n");
    }
    if (stack.count == 0) report.Append("unavailable\r\n");
    report.Append("\r\n[Feature Snapshot]\r\nRevision: %llu\r\nEnabledBits: 0x%016llX\r\n",
                  static_cast<unsigned long long>(features.revision),
                  static_cast<unsigned long long>(features.enabledBits));
    report.Append("NoClipSpeedMilli: %ld\r\nSuperRunMilli: %ld\r\nSuperJumpMilli: %ld\r\nSlotSize: %ld\r\n\r\n",
                  features.walkSpeedMilli, features.sprintSpeedMilli,
                  features.jumpHeightMilli, features.slotSize);

    report.Append("[Hook/Patch Snapshot]\r\n");
    bool hookOrPatch = false;
    for (std::size_t i = 0; i < breadcrumbCount; ++i) {
        const diag::Breadcrumb& item = g_crashBreadcrumbs[i];
        if (item.kind != diag::BreadcrumbKind::HookState &&
            item.kind != diag::BreadcrumbKind::PatchState) continue;
        hookOrPatch = true;
        report.Append("%llu %s %s address=0x%016llX size=%lu detail=%lld success=%u repeats=%lu\r\n",
                      static_cast<unsigned long long>(item.sequence),
                      BreadcrumbKindName(item.kind), item.label,
                      static_cast<unsigned long long>(item.address), item.size,
                      static_cast<long long>(item.detail), item.success, item.repeatCount);
    }
    if (!hookOrPatch) report.Append("none captured\r\n");

    report.Append("\r\n[Breadcrumbs]\r\nDroppedContendedWrites: %llu\r\n",
                  static_cast<unsigned long long>(dropped));
    for (std::size_t i = 0; i < breadcrumbCount; ++i) {
        const diag::Breadcrumb& item = g_crashBreadcrumbs[i];
        report.Append("%llu +%llums tid=%lu %s %s address=0x%016llX size=%lu detail=%lld success=%u repeats=%lu\r\n",
                      static_cast<unsigned long long>(item.sequence),
                      static_cast<unsigned long long>(item.tickMs - g_session.startedTickMs),
                      item.threadId, BreadcrumbKindName(item.kind), item.label,
                      static_cast<unsigned long long>(item.address), item.size,
                      static_cast<long long>(item.detail), item.success, item.repeatCount);
    }

    report.Append("\r\n[Dump Result]\r\nFlags: 0x%08X\r\nSuccess: %s\r\nWin32Error: %lu\r\n",
                  static_cast<unsigned>(kDiagnosticDumpType),
                  dumpSucceeded ? "true" : "false", dumpError);
    report.Append("TextCreateError: %lu\r\n\r\n[Attribution]\r\nCategory: %s\r\n",
                  textCreateError, diag::AttributionName(attribution));
    report.Append("Evidence: rip_in_trinity=%u stack_contains_trinity=%u fault_in_game=%u target=0x%016llX\r\n",
                  InModule(instructionPointer, g_session.trinityBase, g_session.trinitySize) ? 1 : 0,
                  stack.containsTrinity ? 1 : 0,
                  attributionInput.faultModuleIsGameOrDriver ? 1 : 0,
                  static_cast<unsigned long long>(targetAddress));
    report.Append("Limitation: GAME_OR_DRIVER means no captured Trinity link; it does not prove innocence.\r\n");

    if (textFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(textFile, report.Data(), report.Size(), &written, nullptr);
        FlushFileBuffers(textFile);
        CloseHandle(textFile);
    } else {
        OutputDebugStringA("Trinity crash diagnostics: text report creation failed.\n");
    }
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    if (InterlockedCompareExchange(&g_crashHandling, 1, 0) != 0) {
        if (g_previousCrashHandler && g_previousCrashHandler != CrashHandler)
            return g_previousCrashHandler(exceptionPointers);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    __try {
        WriteCrashBundle(exceptionPointers);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("Trinity crash diagnostics failed safely.\n");
    }

    if (g_previousCrashHandler && g_previousCrashHandler != CrashHandler)
        return g_previousCrashHandler(exceptionPointers);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallUnhandledFilter(HMODULE module) noexcept
{
    g_module = module;
    g_previousCrashHandler = SetUnhandledExceptionFilter(CrashHandler);
}

bool InitializeSession(HMODULE module, const wchar_t* outputDirectoryOverride) noexcept
{
    g_module = module;
    g_session = {};
    g_session.processId = GetCurrentProcessId();
    g_session.startedTickMs = GetTickCount64();
    HMODULE executable = GetModuleHandleW(nullptr);
    g_session.executableBase = reinterpret_cast<std::uintptr_t>(executable);
    g_session.executableSize = ModuleImageSize(executable);
    g_session.trinityBase = reinterpret_cast<std::uintptr_t>(module);
    g_session.trinitySize = ModuleImageSize(module);
    GetModuleFileNameW(nullptr, g_session.executablePath, MAX_PATH);
    GetModuleFileNameW(module, g_session.trinityPath, MAX_PATH);
    ToUtf8(g_session.executablePath, g_session.executablePathUtf8,
           sizeof(g_session.executablePathUtf8));
    ToUtf8(g_session.trinityPath, g_session.trinityPathUtf8,
           sizeof(g_session.trinityPathUtf8));
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
