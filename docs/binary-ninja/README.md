# Trinity Binary Ninja knowledge base

Durable, update-survivable reverse-engineering records for the Crimson Desert
functions that Trinity's menu actually uses. The goal is that a new executable
turns into a **comparison and targeted recovery** task instead of a hunt through
an unknown 380 MiB EXE.

This is deliberately **not** a rename-every-`sub_*` project. The game contains
far too many unrelated functions; a function is in scope only when evidence
connects it to a visible Trinity menu feature.

Governing plan: [`../../Trinity_Binary_Ninja_Menu_Analysis_Plan.md`](../../Trinity_Binary_Ninja_Menu_Analysis_Plan.md)
(read it before making any Binary Ninja change).

---

## Current snapshot

| Field | Value |
|---|---|
| PE revision | **2944** (`FileVersion 1.0.0.2944`) |
| Executable | `E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe` |
| SHA-256 | `6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7` |
| Image base / size | `0x140000000` / `0x17FCD000` |
| Binary Ninja | 6.0.10601-Stable, database `CrimsonDesert.exe.bndb` (view `view_1`, PE) |
| Database path | `E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe.bndb` |
| Snapshot record | [`snapshots/PE-2944.md`](snapshots/PE-2944.md) |

The database is ~3.35 GiB and lives beside the executable; it is referenced by
path rather than committed. The Markdown dossiers are the portable record and
must stay sufficient on their own.

---

## Status key

Every control in the inventory below carries exactly one status:

| Status | Meaning |
|---|---|
| **SUPPORTED** | Active on PE 2944, game-dependent, and documented in a dossier with a measured locator |
| **SUPPORTED‑gap** | Active, documented, but a known sub-path is inert or unproven — see the note |
| **UI‑ONLY** | Pure Trinity UI / local logic; no executable function dependency |
| **UNAVAILABLE** | Intentionally fail-closed on this revision (a locator or ABI is absent by design) |
| **LEGACY** | Dormant / dead code, or not safe to enable — documented so it is not revived by accident |

`SUPPORTED‑gap` exists because several features work while a *named part* of the
same row does not. The note column says which part.

---

## Dossier index

| Priority | Dossier | Trinity scope | Game functions documented |
|---|---|---|---|
| P0 | [`dossiers/travel.md`](dossiers/travel.md) | map marker, saved locations, Native Fast Travel | 5 confirmed + 1 candidate + marker-capture census |
| P0 | [`dossiers/locomotion.md`](dossiers/locomotion.md) | Super Run, Super Jump, Free Flight | 2 confirmed (`0x144282080`, `0x14369FF60`) + the `+0x2C0` move-owner contract |
| P0 | [`dossiers/inventory.md`](dossiers/inventory.md) | Add Item, Item Editor, stack size, money | 9 core anchors + realm contract |
| P0 | [`dossiers/player-combat.md`](dossiers/player-combat.md) | God Mode, stamina, spirit, damage, One-Hit Kill | 2 confirmed + 17 status/record functions + 1 absent |
| P1 | [`dossiers/equipment.md`](dossiers/equipment.md) | refine, repair, sockets, abyss gear, dye | 1 confirmed inline anchor + 5 measured-absent locators |
| P1 | [`dossiers/world-time-weather.md`](dossiers/world-time-weather.md) | game speed, time, freeze, weather, fog, wind | 4 named + 6 locator anchors + 2 measured-absent |
| P1 | [`dossiers/trust-worker.md`](dossiers/trust-worker.md) | trust multiplier, worker level and skills | 4 confirmed + 8 support + 5 dead locators |
| P2 | [`dossiers/crime-money.md`](dossiers/crime-money.md) | No Bounty, crime UI banner, legacy money | 1 confirmed + 2 explicitly unavailable |

---

## Complete menu feature inventory

205 live distinct control registrations across 5 tabs and 23 pages, plus 20
dead registrations in the orphaned dye editor. Loop-generated rows are counted
as one family each; `(cond.)` marks a row that only appears conditionally.

### Shell — always visible (5)

| Control | Type | Status | Note |
|---|---|---|---|
| `PLAYER` / `INVENTORY` / `TRAVEL` / `WORLD` / `SYSTEM` | tab ×5 | UI‑ONLY | shell tab strip (`src/gui/menu.cpp:35`) |

### PLAYER tab (45)

