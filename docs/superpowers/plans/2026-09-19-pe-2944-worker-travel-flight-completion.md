# PE 2944 Worker, Travel, and Free Flight Completion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the Crimson Desert PE 2944 compatibility update without turning a known unrelated branch into a risky patch. Restore map-destination teleport first, then identify and validate the real Worker, native Fast Travel, and locomotion paths.

**Architecture:** Keep each feature independent. Signatures only locate a candidate; its function boundary, Win64 calling contract, live event, and semantic game result must agree before the feature is enabled. Direct map-destination reads may replace obsolete marker-capture detours, while Worker, native Fast Travel, and Free Flight must remain fail-closed until their new PE 2944 contracts are proven.

**Tech Stack:** C++17, CMake/Ninja, MinHook, Cheat Engine MCP read-only scans/disassembly and narrowly scoped non-breaking trace capture, CrimsonDesert.exe PE 1.0.0.2944.

**Spec:** User request in this task; live console/screenshots from 2026-09-19; [GAME_UPDATE_PLAYBOOK.md](C:/Users/mul0/Documents/GitHub/Trinity/GAME_UPDATE_PLAYBOOK.md).

## Global Constraints

- Scope is Crimson Desert PE revision 2944 only. Do not let revision 2944 inherit a generic fallback intended for an older executable.
- Keep the working tree's unrelated edits intact. Make focused edits only, and do not overwrite `bin64/Trinity.asi` while the game is running.
- Do not use a CE code patch as a substitute for Trinity behavior. CE is evidence collection only for this update.
- Treat a signature match as locator evidence, not semantic proof. Static/build/CTest proof and in-game proof are reported separately.
- Before changing a patch byte or calling a recovered game function, establish a unique location, surrounding function boundary, exact ABI, live trigger, and a visible ON/OFF/restore result.
- Update nearby code comments whenever the recovery evidence changes the contract, so the next PE update begins from the right constraint rather than a stale guess.

## Current Evidence and Status

| Area | Status | Evidence and conclusion |
|---|---|---|
| PE 2944 revision mapping | Implemented | Version is detected as `PE 2944`; TU 2.01-compatible inventory and gameplay paths are selected. |
| DX12/MinHook fallback | Implemented and live | The earlier `MH_ERROR_MEMORY_ALLOC` failures are gone; current player, inventory, frame-timer, equipment, and trust hooks install. |
| Inventory / Add Item | Implemented and live | Console records the modern constructor, holder, commit path, catalog, and an addition with `server=1 client=1`. The startup instruction to open inventory or pick up an item is already displayed. |
| Player, equipment, trust, frame-time | Partially live-verified | Current console shows their relevant hooks/observers installed. Each still needs an ON/OFF/persistence regression before a release claim. |
| Map-destination reader | Live locator verified | PE 2944 has exactly one direct UI destination reference at `0x140DEFCFE`; it resolves the map marker shown by the menu. |
| Map-destination teleport | Source repair built; live re-test pending | Five old marker-capture sites still match, but all five inline detours fail. The new readiness rule permits the verified direct UI reader plus origin and movement owner, instead of rejecting `hooks=0/5`. |
| Native Fast Travel menu | Blocked by changed contract | All three old trigger signatures return zero matches in the live PE 2944 module. The menu is correctly disabled rather than calling an unknown function. |
| Worker level and skills | Safely disabled | The old six-byte AOB still matches at `0x14214BE8C`, but its surrounding code is a multi-way priority/result branch. It is not proof of a Worker level/ability writer, so enabling it would be an unsafe guess. |
| Free Flight and Super Run | Blocked by changed locomotion locator | Both current and pre-2.01 locomotion-stepper AOBs return zero live matches. Their common callback never installs, so Free Flight cannot process input or write player velocity. |
| Crime UI bypass / legacy money | Disabled by design | Current signatures no longer identify their old contracts. They need separate recovery, not a broad pattern fallback. |

## Review Focus

1. A feature that has an unresolved locator must stay unavailable in both code and menu feedback.
2. A direct, validated source must not be made contingent on an obsolete optional detour.
3. Worker recovery must begin from a normal worker-level/skill state transition, never from the old branch bytes.
4. Fast Travel must use the game's own verified native call with its PE 2944 ABI, not map-marker coordinates or a guessed function pointer.
5. Free Flight and Super Run share the locomotion hook but have separate input/physics tests; one passing does not prove the other.

---

