# Crimson Desert PE 1.0.0.2850 Compatibility Audit Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging for the audit. If repairs are later authorized, create a separate implementation plan and use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Determine, without changing Trinity or the game, which currently exposed menu actions still work on `CrimsonDesert.exe` PE `1.0.0.2850`, which fail, and which signatures, layouts, revision policies, or ABI contracts would need a later update.

**Architecture:** Treat every feature as an evidence pipeline: source/menu mapping -> clean-image AOB result -> initialized live-image result -> instruction/ABI/layout contract -> installed hook state -> user-visible semantic action -> disable/restore behavior. A feature is compatible only after the final semantic checks; a unique AOB or installed hook is not sufficient.

**Tech Stack:** C++20, Win64, Crimson Desert, ASI Loader, MinHook, ImGui, Cheat Engine MCP Bridge v12.0.0, PowerShell, PE/AOB inspection.

**Spec:** `GAME_UPDATE_PLAYBOOK.md`

## Global Constraints

- Audit and planning only: do not edit source AOBs, offsets, version mappings, hooks, menu behavior, configuration, or the installed game.
- Do not compile, package, deploy, toggle features, write game memory, run Auto Assemble, or attach a debugger during this phase.
- Preserve every pre-existing dirty worktree change. The only new repository artifact allowed by this request is this plan.
- Treat the working-tree `src/gui/menu.cpp` as the current source menu. Its uncommitted removal of the dye routes and Easy Parry/Easy Evade state is user-owned and must remain untouched.
- Keep clean-image static matches, initialized live matches, installed hooks, and semantic in-game results as separate evidence levels.
- Do not label an action working from an AOB match, hook log, toast, CTest result, or visible client-only inventory state.
- For Add Item, success requires an authoritative transaction and the log contract `inventory: Added ... [server=1 client=1]`, followed by use/equip and persistence checks where applicable.
- For teleport, `MarkerStatus::Queued` is not success; require the final applied position and no snap-back.
- For equipment and trust, verify each character/realm target and persistence independently.
- Any later repair requires a separate authorization and implementation plan.

---

## Baseline captured on 2026-09-11

| Evidence | Result |
| --- | --- |
| Running process | `CrimsonDesert.exe`, PID `31972`, x64, module base `0x140000000` |
| Executable | PE/file version `1.0.0.2850`, size `375,511,960`, SHA-256 `BCBF623AD5690147DC462AEAED5B4F97BD73296BA0D6AB54663586E7088B1C0E` |
| CE bridge | Connected to PID `31972`; Cheat Engine MCP Bridge `12.0.0` |
| Installed module | `Trinity.asi` loaded; installed SHA-256 `48946494A71576DEC6478F44E85F5875584212A7AD011CE5703F2E59BA2C9B50` |
| Build provenance | Installed ASI hash equals `build/Release/Trinity.asi`, `build/Release/package/Trinity.asi`, and `build-clean/Trinity.asi` |
| Trinity build | `v1.3.5`, built `2026-09-08 23:12:31` |
| Overlay | Log confirms DX12 hooks, wrapped swapchain, and overlay ready at `2560x1440` |
| Version detection | Incorrect: PE `2850` is displayed as `Crimson Desert 1.18.02 (Active)` |
| Readiness | Incorrect profile selected; startup waited 180 seconds and timed out before installing available hooks |

Status vocabulary for the ledger:

- `static survived`: clean executable has the expected unique signature.
- `hook installed`: clean signature is unique and the initialized live entry begins with a MinHook jump or Trinity logs the exact installed target.
- `static ambiguous`: pattern has multiple candidates and cannot be accepted alone.
- `failed`: current runtime explicitly refused or disabled the feature.
- `semantic pending`: no controlled in-game action has yet proved the feature.
- `excluded`: not reachable from the current working-tree menu.

## Initial compatibility findings

### Confirmed failures or update requirements

