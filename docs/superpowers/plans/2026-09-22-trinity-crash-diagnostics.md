# Trinity Crash Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Replace the minimal fatal-crash reporter with a bounded 50–300 MB diagnostic bundle that can cautiously distinguish direct Trinity faults, suspected upstream Trinity corruption, game/driver faults, and inconclusive failures.

**Architecture:** Keep `dllmain.cpp` limited to filter registration and lifecycle calls. Put deterministic ring-buffer, snapshot, naming, retention, and attribution logic in a testable core, while the Windows runtime owns session identity, terminal-safe text/minidump writing, and process-module capture. Feed it low-volume feature/hook/patch/identity events plus aggregated mutation scopes; never log ordinary first-chance exceptions.

**Tech Stack:** C++17, Win32/DbgHelp, BCrypt SHA-256, CMake/CTest, PowerShell contract tests, MSVC Win64 Release.

**Spec:** `docs/superpowers/specs/2026-09-22-trinity-crash-diagnostics-design.md`

## Execution checkpoint

**Paused:** 2026-09-22 after Task 4 at the user's request to conserve tokens.

**Completed:** Tasks 1-4 in commits `4d073b6`, `e2342f8`, `7f38494`, and `faa43bb`. The Release `Trinity` target builds, and `TrinityCrashReportingContractTests` plus `TrinityCrashDiagnosticsTests` pass 2/2.

**Resume at:** Task 5, Step 1 — add failing instrumentation contract checks. Tasks 5-7 have not been started. Do not repeat Tasks 1-4; consult `.superpowers/sdd/2026-09-22-trinity-crash-diagnostics/progress.md` for rulings and RED→GREEN evidence.

**Known baseline issue:** the pre-existing hard-coded timestamp assertion still leaves `TrinityReadinessTests` failing and remains scheduled for Task 7. No ASI was deployed.

## Global Constraints

- Preserve unrelated dirty-tree changes and stage only diagnostics-owned paths or exact hunks.
- Never restore `AddVectoredExceptionHandler`; only the top-level unhandled filter may emit a fatal bundle.
- Fatal-path code must not allocate, lock, enumerate modules, calculate hashes, prune files, or call the normal logger.
- The ring is fixed at 512 entries; full/private-memory minidump flags are forbidden.
- A game-owned instruction pointer does not exonerate Trinity; attribution must consider recent mutation evidence.
- Build only. Do not overwrite an installed/in-use ASI or launch the game without separate authorization.
- Keep static, build, synthetic-crash, and real-game proof explicitly separate.

## Review Focus

- Re-entrancy and degraded-path behavior inside the unhandled exception filter.
- Publication ordering and data-race safety in the fixed ring.
- Attribution must fail closed to `INCONCLUSIVE` when evidence is insufficient.
- Every high-risk write path must have a stable scope name without per-frame breadcrumb flooding.
- Dump policy must remain bounded and must never include `MiniDumpWithFullMemory` or `MiniDumpWithPrivateReadWriteMemory`.
- Retention must run only during clean startup and preserve the newest three complete or partial bundles.

---

### Task 1: Add the deterministic diagnostics core