**Root — `RenderPlayer`, `menu.cpp:119-164` (7)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Combat & Gameplay Options` | submenu | UI‑ONLY | navigation |
| `Edit Equipment` | submenu | UI‑ONLY | navigation |
| `Max Worker Level & Skills` | toggle | **SUPPORTED** | 6-byte patch @ `0x14214BE8C` — [trust-worker](dossiers/trust-worker.md) §2 |
| `Super Run` | toggle + slider | **SUPPORTED** | `0x14369FF60` — [locomotion](dossiers/locomotion.md) §2 |
| `Super Jump` | toggle + slider | **SUPPORTED** | `0x144282080` (+0xC0) — [locomotion](dossiers/locomotion.md) §2 |
| `Free Flight` | toggle + slider | **SUPPORTED** | `0x14369FF60` — [locomotion](dossiers/locomotion.md) §2 |
| `Trust Multiplier` | toggle + slider | **SUPPORTED** | 4 locators — [trust-worker](dossiers/trust-worker.md) §1 |

**Combat & Gameplay Options — `menu.cpp:79-117` (9)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `One-Hit Kill` | toggle | **SUPPORTED‑gap** | `0x1417AE100`; label says 1,000×, code uses `10000.0f` — [player-combat](dossiers/player-combat.md) |
| `God Mode` | toggle | **SUPPORTED‑gap** | `0x1417AE100`; `kSig_StatCommit` is absent, so this is a per-frame pin only — [player-combat](dossiers/player-combat.md) §2 |
| `Infinite Item Durability` | toggle | **SUPPORTED** | `equipment.cpp:2033-2042` — [equipment](dossiers/equipment.md) |
| `No Fall Damage` | toggle | **SUPPORTED** | `0x1417AE100` (default `true`, `state.h:48`) |
| `Infinite Stamina & Mount` | toggle | **SUPPORTED‑gap** | player half OK; **mount half inert** when the TU 2.01 fallback path runs (`player.cpp:333-338`) |
| `Infinite Spirit` | toggle | **SUPPORTED** | `0x1417AE100` |
| `No Bounty` | toggle | **SUPPORTED‑gap** | wanted evaluator `0x1425D7660` active; **crime-banner / minimap / guard suppression UNAVAILABLE** — [crime-money](dossiers/crime-money.md) §3.1 |
| `Outgoing Damage` | slider | **SUPPORTED** | `0x1417AE100` |
| `Incoming Damage` | slider | **SUPPORTED** | `0x1417AE100` |

**Edit Equipment — `RenderEquipSlots`, `menu.cpp:584-670` (8)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Character` | preset | **SUPPORTED** | equipment table — [equipment](dossiers/equipment.md) |
| `Character not loaded` | label *(cond.)* | UI‑ONLY | readiness placeholder |
| `Waiting for your equipment...` | label *(cond.)* | UI‑ONLY | readiness placeholder |
| `Repair All Gear` | button | **SUPPORTED** | equipment table write |
| `Max Refine All (+10)` | button | **SUPPORTED** | `SyncRefineAllRealms` `equipment.cpp:674` |
| `Unlock All Sockets` | button | **SUPPORTED‑gap** | resize anchor resolves (`+0x5` into `0x14488C468`); **effect refresh unresolvable** |
| dynamic per equipped piece (`"<Slot> - <Item> (<n>/<max> sockets) [TypeID <n>]"`) | submenu | **SUPPORTED** | equipment table |
| `Nothing equipped` | label *(cond.)* | UI‑ONLY | placeholder |

**Edit Equipment → piece — `RenderEquipEdit`, `menu.cpp:672-776` (9)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Not equipped` | label *(cond.)* | UI‑ONLY | placeholder |
| `Unlock all sockets` | button *(cond.)* | **SUPPORTED‑gap** | see above |
| `Clear all sockets` | button *(cond.)* | **SUPPORTED** | socket write `equipment.cpp:404-573` |
| `Refinement` | slider | **SUPPORTED** | `SyncRefineAllRealms` |
| `Change Equipment (Bypass Story / Quest Lock)` | submenu | **SUPPORTED** | equip swap path |
| `No sockets` | label *(cond.)* | UI‑ONLY | placeholder |
| `Note: not saving yet` | label *(cond.)* | UI‑ONLY | informational |
| dynamic `"Socket <n>: Locked"` | button *(cond., per socket)* | **SUPPORTED** | socket unlock |
| dynamic `"Socket <n>: <gear>"` / `"Socket <n>: Empty"` | submenu *(per socket)* | **SUPPORTED** | socket write |

**Change Equipment — `RenderEquipSwap`, `menu.cpp:778-909` (6)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Character Filter` | preset | **SUPPORTED** | equipment/catalog |
| `Category Filter` | preset | **SUPPORTED** | equipment/catalog |
| *(search row)* | search | UI‑ONLY | local filter |
| dynamic `"<Item> [TypeID <n>] {<key>}"` | button *(≤200)* | **SUPPORTED** | equip swap |
| `No matches` / `More matches...` | label *(cond.)* | UI‑ONLY | local filter |

