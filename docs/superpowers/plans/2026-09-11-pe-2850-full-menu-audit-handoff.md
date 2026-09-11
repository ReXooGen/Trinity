# Crimson Desert 2.02.00 / PE 2850 Full Menu Audit Handoff

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:systematic-debugging` for every failed feature. This document is an audit runbook, not authorization to modify source, build, deploy, write process memory, or change Cheat Engine scripts. If a repair is required, create a separate implementation plan and obtain explicit approval first.

**Goal:** Verify every currently exposed Trinity menu action against Crimson Desert `2.02.00` / PE `1.0.0.2850`, identify only confirmed regressions, and leave evidence precise enough for a later repair without redoing discovery.

**Architecture:** Work in independent feature families. For each family, retain four separate results: clean-EXE AOB/offset evidence, initialized live hook evidence, controlled in-game semantic evidence, and toggle-off/restore evidence. A function is marked compatible only after the semantic and restore stages pass.

**Tech Stack:** C++20, Win64, MinHook, ImGui, PowerShell, Cheat Engine MCP, Crimson Desert, Trinity ASI.

**Spec:** `GAME_UPDATE_PLAYBOOK.md` and the earlier baseline `docs/superpowers/plans/2026-09-11-crimson-desert-pe-2850-compatibility-audit.md`.

## Global Constraints

- Preserve all existing dirty-worktree changes. This handoff file is documentation only.
- Before any build/deployment, ensure `CrimsonDesert.exe` is closed; back up the installed `Trinity.asi` and compare SHA-256 after copying.
- During audit-only work, use Cheat Engine MCP read-only: process/module inventory, AOB scans, memory reads, disassembly, and breakpoints only when required to establish an ABI/offset.
- Never change an AOB, structure offset, version policy, hook, game memory, or CE script merely because an action appears broken. Record the root cause first.
- A unique AOB match or an `installed` log line is not a working feature. A visible client-only item is not an Add Item success.
- For Add Item, require `inventory: Added ... [server=1 client=1]`, then use/equip and persistence through a relevant reload.
- For map-marker teleport, `Queued` is not success: require arrival at the selected coordinates and no snap-back.
- Test enable and disable states. A feature that cannot restore ordinary gameplay is a failure.
- Do not audit deleted/unreachable UI paths as regressions. Current working-tree dye pages and Easy Parry/Easy Evade are outside the active menu surface.

---

## Known-good baseline after the 2.02.00 repair

| Item | Evidence | Current status |
| --- | --- | --- |
| Game identity | Crimson Desert `2.02.00`, PE `1.0.0.2850` | Confirmed |
| Revision mapping | `2850 -> 2.02.00`; modern TU 2.01-compatible policy includes `2850` | Implemented and built |
| TLS realm selector | Live disassembly identified `0x1EC`; code maps PE 2850 to `0x1EC` | Implemented; Add Item live-confirmed |
| Inventory authority holder | Passive `GetInventoryHolder` observer captures distinct server/client holders once the inventory transaction becomes available | Implemented; live-confirmed |
| Add Item | User observed refusal before holder capture, then successful `Added ... [server=1 client=1]` after a pickup | Semantic pass, startup timing still needs retest |
| Movement owner | Live locomotion disassembly and pointer capture proved owner at `moveComponent + 0x2B8` | Implemented and built |
| Super Run | User confirmed working after the `0x2B8` correction | Semantic pass |
| Free Flight | User confirmed working after the `0x2B8` correction | Semantic pass |
| Latest verified build | `CF3963569C58C9CB903C27941D17A723ECCDCA2B3CA4E143AC071F5A5BDDDA1E` | Installed hash matched build hash |
| Deployment backup | `E:\Steam\steamapps\common\Crimson Desert\bin64\Trinity.asi.pre-movement-owner-2850-20260911-114246.bak` | Preserved |

Important interpretation: Add Item currently becomes available after the game creates/captures an authoritative transaction holder. The next audit must check whether opening the inventory or loading into a save captures it early enough. Do not weaken the safe refusal when `server=0`.

## Audit status labels

| Label | Meaning |
| --- | --- |
| `PASS` | Required semantic scenario and toggle-off/restore behavior both passed on PE 2850. |
| `STATIC` | AOB/offset/hook evidence exists, but no controlled in-game proof yet. |
| `BLOCKED` | Cannot test safely until a prerequisite contract is proven. |
| `FAIL` | A controlled in-game test failed and a log/CE observation identifies the immediate cause. |
| `NOT TESTED` | No controlled test performed. |
| `EXCLUDED` | Not reachable from the current active menu. |

## Required evidence row for every test

Copy this row into the result section when performing a test:

```markdown
| Feature | PE / EXE SHA | ASI SHA | Scenario | Hook/AOB evidence | In-game result | Restore result | Status | Log / CE evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
```

Record the exact control value, selected character, map/context, and whether a save reload was performed. Do not use a generic “works” result.

---

### Task 1: Restart baseline and boot integrity

**Files:**
- Read: `Trinity.log` in the game directory
- Read: `src/core/version_mapping.cpp`
- Read: `src/core/readiness.cpp`
- Read: `src/core/mod.cpp`
- Update only: this handoff document

**Interfaces:**
- Consumes: closed game and the installed ASI.
- Produces: a single PE-2850 / ASI-hash baseline for the remaining ledger.

- [ ] Start the game, load a normal save, wait until the overlay reports ready, and preserve the startup block of `Trinity.log`.
- [ ] Confirm the log names `Crimson Desert 2.02.00 (Active)` and does not select an older fallback profile.
- [ ] Record EXE version, EXE SHA-256, installed ASI SHA-256, module base, process ID, and `Trinity` build line.
- [ ] Confirm overlay keyboard open/close with `Insert`, then controller open/close with the configured binding.
- [ ] Check that player, teleport, inventory, world, equipment, and trust initialization lines either resolve or name a specific missing dependency.
- [ ] With no pickup yet, open/close the inventory once and attempt one harmless known item add. Record whether the new holder observer now has both `server=1 client=1` or still correctly refuses.
- [ ] Pick up one ordinary item only if the previous step still lacked a holder; repeat the same Add Item test and record the transition.

### Task 2: Static AOB and revision-policy sweep

**Files:**
- Read: `src/game/offsets.h`
- Read: `src/core/version_mapping.*`
- Read: `src/game/player.cpp`, `src/game/teleport.cpp`, `src/game/inventory.cpp`, `src/game/world.cpp`, `src/game/equipment.cpp`, `src/game/friendly.cpp`
- Update only: this handoff document

**Interfaces:**
- Consumes: Task 1 executable identity and a live PE-2850 process.
- Produces: a signature/offset matrix that distinguishes broken contracts from untested behavior.

- [ ] Scan every active `kSig_*` used by `Player`, `Teleport`, `Inventory`, `World`, `Equipment`, and `Friendly` against the clean PE image; record count and RVA.
- [ ] For a clean unique entry that is absent live, disassemble its live address and classify a MinHook entry jump as `hook installed`, not as a missing signature.
- [ ] For every multiple match, use the documented string/predicate/caller constraint. Never accept the first raw match.
- [ ] Disassemble each accepted target through argument setup and first material read/write; record Win64 registers, stack arguments, RIP targets, and member offsets actually used.
- [ ] Search all exact `revision == 2760` comparisons and record whether PE 2850 correctly follows the modern path or needs separate live proof.
- [ ] Revalidate the revision-dependent values now known for 2850: movement owner `0x2B8`, realm selector `0x1EC`, and modern inventory core-global policy `0`.
- [ ] For any candidate change discovered during this sweep, write `FAIL` or `BLOCKED` with evidence; do not alter source.

### Task 3: Player, combat, and movement ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderCombatOptions`, `RenderPlayer`, `RenderMountOptions`)
- Read: `src/game/player.cpp`
- Read: `src/game/teleport.cpp`
- Update only: this handoff document

