# Worker Patch and Mount Editor Removal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the unverified Workers editor with a reversible PE 2850 AA byte patch, remove the dedicated Mount/Horse editor, and migrate Trinity.ini to one new persisted worker toggle without affecting shared mount gameplay.

**Architecture:** Keep `Worker` as a narrow patch controller that scans one exact signature, gates on revision 2850, transitions only between known original/patched byte sequences, and restores on shutdown. Keep the existing Settings persistence mechanism but replace all legacy worker fields/keys with `workerMaxLevelAndSkills`; remove only editor-specific mount paths from `Dye` and its menu routes.

**Tech Stack:** C++17, CMake, MSVC x64, Dear ImGui, existing `mem::FindPattern`/`mem::PatchMemory`, existing Trinity Settings/Localization systems, CTest.

**Spec:** `docs/superpowers/specs/2026-09-14-worker-patch-and-editor-removal-design.md`

## Global Constraints

- Apply the worker patch only for detected PE revision `2850`.
- Original bytes are `0F 85 95 00 00 00`; patched bytes are `E9 96 00 00 00 90`.
- Never write when the target bytes are neither the expected original nor the expected Trinity patch.
- Preserve `Infinite Stamina & Mount`, Player mount tracking, movement/Free Flight, Inventory mount-item browsing, Player dye, Inventory dye, and Player Equipment profiles.
- Ignore and omit legacy `workerMaxLevelHook`, `workerAutoApply`, `workerEdit*`, and `workerSkillId*` Trinity.ini keys.
- Preserve unrelated dirty work in the checkout; stage only files belonging to this plan.
- Use x64 Release for final build verification; CTest evidence does not imply in-game semantic proof.

---

### Task 1: Add failing pure contract tests for the new worker patch

**Files:**
- Modify: `tests/readiness_tests.cpp`
- Modify: `src/game/worker_logic.h`

**Interfaces:**
- Produces pure helpers `WorkerPatchSupportedForRevision(int)`, `WorkerPatchState`, and `CanTransitionWorkerPatch(WorkerPatchState, WorkerPatchState, const uint8_t*, const uint8_t*, size_t)` for the controller and tests.

- [ ] **Step 1: Add tests before implementation**

Add a `WorkerPatchContractIsStrict()` test that asserts:

```cpp
Expect(trinity::game::WorkerPatchSupportedForRevision(2850),
       "PE 2850 must support the supplied worker patch");
Expect(!trinity::game::WorkerPatchSupportedForRevision(2760),
       "unrelated PE revisions must not inherit the worker patch");
Expect(!trinity::game::WorkerPatchSupportedForRevision(0),
       "unknown PE revisions must fail closed");
```

The test must also construct the six-byte original and patched arrays and
assert that original-to-patched, patched-to-original, and same-state
transitions are allowed, while an unexpected six-byte array is rejected.

- [ ] **Step 2: Run the focused test and verify the expected RED failure**

Run:

```powershell
cmake --build build-clean --config Release --target TrinityReadinessTests
ctest --test-dir build-clean --build-config Release -R TrinityReadinessTests --output-on-failure
```

Expected: compilation fails because the new worker contract helpers do not yet
exist. Fix only test/setup mistakes if the failure is unrelated; do not add
production implementation before this expected failure is observed.

- [ ] **Step 3: Commit the test-only red state**

```powershell
git add tests/readiness_tests.cpp
git diff --cached --check
git commit -m "test: define strict worker patch contract"
```

### Task 2: Implement and verify the pure worker patch contract

**Files:**
- Modify: `src/game/worker_logic.h`
- Modify: `tests/readiness_tests.cpp`

**Interfaces:**
- Consumes: the failing assertions from Task 1.
- Produces: revision gate, patch-state enum, and strict byte-transition helper used by `Worker`.

- [ ] **Step 1: Implement the minimum helpers**

Define the following in `trinity::game`:

```cpp
enum class WorkerPatchState { Original, Patched };

inline bool WorkerPatchSupportedForRevision(int revision)
{
    return revision == 2850;
}

inline bool CanTransitionWorkerPatch(WorkerPatchState from,
                                     WorkerPatchState to,
                                     const uint8_t* current,
                                     const uint8_t* expected,
                                     size_t size)
{
    if (!current || !expected || size != 6) return false;
    if (from == to) return std::memcmp(current, expected, size) == 0;
    return std::memcmp(current, expected, size) == 0;
}
```

