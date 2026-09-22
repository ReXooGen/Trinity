#pragma once

#include "crash_diagnostics_logic.h"

#include <Windows.h>
#include <DbgHelp.h>

namespace trinity { struct State; }

namespace trinity::core::CrashDiagnostics {

constexpr MINIDUMP_TYPE kDiagnosticDumpType = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithThreadInfo |
    MiniDumpWithUnloadedModules |
    MiniDumpWithHandleData |
    MiniDumpWithIndirectlyReferencedMemory |
    MiniDumpWithDataSegs |
    MiniDumpWithProcessThreadData |
    MiniDumpWithFullMemoryInfo |
    MiniDumpIgnoreInaccessibleMemory);

static_assert((static_cast<unsigned>(kDiagnosticDumpType) & MiniDumpWithFullMemory) == 0);
static_assert((static_cast<unsigned>(kDiagnosticDumpType) & MiniDumpWithPrivateReadWriteMemory) == 0);

struct SessionIdentity {
    wchar_t outputDirectory[MAX_PATH]{};
    wchar_t executablePath[MAX_PATH]{};
    wchar_t trinityPath[MAX_PATH]{};
    char executablePathUtf8[MAX_PATH * 3]{};
    char trinityPathUtf8[MAX_PATH * 3]{};
    char executableSha256[65]{};
    char trinitySha256[65]{};
    char buildTimestamp[32]{};
    std::uintptr_t executableBase{};
    std::size_t executableSize{};
    std::uintptr_t trinityBase{};
    std::size_t trinitySize{};
    std::uint64_t startedTickMs{};
    DWORD processId{};
};

void InstallUnhandledFilter(HMODULE module) noexcept;
bool InitializeSession(HMODULE module,
                       const wchar_t* outputDirectoryOverride = nullptr) noexcept;
void Shutdown() noexcept;
void PublishFeatureSnapshot(const State& state) noexcept;
void Record(diag::BreadcrumbKind kind,
            const char* label,
            std::uintptr_t address = 0,
            std::uint32_t size = 0,
            std::int64_t detail = 0,
            bool success = true) noexcept;
void NoteMemoryWrite(std::uintptr_t address,
                     std::uint32_t size,
                     bool success) noexcept;

class MutationScope final {
public:
    explicit MutationScope(const char* label) noexcept;
    ~MutationScope() noexcept;
    MutationScope(const MutationScope&) = delete;
    MutationScope& operator=(const MutationScope&) = delete;

private:
    friend void NoteMemoryWrite(std::uintptr_t, std::uint32_t, bool) noexcept;
    diag::MutationAccumulator accumulator_;
    MutationScope* previous_{};
};

}  // namespace trinity::core::CrashDiagnostics
