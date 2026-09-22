# Trinity — Prior Reverse-Engineering Knowledge (consolidated reference)

**Purpose.** Single reference for everything the repository's existing documents already
established about Crimson Desert / Trinity offsets, signatures, addresses, feature status,
failures and procedure. Written so a future update (or a new investigation) starts from
recorded evidence instead of re-discovery.

**Scope of this file.** A faithful digest of these documents only:

| Document | Role |
|---|---|
| `GAME_UPDATE_PLAYBOOK.md` | Current maintenance/verification playbook (source baseline commit `34d1c50`) |
| `UPDATE_PLAN_PE2944.md` | PE 2944 diagnostic plan derived from one `Trinity.log` (Ukrainian) |
| `REVERSE_ENGINEERING_GUIDE.md` | Historical architecture guide (Trinity v1.2.4; TU 1.10–1.18+) |
| `README_TU200_OFFSETS.md` | Historical TU 2.00.00 / PE 1.0.0.2625 offset archive |
| `TU200_RE_NOTES.md` | Historical TU 2.00.00 RE notes |
| `README.md` | Product readme, changelog, build/install instructions |
| `docs/superpowers/plans/2026-09-11-crimson-desert-pe-2850-compatibility-audit.md` | PE 2850 no-change audit plan + findings |
| `docs/superpowers/plans/2026-09-11-pe-2850-full-menu-audit-handoff.md` | PE 2850 full-menu audit runbook + known-good baseline |
| `docs/superpowers/plans/2026-09-11-pe-2850-full-menu-audit-results.md` | PE 2850 audit results, drift root cause, repair + deploy record (Ukrainian) |
| `docs/superpowers/plans/2026-09-14-worker-patch-and-editor-removal.md` | Worker patch + Mount editor removal implementation plan |
| `docs/superpowers/specs/2026-09-14-worker-patch-and-editor-removal-design.md` | Design for the same |
| `docs/superpowers/plans/2026-09-19-pe-2944-worker-travel-flight-completion.md` | PE 2944 completion plan |
| `docs/binary-ninja/README.md`, `snapshots/PE-2944.md`, `dossiers/travel.md` | Durable Binary Ninja KB for PE 2944 |
| `_analysis/game_sites.txt`, `_analysis/trinity_frames.txt` | Raw disassembly dumps (no revision metadata) |
| `.superpowers/sdd/2026-09-19-pe-2944-worker-travel-flight-completion/progress.md` | SDD execution ledger (the most recent per-feature runtime record) |

**Not read / not in scope.** `Trinity_Binary_Ninja_Menu_Analysis_Plan.md` (referenced by
`docs/binary-ninja/README.md` as the governing BN plan), all C++ source, and all game
binaries. No live game or Cheat Engine session was used to produce this digest.

**Requested-but-missing path.** The task listed
`docs/superpowers/plans/2026-09-11-pe-2850-compatibility-audit.md`. That exact file does not
exist; the file present in the checkout is
`docs/superpowers/plans/2026-09-11-crimson-desert-pe-2850-compatibility-audit.md`, which was
read instead (and is the "earlier baseline" the handoff document names as its spec).

**Evidence tiers used throughout.** Where the sources distinguish them, this file separates:

- **STATIC** — pattern/offset/disassembly/decompilation evidence from an on-disk or
  database image (`*.bndb`), or from source inspection.
- **LIVE-VERIFIED** — a real game process performed the action (log line, observed UI
  result, or user confirmation on a running build).
- **STATIC READY / hook installed** — the log shows a hook/locator installed; the visible
  behaviour was *not* recorded. The PE 2944 ledger explicitly rules:
  *"hook/locator rows are STATIC READY, not semantic PASS — visible behavior, OFF
  restoration, and persistence must be recorded per feature; cost if wrong is an overbroad
  release claim."* (`progress.md`, Task 6 menu-audit baseline)

---

## 1. PE 2944 status per feature