## Task 1: Preserve a PE 2944 baseline before every recovery pass

**Files:**
- Modify: `docs/superpowers/plans/2026-09-19-pe-2944-worker-travel-flight-completion.md`
- Read: `src/core/version_mapping.cpp`, `src/game/offsets.h`, `src/game/teleport.cpp`, `src/game/worker.cpp`, `Trinity.log`

- [ ] Record the current executable revision, process ID, module base/size, Trinity build hash, installed-ASl hash, and the exact console line for every feature under investigation.
- [ ] Start each scenario with Trinity's corresponding toggle OFF and with no CE injection or byte patch enabled.
- [ ] Record a reproducible in-game action and a visible expected result before setting any trace/watch evidence.
- [ ] Remove a temporary trace/watch immediately after one captured event and record the captured instruction/function plus register/argument evidence.

**Acceptance criteria:** The evidence ledger distinguishes PE 2944 runtime facts from historical 2850 facts and contains no unsupported address promoted as a production signature.

## Task 2: Allow map-destination teleport without obsolete capture hooks

**Files:**
- Modify: `src/game/marker_teleport_logic.h`
- Modify: `src/game/teleport.cpp`
- Modify: `tests/map_marker_tests.cpp`

- [x] Add `MarkerTeleportCanUseDestination`: a validated direct destination and origin make marker teleport available even when capture hooks are zero; legacy captures remain a fallback source.
- [x] Reset the origin during subsystem initialization, preserve the direct UI reader, and log whether the direct route or capture fallback supplies the marker.
- [x] Keep missing origin fail-closed; never queue coordinates whose world-space conversion cannot be validated.
- [x] Add and execute the regression that proves `direct=true`, `origin=true`, `hooks=0` is usable while no marker source or no origin remains unusable.
- [ ] With the fresh built ASI installed after the game closes, set a map marker with zero altitude, press the menu action once, and confirm the log sequence `queued` then `applied and verified` rather than `write failed verification after 3 attempts`.
- [ ] Repeat with a nonzero-altitude destination, a character switch, and a no-marker map. Confirm a normal landing, no stale teleport, and correct error toast.

**Acceptance criteria:** A live PE 2944 session with `hooks=0/5` can teleport to the active UI map marker via the direct reader, and log/readback prove the movement write persisted.

## Task 3: Recover the actual Worker level-and-skills contract

**Files:**
- Modify: `src/game/worker.cpp`
- Modify: `src/game/worker_logic.h`
- Modify: `src/game/offsets.h`
- Modify: `tests/verify_worker_feature_contract.ps1`
- Modify: `tests/readiness_tests.cpp`

- [ ] Select one worker with a known level and skill state; capture before/after screenshots while making exactly one legitimate level or skill change through the game UI.
- [ ] Use the changed field to collect one non-breaking write/access event. Preserve the writing instruction, register state, owning object pointer, and function boundary; clear the watch immediately after capture.
- [ ] Disassemble the writer and its callers until the real level/skill gate and its inputs are identified. Reject a candidate if it only selects an inventory priority, error code, UI state, or result category.
- [ ] Define a PE 2944-specific immutable original-byte contract only after the real gate has one exact match and semantic proof. Validate every original and enabled byte before patching, and restore only bytes Trinity previously changed.
- [ ] Add tests for unsupported revisions, unexpected bytes, unique candidate enforcement, OFF-to-ON transition, ON-to-OFF restoration, and a wrong-candidate rejection.
- [ ] Exercise Worker OFF, ON, OFF in-game against the same worker and after a reload. Confirm the intended level/skills change, no unrelated inventory behavior change, and byte restoration on removal.

**Acceptance criteria:** Worker changes only after a user-visible normal worker event identifies its PE 2944 writer; the old `0F 85 95 00 00 00` priority branch remains unpatched unless it independently satisfies that proof.

## Task 4: Re-find the PE 2944 native Fast Travel trigger and ABI