**Interfaces:**
- Consumes: Task 2 character-manager, damage, and locomotion contracts.
- Produces: per-character PASS/FAIL results for all combat/player actions.

- [ ] `God Mode`: test Kliff, Oongka, and Damiane after a character switch, reload, enemy hit, and toggle-off. Verify no companion/NPC receives the pin.
- [ ] `One-Hit Kill`: test one regular enemy and one resilient target; verify only player outgoing damage changes and disable restores normal damage.
- [ ] `Outgoing Damage` and `Incoming Damage`: test `1.0x`, one non-default value, and restore independently; ensure they do not affect each other.
- [ ] `Infinite Item Durability`: consume durability on weapon, armour, and shield paths; verify normal reduction returns when off.
- [ ] `No Fall Damage`: make one safe controlled fall and one Sky Arrival; toggle off and confirm ordinary fall damage.
- [ ] `Infinite Stamina & Mount`: test sprint, dodge, climb, and mounted travel; repeat after character/mount transition.
- [ ] `Infinite Spirit`: spend the real ability resource for each protagonist and verify it, not merely a visually similar meter, remains available.
- [x] `Super Run`: confirmed by user after the PE-2850 move-owner fix. Recheck a character switch, airborne state, mount transition, and toggle-off restore.
- [ ] `Super Jump`: check rising-only multiplication, landing, character switch, and no stale vertical velocity.
- [x] `Free Flight`: confirmed by user after the PE-2850 move-owner fix. Recheck WASD, Fly Up, Fly Down, landing, character switch, and disable clearing velocity.
- [ ] `Trust Multiplier`: test one NPC gain and one pet/mount gain using recorded before/after values at `1.0x` and a non-default multiplier.
- [ ] `No Bounty`: test a controlled crime/alert path while on, then off; verify UI, wanted value, and normal bounty behavior return.