| Area | Current evidence | Later repair target to investigate |
| --- | --- | --- |
| Version identity | Revision `2850` falls through to the hard-coded `1.18.02` label | `src/core/version_mapping.cpp`, `src/core/version_detect.cpp`, and tests must recognize the new title update without guessing its public TU name |
| Readiness | Revision `2850` selects `LegacyComplete`; old sentinels never all appear; 180-second timeout | Derive a revision-2850 readiness profile from representative current subsystems in `src/core/readiness.*` and `src/core/mod.cpp` |
| Player revision policy | Only 1 of 6 character-manager anchors survived; because revision is not exactly `2760`, Trinity also tries the removed `kSig_StatCommit` path | Re-derive at least two independent char-manager anchors and audit whether revision 2850 keeps the continuous stat-pin model |
| Inventory ABI selection | Modern TU 2.01 primitives survive, but exact `revision == 2760` checks select pre-2.01 placement/commit/holder paths | Replace exact-revision assumptions only after confirming revision-2850 ABI for commit placement, free placement, holder insert, transaction commit, core global, and slot guard |
| Add Item family | Runtime log: `add-item path incomplete (ctor=1 planner=0 commit=0 free=1 teb=1) - Add Item will be refused` | Modern signatures already surviving: `kSig_TrItemValueCtor`, `kSig_InvHolderInsert201`, `kSig_InvCommitPlacement201`, `kSig_InvCommit`, `kSig_InvCoreGlobal`; validate their ABI and realm contracts before selecting them for 2850 |
| Quantity persistence | Runtime log: transaction commit signature not found on the selected legacy branch; edits can revert on reconcile | Confirm `kSig_InvCommit @ 0x142078BC0` remains the correct transaction hook and verify distinct server/client holders |
| Slot Size | Runtime log: old setter signature not found and Slot Size will not apply | Determine whether 2850 still uses the TU 2.01 continuous bucket-state guard; do not invent a replacement setter if the setter ABI remains removed |
| Realm/TLS policy | `RealmFlagOffsetForRevision(2850)` returns legacy `0x1F2`; the surviving realm-select instruction now loads immediate `0x1EC` | Treat `0x1EC` as a candidate only; prove the calling thread's TLS boolean and server/client transitions before changing the inventory realm offset |
| Inventory core-global policy | `kSig_InvCoreGlobal` survives uniquely at `0x14085834A`, but revision 2850 selects the pre-2.01 signature and `+0x15` MOV offset | Verify the current RIP-relative instruction and select the modern zero-offset contract only if object/holder traversal is valid |
| Movement owner policy | `MoveComponentOwnerOffsetForRevision(2850)` falls back to `0x298` although 2760 used `0x2B8` | Capture the live movement owner during character, mount, jump, and free-flight transitions; verify `+0x90/+0x1A0` positions and `+0xC0/+0xD0` velocity before choosing an offset |
| Equipment effect refresh | Modern `kSig_EquipEffectRefresh` is absent; legacy pattern has 2 matches and Trinity selects the first | Re-find the exact refresh routine from socket/refine call flow; validate Win64 arguments and derived-effect refresh before accepting a new unique signature |
| Environment manager | `kSig_EnvManager` has no clean/live match | Re-find the environment manager from the still-hooked weather paths; independently validate instant-clear, preset, sky, cloud, and fog operations |

### Signatures that survived the clean PE image

The following are static evidence, not semantic proof.

