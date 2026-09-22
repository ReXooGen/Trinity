# Trinity in-game menu — complete control inventory

Scope: every visible control row reachable in the Trinity overlay menu as implemented by
`src/gui/menu.cpp` (3,235 lines) plus the widget/shell API in `src/gui/widgets.h`,
`src/gui/widgets.cpp`, `src/gui/framework.h`, `src/gui/framework.cpp`.
Read-only analysis: no source file was modified; this file is the only artifact written.

Repo revision inspected: working tree of `C:\Users\mul0\Documents\GitHub\Trinity`
(HEAD `6d8bc42` "fix: allow direct PE 2944 map markers").

---

## 1. How the menu is built

### 1.1 Rendering model

The menu is **not** made of stock ImGui widgets. Each row is custom-drawn on the ImGui draw
list, 36 px tall, in an immediate-mode list — see `src/gui/framework.h:7-14` ("a tabbed,
list-based menu custom-drawn on the ImGui draw list - no stock ImGui widgets") and
`src/gui/widgets.h:9-12` ("Every widget draws one 36px row between Begin() and End() ...
and returns true when it was activated / changed this frame").

Per-frame contract (`src/gui/framework.h:16-20`, driven from `src/gui/menu.cpp:3151-3234`):

```
BeginFrame();   // framework.cpp:582  - gather nav input once
Begin();        // framework.cpp:870  - brand bar + tab strip + breadcrumb
<rows...>       // widgets.h declarations, all implemented in widgets.cpp
End();          // framework.cpp:1132 - footer hints, description box, tooltip, nav commit
```

`gui::Render()` (`src/gui/menu.cpp:3151`) is the only caller; it is invoked from the DX12
present hook at `src/hooks/dx12_hook.cpp:820`, gated by `gui::WantsDraw()`
(`src/gui/menu.cpp:40-44`, called at `src/hooks/dx12_hook.cpp:1015`).

### 1.2 Exact registration API names (from `src/gui/widgets.h`)

| API (namespace `trinity::ui`) | Declaration | Purpose |
|---|---|---|
| `Option(label, desc)` | widgets.h:15 | plain action row (Enter/A/click) |
| `OptionItem(label, icon, desc)` | widgets.h:21 | action row with game icon |
| `OptionItemWithSubtitle(label, name, icon, subtitle, desc)` | widgets.h:23 | action row + rich side-panel tooltip |
| `OptionItemWithBuff(label, icon, buff, desc)` | widgets.h:25 | action row + effect suffix |
| `Toggle(label, bool* value, desc)` | widgets.h:29 | on/off switch |
| `FloatOption(label, float* value, min, max, step, def, fmt, desc)` | widgets.h:34 | float stepper / inline typing |
| `ToggleFloat(label, bool* enabled, float* value, ...)` | widgets.h:42 | fused toggle + float on one row |
| `IntOption(label, int* value, min, max, step, def, desc)` | widgets.h:45 | int stepper |
| `IntAction(label, int* value, min, max, step, def, desc)` | widgets.h:68 | amount + "apply" on one row |
| `ToggleInt(label, bool* enabled, int* value, ...)` | widgets.h:73 | fused toggle + int |
| `Combo(label, int* index, const char* const* items, count, desc)` | widgets.h:78 | cycling choice / preset |
| `Submenu(label, id, desc)` | widgets.h:83 | pushes submenu `id` |
| `SubmenuItem(label, icon, id, desc)` | widgets.h:86 | submenu with icon |
| `SubmenuEquipItem(label, icon, id, si, desc)` | widgets.h:90 | submenu with equipment slot tooltip |
| `Search(char* buf, size_t cap, desc)` | widgets.h:98 | type-to-filter row |
| `TextInput(label, char* buf, size_t cap, desc)` | widgets.h:102 | editable text field |
| `BookmarkRow(label, desc) -> BookmarkAction{None,Open,Delete}` | widgets.h:106 | saved-location row |
| `ItemRow(label, icon, qty, key, locked, long long* outQty, desc) -> ItemEdit` | widgets.h:131 | in-place item quantity editor |
| `SwatchRow(label, rgb, count, cursor, current, desc, clearIndex) -> int` | widgets.h:157 | circular colour swatch row |
| `ItemAddRow(label, icon, key, locked, desc) -> long long` | widgets.h:179 | catalog "add N of this" row |
| `BindRow(label, cursor, keyText, padText, capturingCol, desc) -> BindEdit` | widgets.h:201 | keyboard+controller rebind row |
| `BindHeader()` | widgets.h:209 | static "Keyboard"/"Controller" column titles |

Shell / navigation API (namespace `trinity::ui`, `src/gui/framework.h`):
`SetTabs` (56), `CurrentTab` (57), `BeginFrame` (59), `Begin` (64), `End` (65), `ListJump` (70),
`ResetMenu` (74), `PushMenu` (77), `PopMenu` (78), `CurrentMenu` (81), `Toast` (87),
`DrawToasts` (88), `ToastsActive` (89), plus tooltip setters (92-99).
`ui::ListJump()` (framework.h:67-70, implemented framework.cpp:1875) maps Left/Right to page
jumps in pure-list menus; it is **not** a row, and is excluded from control counts below but
listed for completeness in each group where it appears.

### 1.3 Tab / group structure

Top-level sections are a fixed 5-entry array:

```cpp
// src/gui/menu.cpp:35
static const char* const kTabs[] = { "PLAYER", "INVENTORY", "TRAVEL", "WORLD", "SYSTEM" };
// src/gui/menu.cpp:36
enum Tab { TabPlayer, TabInventory, TabTravel, TabWorld, TabSystem, TabCount };
```

The literal array is localized per frame (overriding it) at `src/gui/menu.cpp:3156-3159`:

```cpp
const char* const localizedTabs[] = {
    LOC("PLAYER"), LOC("INVENTORY"), LOC("TRAVEL"), LOC("WORLD"), LOC("SYSTEM") };
ui::SetTabs(localizedTabs, TabCount);
```

Navigation (framework.h:11-14): sections Q/E/Tab or LB/RB; rows by arrows/d-pad/wheel/PgUp/
PgDn/Home/End; select Enter/A/click; back Backspace/B/Esc; reset & clear Del/X.

Submenu routing is a flat id → renderer dispatch inside `Render()`:

```cpp
// src/gui/menu.cpp:3189-3233
if (!*cur) { ... RenderPlayer/RenderInventoryHome/RenderTravel/RenderWorld/RenderSystem }
else if (!strcmp(cur, "keybinds"))          RenderKeybinds();
else if (!strcmp(cur, "menu_ui"))           RenderMenuUISettings();
else if (!strcmp(cur, "font_settings"))     RenderFontSettings();
else if (!strcmp(cur, "saved_locs"))        RenderSavedLocations();
else if (!strcmp(cur, "loc_manage"))        RenderSavedLocationManage();
else if (!strcmp(cur, "ftcats"))            RenderFastTravelCats();
else if (!strcmp(cur, "ftnodes"))           RenderFastTravelNodes();
else if (!strcmp(cur, "equipslots"))        RenderEquipSlots();
else if (!strcmp(cur, "equipedit"))         RenderEquipEdit();
else if (!strcmp(cur, "equipgear"))         RenderEquipGear();
else if (!strcmp(cur, "equipswap"))         RenderEquipSwap();
else if (!strcmp(cur, "invedit"))           RenderInventoryEditor();
else if (!strcmp(cur, "invstore"))          RenderInventoryStorage();
else if (!strcmp(cur, "invcat"))            RenderInventoryCat();
else if (!strcmp(cur, "invadd"))            RenderInventoryAdd();
else if (!strcmp(cur, "invaddcat"))         RenderInventoryAddCat();
else if (!strcmp(cur, "invrestore"))        RenderInventoryRestore();
else if (!strcmp(cur, "invrestore_catalog"))RenderRestoreCatalogArchive();
else if (!strcmp(cur, "invrestore_bounty")) RenderRestoreBounty();
... (lore / recipes / keys / collect / gear / mount / medals)
else if (!strcmp(cur, "invmoney"))          RenderInventoryMoney();
else if (!strcmp(cur, "invmoney_opt"))      RenderInventoryMoneyOptional();
else if (!strcmp(cur, "invabyss"))          RenderInventoryAbyss();
else if (!strcmp(cur, "combat_options"))    RenderCombatOptions();
else if (!strcmp(cur, "world_time_presets"))RenderTimePresets();
else if (!strcmp(cur, "world_weather"))     RenderWeatherAtmosphere();
else                                        RenderPlayer();
```

Two page families are generated from a shared template rather than written per page:

* `RenderFilteredList` (`menu.cpp:1241-1254`) — Search row + per-row lambda + "No matches".
* `RenderCategoryList` (`menu.cpp:1265-1285`) — one `ui::SubmenuItem` per index via
  `renderLabel` / `iconFor` / `onSelect` lambdas.

### 1.4 How a control maps to a settings field and to a feature

* **Value plumbing.** Every value widget takes a raw pointer to a field of the global
  `trinity::State` singleton (`State::Get()`, `src/core/state.h:19-197`); e.g.
  `ui::Toggle(LOC("God Mode"), &st.godMode, ...)` (`src/gui/menu.cpp:87`). The widget writes
  through that pointer; the caller only reacts to the boolean return value.
* **Persistence.** The returned "changed" flag is folded into a local `bool changed`/`save`
  and, when `st.autoSave` is on, `Settings::Save()` is called (`src/gui/menu.cpp:114-115`,
  `161-162`, `1084-1085`, `1133-1134`, `1690-1691`, `3059-3060`, `3145-3146`). `Settings::Save`
  writes `Trinity.ini` next to `Trinity.asi` (`src/core/settings.cpp:22-34`, `272-446`); key
  names are the `fprintf` format block at `src/core/settings.cpp:294-360`, read back in
  `Settings::Load` (`src/core/settings.cpp:46-265`). `Settings::Save()` is a no-op until
  `Settings::ClaimOwnership()` has run (`src/core/settings.cpp:275-276`).
* **Keybinds** are persisted regardless of Auto Save (`src/gui/menu.cpp:2785`, `2791`, `2861`,
  `2924`; comment at `src/core/settings.cpp:204-208`).
* **Feature side effects.** Rows that need to poke the game call the owning game module
  directly from the menu (e.g. `game::Equipment::RepairAll` at `menu.cpp:609`,
  `game::World::SetTimeOfDay` at `menu.cpp:992`, `game::Inventory::SetDirectSilver` at
  `menu.cpp:2085`). Continuous features are instead consumed by hooks installed at startup
  (`src/core/mod.cpp:129-135`); the settings-field → hook map is in the tables below.

---

## 2. Control tables, one per menu tab/group

Legend for **Type**: `toggle`, `slider` (FloatOption/IntOption/ToggleFloat/ToggleInt/IntAction),
`button` (Option/OptionItem, fires once), `preset` (Combo), `submenu`, `search`, `text-input`,
`item-row`, `swatch`, `bind-row`, `label` (visible row that is informational / a placeholder —
activating it does nothing). Rows marked *(cond.)* are only drawn under the stated condition.

### 2.0 Menu shell (always visible)

| Control label (exact user-visible English string) | Type | Settings/state field it reads-writes | Backing feature function (C++ symbol) | Source file:line | Notes |
|---|---|---|---|---|---|
| `PLAYER` | tab | `ui::g_tab` (framework.cpp:207) | `ui::SetTabs` → `RenderPlayer` | menu.cpp:35, 3157; framework.cpp:225, 976-1000 | tab strip; Q/E/Tab or LB/RB; click supported (framework.cpp:983-984) |
| `INVENTORY` | tab | same | `RenderInventoryHome` | menu.cpp:35, 3157; menu.cpp:3194 | |
| `TRAVEL` | tab | same | `RenderTravel` | menu.cpp:35, 3157; menu.cpp:3195 | |
| `WORLD` | tab | same | `RenderWorld` | menu.cpp:35, 3157; menu.cpp:3196 | |
| `SYSTEM` | tab | same | `RenderSystem` | menu.cpp:35, 3157; menu.cpp:3197 | |

Shell hints (not controls, drawn by `End()`): `"Enter select"`, `"Enter toggle"`,
`"Enter toggle   < > adjust   Del reset"`, `"Enter open"`, `"Enter type   Del clear"`,
`"Enter done   Bksp erase"`, `"Enter apply   Bksp erase"` (`src/gui/framework.cpp:1115-1123`),
and `"B close   LB/RB tab"` / `"B back   LB/RB tab"` / `"Bksp close   Q/E tab"` /
`"Bksp back   Q/E tab"` (`src/gui/framework.cpp:1193-1195`).

### 2.1 PLAYER → root (`RenderPlayer`, menu.cpp:119-164) — 7 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Combat & Gameplay Options` | submenu | — | `RenderCombatOptions` | menu.cpp:124 → menu.cpp:3230 | id `combat_options` |
| `Edit Equipment` | submenu | — | `RenderEquipSlots` | menu.cpp:127 → menu.cpp:3208 | id `equipslots`; description swaps on `game::Equipment::Ready()` (menu.cpp:128-130) |
| `Max Worker Level & Skills` | toggle | `st.workerMaxLevelAndSkills` | `game::Worker::SetEnabled` (worker.cpp:142) | menu.cpp:134-148 | on failure the toggle is **reverted** and a toast shown: `if (!game::Worker::SetEnabled(...)) { st.workerMaxLevelAndSkills = workerBefore; ui::Toast(...) }` (menu.cpp:139-143) |
| `Super Run` | toggle + slider | `st.superRun`, `st.superRunMult` | `hkMoveUpdate`/`hkLocoStep` (teleport.cpp:1575 / 1326; reads teleport.cpp:1482, 1539, 1549-1550) | menu.cpp:150 | range 1.0–10.0x, step 0.25, default 2.0 |
| `Super Jump` | toggle + slider | `st.superJump`, `st.superJumpMult` | `ApplyJumpScaling` (teleport.cpp:1210; reads 1213, 1221; called teleport.cpp:1588) | menu.cpp:152 | 1.0–10.0x, default 2.0 |
| `Free Flight` | toggle + slider | `st.freeFlight`, `st.flightSpeed` | `hkLocoStep` (teleport.cpp:1326; reads 1336, 1400-1405, 1454, 1481) | menu.cpp:154-155 | 1–40, default 8 |
| `Trust Multiplier` | toggle + slider | `st.trustMult`, `st.trustMultVal` | `hkSetNpc` / `hkSetPet` trust funnel (friendly.cpp:197 / 204; reads friendly.cpp:135, 149, 178, 190, 306, 310) | menu.cpp:156-159 | 1.0–25.0x, default 3.0; description swaps on `game::Friendly::Ready()` |

### 2.2 PLAYER → Combat & Gameplay Options (`RenderCombatOptions`, menu.cpp:79-117) — 9 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `One-Hit Kill` | toggle | `st.oneHitKill` | `hkDamageApply` (player.cpp:726; reads player.cpp:327, 713) | menu.cpp:85 | |
| `God Mode` | toggle | `st.godMode` | `PinEntry` (player.cpp:197; reads player.cpp:539, 585, 660, 758) | menu.cpp:87-90 | description swaps on `game::Player::Ready()` |
| `Infinite Item Durability` | toggle | `st.infDurability` | equipment durability guard (equipment.cpp:2033) | menu.cpp:91 | |
| `No Fall Damage` | toggle | `st.noFallDamage` | `hkDamageApply`/fall guard (player.cpp:327, 762) | menu.cpp:93 | |
| `Infinite Stamina & Mount` | toggle | `st.infStamina` (+ mirrors `st.infMountStamina`) | player stamina/mount pins (player.cpp:326, 375, 547, 555, 586, 775-779, 873-878, 892-897) | menu.cpp:95-100 | writes `st.infMountStamina = st.infStamina` on change (menu.cpp:98) |
| `Infinite Spirit` | toggle | `st.infSpirit` | player spirit pin (player.cpp:326, 378, 563, 587, 784, 881, 900) | menu.cpp:101 | |
| `No Bounty` | toggle | `st.noBounty` | `game::Inventory::SetNoBounty` (inventory.cpp:5681) + `World::Tick` upkeep (world.cpp:611-624) + `hkEvaluateCrimeWantedState` (inventory.cpp:2032) | menu.cpp:103-108 | explicit `SetNoBounty` call on change (menu.cpp:106) |
| `Outgoing Damage` | slider | `st.dmgOutMult` | `hkDamageApply` (player.cpp:726; reads 713) | menu.cpp:109 | 0.00–20.00x, step 0.25 |
| `Incoming Damage` | slider | `st.dmgInMult` | `hkDamageApply` (player.cpp:726; reads 661, 663) | menu.cpp:111 | 0.00–10.00x, step 0.25 |

### 2.3 PLAYER → Edit Equipment (`RenderEquipSlots`, menu.cpp:584-670) — 8 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Character` | preset | `game::Equipment::GetActiveCharacter()` / `SetActiveCharacter` (equipment.cpp:1595) | `Equipment::SetActiveCharacter` | menu.cpp:590 | items: `"Kliff"`, `"Damiane"`, `"Oongka"` (menu.cpp:588, not localized) |
| `Character not loaded` | label *(cond.)* | — | — | menu.cpp:598-599 | drawn when `!game::Equipment::Ready()` and `eqChar > 0`; page then returns (menu.cpp:595-604) |
| `Waiting for your equipment...` | label *(cond.)* | — | — | menu.cpp:601 | drawn when not ready and `eqChar == 0` |
| `Repair All Gear` | button | — | `game::Equipment::RepairAll` (equipment.cpp:1823) | menu.cpp:606-613 | toast with repaired count |
| `Max Refine All (+10)` | button | — | `game::Equipment::RefineAll` (equipment.cpp:1880) | menu.cpp:615-622 | |
| `Unlock All Sockets` | button | — | `game::Equipment::UnlockAllGears` (equipment.cpp:1903) | menu.cpp:624-631 | |
| dynamic per equipped piece: `"<SlotName> - <ItemName>  (<filled>/<max> sockets used) [TypeID <n>]"`, or `"<SlotName> - <ItemName>  (no sockets) [TypeID <n>]"` | submenu | `s_eqTag`, `s_eqItem`, `s_eqRefine` (menu.cpp:566-570) | `RenderEquipEdit` | menu.cpp:640-656 → menu.cpp:3209 | label built menu.cpp:639-653; `LOC(si.slotName)`; icon = `si.icon` |
| `Nothing equipped` | label *(cond.)* | — | — | menu.cpp:667 | when `game::Equipment::SlotCount() == 0` |

### 2.4 PLAYER → Edit Equipment → piece (`RenderEquipEdit`, menu.cpp:672-776) — 9 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Not equipped` | label *(cond.)* | — | — | menu.cpp:679 | when `EqSlotForTag(s_eqTag,&si)` fails (menu.cpp:677) |
| `Unlock all sockets` | button *(cond.)* | — | `game::Equipment::UnlockAll` (equipment.cpp:1760) | menu.cpp:690-696 | only `if (si.maxSockets > 0 && si.unlockedCount < si.maxSockets)` (menu.cpp:686) |
| `Clear all sockets` | button *(cond.)* | — | `game::Equipment::ClearAll` (equipment.cpp:1793) | menu.cpp:700-707 | only `if (si.filledCount > 0)` (menu.cpp:698) |
| `Refinement` | slider | `s_eqRefine` local → `game::Equipment::SetRefine` | `Equipment::SetRefine` (equipment.cpp:1722) | menu.cpp:715-724 | 0–`game::Equipment::kRefineMax`, step 1; toast distinguishes persisted vs session-only |
| `Change Equipment (Bypass Story / Quest Lock)` | submenu | `s_eqFind` cleared | `RenderEquipSwap` | menu.cpp:728-733 → menu.cpp:3211 | id `equipswap`; no icon (`nullptr`) |
| `No sockets` | label *(cond.)* | — | — | menu.cpp:737 | `if (si.maxSockets == 0)` then return (menu.cpp:735-740) |
| `Note: not saving yet` | label *(cond.)* | reads `game::Equipment::EditsPersist()` | — | menu.cpp:743-744 | drawn `if (!game::Equipment::EditsPersist())` (menu.cpp:742) |
| dynamic: `"Socket <n>: Locked"` | button *(cond., per socket)* | — | `game::Equipment::UnlockAll` (equipment.cpp:1760) | menu.cpp:752-757 | for each `k < si.maxSockets` where `!so.unlocked` (menu.cpp:750) |
| dynamic: `"Socket <n>: <gearName>"` or `"Socket <n>: Empty"` | submenu *(per socket)* | `s_eqSocket`, `s_eqFind` | `RenderEquipGear` | menu.cpp:761-771 → menu.cpp:3210 | id `equipgear`; icon = `so.gearIcon` |

### 2.5 PLAYER → Edit Equipment → Change Equipment (`RenderEquipSwap`, menu.cpp:778-909) — 6 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Character Filter` | preset | `s_eqCharFilter` (menu.cpp:571) | `game::Equipment::IsItemForCharacter` (equipment.cpp:1367) | menu.cpp:793-794 | 5 items (menu.cpp:786-792, not localized) |
| `Category Filter` | preset | `s_eqCategoryFilter` (menu.cpp:572) | `game::Equipment::IsItemForSlot` (equipment.cpp:603 caller path) | menu.cpp:805-806 | 6 items (menu.cpp:797-804, not localized) |
| *(no label — search row)* | search | `s_eqFind` | filters the catalog loop (menu.cpp:864) | menu.cpp:808 | placeholder desc `"Find any weapon, shield, or armor to equip."` |
| dynamic per catalog item: `"<ItemName> [TypeID <n>] {<key>}"` | button *(up to 200)* | — | `game::Equipment::EquipItemToSlot` (equipment.cpp:1926) | menu.cpp:872-885 | cap 200 (menu.cpp:819-822); skips `typeId == 0 || typeId == 0xFFFF` (menu.cpp:826) |
| `No matches` | label *(cond.)* | — | — | menu.cpp:904 | `if (shown == 0)` |
| `More matches...` | label *(cond.)* | — | — | menu.cpp:906 | `else if (shown >= 200)` |

### 2.6 PLAYER → Edit Equipment → socket gear picker (`RenderEquipGear`, menu.cpp:911-975) — 6 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `- Empty this socket -` | button | — | `game::Equipment::ClearGear` (equipment.cpp:1688) | menu.cpp:919-931 | pops the submenu on success |
| `No abyss gears found` | label *(cond.)* | — | — | menu.cpp:936 | `if (game::Equipment::GearCount() == 0)` (menu.cpp:933-934) |
| *(no label — search row)* | search | `s_eqFind` | filters gear list (menu.cpp:951) | menu.cpp:942 | desc `"Find an abyss gear by name."` |
| dynamic per gear: gear name (+ `[Effect: <buff>]` appended to the description) | button *(up to 200)* | — | `game::Equipment::AddGear` (equipment.cpp:1651), `GetGearBuffDescription` (equipment.cpp:1645) | menu.cpp:955-967 | `ui::OptionItemWithBuff`; effect suffix built at widgets.cpp:335 |
| `No matches` | label *(cond.)* | — | — | menu.cpp:970 | |
| `More matches...` | label *(cond.)* | — | — | menu.cpp:972 | |

### 2.7 PLAYER → Dye editor — **DEAD CODE, not reachable** (see §6)

`RenderDyeSlots` (menu.cpp:281-398), `RenderDyeEdit` (menu.cpp:400-511),
`RenderDyeCustom` (menu.cpp:513-559) are defined but **never dispatched**: there is no
`"dyeslots"` id anywhere in the tree (repo-wide grep: no matches), the `Render()` chain
(`menu.cpp:3189-3233`) has no `dye*` case, and `RenderPlayer` (menu.cpp:119-164) pushes no dye
submenu. `git log -S'"dyeslots"' -- src/gui/menu.cpp` shows the push sites removed in commit
`7c05de4` ("fix: update Trinity for Crimson Desert 2.02.00"): the deleted lines are
`- if (ui::Submenu(LOC("Mount Equipment Dye"), "dyeslots",` and
`- if (ui::Submenu(LOC("Dye Equipment"), "dyeslots",`.
The 20 registrations inside them are listed here for completeness only — they are **not**
visible in the shipped menu.

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Character` | preset | `game::Dye::GetActiveCharacter`/`SetActiveCharacter` (dye.cpp:1661) | `Dye::SetActiveCharacter` | menu.cpp:288 | unreachable |
| `Character not loaded` | label | — | — | menu.cpp:298 | unreachable |
| `Waiting for your equipment...` | label | — | — | menu.cpp:301 | unreachable |
| `Inject All Dyes to Save Data` | button | — | `game::Dye::InjectAllToSave` (dye.cpp:1963) | menu.cpp:307-314 | unreachable |
| dynamic per dyeable slot: `"<slotName> - <itemName>  (<dyeCount>/<maxZones> zones dyed)"` / `"(<maxZones> zones)"` | submenu | `s_dyeTag`, `s_dyeItem` | `RenderDyeEdit` | menu.cpp:360-366 | unreachable |
| `Nothing equipped` | label | — | — | menu.cpp:383 | unreachable |
| `Nothing dyeable equipped` | label | — | — | menu.cpp:385 | unreachable |
| dynamic `"Can't be dyed: <n> piece(s)"` | label | — | — | menu.cpp:390-394 | unreachable |
| `Dye Zone` | preset | `s_dyeChan` | `game::Dye::Apply` (dye.cpp:1745) | menu.cpp:433-434 | 13 items "All zones"/"Zone 1..12" (menu.cpp:417-420, not localized) |
| `Color Family` | preset | `s_dyeFamily` | `Dye::Apply` | menu.cpp:435-436 | items from `game::kDyeFamilies[i].name` (menu.cpp:424-431) |
| dynamic swatch grids (neutral row + 10 shade rows) | swatch | `s_dyeCursor[]`, `s_dyeMat`, `s_dyeRepair` | `game::Dye::Apply` (dye.cpp:1745) / `Dye::Clear` (dye.cpp:1756) | menu.cpp:468-484 | unreachable |
| `Custom Color` | submenu | — | `RenderDyeCustom` | menu.cpp:487 | unreachable |
| `Remove All Dye (Reset)` | button | — | `Dye::Clear(tag,-1)` (dye.cpp:1756) | menu.cpp:489-493 | unreachable |
| `Material` | slider | `s_dyeMat` | `SendDye` → `Dye::Apply` (menu.cpp:209-223) | menu.cpp:496 | 0–10 |
| `Condition %` | slider | `s_dyeRepair` | `SendDye` → `Dye::Apply` | menu.cpp:498 | 0–100 |
| `Red` | slider | `s_dyeR` | `SendDye` → `Dye::Apply` | menu.cpp:525 | 0–255 |
| `Green` | slider | `s_dyeG` | `SendDye` | menu.cpp:526 | 0–255 |
| `Blue` | slider | `s_dyeB` | `SendDye` | menu.cpp:527 | 0–255 |
| `Apply This Color` | swatch (1 swatch) | `s_applyCursor` | `SendDye` | menu.cpp:535-537 | unreachable |
| `Load Current` | button | `s_dyeR/G/B`, `s_dyeMat`, `s_dyeRepair`, `s_dyeFamily` | `game::Dye::GetChannel` (dye.cpp:1709) | menu.cpp:539-556 | unreachable |

### 2.8 INVENTORY → root (`RenderInventoryHome`, menu.cpp:1666-1694) — 8 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Money & Currency` | submenu | — | `RenderInventoryMoney` | menu.cpp:1671 → menu.cpp:3227 | id `invmoney` |
| `Abyss Items & Artifacts` | submenu | — | `RenderInventoryAbyss` | menu.cpp:1672 → menu.cpp:3229 | id `invabyss` |
| `Add Item` | submenu | — | `RenderInventoryAdd` | menu.cpp:1673 → menu.cpp:3215 | id `invadd` |
| `Restore Items` | submenu | — | `RenderInventoryRestore` | menu.cpp:1674 → menu.cpp:3217 | id `invrestore` |
| `Item Editor` | submenu | — | `RenderInventoryEditor` | menu.cpp:1675 → menu.cpp:3212 | id `invedit` |
| `Slot Size` | toggle + slider | `st.invSlotSize`, `st.invSlotSizeVal` | `ApplySlotCapToHolder` (inventory.cpp:2845) via `hkSetExpandSlots` (inventory.cpp:1737); reads inventory.cpp:1745, 1749, 1769, 3193, 3200-3203 | menu.cpp:1678-1681 | 1–700, step 10, default 700 |
| `Max Stack Size` | toggle | `st.invStackSize` | stack-cap override (inventory.cpp:3174) | menu.cpp:1682-1684 | |
| `Set Max Stack Value` | slider | `st.invStackSizeVal` | stack-cap override (inventory.cpp:3176-3181) | menu.cpp:1685-1688 | 1–999,999,999, step 10,000, default 999,999 |

### 2.9 INVENTORY → Item Editor (`RenderInventoryEditor`, menu.cpp:1696-1739) — 3 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Loading inventory...` | label *(cond.)* | reads `game::Inventory::Ready()` | — | menu.cpp:1702-1703 | `if (!game::Inventory::Ready())` → page returns (menu.cpp:1700-1706) |
| dynamic per storage: `"<StorageName>  (<count>)"` | submenu | `s_invStore`, `s_invFind`, `s_invCat` | `RenderInventoryStorage` | menu.cpp:1714-1730 (template menu.cpp:1274) → menu.cpp:3213 | id `invstore`; storages with 0 items skipped (menu.cpp:1718-1719) |
| `Refresh` | button | — | `game::Inventory::ForceRefresh` (inventory.cpp:2534) | menu.cpp:1732-1736 | |

### 2.10 INVENTORY → Item Editor → storage (`RenderInventoryStorage`, menu.cpp:1745-1799) — 5 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Empty` | label *(cond.)* | — | — | menu.cpp:1754 | `if (n == 0)` (menu.cpp:1752) |
| *(no label — search row)* | search | `s_invFind` | filters all categories of the storage (menu.cpp:1792) | menu.cpp:1765-1766 | desc `"Find an item anywhere in this storage, whatever category it is in."` |
| dynamic per category: `"<CategoryName>  (<count>)"` | submenu | `s_invCat`, `s_invFilter` | `RenderInventoryCat` | menu.cpp:1770-1781 (template menu.cpp:1274) → menu.cpp:3214 | id `invcat`; icon `game::Inventory::CategoryIcon` (menu.cpp:1780) |
| dynamic per item: `<ItemName>` (with qty value column) | item-row | `s_invStore/cat/idx` → `game::Inventory::SetQuantity` / `RemoveItem` | `Inventory::SetQuantity` (inventory.cpp:3333), `Inventory::RemoveItem` (inventory.cpp:5150); widget menu.cpp:1638 | menu.cpp:1792 → menu.cpp:1621-1657 | shown only while the search box is non-empty (menu.cpp:1768-1794) |
| `No matches` | label *(cond.)* | — | — | menu.cpp:1796 | `if (shown == 0)` |

`ui::ListJump()` at menu.cpp:1763 (only while `!s_invFind[0]`, comment menu.cpp:1759-1762).

### 2.11 INVENTORY → storage → category (`RenderInventoryCat`, menu.cpp:1829-1859) — 5 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Empty` | label *(cond.)* | — | — | menu.cpp:1837 | `if (total == 0)` (menu.cpp:1835) |
| *(no label — search row)* | search | `s_invFilter` | filters rows (menu.cpp:1848) | menu.cpp:1842 | desc `"Narrow this category down by name."` |
| `Set All` | slider + button (IntAction) | `s_invSetAllQty` (menu.cpp:1553) | `Inventory::SetQuantity` (inventory.cpp:3333) in a loop (menu.cpp:1821-1824) | menu.cpp:1815-1826 (`RenderSetAll`) | 1–999,999,999, step 1, default 999; inert when `locked \|\| shown == 0` (menu.cpp:1814, 1816) |
| dynamic per item: `<ItemName>` | item-row | as above | `Inventory::SetQuantity` / `RemoveItem` | menu.cpp:1854 → menu.cpp:1621-1657 | |
| `No matches` | label *(cond.)* | — | — | menu.cpp:1856 | |

### 2.12 INVENTORY → Add Item (`RenderInventoryAdd`, menu.cpp:1920-1974) — 6 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Catalog unavailable` | label *(cond.)* | — | — | menu.cpp:1929-1930 | `if (n == 0)` where `n = game::Inventory::CatalogCategoryCount()` (menu.cpp:1926-1927) |
| *(no label — search row)* | search | `s_invAddFind` | filters catalog (menu.cpp:1965) | menu.cpp:1938-1939 | |
| dynamic per catalog category: `"<CatalogCategoryName>  (<count>)"` | submenu | `s_invAddCat`, `s_invAddFilter` | `RenderInventoryAddCat` | menu.cpp:1943-1954 (template menu.cpp:1274) → menu.cpp:3216 | id `invaddcat`; icon `CatalogCategoryIcon` (menu.cpp:1953) |
| dynamic per catalog item: `<ItemName>` + amount-to-add column | item-row | widget-local count keyed by `CatalogKey` (menu.cpp:1578-1581) | `game::Inventory::AddItem` (inventory.cpp:5089) | menu.cpp:1909-1916 → menu.cpp:1450 (`ui::ItemAddRow`, widgets.cpp:1450) | cap 200 (menu.cpp:1961-1966) |
| `No matches` | label *(cond.)* | — | — | menu.cpp:1969 | |
| `More matches...` | label *(cond.)* | — | — | menu.cpp:1971 | `else if (shown >= 200)` |

`ui::ListJump()` at menu.cpp:1936 (only while `!s_invAddFind[0]`).

### 2.13 INVENTORY → Add Item → category (`RenderInventoryAddCat`, menu.cpp:2011-2046) — 5 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Empty` | label *(cond.)* | — | — | menu.cpp:2020 | `if (total == 0)` (menu.cpp:2018) |
| *(no label — search row)* | search | `s_invAddFilter` | filters catalog rows (menu.cpp:2034) | menu.cpp:2025 | |
| `Add All` | slider + button (IntAction) | `s_invAddAllQty` (menu.cpp:1557) | `game::Inventory::AddItemsBulk` (inventory.cpp:5108) | menu.cpp:1989-2008 (`RenderAddAll`) | 1–999,999,999, step 1, default 5; inert when `locked \|\| shown == 0` (menu.cpp:1988) |
| dynamic per catalog item: `<ItemName>` | item-row | as §2.12 | `Inventory::AddItem` | menu.cpp:2041 → menu.cpp:1909 | |
| `No matches` | label *(cond.)* | — | — | menu.cpp:2043 | |

### 2.14 INVENTORY → Money & Currency (`RenderInventoryMoney`, menu.cpp:2073-2150) — 10 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Direct Silver Amount` | slider | `s_directSilverInput` (menu.cpp:2049) | `Inventory::SetDirectSilver` (inventory.cpp:4551) / `AddDirectSilver` (inventory.cpp:4705) | menu.cpp:2079-2080 | 0–20,000,000, step 1,000,000 |
| `>> Set Wallet to Exact Amount <<` | button | reads `s_directSilverInput` | `game::Inventory::SetDirectSilver` (inventory.cpp:4551) | menu.cpp:2082-2092 | failure path emits two toasts (menu.cpp:2089-2090) |
| `>> Add Amount to Existing Wallet <<` | button | reads `s_directSilverInput` | `game::Inventory::AddDirectSilver` (inventory.cpp:4705) | menu.cpp:2095-2102 | |
| `Set to 1,000,000 Silver (1 Million)` | preset button | — | `Inventory::SetDirectSilver(1000000)` | menu.cpp:2105-2109 | |
| `Set to 10,000,000 Silver (10 Million)` | preset button | — | `Inventory::SetDirectSilver(10000000)` | menu.cpp:2111-2115 | |
| `Set to 20,000,000 Silver (20 Million)` | preset button | — | `Inventory::SetDirectSilver(20000000)` | menu.cpp:2117-2121 | |
| `Full Silver Pouches (Count)` | slider | `s_pouchCountInput` (menu.cpp:2050) | `Inventory::SpawnSilverPouches` (inventory.cpp:4733) | menu.cpp:2124-2125 | 10–10,000, step 100 |
| `>> Spawn Full Silver Pouches <<` | button | reads `s_pouchCountInput` | `Inventory::SpawnSilverPouches` (inventory.cpp:4733) | menu.cpp:2127-2134 | |
| `>> Cash In All Pouches (Instant Liquidate) <<` | button | — | `Inventory::CashInAllSilverPouches` (inventory.cpp:4746) | menu.cpp:2136-2144 | |
| `Optional` | submenu | — | `RenderInventoryMoneyOptional` | menu.cpp:2147 → menu.cpp:3228 | id `invmoney_opt` |

### 2.15 INVENTORY → Money → Optional (`RenderInventoryMoneyOptional`, menu.cpp:2052-2071) — 2 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `>> Clear Bugged/Fake Wallet Coins <<` | button | — | `game::Inventory::SetDirectSilver(0)` (inventory.cpp:4551) | menu.cpp:2056-2061 | |
| `Consolidate All Money Stacks` | button | — | `game::Inventory::ConsolidateMoney` (inventory.cpp:4858) | menu.cpp:2063-2068 | |

### 2.16 INVENTORY → Abyss Items & Artifacts (`RenderInventoryAbyss`, menu.cpp:2156-2279) — 16 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| dynamic: `"Sealed Artifacts Collected: <owned> / <max> (<missing> Missing)"` | label | `game::Inventory::GetSealedArtifactStatus()` (inventory.cpp:5493) | — | menu.cpp:2166-2169 | informational row |
| `Target Total Sealed Artifacts` | slider | `s_targetSealedTotal` (menu.cpp:2153) | `Inventory::AddMissingSealedArtifacts` (inventory.cpp:5524) | menu.cpp:2171-2172 | 1–150, step 1, default 10 |
| `>> Add Missing Sealed Artifacts to Target <<` | button | reads `s_targetSealedTotal` | `Inventory::AddMissingSealedArtifacts` (inventory.cpp:5524) | menu.cpp:2181-2188 | |
| `>> Add ALL Missing Sealed Artifacts (1 to 150) <<` | button | — | `Inventory::AddMissingSealedArtifacts(150)` | menu.cpp:2190-2198 | |
| `Clean Duplicate Sealed Artifacts` | button | — | `Inventory::CleanDuplicateSealedArtifacts` (inventory.cpp:5555) | menu.cpp:2200-2208 | |
| `Abyss Artifacts (Count)` | slider | `s_customAbyssArtifactCount` (menu.cpp:2154) | `Inventory::AddItemByKey` (inventory.cpp:4500) | menu.cpp:2211-2212 | 1–99,999, step 10, default 100 |
| `>> Spawn Abyss Artifacts (Usable Material) <<` | button | reads `s_customAbyssArtifactCount` | `Inventory::AddItemByKey("Abyss_Artifact", n)` (inventory.cpp:4500) | menu.cpp:2214-2221 | |
| `Add 100x Abyss Artifacts` | button | — | `AddItemByKey("Abyss_Artifact", 100)` | menu.cpp:2224-2228 | |
| `Add 500x Abyss Artifacts` | button | — | `AddItemByKey("Abyss_Artifact", 500)` | menu.cpp:2230-2234 | |
| `Add 50x Blessing of the Immortal (Skill Points)` | button | — | `AddItemByKey("Boss_Reward_SuperSkill", 50)` | menu.cpp:2236-2240 | |
| `Add 50x Advanced Skill Manuals` | button | — | `AddItemByKey("SkillPoint_Book_02", 50)` | menu.cpp:2242-2246 | |
| `Add 50x Abyssal Seeds` | button | — | `AddItemByKey("AbyssStone_Seed", 50)` | menu.cpp:2248-2252 | |
| `Add 50x Abyss Cells` | button | — | `AddItemByKey("AbyssRuinsCreatureCore", 50)` | menu.cpp:2254-2258 | |
| `Add 10x Vitality of the Abyss (+30 HP)` | button | — | `AddItemByKey("Abyss_InfiniteStat_Hp30", 10)` | menu.cpp:2260-2264 | |
| `Add 10x Spirit of the Abyss (+2 MP)` | button | — | `AddItemByKey("Abyss_InfiniteStat_Mp2", 10)` | menu.cpp:2266-2270 | |
| `Add 10x Breath of the Abyss (+3 SP)` | button | — | `AddItemByKey("Abyss_InfiniteStat_Sp3", 10)` | menu.cpp:2272-2276 | |

### 2.17 INVENTORY → Restore Items (`RenderInventoryRestore`, menu.cpp:2558-2662) — 8 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `No Lost / Sold Items Recorded` | label *(cond.)* | `game::Inventory::GetLostItemsCount()` (inventory.cpp:5332) | — | menu.cpp:2568-2569 | `if (count == 0)` (menu.cpp:2566) |
| dynamic: `"Lost & Sold Items: <count> recorded in history"` | label | `GetLostItemsCount()` | — | menu.cpp:2573-2576 | informational |
| `>> Restore All Lost & Sold Items <<` | button | — | `Inventory::RestoreAllLostItems` (inventory.cpp:5389) | menu.cpp:2578-2583 | |
| `Clear Lost & Sold History` | button | — | `Inventory::ClearLostItems` (inventory.cpp:5411) | menu.cpp:2585-2589 | |
| *(no label — search row)* | search | `s_lostSearch` (menu.cpp:2591) | filters `rec.name/key/source` (menu.cpp:2600-2604) | menu.cpp:2592 | |
| dynamic per lost item: `"[RESTORE]  <qty>x  <name>"` | button | — | `Inventory::RestoreLostItem` (inventory.cpp:5375) | menu.cpp:2610-2650 | rows for items currently equipped are skipped (menu.cpp:2606-2608) |
| `No matches` | label *(cond.)* | — | — | menu.cpp:2655 | `if (shown == 0 && s_lostSearch[0])` |
| `Quest & Special Item Catalog Archive` | submenu | — | `RenderRestoreCatalogArchive` | menu.cpp:2658 → menu.cpp:3218 | id `invrestore_catalog` |

### 2.18 INVENTORY → Restore → Catalog Archive (`RenderRestoreCatalogArchive`, menu.cpp:2532-2556) — 8 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `1. Bounty Notices (Wanted Posters)` | submenu | — | `RenderRestoreBounty` | menu.cpp:2538 → menu.cpp:3219 | id `invrestore_bounty` |
| `2. Documents & Lore Books` | submenu | — | `RenderRestoreLore` | menu.cpp:2540 → menu.cpp:3220 | id `invrestore_lore` |
| `3. Recipes & Crafting Manuals` | submenu | — | `RenderRestoreRecipes` | menu.cpp:2542 → menu.cpp:3221 | id `invrestore_recipes` |
| `4. Quest Keys, Passes & Memories` | submenu | — | `RenderRestoreKeys` | menu.cpp:2544 → menu.cpp:3222 | id `invrestore_keys` |
| `5. Collectibles & Collector's Chest` | submenu | — | `RenderRestoreCollectibles` | menu.cpp:2546 → menu.cpp:3223 | id `invrestore_collect` |
| `6. Unique Quest Weapons & Boss Gear` | submenu | — | `RenderRestoreGear` | menu.cpp:2548 → menu.cpp:3224 | id `invrestore_gear` |
| `7. Mount, Mecha & Vehicle Gear` | submenu | — | `RenderRestoreMount` | menu.cpp:2550 → menu.cpp:3225 | id `invrestore_mount` |
| `8. Rare Medals, Tokens & Artifacts` | submenu | — | `RenderRestoreMedals` | menu.cpp:2552 → menu.cpp:3226 | id `invrestore_medals` |

### 2.19 INVENTORY → Restore → category pages — shared template `RenderRestoreCategoryPage` (menu.cpp:2388-2490), instantiated **8 times** — 5 controls per page (40 rows total)

Page titles and key tables: `Bounty Notices (Wanted Posters)` (menu.cpp:2494, 56 keys menu.cpp:2284-2300),
`Documents & Lore Books` (menu.cpp:2499, 20 keys menu.cpp:2302-2311),
`Recipes & Crafting Manuals` (menu.cpp:2504, 38 keys menu.cpp:2313-2328),
`Quest Keys, Passes & Memories` (menu.cpp:2509, 25 keys menu.cpp:2330-2340),
`Collectibles & Collector's Chest` (menu.cpp:2514, 22 keys menu.cpp:2342-2355),
`Unique Quest Weapons & Boss Gear` (menu.cpp:2519, 20 keys menu.cpp:2357-2367),
`Mount, Mecha & Vehicle Gear` (menu.cpp:2524, 11 keys menu.cpp:2369-2377),
`Rare Medals, Tokens & Artifacts` (menu.cpp:2529, 13 keys menu.cpp:2379-2386).

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| dynamic: `"Collection Status: <owned> / <count> (<missing> Missing)"` | label | `game::Inventory::IsItemKeyOwned` (inventory.cpp:5445) | — | menu.cpp:2402-2405 | informational |
| `>> Restore All Missing in Category <<` | button | — | `Inventory::RestoreCategoryMissing` (inventory.cpp:5465) | menu.cpp:2412-2426 | description carries the missing count (menu.cpp:2407-2410) |
| *(no label — search row)* | search | `s_restoreSearch` (menu.cpp:2282) | filters `displayName`/`key` (menu.cpp:2450-2453) | menu.cpp:2428-2429 | |
| dynamic per key: `"[IN BAG]  <displayName>"` or `"[CATALOG]  <displayName>"` | button | — | `Inventory::AddItemByKey` (inventory.cpp:4500); name via `game::ResolveItemDisplayName` (menu.cpp:2437) / `FindTypeIdByKey` (menu.cpp:2445) | menu.cpp:2455-2482 | `ui::OptionItemWithSubtitle` |
| `No matches` | label *(cond.)* | — | — | menu.cpp:2487 | `if (shown == 0)` |

### 2.20 TRAVEL → root (`RenderTravel`, menu.cpp:1139-1229) — 6 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| dynamic: `"X <x>  Y <y>  Z <z>"` *(cond.)* | button | `game::Teleport::GetLastPosition` (teleport.cpp:1950) | `Teleport::CopyPositionToClipboard` (teleport.cpp:1964) | menu.cpp:1147-1153 | drawn only when a position is known (menu.cpp:1145) |
| `No position yet` | label *(cond.)* | — | — | menu.cpp:1157 | else-branch of the above |
| dynamic: `"Teleport to Destination: X <x>  Y (Sky)  Z <z>"` / `"... X Y Z"` / `"Teleport to Destination: None"` | button | `st.markerFallbackHeight`; `st.markerTeleportKeyVk` in the description | `game::Teleport::TeleportToMarker` (teleport.cpp:2100), `GetMarkerPosition` (teleport.cpp:2071) | menu.cpp:1164-1208 | label/desc built at menu.cpp:1166-1180; same action is also bound to the `Marker Teleport` hotkey (dx12_hook.cpp:986-988) |
| `Sky Arrival Altitude` | slider | `st.markerFallbackHeight` | `Teleport::TeleportToMarker(fallbackHeight)` (teleport.cpp:2100) | menu.cpp:1210-1215 | 50–3000, step 25, default 550; saves when `autoSave` |
| `Saved Locations` | submenu | — | `RenderSavedLocations` | menu.cpp:1218 → menu.cpp:3204 | id `saved_locs` |
| `Fast Travel` | submenu | — | `RenderFastTravelCats` | menu.cpp:1222-1226 → menu.cpp:3206 | id `ftcats`; pushes `game::Teleport::LoadCatalog()` (teleport.cpp:1994) |

### 2.21 TRAVEL → Saved Locations (`RenderSavedLocations`, menu.cpp:1386-1460) — 4 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `+ Save Current Location` | button | appends to `st.savedLocations` | `Settings::Save()` (settings.cpp:272); position from `Teleport::GetLastPosition` (teleport.cpp:1950) | menu.cpp:1394-1414 | writes `loc_name_N/loc_x_N/...` (settings.cpp:426-434) |
| `No saved locations` | label *(cond.)* | `st.savedLocations` | — | menu.cpp:1418 | `if (st.savedLocations.empty())` (menu.cpp:1416) |
| dynamic per bookmark: `"<n>. <name>  (X <x>  Y <y>  Z <z>)"` | bookmark row | `st.savedLocations[i]` | `ui::BookmarkRow` (widgets.cpp:1104); Open → `ui::PushMenu("loc_manage")`, Delete → `Settings::Save()` | menu.cpp:1428-1448 | Enter/A opens, Del/X deletes (menu.cpp:1432, 1435-1448) |
| `Clear All Saved Locations` | button | `st.savedLocations.clear()` | `Settings::Save()` | menu.cpp:1451-1456 | |

### 2.22 TRAVEL → Saved Locations → manage (`RenderSavedLocationManage`, menu.cpp:1462-1520) — 5 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Location not found` | label *(cond.)* | `s_curLocIdx` | — | menu.cpp:1468 | `if (s_curLocIdx < 0 \|\| >= size)` (menu.cpp:1465) |
| dynamic: `"Teleport Here (X <x>  Y <y>  Z <z>)"` | button | `st.savedLocations[s_curLocIdx]` | `game::Teleport::TeleportToCoordinates` (teleport.cpp:2172) | menu.cpp:1479-1486 | |
| `Name` | text-input | `loc.name` | `Settings::Save()` | menu.cpp:1488-1491 | `ui::TextInput` (widgets.cpp:1029) |
| `Update to Current Position` | button | `loc.x/y/z` | `Teleport::GetLastPosition` (teleport.cpp:1950) + `Settings::Save()` | menu.cpp:1495-1509 | |
| `Delete This Location` | button | erases from `st.savedLocations` | `Settings::Save()` + `ui::PopMenu()` | menu.cpp:1511-1517 | |

### 2.23 TRAVEL → Fast Travel categories (`RenderFastTravelCats`, menu.cpp:1287-1315) — 2 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Building destination list...` | label *(cond.)* | `g_catalogReady` | `game::Teleport::LoadCatalog` (teleport.cpp:1994) | menu.cpp:1294-1295 | `if (!LoadCatalog())` → page returns (menu.cpp:1292-1298) |
| dynamic per category: `"<CategoryName>  (<nodeCount>)"` | submenu | `s_ftCat`, `s_ftFilter` | `Teleport::GetCategory` (teleport.cpp:2014), `EnsureCategoryNodes` (teleport.cpp:2024) | menu.cpp:1300-1312 (template menu.cpp:1274) → menu.cpp:3207 | id `ftnodes`; no icons for this list (menu.cpp:1311) |

`ui::ListJump()` at menu.cpp:1290.

### 2.24 TRAVEL → Fast Travel → nodes (`RenderFastTravelNodes`, menu.cpp:1317-1357) — 4 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `No locations` | label *(cond.)* | `Teleport::NodeCount` (teleport.cpp:2029) | — | menu.cpp:1326 | `if (total == 0)` (menu.cpp:1324) |
| *(no label — search row)* | search | `s_ftFilter` | filters node labels (menu.cpp:1342) | menu.cpp:1334 → menu.cpp:1246 | shared `RenderFilteredList` template (menu.cpp:1241-1254) |
| dynamic per node: node label | button | — | `game::Teleport::TravelToNode` (teleport.cpp:2047) | menu.cpp:1337-1353 | label/coords from `Teleport::GetNode` (teleport.cpp:2035) |
| `No matches` | label *(cond.)* | — | — | menu.cpp:1253 | literal string, **not** localized (see §5) |

`ui::ListJump()` at menu.cpp:1320.

### 2.25 WORLD → root (`RenderWorld`, menu.cpp:1090-1137) — 6 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Game Speed` | toggle + slider | `st.gameSpeed`, `st.gameSpeedMult` | `hkFrameTimerUpdate` (world.cpp:43; reads world.cpp:51, 59) | menu.cpp:1097-1100 | 0.1–5.0x, step 0.05; description swaps on `game::World::Ready()` (world.cpp:798) |
| `Freeze Time of Day` | toggle | `st.timeFrozen` | `hkFieldTimeTick` (world.cpp:115; reads world.cpp:118) + sun clamp (world.cpp:640) | menu.cpp:1103-1106 | description swaps on `TimeOfDayReady()` (world.cpp:803) |
| `Advance Time (+)` | slider + button (IntAction) | `s_advHours` (menu.cpp:1109) | `game::World::AdvanceTimeOfDayHours` (world.cpp:808) | menu.cpp:1110-1117 | 1–240, step 1; action runs on Enter-commit |
| `Rewind Time (-)` | slider + button (IntAction) | `s_rewHours` (menu.cpp:1120) | `World::AdvanceTimeOfDayHours(-n)` (world.cpp:808) | menu.cpp:1121-1128 | 1–240, step 1 |
| `Time of Day Presets` | submenu | — | `RenderTimePresets` | menu.cpp:1130 → menu.cpp:3231 | id `world_time_presets` |
| `Weather & Atmosphere` | submenu | — | `RenderWeatherAtmosphere` | menu.cpp:1131 → menu.cpp:3232 | id `world_weather` |

### 2.26 WORLD → Time of Day Presets (`RenderTimePresets`, menu.cpp:977-1022) — 6 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| dynamic: `"Current Time: Day <d>, <hh>:<mm>"` *(cond.)* | label | `game::World::GetCurrentTimeOfDay` (world.cpp:883) | — | menu.cpp:985-987 | drawn only if the read succeeds (menu.cpp:983) |
| `Dawn / Morning (06:00)` | button | — | `game::World::SetTimeOfDay(6)` (world.cpp:851) | menu.cpp:990-994 | description = `"Unavailable right now."` when `!timeReady` (menu.cpp:980, 990) |
| `Midday / Noon (12:00)` | button | — | `World::SetTimeOfDay(12)` | menu.cpp:996-1000 | |
| `Sunset / Golden Hour (18:00)` | button | — | `World::SetTimeOfDay(18)` | menu.cpp:1002-1006 | |
| `Midnight / Night (00:00)` | button | — | `World::SetTimeOfDay(0)` | menu.cpp:1008-1012 | |
| `Set Exact Hour` | slider + button (IntAction) | `s_customHour` (menu.cpp:1014) | `World::SetTimeOfDay(hour)` | menu.cpp:1015-1019 | 0–23, step 1, default 12 |

### 2.27 WORLD → Weather & Atmosphere (`RenderWeatherAtmosphere`, menu.cpp:1024-1088) — 17 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Weather Preset` | preset | `st.weatherPreset` | `game::World::SetWeatherPreset` (world.cpp:896) | menu.cpp:1041-1047 | 6 items (menu.cpp:1032-1039, not localized) |
| `Clear Distant Fog` | toggle | `st.clearDistantFog` | `World::SetClearDistantFog` (world.cpp:988); reads world.cpp:296, 675, 905-990 | menu.cpp:1049 | |
| `Force Clear Sky` | toggle | `st.forceClearSky` | world.cpp:187, 213, 236, 288, 686, 904-971 | menu.cpp:1050 | |
| `Instant Clear Weather` | button | sets `st.forceClearSky`, `st.clearDistantFog`, `st.weatherPreset` | `SetWeatherPreset(1)` + `SetClearDistantFog(true)` | menu.cpp:1052-1061 | composite one-shot (menu.cpp:1054-1059) |
| `Rain Intensity` | slider | `st.rainIntensity` | `hkGetRainIntensity` (world.cpp:181; reads 188, 696-706) | menu.cpp:1064 | 0.0–5.0, step 0.10 |
| `Snow Intensity` | slider | `st.snowIntensity` | `hkGetSnowIntensity` (world.cpp:207; reads 214) | menu.cpp:1065 | 0.0–5.0, step 0.10 |
| `Dust / Sandstorm` | slider | `st.dustIntensity` | `hkGetDustIntensity` (world.cpp:230; reads 237, 240, 701-704) | menu.cpp:1066 | 0.0–5.0, step 0.10 |
| `Cloud Thickness` | slider | `st.cloudThick` | world.cpp:307-311, 706-708 | menu.cpp:1069 | 0.0–5.0, default 1.0 |
| `Cloud Top Altitude` | slider | `st.cloudTop` | world.cpp:334-336, 710-712 | menu.cpp:1070 | 0.1–3.0, step 0.05 |
| `Cloud Base Altitude` | slider | `st.cloudBase` | world.cpp:339-341, 714-716 | menu.cpp:1071 | 0.1–3.0, step 0.05 |
| `Cloud Drift Speed` | slider | `st.cloudScrollSpeed` | world.cpp:349-352, 745-748 | menu.cpp:1072 | 0.0–5.0, step 0.1 |
| `Fog Scattering (A)` | slider | `st.fogA` | world.cpp:303, 682 | menu.cpp:1075 | 0.0–5.0, step 0.10 |
| `Fog Horizon Blend (B)` | slider | `st.fogB` | world.cpp:304, 683 | menu.cpp:1076 | 0.0–5.0, step 0.10 |
| `Wind Speed Multiplier` | slider | `st.windMultiplier` | `hkWindPack` (world.cpp:268; reads 239, 249-251, 732-735) | menu.cpp:1079 | 0.0–5.0, step 0.1 |
| `Wind Gust Strength` | slider | `st.windGust` | world.cpp:737-739 | menu.cpp:1080 | 0.0–3.0, step 0.1 |
| `Turbulence Lift` | slider | `st.windTurbLift` | world.cpp:741-743 | menu.cpp:1081 | 0.0–3.0, step 0.1 |
| `No Wind` | toggle | `st.noWind` | world.cpp:236, 344, 723 | menu.cpp:1082 | |

### 2.28 SYSTEM → root (`RenderSystem`, menu.cpp:3065-3149) — 11 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Keybinds` | submenu | — | `RenderKeybinds` | menu.cpp:3075 → menu.cpp:3201 | id `keybinds` |
| `Menu UI Settings` | submenu | — | `RenderMenuUISettings` | menu.cpp:3078 → menu.cpp:3202 | id `menu_ui` |
| `Theme Color` | preset | `st.themeIndex` | `theme::UpdateColors` (consumed at framework.cpp:54) | menu.cpp:3085-3088 | 6 items: `Crimson Red`, `Cyber Cyan`, `Neon Purple`, `Matrix Emerald`, `Royal Gold`, `Sunset Orange` (menu.cpp:3081-3084) |
| `PlayStation Icons` | toggle | `st.playstationIcons` | `icons.cpp:472, 489` | menu.cpp:3091-3092 | UI-only |
| `Language` | preset *(cond.)* | `st.languageCode`, `st.languageIndex` | `loc::SetLanguage` (localization.cpp:298) | menu.cpp:3103-3110 | only `if (langCount > 1)` (menu.cpp:3096) |
| `Title Font` | submenu | — | `RenderFontSettings` | menu.cpp:3113 → menu.cpp:3203 | id `font_settings`; desc warns "(requires game restart)" |
| `Show FPS Counter` | toggle | `st.showFps` | `DrawFpsCounter` (menu.cpp:46-72), render gate `WantsDraw` (menu.cpp:43) | menu.cpp:3119 | UI-only; also shown at dx12_hook.cpp render gate |
| `Show Console Window` | toggle | `st.showConsole` | `Logger::EnableConsole` / `DisableConsole` (logger.h:48, 52; called menu.cpp:3125-3127; also dx12_hook.cpp:554) | menu.cpp:3121-3128 | UI-only |
| dynamic: game version string, e.g. `Crimson Desert 1.18.01` | label | `core::GetGameVersionDisplay()` (version_detect.h:35) | `core::GetGameVersion` (version_detect.cpp) | menu.cpp:3131-3132 | informational |
| `Auto Save Features` | toggle | `st.autoSave` | `Settings::Save` / `Settings::Load` (settings.cpp:272 / 46) | menu.cpp:3134-3136 | its own flip is always written (comment menu.cpp:3070-3072) |
| `Reset All to Default` | button | — | `Settings::ResetFeatures` (settings.cpp:448) | menu.cpp:3137-3143 | leaves `autoSave`, binds, theme, fonts alone (settings.cpp:451-482) |

### 2.29 SYSTEM → Keybinds (`RenderKeybinds`, menu.cpp:2885-2930) — 6 controls

Rows are produced by `KeybindActionRow` (menu.cpp:2755-2797) → `ui::BindRow` (widgets.cpp:1574).

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Keyboard` / `Controller` column titles | bind header | — | `ui::BindHeader` (widgets.cpp:1539) | menu.cpp:2895; widgets.cpp:1562-1563 | not a navigable row |
| `Open Menu` | bind-row | `st.openKeyVk`, `st.openPadMask` | `ui::PollMenuToggle` (framework.cpp:520) / `PollToggleCombo` (framework.cpp:499) | menu.cpp:2901-2904 | reset defaults from a fresh `State` (menu.cpp:2888) |
| `Marker Teleport` | bind-row | `st.markerTeleportKeyVk`, `st.markerTeleportPadMask` | `Teleport::TeleportToMarker` via hotkey poll (dx12_hook.cpp:976-988) | menu.cpp:2905-2908 | pad column accepts analog-trigger sentinels (menu.cpp:2838) |
| `Fly Up` | bind-row | `st.flyUpKeyVk`, `st.flyUpPadMask` | `hkLocoStep` free-flight ascend (teleport.cpp:1326 ff.) | menu.cpp:2909-2912 | |
| `Fly Down` | bind-row | `st.flyDownKeyVk`, `st.flyDownPadMask` | `hkLocoStep` free-flight descend | menu.cpp:2913-2916 | |
| `Reset All Keybinds` | button | — | `Settings::ResetBinds` (settings.cpp:484) | menu.cpp:2920-2927 | saves regardless of Auto Save (menu.cpp:2924) |

### 2.30 SYSTEM → Menu UI Settings (`RenderMenuUISettings`, menu.cpp:3023-3063) — 3 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Menu Scale` | slider | `st.menuScale` | `ui::SetScale` (framework.h:27; called menu.cpp:3037); consumed dx12_hook.cpp:661, 810 | menu.cpp:3032-3038 | 0.5–2.5x, step 0.1; font rebuild debounced 300 ms (menu.cpp:3040-3048) |
| `Show Item Tooltip` | toggle | `st.showItemTooltip` | side-panel tooltip gate framework.cpp:1223 | menu.cpp:3050-3051 | UI-only |
| `Tooltip Image Size` | slider | `st.tooltipImageScale` | framework.cpp:1226 | menu.cpp:3053-3054 | 0.8–2.5x, step 0.1 |

### 2.31 SYSTEM → Title Font (`RenderFontSettings`, menu.cpp:2963-3021) — 4 controls

| Control label | Type | Settings/state field | Backing feature function | Source file:line | Notes |
|---|---|---|---|---|---|
| `Built-In Fallback` | preset | `st.builtInFontIndex` | font load framework.cpp:380, 389 | menu.cpp:2973-2977 | 3 items: `Segoe UI (Default)`, `Impact (Rampage)`, `Georgia Bold` (menu.cpp:2970-2972) |
| `Enable Custom Font` | toggle | `st.useCustomFont` | framework.cpp:371-375 | menu.cpp:2979-2983 | |
| `No Fonts Found` | label *(cond.)* | `s_fontFiles` | `ScanFonts` (menu.cpp:2935-2961) | menu.cpp:2990 | only when `st.useCustomFont` and no `.ttf/.otf` found (menu.cpp:2985-2991) |
| `Select Custom Font` | preset *(cond.)* | `st.customFont` | `g_needFontRebuild` (framework.h:29; set menu.cpp:3016) | menu.cpp:3005-3010 | only while `st.useCustomFont` and the scanned list is non-empty |

### 2.32 Overlay extras (not rows)

* FPS counter text (`"%.0f FPS"` / `"%.0f FPS  FLY"`) — `DrawFpsCounter`, menu.cpp:46-72, drawn when
  `st.showFps` (menu.cpp:3172-3173); render gate `WantsDraw`, menu.cpp:40-44.
* Toasts — `ui::Toast` (framework.cpp:1915), drawn every frame by `ui::DrawToasts()` (menu.cpp:3166).
* Side-panel tooltip preview — `End()`, framework.cpp:1220-1260+, gated by `st.showItemTooltip`.

---

## 3. Controls with NO game-side dependency (UI-only)

These read/write only Trinity state or local files; they never touch game memory, signatures or
hooks. (They still persist through `Trinity.ini` when Auto Save is on.)

| Control | Group | Source file:line | Why UI-only |
|---|---|---|---|
| `Menu Scale` | SYSTEM → Menu UI Settings | menu.cpp:3032 | scales the overlay (`ui::SetScale`, framework.h:27) |
| `Show Item Tooltip` | SYSTEM → Menu UI Settings | menu.cpp:3050 | gates the overlay's own preview panel (framework.cpp:1223) |
| `Tooltip Image Size` | SYSTEM → Menu UI Settings | menu.cpp:3053 | overlay panel sizing (framework.cpp:1226) |
| `Theme Color` | SYSTEM root | menu.cpp:3085 | menu accent colours (framework.cpp:54) |
| `PlayStation Icons` | SYSTEM root | menu.cpp:3091 | glyph set only (icons.cpp:472, 489) |
| `Language` | SYSTEM root | menu.cpp:3103 | localization table selection (localization.cpp:298) |
| `Title Font` (submenu) | SYSTEM root | menu.cpp:3113 | header font |
| `Built-In Fallback` | SYSTEM → Title Font | menu.cpp:2973 | font selection |
| `Enable Custom Font` | SYSTEM → Title Font | menu.cpp:2979 | font selection; reads `*.ttf/*.otf` from the game dir (menu.cpp:2942-2960) |
| `No Fonts Found` | SYSTEM → Title Font | menu.cpp:2990 | placeholder label |
| `Select Custom Font` | SYSTEM → Title Font | menu.cpp:3005 | font selection |
| `Show FPS Counter` | SYSTEM root | menu.cpp:3119 | overlay counter (menu.cpp:46-72) |
| `Show Console Window` | SYSTEM root | menu.cpp:3121 | debug console window toggle (logger.h:48, 52) |
| game-version label | SYSTEM root | menu.cpp:3132 | display only (version_detect) |
| `Auto Save Features` | SYSTEM root | menu.cpp:3134 | `Trinity.ini` persistence switch (settings.cpp:272) |
| `Reset All to Default` | SYSTEM root | menu.cpp:3137 | resets Trinity feature state (settings.cpp:448) |
| `Open Menu` (key bind) | SYSTEM → Keybinds | menu.cpp:2901 | menu open/close combo only (framework.cpp:499-520) |
| `Reset All Keybinds` | SYSTEM → Keybinds | menu.cpp:2920 | restores bind defaults (settings.cpp:484) |
| `+ Save Current Location` | TRAVEL → Saved Locations | menu.cpp:1394 | writes `Trinity.ini` bookmarks; the position comes from the read-only `Teleport::GetLastPosition` |
| `Name` (rename bookmark) | TRAVEL → manage | menu.cpp:1488 | local text field + `Settings::Save` |
| `Update to Current Position` | TRAVEL → manage | menu.cpp:1495 | local bookmark data |
| `Delete This Location` | TRAVEL → manage | menu.cpp:1511 | local bookmark data |
| `Clear All Saved Locations` | TRAVEL → Saved Locations | menu.cpp:1451 | local bookmark data |
| bookmark rows (`<n>. <name> (X.. Y.. Z..)`) | TRAVEL → Saved Locations | menu.cpp:1434 | local list; "Open" only pushes a submenu |
| `Teleport Here (...)` | TRAVEL → manage | menu.cpp:1480 | *(not UI-only — calls `Teleport::TeleportToCoordinates`)* |

Partially UI-only (bind storage is local, but the bound combination drives game-side hooks):
`Marker Teleport`, `Fly Up`, `Fly Down` bind rows (menu.cpp:2905-2916) — the values feed
`dx12_hook.cpp:976-988` and `teleport.cpp` free-flight respectively.

---

## 4. Feature gating / fail-closed logic

### 4.1 Readiness probes (`*::Ready()`), gates and their exact conditions

| Gate (quoted) | Source file:line | Controls affected | Effect |
|---|---|---|---|
| `bool Player::Ready() { return g_hpEntries[0].load(...) >= kMinPointer && g_actors[0].load(...) >= kMinPointer; }` | player.cpp:935-939 | `God Mode` (menu.cpp:87-90) | **description only** — the toggle is not disabled; the row text becomes "Keeps your health full. Load into the game world first." |
| `bool Worker::Ready() { return g_ready; }` | worker.cpp:132-135 | `Max Worker Level & Skills` (menu.cpp:134-148) | description swaps (menu.cpp:135-137); toggling while not ready makes `Worker::SetEnabled` return false (`if (!g_ready \|\| !g_patchTarget) return false;` worker.cpp:144-145) → menu reverts the toggle and toasts "Worker patch could not be applied" (menu.cpp:139-143) |
| `bool Friendly::Ready() { return g_hooksInstalled; }` | friendly.cpp:350-353 | `Trust Multiplier` (menu.cpp:156-159) | description only ("Unavailable right now.") |
| `bool World::Ready() { return g_frameTimerUpdateTarget != nullptr; }` | world.cpp:798-801 | `Game Speed` (menu.cpp:1097-1100) | description only |
| `bool World::TimeOfDayReady() { return g_timeClient >= kMinPointer && g_timeServer >= kMinPointer; }` | world.cpp:803-806 | `Freeze Time of Day` (menu.cpp:1103), `Advance Time (+)` (1110), `Rewind Time (-)` (1121), all of `Time of Day Presets` (menu.cpp:990, 996, 1002, 1008, 1015) | description becomes "Unavailable right now."; the actions themselves also fail closed (`if (!g_timeClient) return false;` world.cpp:810) and simply produce no toast |
| `bool Equipment::Ready() { return ClientComp() != 0; }` | equipment.cpp:1592 | `Edit Equipment` description (menu.cpp:128-130) and the whole `RenderEquipSlots` page | hard page gate: `if (!game::Equipment::Ready())` → shows `Character not loaded` / `Waiting for your equipment...` and returns before any action row (menu.cpp:595-604) |
| `bool Equipment::EditsPersist() { return ServerComp() != 0; }` | equipment.cpp:1593 | `Note: not saving yet` row (menu.cpp:743) and all equip-edit writes | informational warning; refinement/socket writes still apply visually |
| `bool Inventory::Ready() { return CurrentHolder() != 0; }` | inventory.cpp:2351-2354 | entire `Item Editor` page | `if (!game::Inventory::Ready())` → `Loading inventory...` and return (menu.cpp:1700-1706); also every `RenderItemRow` path depends on the holder |
| `bool Inventory::EditsPersist() { return HolderLooksValid(CurrentHolder()) \|\| HolderLooksValid(ServerHolder()); }` | inventory.cpp:3353-3356 | `Item Editor` item rows (menu.cpp:1786, 1844), `Set All` (menu.cpp:1814), `Add Item` rows (menu.cpp:1959), `Add All` (menu.cpp:1988), `Add Item` category (menu.cpp:2027) | `const bool locked = !game::Inventory::EditsPersist();` → item rows render inert (`ui::ItemRow(..., locked, ...)`, menu.cpp:1638-1640), `Set All`/`Add All` become inert: `const bool inert = locked \|\| shown == 0;` (menu.cpp:1814, 1988) |
| `bool Dye::Ready() { if (s_dyeMode == 2) return Inventory::ClientHolderAddr() != 0; return ClientComp() != 0; }` | dye.cpp:1654-1659 | dead dye pages only (`if (!game::Dye::Ready())` menu.cpp:294-305) | unreachable code |
| `bool Teleport::LoadCatalog() { if (g_catalogReady...) return true; if (!g_registryGlobal \|\| !g_sceneResolver) return false; ... g_catalogRequested.store(true); return false; }` | teleport.cpp:1994-2001 | `Fast Travel` submenu contents (menu.cpp:1292) | shows `Building destination list...` until the catalog is built; permanent if `g_registryGlobal`/`g_sceneResolver` were never resolved |
| `if (!game::Teleport::GetLastPosition(&x,&y,&z))` | teleport.cpp:1950-1957 (`g_posValid`) | coordinate copy row / `No position yet` (menu.cpp:1145-1158), `+ Save Current Location` (menu.cpp:1396), `Update to Current Position` (menu.cpp:1497) | `No position yet` placeholder; bookmark writes toast "Player position not ready" |
| `const bool hasMarker = game::Teleport::GetMarkerPosition(&mx,&my,&mz);` | teleport.cpp:2071 | `Teleport to Destination` label + description (menu.cpp:1162-1180) | label shows `None` and the description asks for a map waypoint; activating returns `MarkerStatus::NoMarker` → toast "No destination found on map" (menu.cpp:1192-1194) |
| `if (n == 0)` / `if (total == 0)` / `if (shown == 0)` empty-list gates | menu.cpp:382, 666, 903, 934, 969, 1324, 1416, 1752, 1835, 1927, 2018, 2486, 2566, 2654 | list pages | replace the list with a placeholder row |
| `if (si.maxSockets > 0 && si.unlockedCount < si.maxSockets)` | menu.cpp:686 | `Unlock all sockets` | row hidden otherwise |
| `if (si.filledCount > 0)` | menu.cpp:698 | `Clear all sockets` | row hidden otherwise |
| `if (si.maxSockets == 0)` | menu.cpp:735-740 | socket list | replaced by `No sockets` label and early return |
| `if (si.maxSockets > 0)` label form `"(%d/%d sockets used)"` vs `"(no sockets)"` | menu.cpp:640-646 | equipped-piece submenu labels | label only |
| `if (rec.typeId && game::Equipment::IsItemEquippedOnAnyCharacter(rec.typeId)) continue;` | menu.cpp:2606-2608 | Restore Items lost-item rows | rows for currently-equipped items are suppressed |
| `if (langCount > 1)` | menu.cpp:3096 | `Language` combo | combo hidden when only the built-in English table exists |
| `if (st.useCustomFont)` / `if (s_fontFiles.empty())` | menu.cpp:2985-2991 | `Select Custom Font`, `No Fonts Found` | conditional rows |
| `if (st.showItemTooltip && (g_selectedTooltip.valid \|\| g_selectedItemName[0] != '\0'))` | framework.cpp:1223 | tooltip preview panel | hidden when off or no data |
| `if (ui::Submenu(LOC("Title Font"), ...))` desc "(requires game restart)" | menu.cpp:3113-3114 | `Title Font` | font only applies after restart (`g_needFontRebuild`, menu.cpp:3016) |
| `save \|= ui::Toggle(...) && st.autoSave;` | menu.cpp:3051, 3092, 3119 | `Show Item Tooltip`, `PlayStation Icons`, `Show FPS Counter` | value applies immediately, but is only written to `Trinity.ini` while Auto Save is on |
| `if (!g_owner) return;` in `Settings::Save()` | settings.cpp:275-276 | every persisting control | `Trinity.ini` is only written by the presenting process (claimed via `Settings::ClaimOwnership`, settings.cpp:267-270, called from the render path) |

### 4.2 Version / PE-revision gates that can disable a menu feature

| Condition (quoted) | Source file:line | Controls affected | Effect |
|---|---|---|---|
| `inline bool WorkerPatchSupportedForRevision(int revision) { return revision == 2850 \|\| revision == 2944; }` | worker_logic.h:18-21, used at worker.cpp:50-55 | `Max Worker Level & Skills` | on any other revision `Worker::Install` returns false → `Worker::Ready()` stays false → every toggle attempt is reverted with a toast (menu.cpp:139-143) |
| `if (core::UsesTu201CompatibleRevision(core::GetGameVersion().revision)) { /* modern continuous stat-pin */ } else { mem::InstallHook("player: stat-commit", kSig_StatCommit, "direct write guard unavailable; current-character pins remain active", ...); }` | player.cpp:821-830; helper `version_mapping.cpp:22-28` (`revision == 2760 \|\| 2850 \|\| 2944`) | `God Mode`, `Infinite Stamina & Mount`, `Infinite Spirit` | selects the guard ABI; the non-TU201 branch may install no hook at all ("direct write guard unavailable") |
| `const auto locoContract = core::LocoStepperContractForRevision(revision);` … `case Unsupported: LOG_ERR("teleport: locomotion-stepper contract unavailable for PE %u - Super Run/Free Flight disabled.")` | teleport.cpp:1905-1926 (`version_mapping.cpp:48-60`) | `Super Run`, `Super Jump`, `Free Flight`, `Fly Up`/`Fly Down` binds | no locomotion hook on unobserved revisions; also `if (g_locoStepTarget) ... else LOG_ERR("... NOT installed - Super Run/Free Flight disabled.")` (teleport.cpp:1930-1933) |
| `if (core::MayProbeLegacyCrimeEventDispatcherForRevision(revision)) { mem::InstallHook("world: register-crime-event", ...) } else { LOG("world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.") }` | inventory.cpp:2070-2083 (`version_mapping.cpp:67-70`, `revision <= 2850`) | `No Bounty` | on PE 2944 only part of the bypass exists (banner/minimap/guard dispatch absent) |
| `if (core::GetGameVersion().revision < 2625) { MH_CreateHook(gameBase + 0x16077B0, hkGetMoney1, ...) ... } else { LOG("inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid)."); }` | inventory.cpp:2096-2105 | money rows (`Set Wallet ...`, `Add Amount ...`, presets) | legacy wallet display hooks absent on TU 2.00+ |
| `const bool usesTu201CompatibleAbi = core::UsesTu201CompatibleRevision(revision); const bool allowLegacyFuzzy = core::MayUseLegacyFuzzySignaturesForRevision(revision);` | inventory.cpp:2119-2120 (`version_mapping.cpp:62-65`) | everything under `Add Item`, `Abyss Items`, `Restore Items`, money spawning | selects the add-item primitives; without a resolvable ctor/planner the add path is refused (comment inventory.cpp:2115-2118: "without any one of them Add Item is refused, and every other inventory feature still works") |
| `if (!oSetExpandSlots && !core::UsesTu201CompatibleRevision(core::GetGameVersion().revision)) return false;` | inventory.cpp:2847-2848 | `Slot Size` | slot-cap override fails closed |
| `if (!mem::FindPattern(kSig_InvGetHolder)) { LOG_ERR("inventory: holder resolver signature NOT FOUND - inventory disabled."); return false; }` | inventory.cpp:2107-2112 | whole INVENTORY tab | `Inventory::Ready()` stays false → `Loading inventory...` forever |
| `if (core::UsesTu201CompatibleRevision(...)) return 0;` in `InventoryCoreGlobalMovOffsetForRevision`; `RealmFlagOffsetForRevision` returns `0x1EC` (2850/2944), `0x1FD` (2760), else `0x1F2` | version_mapping.cpp:72-90 | item add / quantity writes (realm selection) | wrong-revision realm byte causes silent engine rejections (comment version_mapping.cpp:79-87) |
| `const bool isLegacy = core::IsLegacyTU(); const uintptr_t unlockOff = isLegacy ? 0x68 : 0x70;` | equipment.cpp:439-440 (also 522-525, 552-555, 590-594); `IsLegacyTU` version_detect.cpp:100 | all equipment socket rows (`Unlock all sockets`, `Clear all sockets`, socket submenus, `Unlock All Sockets`) | selects socket-vector field offsets per TU family; wrong offsets → `ProfileTargetValid` refuses writes (equipment.cpp:580-597) |
| startup readiness: `const auto profile = trinity::core::ReadinessProfileForRevision(...)` … `required` signature array per profile, then `if (!trinity::mem::FindPattern(required[i])) return false;` | mod.cpp:60-73 (`readiness.cpp:8-13`, `version_mapping.cpp:22-28`) | every gameplay-backed control | if gameplay code never materialises, `WaitForReadiness` times out after 180 s (`mod.cpp:116-125`) and only the hooks that resolved are installed — the overlay still runs, affected features stay inert |
| `if (!mem::InstallHook("inventory: item-count accessor", kSig_InvGetItemQty, ...)) { if (!mem::InstallHook(..., kSig_InvGetItemQty_Legacy, "inventory disabled", ...)) return false; }` | inventory.cpp:2085-2091 | item counts everywhere | legacy fuzzy signature fallback is itself revision-gated by `MayUseLegacyFuzzySignaturesForRevision` (inventory.cpp:2120) |
| `if (matches.size() != 1) { ... "feature disabled." }` / unexpected-byte checks | worker.cpp:57-89 | `Max Worker Level & Skills` | fail-closed patch validation |

### 4.3 Other fail-closed behaviour worth noting

* `game::Equipment::EquipItemToSlot` failure → toast "Could not equip item" (menu.cpp:895).
* `game::Dye::Apply` / `Clear` are queued; failures surface as toasts (`ReportPendingDye`, menu.cpp:189-207).
* `ReportPendingAdd` / `ReportBulkAdd` (menu.cpp:1861-1891) translate the game-side add state into
  "Item added" / "Could not add that item - see the log" / "Added %d items, some failed".
* Rebind capture is abandoned if the user leaves the Keybinds page:
  `if (st.rebindCapture && (ui::CurrentTab() != TabSystem || strcmp(cur, "keybinds") != 0)) st.rebindCapture = false;`
  (menu.cpp:3186-3187); capture also self-cancels after 6 s (`const bool timedOut = GetTickCount64() - startMs > 6000;` menu.cpp:2846).

---

## 5. Localization keys

There is **no symbolic key table**. `LOC(str)` is `trinity::loc::Tr(str)` (`src/core/localization.h:37`),
and `Tr` looks the argument up verbatim in a flat map (`src/core/localization.cpp:331-341`):

```cpp
const char* Tr(const char* text) {
    if (!text || text[0] == '\0' || s_currentIndex == 0) return text;
    auto it = s_translations.find(text);
    if (it != s_translations.end()) return it->second.c_str();
    return text;
}
```

The loader stores every `key=value` line whose section is **not** `[Language]`
(`localization.cpp:86-98`; the `[Language]` section supplies only `Name`/`Code`,
`localization.cpp:141-151`). **Therefore the localization key for every control label is the
exact English string** shown in the tables above — e.g. the key for the God Mode row is
`God Mode=`, and for its description `Keeps your health full.=`.

Files scanned: `Trinity_*.ini` beside `Trinity.asi`, plus `Languages\` and `languages\`
subfolders (`localization.cpp:247-249`). Built-in index 0 is English, which disables lookup
entirely (`Tr("...")` returns early when `s_currentIndex == 0`, localization.cpp:333).

Shipped tables (`languages/`): `Trinity_de.ini`, `_es`, `_fr`, `_id`, `_ja`, `_ko`, `_ptbr`,
`_ru`, `_zh`. Spot check of `languages/Trinity_de.ini` (677 keys): it contains keys for almost
every current control but still carries **stale keys for controls no longer drawn** —
`Dye Equipment=`, `Pardon Contract (Clear Bounty)=`, `Easy Parry=`, `Infinite Stamina=` — and is
**missing** at least `Max Worker Level & Skills` (rendered untranslated).

### Labels that are **not** localized (raw literals) — they cannot be translated

| String | Source file:line |
|---|---|
| `"Kliff"`, `"Damiane"`, `"Oongka"` (character combos) | menu.cpp:286, 588 |
| Character filter items `"All Characters"`, `"Current Character Only"`, `"Kliff Only"`, `"Damiane Only"`, `"Oongka Only"` | menu.cpp:786-792 |
| Category filter items `"All Categories"`, `"Matching Slot Only"`, `"Weapons"`, `"Shields & Off-Hand"`, `"Armor"`, `"Accessories"` | menu.cpp:797-804 |
| Weather preset items `"Dynamic (Game Default)"`, `"Clear Sky (Sunny)"`, `"Overcast (Cloudy)"`, `"Rainy (Light Rain)"`, `"Thunderstorm (Storm)"`, `"Dense Fog / Mist"` | menu.cpp:1032-1039 |
| Dye zone items `"All zones"`, `"Zone 1"`..`"Zone 12"` (dead page) | menu.cpp:417-420 |
| `"No matches"` in the shared filtered-list template | menu.cpp:1253 |
| Fast-travel row description `"Fast travel to %s at %.0f, %.0f, %.0f."` and toast `"Warping to %s"` | menu.cpp:1345, 1351 |
| Dye swatch descriptions (`"Pick a tone, or the first swatch to remove the dye."`, `"Pick a color to dye it right away."`) | menu.cpp:469-470 |
| Socket submenu description `"Recolor this piece."` | menu.cpp:367 |
| Node/keybind capture prompts `"press a key..."`, `"press a button..."`, `"Press the key you want to bind, or Esc to cancel."`, `"Press the button or combo you want to bind, or Esc to cancel."` | menu.cpp:2764-2765, 2770-2771 |
| Toast formats for bind resets/sets (`"%s keyboard bind reset to %s"`, `"%s controller bind reset to %s"`, `"%s set to %s"`) | menu.cpp:2786, 2792, 2862, 2877 |
| Pad button names `"LB"`, `"RB"`, `"D-Pad Up"`, … (`PadMaskName`) | menu.cpp:2700-2717 |
| Key names `"None"`, `"Insert"`, `"Delete"`, … (`KeyName`) | menu.cpp:2667-2692 |
| `"Keyboard"` / `"Controller"` column titles | widgets.cpp:1562-1563 |
| Effect suffix `"[Effect: %s]"` | widgets.cpp:335 |
| Shell footer hints (`"Enter select"`, `"Enter toggle"`, `"B close   LB/RB tab"`, …) | framework.cpp:1115-1123, 1193-1195 |
| Brand/version text `"TRINITY"` and `"v" TRINITY_VERSION` | framework.cpp:935-936, 938 |
| `"Engine BLOCK: ..."`, `"Please sell 1 junk item ..."` toasts | menu.cpp:2089-2090 |

---

## 6. Totals and gaps

### 6.1 Counts per group (distinct registrations)

| Tab / group | Controls |
|---|---|
| Shell (5 tabs) | 5 |
| PLAYER root | 7 |
| PLAYER → Combat & Gameplay Options | 9 |
| PLAYER → Edit Equipment (list) | 8 |
| PLAYER → Edit Equipment → piece | 9 |
| PLAYER → … → Change Equipment | 6 |
| PLAYER → … → socket gear picker | 6 |
| **PLAYER subtotal** | **45** |
| INVENTORY root | 8 |
| INVENTORY → Item Editor | 3 |
| INVENTORY → Item Editor → storage | 5 |
| INVENTORY → storage → category | 5 |
| INVENTORY → Add Item | 6 |
| INVENTORY → Add Item → category | 5 |
| INVENTORY → Money & Currency | 10 |
| INVENTORY → Money → Optional | 2 |
| INVENTORY → Abyss Items & Artifacts | 16 |
| INVENTORY → Restore Items | 8 |
| INVENTORY → Restore → Catalog Archive | 8 |
| INVENTORY → Restore → category page template (×8 pages) | 5 (40 rows) |
| **INVENTORY subtotal** | **81 distinct (116 rows incl. the 8 page instances)** |
| TRAVEL root | 6 |
| TRAVEL → Saved Locations | 4 |
| TRAVEL → Saved Locations → manage | 5 |
| TRAVEL → Fast Travel (categories) | 2 |
| TRAVEL → Fast Travel → nodes | 4 |
| **TRAVEL subtotal** | **21** |
| WORLD root | 6 |
| WORLD → Time of Day Presets | 6 |
| WORLD → Weather & Atmosphere | 17 |
| **WORLD subtotal** | **29** |
| SYSTEM root | 11 |
| SYSTEM → Keybinds | 6 |
| SYSTEM → Menu UI Settings | 3 |
| SYSTEM → Title Font | 4 |
| **SYSTEM subtotal** | **24** |
| **TOTAL (live, distinct registrations, incl. 5 shell tabs)** | **205** |
| **TOTAL live rows if the 8 restore-category pages are counted individually** | **240** |
| Dead / unreachable registrations (dye editor, §2.7) | 20 |
| Auxiliary nav calls excluded above: `ui::ListJump()` ×4 (menu.cpp:1290, 1320, 1763, 1936), `ui::BindHeader()` counted as a control at menu.cpp:2895 | — |

Breakdown by widget API (live call sites in `menu.cpp`): `Option` 90, `Submenu` 25, `Toggle` 19,
`FloatOption` 17, `IntOption` 11, `Combo` 11, `Search` 9, `IntAction` 5, `ToggleFloat` 5,
`SubmenuItem` 4, `OptionItem` 3, `OptionItemWithSubtitle` 2, `SwatchRow` 2, plus one each of
`ToggleInt`, `OptionItemWithBuff`, `SubmenuEquipItem`, `ItemRow`, `ItemAddRow`, `BookmarkRow`,
`TextInput`, `BindRow`, `BindHeader`. (Raw counts include the 20 dead dye-page registrations and
loop bodies counted once each.)

### 6.2 Gaps / uncertainties

1. **The dye editor is unreachable (confirmed, not merely suspected).** `RenderDyeSlots`,
   `RenderDyeEdit`, `RenderDyeCustom` (menu.cpp:281, 400, 513) have no dispatcher entry
   (menu.cpp:3189-3233) and no caller; the string `"dyeslots"` appears nowhere in the
   repository. `git log -S'"dyeslots"' -- src/gui/menu.cpp` shows the two push sites deleted by
   commit `7c05de4` ("fix: update Trinity for Crimson Desert 2.02.00"). Its 20 controls are
   inventoried in §2.7 but are **not visible** and are excluded from the live totals.
2. **Localization is stale in both directions** (§5): `languages/Trinity_de.ini` still contains
   keys for removed controls (e.g. `Dye Equipment`, `Easy Parry`, `Pardon Contract (Clear Bounty)`,
   `Infinite Stamina`) and lacks at least `Max Worker Level & Skills`. Only the German table was
   spot-checked; the other eight were not diffed key-by-key.
3. **"Control" counting is a judgement call for three row families.** (a) informational rows
   built with `ui::Option` whose return value is discarded (menu.cpp:987, 2169, 2405, 2576, 3132)
   are counted as controls and typed `label`; (b) placeholder rows (`No matches`,
   `Loading inventory...`, `Empty`, `No position yet`, …) are counted and typed `label`;
   (c) loop-generated rows (per-item, per-category, per-storage, per-socket, per-gear) are
   counted as **one** control each. If the requester wants only interactive controls, subtract
   the 36 `label`-typed rows spread across the live tables (3 in §2.3, 3 in §2.4, 2+3 in
   §2.5-2.6, 1 in §2.9, 2 each in §2.10-§2.13, 1 in §2.16, 3 in §2.17, 2 in §2.19, 1 each in
   §2.20-§2.23 and §2.26/§2.28/§2.31, 2 in §2.24) — i.e. 205 − 36 = **169 interactive controls**
   (204 − 36 = 168 if the 5 tab-strip entries are treated as navigation rather than controls).
4. **`ui::ListJump()` is not counted** as a control (it is a navigation binding, not a row);
   it appears at menu.cpp:1290, 1320, 1763, 1936.
5. **Two entries are marked uncertain in the source's own comments:** `Max Worker Level & Skills`
   is described in the UI as failing closed with "Worker patch unavailable for this game
   revision." (menu.cpp:137) and the supported revisions are hard-coded (worker_logic.h:18-21),
   so on PE 2760 and older the row is visible but never effective. Likewise the `Slot Size` row
   is visible on revisions where `ApplySlotCapToHolder` refuses to run (inventory.cpp:2847-2848).
6. **Hotkey-only paths.** `Marker Teleport` also fires from the bound key/pad outside the menu
   (`dx12_hook.cpp:976-988`) and `Free Flight` ascend/descend are read by `teleport.cpp`
   directly; those are the same state fields as the Keybinds rows, so no extra rows exist — but
   a full behavioural audit of those features is outside this inventory.
7. **Resolved during this pass:** `Equipment::EquipItemToSlot` is declared
   `static bool EquipItemToSlot(uint16_t tag, uint16_t typeId, int64_t instId = 0);`
   (`src/game/equipment.h:140`, defined `equipment.cpp:1926`), which is why `menu.cpp:887` may
   call it with two arguments. No open question remains for that call.
8. **Not audited:** the nine non-German localization tables were not diffed key-by-key against
   the control list; §5 reports only what was verified for `Trinity_de.ini`.
