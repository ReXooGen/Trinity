# Crimson Desert PE 2949 Full Menu Compatibility Audit Plan

> **Execution status (2026-09-21, final):** Tasks 1, 2 (static) and 3 (static) are complete; the
> Release build succeeded and CTest ran (1 pre-existing failure, documented). The artifact was
> **deployed and launched by the owner**: the installed hash matches the audited build, every hook
> resolved to the audited RVA, and both live code patches contain the contracted bytes. Two struct
> offsets that were static-only are now live-confirmed. The 2026-09-21 crash reports were attributed
> to an **NVIDIA driver fault**, not the mod — the mod-side bisect is not required.
> **Still outstanding:** the per-control ON/OFF/persistence sweep, and blockers B1/B2/B3.
> Evidence: `docs/audits/pe2949-a9e5ca20.md`, `docs/audits/pe2949-final-verification.md`,
> `docs/audits/pe2949-live-session-runbook.md`.
> Unchecked boxes below are genuinely outstanding, not forgotten.

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` or `superpowers:subagent-driven-development` task-by-task. Mark a checkbox only after the listed evidence is saved.

**Goal:** Establish a reproducible, fail-closed compatibility verdict for every Trinity menu control against Crimson Desert 2.03.01 / PE revision 2949, including its executable identity, AOB uniqueness, RVAs, ABI/layout contracts, OFF restoration, persistence, and live in-game result.

**Architecture:** Treat `src/game/offsets.h` as the signature registry, but validate every consumer because several literals and revision gates live outside that file. Produce a version-locked audit artifact from the exact on-disk EXE; no AOB, offset, ABI, or old Binary Ninja database is trusted until its bytes match the current file. A feature is **supported** only after static contract, clean install, ON behavior, OFF restoration, persistence, and its menu result have all passed.

**Tech Stack:** C++20 ASI, MinHook, DX12 overlay, PowerShell, Visual Studio CMake/Ninja, CTest, Binary Ninja or equivalent read-only disassembler, current retail `CrimsonDesert.exe`.

**Spec:** `GAME_UPDATE_PLAYBOOK.md`; current installed target is Crimson Desert 2.03.01 / PE 1.0.0.2949.

## Global Constraints

- Audit the exact EXE that will be launched: record absolute path, file version, PE revision, SHA-256, image base, image size, section table, and timestamp.
- Use read-only static/runtime analysis until a feature-specific mutation is approved; never treat a stale BNDB address as current evidence.
- Preserve unrelated dirty work. Never reset, overwrite, or deploy over a running `CrimsonDesert.exe`.
- Every exact AOB must have one accepted match unless its installer explicitly supports a documented alternate. Record file offset, RVA, VA, instruction boundary, and decoded ABI.
- Every mutation needs a verified original byte/value, bounded target, expected result, OFF restoration, and a clean game restart test.
- Do not enable Slot Size by only patching a pickup branch. PE 2949 uses the four-argument native setter and the reversible pickup patch only while Slot Size is ON.
- Do not call an old ABI with a new signature. Unknown PE revisions fail closed and remain visibly unsupported in the menu.
- Keep static proof, build/package proof, and live semantic proof separate in all records.

## Review Focus

- A one-match AOB inside data, a jump table, or a thunk is not a callable function; decode the prologue and callers before hooking it.
- Client-only inventory/equipment edits may display briefly and then be reconciled; exercise a save/load and an authoritative transaction.
- Features with an ON effect but no OFF restoration are regressions; test the exact original bytes/values after disable and after ASI unload.
- Overlay availability does not prove input routing; verify keyboard and controller navigation while gameplay is active and while an in-game menu is open.
- A wrong revision gate can select an ABI that looks valid but corrupts state; test PE 2949 and at least one unsupported neighboring revision policy path.

## Evidence Package Required Before Any Code Change

- [x] Create `docs/audits/pe2949-a9e5ca20.md` for the currently verified EXE hash `A9E5CA2076367E7995B81A3A4803F7259AB7DAC3415DF8EA949043EF635A174A`.

```text
EXE path:
SHA-256:
FileVersion/ProductVersion:
PE revision detected by Trinity:
Image base / SizeOfImage:
Sections (name, RVA, virtual size, raw offset, raw size, execute flag):
Trinity.asi source hash / installed hash:
Trinity.ini relevant settings:
Game PID at audit start (or "not running"):
```

- [x] Generate and attach an AOB manifest from the current source, not from a cached report.
  → `docs/audits/pe2949-aob-manifest.txt` (the repo has no `rg`; the equivalent extractor is
  `tools/audit/pe2949_audit.py`, which parses the registry instead of grepping lines, so it also
  captures multi-line and array-declared patterns).

```powershell
$out = 'docs/audits/pe2949-aob-manifest.txt'
rg -n 'kSig_|kCharMgrAnchors|kOff_|kTls_' src/game/offsets.h src/game/map_marker.h src/game/*.cpp src/core/*.cpp |
  Set-Content -Encoding utf8 $out
```

- [x] For each manifest entry, record: `symbol | source file:line | exact pattern | match count | file offset | RVA | VA | section | decoder result | ABI/layout evidence | feature consumers | status`.
  → all 233 entries in `docs/audits/pe2949-scan-table.md` (raw form: `pe2949-scan.json`).
- [x] Add a **not applicable** row for data-only UI controls and a **blocked** row for any unknown signature. Never silently inherit a nearby version's result.
  → `LAYOUT` rows for the 138 struct-offset constants, `NONE`/`MULTI`/`PARTIAL` rows for every
  unresolved signature, and `N/A` rows for the two compiled-out controls.

## PE 2949 Anchor Contracts Already Requiring Revalidation

| Contract | Current PE 2949 evidence to verify | Required decoder proof | Consumers / acceptance |
| --- | --- | --- | --- |
| Pickup capacity branch | `RVA 0x2407824`, original `74 07`, signature `84 D2 74 07 0F B7 4C 24 48 EB 0F 0F B7 4F 14 66 39 4C 24 48 66 0F 4C 4C 24 48 66 89 4C 24 32` | `test dl,dl; je +7`; prove patch target is exactly the two branch bytes | Slot Size companion patch: ON=`90 90`, OFF=`74 07`, no other byte changes |
| Native Slot Size setter | `RVA 0x2135850`, signature `48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 20 48 8B 41 18 41 0F B7 E9 8B 49 20 4C 8B F2 4C 8D 14 C8` | ABI `(holder, int* err, uint16 type, uint16 expansion)`; prove writes `bucket+0x16`, `+0x1A`, derived `+0x14` | Slot Size 1..700 works, no InventoryInfo mutation, sell/discard/save/reload stable |
| Inventory holder | `kSig_InvGetHolder` | holder bucket vector, count, bucket type `+0x10` | item browser, quantity, Add Item, Slot Size, stack size |
| Realm selection | `RealmFlagOffsetForRevision(2949)` and transaction hooks | TLS offset and client/server call path | authoritative Add Item, quantity persistence, inventory reconciliation |
| Movement owner | `MoveComponentOwnerOffsetForRevision(2949)` and PE2944-compatible loco contract | local player identity and vector argument ABI | teleport, Super Run, Free Flight, No Clip |

## Complete Audit Matrix

### 1. Bootstrap, overlay, input, persistence, localization

| Menu / subsystem | Source owners | Required signatures or offsets | Checks |
| --- | --- | --- | --- |
| ASI bootstrap and readiness | `src/dllmain.cpp`, `src/core/mod.cpp`, `src/core/readiness.*` | readiness sentinels in `mod.cpp`: damage, timing, move, inventory, wanted, timer/time/weather | DLL loads once; startup banner identifies PE 2949; each sentinel is unique; timeout reports exact missing contract; no false `Ready` state |
| DX12 overlay | `src/hooks/dx12_hook.cpp` | DXGI factory / swapchain / Present hooks | overlay appears after swapchain creation, resize/HDR works, game does not flicker/crash, input ownership toggles correctly |
| Keyboard/controller/menu input | `src/hooks/input.cpp`, `src/hooks/xinput_hook.cpp`, `src/gui/framework.cpp` | input hook path and controller masks in `State` | INSERT / controller toggle opens and closes; navigation, changing values, typing, back behavior and focus restoration work |
| Settings and language | `src/core/settings.cpp`, `state.h`, `localization.cpp`, `languages/*.ini` | no game AOB; revision support gates | every visible toggle/value saves, reloads, clamps safely, unsupported state resets visibly, translated label/tooltips do not overflow |
| System page | `menu.cpp:RenderSystem`, keybinds/UI/font pages | no gameplay AOB unless a linked feature is changed | reset defaults, autosave, key rebinding, menu scale/font/language and console/FPS state are persistent and reversible |

### 2. Player and combat

Audit every control exposed by `RenderPlayer` and `RenderCombatOptions`: health/stamina/stat edits, God/One Hit/Ignore Hit/Easy Parry, speed/jump, damage multipliers, status/debuff controls, and all character-selection actions.

| Contract symbols | Offset/layout checks | Live proof |
| --- | --- | --- |
| `kCharMgrAnchors[0..5]`, `kSig_StatCommit` | all anchors agree on one manager; list `+0xB8/+0xC0`; possessor round-trip `owner+0xA0 -> +0xD0`; stat array `root+0x58` | switch character, load save, enter combat; edit each stat; verify no NPC/companion is edited |
| `kSig_DamageApply`, `_Alt`, `kSig_CombatTimingEval`, `kSig_JustCore`, `_Alt` | hook prologue, source/target argument roles, return/flag semantics | player damage, enemy damage, parry window and toggle OFF behavior independently; no global NPC damage corruption |
| player offsets in `offsets.h` and `player.cpp` | pointer validity, type descriptor, controlled body identity | death/revive, cutscene, mount/dismount, reload, and controller input all retain correct local target |

### 3. Travel and movement

| Feature | Required contract | Required live cases |
| --- | --- | --- |
| Teleport / saved locations | `kSig_MoveUpdate`; position `+0x90`, desired velocity `+0xC0`, velocity `+0xD0`, second destination `+0x1A0` | current position, saved slot save/load/delete, uneven terrain, water, interior, loading boundary; read back final coordinates |
| Map-marker teleport | `kSig_MarkerPattern`, `kSig_MarkerOriginPrefix`, `kSig_MarkerPlayer`, `kSig_MarkerProtection`; waypoint `global -> +0xA8 -> +0x20/+0x24/+0x28` | map marker create/move/remove, queue/cancel/result text, unreachable marker, protection/height fallback and OFF no residual movement |
| Fast Travel | `kSig_TravelToNode`, `_Pre201`, `_Legacy`, node/catalog structures | every category opens; a node travels exactly once; cancel/back works; unknown node fails safely |
| Super Run / Free Flight | `kSig_LocoStepper` or `_Pre201`; revision movement-owner offset | keyboard and controller, ground/air/mount, speed bounds, collision, toggle OFF restores velocity/locomotion; do not claim collision bypass without collision proof |
| No Clip | its local-body collision-filter lifecycle, not position pinning | body changes across reload/death/mount; collision disabled only while ON and restored on OFF; Space/Ctrl and speed do not substitute for collision evidence |

### 4. Inventory, money, crime, and restore catalogs

| Menu function | Required AOB/ABI/layout audit | Live acceptance |
| --- | --- | --- |
| Inventory browser/editor | `kSig_InvGetItemQty`, `_Legacy`; `kSig_InvGetHolder`; inventory tables and slot stride | open inventory, every category and item list, filter/search, item selection, stack display matches game UI |
| Add Item / bulk Add / money/camp currency | `kSig_TrItemValueCtor` or pre201; `kSig_InvCommitPlacement201`/legacy; `kSig_InvFreePlacements201`/legacy; `kSig_InvHolderInsert2944` for PE2949; `kSig_InvCommit`; TLS realm | client and server holder are distinct/live; one item, bulk item, money, optional currency; save/reload; failed add reports error without a ghost item |
| Quantity / remove / restore | holder bucket and item slot offsets; transaction/commit ABI | edit stack, consume/use/equip, sell, discard, buy, save/reload; restore each catalog category; no duplicate or invalid transaction |
| Max Stack Size | item definition max-stack and cap flag offsets | enable, disable, edited stack, merge behavior, purchase/pickup, save/load; non-stackable/unique items remain valid |
| Slot Size | PE2949 native setter and pickup contracts in anchor table | values 1, vanilla, 700; ON shows real capacity; pickup when full; sell/discard; vendor transaction; save/load; OFF restores every bucket and `74 07` |
| No Bounty | `kSig_EvaluateCrimeWantedState`; only use `kSig_RegisterCrimeEvent` if revision policy permits | theft/assault/murder test, wanted UI/state/guards, toggle OFF restores ordinary game behavior |
| Item/localization/category tables | `kStr_ItemInfoTable`, `kStr_ItemGroupInfoTable`, `kStr_StringInfoTable`, `kStr_InventoryInfoTable`; `kSig_LocStringGet` alternatives | all Add/Restore categories label and icon correctly; delayed table materialization retries; no raw pointer dereference on missing table |

### 5. World, time, weather

| Feature | Required signatures | Live acceptance |
| --- | --- | --- |
| Game speed / pause/time freeze | `kSig_FrameTimerBody`, `_Pre201`, `kSig_FieldTimeRealm`, `kSig_FieldTimeTick`, `_Pre201`, `kSig_TodEngineGlobal` | client/server behavior, time preset, pause/unpause, day transition, save/reload, OFF restores real-time progression |
| Weather / sky | `kSig_WeatherRain`, `Snow`, `Dust`, `kSig_WindPack`, `_Pre201`, `kSig_EnvManager` | rain/snow/dust/wind/clear sky/fog intensity, zone transition, weather change by game, OFF restores engine-owned values |

### 6. Equipment, dyes, appearance, trust, workers

| Menu function | Required AOB/offset contract | Live acceptance |
| --- | --- | --- |
| Equipment editor / gear / swap | `kSig_EquipEffectRefresh`, `_Legacy`; socket/vector ABI; item instance and selected-character layout | every socket, refine, durability/stat edit, Add Gear, swap; equip/unequip, save/reload, effect refresh, no invalid item ID |
| Dye slots / presets / custom color | `kSig_EquipBatch`, `_Legacy`, `kSig_DyeApplySlot`, `ApplyBatch`, `Upsert`, visual set/clear, record remove | each visual slot/channel, preset/custom color, clear, inventory vs equipped mode, zone load, save/reload and render refresh |
| NPC/Pet trust | `kSig_FriendlySetNpc201`, `SetPet201`, getters and fallback patterns; trust-site hook offset `+0x12` | target NPC and pet separately, multiplier values, relationship UI, reload, disable restoration/no cross-target change |
| Worker level/skills | `WorkerPatchSupportedForRevision`, worker signatures/offsets in `worker.cpp` | PE2949 gate, one worker, all workers, save/reload, UI and quest progression; unsupported revision stays disabled |

## Task 1: Freeze the exact PE 2949 baseline

**Files:**
- Create: `docs/audits/pe2949-a9e5ca20.md`
- Create: `docs/audits/pe2949-aob-manifest.txt`
- Inspect: `src/core/version_detect.*`, `src/core/version_mapping.*`, `src/mem/scanner.*`, `src/mem/section_filter.*`

- [x] Record the identity block and hash the current EXE plus installed/built ASI.
- [x] Run the manifest command above and add a row for all 216 current signature/offset entries.
  → the current tree holds **233** registry entries, **219** of them in the plan's prefix scope
  (`kSig_`/`kOff_`/`kTls_`/`kCharMgrAnchors`); "216" was a snapshot figure. Every one has a row.
- [x] Scan every exact AOB against executable code sections only; decode each match and record the current RVA/VA. A zero/multiple match is `BLOCKED`, not a candidate for fuzzy widening.
  → scanned across the runtime scanner's own section set (all but a non-executable `.debug`); 36
  `UNIQUE`, 42 `NONE`, 6 `MULTI`, 1 `PARTIAL`, 7 `PRESENT`, 140 `LAYOUT`, 1 `DISABLED`.
- [x] Verify PE2949-specific contracts: native Slot Size setter at `0x2135850` has one match; pickup branch at `0x2407824` begins `74 07`; no assumption is copied from PE2944.
  → 23/23 checks pass in `docs/audits/pe2949-contracts.md`, including "the PE 2850/2760 holder-insert
  AOB must not match PE 2949" (0 matches).
- [x] Add C++ readiness tests for each new revision gate or exact mutable byte contract before implementation changes.
  → five `Pe2949*` tests added to `tests/readiness_tests.cpp`; all pass.

## Task 2: Audit bootstrap and every visible menu route

**Files:**
- Inspect: `src/gui/menu.cpp`, `src/gui/framework.cpp`, `src/hooks/dx12_hook.cpp`, `src/hooks/input.cpp`, `src/hooks/xinput_hook.cpp`, `src/core/settings.cpp`
- Test: `tests/readiness_tests.cpp` and manual checklist in audit artifact

- [ ] Open every top-level tab: Player, Inventory, Travel, World, System.
- [ ] Open every route listed by `Render*` functions, including all Inventory Restore pages, Add categories, Fast Travel categories/nodes, equipment/dye pages, keybinds, UI and fonts.
  → **static equivalent done**: all 45 `Render*` entry points and 38 dispatcher route ids were
  enumerated programmatically, and 208 visible controls were inventoried in
  `docs/audits/pe2949-menu-inventory.md`. Three routes (`RenderDyeSlots`, `RenderDyeEdit`,
  `RenderDyeCustom`) have no call site at all. Opening them in-game is still outstanding.
- [ ] For every UI item record `label | state type | State field | implementation entry point | required contract rows | ON proof | OFF proof | persistence proof | result`.
  → label, widget, State binding, entry point and source line are recorded for all 208 controls;
  the ON/OFF/persistence columns require a live session and are deliberately still empty rather
  than guessed.
- [ ] Test keyboard and controller with overlay open/closed and with the game inventory/map open. Record an input routing failure separately from a gameplay hook failure.

## Task 3: Validate feature contracts one subsystem at a time

**Files:**
- Inspect: `src/game/player.cpp`, `teleport.cpp`, `inventory.cpp`, `world.cpp`, `equipment.cpp`, `dye.cpp`, `friendly.cpp`, `worker.cpp`, all corresponding `*_logic.*`, `offsets.h`
- Test: relevant `tests/*_tests.cpp`, `TrinityReadinessTests`, manual cases in sections 2-6

- [ ] Complete Player/Combat before enabling any player-writing control.
- [ ] Complete Travel/Movement, including marker queue/cancel and local-player identity, before enabling movement controls.
- [ ] Complete Inventory as one transaction domain: holder, client/server authority, constructor, placement, commit, removal, restore, stack, Slot Size and No Bounty.
- [ ] Complete World, then Equipment/Dye/Trust/Worker. For each domain, run the exact ON/OFF/save/load live cases in its table.
  → **static contract pass complete for every domain** (see `pe2949-a9e5ca20.md` §5). Blockers found:
  character-manager anchor consensus (1/6) and the ambiguous `kSig_EquipEffectRefresh_Legacy` (2
  matches, first-hit); plus the entire dye subsystem (7/7 AOBs missing + UI unreachable). Map-marker
  teleport was initially called blocked but is **not** — the live run installed 5/5 capture hooks, and
  the earlier `0/5` was a MinHook trampoline-allocation failure (see §8). The live ON/OFF/save/load
  cases remain outstanding.
- [ ] For a mismatch, create a narrow contract note: old bytes/disassembly, new bytes/disassembly, callers, ABI, candidate replacement, test, risk, and rollback. Do not patch before this note exists.
  → mismatch notes exist for B1/B2/B3 and R1-R4 in `pe2949-a9e5ca20.md` §6. **No patching was
  performed**, exactly as required.

## Task 4: Gate, package, and report

**Files:**
- Modify only approved source/tests/docs
- Create: `docs/audits/pe2949-final-verification.md`
- Inspect: `Build_Trinity.ps1`, `SHA256SUMS.txt`, `Trinity.ini`, `Trinity.log`

- [x] Run the Release Win64 build using `VsDevCmd.bat` and CMake; record command, exit code, artifact hash.
  → `Build_Trinity.ps1` uses `vcvars64.bat` rather than `VsDevCmd.bat`, and its packaging/deploy half
  recreates `bin64\Trinity.asi` while the game was running, so configure+build were invoked directly
  instead. Exit code 0; artifact `8B2318970C21577F4621805F2DE921EB6CCEF70DDFE56DBD57C122FD0E05F9A2`.
  A local MSVC-19.51 / `rc.exe` defect had to be shimmed — see `pe2949-final-verification.md` §2.1.
- [x] Run all CTest targets. If `TrinityReadinessTests` fails due a build timestamp, record it as an independent failure; do not represent the suite as green.
  → it does fail, on the hard-coded literal `"Sep 21 2026 14:11:37"`; recorded as an independent
  failure in `pe2949-final-verification.md` §3.1. 4/5 tests pass; the suite is **not** represented as
  green.
- [x] With game closed, hash/backup installed `Trinity.asi`, deploy, hash-compare, fresh-launch, and capture startup log.
  → **done by the owner on 2026-09-21 19:58**. Pre-deploy backup in
  `docs/audits/pe2949-predeploy-backup/` (+ `SHA256SUMS-audit-backup.txt`); installed hash
  `8B231897…F9A2` **equals** the audited build hash; the startup log reports that build's stamp
  (`built Sep 21 2026 19:20:07`) and every hook resolved to the audited RVA. One crash on launch #1
  was triaged and attributed to an **NVIDIA driver fault**, not the mod. Live session evidence:
  `pe2949-a9e5ca20.md` §8 and `pe2949-final-verification.md` §4.
- [ ] Re-run the full menu route checklist in a fresh process. Mark each feature `PASS`, `STATIC ONLY`, `BLOCKED`, or `FAILED`; no blank status.
  → the game **was** launched fresh and all hooks resolved, but the control-by-control sweep was not
  performed. The full matrix **is** published with a status on every row (§5 of the final report):
  9 `PASS` (audit infrastructure), 33 `STATIC ONLY` route rows, 5 `BLOCKED` route rows, 1 `FAILED`
  (test suite), 2 `N/A`. Promotions from `STATIC ONLY` to `PASS` still need the live sweep.
- [x] Publish the final matrix with exact PE identity, AOB/RVA evidence, live test date/save state, artifact hash, backup path, and all remaining blockers.
  → `docs/audits/pe2949-final-verification.md`. Save state and live test results are explicitly
  recorded as *not yet captured* rather than invented.

## Completion Criteria

- Every visible control has one matrix row and no undocumented dependency.
- Every active PE2949 AOB/offset has an exact current-file record; unknown contracts are disabled rather than guessed.
- All enabled mutations have OFF restoration proof, including the Slot Size native setter state and `90 90 -> 74 07` pickup branch restoration.
- Build/package evidence and live semantic evidence are both present and labelled separately.
- The installed artifact hash matches the verified build hash; a recoverable pre-deploy backup exists.