- Installed-hook evidence (unique in the clean image; live entry replaced by a MinHook `E9` jump): `kSig_DamageApply`/`_Alt`, `kSig_CombatTimingEval`, `kSig_MoveUpdate`, `kSig_LocoStepper`, `kSig_InvGetItemQty`, `kSig_EvaluateCrimeWantedState`, `kSig_RegisterCrimeEvent`, `kSig_FieldTimeTick`, `kSig_WeatherRain`, `kSig_WeatherSnow`, `kSig_WeatherDust`, `kSig_WindPack`, `kSig_FriendlySetNpc201`, `kSig_FriendlySetPet201`, `kSig_FriendlyGetNpc201`, and `kSig_FriendlyGetPet201`.
- Unique live/unhooked evidence: `kSig_TravelToNode @ 0x1405E3490`, `kSig_InvGetHolder @ 0x142074BA0`, `kSig_InvHolderInsert201 @ 0x14234E630`, `kSig_InvCommit @ 0x142078BC0`, `kSig_InvCoreGlobal @ 0x14085834A`, `kSig_TrItemValueCtor @ 0x1423507B0`, `kSig_InvCommitPlacement201 @ 0x1420788A0`, `kSig_InvFreePlacements @ 0x140418C60`, `kSig_InvFreePlacements201 @ 0x1487D6BC0`, `kSig_LocStringGet @ 0x141232620`, `kSig_FrameTimerBody @ 0x140A54C53`, `kSig_FieldTimeRealm @ 0x1420303B4`, `kSig_TodEngineGlobal @ 0x142C159FA`, and `kSig_FriendlyNpcTrustWriter @ 0x142AAF030`.
- Expected multi-stage/broad patterns: `kSig_LeaR8Rip`, `kSig_TableResolverPrologue`, `kSig_MarkerOriginPrefix`, and `kSig_MovR8Rip` remain multi-match and must only be accepted through their string/consensus predicates.
- Unexpected ambiguity: `kSig_EquipEffectRefresh_Legacy` has 2 matches (`0x1405E6F20`, `0x14099E140`); neither is accepted as correct without call-flow proof.
- Hidden patterns outside the named registry: marker-destination reference survived uniquely at `0x140D6786E`; `ResizeSocketVector` survived uniquely at `0x14479B82D`; only character-manager anchor 1 survived at `0x14276C9A7`.
- Full named scan totals: clean file `32 unique / 6 multi / 38 zero`; initialized live module `14 unique / 5 multi / 57 zero`. Live zeroes that correspond to clean unique prologues were checked for MinHook jumps before classification.

### Runtime evidence already observed

- Player damage and combat-timing hooks installed. Their actual menu behavior remains semantic pending.
- Movement-update and locomotion-stepper hooks are installed. Super Run, Super Jump, Free Flight, and teleport remain semantic pending because their structure offsets have not been revalidated.
- Map-marker subsystem reports `hooks=5/5` and protection present. Marker selection, final application, and snap-back remain semantic pending.
- Inventory item/string/storage tables resolved; catalog contains 6,813 rows and 6,812 named entries.
- No Bounty applied to 35/35 `WantedInfo` rows while enabled. Normal crime behavior after disabling remains semantic pending.
- Frame-timer, field-time, rain/snow/dust/wind, and trust observers are installed. Hook presence does not prove the visible world or trust result.
- Trust Multiplier logged as engaged at `25.0x`; NPC and pet/mount deltas remain semantic pending.
- Equipment refresh resolved only through the ambiguous legacy pattern; socket-vector resize resolved uniquely.
- Dye installation and menu routes are removed by the current user-owned working-tree changes, so dye functions are excluded from the exposed-menu audit.

---

### Task 1: Freeze evidence and map the new revision

**Files:**
- Read: `src/core/version_detect.cpp`
- Read: `src/core/version_mapping.cpp`
- Read: `src/core/readiness.cpp`
- Read: `src/core/mod.cpp`
- Record only in: this plan

**Interfaces:**
- Consumes: executable identity, initialized module map, source revision policies.
- Produces: one immutable revision-2850 evidence header used by every later test row.

- [x] Record PID, module base, version, size, executable SHA-256, installed ASI SHA-256, and CE bridge version.
- [x] Confirm that the installed ASI is loaded and matches the known local build artifact.
- [x] Capture the version misidentification and exact readiness timeout from `Trinity.log`.
- [ ] Determine the public game title-update label corresponding to PE revision `2850` from an authoritative game source; keep the PE version as the primary identifier until confirmed.
- [ ] Re-run the read-only identity capture after any game restart and reject mixed evidence from a different PID/hash.

### Task 2: Complete the AOB and revision-contract matrix

**Files:**
- Read: `src/game/offsets.h`
- Read: `src/mem/scanner.*`
- Read: `src/mem/hooks.h`
- Read: all consumers returned by `rg -n "kSig_|revision == 2760" src`
- Record only in: this plan

**Interfaces:**
- Consumes: Task 1 identity and the current 76 non-empty named signatures.
- Produces: for each current signature, clean count/RVA, live count/address, consumer, hook state, instruction/ABI verdict, layout verdict, and disposition.