**Files:**
- Create: `src/core/crash_diagnostics_logic.h`
- Create: `src/core/crash_diagnostics_logic.cpp`
- Create: `tests/crash_diagnostics_tests.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write failing ring and attribution tests**

Add a standalone test executable covering ordering, 512-entry wraparound, consecutive-event coalescing, dropped contended writes, concurrent snapshots, and all attribution outcomes. The public test surface is:

```cpp
namespace trinity::core::diag {

enum class BreadcrumbKind : std::uint8_t {
    FeatureState,
    HookState,
    PatchState,
    Identity,
    Mutation,
    Operation,
    SafetyFailure,
};

enum class Attribution : std::uint8_t {
    TrinityDirect,
    TrinitySuspected,
    GameOrDriver,
    Inconclusive,
};

struct BreadcrumbInput {
    BreadcrumbKind kind{};
    const char* label{};
    std::uint64_t tickMs{};
    std::uint32_t threadId{};
    std::uintptr_t address{};
    std::uint32_t size{};
    std::int64_t detail{};
    bool success{};
};

struct Breadcrumb {
    std::uint64_t sequence{};
    std::uint64_t tickMs{};
    std::uint32_t threadId{};
    BreadcrumbKind kind{};
    char label[32]{};
    std::uintptr_t address{};
    std::uint32_t size{};
    std::int64_t detail{};
    std::uint32_t repeatCount{};
    std::uint8_t success{};
};

class BreadcrumbRing final {
public:
    static constexpr std::size_t kCapacity = 512;
    bool Record(const BreadcrumbInput& input) noexcept;
    std::size_t Snapshot(Breadcrumb* output,
                         std::size_t outputCapacity,
                         std::uint64_t* dropped) const noexcept;
};

struct AttributionInput {
    std::uintptr_t instructionPointer{};
    std::uintptr_t targetAddress{};
    std::uintptr_t trinityBase{};
    std::size_t trinitySize{};
    bool faultModuleIsGameOrDriver{};
    bool stackContainsTrinity{};
    std::uint64_t nowMs{};
    const Breadcrumb* breadcrumbs{};
    std::size_t breadcrumbCount{};
};

Attribution Classify(const AttributionInput& input) noexcept;
const char* AttributionName(Attribution value) noexcept;

}  // namespace trinity::core::diag
```

The attribution tests must assert these exact rules:

```cpp
CHECK(Classify(directRip) == Attribution::TrinityDirect);
CHECK(Classify(trinityOnStack) == Attribution::TrinitySuspected);
CHECK(Classify(recentOverlappingMutation) == Attribution::TrinitySuspected);
CHECK(Classify(recentSafetyFailure) == Attribution::TrinitySuspected);
CHECK(Classify(cleanGameFault) == Attribution::GameOrDriver);
CHECK(Classify(noEvidence) == Attribution::Inconclusive);
```

- [x] **Step 2: Register and run the failing test**

Add `TrinityCrashDiagnosticsTests` to CMake using `tests/crash_diagnostics_tests.cpp` and `src/core/crash_diagnostics_logic.cpp`, then run:

```powershell
ctest --test-dir build --build-config Release -R TrinityCrashDiagnosticsTests --output-on-failure
```

Expected: failure because the diagnostics core is not implemented.

- [x] **Step 3: Implement the fixed ring and classifier**

Use preallocated slots and `std::atomic_flag` as a non-blocking writer gate. A contended writer increments an atomic dropped counter and returns `false`. Publish a completed entry only after all fields are copied. Snapshot copies only published sequence numbers, sorts by sequence into caller storage, and never waits on the writer.

Coalesce only an identical immediately preceding event within 250 ms. Classify a mutation as suspicious only when it succeeded, overlaps the exception target, and is at most 60 seconds old. Treat a `SafetyFailure` as suspicious only when at most 5 seconds old.

- [x] **Step 4: Run the test to green**

```powershell
cmake --build build --config Release --target TrinityCrashDiagnosticsTests
ctest --test-dir build --build-config Release -R TrinityCrashDiagnosticsTests --output-on-failure
```

- [x] **Step 5: Commit the core**

```powershell
git add src/core/crash_diagnostics_logic.h src/core/crash_diagnostics_logic.cpp tests/crash_diagnostics_tests.cpp
git add -p CMakeLists.txt
git diff --cached --check
git commit -m "test: add crash diagnostics core"
```

### Task 2: Add immutable snapshots and mutation aggregation

**Files:**
- Create: `src/core/crash_diagnostics.h`
- Create: `src/core/crash_diagnostics.cpp`
- Modify: `src/core/crash_diagnostics_logic.h`
- Modify: `src/core/crash_diagnostics_logic.cpp`
- Modify: `tests/crash_diagnostics_tests.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write failing snapshot and mutation tests**

Test stable feature-bit encoding, unchanged-snapshot deduplication, mutation range expansion, write/failure counters, and nested-scope isolation. Use these exact types:

```cpp
struct FeatureSnapshot {
    std::uint64_t revision{};
    std::uint64_t enabledBits{};
    std::int32_t walkSpeedMilli{};
    std::int32_t sprintSpeedMilli{};
    std::int32_t jumpHeightMilli{};
    std::int32_t slotSize{};
};

struct MutationSummary {
    char label[32]{};
    std::uintptr_t firstAddress{};
    std::uintptr_t lastAddress{};
    std::uint32_t writes{};
    std::uint32_t failures{};
};

class MutationAccumulator final {
public:
    explicit MutationAccumulator(const char* label) noexcept;
    void Note(std::uintptr_t address, std::uint32_t size, bool success) noexcept;
    MutationSummary Finish() noexcept;
};
```