### Task 4: Travel and teleport ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderTravel`, `RenderFastTravelCats`, `RenderFastTravelNodes`, `RenderSavedLocations`)
- Read: `src/game/teleport.cpp`
- Read: `src/game/map_marker.h`
- Update only: this handoff document

**Interfaces:**
- Consumes: Task 2 position layout, map marker, table resolver, and travel-trigger contracts.
- Produces: final-coordinate proof for every exposed teleport route.

- [ ] Position display and clipboard copy: compare shown coordinates with live player coordinates and inspect the clipboard text.
- [ ] Map marker teleport: move the marker three times, test an ordinary ground marker and Sky Arrival fallback, then verify final XYZ and no snap-back.
- [ ] Map marker hotkey: repeat the final-coordinate test with the configured keyboard and controller bindings.
- [ ] Missing marker and unsafe-context paths: verify failure is clean and does not move the player.
- [ ] Saved locations: create a disposable location, rename it, update it, teleport, delete it, and clear another disposable entry.
- [ ] Fast-travel catalog: load categories, visit one normal node and a node in a different category, and verify the actual destination after load.

### Task 5: Inventory, Add Item, money, Abyss, and restore ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderInventoryHome`, `RenderInventoryEditor`, `RenderInventoryStorage`, `RenderInventoryAdd`, `RenderInventoryMoney*`, `RenderInventoryAbyss`, `RenderInventoryRestore`)
- Read: `src/game/inventory.cpp`
- Read: `src/game/inventory_logic.*`
- Update only: this handoff document

**Interfaces:**
- Consumes: Task 1 authority capture and Task 2 inventory transaction/realm ABI evidence.
- Produces: authoritative and persistent verdicts for every inventory family.

- [ ] Catalog/search/category list: verify item count, several names, category filters, and a search result.
- [ ] Add Item: test one stackable consumable, one equipment item, and one unique/special item. Require `server=1 client=1`, usable/equippable state, and persistence after reload.
- [ ] Add category / Add All: use a small harmless category first; verify quantity, no duplicates beyond requested amount, and persistence.
- [ ] Item Editor and Set All: change one reversible quantity, perform a normal pickup and vendor purchase, reload, and restore the original value.
- [ ] Max Stack Size / Set Max Stack Value: test stackable and non-stackable items, split/merge, pickup, vendor purchase, reload, and disabled-state restoration.
- [ ] Slot Size: test a conservative capacity change only after the 2850 guard contract is proven. Validate normal pickup, purchase, save/load, and no engine error; never exceed the known safe ceiling of 700.
- [ ] Money optional cleanup: use a backed-up disposable save, run cleanup/consolidation, verify wallet/vendor behavior, then restore the save if needed.
- [ ] Money controls: test exact/add amounts, 1M/10M/20M presets, pouch spawn, and pouch cash-in; require authoritative balance and reload persistence.
- [ ] Abyss materials: test one item from each exposed family (artifacts, blessing, manuals, seeds, cells, vitality, spirit, breath) and verify TypeID, displayed name, usable quantity, and reload persistence.
- [ ] Restore pages: test one disposable entry from each exposed restore category individually before Restore All Missing; verify actual collection state after reload.
- [ ] Lost/Sold Items: test individual restore, restore all, search, and clear-history behavior separately.

### Task 6: Equipment ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderEquipSlots`, `RenderEquipEdit`, `RenderEquipSwap`, `RenderEquipGear`)
- Read: `src/game/equipment.cpp`
- Read: `src/game/equipment_logic.*`
- Update only: this handoff document

**Interfaces:**
- Consumes: Task 2 active-character/equip identity, refresh, socket-vector, and persistence contracts.
- Produces: character- and instance-specific equipment verification.

- [ ] Character selection: independently inspect Kliff, Oongka, and Damiane; record selected character index, slot tag, and instance ID.
- [ ] Repair All Gear: damage at least two equipment types, repair, re-equip, and reload.
- [ ] Max Refine All: confirm only equipped instances change, derived combat values refresh, and results persist.
- [ ] Unlock All Sockets: validate exactly valid socket counts, UI visibility, re-equip/reload behavior, and absence of item corruption.
- [ ] Per-item refine/unlock/clear/add-gear actions: verify the selected slot tag and instance ID, compatible-category filtering, immediate effects, removal effects, and persistence.
- [ ] Equipment swap: equip a replacement then restore the original on every protagonist; verify filters do not offer incompatible gear.
- [ ] If an edit changes data but not effect, capture the chosen effect-refresh target/call ABI as a separate `FAIL`; do not accept an ambiguous refresh AOB.