- [x] Scan all 76 non-empty named `kSig_*` patterns against the clean executable and the initialized live module.
- [x] Separate expected live prologue replacement from genuinely missing patterns by verifying MinHook entry jumps.
- [x] Scan the six `kCharMgrAnchors`, marker-destination reference, and `ResizeSocketVector` pattern outside the named registry.
- [ ] For each surviving unique function, disassemble through argument setup and the first meaningful call/write, not only the matching prologue.
- [ ] Resolve every RIP-relative operand and verify the target section, pointer depth, object bounds, and current realm.
- [ ] Audit every exact `revision == 2760` branch and classify it as unchanged-modern, new-2850, legacy, or unsafe-unknown.
- [ ] Re-run table resolvers through their string anchors (`iteminfo`, `stringinfo`, `Inventory`, scene/level tables) and record the resolved globals rather than accepting broad LEA/MOV patterns.
- [ ] For character manager, find at least one additional independent call-site anchor and require agreement on the same global before declaring all-character tracking safe.
- [ ] For `kSig_EquipEffectRefresh_Legacy`, reject first-match selection until xrefs/callers identify the correct routine.

### Task 3: Player and combat menu ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderCombatOptions`, `RenderPlayer`, `RenderMountOptions`)
- Read: `src/game/player.cpp`
- Read: `src/game/teleport.cpp`
- Record only in: this plan

**Interfaces:**
- Consumes: Task 2 char-manager, damage, crime, movement, and locomotion contracts.
- Produces: a separate semantic verdict for every exposed player/combat action.

- [ ] `One-Hit Kill`: test normal enemy and boss; verify only outgoing player damage is multiplied; disable and verify normal damage.
- [ ] `God Mode`: test each of the three protagonists, after character swap, mount/dismount, combat transient, and reload; verify no NPC/mount/companion is pinned accidentally.
- [ ] `Infinite Item Durability`: use weapon, shield, and armor durability paths; verify 100% lock and normal loss after disabling.
- [ ] `No Fall Damage`: test a controlled damaging fall and Sky Arrival; verify ordinary fall damage returns when disabled.
- [ ] `Infinite Stamina & Mount`: test sprint, dodge, climb, and mount gallop for each relevant body; verify target discrimination and disable behavior.
- [ ] `Infinite Spirit`: spend the actual special-ability gauge on each protagonist; do not accept a full secondary field as proof.
- [ ] `No Bounty`: confirm assault/theft/murder paths, guard alert, UI banner, and wanted value; disable and prove ordinary crime behavior returns.
- [ ] `Outgoing Damage`: test `1.0x`, a non-default multiplier, and restore; distinguish it from One-Hit Kill.
- [ ] `Incoming Damage`: test `1.0x`, reduction, amplification, and restore; verify it does not scale outgoing damage.
- [ ] `Super Run`: test grounded/airborne, character swap, and mount transition; verify restored speed after disabling.
- [ ] `Super Jump`: test rising-only scaling and landing; verify no repeated vertical acceleration or stale owner after transition.
- [ ] `Free Flight`: test enable, move, Fly Up, Fly Down, grounded recovery, character swap, and disable; verify no stale velocity.
- [ ] `Trust Multiplier`: test one positive NPC gain and one pet/mount gain with exact before/after values, then repeat at `1.0x`/disabled.

### Task 4: Travel and teleport menu ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderTravel`, `RenderFastTravel*`, `RenderSavedLocation*`)
- Read: `src/game/teleport.cpp`
- Read: `src/game/map_marker.h`
- Record only in: this plan

**Interfaces:**
- Consumes: Task 2 movement owner, marker, travel trigger, table-resolver, and position-layout contracts.
- Produces: final-applied-position evidence for every teleport route.

- [ ] Coordinate display/copy: walk to a known place, compare displayed XYZ with live player state, and verify clipboard output.
- [ ] `Teleport to Destination`: move a right-click waypoint at least three times; test specified height, Sky Arrival fallback, missing marker, invalid context, final readback, and no snap-back.
- [ ] Marker hotkey: repeat the destination test through the keyboard/controller binding and require the same final status.
- [ ] `Sky Arrival Altitude`: test one safe non-default value and restore it without changing unrelated settings.
- [ ] Saved Locations: save, rename, teleport, update to current position, delete one, and clear all using disposable entries.
- [ ] Fast Travel catalog: load categories and names, invoke at least one ordinary node and one different category, and verify actual destination rather than the `Warping` toast.
- [ ] Confirm travel calls use the current function entry/Win64 arguments and do not pass stale scene/node identifiers after table reload.