- [x] **Step 2: Run the focused test and confirm RED**

```powershell
cmake --build build --config Release --target TrinityCrashDiagnosticsTests
ctest --test-dir build --build-config Release -R TrinityCrashDiagnosticsTests --output-on-failure
```

- [x] **Step 3: Implement the runtime API without fatal-path work yet**

Expose this interface from `crash_diagnostics.h`:

```cpp
namespace trinity::core::CrashDiagnostics {

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
    MutationAccumulator accumulator_;
    MutationScope* previous_{};
};

}  // namespace trinity::core::CrashDiagnostics
```

Maintain two fixed feature-snapshot buffers and atomically publish the active index. `PublishFeatureSnapshot` records a breadcrumb only when encoded values change. `MutationScope` uses a thread-local active pointer; `NoteMemoryWrite` updates only the active scope, and the destructor emits one summary breadcrumb.

- [x] **Step 4: Run focused tests to green**

```powershell
cmake --build build --config Release --target TrinityCrashDiagnosticsTests
ctest --test-dir build --build-config Release -R TrinityCrashDiagnosticsTests --output-on-failure
```

- [x] **Step 5: Commit snapshots and aggregation**

```powershell
git add src/core/crash_diagnostics.h src/core/crash_diagnostics.cpp src/core/crash_diagnostics_logic.h src/core/crash_diagnostics_logic.cpp tests/crash_diagnostics_tests.cpp
git add -p CMakeLists.txt
git diff --cached --check
git commit -m "feat: add diagnostic snapshots and mutation scopes"
```

### Task 3: Add session identity, bounded dump policy, naming, and retention

**Files:**
- Modify: `src/core/crash_diagnostics.h`
- Modify: `src/core/crash_diagnostics.cpp`
- Modify: `src/core/crash_diagnostics_logic.h`
- Modify: `src/core/crash_diagnostics_logic.cpp`
- Modify: `tests/crash_diagnostics_tests.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write failing policy, filename, and retention tests**

Tests must verify:

```cpp
constexpr MINIDUMP_TYPE kDiagnosticDumpType = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithThreadInfo |
    MiniDumpWithUnloadedModules |
    MiniDumpWithHandleData |
    MiniDumpWithIndirectlyReferencedMemory |
    MiniDumpWithDataSegs |
    MiniDumpWithProcessThreadData |
    MiniDumpWithFullMemoryInfo |
    MiniDumpIgnoreInaccessibleMemory);