Use `<cstring>` and keep the helper strictly about the expected byte state;
the controller supplies the correct expected array for each transition.
Delete the old worker layout structs and clamp/template/observer helpers rather
than retaining unused legacy contracts.

- [ ] **Step 2: Run the focused test and verify GREEN**

Run the same `cmake --build` and `ctest` commands from Task 1. Expected:
`TrinityReadinessTests` passes, including the new worker contract test.

- [ ] **Step 3: Commit the pure contract**

```powershell
git add src/game/worker_logic.h tests/readiness_tests.cpp
git diff --cached --check
git commit -m "feat: add strict worker patch contract"
```

### Task 3: Replace the legacy Worker implementation with a reversible controller

**Files:**
- Modify: `src/game/worker.h`
- Modify: `src/game/worker.cpp`
- Modify: `src/game/offsets.h`
- Modify: `src/core/mod.cpp`
- Modify: `src/game/teleport.cpp`

**Interfaces:**
- Consumes: `WorkerPatchSupportedForRevision`, `WorkerPatchState`, and `CanTransitionWorkerPatch` from Task 2; `mem::FindPattern`, `mem::PatchMemory`, and `core::GetGameVersion`.
- Produces: `Worker::Install()`, `Worker::Remove()`, `Worker::Ready()`, `Worker::Enabled()`, and `Worker::SetEnabled(bool)`.

- [ ] **Step 1: Add the exact source contract in `offsets.h`**

Replace the old worker function/object offsets with constants:

```cpp
inline constexpr size_t kWorkerPatchSize = 6;
inline constexpr uint8_t kWorkerPatchOriginal[kWorkerPatchSize] =
    { 0x0F, 0x85, 0x95, 0x00, 0x00, 0x00 };
inline constexpr uint8_t kWorkerPatchEnabled[kWorkerPatchSize] =
    { 0xE9, 0x96, 0x00, 0x00, 0x00, 0x90 };
inline constexpr const char* kSig_WorkerMaxLevelAndSkills =
    "0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5";
```

Document that the first six bytes are the exact injection point from the
provided AA script and that the surrounding bytes disambiguate the site.

- [ ] **Step 2: Rewrite `worker.h` to expose only controller operations**

Remove all enumeration, observation, setter, template, and auto-apply methods.
Declare only:

```cpp
class Worker
{
public:
    static bool Install();
    static void Remove();
    static bool Ready();
    static bool Enabled();
    static bool SetEnabled(bool enabled);
};
```

- [ ] **Step 3: Implement the minimum controller**

Store the resolved target, a readiness flag, and an enabled flag in the
anonymous namespace. `Install()` must return false unless the current revision
is 2850 and `mem::FindAllMatches(kSig_WorkerMaxLevelAndSkills, ..., 2)` returns
exactly one match. Set the target to the match address, set ready, and if
`State::Get().workerMaxLevelAndSkills` is true call `Worker::SetEnabled(true)`;
on failure reset that state field to false and log the reason.

`SetEnabled` must select the expected current bytes and desired bytes from
`offsets.h`, read the target with a guarded read, reject any unexpected current
bytes, and call `mem::PatchMemory` only for a valid transition. Repeated calls
to the already-requested state must succeed without writing. Update the
controller state only after a successful patch.

`Remove()` must restore `kWorkerPatchOriginal` only when the target still reads
as `kWorkerPatchEnabled`; if it reads anything else, log a warning and leave it
untouched. Clear all controller state afterward.

- [ ] **Step 4: Remove old lifecycle calls and legacy includes**

Remove `Worker::Tick()` from `src/game/teleport.cpp`, remove all old worker API
comments from `mod.cpp`, and keep one install/remove call for the new controller.
Do not remove the Worker source from CMake because the new controller remains a
compiled subsystem.

- [ ] **Step 5: Build and run the focused tests**

Run:

```powershell
cmake --build build-clean --config Release --target TrinityReadinessTests Trinity
ctest --test-dir build-clean --build-config Release -R "TrinityReadinessTests|TrinityMapMarkerTests" --output-on-failure
```

Expected: both tests pass and the Trinity target links successfully.

- [ ] **Step 6: Commit the controller**

```powershell
git add src/game/worker.h src/game/worker.cpp src/game/offsets.h src/core/mod.cpp src/game/teleport.cpp
git diff --cached --check
git commit -m "feat: replace worker editor with reversible patch"
```

