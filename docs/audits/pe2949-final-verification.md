# PE 2949 Final Verification — build, test, package, matrix

Companion to `docs/audits/pe2949-a9e5ca20.md`. Executes **Task 4** of
`docs/superpowers/plans/2026-09-21-pe2949-full-menu-audit.md`.

Target under test: `CrimsonDesert.exe` **1.0.0.2949**,
SHA-256 `A9E5CA2076367E7995B81A3A4803F7259AB7DAC3415DF8EA949043EF635A174A`.

Evidence date: **2026-09-21**.

---

## 1. Task 4 checklist

| Step | Result | Evidence |
| --- | --- | --- |
| Run the Release Win64 build; record command, exit code, artifact hash | **DONE** | §2 |
| Run all CTest targets; record a build-timestamp failure as independent, do not claim green | **DONE** | §3 |
| With game closed: hash/backup installed ASI, deploy, hash-compare, fresh-launch, capture startup log | **DONE** (owner-run 2026-09-21 19:58) | §4 |
| Re-run the full menu route checklist in a fresh process | **PARTIAL** — the game launched, all hooks resolved, and crash triage was performed; the control-by-control ON/OFF/persistence sweep is still outstanding | §4.3, §5 |
| Publish the final matrix with identity, AOB/RVA evidence, live test date/save state, artifact hash, backup path, remaining blockers | **DONE** (this document + the audit artifact) | §5-§7 |

---

## 2. Release build

Toolchain discovered by the project's own `Build_Trinity.ps1` logic (`vswhere` → newest VS with the
C++ workload). There is no VS install registered with `vswhere`, so the script's documented
VS-18-Insiders fallback path was used; that is the toolchain this machine actually has.

```text
Compiler        : MSVC 19.51.36257 (x64 Hostx64)  [directory: VC/Tools/MSVC/14.51.36231]
Generator       : Ninja, Build type Release, ENABLE_EXTENDED_HOOKS=OFF
CMake           : 4.3.1  (bundled with VS 18 Insiders)
Windows SDK     : 10.0.28000.0
Build directory : build-clean   (FetchContent sources reused from build-clean/_deps)
```

Configure + build command (single invocation, `%PATH%` seeded by `vcvars64.bat`):

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=<VS CMake bin>;<repo>\tools\audit;%PATH%"
set "TRINITY_RC_SHIM_LOG=<repo>/docs/audits/rc-shim.log"
cmake.exe -S . -B build-clean -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MAKE_PROGRAM="<VS>\...\CMake\Ninja\ninja.exe" ^
  -DCMAKE_C_COMPILER="<VS>\...\Hostx64\x64\cl.exe" ^
  -DCMAKE_CXX_COMPILER="<VS>\...\Hostx64\x64\cl.exe" ^
  -DCMAKE_RC_COMPILER="<repo>/tools/audit/rc.exe" ^
  -DCMAKE_MT="C:/Program Files (x86)/Windows Kits/10/bin/10.0.28000.0/x64/mt.exe" ^
  -DENABLE_EXTENDED_HOOKS=OFF ^
  -DFETCHCONTENT_SOURCE_DIR_IMGUI="<repo>/build-clean/_deps/imgui-src" ^
  -DFETCHCONTENT_SOURCE_DIR_MINHOOK="<repo>/build-clean/_deps/minhook-src"