static_assert((kDiagnosticDumpType & MiniDumpWithFullMemory) == 0);
static_assert((kDiagnosticDumpType & MiniDumpWithPrivateReadWriteMemory) == 0);
```

Also assert the stem format `Trinity_Crash_YYYYMMDD-HHMMSS_PID`, preservation of the newest three stems, deletion of both members of an older pair, and safe handling of a partial `.txt`-only or `.dmp`-only bundle.

- [x] **Step 2: Run focused tests and confirm RED**

```powershell
cmake --build build --config Release --target TrinityCrashDiagnosticsTests
ctest --test-dir build --build-config Release -R TrinityCrashDiagnosticsTests --output-on-failure
```

- [x] **Step 3: Capture immutable session identity during clean startup**

In `InitializeSession`, precompute and store in fixed buffers:

```cpp
struct SessionIdentity {
    wchar_t outputDirectory[MAX_PATH]{};
    wchar_t executablePath[MAX_PATH]{};
    wchar_t trinityPath[MAX_PATH]{};
    char executableSha256[65]{};
    char trinitySha256[65]{};
    char buildTimestamp[32]{};
    std::uintptr_t trinityBase{};
    std::size_t trinitySize{};
    std::uint64_t startedTickMs{};
    DWORD processId{};
};
```

Use BCrypt for SHA-256 and change the existing link line to:

```cmake
target_link_libraries(Trinity PRIVATE imgui minhook d3d12 dxgi dwmapi imm32 xinput9_1_0 version bcrypt)
```

Resolve paths, PE image size, hashes, and build timestamp before the game hooks start. Do not recalculate them inside the exception filter.

- [x] **Step 4: Implement startup-only retention**

Scan only files matching `Trinity_Crash_*.txt` and `Trinity_Crash_*.dmp` in the selected output directory. Group by stem, order by embedded timestamp and PID, retain the newest three groups, and delete older group members. Run this once from `InitializeSession`, never from the filter.

- [x] **Step 5: Run focused tests to green**

```powershell
cmake --build build --config Release --target TrinityCrashDiagnosticsTests
ctest --test-dir build --build-config Release -R TrinityCrashDiagnosticsTests --output-on-failure
```

- [x] **Step 6: Commit policy and session work**

```powershell
git add src/core/crash_diagnostics.h src/core/crash_diagnostics.cpp src/core/crash_diagnostics_logic.h src/core/crash_diagnostics_logic.cpp tests/crash_diagnostics_tests.cpp
git add -p CMakeLists.txt
git diff --cached --check
git commit -m "feat: add bounded crash bundle policy"
```

### Task 4: Move fatal reporting out of `dllmain.cpp`

**Files:**
- Modify: `src/core/crash_diagnostics.cpp`
- Modify: `src/dllmain.cpp`
- Modify: `tests/verify_crash_reporting_contract.ps1`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Strengthen the source contract and confirm RED**

Update the PowerShell contract to require `CrashDiagnostics::InstallUnhandledFilter`, `CrashDiagnostics::InitializeSession`, `kDiagnosticDumpType`, an interlocked recursion guard, and previous-filter chaining. It must reject `AddVectoredExceptionHandler`, `MiniDumpWithFullMemory`, and direct dump/report implementation in `dllmain.cpp`.

```powershell
ctest --test-dir build --build-config Release -R TrinityCrashReportingContractTests --output-on-failure
```

- [x] **Step 2: Implement independent text and dump writers**

The filter must:

1. Enter with `InterlockedCompareExchange`; a recursive entry immediately chains or returns `EXCEPTION_CONTINUE_SEARCH`.
2. Snapshot the preallocated ring and active feature snapshot into static storage.
3. Resolve fault module/RVA and walk stacks with preloaded DbgHelp state; if stack walking is unavailable, still report exception code, address, registers, and breadcrumbs.
4. Attempt `.txt` creation/write/flush independently from `.dmp` creation/write/flush.
5. Record each failure using numeric `GetLastError()` in whichever output remains available.
6. Classify from the captured instruction pointer, target address, Trinity range, stack presence, and breadcrumbs.
7. Chain the previously installed unhandled filter after local capture.

The text report must contain these fixed sections: `Session`, `Exception`, `Registers`, `Fault Module`, `Stack`, `Feature Snapshot`, `Hook/Patch Snapshot`, `Breadcrumbs`, `Dump Result`, and `Attribution`.

- [x] **Step 3: Reduce `dllmain.cpp` to registration and lifecycle**

Use this lifecycle shape:

```cpp
static DWORD WINAPI MainThread(LPVOID parameter) {
    HMODULE module = static_cast<HMODULE>(parameter);
    trinity::core::CrashDiagnostics::InitializeSession(module);
    trinity::core::Mod::Initialize(module);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        trinity::core::CrashDiagnostics::InstallUnhandledFilter(module);
        HANDLE thread = CreateThread(nullptr, 0, MainThread, module, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        trinity::core::CrashDiagnostics::Shutdown();
    }
    return TRUE;
}
```

`InstallUnhandledFilter` may only store the module and register the UEF under loader lock. All hashing, retention, symbol initialization, and directory work belongs to `MainThread` via `InitializeSession`.

- [x] **Step 4: Run contract and unit tests**

```powershell
cmake --build build --config Release --target Trinity TrinityCrashDiagnosticsTests
ctest --test-dir build --build-config Release -R "TrinityCrash(ReportingContract|Diagnostics)Tests" --output-on-failure
```

- [x] **Step 5: Commit the fatal reporter migration**

```powershell
git add src/core/crash_diagnostics.cpp src/dllmain.cpp tests/verify_crash_reporting_contract.ps1
git add -p CMakeLists.txt
git diff --cached --check
git commit -m "feat: write bounded fatal crash bundles"
```

### Task 5: Publish feature, lifecycle, hook, and patch breadcrumbs from the actual game and hook modules

**Files:**
- Modify: `src/core/mod.cpp`
- Modify: `src/core/settings.cpp`
- Modify: `src/gui/menu.cpp`
- Modify: `src/game/player.cpp`
- Modify: `src/game/teleport.cpp`
- Modify: `src/game/inventory.cpp`
- Modify: `src/game/world.cpp`
- Modify: `src/game/equipment.cpp`
- Modify: `src/game/friendly.cpp`
- Modify: `src/game/worker.cpp`
- Modify: `src/hooks/dx12_hook.cpp`
- Modify: `tests/verify_crash_reporting_contract.ps1`

- [x] **Step 1: Add failing instrumentation contract checks**

Require each subsystem installer to call `CrashDiagnostics::Record` with a stable subsystem label and success result. Require `Settings::Load` and menu rendering to call `PublishFeatureSnapshot`. Require worker and inventory patch toggles to emit `PatchState`.

- [x] **Step 2: Run the contract test and confirm RED**

```powershell
ctest --test-dir build --build-config Release -R TrinityCrashReportingContractTests --output-on-failure
```

- [x] **Step 3: Instrument lifecycle and feature snapshots**

Record `mod.initialize.begin`, each subsystem initialization result, `mod.initialize.complete`, and reverse shutdown results. Publish once after settings load and once per menu render; internal bitwise deduplication prevents repeated events when state is unchanged.

Use stable labels from this set:

```text
hook.player
hook.teleport
hook.inventory
hook.world
hook.equipment
hook.friendly
hook.worker
hook.dx12
patch.inventory.pickup-capacity
patch.worker.job-time
```

For hooks, store the target address, overwritten byte count where known, and success. For patches, record queued and completed states, exact target address/size, and success/failure without copying original memory contents into the log.

- [x] **Step 4: Run contract tests and build**

```powershell
cmake --build build --config Release --target Trinity
ctest --test-dir build --build-config Release -R TrinityCrashReportingContractTests --output-on-failure
```

- [x] **Step 5: Commit instrumentation**

```powershell
git add src/core/mod.cpp src/core/settings.cpp src/gui/menu.cpp src/game/player.cpp src/game/teleport.cpp src/game/inventory.cpp src/game/world.cpp src/game/equipment.cpp src/game/friendly.cpp src/game/worker.cpp src/hooks/dx12_hook.cpp tests/verify_crash_reporting_contract.ps1
git diff --cached --check
git commit -m "feat: record feature and hook crash context"
```

### Task 6: Aggregate risky memory mutations at the shared write boundary

**Files:**
- Modify: `src/mem/safe_memory.h`
- Modify: `src/game/player.cpp`
- Modify: `src/game/teleport.cpp`
- Modify: `src/game/inventory.cpp`
- Modify: `src/game/world.cpp`
- Modify: `src/game/equipment.cpp`
- Modify: `src/game/friendly.cpp`
- Modify: `src/game/worker.cpp`
- Modify: `tests/verify_crash_reporting_contract.ps1`

- [x] **Step 1: Add failing memory-instrumentation contract checks**

Require guarded write and patch helpers to call:

```cpp
CrashDiagnostics::NoteMemoryWrite(
    reinterpret_cast<std::uintptr_t>(address),
    static_cast<std::uint32_t>(sizeof(T)),
    success);