### Task 4: Replace Worker state and Trinity.ini persistence

**Files:**
- Modify: `src/core/state.h`
- Modify: `src/core/settings.cpp`
- Modify: `src/core/settings.h`
- Modify: `config/Trinity.ini.example`

**Interfaces:**
- Consumes: `Worker::SetEnabled` and `Worker::Enabled` from Task 3.
- Produces: persisted `State::workerMaxLevelAndSkills` and the `workerMaxLevelAndSkills=` Trinity.ini contract.

- [ ] **Step 1: Add the failing persistence contract check**

Extend the existing static/readiness verification command or add a focused
PowerShell test under `tests/verify_worker_feature_contract.ps1` that reads the
source files and asserts `workerMaxLevelAndSkills` is present while each legacy
worker key is absent from `Settings::Save` output and the example config.
Run it before implementation and verify it fails because the new field/key do
not exist yet.

- [ ] **Step 2: Implement the state migration**

Remove every legacy worker state field from `State` and add:

```cpp
bool workerMaxLevelAndSkills = false;
```

Do not change `infMountStamina` or other shared mount state.

- [ ] **Step 3: Implement Load/Save migration**

Parse only `workerMaxLevelAndSkills` into the temporary state, copy it after
the existing Auto Save gate, and emit exactly one line
`workerMaxLevelAndSkills=%d` from `Settings::Save`. Remove all legacy worker
parsing, clamping, copying, formatting, and argument expressions. Existing
files with old keys must continue to load all unrelated keys normally.

- [ ] **Step 4: Update the example and run the contract test GREEN**

Add the new key under the Player feature section with a comment that `1`
enables max level and all worker abilities. Run the focused PowerShell test and
expect PASS.

- [ ] **Step 5: Commit persistence changes**

```powershell
git add src/core/state.h src/core/settings.cpp src/core/settings.h config/Trinity.ini.example tests/verify_worker_feature_contract.ps1
git diff --cached --check
git commit -m "feat: persist worker max level toggle"
```

### Task 5: Replace the old menu with one Player toggle and remove Mount/Horse routes

**Files:**
- Modify: `src/gui/menu.cpp`
- Modify: `src/game/dye.h`
- Modify: `src/game/dye.cpp`

**Interfaces:**
- Consumes: `State::workerMaxLevelAndSkills`, `Worker::Ready`, `Worker::Enabled`, and `Worker::SetEnabled`.
- Produces: a single Player row and no `mount_options`/`world_workers` UI routes.

- [ ] **Step 1: Add the failing static menu contract**

Extend `tests/verify_worker_feature_contract.ps1` to require the new Player
label and reject `RenderWorkers`, `RenderMountOptions`, `mount_options`,
`world_workers`, and legacy worker action names. Run it and verify RED against
the current source.

- [ ] **Step 2: Implement the Player toggle**

Add one row in `RenderPlayer`:

```cpp
const bool workerBefore = st.workerMaxLevelAndSkills;
if (ui::Toggle(LOC("Max Worker Level & Skills"),
               &st.workerMaxLevelAndSkills,
               game::Worker::Ready()
                   ? LOC("Unlocks maximum level and all worker abilities.")
                   : LOC("Worker patch unavailable for this game revision.")))
{
    if (!game::Worker::SetEnabled(st.workerMaxLevelAndSkills))
    {
        st.workerMaxLevelAndSkills = workerBefore;
        ui::Toast(LOC("Worker patch could not be applied"));
    }
    else if (st.autoSave)
        Settings::Save();
}
```

Use the controller's `Enabled()` for any displayed readiness/status if the
widget needs a runtime state; do not treat a checked state as proof that bytes
were patched.

- [ ] **Step 3: Remove old Workers UI and routes**

Delete the complete `RenderWorkers` function, the `WORLD` submenu, and both
dispatch routes. Remove the Worker include if no longer needed beyond the new
Player toggle. Delete all old editor/template/observer labels.

- [ ] **Step 4: Remove editor-specific Mount/Horse UI and Dye API**

Delete `RenderMountOptions`, its Player submenu, and its dispatch route. Remove
`Dye::SetTargetMode`, `GetTargetMode`, `SetActiveMount`, and `GetActiveMount`
from the header and implementation. Remove only mode-1 mount branches,
mount-component capture/resolution, `SavedMountSlot` cache serialization, and
mount restore/apply code. Keep Player and Inventory dye modes and all shared
Player mount tracking calls.