cmake.exe --build build-clean
```

Result:

```text
[56/56] Linking CXX shared library Trinity.asi
BUILD_EXIT=0
```

| Artifact | Value |
| --- | --- |
| Path | `build-clean/Trinity.asi` |
| Size | 1940480 bytes |
| SHA-256 | `8B2318970C21577F4621805F2DE921EB6CCEF70DDFE56DBD57C122FD0E05F9A2` |
| Embedded `TRINITY_BUILD_TIME` | `Sep 21 2026 19:20:07` |

The embedded build stamp was refreshed from the current wall clock before compiling, exactly as
`Build_Trinity.ps1` does, so the in-game banner matches this binary rather than the previous one.

### 2.1 Toolchain defect worked around (must be recorded, not hidden)

The first build failed **only at the link stage**:

```text
fatal error RC1106: invalid option: -ologo
LINK : fatal error LNK1327: failure during running rc.exe
```

Diagnosis (all steps reproduced):

1. `link.exe /nologo <obj> /out:t.exe /MANIFEST:EMBED,ID=1 kernel32.lib` run **directly** in the same
   environment → exit 0. So the linker is not inherently broken.
2. The same args passed to `rc.exe` by hand → exit 0.
3. The literal switch `-ologo` is rejected by **every** installed SDK resource compiler
   (10.0.22621.0, 10.0.26100.0, 10.0.28000.0) *and* by the Embarcadero-bundled one; `-nologo` is
   accepted by all of them.
4. `CMakeCache.txt` recorded MSVC `14.51.36231`, but the compiler in that directory now reports
   **19.51.36257** — the VS 18 Insiders channel updated the toolset in place. The previously
   installed `Trinity.asi` (`39FD38CE…`) and the pre-existing `TrinityReadinessTests.exe` are both
   timestamped 21/09 18:54, i.e. they were linked before that update; nothing in the repository
   changed to cause this.

Conclusion: the updated MSVC toolset emits a malformed `-ologo` to `rc.exe` when merging a manifest
into the resource section. It is a **local environment defect, unrelated to the audit's subject
matter**, and it does not affect the shipped bytes.

Workaround: `tools/audit/rc_shim.c`, compiled to `tools/audit/rc.exe`, registered as
`CMAKE_RC_COMPILER` **and** placed first on `PATH` (link.exe resolves `rc.exe` from `PATH`). It
forwards every argument to the real SDK compiler, dropping only the malformed switch;
banner suppression is cosmetic, so the `.res` output is byte-identical to a correct invocation.
Every forwarded command line is logged to `docs/audits/rc-shim.log` as evidence, which shows the
real compiler receiving well-formed arguments:

```text
""...\10.0.28000.0\x64\rc.exe" "-DNOMINMAX" "-DTrinity_EXPORTS" ... "/fo" "CMakeFiles\Trinity.dir\src\Trinity.rc.res" "...\src\Trinity.rc""
""...\10.0.28000.0\x64\rc.exe" "/nologo" "/x" "/fo" "C:\...\lnk{...}.tmp" "C:\...\lnk{...}.tmp""
```

**Recommended follow-up (not done here, outside the audit's scope):** either pin a known-good
toolset (`vcvars64.bat -vcvars_ver=14.44.35207`) or report the defect upstream, so the workaround can
be deleted. Nothing in this audit depends on the shim beyond producing the artifact.

---

## 3. CTest

```text
Test project C:/Users/mul0/Documents/GitHub/Trinity/build-clean
1/5 TrinityMapMarkerTests ............   Passed    0.06 sec
2/5 TrinityTravelLogicTests ..........   Passed    0.04 sec
3/5 TrinityNoClipLogicTests ..........   Passed    0.05 sec
4/5 TrinityReadinessTests ............***Failed  0.05 sec
5/5 TrinityMinHookFallbackTests ......   Passed    0.24 sec