**Socket gear picker — `RenderEquipGear`, `menu.cpp:911-975` (6)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `- Empty this socket -` | button | **SUPPORTED** | socket write |
| `No abyss gears found` | label *(cond.)* | UI‑ONLY | placeholder |
| *(search row)* | search | UI‑ONLY | local filter |
| dynamic gear name (+ `[Effect: <buff>]`) | button *(≤200)* | **SUPPORTED** | `VIBE_BuffInfoTable_GetById` `0x14066F620` |
| `No matches` / `More matches...` | label *(cond.)* | UI‑ONLY | local filter |

**Dye editor — `menu.cpp:281/400/513` (20) — DEAD, NOT REACHABLE**

All 20 controls (`Character`, `Inject All Dyes to Save Data`, `Dye Zone`,
`Color Family`, swatch grids, `Custom Color`, `Remove All Dye (Reset)`,
`Material`, `Condition %`, `Red`, `Green`, `Blue`, `Apply This Color`,
`Load Current`, plus 6 conditional labels) are **LEGACY**. The push sites were
deleted in commit `7c05de4` ("update Trinity for Crimson Desert 2.02.00"); there
is no `"dyeslots"` string and no dispatch case. Additionally **all eight dye
engine signatures measure 0** on PE 2944. See
[equipment](dossiers/equipment.md) §3.

### INVENTORY tab (81)

**Root — `RenderInventoryHome`, `menu.cpp:1666-1694` (8)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Money & Currency` | submenu | UI‑ONLY | navigation |
| `Abyss Items & Artifacts` | submenu | UI‑ONLY | navigation |
| `Add Item` | submenu | UI‑ONLY | navigation |
| `Restore Items` | submenu | UI‑ONLY | navigation |
| `Item Editor` | submenu | UI‑ONLY | navigation |
| `Slot Size` | toggle + slider | **SUPPORTED** | per-tick guard (expansion setter absent) — [inventory](dossiers/inventory.md) §3.2 |
| `Max Stack Size` | toggle | **SUPPORTED‑gap** | per-tick guard; the legacy max-stack pin is gated `revision < 2625` and is **inactive** |
| `Set Max Stack Value` | slider | **SUPPORTED‑gap** | as above |

**Item Editor — `RenderInventoryEditor`, `menu.cpp:1696-1739` (3)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Loading inventory...` | label *(cond.)* | UI‑ONLY | placeholder |
| dynamic `"<Storage>  (<count>)"` | submenu | **SUPPORTED** | holder resolver `0x14212A0F0` |
| `Refresh` | button | **SUPPORTED** | holder/quantity read |

**Item Editor → storage — `RenderInventoryStorage`, `menu.cpp:1745-1799` (5)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Empty` / `No matches` | label *(cond.)* | UI‑ONLY | placeholder |
| *(search row)* | search | UI‑ONLY | local filter |
| dynamic `"<Category>  (<count>)"` | submenu | **SUPPORTED** | holder read |
| dynamic `<ItemName>` + qty | item‑row | **SUPPORTED** | quantity read `0x1417F8E70` |

**Item Editor → category — `RenderInventoryCat`, `menu.cpp:1829-1859` (5)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Empty` / `No matches` | label *(cond.)* | UI‑ONLY | placeholder |
| *(search row)* | search | UI‑ONLY | local filter |
| `Set All` | slider + button | **SUPPORTED** | quantity write (realm contract) |
| dynamic `<ItemName>` | item‑row | **SUPPORTED** | quantity write |

**Add Item — `RenderInventoryAdd`, `menu.cpp:1920-1974` (6)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Catalog unavailable` | label *(cond.)* | UI‑ONLY | readiness placeholder |
| *(search row)* | search | UI‑ONLY | local filter |
| dynamic `"<CatalogCategory>  (<count>)"` | submenu | **SUPPORTED** | catalog `inventory.cpp:4296-4460` |
| dynamic `<ItemName>` + amount | item‑row | **SUPPORTED** | ctor `0x142409910` + planner `0x142407770` + commit `0x14212E160` |
| `No matches` / `More matches...` | label *(cond.)* | UI‑ONLY | local filter |

**Add Item → category — `RenderInventoryAddCat`, `menu.cpp:2011-2046` (5)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Empty` / `No matches` | label *(cond.)* | UI‑ONLY | placeholder |
| *(search row)* | search | UI‑ONLY | local filter |
| `Add All` | slider + button | **SUPPORTED** | bulk add (same fail-closed gate) |
| dynamic `<ItemName>` | item‑row | **SUPPORTED** | add path |