### Task 5: Inventory, money, Abyss, and restore menu ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderInventory*`, `RenderAddAll`, `RenderSetAll`, `RenderRestore*`)
- Read: `src/game/inventory.cpp`
- Read: `src/game/inventory_logic.*`
- Read: `src/core/version_mapping.*`
- Record only in: this plan

**Interfaces:**
- Consumes: Task 2 inventory getter, holder, constructor, placement, transaction, realm, table, slot, and stack contracts.
- Produces: authoritative server/client and persistence verdicts for every inventory action family.

- [x] Confirm catalog/search/category tables load on revision 2850.
- [x] Confirm current `Add Item` path is refused, persistent quantity commit is unavailable, and Slot Size reports unavailable.
- [ ] `Max Stack Size` and `Set Max Stack Value`: test a stackable material, a non-stackable/unique item, pickup, vendor purchase, split/merge, reload, and disable behavior.
- [ ] Item Editor: test Refresh, storage/category/search navigation, one quantity edit, and `Set All`; require reconcile/reload persistence.
- [ ] `Add Item`, per-category add, global search add, and `Add All`: test only after Task 2 proves the complete modern transaction path; require `server=1 client=1` and persistence.
- [ ] Money Optional: test fake-wallet cleanup and stack consolidation on a disposable backed-up save state; verify vendor behavior afterward.
- [ ] Money actions: exact amount, add amount, 1M/10M/20M presets, spawn pouches, and cash-in pouches; verify authoritative balance, purchase use, and reload persistence.
- [ ] Sealed Artifacts: target count, add missing to target, add all missing, and duplicate cleanup; verify uniqueness and collection counts after reload.
- [ ] Abyss material presets: custom count, 100/500 artifacts, Blessing, manuals, seeds, cells, vitality, spirit, and breath; verify each TypeID/name and usable quantity independently.
- [ ] Restore catalog: each of the eight exposed categories, individual restore, and restore-all-missing; a success in one category does not validate the others.
- [ ] Lost/Sold Items: individual restore, restore all, search, and clear history; verify restored quantity and history behavior separately.

### Task 6: Equipment menu ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderEquipSlots`, `RenderEquipEdit`, `RenderEquipGear`, `RenderEquipSwap`)
- Read: `src/game/equipment.cpp`
- Record only in: this plan

**Interfaces:**
- Consumes: Task 2 active-character/equip identity, refresh, resize-vector, slot, instance, and persistence contracts.
- Produces: per-character equipment edit and derived-effect evidence.

- [ ] Character selector: verify Kliff, Oongka, and Deadeye independently, including swap and companion/preview actors.
- [ ] `Repair All Gear`: damage multiple equipment types, repair, verify values and persistence.
- [ ] `Max Refine All (+10)`: verify intended equipped instances only and confirm derived stats refresh.
- [ ] `Unlock All Sockets`: verify vector size/capacity, UI visibility, valid socket count, and no corruption after re-equip/reload.
- [ ] Per-item actions: unlock sockets, clear sockets, set refinement, and save behavior; verify the exact selected instance ID and slot tag.
- [ ] Abyss gear insertion/removal/search: verify compatible slot/category rules, item ownership, effect refresh, and persistence.
- [ ] Equipment swap/filter actions: test character and category filters, equip a replacement, return to original, and verify all three characters.
- [ ] Resolve the ambiguous effect-refresh candidate before accepting any stat/effect result from an edit.

### Task 7: World, time, and weather menu ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderWorld`, `RenderTimePresets`, `RenderWeatherAtmosphere`)
- Read: `src/game/world.cpp`
- Record only in: this plan

**Interfaces:**
- Consumes: Task 2 frame timer, field clock, TOD manager, weather hooks, wind pack, and environment-manager contracts.
- Produces: independent simulation, clock, sun, precipitation, wind, cloud, and fog verdicts.