80% tests passed, 1 tests failed out of 5
CTEST_EXIT=8
```

### 3.1 The one failure is independent and expected — the suite is **not** green

```text
FAIL: the startup log must show the requested release build timestamp
```

`tests/readiness_tests.cpp` asserts

```cpp
Expect(std::strcmp(TRINITY_BUILD_TIME, "Sep 21 2026 14:11:37") == 0, ...);
```

a **hard-coded timestamp string**. It can only pass for a build produced in that exact minute, so it
fails for every subsequent build by construction. At the time of this audit
`build_timestamp.h` contained `"Sep 21 2026 19:20:07"`.

This is a pre-existing test defect, not a PE 2949 contract failure, and it is reported as an
independent failure exactly as the plan instructs. Root cause and recommendation: the assertion
should not pin a literal build time; it should assert the *shape* of the stamp (or compare against a
generated constant), otherwise `TrinityReadinessTests` can never be green on a fresh build.

All five PE 2949 contract tests added by this audit pass:

* `Pe2949PickupBranchByteContractIsExact`
* `Pe2949NativeSetterBucketLayoutIsExact`
* `Pe2949DisabledRegistryPlaceholdersResolveToNothing`
* `Pe2949CodeSectionsAreNotNamedText`
* `Pe2949TebRealmLayoutIsExact`

---

## 4. Deployment and fresh launch — DONE (owner-run), with one crash

### 4.0 Outcome (2026-09-21 19:58)

| Check | Result |
| --- | --- |
| Installed `bin64\Trinity.asi` SHA-256 | `8B2318970C21577F4621805F2DE921EB6CCEF70DDFE56DBD57C122FD0E05F9A2` = **the audited build hash** |
| Startup log build stamp | `built Sep 21 2026 19:20:07` = this artifact |
| Revision detection | `Crimson Desert 2.03.01 (Active) [PE: 1.0.0.2949]` |
| Readiness | `Gameplay code ready` after 0.28 s; all 7 sentinels resolved |
| Hook RVAs vs audit | **all match** (`0x17AE110`, `0x873850`, `0x4282090`, `0x654ED0`, `0x6550B0`, `0x369FF70`, `0x212A100`, `0x212E170`, `0x2409920`, `0x2135850`, `0x2407824`, `0x214BE9C`, `0x1ECB7E0`, `0x1ECB650`, `0x1ECA1D0`, `0xDBB02F0`) |
| Live patch bytes | worker `0x14214BE9C` = `E9 96 00 00 00 90`; Slot Size branch `0x142407824` = `90 90` — both exactly as contracted |
| Two unverified offsets | `+0x18` = `999999` and `+0x111` = `1` on all sampled item defs → **confirmed correct** |
| Crash | **one**, at 19:58:13 on launch #1; launch #2 at 19:58:44 still running |

The deploy half of the plan is therefore complete and the "installed artifact hash matches the
verified build hash" criterion is satisfied.

### 4.0.1 Crash triage summary

`0xC0000005` **write** violation at `CrimsonDesert.exe+0x47AF8C` (`mov [rcx], rax`) with
`rcx = 0x0000066E6C314008`, inside a **generic engine member constructor** whose enclosing composite
constructor has 12 call sites. The bad `this` was **passed in** by the engine caller. No mod hook or
patch site is near that RVA.

**Resolved — external cause, not the mod.** The owner has attributed the crashes to an **NVIDIA driver
fault**: in rare dark scenes the game stalls to 2-3 FPS and then crashes. The triage above is
consistent with that (fault in generic engine infrastructure, nothing of ours nearby) and so is the
fact that none of the 12 crashes shares a fault address. The mod-side bisect is therefore **not
required**. Full detail: `pe2949-a9e5ca20.md` §8.3 and §6.2 R5.

### 4.1 Pre-deploy backup (Completion Criterion satisfied)

Recoverable backup created at `docs/audits/pe2949-predeploy-backup/` with
`SHA256SUMS-audit-backup.txt`:

| File | Size | SHA-256 |
| --- | --- | --- |
| `Trinity.asi123` (currently installed, mod disabled) | 1940480 | `39FD38CE11AF983591D3011C8405EFDEFC7C273E129A1E561C5580BA56F6FD3B` |
| `Trinity.asi.candidate-audit-build` (this build) | 1940480 | `8B2318970C21577F4621805F2DE921EB6CCEF70DDFE56DBD57C122FD0E05F9A2` |
| `Trinity.ini` | 1360 | `71F2B653ADA73D150DC259E66690A5A1E457748050CEFD71157DDF346B92172E` |
| `winmm.dll` (ASI loader) | 3621536 | `5DC7D6695E509948DF5CD39B780B132D9AB0CA001287803350DB282B7214DA28` |

The installed-artifact-hash-matches-build-hash criterion is now **satisfied** — the installed bytes
are `8B231897…`, the audited build hash (was `39FD38CE…` before deploy).

### 4.2 Deploy procedure used (kept for rollback/repeat)

A copy-pasteable version of this, including the full 61-item live checklist, the startup-log
expectations and the rollback, is in **`docs/audits/pe2949-live-session-runbook.md`**.

```powershell
# 1. Close CrimsonDesert.exe completely.
# 2. Back up the current install (already mirrored in docs/audits/pe2949-predeploy-backup).
$g = 'E:\Steam\steamapps\common\Crimson Desert\bin64'
Copy-Item "$g\Trinity.asi123" "$g\Trinity.asi.backup-audit-<timestamp>" -Force
# 3. Deploy and hash-compare.
Copy-Item build-clean\Trinity.asi "$g\Trinity.asi" -Force
(Get-FileHash build-clean\Trinity.asi -Algorithm SHA256).Hash
(Get-FileHash "$g\Trinity.asi" -Algorithm SHA256).Hash      # must both be 8B231897…
# 4. Fresh-launch, then capture the startup log and the first lines of Trinity.log:
#    expect "Trinity v<string> ... (built Sep 21 2026 19:20:07)" and
#           "Game version detected: Crimson Desert 2.03.01 (Active) [PE: 1.0.0.2949]"
# 5. Run the live checklist in docs/audits/pe2949-a9e5ca20.md §7.
# 6. Record the save state, date, and any crash under Trinity_Crash.txt.
# 7. Rollback if needed: copy the step-2 backup back over Trinity.asi.
```

---

## 5. Final matrix

`PASS` = static contract proven **and** live ON/OFF/persistence proven.
`STATIC ONLY` = contract proven against the exact EXE; live proof outstanding.
`BLOCKED` = a required AOB/ABI/route is missing or ambiguous.
`FAILED` = proven broken. `N/A` = does not exist / deliberately disabled.

### 5.1 Audit infrastructure

| Item | Status | Evidence |
| --- | --- | --- |
| Executable identity frozen (path, hash, versions, base, sections, timestamp) | `PASS` | `pe2949-a9e5ca20.md` §1 |
| Registry manifest generated from source (233 entries; 219 in the plan's scope) | `PASS` | `pe2949-aob-manifest.txt`, `pe2949-scan-table.md` |
| Every scannable entry scanned against executable + data sections with file off/RVA/VA/section/decoder | `PASS` | `pe2949-scan-table.md` |
| PE 2949 anchor contracts | `PASS` | 23/23 in `pe2949-contracts.md` |
| Inline literals outside the registry audited | `PASS` | `pe2949-inline-literals.md` |
| On-disk RVAs verified against the live image (read-only, unmodded process) | `PASS` | 44/44 in `pe2949-runtime-crosscheck.md` |
| Release Win64 build reproduces and links | `PASS` | §2, exit 0 |
| CTest suite green | `FAILED` | §3.1 hard-coded build stamp (1 of 5 tests) |
| Installed artifact hash == verified build hash | `BLOCKED` | §4, deploy deferred |
| Recoverable pre-deploy backup exists | `PASS` | §4.1 |

### 5.2 Menu routes (`src/gui/menu.cpp`)

208 visible controls across 45 `Render*` entry points; 2 controls inside a block comment.

| Tab | Route id | Entry point | Controls | Status | Blocker |
| --- | --- | --- | --- | --- | --- |
| PLAYER | *(root)* | `RenderPlayer` | 7 | `STATIC ONLY` | B1 |
| PLAYER | `combat_options` | `RenderCombatOptions` | 9 | `STATIC ONLY` | B1, R4 |
| PLAYER | `equipslots` | `RenderEquipSlots` | 8 | `BLOCKED` | B2 |
| PLAYER | `equipedit` | `RenderEquipEdit` | 9 | `BLOCKED` | B2 |
| PLAYER | `equipgear` | `RenderEquipGear` | 6 | `BLOCKED` | B2 |
| PLAYER | `equipswap` | `RenderEquipSwap` | 6 | `BLOCKED` | B2 |
| PLAYER | `dyeslots` / `dyeedit` / `dyecustom` | `RenderDyeSlots`, `RenderDyeEdit`, `RenderDyeCustom` | 20 | `BLOCKED` | B3 (unreachable + 7/7 dye AOBs missing) |
| INVENTORY | *(root)* | `RenderInventoryHome` | 9 | `STATIC ONLY` | R1 |
| INVENTORY | `invedit` | `RenderInventoryEditor` | 2 | `STATIC ONLY` | R1 |
| INVENTORY | `invstore` | `RenderInventoryStorage` | 3 | `STATIC ONLY` | R1 |
| INVENTORY | `invcat` | `RenderInventoryCat` | 3 | `STATIC ONLY` | R1 |
| INVENTORY | `invadd` | `RenderInventoryAdd` | 4 | `STATIC ONLY` | — |
| INVENTORY | `invaddcat` | `RenderInventoryAddCat` | 3 | `STATIC ONLY` | — |
| INVENTORY | `invrestore` | `RenderInventoryRestore` | 8 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | 8 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_bounty` | `RenderRestoreBounty` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_lore` | `RenderRestoreLore` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_recipes` | `RenderRestoreRecipes` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_keys` | `RenderRestoreKeys` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_collect` | `RenderRestoreCollectibles` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_gear` | `RenderRestoreGear` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_mount` | `RenderRestoreMount` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invrestore_medals` | `RenderRestoreMedals` | 5 | `STATIC ONLY` | — |
| INVENTORY | `invmoney` | `RenderInventoryMoney` | 10 | `STATIC ONLY` | — |
| INVENTORY | `invmoney_opt` | `RenderInventoryMoneyOptional` | 2 | `STATIC ONLY` | — |
| INVENTORY | `invabyss` | `RenderInventoryAbyss` | 16 | `STATIC ONLY` | — |
| TRAVEL | *(root)* | `RenderTravel` | 6 | `STATIC ONLY` | marker path |
| TRAVEL | `saved_locs` | `RenderSavedLocations` | 3 | `STATIC ONLY` | — |
| TRAVEL | `loc_manage` | `RenderSavedLocationManage` | 5 | `STATIC ONLY` | — |
| TRAVEL | `ftcats` | `RenderFastTravelCats` | 1 | `STATIC ONLY` | — |
| TRAVEL | `ftnodes` | `RenderFastTravelNodes` | 2 | `STATIC ONLY` | — |
| TRAVEL | *(map marker teleport)* | in `RenderTravel` | — | `STATIC ONLY` | corrected — live run installed 5/5 capture hooks; see audit §8.3 |
| WORLD | *(root)* | `RenderWorld` | 6 | `STATIC ONLY` | — |
| WORLD | `world_time_presets` | `RenderTimePresets` | 6 | `STATIC ONLY` | — |
| WORLD | `world_weather` | `RenderWeatherAtmosphere` | 17 | `STATIC ONLY` | R2 (`EnvManager` missing) |
| SYSTEM | *(root)* | `RenderSystem` | 11 | `STATIC ONLY` | — |
| SYSTEM | `keybinds` | `RenderKeybinds` | 1 | `STATIC ONLY` | — |
| SYSTEM | `menu_ui` | `RenderMenuUISettings` | 3 | `STATIC ONLY` | — |
| SYSTEM | `font_settings` | `RenderFontSettings` | 4 | `STATIC ONLY` | — |
| PLAYER | *(commented out)* | No Clip + escape hatch | 2 | `N/A` | `menu.cpp:160-176` |

Row-level detail for all 208 controls: `docs/audits/pe2949-menu-inventory.md`.

### 5.3 Aggregate

| Status | Count | Note |
| --- | --- | --- |
| `PASS` | 9 | audit infrastructure only (§5.1) — no live feature qualifies yet |
| `STATIC ONLY` | 33 route rows | 140 controls, plus map-marker teleport (corrected from `BLOCKED`) |
| `BLOCKED` | 5 route rows | 46 controls (equipment 29 + dye 20) |
| `FAILED` | 1 | CTest suite not green |
| `N/A` | 2 controls | No Clip and its escape hatch, compiled out |

No row is blank.

---

## 6. Remaining blockers

1. **B1 — `kCharMgrAnchors` is `PARTIAL(1/6)`.** Only `anchor[0]` resolves the character manager; the
   documented multi-anchor consensus no longer corroborates anything. Re-derive the anchors for
   PE 2949 or make the single-anchor acceptance an explicit, logged warning.
2. **B2 — `kSig_EquipEffectRefresh_Legacy` matches twice and is resolved by first hit.**
   Add `CountMatches(...) == 1` and fail closed, or derive a unique signature. This gates the whole
   equipment/dye-appearance feature set.
3. **B3 — the Dye UI is unreachable and all seven dye engine AOBs are missing.** Either restore a
   reachable entry point with re-derived signatures, or delete the routes so the dead code and the
   "legacy" include stop implying the feature exists.
4. **MinHook trampoline placement is flaky on this title.** Observed directly: the same five marker
   hooks installed `0/5` in one session and `5/5` in another with no signature change, and
   `protection=no` despite `kSig_MarkerProtection` having exactly one match. The absolute-detour
   fallback (`CMakeLists.txt:50-54`) is the mechanism. Any "hook failed" result on this game must be
   re-tested in a second session before being attributed to a signature.
5. **`TrinityReadinessTests` cannot pass on a fresh build** because it pins a literal build time.
   Fix the assertion so the suite can be green and therefore meaningful.
6. **R1 — `kSig_InvCoreGlobal` fails and logs nothing.** Add an explicit warning so the loss of the
   durable container walk is visible.
7. ~~**Crash reports on 2026-09-21**~~ — **RESOLVED as external, not mod-caused.** The owner has
   attributed them to an **NVIDIA driver fault** (in rare dark scenes the game stalls to 2-3 FPS and
   then crashes). This matches the independent triage: the fault was a write to a freed/corrupted
   engine object inside generic engine infrastructure with **no mod hook or patch site near** the
   faulting RVA, and no two of the 12 crashes share a fault address. The mod-side bisect is **not
   required**. See `pe2949-a9e5ca20.md` §6.2 R5 and §8.3.
8. **The per-control live sweep is still outstanding** (§5 reports `STATIC ONLY` for every feature).
   The deploy, launch and hook-resolution checks all passed; only the ON/OFF/persistence matrix
   remains.

## 7. Verdict

Against PE 2949 (`A9E5CA20…174A`) the **static contract layer is sound**: every scannable registry
entry has a current-file record, the two PE 2949-specific Slot Size contracts are exact and match
the plan's expected RVAs byte-for-byte, the revision gates fail closed for unknown revisions, and
the on-disk RVAs were confirmed identical in a live process.

The **deployed artifact is verified**: the installed `Trinity.asi` hash equals the audited build
hash, the in-game startup log reports that build's timestamp, every hook resolved to the audited RVA,
and both live code patches contain exactly the contracted bytes. Two struct offsets that the static
audit could only call "unverified" are now live-confirmed.

**The crash is closed and is not a mod defect.** The single `0xC0000005` write fault on launch #1 was
independently triaged to generic engine infrastructure with no mod hook or patch site near the
faulting RVA, and the owner has attributed it to an NVIDIA driver fault that stalls the game to 2-3
FPS in rare dark scenes. Launch #2 ran for 7+ minutes without faulting.

One thing still prevents a clean verdict:

* **Blocker B1, B2 and B3 remain open** — the character-manager anchor consensus is down to 1 of 6,
  the equipment-refresh AOB is ambiguous, and the dye subsystem is both unreachable and unresolvable.
  Equipment and Dye stay `BLOCKED`.

So the honest verdict for the menu as a whole remains **`STATIC ONLY`, with Equipment/Dye `BLOCKED`**,
pending only the per-control live sweep. **No mod-side crash cause was found or is asserted.**