**Money & Currency — `RenderInventoryMoney`, `menu.cpp:2073-2150` (10)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Direct Silver Amount` | slider | UI‑ONLY | local value |
| `>> Set Wallet to Exact Amount <<` | button | **SUPPORTED** | inventory machinery — [crime-money](dossiers/crime-money.md) §4 |
| `>> Add Amount to Existing Wallet <<` | button | **SUPPORTED** | inventory machinery |
| `Set to 1,000,000 Silver (1 Million)` | preset button | **SUPPORTED** | inventory machinery |
| `Set to 10,000,000 Silver (10 Million)` | preset button | **SUPPORTED** | inventory machinery |
| `Set to 20,000,000 Silver (20 Million)` | preset button | **SUPPORTED** | inventory machinery |
| `Full Silver Pouches (Count)` | slider | UI‑ONLY | local value |
| `>> Spawn Full Silver Pouches <<` | button | **SUPPORTED** | item grant |
| `>> Cash In All Pouches (Instant Liquidate) <<` | button | **SUPPORTED** | item grant |
| `Optional` | submenu | UI‑ONLY | navigation |

**Money → Optional — `menu.cpp:2052-2071` (2)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `>> Clear Bugged/Fake Wallet Coins <<` | button | **SUPPORTED** | inventory machinery |
| `Consolidate All Money Stacks` | button | **SUPPORTED** | inventory machinery |

**Abyss Items & Artifacts — `RenderInventoryAbyss`, `menu.cpp:2156-2279` (16)**

All 16 are **SUPPORTED** through the same item-grant path (hardcoded item-id
quick-adds + a count slider): the `Sealed Artifacts Collected: <owned>/<max>`
status label, `Target Total Sealed Artifacts`, `>> Add Missing Sealed Artifacts
to Target <<`, `>> Add ALL Missing Sealed Artifacts (1 to 150) <<`,
`Clean Duplicate Sealed Artifacts`, `Abyss Artifacts (Count)`,
`>> Spawn Abyss Artifacts (Usable Material) <<`, `Add 100x Abyss Artifacts`,
`Add 500x Abyss Artifacts`, `Add 50x Blessing of the Immortal (Skill Points)`,
`Add 50x Advanced Skill Manuals`, `Add 50x Abyssal Seeds`, `Add 50x Abyss Cells`,
`Add 10x Vitality of the Abyss (+30 HP)`, `Add 10x Spirit of the Abyss (+2 MP)`,
`Add 10x Breath of the Abyss (+3 SP)`.

**Restore Items — `RenderInventoryRestore`, `menu.cpp:2558-2662` (8)**

All **SUPPORTED** through the item-grant path: `No Lost / Sold Items Recorded`
*(cond.)*, `Lost & Sold Items: <count> recorded in history`, `>> Restore All
Lost & Sold Items <<`, `Clear Lost & Sold History`, *(search row)*,
`"[RESTORE]  <qty>x  <name>"`, `No matches` *(cond.)*,
`Quest & Special Item Catalog Archive`.

**Restore → Catalog Archive — `menu.cpp:2532-2556` (8)**

All 8 category submenus are **SUPPORTED**: `1. Bounty Notices (Wanted Posters)`,
`2. Documents & Lore Books`, `3. Recipes & Crafting Manuals`,
`4. Quest Keys, Passes & Memories`, `5. Collectibles & Collector's Chest`,
`6. Unique Quest Weapons & Boss Gear`, `7. Mount, Mecha & Vehicle Gear`,
`8. Rare Medals, Tokens & Artifacts`.

**Restore → category pages — `RenderRestoreCategoryPage`, `menu.cpp:2388-2490`, instantiated 8× (5 per page = 40 rows)**

`Collection Status: <owned> / <count> (<missing> Missing)`,
`>> Restore All Missing in Category <<`, *(search row)*,
`"[IN BAG]  <name>"` / `"[CATALOG]  <name>"`, `No matches` *(cond.)* — the
interactive rows are **SUPPORTED**; the labels are UI‑ONLY.

### TRAVEL tab (21)

