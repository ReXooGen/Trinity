#include "core/crash_diagnostics.h"
#include "core/crash_diagnostics_logic.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cwchar>

namespace {

void DisableErrorDialogs()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

int RunHandled(const wchar_t* outputDirectory = nullptr)
{
    if (outputDirectory && outputDirectory[0] != L'\0') {
        HMODULE module = GetModuleHandleW(nullptr);
        trinity::core::CrashDiagnostics::InstallUnhandledFilter(module);
        trinity::core::CrashDiagnostics::InitializeSession(module, outputDirectory);
    }
    __try {
        RaiseException(0xE0424242, 0, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 1;
}

// Referenced diagnostic memory to exercise indirect memory capture in dump
static volatile char g_seedDiagnosticMemory[1024 * 1024];

int RunFatal(const wchar_t* outputDirectory)
{
    HMODULE module = GetModuleHandleW(nullptr);
    trinity::core::CrashDiagnostics::InstallUnhandledFilter(module);
    trinity::core::CrashDiagnostics::InitializeSession(module, outputDirectory);
    trinity::core::CrashDiagnostics::Record(
        trinity::core::diag::BreadcrumbKind::Operation,
        "harness.pre-crash");

    for (std::size_t i = 0; i < sizeof(g_seedDiagnosticMemory); i += 4096) {
        g_seedDiagnosticMemory[i] = static_cast<char>(i & 0xFF);
    }

    *reinterpret_cast<volatile std::uint64_t*>(0x1234) = 1;
    return 1;
}

int RunFatalBlockDump(const wchar_t* outputDirectory)
{
    HMODULE module = GetModuleHandleW(nullptr);
    trinity::core::CrashDiagnostics::InstallUnhandledFilter(module);
    trinity::core::CrashDiagnostics::InitializeSession(module, outputDirectory);
    trinity::core::CrashDiagnostics::Record(
        trinity::core::diag::BreadcrumbKind::Operation,
        "harness.pre-crash");

    // Pre-create the expected .dmp file with exclusive access so dump file creation fails
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const WORD startSec = st.wSecond;
    while (st.wSecond == startSec) {
        Sleep(2);
        GetLocalTime(&st);
    }

    const trinity::core::diag::CrashTimestamp timestamp{
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond};
    wchar_t stem[80]{};
    if (trinity::core::diag::FormatCrashStem(timestamp, GetCurrentProcessId(), stem, 80)) {
        wchar_t dumpPath[MAX_PATH]{};
        std::swprintf(dumpPath, MAX_PATH, L"%s\\%s.dmp", outputDirectory, stem);
        HANDLE hDump = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        (void)hDump;
    }

    *reinterpret_cast<volatile std::uint64_t*>(0x1234) = 1;
    return 1;
}

static wchar_t g_downstreamWitnessPath[MAX_PATH]{};

static LONG WINAPI DummyDownstreamFilter(EXCEPTION_POINTERS*)
{
    HANDLE hFile = CreateFileW(g_downstreamWitnessPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hFile, "DOWNSTREAM_CALLED", 17, &written, nullptr);
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int RunChained(const wchar_t* outputDirectory)
{
    HMODULE module = GetModuleHandleW(nullptr);
    trinity::core::CrashDiagnostics::InstallUnhandledFilter(module);
    trinity::core::CrashDiagnostics::InitializeSession(module, outputDirectory);

    std::swprintf(g_downstreamWitnessPath, MAX_PATH, L"%s\\downstream_witness.txt", outputDirectory);

    // Simulate Sentry/game registering a downstream filter AFTER Trinity
    SetUnhandledExceptionFilter(DummyDownstreamFilter);

    *reinterpret_cast<volatile std::uint64_t*>(0x1234) = 1;
    return 1;
}

int RunPruneCheck(const wchar_t* outputDirectory)
{
    HMODULE module = GetModuleHandleW(nullptr);
    trinity::core::CrashDiagnostics::InitializeSession(module, outputDirectory);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t* argv[])
{
    DisableErrorDialogs();
    if (argc < 2) {
        std::fprintf(stderr, "Usage: TrinityCrashDiagnosticsHarness <--handled [dir] | --fatal <dir> | --fatal-block-dump <dir> | --chained <dir> | --prune-check <dir>>\n");
        return 2;
    }

    if (std::wcscmp(argv[1], L"--handled") == 0) {
        return RunHandled(argc >= 3 ? argv[2] : nullptr);
    }
    if (argc >= 3 && std::wcscmp(argv[1], L"--fatal") == 0) {
        return RunFatal(argv[2]);
    }
    if (argc >= 3 && std::wcscmp(argv[1], L"--fatal-block-dump") == 0) {
        return RunFatalBlockDump(argv[2]);
    }
    if (argc >= 3 && std::wcscmp(argv[1], L"--chained") == 0) {
        return RunChained(argv[2]);
    }
    if (argc >= 3 && std::wcscmp(argv[1], L"--prune-check") == 0) {
        return RunPruneCheck(argv[2]);
    }

    std::fprintf(stderr, "Unknown option: %ls\n", argv[1]);
    return 2;
}