**Files:**
- Modify: `src/game/offsets.h`
- Modify: `src/game/teleport.cpp`
- Add/Modify: `tests/travel_logic_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] Use a known unlocked in-game Fast Travel destination and invoke the game's native Fast Travel UI once with Trinity's Fast Travel action disabled.
- [ ] Collect one non-breaking function trace from the UI dispatch path, then identify the final game-side trigger that receives the scene/node destination.
- [ ] Confirm the PE 2944 function start, its unique AOB, argument order/types, return convention, and the exact table/registry entries used by the call.
- [ ] Add a PE 2944-only signature and call wrapper. Leave the current and legacy signatures as their explicitly scoped older-build branches.
- [ ] Add a small behavior test for the resolver result and invalid/unavailable destination refusal; build the test target in CMake.
- [ ] Test one valid node, a locked/unavailable node, menu close/reopen, and reload. Confirm only the native action travels, no crash occurs, and unavailable nodes fail with a specific log/toast.

**Acceptance criteria:** The Fast Travel menu is populated only when the PE 2944 native trigger and registry are both verified; each accepted entry invokes the real game flow once.

## Task 5: Re-find the PE 2944 locomotion stepper for Free Flight and Super Run

**Files:**
- Modify: `src/game/offsets.h`
- Modify: `src/core/version_mapping.cpp`
- Modify: `src/core/version_mapping.h`
- Modify: `src/game/teleport.cpp`
- Modify: `tests/readiness_tests.cpp`

- [ ] Trace a normal local-player locomotion frame to locate the driver that receives the live velocity vector, without relying on either zero-match old prologue.
- [ ] Confirm the function's unique PE 2944 AOB, full x64 ABI, player-component owner field, and that its velocity argument is writable for the whole original call.
- [ ] Add a PE 2944-specific stepper signature and owner offset only when both are observed in the live function. Do not route PE 2944 through the `0x298` legacy fallback by assumption.
- [ ] Update comments beside the signature and owner mapping with the evidence that distinguishes it from PE 2850.
- [ ] Add mapping tests that reject unknown revisions and select the PE 2944 value only after that direct evidence exists.
- [ ] Test Free Flight on an airborne player: rise, sink, horizontal propulsion, release, land, OFF, then reload. Separately test Super Run horizontal speed and ensure Super Jump remains unchanged.

**Acceptance criteria:** The console confirms the PE 2944 locomotion hook installed; Free Flight visibly responds to its configured keys/controller controls only for the local player and returns fully to normal physics when disabled.

## Task 6: Complete the remaining PE 2944 feature ledger

**Files:**
- Modify: `docs/superpowers/plans/2026-09-19-pe-2944-worker-travel-flight-completion.md`
- Read/Modify as needed: `src/game/player.cpp`, `src/game/inventory.cpp`, `src/game/world.cpp`, `src/game/equipment.cpp`, `src/game/friendly.cpp`, `src/game/time.cpp`

- [ ] Exercise player stat guard/damage and combat timing with OFF/ON/OFF scenarios; label any unavailable subfeature rather than grouping it with a passing hook.
- [ ] Exercise Add Item after the inventory bootstrap instruction, use/equip the item, reload, and retain only `server=1 client=1` as persistent-success evidence.
- [ ] Exercise equipment socket/refinement on each playable character, including character switching and save/reload persistence.
- [ ] Exercise trust multipliers for NPC and pet/mount independently, compare baseline and multiplier result, and confirm OFF restores normal gain.
- [ ] Exercise time/weather/world features separately; recover or explicitly label the still-missing crime and legacy money contracts.
- [ ] Update the table in this document with `PASS`, `FAIL`, `BLOCKED`, or `NOT TESTED`, plus exact log evidence and restoration result for each row.

**Acceptance criteria:** No broad "updated" claim remains; every menu capability has an individual runtime status and a concrete next action.

## Task 7: Build, deploy safely, and publish verification evidence

**Files:**
- Read: `build-clean/Trinity.asi`
- Read: `E:/Steam/steamapps/common/Crimson Desert/bin64/Trinity.asi`
- Modify: release/checksum files only for an explicitly requested release

- [ ] Build `Trinity`, `TrinityMapMarkerTests`, `TrinityReadinessTests`, and `TrinityMinHookFallbackTests` from a Visual Studio x64 developer environment.
- [ ] Run CTest and save the complete result with the source revision and SHA-256 of the build artifact.
- [ ] Close Crimson Desert before replacing the ASI; hash and make a timestamped backup of the installed artifact, then copy the exact verified build and compare hashes.
- [ ] Restart the game fresh and collect the relevant success/failure logs for each completed task.
- [ ] Do not package, publish, or claim a full PE 2944 release until every user-facing feature is either live-verified or clearly shown as unavailable.

**Acceptance criteria:** The installed artifact exactly matches the verified build artifact, and the release record cleanly separates static test results from fresh live behavior.