`PE 2944` = `CrimsonDesert.exe` FileVersion `1.0.0.2944`, SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`, image base
`0x140000000`, image size `0x17FCD000` (`docs/binary-ninja/snapshots/PE-2944.md`).

The **PE 2850 prior-status** column is the last recorded per-feature verdict on the previous
audited build (`2.02.00` / PE `1.0.0.2850`). Where the 2944 documents are silent about a
feature, the row says so explicitly rather than promoting a 2850 result.

### 1.1 Player, combat and workers

| Feature | PE 2944 status | PE 2944 locator / address | PE 2850 prior status | Source |
|---|---|---|---|---|
| Player stat pin (God Mode infrastructure) | Hook installed, STATIC READY (no semantic pass recorded) | `kSig_StatCommit` modern "continuous stat-pin" path; char-manager anchors | PASS (continuous HP pin) | `progress.md` (Task 6 baseline); `UPDATE_PLAN_PE2944.md` §6.4 |
| God Mode | Not separately recorded for 2944 | via stat pin + char-manager consensus | PASS | `…pe-2850-full-menu-audit-results.md` §2 tab.1 |
| One-Hit Kill | Damage hook installed, STATIC READY | `kSig_DamageApply` | PASS | `progress.md` (Task 6 baseline); 2850 results §2 tab.1 |
| Outgoing Damage multiplier | Not separately recorded for 2944 | `kSig_DamageApply` | PASS (0.0x–20.0x) | 2850 results §2 tab.1 |
| Incoming Damage multiplier | Not separately recorded for 2944 | `kSig_DamageApply` | PASS (0.0x–10.0x) | 2850 results §2 tab.1 |
| Infinite Item Durability | Not separately recorded for 2944 | `hkDamageApply` / Equip path | PASS | 2850 results §2 tab.1 |
| No Fall Damage | Not separately recorded for 2944 | `kSig_DamageApply` (fall source path) | PASS (works with Sky Arrival) | 2850 results §2 tab.1 |
| Infinite Stamina & Mount | Hook installed, STATIC READY | stat pin + PE 2944 move owner `+0x2C0` | PASS | `progress.md` (Task 6 baseline) |
| Infinite Spirit | Not separately recorded for 2944 | stat pin / char-manager | PASS | 2850 results §2 tab.1 |
| Combat timing (Easy Parry / Easy Evade backend) | Hook installed, STATIC READY, but the **menu routes are removed** | `kSig_CombatTimingEval` | EXCLUDED (9 signatures removed from menu) | `progress.md` (Task 6 baseline); 2850 audit plan ("Global Constraints") |
| Easy Parry (Just Guard) | **EXCLUDED / disabled** — "outside the active menu surface" | — | EXCLUDED | `…pe-2850-full-menu-audit-handoff.md` ("Global Constraints") |
| Easy Evade (Just Evade) | **EXCLUDED / disabled** — same | — | EXCLUDED | same |
| Super Run | **Working — LIVE-VERIFIED** (user confirmation after full restart + log) | `kSig_LocoStepper_PE2944`, function `0x14369FF60`, owner `[component+0x2C0]` | PASS after `0x2B8` fix | `progress.md` (Task 5 root cause + live evidence); `docs/binary-ninja/README.md` |
| Free Flight (Fly Up / Fly Down) | **Working — LIVE-VERIFIED**, full ON/OFF/landing/reload checklist *not* separately recorded | same locomotion stepper; input path `hooks` in `teleport.cpp` | PASS after `0x2B8` fix | `progress.md` (Task 5 live evidence) |
| Super Jump | Not re-recorded for 2944; shares the locomotion stepper with Super Run | `kSig_LocoStepper_PE2944`, `kSig_MoveUpdate` | PASS | `progress.md`; 2850 results §2 tab.1 |
| Move/position tracking (`MoveUpdate`) | Hook/locator confirmed at runtime, STATIC READY | `kSig_MoveUpdate` | Installed; semantic pending; later audited PASS via marker tests | `progress.md` (Task 6 baseline); 2850 audit plan |
| Trust Multiplier | Observers installed, STATIC READY; per-NPC/pet delta **not** recorded | `kSig_FriendlySetNpc201` / `SetPet201` + getters | PASS ("all 4 trust functions unique in PE 2850"; log `Trust Multiplier (25.0x) ENGAGED`) | `progress.md` (Task 6 baseline); 2850 results §2 tab.1 |
| Worker Max Level & Skills | **Working — LIVE-VALIDATED** (user-verified; log shows target resolved in original state then patch enabled) | patch site `0x14214BE8C`; `kSig_WorkerMaxLevelAndSkills`; `0F 85 95 00 00 00` → `E9 96 00 00 00 90` | PE 2850-only patch (2944 previously fail-closed by design) | `progress.md` ("Status correction — Task 3 Worker") |
| No Bounty (wanted state) | Available; hook installed at runtime | `kSig_EvaluateCrimeWantedState` @ `0x1425D7660` | PASS (35/35 `WantedInfo` rows zeroed) | `docs/binary-ninja/README.md`; `progress.md` (Task 6 crime status) |
| Crime UI banner / event dispatch | **Unavailable — fail-closed** (legacy dispatcher AOB zero matches) | `kSig_RegisterCrimeEvent` (0 matches); old dispatcher `0x141595BC0` unrelated | PASS on 2850 | `progress.md` (Task 6 crime status) |

### 1.2 Movement, travel and teleport

| Feature | PE 2944 status | PE 2944 locator / address | PE 2850 prior status | Source |
|---|---|---|---|---|
| Player XYZ / heading display + clipboard copy | Not re-recorded for 2944 | move owner + position `+0x90` | PASS | 2850 results §2 tab.2 |
| Sky Arrival Altitude | Not re-recorded for 2944 | `st.markerFallbackHeight` (default `650.0`) | PASS | 2850 results §2 tab.2 |
| Map-destination teleport (direct UI reader) | **Source repair built and live-verified** per ledger pre-flight; the fresh-install `queued` → `applied and verified` test is still unchecked | one direct UI destination reference @ `0x140DEFCFE`; `MarkerTeleportCanUseDestination` in `src/game/marker_teleport_logic.h` | PASS (6 real teleports logged, no snap-back) | `…pe-2944-worker-travel-flight-completion.md` Task 2; `progress.md` pre-flight |
| Legacy marker-capture hooks (5 sites) | **Fail** — all five sites still match but all five inline detours fail (`hooks=0/5`) | `kSig_MarkerPattern` / `_Player` / `_Protection` / `_OriginPrefix`; `kSig_RightClickWaypointRef` | PASS | `…pe-2944-…completion.md` status table |
| Marker teleport via hotkey | Not recorded for 2944 | configured keybind → same queued flow | Not tested (2850) | `…pe-2850-full-menu-audit-handoff.md` Task 4 |
| Saved Locations (save/rename/update/delete/warp) | Not recorded for 2944 | INI manager + `teleport::WarpTo` | PASS | 2850 results §2 tab.2 |
| Native Fast Travel menu | **Implemented but fail-closed; runtime travel semantics NOT verified** (requires *both* locators) | `kSig_TravelToNode_PE2944` @ `0x140654ED0` (presence gate) **and** `kSig_TravelDispatcher_PE2944` @ `0x1406550B0` (the native call target) | PASS (native engine path, `kSig_TravelToNode` @ `0x1405E3490`) | `docs/binary-ninja/dossiers/travel.md`; `progress.md` Task 4 |
| Old fast-travel trigger signatures (`TravelToNode`, `_Pre201`, `_Legacy`) | **Zero matches in live PE 2944 module** — deliberately not used as a fallback | `kSig_TravelToNode`, `kSig_TravelToNode_Pre201`, `kSig_TravelToNode_Legacy` | Old contract was three-arg `sub_505140(ignored, sceneId, nodeIndex)` | `…pe-2944-…completion.md`; `snapshots/PE-2944.md` ("Change summary") |
| Area-name resolver | Not recorded for 2944 | — | No match in 2.00 / 2850 (non-fatal) | `README_TU200_OFFSETS.md` §1; 2850 audit plan |
| Marker protection | Not recorded for 2944 | `kSig_MarkerProtection` | No match in 2.00 ("marker protection off", non-fatal) | `README_TU200_OFFSETS.md` §1 |
| Free Flight / Super Run locomotion locator (pre-fix state) | Was blocked (both old stepper AOBs zero live matches) | `kSig_LocoStepper`, `kSig_LocoStepper_Pre201` | PASS with owner `0x2B8` | `…pe-2944-…completion.md` status table (superseded by Task 5) |

### 1.3 Inventory, money, Abyss and restore

| Feature | PE 2944 status | PE 2944 locator / address | PE 2850 prior status | Source |
|---|---|---|---|---|
| Catalog / search / categories | Live-confirmed at runtime (catalog built; startup instruction shown) | item/string/Inventory table resolvers | PASS (6,813 items, 6,812 named, 45 categories) | `progress.md` (Task 6 baseline); 2850 results §2 tab.3 |
| Add Item (authoritative transaction) | **Working — LIVE-VERIFIED** (`server=1 client=1` recorded) | `kSig_TrItemValueCtor`, holder capture, `kSig_InvHolderInsert201`, `kSig_InvCommit`, `kSig_InvCommitPlacement201`, free-placement helpers | PASS after holder capture; previously refused | `progress.md` (Tasks 1/6); 2850 handoff baseline |
| Add category / Add All | Not recorded for 2944 | same pipeline | Not tested | 2850 handoff Tasks 5 |
| Item Editor / Set All quantities | Not recorded for 2944 | `kSig_InvGetItemQty`, `kSig_InvCommit` | PASS | 2850 results §2 tab.3 |
| Max Stack Size / Set Max Stack Value | Not recorded for 2944 | stack-size pin | PASS (999,999) | 2850 results §2 tab.3 |
| Slot Size (bag expansion) | Not recorded for 2944 | "modern continuous bucket-state guard" replaced the removed setter | PASS up to 700; **"never exceed the known safe ceiling of 700"** | `…pe-2850-full-menu-audit-handoff.md` Task 5 |
| Money controls (add/exact/1M/10M/20M/pouches/cash-in) | Not recorded for 2944 | inventory pipeline + TLS realm selector | PASS | 2850 results §2 tab.3 |
| Money bugged-stack cleanup | Not recorded for 2944 | stack consolidation | PASS | 2850 results §2 tab.3 |
| **Legacy money hooks** | **Disabled by design** — "TU 2.00+ offsets unsafe" | `gameBase + 0x16077B0 / 0x16078C0 / 0x16081D0` are INVALID | Disabled | `progress.md` (Task 6 baseline); `README_TU200_OFFSETS.md` §5 |
| Abyss sealed artifacts (scanner 0001..0150) | Not recorded for 2944 | internal key tables | PASS | 2850 results §2 tab.3 |
| Abyss materials presets (blessing/seeds/cells/vitality/spirit/breath) | Not recorded for 2944 | `AddItemByKey` | PASS | 2850 results §2 tab.3 |
| Restore Items / Lost & Sold (buyback) | Not recorded for 2944 | `Trinity_LostItems.txt` ledger | PASS (13,266-byte log) | 2850 results §2 tab.3 |
| Quest & Special item archive | Not recorded for 2944 | built-in key tables | PASS (8 subcategories) | 2850 results §2 tab.3 |
| Vendor purchase / insert planner (`oHolderInsert`) | Not recorded for 2944 | `kSig_InvHolderInsert*` | Historically caused "cannot buy items" via max-stack corruption; repaired | `REVERSE_ENGINEERING_GUIDE.md` §12.B |
| Live self-healing item icons | Not recorded for 2944 | `IconForTypeId` (engine item-def tables) | Shipped in v1.3.1 | `README.md` ("What's New in v1.3.1") |

### 1.4 Equipment, dye and appearance

| Feature | PE 2944 status | PE 2944 locator / address | PE 2850 prior status | Source |
|---|---|---|---|---|
| Equipment effect refresh | Resolved at runtime, STATIC READY | resolved `@ 0x140659510` per 2944 log | `kSig_EquipEffectRefresh` = 0 matches; `_Legacy` = 2 matches, first accepted | `UPDATE_PLAN_PE2944.md` §1; 2850 results §4 |
| Socket vector resize (`ResizeSocketVector`) | Resolved at runtime, STATIC READY | resolved `@ 0x14488C46D` per 2944 log | unique `@ 0x14479B82D` | `UPDATE_PLAN_PE2944.md` §1; 2850 audit plan |
| Character selection (Kliff / Damiane / Oongka) | Not recorded for 2944 | `SetActiveCharacter` | PASS | 2850 results §2 tab.4 |
| Repair All Gear | Not recorded for 2944 | durability slot pin | PASS | 2850 results §2 tab.4 |
| Max Refine All (+10) | Not recorded for 2944 | `kSig_EquipEffectRefresh_Legacy` | PASS | 2850 results §2 tab.4 |
| Unlock All Sockets | Runtime "socket vector" confirmed installed, semantic not recorded | `kSig_ResizeSocketVector` | PASS (5 Abyss sockets) | `progress.md` (Task 6 baseline); 2850 results §2 tab.4 |
| Per-item refine | Not recorded for 2944 | levels 0..10 | PASS | 2850 results §2 tab.4 |
| Socket insert / clear (Abyss gear) | Not recorded for 2944 | `equipgear` catalogue | PASS | 2850 results §2 tab.4 |
| Change Equipment / swap bypass | Not recorded for 2944 | `equipswap` | PASS | 2850 results §2 tab.4 |
| Dye editor (Player + Inventory) | **Removed from the active menu** (user-owned working-tree removal) | `kSig_DyeApplyBatch` family excluded from the ledger | EXCLUDED | `…pe-2850-compatibility-audit.md` ("Global Constraints"); 2850 results §1 |
| Mount / Horse editor + mount dye editing | **Removed by design** (v1.3.x cleanup) | `Dye::SetTargetMode` / `SetActiveMount` / `SavedMountSlot` deleted | n/a | `…2026-09-14-worker-patch-and-editor-removal.md` Tasks 5–6 |
| Retained mount behaviour (Infinite Stamina & Mount, mount tracking, inventory mount-items) | **Must stay in the product** (explicitly preserved) | `infMountStamina`, `GetMountActor`, `GetTrackedMountCount`, `invrestore_mount` | n/a | `…2026-09-14-worker-patch-and-editor-removal.md` Task 6 Step 3 |

### 1.5 World, time and weather

| Feature | PE 2944 status | PE 2944 locator / address | PE 2850 prior status | Source |
|---|---|---|---|---|
| Game Speed (time scale) | Confirmed at runtime, STATIC READY | `kSig_FrameTimerBody` + `FrameTimerUpdate` | PASS | `progress.md` (Task 6 baseline); 2850 results §2 tab.5 |
| Freeze Time of Day | Not recorded for 2944 | `kSig_FieldTimeTick`, `kSig_TodEngineGlobal` | PASS | 2850 results §2 tab.5 |
| Time presets / exact hour | Not recorded for 2944 | `kSig_FieldTimeRealm` | PASS | 2850 results §2 tab.5 |
| Advance / Rewind Time | Not recorded for 2944 | field-time tick | Not tested | 2850 audit plan Task 7 |
| Rain intensity | Not recorded for 2944 | `kSig_WeatherRain` | PASS | 2850 results §2 tab.5 |
| Snow intensity | Not recorded for 2944 | `kSig_WeatherSnow` | PASS | 2850 results §2 tab.5 |
| Dust / sandstorm | Not recorded for 2944 | `kSig_WeatherDust` | PASS | 2850 results §2 tab.5 |
| Wind multiplier / gust / turbulence | Not recorded for 2944 | `kSig_WindPack` | PASS | 2850 results §2 tab.5 |
| No Wind | Not recorded for 2944 | `kSig_WindPack` | PASS | 2850 results §2 tab.5 |
| Clear Distant Fog | Not recorded for 2944 | `kSig_EnvManager` (+ legacy backport) | BLOCKED during audit → fixed and double-verified in memory | 2850 results §3, §5 |
| Force Clear Sky | Not recorded for 2944 | `kSig_EnvManager` | BLOCKED → fixed | 2850 results §3, §5 |
| Clouds & fog (height / thickness / base / drift) | Not recorded for 2944 | `kSig_EnvManager` entity/cloud nodes | BLOCKED → fixed | 2850 results §3, §5 |
| Weather preset / Instant Clear Weather | Not recorded for 2944 | weather hooks + `EnvManager` | PASS | 2850 results §2 tab.5 |

### 1.6 System, UI, persistence and bootstrap

| Feature | PE 2944 status | PE 2944 locator / address | PE 2850 prior status | Source |
|---|---|---|---|---|
| DX12 / overlay / Present | **Implemented and live** | `CreateSwapChainForHwnd` + `Present` in `dxgi.dll` | PASS | `…pe-2944-…completion.md`; `REVERSE_ENGINEERING_GUIDE.md` §1 |
| MinHook backend | **Implemented and live** — "the earlier `MH_ERROR_MEMORY_ALLOC` failures are gone" | `TrinityMinHookFallbackTests` target; 20-byte prologue patching | Broken (all MinHook hooks failed) | `…pe-2944-…completion.md` status table; `UPDATE_PLAN_PE2944.md` §2 |
| Readiness / delayed-code gate | Implemented (2944 profile reachable); 2850 previously timed out 180 s | `GameplayCodeReady` sentinel profiles | Was broken (wrong profile → 180 s timeout) | `…pe-2944-…completion.md`; 2850 audit plan |
| Version detection / revision mapping | Implemented — "Version is detected as `PE 2944`; TU 2.01-compatible inventory and gameplay paths are selected" | `version_mapping.cpp`, `version_detect.cpp` | Was wrong (`2850` displayed as `1.18.02`) | `…pe-2944-…completion.md`; 2850 audit plan |
| Startup quick-start / Add Item prerequisite notice | Implemented (five-line ASCII `TRINITY | QUICK START & SUPPORT` banner) | `StartupNoticeLines`, `Mod::Initialize` | n/a | `progress.md` (console startup notice polish) |
| Build stamp | Set to `"Sep 21 2026 14:11:37"` | `src/core/build_timestamp.h` | n/a | `progress.md` (build-stamp request) |
| Keybind rebinding (menu/marker/Fly Up/Fly Down) | Not re-recorded for 2944 | `State::openKeyVk`, XInput path | PASS | 2850 results §2 tab.6 |
| Menu scale | Not re-recorded for 2944 | ImGui dynamic font rebuild 0.5x–2.5x | PASS | 2850 results §2 tab.6 |
| Item tooltips / preview size | Not re-recorded for 2944 | UI preview | PASS | 2850 results §2 tab.6 |
| Themes (6) | Not re-recorded for 2944 | Theme engine | PASS | 2850 results §2 tab.6 |
| PlayStation icons | Not re-recorded for 2944 | Controller UI | PASS | 2850 results §2 tab.6 |
| Localization (9–10 languages) | Not re-recorded for 2944 | `Trinity_*.ini` auto-detect | PASS | 2850 results §2 tab.6; `README.md` v1.3.0 |
| Font engine (built-in + custom TTF/OTF) | Not re-recorded for 2944 | font scanner | PASS | 2850 results §2 tab.6 |
| FPS counter | Not re-recorded for 2944 | DX12 hook | PASS | 2850 results §2 tab.6 |
| Debug console window | Not re-recorded for 2944 | Win32 console toggle | PASS | 2850 results §2 tab.6 |
| Auto Save Features / `Trinity.ini` | Not re-recorded for 2944 | Settings engine | PASS | 2850 results §2 tab.6 |
| Resize / HDR / Frame Generation transitions | Not re-recorded for 2944 | DX12 present path | Not tested | 2850 audit plan Task 8 |

**Row count for §1: 88 feature rows** — §1.1 player/combat/worker = 20, §1.2
travel/movement = 11, §1.3 inventory/money/Abyss/restore = 15, §1.4 equipment/dye = 12,
§1.5 world/time/weather = 13, §1.6 system/UI/persistence = 17. That counts every distinct feature
the source documents discuss. Groups where the documents only ever produced a group-level verdict
(e.g. "world environment controls") are expanded into their named controls, with the group verdict
carried by each row.

**Coverage caveat.** The only *systematic* full-menu ledger in the repository is the PE 2850
audit (`…full-menu-audit-results.md`: "144" UI controls, 5 root tabs + 33 submenus). No
equivalent full-menu ledger exists for PE 2944. For 2944, the authoritative source is
`progress.md`, whose Task 6 baseline explicitly rules that hook/locator presence is not a
semantic pass.

---

## 2. Known addresses and what they are

### 2A. PE 1.0.0.2944 (current target)

| Address (hex) | Claimed to be | Confidence | Source doc |
|---|---|---|---|
| `0x140000000` | Image base | Confirmed (PE headers) | `snapshots/PE-2944.md` |
| `0x17FCD000` | SizeOfImage (402,444,288 bytes) | Confirmed | `snapshots/PE-2944.md` |
| `0x14E94FFA0` | PE entry point | Confirmed | `snapshots/PE-2944.md` |
| `0x1406550B0` | `VIBE_FastTravel_TryAcceptRequest` — final post-confirmation **native Fast Travel dispatcher**; the function Trinity calls | Confirmed static (unique 44-byte AOB over all 12 sections); live call unverified | `dossiers/travel.md`; `progress.md` Task 4 |
| `0x140654ED0` | `VIBE_FastTravel_SelectDestination` — world-map selection/validation gate; used **only** as a presence/contract check, never called | Strong candidate (2 independent sources; 1 inherited claim unproven) | `dossiers/travel.md` |
| `0x140DBC6C0` | `VIBE_FastTravel_FlushPendingRequest` — CommonModalMessage affirmative callback / queued-request consumer | Confirmed (locator + disassembly) | `dossiers/travel.md`; `progress.md` Task 4 |
| `0x140A9A8D0` | `VIBE_FastTravel_Execute` — performs the actual travel (end of the chain) | Confirmed (locator + existing decompilation) | `dossiers/travel.md` |
| `0x1404A2630` | `VIBE_LevelGimmickSceneObjectInfoTable_GetById` — scene/destination registry resolver (lazy-loads its data row; game thread only) | Confirmed | `dossiers/travel.md` |
| `0x140DBAC20` | `sub_140DBAC20` — world-map UI dispatch; holds the only travel callsite. Deliberately **not** renamed (also drives `SkillTreePanel`) | Candidate | `dossiers/travel.md` |
| `0x140D9E858` | Third dispatcher caller `sub_140D9E6C0` (likely mercenary/companion travel UI) | Unanalysed | `dossiers/travel.md` §1, §6.2 |
| `0x140DBC374` `lea rcx,[rbp+0x1320]` | Caller's stack-local `sceneId` copy passed in `RCX` to the selection gate | Confirmed static | `dossiers/travel.md` |
| `0x140DBC37B` | `call VIBE_LevelGimmickSceneObjectInfoTable_GetById` | Confirmed static | `dossiers/travel.md` |
| `0x140DBC380` | `cmp ebx, dword [rax+0x28]` → `nodeIndex < nodeCount` bound check | Confirmed static | `dossiers/travel.md` |
| `0x140DBC385` / `0x140DBC388` / `0x140DBC38A` / `0x140DBC38F` | `mov r8d, ebx` / `mov edx, edi` / `call SelectDestination` / `test al, al` — the caller's register setup that fixes the gate ABI | Confirmed static | `dossiers/travel.md`; `progress.md` Task 4 |
| `0x140DBC393` | `mov dword [rsi+0x960], edi` / `[rsi+0x964], ebx` — stages the pending sceneId/nodeIndex | Confirmed static | `dossiers/travel.md` |
| `0x140DBC7BA` / `0x140DBC7BD` / `0x140DBC7E8` | `xor r9d,r9d` then the scene-path and mercenary-path calls into the dispatcher | Confirmed static | `dossiers/travel.md` |
| `0x1406550B5`, `0x1406550B9`, `0x1406550D4`, `0x1406550D7`, `0x1406550DA` | Dispatcher prologue proof: EDX spill, RCX spill, `mov edi,r9d`, `mov r15d,r8d`, `xor ebx,ebx` | Confirmed static | `dossiers/travel.md` |
| `0x140655344` / `0x140655348` | `mov bl,1` / `xor bl,bl` — return value built in `BL` | Confirmed static | `dossiers/travel.md` |
| `0x140655566` | `call VIBE_FastTravel_Execute` from inside the dispatcher | Confirmed static | `dossiers/travel.md` |
| `0x14064F490` | `VIBE_ScopeAttacher_ClientActor_InitGlobal` — **ignores RCX**, uses the global client actor (hence `context = nullptr` is safe) | Confirmed static | `dossiers/travel.md`; `progress.md` Task 4 |
| `0x14064F4D0` | Field attacher | Confirmed static | `dossiers/travel.md` |
| `0x1415DF310` | Copies one `0xD8`-byte node entry | Confirmed static | `dossiers/travel.md` |
| `0x1417EDAC0` | Decisive un-named callee of the selection gate (888 bytes, 31 basic blocks); returns the boolean that becomes `AL` | Unidentified / claim unproven | `dossiers/travel.md` §6.1, §6.4 |
| `0x14369FF60` | PE 2944 locomotion stepper; move owner is `[component+0x2C0]` (**not** `+0x2B8`) | Confirmed static (40-byte prologue, unique in the 85,893,120-byte `.code`) **and** live hook installed | `docs/binary-ninja/README.md`; `progress.md` Task 5 |
| `0x14214BE8C` | Worker six-byte patch site (`0F 85 95 00 00 00`); original state observed live, then "patch enabled" | LIVE-VALIDATED | `progress.md` ("Status correction — Task 3 Worker") |
| `0x1425D7660` | Wanted-state evaluator; the valid No Bounty path (live entry MinHook-jumped, so the on-disk signature is the locator) | Confirmed hook installed | `docs/binary-ninja/README.md`; `progress.md` Task 6 |
| `0x140DEFCFE` | Exactly one direct UI map-destination reference; resolves the marker shown by the menu | Live locator verified | `…pe-2944-…completion.md` status table |
| `0x141595BC0` | The **old** crime-event dispatcher VA — "unrelated code in this build" | Ruled out | `progress.md` Task 6 |
| `0x14066B387`, `0x14172EBC9`, `0x14172EE18`, `0x14179B2CF`, `0x1417ED7B5`, `0x1420B7103`, `0x1422E579F`, `0x1429598FD`, `0x14CC42ABD` | All nine direct callers of the wanted-state evaluator; each only reads/compares the wanted state as a gate. **None** receives the old four-argument crime-dispatch contract; none promoted to a replacement hook | Inspected and ruled out | `progress.md` Task 6 |
| `0x140659510` | `EquipEffectRefresh` resolved (2944 log) | Runtime log, not a signature | `UPDATE_PLAN_PE2944.md` §1 |
| `0x14488C46D` | `ResizeSocketVector` resolved (2944 log) | Runtime log, not a signature | `UPDATE_PLAN_PE2944.md` §1 |
| `0x7FFC83D40000` | `Trinity.asi` live base (one recorded session), image size 2,220,032 | Runtime observation (session-specific) | `progress.md` Task 1 |
| file `0x6542D0` → VA `0x140654ED0`; file `0x6544B0` → VA `0x1406550B0` | On-disk offset → VA mapping for the two shipping PE 2944 locators | Confirmed static | `progress.md` Task 4 |
| `0x146D6AA40` (`data_146D6AA40`) | Travel-target id map (bucketCount `+0x68`, buckets `+0x78`, nodes `+0x80`) | **Inherited only** — not re-derived in the 2944 pass | `dossiers/travel.md` §2, §6.6 |
| `0x146CE5B28` (`data_146CE5B28`) + `sub_14E877080` | Travel-target id source | Inherited only | `dossiers/travel.md` §6.6 |
| `0x140000000..0x149000000` | Range suspected of being a large committed/reserved region that blocks MinHook's ±1 GiB pool window | **Unconfirmed hypothesis** — requires live diagnostics | `UPDATE_PLAN_PE2944.md` §2 |

**PE 2944 locator patterns (shipping, `src/game/offsets.h`):**

- `kSig_TravelDispatcher_PE2944` (44 bytes) →
  `48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 41 56 41 57 48 8D AC 24 50 FE FF FF 48 81 EC B0 02 00 00 41 8B F9 45 8B F8 33 DB`
  — exactly 1 match in the whole image (`dossiers/travel.md`).
- `kSig_TravelToNode_PE2944` (31 bytes) →
  `89 54 24 10 48 89 4C 24 08 53 55 56 57 41 54 41 56 41 57 48 81 EC 90 00 00 00 41 8B D8 33 FF`
  — exactly 1 match. *Documentation nit recorded in the dossier: `offsets.h` prose calls it a
  "32-byte prologue"; the string is 31 byte-tokens.*
- `kSig_LocoStepper_PE2944` — 40-byte prologue, exactly one match in `.code`
  (`progress.md` Task 5).
- `kSig_WorkerMaxLevelAndSkills` = `0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5`, patch bytes
  `kWorkerPatchOriginal` `0F 85 95 00 00 00` → `kWorkerPatchEnabled` `E9 96 00 00 00 90`
  (`…2026-09-14-worker-patch-and-editor-removal.md` Task 3).

### 2B. PE 1.0.0.2850 / TU 2.02.00 (previous audited build)

Executable: size `375,511,960`; SHA-256
`BCBF623AD5690147DC462AEAED5B4F97BD73296BA0D6AB54663586E7088B1C0E`; PID `31972`
(audit plan) and PID `5732`, ImageSize `0x16B10000` (results doc).

| Address (hex) | Claimed to be | Confidence | Source doc |
|---|---|---|---|
| `0x1405E3490` | `kSig_TravelToNode` (Fast Travel) | Unique static | 2850 audit plan / results §2 tab.2 |
| `0x142074BA0` | `kSig_InvGetHolder` | Unique live/unhooked | 2850 audit plan |
| `0x14234E630` | `kSig_InvHolderInsert201` | Unique live/unhooked | 2850 audit plan |
| `0x142078BC0` | `kSig_InvCommit` (transaction commit) | Unique live/unhooked | 2850 audit plan |
| `0x14085834A` | `kSig_InvCoreGlobal` | Unique (after policy fix) | 2850 audit plan |
| `0x1423507B0` | `kSig_TrItemValueCtor` | Unique | 2850 audit plan / results §2 tab.3 |
| `0x1420788A0` | `kSig_InvCommitPlacement201` | Unique | 2850 audit plan / results §2 tab.3 |
| `0x140418C60` | `kSig_InvFreePlacements` | Unique | 2850 audit plan |
| `0x1487D6BC0` | `kSig_InvFreePlacements201` | Unique | 2850 audit plan / results §2 tab.3 |
| `0x141232620` | `kSig_LocStringGet` | Unique | 2850 audit plan |
| `0x140A54C53` | `kSig_FrameTimerBody` match; `FrameTimerUpdate` at `0x140A54C10` | Unique static | 2850 audit plan / results §2 tab.5 |
| `0x1420303B4` | `kSig_FieldTimeRealm` | Unique | 2850 audit plan / results §2 tab.5 |
| `0x142C159FA` | `kSig_TodEngineGlobal` | Unique | 2850 audit plan / results §2 tab.5 |
| `0x142AAF030` | `kSig_FriendlyNpcTrustWriter` | Unique | 2850 audit plan |
| `0x14276C9A7` | Character-manager anchor 1 (only survivor of 6) / `kCharMgrAnchor_0` | Unique static | 2850 audit plan |
| `0x146C2DF18` | Char-manager global resolved live (→ manager `0x2314BB9A560` → player entity `0x2314BAE3F00`) | Runtime heap values, session-specific | 2850 results §2 tab.1 |
| `0x141719850` | `kSig_DamageApply` (One-Hit Kill, God Mode, multipliers, No Fall Damage) | Unique static | 2850 results §2 tab.1 |
| `0x14251C140` | `kSig_EvaluateCrimeWantedState` | Unique static | 2850 results §2 tab.1 |
| `0x141E4F170` | `kSig_RegisterCrimeEvent` | Unique static | 2850 results §2 tab.1 |
| `0x146C4EE70` | `WantedInfo` table (35 crime rows zeroed) | Runtime global | 2850 results §2 tab.1 |
| `0x1435C0B00` | `kSig_LocoStepper` (Super Run, Super Jump, Free Flight) | Unique static | 2850 results §2 tab.1 |
| `0x1435C0B76` | `lea rbx, [rcx+0x2B8]` — **move-owner offset proof**; position `+0x90` | Unique static | 2850 results §2 tab.1 |
| `0x144191450` | `kSig_MoveUpdate` (Super Jump) | Unique static | 2850 results §2 tab.1 |
| `0x141E2C090` / `0x14D87BCB0` | `kSig_FriendlySetNpc201` / `kSig_FriendlySetPet201` | Unique static | 2850 results §2 tab.1 |
| `0x140D6786E` | Right-click map-waypoint destination reference (`kSig_RightClickWaypointRef`) | Unique hidden pattern | 2850 audit plan / results §2 tab.2 |
| `0x14C4542E2` | `kSig_MarkerProtection` | Static | 2850 results §2 tab.2 |
| `0x146C2DA48` | Map-marker source global → `[+0xA8]` → coords `[+0x20]`/`[+0x28]` | Runtime global | 2850 results §2 tab.2 |
| `0x146C2E2E8` | `iteminfo` table (6,813 items / 6,812 named / 45 categories) | Runtime global | 2850 results §2 tab.3 |
| `0x141763160` | `kSig_InvGetItemQty` (Item Editor) | Unique static | 2850 results §2 tab.3 |
| `0x1405E6F20` | `kSig_EquipEffectRefresh_Legacy` **first** of 2 matches; the correct equip-slot refresh function | Static, accepted by first-match (ambiguity unresolved) | 2850 results §4 |
| `0x14099E140` | `kSig_EquipEffectRefresh_Legacy` **second** match — "dead match" | Ruled out for use; a unique signature was recommended | 2850 results §4 |
| `0x14479B82D` | `kSig_ResizeSocketVector` ("Unlock All Sockets") | Unique static | 2850 results §2 tab.4 |
| `0x1409BD050` | `kSig_FieldTimeTick` (Freeze Time) | Unique static | 2850 results §2 tab.5 |
| `0x143CD2B20` / `0x143CD2BD0` / `0x143CD2C80` / `0x143CCBF20` | `kSig_WeatherRain` / `_Snow` / `_Dust` / `kSig_WindPack` | Unique static | 2850 results §2 tab.5 |
| `0x14391E920` | `kSig_EnvManager` drift site (tail differs: `lea rcx,[rbp+0x340]` instead of `mov rdx,rdi; mov rcx,[rax+0xEE0]`) | Confirmed in memory | 2850 results §3 |
| `0x146C1ADC0` | `pEnvManager` global read at the drift site → `EnvManager 0x2307AB44080`; vtable `0x145BF9840`; `vtable[0x40] -> 0x144059B70` | Runtime values, session-specific | 2850 results §3 |
| `0x140C5579F` | The **updated** `kSig_EnvManager` "exact modern PE 2850 pattern" unique match (repair record) | Confirmed match (doc does not reconcile with `0x14391E920`) | 2850 results §5.2 |
| `0x146B52FF0` → `0x23079A22100` → `0x2306B705000` → `0x2307B680000` → `0x2308AC95628` | Verified pointer chain `g_pEnvManager` → `EnvManager` → `entity` → `weatherState` → `cloudNode` | Live double-verification | 2850 results §5.2 |
| `0x146276CF0` / `0x146276D40` | Money wrapper global keys (2850-era candidate list) | Candidate | 2850 audit plan |
| `0x144899FB0` / `0x144899FE0` / `0x1402ED7A0` / `0x14115BB10` | Money wrappers / lookup helper / worker function (PE 2625 list, carried as candidates) | Candidate, live-testing required | `README_TU200_OFFSETS.md` §5 |

### 2C. PE 1.0.0.2625 / TU 2.00.00 (historical archive — do not reuse)

Image base `0x140000000`; live code section `.xpdata` VA `0x140001000`–`0x14496C000`;
Steam Build ID `24934353`.

| Address (hex) | Claimed to be | Confidence | Source doc |
|---|---|---|---|
| `0x142093010` | `TrItemValue` constructor (structure layout derived from it) | Historical, confirmed for that capture | `README_TU200_OFFSETS.md` §2; `TU200_RE_NOTES.md` §4 |
| `0x142091150` | `InvHolderInsert` | Historical | `README_TU200_OFFSETS.md` §3 |
| `0x141DF9CF0` | `InvCommitPlacement` | Historical | `README_TU200_OFFSETS.md` §3 |
| `0x140948158` | `kSig_GameSpeed` unique (`vmovss` value at match+35) | Historical | `README_TU200_OFFSETS.md` §1; `TU200_RE_NOTES.md` §2 |
| `0x14282011A` | `kSig_TodEngineGlobal` unique | Historical | same |
| `0x140AEBA93` | `kSig_EnvManager` first hit; field manager `+0xEE0`; global pointer `0x14625AF90` | Historical | same |
| `0x1410D5200` | `kSig_LocStringGet` unique | Historical | same |
| `0x140AC0FB0` | `kSig_JustCore` unique candidate (under verification) | Candidate | same |
| `0x143BAF7E0` | `MoveUpdate` hook — legacy signature matched uniquely and accurately | Historical (2.00) | `TU200_RE_NOTES.md` §1 |
| `0x140648390` → `0x141BDA250` | `FriendlySetNpc` caller → wrapper → leaf | Historical | `README_TU200_OFFSETS.md` §1; `TU200_RE_NOTES.md` §3 |
| `0x141BDA120` (`+0x18`) / `0x141BDB390` (`+0x38`) | NPC map helper / Pet helper | Historical | same |
| `0x144899FB0` / `0x144899FE0` | Money wrappers; globals `0x146276CF0` / `0x146276D40` | Candidate | `README_TU200_OFFSETS.md` §5 |
| `0x1402ED7A0` / `0x14115BB10` | Money lookup helper / worker function | Candidate | same |
| `gameBase + 0x16077B0`, `+0x16078C0`, `+0x16081D0` | Legacy money hooks — **INVALID and disabled** | Ruled out | `README_TU200_OFFSETS.md` §5; `TU200_RE_NOTES.md` §3 |

### 2D. Historical TU 1.x constants and addresses (background only)

| Value | Claimed to be | Confidence | Source doc |
|---|---|---|---|
| `kOff_Teb_TlsPointer = 0x58` | TEB.ThreadLocalStoragePointer | Historical | `REVERSE_ENGINEERING_GUIDE.md` §2 |
| `kTls_RealmFlag = 498` (`0x1F2`) | TLS realm flag (0 = Client, 1 = Server) | Historical (superseded by `0x1EC`/`0x1FD` policies) | same |
| `kMinPointer = 0x10000000` | Virtual address floor — pointers below are discarded | Historical design rule | `REVERSE_ENGINEERING_GUIDE.md` §3 |
| `sub_22E6330` | Char-manager anchor 1 comment site | Historical comment | `REVERSE_ENGINEERING_GUIDE.md` §4 |
| `sub_2F49550` | Locomotion sub-step driver (velocity servo) | Historical | `REVERSE_ENGINEERING_GUIDE.md` §7 |
| `0x11CD047F` (= `298648703`) | Server validation error from forced stack overrides / trust ledger desync | Historical, repeated in playbook and 2944 plan | `REVERSE_ENGINEERING_GUIDE.md` §12.A; `GAME_UPDATE_PLAYBOOK.md` §4 |
| `0xC0000005` / `0xC000001D` / `0xC00000FD` | Access violation / illegal instruction / stack overflow — the three exceptions Trinity's crash logger handles | Corroborated by raw dump `trinity_frames.txt` | `_analysis/trinity_frames.txt` |

### 2E. Addresses and rules explicitly ruled out

| Item | Why it is ruled out | Source doc |
|---|---|---|
| `gameBase + 0x16077B0 / 0x16078C0 / 0x16081D0` | "Legacy Hooks … are **INVALID** and disabled." | `README_TU200_OFFSETS.md` §5 |
| `0x14099E140` | Second `EquipEffectRefresh_Legacy` match; dead match. First-match selection was only tolerated, and a unique signature was recommended. | 2850 results §4 |
| `0x141595BC0` | Old crime-event dispatcher — unrelated code in PE 2944 | `progress.md` Task 6 |
| `0x1417EDAC0` → *candidate only* | Decisive callee of the selection gate, unidentified; the "opens `CommonModalMessage`" claim is **unproven** | `dossiers/travel.md` §6.1 |
| `.debug` / `.debug$P` section exclusion | The TU 2.00 exclusion was a one-capture observation, "not a permanent rule"; PE 2944 has no section named exactly `.debug`, so **all 12 sections are scanned** | `README_TU200_OFFSETS.md` preamble; `snapshots/PE-2944.md` |
| Old absolute VAs, TU labels, TLS constants from `README_TU200_OFFSETS.md` / `TU200_RE_NOTES.md` | "Treat their addresses, TLS constants, support claims and version assumptions as historical until corroborated" | `GAME_UPDATE_PLAYBOOK.md` endnote + §2F |
| `kSig_StatCommit` re-find for 2944 | "`kSig_StatCommit` … **НЕ треба** re-find: його видалили ще в TU 2.01" — removed in TU 2.01; the modern continuous stat-pin branch is correct | `UPDATE_PLAN_PE2944.md` §3 (warning box), §10 |
| First-match acceptance for `kCharMgrAnchors` | Consensus is mandatory; `InstallHook` first-match warnings are "a failed diagnosis" | `GAME_UPDATE_PLAYBOOK.md` §2D, §3 |
| Hook of `kSig_LeaR8Rip` / `kSig_MovR8Rip` | "inspect their predicates, never hook them" | `GAME_UPDATE_PLAYBOOK.md` §2A |
| `tools/deploy_master.ps1` as a generic deploy | "its hard-coded paths and old version markers make it unsuitable for a future repair without a fresh review" | `GAME_UPDATE_PLAYBOOK.md` §2E |
| `Trinity_SafeMode.txt` bit values | "not part of the current source tree, so first verify that the active build still implements it" | `README_TU200_OFFSETS.md` §8; `TU200_RE_NOTES.md` §7 |
| `tools/scan_signatures_200.py`, `audit_live_200.py`, `deep_analysis_200.py`, `find_sigs_200.py`, `disasm.py`, `find_unlocked_field.py`, `dump_bucket_types.py`, `.agents/skills/crimson-binary-inspector` | "The listed scripts are not present in this checkout. Do not treat these paths as an available workflow." | `README_TU200_OFFSETS.md` §9; `TU200_RE_NOTES.md` §6 |

### 2F. Revision-dependent constants, struct offsets and magic values

| Value | Meaning | Applies to | Source doc |
|---|---|---|---|
| `0x2B8` | Movement-component owner offset | PE 2760 / 2850 (selected); contradicted for 2944 | `GAME_UPDATE_PLAYBOOK.md` §2B; 2850 handoff baseline |
| `0x2C0` | Movement-component owner offset | **PE 2944** (live-proven) | `docs/binary-ninja/README.md`; `progress.md` Task 5 |
| `0x298` | Movement owner legacy fallback | Older revisions (must not be assumed) | `UPDATE_PLAN_PE2944.md` §5 |
| `0x1FD` | TLS realm flag offset | PE 2760 | `GAME_UPDATE_PLAYBOOK.md` §2B; `UPDATE_PLAN_PE2944.md` §5 |
| `0x1EC` | TLS realm selector | PE 2850 (live disassembly) | 2850 handoff baseline |
| `0x1F2` | TLS realm flag legacy | Older revisions / incorrect fallback for 2850 & 2944 | `UPDATE_PLAN_PE2944.md` §5 |
| Realm flag `0x1FD` in code | Corroborated in raw dump at `0x1434E530D` (`mov ecx, 0x1fd`) and `0x1412F46D4` (`mov edx, 0x1fd`), each followed by `gs:[0x58]` TLS dereference | Unattributed image | `_analysis/game_sites.txt` |
| `0` | Inventory core-global `mov` offset | PE 2760 / 2850 (modern) | `UPDATE_PLAN_PE2944.md` §5 |
| `0x15` | Inventory core-global `mov` offset | Legacy (wrongly selected for 2944) | same |
| `0x428` | `ItemDef` BucketType (`uint16_t`), relocated from `+0x418`, **not** `+0x420`; binary-confirmed via `movzx r, word [def+0x428]` + `cmp [bucket+0x10], r` (225 unique hits) | revision ≥ 2625 | `README_TU200_OFFSETS.md` §3 |
| `0x418` / `0x410` | Older BucketType offsets | TU 1.10–1.18 | `REVERSE_ENGINEERING_GUIDE.md` §8 |
| `0xC8` / `0xC0` | Inventory slot stride (modern / TU ≤ 1.15) | historical | `REVERSE_ENGINEERING_GUIDE.md` §8; `UPDATE_PLAN_PE2944.md` §5 |
| `0xD8` / `0xE0` | Inventory placement stride (legacy / modern) — "перевірити" (verify) | 2944 pending | `UPDATE_PLAN_PE2944.md` §5 |
| `+0x08` typeId, `+0x10` quantity, `+0xD8` slotIdx | Inventory slot field offsets | 2944 candidate | same |
| `+0x18` / `+0x20`; `type 0x10`, `maxSlots 0x14`, `used 0x12`, `expand 0x1A` | Holder buckets and bucket fields | 2944 candidate | same |
| `0x90` stride; `+0x08/+0x18/+0x20/+0x28/+0x30`; `root+0x58` | Player stat entry layout | 2944 candidate | same |
| `manager+0xB8/+0xC0`, `owner+0xA0 → +0xD0`, `owner+0x88`, `owner+0x68`, `actor+0x20`, `marker+0x18` | Char-manager list, possessor round-trip, type descriptor, actor, marker, root | current layout | `GAME_UPDATE_PLAYBOOK.md` §2A; `UPDATE_PLAN_PE2944.md` §5 |
| `+0x90` / `+0xC0` / `+0xD0` / `+0x1A0` | Movement position / desired velocity / velocity / second teleport destination | current | `GAME_UPDATE_PLAYBOOK.md` §2A, §4 |
| `+0xA8`, then `+0x20/+0x24/+0x28` | Map waypoint global → destination → XYZ (read separately in `map_marker.h`) | current | `GAME_UPDATE_PLAYBOOK.md` §2A |
| Readback tolerance `0.5f`; three attempts | Teleport write verification (`MarkerStatus::Queued` means only queued) | current | `GAME_UPDATE_PLAYBOOK.md` §2B, §4 |
| `0x64` / `0x68` | Time struct delta / scaledDelta | 2944 candidate | `UPDATE_PLAN_PE2944.md` §5 |
| `+0x2F8`, `+0x3D0`, `+0x3D4`, `+0x3D8` | TOD manager / currentHour / lower / upper | 2944 candidate | same |
| `+0x28` | `kOff_SceneDesc_NodeCount` | 2850 confirmed | 2850 results §2 tab.2 |
| `record+0x08`, `+0x10`, `+0x18`, `+0x20`, `+0x28`, `+0x49`, `+0x6C` | Scene record: stringKey, isBlocked, levelName, nodeArray, nodeCount, useTeleport, isEmpty | PE 2944 | `dossiers/travel.md` |
| `0xD8` | Travel node stride; `+0x958` key, `+0x960` sceneId, `+0x964` nodeIndex, `+0x968` mercenary handle; `[mapUI+0x5C0]` → `+0xC8` / `+0xD0` (kind byte vs `0xE`) | PE 2944 | `dossiers/travel.md` |
| `+0x60` (vector ptr), `+0x68` (size), `+0x6C` (capacity), `+0x70` (unlocked count — **low byte only**) | `TrItemValue` socket fields (modern) | 2.00 / 1.18+ | `README_TU200_OFFSETS.md` §2 |
| `0xFFFFFF02` (live `65283`) | Observed `+0x70` value; upper bytes are engine flags → "write to the **LOW BYTE ONLY**" | 2.00 live | `TU200_RE_NOTES.md` §8 |
| `+0x58` / `+0x60` (SocketData), `+0x68` / `+0x70` (UnlockedCount) | Legacy / modern socket offsets (8-byte shift in TU 1.18+) | historical | `REVERSE_ENGINEERING_GUIDE.md` §12.D |
| `6` (`kSocketRec_Stride`) | Abyss socket record stride; `+0x00` = `uint16_t` rune type id, `0xFFFF` = empty | historical | `REVERSE_ENGINEERING_GUIDE.md` §9.C |
| `0x0C24` | Gem TypeID used in the verified 6-byte socket injection (Tag 0, Slot 3) | 2.00 live | `TU200_RE_NOTES.md` §8 |
| Trust record `+0x00` key, `+0x04` group, `+0x10` i64 value | Legacy trust layout; `key == 0` = system baseline | historical | `REVERSE_ENGINEERING_GUIDE.md` §12.A |
| Trust value `+0x20`, cap `100` | Trust record layout | 2944-era candidate ("Перевірити layout після re-find") | `UPDATE_PLAN_PE2944.md` §5 |
| `kFriendly_Max` / `20.0` clamp | Max trust delta per transaction to avoid error `298648703` | historical | `REVERSE_ENGINEERING_GUIDE.md` §12.A |
| `1999` (`kMaxInventorySlots`) | Baseline UI inventory limit | source baseline commit `34d1c50` | `GAME_UPDATE_PLAYBOOK.md` §4 |
| `700` | "known safe ceiling" for Slot Size | 2850 | `…pe-2850-full-menu-audit-handoff.md` Task 5 |
| `650.0` | Default `st.markerFallbackHeight` (Sky Arrival) | current | 2850 results §2 tab.2 |
| Status IDs `0` health, `1` stamina, `3` spirit, `48` mount sprint / wyvern breath | Damage/stat routing (`REVERSE_ENGINEERING_GUIDE.md` §6) | historical | `REVERSE_ENGINEERING_GUIDE.md` §6 |
| Status IDs `17` mount health, `19` mount sprint, `22` wyvern flight | Competing status-ID table ("Struct Offset Resolution") | historical — **conflicts with the row above** | `REVERSE_ENGINEERING_GUIDE.md` §12.C |
| Type IDs `53935`, `6324`, `5450..5468` → Damiane; `6560`, `6550..6570` → Oongka | Companion identification by weapon TypeID range | historical | `REVERSE_ENGINEERING_GUIDE.md` §12.D |
| `1.0.0.2474` | PE `dwFileVersion` statically pinned across Steam title updates — the "static PE header trap" | historical | `REVERSE_ENGINEERING_GUIDE.md` §11; `UPDATE_PLAN_PE2944.md` §0 |
| `0x40000000` (`MAX_MEMORY_RANGE`), `0x1000` | MinHook free-block search window and block size (from `build-clean/_deps/minhook-src/src/buffer.c`) | mechanism | `UPDATE_PLAN_PE2944.md` §2 |
| `180` seconds | Readiness timeout observed | 2850 / 2944 log | `GAME_UPDATE_PLAYBOOK.md` §2B; 2850 audit plan |
| `0.67 Hz` (`1500 ms`) | Lost/sold item tracker main-thread delta cadence | historical | `REVERSE_ENGINEERING_GUIDE.md` §8 |
| `Trinity_SafeMode.txt` bits `1..16384` | Historical subsystem/tick bypass diagnostic bits | historical, unverified in current tree | `README_TU200_OFFSETS.md` §8; `TU200_RE_NOTES.md` §7 |

---

## 3. Confirmed failures and why

Grouped by the reason the documents give. Quotes are verbatim.

### 3.1 PE 2944 failures / deliberate removals

**Systemic MinHook allocation failure (PE 2944 launch of Trinity v1.3.5.4) — later resolved.**

> "**`MH_CreateHook failed (MH_ERROR_MEMORY_ALLOC)`** — **системний** збій алокатора MinHook,
> а НЕ окремих сигнатур. Він однаково вбив **усі** хуки MinHook, поки всі «нативні» резолви
> (pointer-walk / direct call) спрацювали."
> (`UPDATE_PLAN_PE2944.md` §0.2)

Diagnosis: MinHook looks for a free `0x1000` block only within ±1 GiB of the target
(`MAX_MEMORY_RANGE = 0x40000000`); if no free page exists in that window, `AllocateBuffer`
returns `NULL` and `MH_CreateHook` returns `MH_ERROR_MEMORY_ALLOC` (`UPDATE_PLAN_PE2944.md` §2).
**Status now:** "The earlier `MH_ERROR_MEMORY_ALLOC` failures are gone; current player,
inventory, frame-timer, equipment, and trust hooks install."
(`…2026-09-19-pe-2944-worker-travel-flight-completion.md` status table).

**Cascading early returns hid additional breakage (PE 2944 log).**

> "**`src/game/teleport.cpp:1772-1774`** — якщо `movement-update` не хукнувся, `return false`,
> тому fast-travel trigger, scene/area resolver, loco-stepper і map-marker підсистема
> **взагалі не ініціалізуються**"
> "**`src/game/inventory.cpp:2072-2078`** — якщо `item-count accessor` (primary + legacy) не
> хукнувся, `return false`, тому holder-resolver, Add Item примітиви і money-хуки
> **не запускаються**"
> (`UPDATE_PLAN_PE2944.md` §7)

**Signature drift / NOT FOUND on 2944** (`UPDATE_PLAN_PE2944.md` §1, §3): `kSig_StatCommit`
(expected — removed in TU 2.01), `kSig_RegisterCrimeEvent`, `kSig_InvGetItemQty_Legacy`,
`kSig_FieldTimeTick_Pre201`, `kSig_WindPack_Pre201`, the whole `kSig_Friendly*` set
(NPC/pet 201 setters and getters, legacy setters, trust writer, alert dispatcher).
Char-manager anchors: only **1 of 6** verified.

**Map-marker capture hooks (PE 2944).**

> "Five old marker-capture sites still match, but all five inline detours fail. The new
> readiness rule permits the verified direct UI reader plus origin and movement owner, instead
> of rejecting `hooks=0/5`."
> (`…2026-09-19-pe-2944-worker-travel-flight-completion.md` status table)

**Native Fast Travel (PE 2944, pre-recovery).**

> "All three old trigger signatures return zero matches in the live PE 2944 module. The menu is
> correctly disabled rather than calling an unknown function."
> (same status table)

**Fast Travel first recovery attempt was a false success.**

> "Multiple Trinity menu requests dispatched to `0x140654ED0` and returned accepted
> (scenes/nodes: 27/0, 0/0, 6/0, 148/0-3), but the player did not travel. Static continuation at
> the real UI callsite proves that this function is only a selection/validation step … PE2944
> Fast Travel was returned to fail-closed status; do not ship a misleading success toast."
> (`progress.md` Task 4 semantic result)

**Locomotion owner offset (PE 2944).**

> "Trinity recorded `moveOwnerOffsetFound=0x2C0` while the current mapping used +0x2B8 and
> consequently logged `isPlayer=0`; that false identity gate bypassed both Free Flight and
> Super Run."
> (`progress.md` Task 5 root cause)

**Worker patch was unsafe on the old branch bytes.**

> "The old six-byte AOB still matches at `0x14214BE8C`, but its surrounding code is a multi-way
> priority/result branch. It is not proof of a Worker level/ability writer, so enabling it would
> be an unsafe guess."
> (`…2026-09-19-pe-2944-worker-travel-flight-completion.md` status table)
> Later superseded: "user had already manually verified Worker level & skills in-game and
> reports it working … Treat Worker as implemented and live-validated" (`progress.md`).

**Crime UI bypass / legacy money (PE 2944).**

> "Current signatures no longer identify their old contracts. They need separate recovery, not a
> broad pattern fallback."
> (same status table)
> "Therefore no caller is promoted to a replacement hook. `WantedInfo`/wanted-state No Bounty
> remains available; only suppression of the crime UI banner/minimap/guard dispatch remains
> unavailable and fail-closed." (`progress.md` Task 6)

**Hardware-breakpoint tracing is banned for this game/session.**

> "a second hardware execution logger at the UI callsite caused the game process to exit before
> recording any hit … Do not use hardware breakpoint tracing again for this game/session."
> (`progress.md`, safety ruling)

**Deliberately removed UI (still present in code/state history).**

- Dye pages (`RenderDye*`) and Easy Parry / Easy Evade: "Current working-tree dye pages and
  Easy Parry/Easy Evade are outside the active menu surface."
  (`…pe-2850-full-menu-audit-handoff.md` "Global Constraints")
- Mount/Horse editor, mount dye editing, legacy Workers editor: removed by design in the
  2026-09-14 plan. "Delete `docs/WORKER_LEVEL_AND_DISPATCH.md` because it documents the removed
  legacy direct-memory editor and claims the unverified layout is working. Do not replace it
  with a claim that the new AA patch is in-game verified."
  (`…2026-09-14-worker-patch-and-editor-removal.md` Task 6 Step 1)

### 3.2 PE 2850 failures (recorded, then repaired)

- **Version misidentification:** "Revision `2850` falls through to the hard-coded `1.18.02`
  label" (`…crimson-desert-pe-2850-compatibility-audit.md`).
- **Readiness:** "Revision `2850` selects `LegacyComplete`; old sentinels never all appear;
  180-second timeout" (same).
- **Player revision policy:** "Only 1 of 6 character-manager anchors survived; because revision
  is not exactly `2760`, Trinity also tries the removed `kSig_StatCommit` path" (same).
- **Inventory ABI selection:** "Modern TU 2.01 primitives survive, but exact `revision == 2760`
  checks select pre-2.01 placement/commit/holder paths" (same).
- **Add Item:** log `add-item path incomplete (ctor=1 planner=0 commit=0 free=1 teb=1) - Add
  Item will be refused` (same).
- **Quantity persistence:** "transaction commit signature not found on the selected legacy
  branch; edits can revert on reconcile" (same).
- **Slot Size:** "old setter signature not found and Slot Size will not apply" (same).
- **Realm/TLS:** "`RealmFlagOffsetForRevision(2850)` returns legacy `0x1F2`; the surviving
  realm-select instruction now loads immediate `0x1EC`" (same).
- **Inventory core-global:** "`kSig_InvCoreGlobal` survives uniquely at `0x14085834A`, but
  revision 2850 selects the pre-2.01 signature and `+0x15` MOV offset" (same).
- **Movement owner:** "`MoveComponentOwnerOffsetForRevision(2850)` falls back to `0x298`
  although 2760 used `0x2B8`" (same).
- **Equipment refresh:** "Modern `kSig_EquipEffectRefresh` is absent; legacy pattern has 2
  matches and Trinity selects the first" (same).
- **Environment manager:** "`kSig_EnvManager` has no clean/live match" (same) — later repaired
  and double-verified in live memory (`…full-menu-audit-results.md` §3, §5).
- **Verdict:** "**Confirmed currently unavailable:** Add Item and every menu action that depends
  on the native add transaction; durable quantity edits; Slot Size."
  (`…crimson-desert-pe-2850-compatibility-audit.md` "Current decision summary") — all three were
  subsequently recorded as `PASS` in `…full-menu-audit-results.md`.

### 3.3 Historical failures the docs preserve as lessons

- **Trust anti-cheat kick:** "Trust/Affinity multipliers that inject a massive instantaneous
  `delta` (e.g., > 20 points) during `FriendlySetNpc` trigger a ledger desynchronization,
  resulting in an immediate kick to the main menu." Mitigation: clamp gain to `20.0`.
  (`REVERSE_ENGINEERING_GUIDE.md` §12.A)
- **Vendor purchase rejection:** "Forcing the `Money_Copper` max stack to `999,999,999` breaks
  the `kOff_InvBucket_Used` (`+0x12`) counter … Modifying max stacks for non-stackable gear
  locks the `cap - used > 0` free-space gate in the `oHolderInsert` planner."
  (`REVERSE_ENGINEERING_GUIDE.md` §12.B)
- **Dye persistence:** "`DyeApplyBatch` only writes to the DX12 buffer. The server realm ignores
  it unless explicitly forced to serialize." (`REVERSE_ENGINEERING_GUIDE.md` §12.C)
- **Velocity scaling:** "Directly modifying position in the Havok integrator causes the servo to
  detect an illegal displacement, causing severe rubberbanding and stuttering."
  (`REVERSE_ENGINEERING_GUIDE.md` §7)
- **Save corruption:** "Writing arbitrary bytes directly into empty inventory slots bricks save
  files because the item lacks an allocated tracking ID." (`REVERSE_ENGINEERING_GUIDE.md` §8)
- **Static PE header trap:** "Pearl Abyss leaves the Windows PE resource header `dwFileVersion`
  statically fixed at `1.0.0.2474` regardless of Title Updates."
  (`REVERSE_ENGINEERING_GUIDE.md` §11)
- **2.00 world-entry freeze:** "`Inventory::Tick` was overwriting `Money_Copper`
  `ItemDef+0x18/+0x111` every second without toggle gating (offset shifted in 2.00)."
  (`README_TU200_OFFSETS.md` §6.1)
- **2.00 Add Item failure for boots/helmets:** "`BucketForItem` was reading garbage from
  `+0x418`" (`README_TU200_OFFSETS.md` §6.2).
- **Scanner false positives:** "`.debug$P` section contained stale build code."
  (`README_TU200_OFFSETS.md` §6.3)
- **EnvManager off-by-3:** "Legacy `ResolveRipAt(envSig+3)` returned garbage addresses."
  Fixed to `ResolveRipAt(envSig, 7)`. (`README_TU200_OFFSETS.md` §6.4)
- **Crash-cause ambiguity (2944):** "the prior game process exited after a temporary
  non-breaking hardware execution logger was placed on the hot scene resolver, which recorded
  3029 hits and was removed. No fresh Trinity crash report was produced, so causality is
  unknown; do not repeat this high-frequency trace." (`progress.md`)

---

## 4. Open investigations

### 4.1 PE 2944 — explicitly open

| Item | What is still required | Source |
|---|---|---|
| Map-destination teleport live re-test | Install the fresh built ASI with the game closed, set a zero-altitude marker, press the action once, confirm the log sequence `queued` → `applied and verified` (not `write failed verification after 3 attempts`); repeat with nonzero altitude, a character switch, and a no-marker map | `…pe-2944-…completion.md` Task 2 (unchecked boxes) |
| Native Fast Travel runtime semantics | One explicit fresh-game valid / locked / reload test; "Runtime travel semantics remain unverified until one explicit fresh-game valid/locked/reload test" | `progress.md` Task 4 |
| `R9D` semantics on the dispatcher | BN prototype names it `mercenaryHandle`; `offsets.h` / `travel_logic.h` name it `travelMode`. Both agree `0` = ordinary; non-zero meaning not re-derived | `dossiers/travel.md` §6.3 |
| Does the selection gate really open `CommonModalMessage`? | The decisive callee `sub_1417EDAC0` (888 bytes) is unidentified; the modal claim is "**unproven** until a visible UI observation confirms it" | `dossiers/travel.md` §6.1 |
| Third dispatcher caller | `sub_140D9E6C0` @ `0x140D9E858` — semantics unknown (likely mercenary/companion travel UI) | `dossiers/travel.md` §6.2 |
| `sub_1417EDAC0` argument typing | Only partially typed in the decompiler | `dossiers/travel.md` §6.4 |
| `VIBE_FastTravel_Execute` stack argument layout | "stack argument layout not re-verified this pass" | `dossiers/travel.md` §6, §2 |
| `sub_140DBAC20` ABI | Uncharacterised; only its travel callsite is documented | `dossiers/travel.md` §6.7 |
| Inherited-only travel details | travel-target id map `data_146D6AA40`, id source `data_146CE5B28` / `sub_14E877080`, `sub_140C04D80`, `sub_1407782C0`, `sub_140778A50` | `dossiers/travel.md` §6.6 |
| Two un-wired PE 2944 prologue AOBs | `FlushPendingRequest`, `Execute`, `sub_140DBAC20`, `LevelGimmickSceneObjectInfoTable_GetById` prologues are unique but not yet wired into Trinity source or tests | `dossiers/travel.md` §3 |
| Weak locator | `VIBE_LevelGimmickSceneObjectInfoTable_GetById`'s AOB contains a RIP displacement (`48 8B 1D DD BD 8C 06`) that "moves on any PE relayout. Prefer the predicate." | `dossiers/travel.md` §2 |
| Ambiguous registry anchors | `LevelGimmickSceneObjectInfo` = 69 matches, `FieldLevelNameTableInfo` = 25 matches; "Uniqueness therefore comes from the string + the resolver-prologue walk … Do not 'simplify' the locator to a string search." | `dossiers/travel.md` §3 |
| Worker recovery discipline | If the Worker contract is ever re-derived: start from "a normal worker-level/skill state transition, never from the old branch bytes"; require "unique location, surrounding function boundary, exact ABI, live trigger, and a visible ON/OFF/restore result" | `…pe-2944-…completion.md` Review Focus 3, Task 3 |
| Free Flight / Super Run full checklist | "the plan's individually enumerated rise/sink/horizontal/release/land/OFF and reload restoration checklist is not yet separately recorded" | `progress.md` Task 5 live evidence |
| Per-feature semantic passes | "hook/locator rows are STATIC READY, not semantic PASS — visible behavior, OFF restoration, and persistence must be recorded per feature" | `progress.md` Task 6 baseline |
| Remaining PE 2944 ledger | Task 6 asks for per-feature OFF/ON/OFF exercises (player stat guard, Add Item + reload, equipment sockets per character, trust per NPC/pet, time/weather) and for crime/legacy-money to be "recovered or explicitly labeled" | `…pe-2944-…completion.md` Task 6 |
| No PE 2944 full-menu ledger | The systematic 5-tab/33-submenu audit exists only for PE 2850 | `…full-menu-audit-handoff.md`; 2850 results §1 |
| Binary Ninja dossiers outstanding | `locomotion.md`, `inventory.md`, `player-combat.md` (P0); `equipment.md`, `world-time-weather.md`, `trust-worker.md` (P1); `crime-money.md` (P2) all "not started" | `docs/binary-ninja/README.md` feature index |
| PE 2944 baseline completeness | "Game EXE SHA-256 and exact file-version collection still pending a clean single-line record" | `progress.md` Task 1 |
| Suspended MinHook diagnosis | The ±1 GiB window / large-reservation hypothesis was never confirmed by `enum_memory_regions_full` — it simply stopped failing | `UPDATE_PLAN_PE2944.md` §2 (diagnostics steps all pending) |

### 4.2 PE 2850 / general — explicitly open

| Item | What is still required | Source |
|---|---|---|
| `kSig_EquipEffectRefresh` uniqueness | "У майбутньому зафіксувати унікальну сигнатуру для `0x1405E6F20`, щоб позбутися другого мертвого збігу." | 2850 results §4 |
| Add Item availability timing | "The next audit must check whether opening the inventory or loading into a save captures it early enough. Do not weaken the safe refusal when `server=0`." | `…full-menu-audit-handoff.md` baseline |
| Deleted/disconnected UI paths | Dye and parry/evade must not be audited as regressions | `…full-menu-audit-handoff.md` |
| Character-manager redundancy | "For character manager, find at least one additional independent call-site anchor and require agreement on the same global" | 2850 audit plan Task 2 |
| Public TU name for PE revisions | "Determine the public game title-update label corresponding to PE revision `2850` from an authoritative game source" (still PE-version-first) | 2850 audit plan Task 1 |
| Ambiguity contract | "For every multiple match, use the documented string/predicate/caller constraint. Never accept the first raw match." | `…full-menu-audit-handoff.md` Task 2 |
| Safety-mode bits | Verify the active build still implements `Trinity_SafeMode.txt` before relying on any bit value | `README_TU200_OFFSETS.md` §8 |
| Trust live correctness | "Historical notes did not establish complete live trust correctness." | `GAME_UPDATE_PLAYBOOK.md` §4 |
| Whole-menu compatibility claim | "Do not mark the whole menu compatible until every currently exposed action is `PASS` or explicitly `EXCLUDED`." | `…full-menu-audit-handoff.md` Task 9 |

---

## 5. Runtime evidence already collected

### 5.1 LIVE-VERIFIED (a real process performed the action)

| Claim | Evidence | Source |
|---|---|---|
| Add Item works authoritatively on PE 2944 | Console records the modern constructor, holder, commit path, catalog, and an addition with `server=1 client=1`; the startup instruction to open inventory / pick up an item is displayed | `…pe-2944-…completion.md` status table |
| Free Flight and Super Run work on PE 2944 | User confirmation after a full computer restart; fresh Trinity log at `18:55:26` shows `loco diag … isPlayer=1 moveOwnerOffsetFound=0x2C0` under the installed Task 5 ASI | `progress.md` Task 5 live evidence |
| Worker Max Level & Skills works on PE 2944 | User manually verified in-game; fresh PE 2944 log shows the exact unique target resolved at `0x14214BE8C` in its original state and `worker: max level and all abilities patch enabled` immediately afterward | `progress.md` status correction |
| Map-destination reader exists and resolves the menu's marker | "PE 2944 has exactly one direct UI destination reference at `0x140DEFCFE`; it resolves the map marker shown by the menu" | `…pe-2944-…completion.md` status table |
| Map-destination teleport source repair is live | Ledger pre-flight: "Task 2 produces a direct marker destination consumed by Task 7 deployment tests; committed as `6d8bc42` and currently live-verified" | `progress.md` pre-flight |
| Native Fast Travel selection gate is really invoked by the UI | Fresh process PID `7808` invoked the recovered trigger exactly once during a normal, known-unlocked native Fast Travel action with Trinity Fast Travel disabled. A non-breaking entry logger at `0x140654ED0` recorded `RDX=0x6` (sceneId 6) and `R8=0x8E` (nodeIndex 142), matching the UI bounds-check/call contract; entry `RCX` was a non-null live context | `progress.md` Task 4 live proof |
| Locomotion hook installs on PE 2944 | Fresh process PID `42188` log confirms the hook installed at `0x14369FF60`; on its first real movement frame it recorded `moveOwnerOffsetFound=0x2C0` | `progress.md` Task 5 root cause |
| MinHook hooks install on PE 2944 | "The earlier `MH_ERROR_MEMORY_ALLOC` failures are gone; current player, inventory, frame-timer, equipment, and trust hooks install." Fresh launch at 19:27 confirms player stat-pin, damage and combat timing hooks; movement/locomotion, map-marker and native Fast Travel; wanted-state and `WantedInfo`; modern inventory/Add Item primitives; time-scale; Equipment effect refresh/socket vector; NPC and pet/mount trust observers; Worker patch resolution | `…pe-2944-…completion.md`; `progress.md` Task 6 baseline |
| PE 2850: Super Run / Free Flight work after the `0x2B8` fix | "User confirmed working after the `0x2B8` correction" (both rows) | `…full-menu-audit-handoff.md` baseline |
| PE 2850: Add Item authoritative | "User observed refusal before holder capture, then successful `Added ... [server=1 client=1]` after a pickup" | same |
| PE 2850: TLS realm selector `0x1EC` | "Live disassembly identified `0x1EC`; code maps PE 2850 to `0x1EC` … Implemented; Add Item live-confirmed" | same |
| PE 2850: map-marker teleport | "**Підтверджено 6 реальними телепортами в лозі** … Жодного снепбеку (snap-back)." | 2850 results §2 tab.2 |
| PE 2850: No Bounty | "No Bounty applied to 35/35 `WantedInfo` rows while enabled" | 2850 audit plan "Runtime evidence" |
| PE 2850: EnvManager repair double-verified in live memory | Chain `0x146B52FF0` → `0x23079A22100` → `0x2306B705000` → `0x2307B680000` → `0x2308AC95628` valid; cloud/wind node page is `PAGE_READWRITE` / `MEM_COMMIT`; all 10 float fields in a safe range (`DUST_BASE=5.0`, `CLOUD_THICK=0.02`, `FOG_B=0.498`) | 2850 results §5.2 |
| PE 2850: catalog | `inventory: table 'iteminfo' / 'categorygroupinfo' / 'stringinfo' / 'Inventory' resolved`, `catalog built (6815/44)`; the audit measured 6,813 items / 6,812 named / 45 categories | `UPDATE_PLAN_PE2944.md` §1; 2850 results §2 tab.3 |
| PE 2.00 historical live findings | Kliff component walk `*(*(owner+0x68)+0x38)` VALID; 6-byte socket record injection verified stable (gem `0x0C24`, Tag 0, Slot 3); `+0x70` read `65283` (`0xFFFFFF02`) | `TU200_RE_NOTES.md` §8 |

### 5.2 Recorded log line formats (triage keys)

Startup / readiness (`GAME_UPDATE_PLAYBOOK.md` §2B, `UPDATE_PLAN_PE2944.md` §1):

```text
Gameplay-code readiness timed out after 180 seconds
player: char-manager successfully resolved (N anchors verified)
player: char-manager anchors DISAGREE
player: stat-commit signature NOT FOUND
player: damage-apply: MH_CreateHook failed
player: damage-apply signature NOT FOUND (tried primary + alt)
teleport: movement-update: MH_CreateHook failed
teleport: scene-registry resolver NOT FOUND - fast-travel menu disabled.
PE 2944 native Fast Travel contract incomplete … - menu disabled
teleport: native fast-travel accepted/refused scene=… node=…
world: field-clock signature NOT FOUND
world: TOD engine-global signature NOT FOUND
inventory: Added ... [server=1 client=1]
add-item path incomplete (ctor=1 planner=0 commit=0 free=1 teb=1) - Add Item will be refused
inventory: table 'iteminfo' resolved / catalog built (6815/44)
equipment: EquipEffectRefresh resolved @ 0x140659510
equipment: native ResizeSocketVector resolved @ 0x14488C46D
friendly: Trust Multiplier setters NOT FOUND
Trust Multiplier (25.0x) ENGAGED
worker: supplied level/ability patch disabled for unsupported PE revision 2944
worker: max level and all abilities patch enabled
loco diag ... isPlayer=1 moveOwnerOffsetFound=0x2C0
map marker queued
applied and verified
write failed verification after 3 attempts
```

Additional triage keys named in contract cards: `marker signatures count mismatch`, `no marker
hooks`, `slot-expansion setter hook failed`, `effect refresh faulted`, `upsert signature not
found`, `visual test only`, `realm flag unresolved`, `ambiguous` (`GAME_UPDATE_PLAYBOOK.md` §2B).

### 5.3 Runtime negative results / cautions

- Fast Travel requests accepted at `0x140654ED0` but the player did not travel — the function is
  a selection gate, not the dispatcher (`progress.md`).
- A hardware execution logger on the hot scene resolver recorded 3,029 hits and was removed; the
  process exited afterwards with no fresh Trinity crash report, so causality is unknown.
  A second logger at the UI callsite killed the process before any hit.
  **"Do not use hardware breakpoint tracing again for this game/session."** (`progress.md`)
- `CE MCP get_opened_process_id` was retried after a user restart but "did not respond within
  20 seconds; no CE breakpoint, injection, or memory write was used." (`progress.md` Task 5)

### 5.4 Runtime state / hashes observed (session-specific, not reusable as addresses)

- `Trinity.asi` loaded at `0x7FFC83D40000`, image size `2,220,032`; `CrimsonDesert.exe` PID
  `48492`, base `0x140000000`, image size 402,444,288 (`progress.md` Task 1).
- ASI SHA-256 values recorded across the documents, in the order each document records them
  (**not** a single verified deploy chain — the 2850 documents and the 2944 ledger never share one
  timeline): `48946494…9BA2C9B50` (PE 2850 audit) → `CF396356…5A5BDDDA1E` (2850 movement-owner
  build) → `1CD8B2C2…AAF6D822D` (2850 EnvManager repair) → `FDB36821…F1130318A4` (2944 installed at
  Task 1) → `83FFF852…9277C92061` (Task 4 selection gate) → `B347E42A…2A2DD66689D`
  (fail-closed) → `7708B2A6…5F6BCD4E554` (Task 4 dispatcher) → `C2478B78…7698AFCB045`
  (Task 5 locomotion) → `F5AA928E…7FD6B25571E3` (Task 6 crime diagnostic) →
  `FE5D72FA…6FB9397B6` (startup notice) → `F698A774…B719C017D97` (build stamp, final recorded).
- The 2850 audit baseline also records EXE SHA-256
  `BCBF623A…E7088B1C0E` (PE 2850) and Trinity build `v1.3.5` built `2026-09-08 23:12:31`.

### 5.5 Raw dumps (`_analysis/`)

Both files are unauthored disassembly dumps with **no PE revision, SHA-256, or date header** —
they corroborate static patterns but cannot be attributed to a specific build.

- `_analysis/game_sites.txt` — CrimsonDesert.exe snippets at `+0x2074BA0`, `+0x22771A6`,
  `+0x34E530D`, `+0x12F4681`. Notable: the `+0x2074BA0` body matches the shape of
  `kSig_InvGetHolder` (`mov rax,[rcx+0x68]; mov rcx,[rax+0x20]; movzx eax,word [rcx+0x30]`)
  **and** contains a visible possessor round-trip check `[rbx+0xA0] → [rbx+0xD0]` at
  `0x142074BEE`–`0x142074C04`; the `+0x22771A6` body RIP-loads RVA `0x6C2E328` (an `iteminfo`-like
  table global); `+0x34E530D` and `+0x12F4681` both use TLS flag `0x1FD` via `gs:[0x58]`.
  The file ends with the note `RVA 0x20 not in any section`.
- `_analysis/trinity_frames.txt` — Trinity.asi frames. Shows the vectored crash logger
  dispatching on `0xC0000005`, `0xC000001D`, `0xC00000FD` (base `0x180000000`, matching the ASI
  default base) and string data including `...Get: %s | In...`,
  `OneHitKill: %s | No Bounty: %s | No...`, `Write Access Violation (Writing to invalid
  address)`, `DEP Violation (Writing to invalid address)`.

---

## 6. Trinity build / test / deploy contract

### 6.1 Build

**Preferred narrow repair path** (`GAME_UPDATE_PLAYBOOK.md` §6) — direct CMake in a Visual Studio
x64 developer environment with CMake and Ninja available:

```powershell
cmake -S . -B build-update -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_EXTENDED_HOOKS=OFF
cmake --build build-update --target Trinity TrinityReadinessTests TrinityMapMarkerTests
ctest --test-dir build-update --output-on-failure
git diff --check
```

> "Stop if any command fails. Select the extended-hooks option to match the authorized target
> variant; verify that its sources exist before enabling it. Do not reuse a build directory
> configured for a different generator/toolchain." (`GAME_UPDATE_PLAYBOOK.md` §6)

**Full product build** (`README.md` "Building from Source"):

```powershell
powershell -ExecutionPolicy Bypass -File .\Build_Trinity.ps1
```

Output: `build/Release/Trinity.asi`. Warning carried in both README and playbook:
"`Build_Trinity.ps1` also rewrites the build timestamp and creates/copies release packages; it is
not a compile-only command."

**Alternative used by the SDD ledger** — one `cmd.exe` process, `build-clean` tree:

```cmd
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 && cmake --build build-clean --config Release --target Trinity TrinityReadinessTests TrinityMapMarkerTests
```

Build stamp: `TRINITY_BUILD_TIME` is asserted by a test and generated into
`src/core/build_timestamp.h`; changing it requires rebuilding the test target too — "The first
CTest run after building only target `Trinity` used a stale `TrinityReadinessTests.exe`"
(`progress.md`).

### 6.2 Test suite (CTest)

- Configured CTest targets named across the docs:
  - `TrinityReadinessTests` — "readiness, version, scanner and extracted gameplay logic
    contracts" (`GAME_UPDATE_PLAYBOOK.md` §6)
  - `TrinityMapMarkerTests` — "marker reading/application" (same)
  - `TrinityMinHookFallbackTests` (`…pe-2944-…completion.md` Task 7)
  - `TrinityTravelLogicTests` (`progress.md` Task 4)
  - Reported as "full CTest passed 4/4" throughout the PE 2944 ledger.
- Full suite command and expected result:
  `ctest.exe --test-dir build-clean --build-config Release --output-on-failure` →
  `100% tests passed, 0 tests failed` (`…2026-09-14-worker-patch-and-editor-removal.md` Task 7
  Step 3).
- **Hard limit:** "They do not execute the game's engine or validate live signatures."
  (`GAME_UPDATE_PLAYBOOK.md` §6) and "CTest evidence does not imply in-game semantic proof."
  (`…2026-09-14-worker-patch-and-editor-removal.md` "Global Constraints")
- Static source-contract scripts also exist, e.g.
  `tests/verify_worker_feature_contract.ps1` (run with
  `powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify_worker_feature_contract.ps1`).
- TDD discipline recorded in the plans: write the failing test first, record the RED failure, then
  implement, then GREEN, then commit (`…2026-09-14-worker-patch-and-editor-removal.md` Tasks 1–2).

### 6.3 Deployment and hash/backup requirements

The recorded procedure (`GAME_UPDATE_PLAYBOOK.md` §6, `…full-menu-audit-handoff.md` Global
Constraints, `…pe-2944-…completion.md` Task 7, `progress.md` Task 7 entries):

1. **Confirm the game is closed.** The PE 2944 ledger verifies process absence explicitly
   ("no `CrimsonDesert.exe` process was found") before every install.
2. **Back up the installed ASI** to a timestamped path and **verify the backup hash**. Actual
   naming used: `E:\Steam\steamapps\common\Crimson Desert\bin64\Trinity.asi.backup-YYYYMMDD-HHMMSS`
   and one earlier `Trinity.asi.pre-movement-owner-2850-20260911-114246.bak`.
3. **Copy the exact verified build artifact** and compare **SHA-256 build vs installed** —
   recorded each time as "installed SHA-256 exactly matches `<hash>`".
4. **Relaunch fresh** and confirm the actual loaded module/build and the startup log block.
5. **Do not overwrite an in-use ASI**; never add a second proxy loader to an installation that
   already has one (`GAME_UPDATE_PLAYBOOK.md` §2E).
6. Keep **build, installed and release-package hashes aligned** (`README_TU200_OFFSETS.md`-era
   task lists and the 2850 audit baseline: installed ASI hash equals `build/Release/Trinity.asi`,
   `build/Release/package/Trinity.asi` and `build-clean/Trinity.asi`).
7. **Never use `tools/deploy_master.ps1` as a generic deploy command** — "its hard-coded paths
   and old version markers make it unsuitable for a future repair without a fresh review."
8. **Do not publish** personal settings, logs, profiles, backups or saves. On a crash, preserve
   `Trinity.log`, `Trinity_Crash.txt`, `Trinity_Crash.dmp` plus the EXE and installed ASI hashes,
   but keep them out of release packages (`GAME_UPDATE_PLAYBOOK.md` §2E).
9. Rollback: "close the game, retain the known-good `Trinity.asi` with its SHA-256, and restore
   that exact artifact only after recording the failed build's hash. Verify the restored installed
   file hash before relaunch." (`GAME_UPDATE_PLAYBOOK.md` §2E)
10. Isolation: preserve the user's original `Trinity.ini`, then test from a known-safe copy with
    feature toggles disabled and `fileLogging=1` (same section).

### 6.4 Required evidence record before changing a signature

`GAME_UPDATE_PLAYBOOK.md` §2D (do not replace an AOB until every applicable fact is recorded):
symbol/feature/consumer; old AOB / new AOB / wildcard reason; EXE SHA-256 / PE version / module
base / section name and characteristics; match count and each candidate RVA; instruction bytes
before/after and function boundary; RIP displacement location, instruction length, resolved
target; Win64 ABI (RCX/RDX/R8/R9, stack, XMM, return); original-call timing and calling thread;
object layout, pointer chain and realm; hook form (MinHook vs manual inline) with
overwrite/replay/jump-back proof; and the live action that proves the intended function.
`UPDATE_PLAN_PE2944.md` §9 restates the same form in Ukrainian, and §7 of the playbook defines
the append-only repair entry format.

### 6.5 Standing prohibitions (from the audit/handoff documents)

- Never change an AOB, structure offset, version policy, hook, game memory, or CE script merely
  because an action appears broken — record the root cause first
  (`…full-menu-audit-handoff.md`).
- A unique AOB match, an `installed` log line, a toast, a CTest pass, or a visible client-only
  item is **not** a working feature (playbook §5; handoff Global Constraints).
- Do not shorten the readiness timeout or delete sentinels "щоб швидше" — first confirm which
  sentinels exist in the new build (`UPDATE_PLAN_PE2944.md` §10).
- Do not copy old absolute VAs / TLS constants from the historical docs without fresh evidence
  (`UPDATE_PLAN_PE2944.md` §10; playbook §2F).
- Never accept the first raw match for char-manager anchors or any multi-match pattern
  (playbook §2D; handoff Task 2).
- `MarkerStatus::Queued` is not success; require final applied position and no snap-back.
- For Add Item, require `server=1 client=1` plus use/equip and persistence; do not weaken the safe
  refusal when `server=0` (handoff).

---

## 7. Conflicts and caveats

### 7.1 Cross-document disagreements

1. **PE 2944 "Free Flight / Super Run" status.**
   `…2026-09-19-pe-2944-worker-travel-flight-completion.md` "Current Evidence and Status" says:
   *"Free Flight and Super Run | Blocked by changed locomotion locator | Both current and pre-2.01
   locomotion-stepper AOBs return zero live matches."*
   `progress.md` (later entry) says the locomotion hook **was installed** at `0x14369FF60` on a
   fresh process, and the user confirmed both features working. The 2944 status table is
   therefore **stale** relative to the execution ledger; the documents do not explain how a
   zero-match locator became an installed hook.

2. **PE 2944 "Worker" status.**
   The completion plan marks Worker "Safely disabled"; `progress.md` records a "Status
   correction" stating the user verified it in-game and the log shows the patch enabled, calling
   the plan's Task 3 checklist "stale documentation, not a functional blocker." Treat the ledger
   as later, but note the plan's acceptance criteria ("the old `0F 85 95 00 00 00` priority branch
   remains unpatched unless it independently satisfies that proof") were never ticked.

3. **PE 2944 MinHook failure.**
   `UPDATE_PLAN_PE2944.md` §0/§2 asserts a systemic allocator failure that "killed **all**
   MinHook hooks". The later 2944 plan says those failures are gone. No document records what
   fixed it.

4. **PE 2944 "Native Fast Travel" status.**
   The completion plan says the menu is "Blocked by changed contract" and "correctly disabled";
   `progress.md` records both locators recovered, TDD tests added, two deployments
   (`7708B2A6…`), and the row "Map-destination teleport | Source repair built; live re-test
   pending". The plan's status table again lags the ledger.

5. **`kSig_EnvManager` PE 2850 repair vs audit.**
   The audit section reports the drift site at `0x14391E920` and recommends shortening the tail
   pattern. The repair record says the signature was updated to "the exact modern PE 2850 pattern
   (`0x140C5579F`)". The document never reconciles the two addresses or explains whether they are
   the same code path.

6. **`pEnvManager` global PE 2850.**
   The drift analysis reads `pEnvManager` at `0x146C1ADC0`; the later double-verification uses
   `g_pEnvManager (0x146B52FF0)`. Both are runtime globals from (possibly) different sessions;
   the document does not reconcile them.

7. **Movement owner for PE 2944.**
   `UPDATE_PLAN_PE2944.md` §5 lists the 2944 requirement as "реальний (2760/2850→0x2B8)".
   `docs/binary-ninja/README.md` and `progress.md` prove `+0x2C0` for 2944 and explicitly warn
   "(not `+0x2B8`)". The 2944 plan predates the proof.

8. **Historical offsets for the same fields.**
   `README_TU200_OFFSETS.md` states `TrItemValue+0x60` socket-vector pointer and `+0x68` size are
   "**Identical**" to 1.18; `REVERSE_ENGINEERING_GUIDE.md` §12.D claims "Title Update 1.18+
   shifted `TrItemValue` socket pointers by exactly 8 bytes" with `+0x58` legacy → `+0x60` modern.
   These are consistent only if the "legacy" column means TU ≤ 1.17; the docs use different
   boundary labels (`TU 1.10 – 1.16` vs `TU 1.17 – 1.18+`), so the boundary should be re-derived,
   not assumed.

### 7.2 Internal inconsistencies within single documents

9. **PE 2850 audit control count.** The prose says "**124 інтерактивні елементи керування**"
   while the totals row of the UI table says **144** (138 PASS + 6 BLOCKED). The two numbers are
   never reconciled.

10. **PE 2850 signature census.** The categories sum to **74** (32 + 5 + 27 + 9 + 1) but the
    total row says **79**. Every percentage in that table is consistent with a denominator of 79
    (32/79 = 40.5%, 27/79 = 34.2%, …), so the counts — not the percentages — are the inconsistent
    part; one or more categories are under-counted.

11. **PE 2850 "retired" signatures vs "excluded".** 27 signatures are described as retired
    pre-2.01 patterns that are "навмисно не використовуються" (deliberately unused), while the 9
    excluded ones belong to removed menu pages. Both counts are used rhetorically in the summary;
    they overlap conceptually with the "1 regression" figure.

12. **PE 2944 image size.** `progress.md` Task 1 writes "image size 402444288 (0x17FC000)";
    `docs/binary-ninja/snapshots/PE-2944.md` says `0x17FCD000`. 402,444,288 decimal equals
    `0x17FCD000`, so the ledger's parenthesised hex is a typo.

13. **PE 2944 locator length.** `dossiers/travel.md` notes the `offsets.h` prose calls
    `kSig_TravelToNode_PE2944` a "32-byte prologue" while the pattern is 31 byte-tokens.

14. **`kSig_EnvManager` locator description.** `offsets.h` prose (quoted in the 2850 results)
    describes the registry table-name string as "its unique table-name string"; the 2944 dossier
    measured **69 matches** for `LevelGimmickSceneObjectInfo` and **25** for
    `FieldLevelNameTableInfo`, i.e. unique as a name, not as a byte pattern.

15. **Crime status vs `kSig_RegisterCrimeEvent`.** `progress.md` reports zero live matches for the
    legacy crime-event AOB on 2944, while the PE 2850 audit lists `kSig_RegisterCrimeEvent` as a
    survivor with an installed hook. The two are different builds, so this is expected — but the
    2944 plan's `TheWantedState` recovery also introduced
    `MayProbeLegacyCrimeEventDispatcherForRevision`, which keeps the legacy probe alive through
    **PE 2850 and older only**.

### 7.3 Documented precedence (resolves most conflicts)

`GAME_UPDATE_PLAYBOOK.md` §2F — "When sources disagree, use this order":

1. The exact initialized game process and a reproduced action.
2. The current source code and its final diff.
3. The verified build artifact and the ASI actually loaded by that process.
4. The playbook's dated snapshot and repair record.
5. Historical README/RE notes and absent-tool references.

Explicit demotions:

- `README_TU200_OFFSETS.md` — "Treat it as background only … Never copy an old absolute VA,
  `.debug` rule, TU label or offset into a new build without fresh evidence."
- `REVERSE_ENGINEERING_GUIDE.md` — "Architecture background, not a current compatibility
  declaration … its header version, title-update coverage, offsets and code examples predate the
  current TU 2.x maintenance work."
- `TU200_RE_NOTES.md` — "Historical archive, not current implementation guidance. Every VA, AOB,
  offset, section decision and 'safe/fixed' statement below belongs to the one TU 2.00.00 capture."
- `docs/binary-ninja/*` — "**Every address in the dossiers is snapshot evidence only**: a new
  executable must be re-located from its recorded signature or predicate before any Trinity
  feature is enabled."
- The 2944 plan's own status table — superseded by `progress.md` (see 7.1 items 1, 2, 4).
- `src/game/offsets.h` remains "the single source of truth when the two differ"
  (`GAME_UPDATE_PLAYBOOK.md` preamble).

### 7.4 Claims the documents themselves flag as unproven

- The `CommonModalMessage` claim for the travel selection gate — "Treat the modal claim as
  **unproven** until a visible UI observation confirms it."
- `R9D` non-zero semantics — inherited, not re-derived.
- The MinHook ±1 GiB exhaustion hypothesis for PE 2944 — never confirmed.
- `VIBE_FastTravel_TryAcceptRequest` / `VIBE_FastTravel_Execute` / `sub_140DBAC20` — "Live proof:
  **none in this pass**" for every function in the travel dossier; the intended runtime check is
  Trinity's own `teleport: native fast-travel accepted/refused scene=… node=…` log line.
- `kSig_JustCore` `@ 0x140AC0FB0` — "under verification" (2.00 notes).
- Money getters — "Requires live-testing before permanent hook insertion."
- `_analysis/game_sites.txt` and `_analysis/trinity_frames.txt` carry no PE revision, hash, or
  date, so no claim in this digest attributes their contents to a specific build.
- The 2850 results document is partly an audit and partly a repair/deploy record (it contains both
  the BLOCKED finding and the fix, plus the final ASI hash); its §1 statistics describe the
  pre-fix state while §5 describes the post-fix state.

---

*Compiled from repository documents only. No source file, binary, game process, or Cheat Engine
session was modified or used. Static claims and live-verified claims are separated wherever the
sources make the distinction.*