```

Require every named high-risk operation below to construct a `MutationScope`.

- [x] **Step 2: Run the contract test and confirm RED**

```powershell
ctest --test-dir build --build-config Release -R TrinityCrashReportingContractTests --output-on-failure
```

- [x] **Step 3: Instrument central safe writes**

Call `NoteMemoryWrite` after each attempted `Write`, `WriteBytes`, and `PatchMemory`, including failures. Reads remain uninstrumented. When no scope is active, record only failed writes as `SafetyFailure`; successful unscoped writes do not generate breadcrumbs.

- [x] **Step 4: Add stable high-level mutation scopes**

Wrap the existing operations, without changing their behavior, using these exact labels:

```text
player.stat-pin
player.movement
teleport.position
teleport.flight
teleport.noclip
inventory.stack-size
inventory.slot-size
inventory.add-item
inventory.quantity
world.time
world.weather
equipment.modify
friendly.trust
worker.job-time
```

Queued actions must emit an `Operation` event when queued and another when completed. Per-frame scopes may emit at most one aggregate mutation event, and existing ring deduplication must collapse unchanged adjacent summaries.

- [x] **Step 5: Run unit, contract, and build checks**

```powershell
cmake --build build --config Release --target Trinity TrinityCrashDiagnosticsTests
ctest --test-dir build --build-config Release -R "TrinityCrash(ReportingContract|Diagnostics)Tests" --output-on-failure
```

- [x] **Step 6: Commit mutation instrumentation**

```powershell
git add src/mem/safe_memory.h src/game/player.cpp src/game/teleport.cpp src/game/inventory.cpp src/game/world.cpp src/game/equipment.cpp src/game/friendly.cpp src/game/worker.cpp tests/verify_crash_reporting_contract.ps1
git diff --cached --check
git commit -m "feat: aggregate risky memory mutations"
```

### Task 7: Add a controlled crash harness and complete verification

**Files:**
- Create: `tests/crash_diagnostics_harness.cpp`
- Create: `tests/verify_crash_diagnostics_harness.ps1`
- Modify: `tests/readiness_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-09-22-trinity-crash-diagnostics-design.md`

- [x] **Step 1: Write the failing harness verifier**

The PowerShell test must create a unique temporary directory, run a handled-exception mode, assert that no bundle exists, then run a fatal-child mode and assert:

- the child exits nonzero;
- exactly one timestamp-matched `.txt`/`.dmp` pair exists;
- the text includes all required sections and a pre-crash marker;
- the dump size is between 50 MB and 300 MB;
- a second forced terminal write failure still leaves whichever member can be created;
- startup retention leaves exactly three newest stems after five child runs.

Always remove only the verified test directory in `finally`.

- [x] **Step 2: Add the harness executable**

Implement these modes:

```cpp
int RunHandled() {
    __try {
        RaiseException(0xE0424242, 0, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int RunFatal(const wchar_t* outputDirectory) {
    HMODULE module = GetModuleHandleW(nullptr);
    trinity::core::CrashDiagnostics::InstallUnhandledFilter(module);
    trinity::core::CrashDiagnostics::InitializeSession(module, outputDirectory);
    trinity::core::CrashDiagnostics::Record(
        trinity::core::diag::BreadcrumbKind::Operation,
        "harness.pre-crash");
    *reinterpret_cast<volatile std::uint64_t*>(0x1234) = 1;
    return 1;
}
```

Build `TrinityCrashDiagnosticsHarness` from the harness, diagnostics sources, and the same DbgHelp/BCrypt dependencies. Register only the parent PowerShell verifier with CTest so the expected child crash is interpreted correctly.

- [x] **Step 3: Fix the unrelated hard-coded readiness timestamp assertion**

Change the literal timestamp assertion in `readiness_tests.cpp` to validate that `TRINITY_BUILD_TIMESTAMP` is non-empty and matches `YYYY-MM-DD HH:MM:SS UTC`. Do not alter release compatibility or signature assertions.

- [x] **Step 4: Run the harness and inspect the dump**

```powershell
cmake --build build --config Release --target TrinityCrashDiagnosticsHarness
ctest --test-dir build --build-config Release -R TrinityCrashDiagnosticsHarnessTests --output-on-failure
```

Then open the generated retained harness dump with WinDbg/cdb and capture:

```text
!analyze -v
~* kv
lm
```

Confirm the exception address, register block, thread stacks, loaded/unloaded modules, and `harness.pre-crash` text marker correlate to the same timestamped bundle.

- [x] **Step 5: Run the complete Release verification**

Enter the Visual Studio developer environment, then run:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
Get-FileHash build\Release\Trinity.asi -Algorithm SHA256
```

Expected: all tests pass. Record the ASI SHA-256 and synthetic `.txt`/`.dmp` sizes. Do not deploy the artifact.

- [x] **Step 6: Update the design evidence and commit verification**

Append an implementation-evidence section to the approved spec containing commit IDs, CTest result, ASI hash, synthetic bundle sizes, and the explicit remaining proof: a fresh real-game crash is still required to validate live attribution.

```powershell
git add tests/crash_diagnostics_harness.cpp tests/verify_crash_diagnostics_harness.ps1 tests/readiness_tests.cpp docs/superpowers/specs/2026-09-22-trinity-crash-diagnostics-design.md
git add -p CMakeLists.txt
git diff --cached --check
git commit -m "test: verify bounded crash diagnostics bundle"
```

- [x] **Step 7: Review scope and history**

```powershell
git status --short
git log --oneline -7
git diff HEAD~7..HEAD --stat
```

Verify that unrelated dirty files remain untouched, no installed ASI was overwritten, and no claim of real-game semantic proof is made.