**Root — `RenderTravel`, `menu.cpp:1139-1229` (6)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `"X <x>  Y <y>  Z <z>"` *(cond.)* | button | **SUPPORTED** | read from `moveOwner+0x90` — [locomotion](dossiers/locomotion.md) §2 |
| `No position yet` | label *(cond.)* | UI‑ONLY | placeholder |
| `"Teleport to Destination: …"` / `"…: None"` | button | **SUPPORTED‑gap** | direct marker reader (inline AOB → `0x140DEFCFE`); the marker-player proxy hook is **UNAVAILABLE** (`kSig_MarkerPlayer` = 0) — [travel](dossiers/travel.md) §8.2 |
| `Sky Arrival Altitude` | slider | UI‑ONLY | local value consumed by the teleport write |
| `Saved Locations` | submenu | UI‑ONLY | navigation |
| `Fast Travel` | submenu | **SUPPORTED‑gap** | dispatcher `0x1406550B0` + gate `0x140654ED0` both exact-unique, but travel semantics remain **never live-verified** — [travel](dossiers/travel.md) §6 |

**Saved Locations — `menu.cpp:1386-1460` (4)**

`+ Save Current Location`, `No saved locations` *(cond.)*,
dynamic `"<n>. <name>  (X <x> Y <y> Z <z>)"`, `Clear All Saved Locations` —
**SUPPORTED** (local bookmark store + position read); the label is UI‑ONLY.

**Saved Locations → manage — `menu.cpp:1462-1520` (5)**

`Location not found` *(cond.)* · `"Teleport Here (X <x> Y <y> Z <z>)"` ·
`Name` (text input) · `Update to Current Position` · `Delete This Location` —
the teleport row is **SUPPORTED**; the rest are UI‑ONLY / local storage.

**Fast Travel categories — `menu.cpp:1287-1315` (2)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Building destination list...` | label *(cond.)* | UI‑ONLY | readiness placeholder |
| dynamic `"<Category>  (<nodeCount>)"` | submenu | **SUPPORTED‑gap** | `VIBE_LevelGimmickSceneObjectInfoTable_GetById` `0x1404A2630` (predicate locator; the bare table-name string is **ambiguous**) |

**Fast Travel → nodes — `menu.cpp:1317-1357` (4)**

`No locations` *(cond.)* · *(search row)* · dynamic node label · `No matches`
*(cond.)* — listing is **SUPPORTED‑gap** (same registry); the actual dispatch is
the unverified leg.

### WORLD tab (29)

**Root — `RenderWorld`, `menu.cpp:1090-1137` (6)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Game Speed` | toggle + slider | **SUPPORTED** | `VIBE_World_GetTimeScale` `0x140952080` (3 callers, all `World::FrameTimerUpdate`) — [world](dossiers/world-time-weather.md) §2 |
| `Freeze Time of Day` | toggle | **SUPPORTED‑gap** | menu gate `TimeOfDayReady()` is **narrower** than the feature (also needs `g_todEngineGlobal` + render manager); the sun clamp rides `World::Tick` |
| `Advance Time (+)` | slider + button | **SUPPORTED** | field-time tick `0x140A38440` |
| `Rewind Time (-)` | slider + button | **SUPPORTED** | field-time tick |
| `Time of Day Presets` | submenu | UI‑ONLY | navigation |
| `Weather & Atmosphere` | submenu | UI‑ONLY | navigation |

**Time of Day Presets — `menu.cpp:977-1022` (6)**

`"Current Time: Day <d>, <hh>:<mm>"` *(cond.)* · `Dawn / Morning (06:00)` ·
`Midday / Noon (12:00)` · `Sunset / Golden Hour (18:00)` ·
`Midnight / Night (00:00)` · `Set Exact Hour` — all **SUPPORTED** (clock
globals + field-time realm `0x1420E55A4`).

**Weather & Atmosphere — `menu.cpp:1024-1088` (17)**

| Control | Type | Status | Game contract |
|---|---|---|---|
| `Weather Preset` | preset | **SUPPORTED‑gap** | presets are **not self-contained** — unassigned fields persist from the previous preset |
| `Clear Distant Fog` | toggle | **SUPPORTED** | shader pack |
| `Force Clear Sky` | toggle | **SUPPORTED** | shader pack |
| `Instant Clear Weather` | button | **SUPPORTED** | weather leaves |
| `Rain Intensity` | slider | **SUPPORTED** | `0x143DC39B0` (exact unique) |
| `Snow Intensity` | slider | **SUPPORTED‑gap** | anchor unique, but **snow intensity is unreachable from any preset** (`world.cpp:896-986`) |
| `Dust / Sandstorm` | slider | **SUPPORTED** | `0x143DC3B10` (exact unique) |
| `Cloud Thickness` | slider | **SUPPORTED‑gap** | `CN::CLOUD_THICK` vs packed `0x2F` unit equivalence is **unasserted** |
| `Cloud Top Altitude` | slider | **SUPPORTED** | cloud pack |
| `Cloud Base Altitude` | slider | **SUPPORTED** | cloud pack |
| `Cloud Drift Speed` | slider | **SUPPORTED** | cloud pack |
| `Fog Scattering (A)` | slider | **SUPPORTED** | shader pack |
| `Fog Horizon Blend (B)` | slider | **SUPPORTED** | shader pack |
| `Wind Speed Multiplier` | slider | **SUPPORTED‑gap** | `0x143DBCCC0` unique, but `hkWindPack` indexes with **unbounded magic indices** |
| `Wind Gust Strength` | slider | **SUPPORTED‑gap** | as above |
| `Turbulence Lift` | slider | **SUPPORTED‑gap** | as above |
| `No Wind` | toggle | **SUPPORTED** | wind pack |