### Task 7: World, time, and weather ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderWorld`, `RenderTimePresets`, `RenderWeatherAtmosphere`)
- Read: `src/game/world.cpp`
- Update only: this handoff document

**Interfaces:**
- Consumes: Task 2 frame timer, field time, TOD manager, weather, wind, and environment contracts.
- Produces: one independent semantic result per world control.

- [ ] Game Speed: test `1.0x`, a non-default value, pause/reload sensitivity, and restoration of normal simulation.
- [ ] Freeze Time, advance, rewind, presets, and exact hour: verify numeric clock, visible sun, and normal progression after disabling.
- [ ] Weather preset and Instant Clear Weather: verify actual atmosphere, not only the menu label, then restore normal weather.
- [ ] Clear Distant Fog and Force Clear Sky: test separately because they share environment dependencies but may use different members.
- [ ] Rain, snow, dust, wind speed, gust, turbulence lift, No Wind, cloud controls, and fog controls: test zero, one non-default value, and restore independently.
- [ ] Any world action with a missing/ambiguous environment manager is `BLOCKED`, not silently treated as a normal visual failure.

### Task 8: System, UI, and persistence ledger

**Files:**
- Read: `src/gui/menu.cpp` (`RenderKeybinds`, `RenderFontSettings`, `RenderMenuUISettings`, `RenderSystem`)
- Read: `src/core/settings.cpp`
- Read: `src/hooks/input.cpp`, `src/hooks/xinput_hook.cpp`, `src/hooks/dx12_hook.cpp`
- Update only: this handoff document

**Interfaces:**
- Consumes: Task 1 overlay startup baseline.
- Produces: UI/input/settings evidence independent of gameplay compatibility.

- [ ] Rebind menu, marker teleport, Fly Up, and Fly Down; test keyboard/controller bindings, reset, then restore personal bindings.
- [ ] Test menu scale, tooltip images, FPS counter, PlayStation icons, theme, each available language, and console visibility.
- [ ] Test built-in and custom fonts, including a missing font path; verify safe fallback and restart persistence.
- [ ] Test Auto Save Features, restart/load, and Reset All to Default only after backing up `Trinity.ini`.
- [ ] Exercise resize, HDR, and Frame Generation transitions while the menu is open and closed; verify no input capture or rendering regression.

### Task 9: Produce the repair-ready results

**Files:**
- Update only: this handoff document

**Interfaces:**
- Consumes: tasks 1-8 evidence rows.
- Produces: a minimal, separately authorized repair backlog.

- [ ] Group only confirmed defects as: revision-policy mapping, AOB/function rediscovery, structure/offset correction, ABI/realm correction, or surviving-hook semantic regression.
- [ ] For each `FAIL`, include source function, AOB/RVA/live VA, observed instruction/field, expected vs actual behavior, and minimum candidate source files.
- [ ] Keep `NOT TESTED` separate from `FAIL`; do not inflate the bug list.
- [ ] Record post-repair deployment provenance separately if a later user request authorizes implementation.
- [ ] Do not mark the whole menu compatible until every currently exposed action is `PASS` or explicitly `EXCLUDED`.

## First continuation session: recommended order

1. Run Task 1 after launching the game and loading a save.
2. Verify Tasks 3 and 4 first because movement/teleport share the newly corrected `+0x2B8` owner contract.
3. Verify Task 5 Add Item timing/persistence next, before bulk money, restore, or capacity actions.
4. Continue through equipment, world, and UI one family at a time; stop the family immediately on the first concrete hook/ABI failure and capture evidence instead of guessing.
5. Only after the ledger identifies a concrete `FAIL` may a separate update plan be drafted.

## Results ledger

Populate this section during the next continuation. Do not overwrite the known-good baseline above.

| Feature | PE / EXE SHA | ASI SHA | Scenario | Hook/AOB evidence | In-game result | Restore result | Status | Log / CE evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Add Item authority after inventory open/load |  |  |  |  |  |  | NOT TESTED |  |
| Super Run transitions |  |  |  | owner `+0x2B8` live-proven | basic run already confirmed |  | STATIC |  |
| Free Flight transitions |  |  |  | owner `+0x2B8` live-proven | basic flight already confirmed |  | STATIC |  |
| Map-marker teleport |  |  |  |  |  |  | NOT TESTED |  |
| Equipment effect refresh |  |  |  |  |  |  | NOT TESTED |  |
| World environment controls |  |  |  |  |  |  | NOT TESTED |  |