- [ ] `Game Speed`: test `1.0x`, non-default speed, pause-sensitive behavior, and restore; verify simulation rather than only UI animation.
- [ ] `Freeze Time of Day`: verify numeric clock and visible sun remain fixed, then resume normally.
- [ ] `Advance Time (+)` and `Rewind Time (-)`: test exact increments and both client/server field clocks.
- [ ] Dawn, Midday, Sunset, Midnight, and `Set Exact Hour`: validate clock, sun, and persistence/normal progression.
- [ ] Weather preset selector and `Instant Clear Weather`: verify actual atmosphere and safe restoration.
- [ ] `Clear Distant Fog` and `Force Clear Sky`: verify each independently; current missing `kSig_EnvManager` makes these priority checks.
- [ ] Rain, Snow, and Dust intensity: test zero, non-default, and normal restore separately.
- [ ] Cloud thickness/top/base/drift and fog A/B: verify each control independently; do not infer them from the installed wind hook.
- [ ] Wind speed, gust, turbulence lift, and `No Wind`: verify physical/visual result and restoration.

### Task 8: System and overlay menu ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderKeybinds`, `RenderFontSettings`, `RenderMenuUISettings`, `RenderSystem`)
- Read: `src/core/settings.cpp`
- Read: renderer/input/localization sources in `src/gui` and `src/core`
- Record only in: this plan

**Interfaces:**
- Consumes: Task 1 loaded-overlay identity and the current settings file.
- Produces: UI/input/persistence verdicts independent of gameplay AOB compatibility.

- [x] Confirm the DX12 overlay initializes and renders on the current build.
- [ ] Open/close menu by keyboard and controller; verify text capture and no stuck input.
- [ ] Rebind Open Menu, Marker Teleport, Fly Up, and Fly Down; test each, reset all keybinds, and restore the original settings.
- [ ] Built-in font fallback, custom font enable/select, and restart behavior; verify invalid/missing fonts fail safely.
- [ ] Menu Scale, item tooltip, tooltip image size, and window resize/HDR/Frame Generation behavior.
- [ ] Theme, PlayStation icons, every discovered language, FPS counter, and console window.
- [ ] Auto Save Features: test save/load across restart, then `Reset All to Default`; preserve a backup of personal `Trinity.ini` before any later semantic session.
- [ ] Confirm unknown PE revision is visibly reported as unsupported/under audit until compatibility is actually proven.

### Task 9: Produce the final no-change audit report

**Files:**
- Update only: this plan's status rows and evidence notes.
- Do not modify source or installed artifacts.

**Interfaces:**
- Consumes: Tasks 1-8 evidence.
- Produces: one decision table suitable for a later, separately authorized repair plan.

- [ ] For every exposed action, record `live passed`, `live failed`, `blocked`, or `not tested`, plus the exact scenario, executable hash, ASI hash, and relevant log lines.
- [ ] Split needed work into four exact buckets: revision-policy changes, AOB/function re-finds, structure/offset updates, and semantic regressions with surviving hooks.
- [ ] For each proposed update, state the confirmed root cause and the minimum affected source files; do not propose speculative byte changes.
- [ ] List removed/unreachable dye and parry/evade code separately so it is not accidentally treated as a current-menu regression.
- [ ] End with a strict repair gate: no implementation, build, deployment, or public compatibility claim until the user explicitly approves a new implementation plan.

## Current decision summary

- **Definitely needs later attention:** revision 2850 mapping/display, readiness profile, exact-2760 ABI policy, inventory Add Item/commit/slot paths, character-manager redundancy, movement-owner offset verification, realm/TLS offset verification, equipment refresh ambiguity, and environment-manager resolution.
- **Static/hook evidence survived but semantic testing is still required:** damage/combat, movement, marker teleport, fast travel, item count/catalog, crime hooks, game speed/time, precipitation/wind, localization, and trust observers.
- **Confirmed currently unavailable:** Add Item and every menu action that depends on the native add transaction; durable quantity edits; Slot Size.
- **Not part of the current exposed menu:** dye pages and Easy Parry/Easy Evade, according to the working-tree routes/state. Their old signatures must not inflate the active regression count.
- **No source repair has been authorized or performed.**