> **Environment manager is UNAVAILABLE.** Both `kSig_EnvManager` and
> `kSig_EnvManager_Legacy` measure **0** on PE 2944. Do not import the PE 2850
> addresses (`0x14391E920`, `0x146C1ADC0`) — neither is reconciled to this build.

### SYSTEM tab (24)

| Group / control | Type | Status | Note |
|---|---|---|---|
| `Keybinds`, `Menu UI Settings`, `Title Font` | submenu ×3 | UI‑ONLY | navigation |
| `Theme Color` | preset | UI‑ONLY | local |
| `PlayStation Icons` | toggle | UI‑ONLY | local asset switch |
| `Language` *(cond.)* | preset | UI‑ONLY | localisation loader |
| `Show FPS Counter` | toggle | UI‑ONLY | local overlay |
| `Show Console Window` | toggle | UI‑ONLY | local |
| game version string (e.g. `Crimson Desert 1.18.01`) | label | UI‑ONLY | from `version_detect` |
| `Auto Save Features` | toggle | UI‑ONLY | `Settings::Save` |
| `Reset All to Default` | button | UI‑ONLY | local |
| `Keyboard` / `Controller` headers | bind header | UI‑ONLY | — |
| `Open Menu` | bind‑row | UI‑ONLY | input mapping |
| `Marker Teleport` | bind‑row | UI‑ONLY | input mapping that *triggers* the marker feature (see TRAVEL) |
| `Fly Up` / `Fly Down` | bind‑row ×2 | UI‑ONLY | input mapping that *triggers* Free Flight |
| `Reset All Keybinds` | button | UI‑ONLY | local |
| `Menu Scale` | slider | UI‑ONLY | local |
| `Show Item Tooltip` | toggle | UI‑ONLY | local |
| `Tooltip Image Size` | slider | UI‑ONLY | local |
| `Built-In Fallback` | preset | UI‑ONLY | local font |
| `Enable Custom Font` | toggle | UI‑ONLY | local font |
| `No Fonts Found` *(cond.)* | label | UI‑ONLY | placeholder |
| `Select Custom Font` *(cond.)* | preset | UI‑ONLY | local font |

---

## Feature-level status summary (game-dependent only)

| Feature | Status | Primary contract |
|---|---|---|
| Super Run | SUPPORTED | `0x14369FF60`, drive vector `R8` |
| Super Jump | SUPPORTED | `0x144282080` + desired velocity `+0xC0` |
| Free Flight | SUPPORTED | `0x14369FF60` |
| Map Marker Teleport | SUPPORTED‑gap | direct reader @ `0x140DEFCFE`; proxy hook unavailable |
| Saved Locations | SUPPORTED | local + `moveOwner+0x90` |
| Native Fast Travel | SUPPORTED‑gap | dispatcher `0x1406550B0` / gate `0x140654ED0`; **never live-verified** |
| Add Item | SUPPORTED | ctor + planner + commit + free, strict fail-closed |
| Item Editor / quantity | SUPPORTED | `0x1417F8E70` / holder `0x14212A0F0` |
| Money (modern) | SUPPORTED | inventory machinery |
| Money (legacy hooks) | **UNAVAILABLE** | raw RVAs invalid; also created-but-never-enabled |
| Abyss / Restore / Catalog Archive | SUPPORTED | item grant path |
| God Mode | SUPPORTED‑gap | per-frame pin only (`kSig_StatCommit` absent) |
| One-Hit Kill / damage sliders | SUPPORTED | `0x1417AE100` (two signatures agree) |
| Infinite Stamina / Spirit | SUPPORTED | `0x1417AE100` |
| Infinite Stamina (mount) | **UNAVAILABLE‑on‑fallback** | mount discovery off on the TU 2.01 path |
| Infinite Item Durability | SUPPORTED | equipment data path |
| Repair / Refine / Sockets / Abyss gear | SUPPORTED‑gap | resize anchor resolves; **effect refresh unresolvable** |
| Trust Multiplier (NPC / pet / mount) | SUPPORTED | 4 unique locators |
| Max Worker Level & Skills | SUPPORTED | 6-byte patch @ `0x14214BE8C` |
| Game Speed | SUPPORTED | `0x140952080` |
| Freeze / Advance / Rewind / Presets | SUPPORTED‑gap | gate narrower than feature; sun clamp needs `World::Tick` |
| Weather / fog / clouds / wind | SUPPORTED‑gap | all anchors unique; preset leakage + wind index bounds + EnvManager absent |
| No Bounty | SUPPORTED‑gap | evaluator `0x1425D7660` active |
| Crime UI banner / minimap / guard hostility | **UNAVAILABLE** | legacy dispatcher: 0 matches; gate `revision <= 2850` |
| Easy Parry / Easy Evade | **LEGACY** | `hkCombatTimingEval` is a pure pass-through; no state field, no menu row |
| Dye / appearance | **LEGACY** | UI unreachable + all 8 signatures measure 0 |