- [ ] **Step 5: Run the static contract test GREEN**

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify_worker_feature_contract.ps1` and expect PASS. Verify that retained strings/functions for `infMountStamina`, `GetMountActor`, `GetTrackedMountCount`, `FreeFlight`, and `invrestore_mount` are still present.

- [ ] **Step 6: Commit menu and editor cleanup**

```powershell
git add src/gui/menu.cpp src/game/dye.h src/game/dye.cpp tests/verify_worker_feature_contract.ps1
git diff --cached --check
git commit -m "feat: replace worker menu and remove mount editor"
```

### Task 6: Remove obsolete offsets/docs/localization without deleting retained mount features

**Files:**
- Modify: `src/game/equipment.cpp` only where it is editor-specific
- Modify: `src/game/equipment.h` only where it is editor-specific
- Modify: `languages/Trinity_*.ini`
- Modify: `README.md`
- Delete: `docs/WORKER_LEVEL_AND_DISPATCH.md`

**Interfaces:**
- Consumes: the source cleanup from Tasks 3–5.
- Produces: no stale public descriptions or translation entries for removed editor routes, while retained mount gameplay/catalog descriptions remain.

- [ ] **Step 1: Remove stale Worker reverse-engineering note**

Delete `docs/WORKER_LEVEL_AND_DISPATCH.md` because it documents the removed
legacy direct-memory editor and claims the unverified layout is working. Do
not replace it with a claim that the new AA patch is in-game verified.

- [ ] **Step 2: Remove only orphaned editor copy**

Delete translation keys and README lines that name the removed Mount & Horse
editor, Mount Equipment Dye, Edit Mount Equipment, Workers & Mercenaries, or
worker direct editing. Retain text describing Infinite Stamina & Mount and the
Inventory Mount/Mecha/Vehicle Gear category.

- [ ] **Step 3: Verify no stale identifiers remain**

Run:

```powershell
rg -n -i "RenderWorkers|RenderMountOptions|mount_options|world_workers|workerAutoApply|workerEdit|workerSkillId|WorkerEntry|WorkerObservation|SavedMountSlot|SetTargetMode|GetTargetMode|SetActiveMount|GetActiveMount|WORKER_LEVEL_AND_DISPATCH" src tests config languages README.md docs
```

Expected: no matches. Separately verify retained mount identifiers with:

```powershell
rg -n "infMountStamina|GetMountActor|GetTrackedMountCount|invrestore_mount|Free Flight" src config languages README.md
```

Expected: retained functionality still has matches.

- [ ] **Step 4: Commit documentation cleanup**

```powershell
git add src/game/equipment.cpp src/game/equipment.h languages README.md docs/WORKER_LEVEL_AND_DISPATCH.md
git diff --cached --check
git commit -m "docs: remove obsolete worker and mount editor references"
```

### Task 7: Full verification and handoff

**Files:**
- No production files unless a verification failure identifies a scoped defect.

**Interfaces:**
- Consumes: all implementation tasks and their focused tests.
- Produces: fresh source, test, build, and diff evidence; no deployment claim.

- [ ] **Step 1: Inspect the final scoped diff**

Run:

```powershell
git status --short --branch
git diff --stat HEAD~6..HEAD
git diff --check HEAD~6..HEAD
```

Confirm unrelated pre-existing dirty files are not staged or committed by this
plan.

- [ ] **Step 2: Configure/build in the x64 Visual Studio environment**

Run in one `cmd.exe` process:

```cmd
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 && cmake --build build-clean --config Release --target Trinity TrinityReadinessTests TrinityMapMarkerTests
```

Expected: exit code 0 and successful Release link for all three targets.

- [ ] **Step 3: Run the complete configured CTest suite**

```cmd
ctest.exe --test-dir build-clean --build-config Release --output-on-failure
```

Expected: `100% tests passed, 0 tests failed` for every configured test.

- [ ] **Step 4: Re-run static contracts and source searches**

Run the worker contract PowerShell test, the stale-identifier `rg` command, and
the retained-feature `rg` command from Task 6. Record the exact results.

- [ ] **Step 5: Report evidence boundaries**

Report the exact build/CTest/static results and the new patch contract. State
explicitly that the implementation has not been deployed or semantically
validated in a live game unless a separate authorized closed-game deployment
and fresh-launch test is performed.