---

## PE 2944 anchors preserved

| Feature | Anchor | Locator status in this KB |
|---|---|---|
| Native Fast Travel dispatcher | `0x1406550B0` | exact unique (44 B), re-verified |
| Native Fast Travel selection gate | `0x140654ED0` | exact unique (31 B), re-verified |
| Locomotion stepper | `0x14369FF60` | exact unique (40 B) — `VIBE_Locomotion_StepDriveVelocity` |
| Movement integrator | `0x144282080` | exact unique (14 tok, 2 wildcards) — `VIBE_Locomotion_MoveUpdateIntegrator` |
| Local move owner | `[component + 0x2C0]` | confirmed from 3 independent directions |
| Worker patch target | `0x14214BE8C` | exact unique (15 B) — the patch site itself |
| Wanted-state evaluator | `0x1425D7660` | exact unique (25 tok) |
| Marker direct reader | `0x140DEFCFE` | exact unique (20 tok, 4 wildcards) |
| Legacy crime dispatcher AOB | — | **0 matches** → crime UI banner bypass unavailable |
| Environment manager | — | **0 matches** (both forms) |

---

## Layout

```text
docs/binary-ninja/
  README.md                     # this file: inventory, status, snapshot, index
  snapshots/
    PE-2944.md                  # executable identity, sections, database, change summary
  dossiers/
    travel.md                   # P0
    locomotion.md               # P0
    inventory.md                # P0
    player-combat.md            # P0
    equipment.md                # P1
    world-time-weather.md       # P1
    trust-worker.md             # P1
    crime-money.md              # P2
  templates/
    function-dossier.md         # copy this block per function
    update-diff-checklist.md    # run this per new executable
```

Analysis scratch and the reusable tooling live outside this folder under
`_analysis/`:

| Helper | Purpose |
|---|---|
| `_analysis/bn.ps1` | single Binary Ninja MCP tool call |
| `_analysis/bnbatch.ps1` | several MCP calls in one process (spec-file driven) |
| `_analysis/aob_scan.ps1` | exact-byte locator scan over the whole image |
| `_analysis/aob_scan2.ps1` | **wildcard-capable** locator scan (`?` / `??`) |
| `_analysis/extract_sigs.ps1` | extract every `kSig_*` from `offsets.h` |
| `_analysis/extract_inline_aobs.ps1` | extract inline byte-pattern literals from a `.cpp` |
| `_analysis/inventory/*.md` | raw per-area inventories produced for this pass |

---

## How these records are produced

### Access

Analysis is performed against the **running Binary Ninja instance** through its
built-in WARP MCP endpoint:

```text
url           = http://127.0.0.1:24642/mcp
authorization = disabled
helper        = pwsh -File _analysis/bn.ps1 -Tool <tool> -ArgsJson '<json>'
batch         = pwsh -File _analysis/bnbatch.ps1 -SpecFile <spec.json>
```

Files opened directly in the Binary Ninja UI are not automatically active for
MCP, so always:

```text
bn_binary_view_list        -> pick the PE view (not the Raw view)
bn_binary_view_set_active  -> { "binaryView": "view_1" }
```

If the harness-provided `mcp__binaryninja__*` tools fail with WinError 10061
while port 24642 is listening, the WARP endpoint is still healthy — use the
`_analysis/bn*.ps1` helpers above.

### Rules that never change

1. Work against a named snapshot: record SHA-256, PE version, image base, image
   size, database path before annotating.
2. A `VIBE_` name requires **at least two independent evidence sources**.
   Otherwise keep `sub_*` and annotate it `Candidate`.
3. **Static analysis only.** No breakpoints, no debugger attach, no code
   injection, no byte patches, no memory writes, no direct game-function calls,
   no game launch. A crash is not evidence.
4. Do not rename an existing `VIBE_*` function unless the recorded evidence
   proves the current name wrong; correct the comment instead.
5. Never convert a number base by hand — let tooling do it and record the result.
6. Record both **RVA** (survives rebase) and **VA** (what the database shows).
7. A locator is only durable if its match count was **measured over the whole
   image**, not assumed from the expected address.

### Locator semantics used for every uniqueness number

Trinity's scanner walks the loaded module's PE sections and skips only a section
named *exactly* `.debug` that lacks `IMAGE_SCN_MEM_EXECUTE`
(`src/mem/scanner.cpp`, `src/mem/section_filter.cpp`). PE 2944 has no such
section — its debug section is named `.debug$P` — so **all 12 sections are
scanned**. Uniqueness numbers in the dossiers are measured over all 12.

### Known source-side defects found while building this KB

These are recorded because each one can mislead a future update. None were fixed
(Trinity C++ source is out of scope for this pass).

| Defect | Where |
|---|---|
| `kSig_LocoStepper` and `kSig_LocoStepper_PE2944` are **byte-identical** despite the "do not use as a fallback" comment | `offsets.h:401-409` |
| `kSig_InvHolderInsert` and `kSig_InvHolderInsert2944` are **byte-identical**, both matching `0x142407770` | `offsets.h` |
| `kSig_TrItemValueDtor` is the **empty string** → silently never resolves; the dtor call is dead | `offsets.h:942`, `inventory.cpp:3932` |
| `kExpected_OriginMatches = 9` is **dead**; the code requires `>= 6` and measures 26 | `offsets.h:565`, `teleport.cpp:549` |
| `kSig_MarkerPlayer` measures **0**, so the proxy write path never installs | `teleport.cpp:524,586` |
| `kSig_MarkerProtection` resolves but its hook is **hard-disabled** | `teleport.cpp:461-468` |
| `Equipment::Install()` returns `true` **unconditionally**, even when both signatures miss | `equipment.cpp:1564` |
| Three legacy money hooks are `MH_CreateHook`-ed but **never enabled**, and never removed | `inventory.cpp:2098-2100` |
| `src/core/mod.cpp:130` **discards** `Teleport::Install()`'s `false` return | `mod.cpp:130` |
| `kCharMgrAnchors` comment says "all four" but the array has **six** entries | `offsets.h` |
| `inventory.h:319-320` promises TribeInfo `_wantedCrimeType` zeroing that is **never performed** | `inventory.h:319-320` |
| Scanner doc claims a `VirtualQuery` page walk; the implementation is **section-driven** | `scanner.h:29-31` |

---

## Definition of done for a feature

A dossier is complete only when it has: the exact executable identity; every
relevant function with a `VIBE_` name or an explicit reason it stayed `sub_*`;
function start RVA and VA; an update-resilient locator with its uniqueness
result; the x64 ABI (RCX/RDX/R8/R9, return, stack, XMM); important structure
offsets and their observed meaning; inbound and outbound xrefs that explain the
game flow; the Trinity source consumer (file, symbol, hook/call/patch role);
static evidence separated from live evidence; and an explicit OFF/restore or
fail-safe result where the feature changes game state.

See [`templates/update-diff-checklist.md`](templates/update-diff-checklist.md)
for the per-executable procedure.

---

## Remaining work

1. **Live verification is the largest gap.** Every P0 travel function still
   records `Live proof: none in this pass`, and Native Fast Travel has **never**
   been confirmed to actually move the player — the recorded `accepted` log line
   proves the selection gate ran, not that travel happened.
2. **ABIs resolved from database prototypes are `strong candidate`, not
   confirmed,** for the inventory core anchors (the 8-arg commit and 9-arg
   holder-insert in particular). They need either decompilation or a runtime
   confirmation.
3. **`progress.md` classifies every hook/locator row as "STATIC READY, not
   semantic PASS".** No PE 2944 semantic ledger exists yet: player stat-pin /
   damage / combat-timing, equipment refresh / socket vector, trust observers
   and time-scale all still need per-feature ON/OFF/persistence evidence.
4. **Source-side defects in the table above** should be fixed before the next
   PE, so the update starts from a clean locator set.
