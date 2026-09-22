# Trinity — source-side consumers: TRAVEL and MOVEMENT/LOCOMOTION

Scope: static documentation of every place the Trinity mod touches the Crimson Desert
executable for the **Travel** and **Movement/Locomotion** feature areas, with exact
`source file:line` citations, verbatim byte patterns/offsets, and the assumed ABI.

Sources read in full: `src/game/teleport.cpp` (2195 lines), `src/game/teleport.h`,
`src/game/travel_logic.h`, `src/game/marker_teleport_logic.h`, `src/game/map_marker.h`,
`src/game/player.cpp` (984 lines), `src/game/player.h`, `src/game/player_logic.h`,
`src/game/player_logic.cpp`, `src/mem/hooks.h`, `src/mem/safe_memory.h`.
Grepped for relevance (not full reads): `src/game/offsets.h`, `src/core/version_mapping.{h,cpp}`,
`src/core/state.h`, `src/core/logger.h`, `src/core/mod.cpp`, `src/mem/scanner.h`,
`src/hooks/xinput_hook.h`, `src/hooks/dx12_hook.cpp`, `src/gui/menu.cpp`, `src/game/world.cpp`.

Convention in this document: `f:NNN` = `path/file:line`. No source file was modified.

---

## 0. Entry points and wiring summary

| Item | Value | Source |
|---|---|---|
| Install order | `game::Player::Install();` then `game::Teleport::Install();` | `src/core/mod.cpp:129-130` |
| Install is non-fatal | return values of both `Install()`s are discarded; overlay keeps running | `src/core/mod.cpp:127-135` |
| Removal | `game::Player::Remove(); game::Teleport::Remove();` | `src/core/mod.cpp:157-158` |
| MinHook init | `MH_Initialize()` must precede both installs | `src/core/mod.cpp:97-101`, `src/game/teleport.h:15`, `src/game/player.h:36-37` |
| Readiness probe (address presence only, no hook) | `kSig_MoveUpdate`, `kSig_DamageApply_Alt`, `kSig_CombatTimingEval`, … | `src/core/mod.cpp:36-58` |

`Teleport` (declared `src/game/teleport.h:12-110`) owns: position tracking, fast travel /
map-gimmick catalog, Map Marker Teleport, and (via the same file) Super Run / Super Jump /
Free Flight. `Player` (declared `src/game/player.h:33-70`) owns stat/combat features; it is
in this area's file list but is **not itself locomotion** — only three couplings to
locomotion exist, listed in §A.2 and rows `P6`/`P7`.

---

## A. Feature list (user-visible menu features + exact C++ entry points)

### A.1 TRAVEL

Menu tab: `TRAVEL` — `kTabs`/`Tab` at `src/gui/menu.cpp:35-36`, dispatched at
`src/gui/menu.cpp:3195`. Page body: `RenderTravel()` `src/gui/menu.cpp:1139-1229`;
sub-pages `RenderFastTravelCats()` `src/gui/menu.cpp:1287-1315`,
`RenderFastTravelNodes()` `src/gui/menu.cpp:1317-1357`,
`RenderSavedLocations()` `src/gui/menu.cpp:1386-1460`,
`RenderSavedLocationManage()` `src/gui/menu.cpp:1462-1520`; routing at
`src/gui/menu.cpp:3206-3207`.

| # | Menu feature (label) | C++ entry point(s) | Game-side effect |
|---|---|---|---|
| TV1 | Live position readout `"X %.2f  Y %.2f  Z %.2f"` | `Teleport::GetLastPosition` `src/gui/menu.cpp:1145` → `src/game/teleport.cpp:1950-1957` | reads cached `g_posX/Y/Z` published by `hkMoveUpdate` from `moveOwner+0x90` |
| TV2 | `"Copy these coordinates to the clipboard."` | `Teleport::CopyPositionToClipboard` `src/gui/menu.cpp:1151` → `src/game/teleport.cpp:1964-1991` | none (Win32 clipboard only) |
| TV3 | `"Teleport to Destination"` (map marker teleport) | `Teleport::GetMarkerPosition` `src/gui/menu.cpp:1162`; `Teleport::TeleportToMarker(st.markerFallbackHeight)` `src/gui/menu.cpp:1184` → `src/game/teleport.cpp:2100-2162` | queues a write applied inside `hkMoveUpdate` |
| TV4 | Marker teleport hotkey (default `VK_F10` = `0x79`) | `st.markerTeleportKeyVk` `src/core/state.h:93`; polled at `src/hooks/dx12_hook.cpp:962-1013` → `Teleport::TeleportToMarker` `src/hooks/dx12_hook.cpp:988` | same as TV3 |
| TV5 | Async marker result toast | `Teleport::ConsumeMarkerResult` `src/hooks/dx12_hook.cpp:954` → `src/game/teleport.cpp:2164-2170` | none |
| TV6 | `"Sky Arrival Altitude"` slider (`st.markerFallbackHeight`, 50–3000, default 1200) | `src/gui/menu.cpp:1210-1215`, `src/core/state.h:95` | consumed by `TeleportToMarker` fallback branch `src/game/teleport.cpp:2120-2136` |
| TV7 | `"Saved Locations"` bookmarks (`+ Save Current Location`, rename, `Update to Current Position`, delete, clear all) | `Teleport::GetLastPosition` `src/gui/menu.cpp:1392,1494` | none |
| TV8 | `"Teleport Here"` (bookmark teleport) | `Teleport::TeleportToCoordinates` `src/gui/menu.cpp:1482` → `src/game/teleport.cpp:2172-2194` | queues a raw destination write (no origin subtraction) |
| TV9 | `"Fast Travel"` → category list | `Teleport::LoadCatalog` `src/gui/menu.cpp:1225,1292`; `CategoryCount` `:1300`; `GetCategory` `:1307`; `EnsureCategoryNodes` `:1312` | builds catalog on game thread |
| TV10 | Fast-travel destination list (`"Warp to %s"`) | `Teleport::EnsureCategoryNodes` `src/gui/menu.cpp:1322`; `NodeCount` `:1323`; `GetNode` `:1341`; `TravelToNode` `:1350` → `src/game/teleport.cpp:2047-2057` | queues native fast travel, fired at `src/game/teleport.cpp:1666-1693` |
| TV11 | `"Building destination list..."` placeholder | `LoadCatalog` return `src/gui/menu.cpp:1292-1298` | none |

There is **no** "fast travel while mounted"/"teleport to waypoint" beyond TV3/TV10, and **no**
separate "Free Flight"/"Game Speed" entry inside the TRAVEL tab (those live in PLAYER/WORLD, §A.2).

### A.2 MOVEMENT / LOCOMOTION

Menu tab: `PLAYER` — `RenderPlayer()` `src/gui/menu.cpp:119-164`.

| # | Menu feature (label) | C++ entry point(s) | Game-side effect |
|---|---|---|---|
| LM1 | `"Super Run"` toggle + multiplier (1.0–10.0x, default 2.0) | `st.superRun` / `st.superRunMult` `src/gui/menu.cpp:150`; state `src/core/state.h:63-64` | `hkLocoStep` ground path `src/game/teleport.cpp:1539-1563` |
| LM2 | `"Super Jump"` toggle + multiplier (1.0–10.0x, default 2.0) | `st.superJump` / `st.superJumpMult` `src/gui/menu.cpp:152`; state `src/core/state.h:65-66` | `ApplyJumpScaling` inside `hkMoveUpdate` `src/game/teleport.cpp:1210-1222`, called at `:1588` |
| LM3 | `"Free Flight"` toggle + speed (1–40, default 8) | `st.freeFlight` / `st.flightSpeed` `src/gui/menu.cpp:154`; state `src/core/state.h:76-77` | `hkLocoStep` flight path `src/game/teleport.cpp:1400-1519` |
| LM4 | Fly Up / Fly Down keybinds (defaults `VK_CAPITAL 0x14` / `VK_CONTROL 0x11`) | `st.flyUpKeyVk` / `st.flyDownKeyVk` `src/core/state.h:78-79`; bind rows `src/gui/menu.cpp:2839,2841`; editor `:2906-2915` | `PollFlyInputs` `src/game/teleport.cpp:1285-1288` |
| LM5 | Fly Up / Fly Down controller binds (defaults `RB 0x0200` / `RT 0x20000`) | `st.flyUpPadMask` / `st.flyDownPadMask` `src/core/state.h:86-87`; bind rows `src/gui/menu.cpp:2840,2842` | `PollFlyInputs` `src/game/teleport.cpp:1300-1303` |
| LM6 | HUD `"  FLY"` indicator (appended to the FPS counter) | `Teleport::GetFlightEngaged` `src/gui/menu.cpp:55-58` → `src/game/teleport.cpp:1959-1962` | reads `g_flightEngaged` published at `src/game/teleport.cpp:1523` |
| LM7 | *(adjacent, WORLD tab)* `"Game Speed"` toggle + multiplier (0.1–5.0x) | `st.gameSpeed` / `st.gameSpeedMult` `src/gui/menu.cpp:1097`; state `src/core/state.h:109-110` | `hkFrameTimerUpdate` `src/game/world.cpp:43-76`; the **tick dispatch** rides the movement hook: `World::Tick()` at `src/game/teleport.cpp:1645` (no-op for speed, see `src/game/world.cpp:632`) |
| LM8 | *(adjacent, PLAYER tab)* `"No Fall Damage"` — interacts with flight/protection | `st.noFallDamage` `src/gui/menu.cpp:93`, default `true` `src/core/state.h:48` | `hkDamageApply` `src/game/player.cpp:762-767`; also gates the player-set resolve `src/game/player.cpp:325-328` |

There is **no** Trinity-side "sprint speed", "walk speed", "climb speed" or "dodge" feature.
Movement speed is deliberately *not* applied via the stat array — see the note at
`src/game/offsets.h:115-119` ("writing them has NO effect on locomotion").

---

## B. Game-side function contracts

### B.1 TRAVEL touch points (21 rows)

| Trinity role | Game function or address expression | Address obtained by | Exact ABI assumed | Object/structure operated on | Offsets read/written | Source | Fail-safe if locator missing |
|---|---|---|---|---|---|---|---|
| T1 | hook (MinHook inline detour) | `sub_3A3E140` — matched by `kSig_MoveUpdate` = `"48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56"` | `mem::InstallHook` → `FindPattern` + `CountMatches` + `MH_CreateHook` + `MH_EnableHook` (`src/mem/hooks.h:31-64`) | `uint64_t __fastcall hkMoveUpdate(uint64_t moveOwner /*rcx*/, uint64_t a2, a3, a4, a5, a6, a7)`; return `uint64_t` in RAX passed through from the trampoline | per-actor "physics move controller" / Havok character proxy | write `+0x90` (destination), `+0xC0` (desired vel), `+0xD0` (velocity), `+0x1A0` (destination 2); read `+0x90` | `src/game/teleport.cpp:60-62`, `:1575-1577`, `:1839-1842`; sig `src/game/offsets.h:325-326` | `InstallHook` logs `LOG_ERR("%s signature NOT FOUND - %s.", …)` (`src/mem/hooks.h:35`); `Teleport::Install` returns `false` at `src/game/teleport.cpp:1841` — **but `src/core/mod.cpp:130` ignores it**, so the overlay still starts with position tracking, fast-travel dispatch, marker apply and all game-thread ticks absent |
| T2 | native call | PE 2944 confirmation dispatcher `sub_1406550B0` — `kSig_TravelDispatcher_PE2944` = `"48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 41 56 41 57 48 8D AC 24 50 FE FF FF 48 81 EC B0 02 00 00 41 8B F9 45 8B F8 33 DB"` | `mem::FindPattern(kSig_TravelDispatcher_PE2944)` gated on `revision == 2944` | `char __fastcall(void* /*rcx, ignored*/, int sceneId /*edx*/, unsigned nodeIndex /*r8d*/, int travelMode /*r9d*/)`; returns acceptance in **AL**; called as `g_travelFn(call.context, call.sceneId, call.nodeIndex, call.travelMode)` | the game's own travel manager, pulled from an internal global by the callee (RCX deliberately `nullptr`) | none read/written by Trinity | `src/game/teleport.cpp:68-69`, `:1851-1852`, `:1858`, `:1678`; ABI note `src/game/offsets.h:448-456`; arg packing `src/game/travel_logic.h:11-30` | if either pattern is missing: `LOG_ERR("teleport: PE 2944 native Fast Travel contract incomplete (selection=%p dispatcher=%p) - menu disabled.")` `:1864`, `travel` stays 0 |
| T3 | address resolution only (never called) | PE 2944 selection gate — `kSig_TravelToNode_PE2944` = `"89 54 24 10 48 89 4C 24 08 53 55 56 57 41 54 41 56 41 57 48 81 EC 90 00 00 00 41 8B D8 33 FF"` | `mem::FindPattern(kSig_TravelToNode_PE2944)` | none — resolved purely to prove the contract is complete and for the log line | n/a (its caller-side bounds check is `SceneDesc+0x28`, documented in `src/game/offsets.h:438-443`) | none | `src/game/teleport.cpp:1851`, `:1859` | if missing, the PE 2944 contract is judged incomplete → row T2 fail-safe applies |
| T4 | native call (pre-2944 revisions) | legacy fast-travel trigger — `kSig_TravelToNode` (`src/game/offsets.h:425-427`), then `kSig_TravelToNode_Pre201` (`:430-432`), then `kSig_TravelToNode_Legacy` (`:435-436`) | `mem::FindPattern` chain, only when `revision != 2944` | same `char __fastcall(void*, int, unsigned, int)` as T2 (comment `src/game/offsets.h:421` documents the 3-arg form `sub_505140`) | as T2 | none | `src/game/teleport.cpp:1868-1875`, `:1877-1882` | `LOG_ERR("teleport: fast-travel trigger signature NOT FOUND - fast-travel menu disabled.")` `:1885`; note the `else if (revision != 2944)` at `:1883` means **no second error is logged on PE 2944** |
| T5 | native call (lazy table resolver) | `LevelGimmickSceneObjectInfo` scene resolver (IDB `sub_396CC0` per `src/game/offsets.h:458-461`) | string-anchored scan: `mem::FindPatternIf(kSig_LeaR8Rip, &LeaIsResolverTableRef, &scan)` + `FindResolverPrologueAbove` + `mem::ResolveRipAt(fn + kOff_TableResolver_MovGlobal, kLen_MovGlobalInstr)` | `uintptr_t __fastcall(uint32_t* key /*rcx*/)` → row pointer in RAX; **game-thread only** (lazy-loads the row) | scene descriptor (`LevelGimmickSceneObjectInfo`) data-table row | read `+0x28` nodeCount, `+0x20` nodeArray, `+0x10` isBlocked, `+0x49` useTeleport, `+0x08` stringKey | `src/game/teleport.cpp:1796-1834`, `:1888`, call site `:1119`, `:747-752`; consts `src/game/offsets.h:485-513` | `LOG_ERR("teleport: scene-registry resolver NOT FOUND - fast-travel menu disabled.")` `:1889`; `BuildCatalogGameThread` returns false at `:1104-1105`; `LoadCatalog` returns false at `:1997` |
| T6 | memory read | scene registry global `g_registryGlobal` → `*(global)` then `+0x08` sceneCount | resolved from the resolver prologue's `mov rbx, cs:<global>` (with a dynamic forward scan fallback) | `Registry()` = `ReadPtr(g_registryGlobal, &r)` | the `LevelGimmickSceneObjectInfo` registry object | read `+0x08` (u32 sceneCount). `kOff_Registry_SceneTable = 0x50` is **declared but never used** by `teleport.cpp` | `src/game/teleport.cpp:754-759`, `:1104-1109`; `:1805-1833`; const `src/game/offsets.h:487-488` | `Registry()` returns 0 → `BuildCatalogGameThread` returns false at `:1105` |
| T7 | native call + memory read | `FieldLevelNameTableInfo` area-name resolver + its registry global; probes `"regioninfo"` then `"fieldinfo"` as fallbacks | string-anchored scan, same routine as T5 | `uintptr_t __fastcall(uint32_t*)` (one call per field id) | `LevelNameInfo` hash-map rows and entries | read row `+0x20` buckets, `+0x24` size, `+0x30` bucket array, `+0x38` entry array; bucket stride `0x100`, pairs at `+0x08`; entry `+0x18` `_isSectorLevel`, `+0x10` engine string name, `+0x1C` 6 floats AABB | `src/game/teleport.cpp:788-851`, `:1892-1901`; consts `src/game/offsets.h:532-556` | `LOG_WARN("teleport: area-name resolver not found - waypoint names fall back to indices.")` `:1898` (non-fatal; `BuildAreaBoxes` early-returns at `:792`) |
| T8 | memory read | gimmick scene **node** array | node = `na + kNode_Stride * i` | `TpNode` catalog entry | read `node + 0x84` (f32 x,y,z), `node + 0x10` (gimmick pointer, **flagged UNVERIFIED** at `src/game/offsets.h:492`) | `src/game/teleport.cpp:1148-1168`; consts `src/game/offsets.h:491-493` | per-node `ReadVec3` failure leaves `pos = {0,0,0}`, node still added (`:1151-1152`) |
| T9 | memory read | direct map-destination pointer global `g_markerDestinationGlobal` | `mem::FindAllMatches("48 8B 05 ?? ?? ?? ?? 48 8B 98 A8 00 00 00 C4 C1 78 10 04 24", 2)` then `mem::ResolveRipAt(destinationRefs.front(), 7)`, accepted only if `size()==1` | inline AOB (10-byte instruction + 7-byte RIP operand, `ResolveRipAt(...,7)`) | map UI state object | read `*(uiGlobal)` → `+0xA8` → `+0x20` (3 floats) | `src/game/teleport.cpp:528-532`, `:696-707`; consumer `src/game/map_marker.h:16-34` | `LOG_WARN("teleport: direct map-destination reference not found; legacy capture hooks are required.")` `:541`; only legal if capture hooks installed (see T14/T15 and `src/game/marker_teleport_logic.h:18-23`) |
| T10 | memory read (marker capture signature set) | `kSig_MarkerPattern` = `"C5 FB 10 07 C5 FB 11 02 8B 47 08 89 42 08"`, expected `kExpected_MarkerMatches = 5` | `mem::FindAllMatches(kSig_MarkerPattern, 16)` | address-array scan | five engine marker-write sites | n/a (the hooked instruction reads `[rdi]` and `[rdi+8]`) | `src/game/teleport.cpp:525`, `:544-548`; consts `src/game/offsets.h:559-561` | `LOG_WARN("teleport: marker capture signature count mismatch (markers=%zu exp=%zu); direct reader will be used when available.")` `:546`; the install loop is skipped entirely (`:593`) |
| T11 | memory read (origin) | `kSig_MarkerOriginPrefix` = `"C5 F8 5C 05"` | `mem::FindAllMatches(kSig_MarkerOriginPrefix, 32)`, then per hit read `int32` at `hit+4` and compute `resolved = hit + 8 + displacement`; majority vote among candidates with ≥1 vote | address-array scan + manual RIP-relative decode (not `ResolveRipAt`) | the world-origin vector constant referenced by the marker math | final `mem::ReadVec3(g_markerOriginAddress, …)` = 3 floats at the resolved origin | `src/game/teleport.cpp:526`, `:549-584`, read at `:2117`; consts `src/game/offsets.h:563-565` | `origins.size() < 6` → `LOG_WARN("teleport: marker origin signature count mismatch (origins=%zu); marker teleport disabled.")` `:551` + `return false` `:553`; no candidate wins → `LOG_ERR("teleport: origin address could not be resolved from prefix matches.")` `:581` + `return false` |
| T12 | memory read (marker player actor) | `kSig_MarkerPlayer` = `"48 8B 06 C5 F8 11 88 B0 01 00 00"` | `mem::FindAllMatches(kSig_MarkerPlayer, 2)`, accepted only when `size()==1`; hook target is `players.front() + 3` | address-array scan | per-frame marker-player write site | the hooked 8 bytes are exactly `C5 F8 11 88 B0 01 00 00` = a VEX float store to `[rax+0x1B0]` (source XMM register as encoded; the stub captures RAX) | `src/game/teleport.cpp:524`, `:586-590`, `:436-459`; const `src/game/offsets.h:567-568` | hook failure → `LOG_WARN("teleport: marker player hook failed (fallback to move-owner).")` `:589`; `size()!=1` → hook silently not attempted |
| T13 | memory read (protection signature) | `kSig_MarkerProtection` = `"48 8B 46 08 48 89 F1"` | `mem::FindAllMatches(kSig_MarkerProtection, 2)`, accepted only when `size()==1` | address-array scan | **never used**: `InstallMarkerProtectionHook` returns `false` immediately | none | `src/game/teleport.cpp:527`, `:614-615`, `:461-468`; const `src/game/offsets.h:570-571` | the stub is a hard `return false` with the rationale comment at `:463-466` (target `0x14C4542E2` is an engine streaming/task-queue ring buffer, not player collision) |
| T14 | byte patch + inline hook code cave (×5) | each `kSig_MarkerPattern` match **+4** | `InstallMarkerHook(markers[i] + 4, g_markerCandidates[i])` | expects 7 bytes `C5 FB 11 02 8B 47 08`, allocates a 160-byte stub within ±`0x7FFF0000`, writes `E9 rel32` + `0x90` padding at the target; stub = `pushfq; push rcx; push r11; mov r11,&slot.writer; lock cmpxchg [r11],1; …; mov [abs],rax (xyBits); mov [abs],eax (zBits); mov [abs],1 (valid); lock incq [seq]; mov dword [writer],0; pop; original 7 bytes; jmp target+7`. Decoded callee contract of the replaced bytes: source Vec3 read from `[rdi]` (XY into XMM0) and `[rdi+8]` (Z in EAX), destination store to `[rdx]` / `[rdx+8]` | Trinity-owned `CandidateSlot` (never game memory) | stub reads game `[rdi]`, `[rdi+8]`; writes only Trinity globals | `src/game/teleport.cpp:357-434`, `:593-606`; slot layout `:136-143`; constants `:359` | per-index failure → `LOG_WARN("teleport: marker hook index %zu skipped (best-effort).")` `:603`; zero successes → `LOG_WARN("teleport: no marker hooks could be installed; using direct map destination when available.")` `:610` + `RemoveMarkerHooks()` `:611` |
| T15 | byte patch + inline hook code cave (×1) | each `kSig_MarkerPlayer` match **+3** | `InstallMarkerPlayerHook(players.front() + 3)` | expects 8 bytes `C5 F8 11 88 B0 01 00 00`, 128-byte stub: `mov [abs], rax` into `g_markerPlayer`, then the original 8 bytes; `jmp target+8` | Trinity-owned `g_markerPlayer` (Trinity's name for the pointer delivered in RAX at that site) | writes only Trinity global | `src/game/teleport.cpp:436-459`, `:586-590`; expected bytes `:438` | `LOG_WARN("teleport: marker player hook failed (fallback to move-owner).")` `:589` |
| T16 | disabled patch (documented, never applied) | historical protection target `0x14C4542E2` | hard-coded address in the comment only (`InstallMarkerProtectionHook` takes `uintptr_t /*target*/` and ignores it) | would have been `mov [rsi+8], [rsi+0x18]` per the comment — **no source line performs this write** | engine streaming/task-queue ring buffer | none at runtime | `src/game/teleport.cpp:461-468` | `return false` unconditionally `:467`; `g_markerProtectionReady` stays false (`:510`, `:615`) |
| T17 | memory write + read-back | marker destination application (game thread) | called from `hkMoveUpdate` after `oMoveUpdate` returns | helper signature `bool ApplyMarkerTeleportDestination(uintptr_t moveOwner, uintptr_t markerPlayer, const MapMarkerPosition&, void* context, MarkerTeleportWrite, MarkerTeleportRead)`; the write/read function pointers are `WriteTeleportPosition` / `ReadTeleportPosition` (`__try`-guarded raw `Vec3` store / `mem::ReadVec3`) | the movement owner object (+ optional marker-player proxy) | **writes**: `moveOwner+0x90` (`kOff_Player_Dest0`), `moveOwner+0x1A0` (`kOff_Player_Dest1`), `moveOwner+0xC0` = 0 (`kOff_MoveOwner_DesiredVel`), `moveOwner+0xD0` = 0 (`kOff_MoveOwner_Velocity`); if `markerPlayer && markerPlayer != moveOwner`: `markerPlayer+0x90`, `+0x1A0`; **reads back** `moveOwner+0x90` with tolerance `0.5f` | `src/game/marker_teleport_logic.h:25-57`; consts `src/game/offsets.h:573-574`, `:341`, `:332`; call `src/game/teleport.cpp:1594-1622`; write/read impls `:657-673` | returning `false` (or non-finite destination) aborts before any write (`src/game/marker_teleport_logic.h:30-33`); 3 consecutive failed attempts → `LOG_WARN("teleport: map marker write failed verification after 3 attempts.")` `src/game/teleport.cpp:1629` |
| T18 | memory read | marker origin vector during `TeleportToMarker` | `g_markerOriginAddress` (row T11) | `mem::ReadVec3(g_markerOriginAddress, &origin.x)` | world origin constant | 3 floats | `src/game/teleport.cpp:2116-2118` | `g_markerOriginAddress == 0` or non-finite → `return MarkerStatus::InvalidCoordinates` `:2118` |
| T19 | memory read (position publish) | `moveOwner + 0x90` inside `hkMoveUpdate` | same `moveOwner` argument the hook received | `ReadVec3(static_cast<uintptr_t>(moveOwner) + kOff_MoveOwner_Position, pos)` | movement owner | read `+0x90` (3 of 4 floats) | `src/game/teleport.cpp:1713-1720`; const `src/game/offsets.h:327` | read failure leaves the previous cached position and `g_posValid` unchanged |
| T20 | observer (read only, no call) | `g_markerDestinationGlobal` chain in the menu frame | row T9 | `ReadCurrentMapMarker(uiGlobal, &ReadMarkerPointer, &ReadMarkerPosition, out)` — `ReadMarkerPointer = bool(*)(uintptr_t, uintptr_t*)`, `ReadMarkerPosition = bool(*)(uintptr_t, float*)` | map UI state | read `+0` → `+0xA8` → `+0x20` | `src/game/teleport.cpp:701-702`; `src/game/map_marker.h:16-34` | any hop below `kMinPointer`, non-finite, `|v| > 1.0e9` or `{0,0,0}` → returns false; falls through to the capture-slot candidates (`src/game/teleport.cpp:709-732`) |
| T21 | memory read (readiness probe, install-time) | `kSig_MoveUpdate` presence | `trinity::mem::FindPattern(required[i])` in `GameplayCodeReady()` | presence only — no hook, no call | n/a | none | `src/core/mod.cpp:36-44`, `:70-73` | gate loops until all of `currentRequired`/`legacyRequired` match, up to 180 s (`src/core/mod.cpp:116-125`) |

### B.2 MOVEMENT/LOCOMOTION touch points (12 rows)

| Trinity role | Game function or address expression | Address obtained by | Exact ABI assumed | Object/structure operated on | Offsets read/written | Source | Fail-safe if locator missing |
|---|---|---|---|---|---|---|---|
| L1 | hook (MinHook inline detour) | locomotion sub-step driver (IDB `sub_2F49550`) — `kSig_LocoStepper_PE2944` / `kSig_LocoStepper` / `kSig_LocoStepper_Pre201` | `mem::InstallHook(locoHookName, locoSignature, "Super Run/Free Flight disabled", &hkLocoStep, &oLocoStep, &g_locoStepTarget)` | `void __fastcall hkLocoStep(uintptr_t comp /*rcx*/, float dt /*xmm1*/, float* vel /*r8*/, uint64_t a4 /*r9*/, uint64_t a5, a6, a7 /*stack*/)`; `dt` is declared as a real `float` parameter solely to keep XMM1 intact through the trampoline (`src/game/teleport.cpp:82-90`) | the movement component + the caller's scratch drive-velocity vector | reads `comp + MoveComponentOwnerOffsetForRevision(revision)` (`0x2C0` PE 2944 / `0x2B8` modern / `0x298` legacy / `0` unsupported); reads and writes `vel[0]`, `vel[+1]`, `vel[+2]` raw | `src/game/teleport.cpp:87-90`, `:1326-1328`, `:1905-1933`; sigs `src/game/offsets.h:401-412`; ABI notes `:382-399`; offset selector `src/core/version_mapping.cpp:30-46` | contract `Unsupported` → `LOG_ERR("teleport: locomotion-stepper contract unavailable for PE %u - Super Run/Free Flight disabled.")` `:1923` and `locoSignature` stays null; install failure → `LOG_ERR("teleport: locomotion-stepper hook NOT installed - Super Run/Free Flight disabled.")` `:1933`. **There is no `Teleport::` readiness accessor for locomotion**, so the PLAYER-tab toggles remain visible and settable while inert (`src/gui/menu.cpp:150-155` has no gating, unlike God Mode at `:88-90`) |
| L2 | memory write | Super Run ground scaling | inside `hkLocoStep` | `RawReadFloat` / `RawWriteFloat` (SEH-guarded raw float, **deliberately bypassing `mem::Read32/Write32`** because the drive vector lives near `0x013FDD70`, far below `kMinPointer`) | caller's drive-velocity vector (arg3) | writes `vel[0] *= st.superRunMult`, `vel[2] *= st.superRunMult`, clamped to `kMaxSafeGroundSpeed = 50.0f`; gated on `curHorizSpeed > 0.15f`, `!flyingNow`, `!st.menuOpen`, `!st.textCapture` | `src/game/teleport.cpp:1234-1252`, `:1539-1563` | scale is simply skipped when `st.superRunMult == 1.0f` (`:1539`); read failure skips the write (`:1542`) |
| L3 | memory write | Free Flight propulsion / hover / forced landing | inside `hkLocoStep` | same raw float helpers | caller's drive-velocity vector | writes: protection cushion `vel[1] = -45.0f` (`:1384-1387`); menu-open hover `vel[0]=0, vel[1]=±0.004, vel[2]=0` (`:1445-1447`); vertical `vel[1] = ±safeFlightSpeed` capped at `kMaxSafeVerticalSpeed = 35.0f` (`:1453-1463`); hover `vel[1]=±0.004` (`:1469`); horizontal scaling capped at `kMaxSafeFlightSpeed = 35.0f` (`:1481-1498`); no-input damping (`:1504-1505`); forced landing `vel = {0,-4,0}` (`:1527-1533`) | `src/game/teleport.cpp:1391-1534` | all writes are guarded by `vel != nullptr` and `isPlayer`; `RawWriteFloat` failure is silent |
| L4 | memory read (diagnostic) | movement-component pointer scan `comp + 0x200 … 0x600` step 8 | inside `hkLocoStep`, once per session (`static bool s_locoDiag`) | `ReadPtr` | movement component | reads `comp + o` for `o` in `[0x200, 0x600]` looking for `g_playerMoveOwner` | `src/game/teleport.cpp:1356-1375` | first-fire only; read failures are ignored |
| L5 | memory write | Super Jump desired-velocity scaling | `ApplyJumpScaling(owner)` called from `hkMoveUpdate` **before** `oMoveUpdate` | `mem::ReadVec3` + `WriteFloat` (which is `Write32` on the float's bit pattern) | movement owner | reads `moveOwner + 0xC0` (3 floats); gated on `v[kIdx_MoveOwner_Up] > kSuperJump_RiseThreshold (1.0f)`; writes the up component `moveOwner + 0xC0 + 4*1` | `src/game/teleport.cpp:1197-1222`, `:1586-1588`; consts `src/game/offsets.h:341-346` | `st.superJump == false` or `st.superJumpMult == 1.0f` → early return `:1213`; `ReadVec3` failure → early return `:1217` (note `mem::Write32` still enforces the `kMinPointer` floor here, unlike L2/L3) |
| L6 | observer / cross-subsystem tick | none (game-thread scheduling only) | inside `hkMoveUpdate` after the trampoline | `Player::Tick()` `:1641`, `World::Tick()` `:1645`, `Inventory::Tick()` `:1649`, `Dye::Tick()` `:1653`, `Equipment::Tick()` `:1657`, `Friendly::Tick()` `:1660`, `ServiceProtectionExpiry()` `:1663` | those subsystems' own objects | n/a here | `src/game/teleport.cpp:1637-1663` | if row T1 fails, **every** game-thread tick in this row is absent (mod.cpp ignores the install result) |
| L7 | input read (not game memory) | keyboard `GetAsyncKeyState` / `XINPUT_STATE` via `hooks::XInputReadReal` | `PollFlyInputs(const State&)` | `XInputReadReal(DWORD userIndex, XINPUT_STATE*)` returns `DWORD` (XInput error code), real pad bypassing menu neutralisation | n/a | reads `sThumbLX/LY` with deadzone `7849`, triggers `> 64` mapped to `kPadLTrigger 0x10000` / `kPadRTrigger 0x20000` | `src/game/teleport.cpp:1271-1324`; `src/hooks/xinput_hook.h:18-20`; sentinels `src/core/state.h:13-14` | a disabled/failed XInput hook just falls back to the plain export (`src/hooks/xinput_hook.h:19-20`) |
| L8 | memory write (locomotion-coupled, in `player.cpp`) | n/a — direct writes to stat entries | `PinEntry(uintptr_t e)` | `Write64(e + 0x08, full)`, `Write64(e + 0x20, full - base)` | 0x90-byte stat entry | writes `+0x08` current, `+0x20` normalized | `src/game/player.cpp:197-210` | `e < kMinPointer` → return `:199`; `base`/`cap` above `1000000000ULL` → return `:204`; no `full` → return `:207` |
| L9 | memory read | gameplay-character manager vector | `ResolveCharMgrGlobal()` — `mem::FindPattern(a.sig)` per `kCharMgrAnchors[]` + `mem::ResolveRipAt(m + a.movOff, 7)`, majority vote | address-only; no game call | manager → character vector | reads `*(g_charMgrGlobal)` → `*(p)` → `+0xB8` data / `+0xC0` count (sanity bound `8192`) | `src/game/player.cpp:65-103`, `:390-399`; anchors `src/game/offsets.h:229-264` | no anchor matches → `LOG_ERR("player: char-manager global NOT FOUND (no anchor matched) - God Mode / Infinite Stamina / Infinite Spirit limited to the current-character fallback.")` `:814-816`; `TickResolveSelf` then routes to the fallback `:385-389` |
| L10 | memory read | per-character vital chain | via row L9's vector | read-only walks | character ("owner") objects | reads `owner+0xA0` possessor, `owner+0x88` type descriptor (`+1` tag), `owner+0x68` actor, `actor+0x20` marker, `marker+0x18` root, `root+0x58` stat array, plus `owner+0x48` documented-but-unused | `src/game/player.cpp:222-255`, `:257-273`, `:401-513`; consts `src/game/offsets.h:43-45`, `:189`, `:285-303` | any hop below `kMinPointer` aborts that character's walk (returns false) |
| L11 | observer / locomotion coupling | none — state reads only | in `hkLocoStep` and `hkDamageApply` | `Teleport::IsProtected()` `src/game/teleport.cpp:2086-2090`; `Teleport::GetFlightEngaged()` `:1959-1962` | n/a | n/a | `src/game/player.cpp:325` (forces the character resolve while flying/protected) and `:762-782` (nullifies fall damage and zeroes stamina drain while flying) | if the loco hook never installed, `g_flightEngaged` is never set true (`:1523`), so these couplings stay inert |
| L12 | hook (revision-gated install; `player.cpp`) | n/a — hook installation only | `mem::InstallHook("player: stat-commit", kSig_StatCommit, …, …)` | MinHook detour; **skipped entirely** on `core::UsesTu201CompatibleRevision(revision)` | n/a | n/a | `src/game/player.cpp:821-830`; gate `src/core/version_mapping.cpp:22-28` | TU 2.01+ builds log `LOG_OK("player: modern continuous stat-pin guard active (all resolved characters).")` `:823` and rely on per-frame pins |

### B.3 Remaining `player.cpp` game-side touch points (5 rows — combat/stats, not locomotion)

Listed for completeness because `player.cpp` is in this area's file list; none of these is a
movement/locomotion consumer.

| Trinity role | Game function | Address obtained by | ABI | Object | Offsets | Source | Fail-safe |
|---|---|---|---|---|---|---|---|
| P1 | hook (MinHook) | `pa_StatCommit` (IDB `sub_BED7820`) — `kSig_StatCommit` = `"48 89 5C 24 10 55 56 57 48 83 EC 20 48 8B 59 18 41 0F B7 E9 48 03 59 20 48 89 D6 48 89 CF 4C 39 C3"` | `mem::InstallHook`, `consequence` string is `"direct write guard unavailable; current-character pins remain active"` | `int64_t __fastcall(void* entry /*rcx*/, int64_t time /*rdx*/, int64_t target /*r8*/, uint16_t flag /*r9w*/)` | stat entry | reads/writes `+0x18` base, `+0x30` cap, `+0x08` current, then `PinEntry` writes `+0x08`/`+0x20` | `src/game/player.cpp:141-146`, `:575-611`, `:827-829`; sig `src/game/offsets.h:145-146` | `LOG_ERR("player: stat-commit signature NOT FOUND - direct write guard unavailable; current-character pins remain active.")` via `src/mem/hooks.h:35` |
| P2 | hook (MinHook, primary + alt) | `pa_StatApplyDelta` (IDB `sub_145B2A0`) — `kSig_DamageApply` then `kSig_DamageApply_Alt` | `mem::InstallHook`; the **primary** call passes `consequence = ""`, which suppresses the NOT-FOUND error (`src/mem/hooks.h:33-36`) | `int64_t __fastcall(void* targetOwner /*rcx*/, uint16_t statusId /*rdx*/, int64_t time /*r8*/, int64_t delta /*r9*/, uintptr_t sourceCtx /*stack*/, char a6..a10, void* out)` | victim vital-owner + attacker context | reads `sourceCtx+0x68` actor, `sourceCtx+0x20` marker, `sourceCtx+0xA0` possessor, `sourceCtx + kOff_Container_Sub` | `src/game/player.cpp:148-157`, `:653-792`, `:833-849`; sigs `src/game/offsets.h:167-170` | both fail → `LOG_ERR("player: damage-apply signature NOT FOUND (tried primary + alt) - infinite stamina drain block disabled.")` `:843` |
| P3 | hook (MinHook) | combat timing/hitbox evaluator `sub_1407219c0` — `kSig_CombatTimingEval` | `mem::InstallHook` | `bool __fastcall(void* combatComp /*rcx*/, void* hitData /*rdx*/, float distance /*xmm2*/, uint8_t isGuardMode /*r9b*/, void* outResult)` | combat component | none | `src/game/player.cpp:794-805`, `:852-857`; sig `src/game/offsets.h:175-176` | `LOG_ERR("player: combat-timing signature NOT FOUND - Easy Parry & Easy Evade helper timing disabled.")` via `src/mem/hooks.h:35` |
| P4 | observer | current-character fallback | `Inventory::ClientCharacterAddr()` | external (inventory subsystem) | live client character | walked by `WalkSelfChain` | `src/game/player.cpp:339-350` | owner below `kMinPointer` or chain failure → `ClearPlayerSets()` and return `:341-346` |
| P5 | observer | engine local-player accessor `sub_2393AA0` class gate (documented, not hooked) | n/a — replicated as a predicate | `IsPlayerClass` reproduces `((tag - 1) & 0xF7) == 0` | type descriptor | reads `owner+0x88` → `+1` tag byte | `src/game/player.cpp:212-228`; doc `src/game/offsets.h:273-294` | not applicable (pure read) |

**Game-side touch-point count: 21 (travel) + 12 (locomotion) + 5 (player.cpp combat/stats) = 38.**

---

## C. Hook table

| # | Hook name (log context) | Target address source | Hook type | Callback signature | What it does | Removal / restore | Source |
|---|---|---|---|---|---|---|---|
| H1 | `"teleport: movement-update"` (`teleport: movement-update hook installed @ %p`) | `kSig_MoveUpdate` → `FindPattern` (`src/game/offsets.h:325-326`) | MinHook inline detour (`MH_CreateHook` + `MH_EnableHook`) | `uint64_t __fastcall hkMoveUpdate(uint64_t moveOwner, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)` | publishes `g_playerMoveOwner`; `ApplyJumpScaling(owner)`; calls the trampoline; applies a queued marker teleport with read-back; runs `Player::Tick`, `World::Tick`, `Inventory::Tick`, `Dye::Tick`, `Equipment::Tick`, `Friendly::Tick`, `ServiceProtectionExpiry`; fires a queued native fast travel; builds the fast-travel catalog; publishes live position from `moveOwner+0x90` | `mem::RemoveHook(&g_moveUpdateTarget)` → `MH_DisableHook` + `MH_RemoveHook` (`src/mem/hooks.h:70-76`), called from `Teleport::Remove()` `src/game/teleport.cpp:1946` ← `Mod::Shutdown` `src/core/mod.cpp:158` | `src/game/teleport.cpp:1575-1722`, install `:1839-1842`, remove `:1941-1948` |
| H2 | `"teleport: locomotion-stepper"` / `"teleport: locomotion-stepper (PE 2944)"` / `"teleport: locomotion-stepper (pre-2.01)"` | `core::LocoStepperContractForRevision(revision)` → `kSig_LocoStepper_PE2944` / `kSig_LocoStepper` / `kSig_LocoStepper_Pre201` | MinHook inline detour | `void __fastcall hkLocoStep(uintptr_t comp, float dt, float* vel, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)` | identifies the local player via `comp + MoveComponentOwnerOffsetForRevision`; teleport landing cushion (dead — see §E notes); Free Flight propulsion/hover/forced-landing; Super Run ground scaling; publishes `g_flightEngaged`; calls the trampoline inside `__try/__except` | `mem::RemoveHook(&g_locoStepTarget)` `src/game/teleport.cpp:1945` ← `Teleport::Remove()` ← `Mod::Shutdown` | `src/game/teleport.cpp:1326-1573`, install `:1903-1933`, remove `:1945` |
| H3 | (no name; `LOG_WARN("teleport: marker hook index %zu skipped (best-effort).")`) ×5 | each `kSig_MarkerPattern` match **+4**, 7 bytes | hand-written inline hook: `E9 rel32` + `0x90` fill at the target, stub allocated by `AllocateNear(target, 160)` (`kMaxDistance = 0x7FFF0000`) | raw code cave — no C++ signature; the stub preserves `rax`, `rcx`, `r11`, `rflags`, claims `slot.writer` with `lock cmpxchg`, copies `[rdi]`/`[rdi+8]` into the slot, bumps `slot.seq`, replays the original 7 bytes, jumps back to `target+7`. A busy slot replays the original instruction immediately (the `jne busyReplay` path) | captures each engine marker-position write into a Trinity `CandidateSlot` | `RemoveMarkerHooks()` `src/game/teleport.cpp:470-495`: suspends other threads, verifies `target[0] == 0xE9` and `target + 5 + rel32 == stub`, copies `hook.original` back, `FlushInstructionCache`; called from `Teleport::Remove()` `:1943` | `src/game/teleport.cpp:357-434`, `:593-606`; removal `:470-495` |
| H4 | (no name; `LOG_WARN("teleport: marker player hook failed (fallback to move-owner).")`) ×1 | `kSig_MarkerPlayer` match **+3**, 8 bytes | hand-written inline hook: `E9 rel32` at the target, stub from `AllocateNear(target, 128)` | raw code cave: `mov [abs], rax` into `g_markerPlayer`, then the original 8 bytes, then `jmp target+8` | caches the marker-specific player proxy pointer | restored by the same `RemoveMarkerHooks()` loop (`g_markerHooks` holds both kinds) | `src/game/teleport.cpp:436-459`, `:586-590` |
| H5 | (never installed) marker protection hook | `kSig_MarkerProtection` match, accepted only when `FindAllMatches(..., 2).size() == 1` | would have been an inline hook write | `bool InstallMarkerProtectionHook(uintptr_t /*target*/)` — parameter unused | **nothing.** Body is `return false;` with the rationale that overwriting `[rsi+8]` with `[rsi+0x18]` at `0x14C4542E2` corrupts an engine streaming/task-queue ring buffer and caused crashes in `SceneObjectServer@pa` (`0x14040BCCC`) | not installed, so nothing to restore; `g_markerProtectionReady` remains `false` (`:510`, `:615`) | `src/game/teleport.cpp:461-468`, `:614-615` |
| H6 | `"player: stat-commit"` | `kSig_StatCommit` | MinHook inline detour — **skipped** when `core::UsesTu201CompatibleRevision(revision)` is true | `int64_t __fastcall hkStatCommit(void* entry, int64_t time, int64_t target, uint16_t flag)` | for a tracked player-HP / stamina / mount-stamina / spirit entry, substitutes `target = full` before the commit and re-pins after; returns `fullTarget` instead of the engine result when locked | `mem::RemoveHook(&g_commitTarget)` `src/game/player.cpp:909` ← `Player::Remove()` ← `Mod::Shutdown` `src/core/mod.cpp:157` | `src/game/player.cpp:575-611`, install `:821-830`, remove `:907-933` |
| H7 | `"player: damage-apply"` then `"player: damage-apply (alt)"` | `kSig_DamageApply` → fallback `kSig_DamageApply_Alt` | MinHook inline detour | `int64_t __fastcall hkDamageApply(void* targetOwner, uint16_t statusId, int64_t time, int64_t delta, uintptr_t sourceCtx, char a6, char a7, char a8, char a9, char a10, void* out)` | zeroes/blocks player & mount damage (God Mode, no-fall-damage, flight, protection), zeroes stamina/spirit drain, applies incoming/outgoing multipliers, then calls the trampoline with the (possibly rewritten) `delta` | `mem::RemoveHook(&g_damageHookTarget)` `src/game/player.cpp:910` | `src/game/player.cpp:726-792`, install `:832-849` |
| H8 | `"player: combat-timing"` | `kSig_CombatTimingEval` | MinHook inline detour | `bool __fastcall hkCombatTimingEval(void* combatComp, void* hitData, float distance, uint8_t isGuardMode, void* outResult)` | currently a pure pass-through: it calls the original and returns `orig` unchanged (`src/game/player.cpp:799-805`) | `mem::RemoveHook(&g_combatTimingTarget)` `src/game/player.cpp:911` | `src/game/player.cpp:794-805`, install `:851-857` |

---

## D. Patch table

All byte patches live in the `teleport.cpp` marker subsystem. There are **no** patches in
`player.cpp` (its only game-memory writes are the `PinEntry` stat writes, rows L8/P1).

| # | Target locator | Original bytes | Patched bytes | When applied | How restored | Safety condition | Source |
|---|---|---|---|---|---|---|---|
| D1–D5 | `kSig_MarkerPattern` matches **+4** (up to `kExpected_MarkerMatches = 5`), 7-byte span | `C5 FB 11 02 8B 47 08` (`vmovss [rdx],xmm0` + `mov eax,[rdi+8]`) | `E9 <rel32>` to the stub, then `0x90` for the remaining bytes (`std::fill(target + 5, target + hook.length, 0x90)`) | `Teleport::Install()` → `InitMarkerSubsystem()` → `InstallMarkerHook(markers[i] + 4, g_markerCandidates[i])` | `RemoveMarkerHooks()` copies `hook.original` back and flushes the instruction cache | bytes at the target are first compared against `kExpected` (`memcmp != 0` → `return false`, no write); the stub address must be within `int32` range of `target+5`; other threads are suspended and any thread whose RIP is inside `[target, target+length)` forces a retry (up to 64 attempts, `ThreadSuspender::SuspendOthersAvoiding`) | patch `src/game/teleport.cpp:359-362`, `:324-355`, byte emit `:347-351`; expected `:359`; install `:593-606`; restore `:470-495` |
| D6 | `kSig_MarkerPlayer` match **+3**, 8-byte span | `C5 F8 11 88 B0 01 00 00` (`vmovss [rax+0x1B0],xmm1`) | `E9 <rel32>` to the stub; no NOP fill is needed because the span is exactly 5 + 3 replayed bytes inside the stub (the stub re-emits the original 8 bytes and jumps to `target+8`) | `InitMarkerSubsystem()` → `InstallMarkerPlayerHook(players.front() + 3)` | same `RemoveMarkerHooks()` path | identical `kExpected` `memcmp` gate (`:440-441`), stub range check, thread suspension | `src/game/teleport.cpp:436-459`; expected `:438` |
| D7 | historical protection target `0x14C4542E2` (comment only) | not recorded in source | would have been `mov [rsi+8], [rsi+0x18]` per the comment at `:464` | **never applied** — `InstallMarkerProtectionHook` returns `false` before touching memory | n/a | hard-coded `return false`; the comment states the target is an engine streaming/task-queue ring buffer and the write caused heap-pool corruption / crashes in `SceneObjectServer@pa` (`0x14040BCCC`) | `src/game/teleport.cpp:461-468` |

Adjacent, non-code write: `hkMoveUpdate` zeroes `moveOwner+0xC0` and `moveOwner+0xD0` as part of
the teleport transaction (row T17) — a data write, not a byte patch.

---

## E. Fail-closed / version-gated behaviour

### E.1 Version gates

| Gate | Condition | Effect | Log | Source |
|---|---|---|---|---|
| Locomotion ABI selection | `LocoStepperContractForRevision(revision)`: `2944 → Pe2944`, `2760/2850 → Modern`, `2625/2658/2692 → Legacy`, **default → `Unsupported`** | `Unsupported` leaves `locoSignature == nullptr`, so **no hook** is installed and Super Run / Free Flight / Super Jump are all inert | `LOG_ERR("teleport: locomotion-stepper contract unavailable for PE %u - Super Run/Free Flight disabled.", static_cast<unsigned>(revision))` | `src/core/version_mapping.cpp:48-60`, `src/game/teleport.cpp:1905-1926` |
| Loco hook install failure | `mem::InstallHook` returns false **or** `g_locoStepTarget` is null | same as above | `LOG_ERR("teleport: locomotion-stepper hook NOT installed - Super Run/Free Flight disabled.")` | `src/game/teleport.cpp:1928-1933` |
| Move-component owner offset | `MoveComponentOwnerOffsetForRevision`: PE 2944 `0x2C0`, Modern `0x2B8`, Legacy `0x298`, `Unsupported → 0` | `0` would make `ReadPtr(comp + 0)` read the vtable instead of the owner, so `isPlayer` never matches (moot in practice because the hook is not installed at all on `Unsupported`) | none | `src/core/version_mapping.cpp:30-46`, consumer `src/game/teleport.cpp:1350-1353` |
| Fast travel, PE 2944 | `revision == 2944` and **either** `kSig_TravelToNode_PE2944` or `kSig_TravelDispatcher_PE2944` fails to resolve | `travel` stays 0 → `TravelToNode` returns false at `:2049` | `LOG_ERR("teleport: PE 2944 native Fast Travel contract incomplete (selection=%p dispatcher=%p) - menu disabled.", selection, dispatcher)` | `src/game/teleport.cpp:1849-1867` |
| Fast travel, all other revisions | all three of `kSig_TravelToNode`, `_Pre201`, `_Legacy` fail | same | `LOG_ERR("teleport: fast-travel trigger signature NOT FOUND - fast-travel menu disabled.")` (`else if (revision != 2944)` at `:1883` means PE 2944 does **not** emit this second message) | `src/game/teleport.cpp:1868-1886` |
| Stat-commit hook | `core::UsesTu201CompatibleRevision(revision)` (`2760`, `2850`, `2944`) | the `kSig_StatCommit` hook is **not installed**; per-frame `PinEntry` pins are the guard | `LOG_OK("player: modern continuous stat-pin guard active (all resolved characters).")` | `src/core/version_mapping.cpp:22-28`, `src/game/player.cpp:821-824` |
| Damage-apply | primary `kSig_DamageApply` fails → try `kSig_DamageApply_Alt`; both fail | damage multipliers and the stamina-drain block are disabled | `LOG_ERR("player: damage-apply signature NOT FOUND (tried primary + alt) - infinite stamina drain block disabled.")` | `src/game/player.cpp:832-845` |

### E.2 Travel / marker fail-closed conditions

| Condition (exact source) | Result | Log |
|---|---|---|
| `origins.size() < 6` — `src/game/teleport.cpp:549` | `InitMarkerSubsystem` returns `false` (`:553`); marker teleport entirely disabled | `LOG_WARN("teleport: marker origin signature count mismatch (origins=%zu); marker teleport disabled.", origins.size())` `:551` |
| `origin == 0` after the vote — `:579` | returns `false` (`:582`) | `LOG_ERR("teleport: origin address could not be resolved from prefix matches.")` `:581` |
| `destinationRefs.size() != 1` — `:531` | `g_markerDestinationGlobal` stays 0; the direct reader is unavailable and legacy capture hooks become mandatory | `LOG_WARN("teleport: direct map-destination reference not found; legacy capture hooks are required.")` `:541` |
| `markers.size() != kExpected_MarkerMatches (5)` — `:544` | the capture-hook install loop is skipped (`:593`) | `LOG_WARN("teleport: marker capture signature count mismatch (markers=%zu exp=%zu); direct reader will be used when available.", markers.size(), kExpected_MarkerMatches)` `:546` |
| an individual `InstallMarkerHook` fails — `:597-604` | that index is skipped, others continue | `LOG_WARN("teleport: marker hook index %zu skipped (best-effort).", i)` `:603` |
| `installedHooks == 0` — `:608` | `RemoveMarkerHooks()` is called (`:611`) | `LOG_WARN("teleport: no marker hooks could be installed; using direct map destination when available.")` `:610` |
| `!MarkerTeleportCanUseDestination(hasDirectDestination, origin >= kMinPointer, installedHooks)` — `:617-619`; the predicate is `hasOrigin && (hasDirectDestination \|\| installedHooks != 0)` (`src/game/marker_teleport_logic.h:18-23`) | `g_markerReady = false`; returns `false` | `LOG_WARN("teleport: no usable marker source with a verified origin; marker teleport disabled.")` `:622` |
| `!g_markerReady` — `:2102` | `TeleportToMarker` returns `MarkerStatus::NotReady` | none |
| `moveOwner < kMinPointer && markerPlayer < kMinPointer` — `:2107` | returns `MarkerStatus::NoPlayer` | none |
| `g_pendingMarkerTp` already set — `:2109` (and `TeleportToCoordinates` `:2178`) | returns `MarkerStatus::UnsafeContext` / `false` | none |
| `!FindActiveMarker(marker)` — `:2113` | returns `MarkerStatus::NoMarker` | none |
| `g_markerOriginAddress == 0` or `!mem::ReadVec3(...)` or `!FiniteCoordinate(origin)` — `:2117` | returns `MarkerStatus::InvalidCoordinates` | none |
| `!FiniteCoordinate(destination)` — `:2147` (`FiniteCoordinate` rejects non-finite and `|v| > kMarker_CoordLimit = 1.0e9f`, `:632-640`) | returns `MarkerStatus::InvalidCoordinates` | none |
| destination write fails read-back 3× — `:1623` | the pending transaction is abandoned | `LOG_WARN("teleport: map marker write failed verification after 3 attempts.")` `:1629` |
| `!g_registryGlobal \|\| !g_sceneResolver` — `:1997` | `LoadCatalog()` returns false (never requests a build) | none (the missing-resolver case already logged at `:1889`) |
| `cats.empty()` after the catalog pass — `:1185` | `BuildCatalogGameThread` returns false; the request is cleared and re-armed by the menu | none |
| `!ResolveTableResolver(kStr_GimmickSceneTable, …)` — `:1888` | no catalog, no fast travel | `LOG_ERR("teleport: scene-registry resolver NOT FOUND - fast-travel menu disabled.")` `:1889` |
| `!ResolveTableResolver(kStr_LevelNameTable, …)` and both fallbacks fail — `:1892-1901` | waypoint names degrade to `#<index>` (`:1169-1174`) | `LOG_WARN("teleport: area-name resolver not found - waypoint names fall back to indices.")` `:1898` |
| `hkMoveUpdate`'s `__try/__except` around the native travel call — `:1674-1691` | the exception is swallowed; the game thread continues | `LOG_ERR("teleport: native fast-travel raised an exception (scene=%d node=%u).", …)` `:1689` |

### E.3 Notes on fail-closed behaviour that are *not* fail-closed (findings)

1. **Landing/fall protection is permanently inert.** `Teleport::ActivateProtection` is a hard
   no-op that stores 0 into both `g_markerProtectFlag` and `g_markerProtectDeadline`
   (`src/game/teleport.cpp:2092-2098`), and `Teleport::IsProtected()` returns
   `deadline != 0 && GetTickCount64() < deadline` (`:2086-2090`). Every writer of
   `g_markerProtectDeadline` writes 0 (`:503`, `:511`, `:2097`), so `IsProtected()` is always
   false. Consequently: the "Automatic Safe Landing cushion" branch in `hkLocoStep`
   (`:1377-1389`) is dead code, and `Teleport::ActivateProtection(120000)` after a successful
   marker teleport (`:1609`) does nothing. The `noFallDamage` default of `true`
   (`src/core/state.h:48`) is what actually protects the player, via `hkDamageApply`
   (`src/game/player.cpp:762-767`).
2. **`Teleport::Install`'s failure is ignored.** `src/core/mod.cpp:130` discards the bool, and
   `Teleport::Install` returns `false` only when the movement hook fails (`:1839-1841`).
3. **No menu gating for locomotion.** Unlike God Mode (`src/gui/menu.cpp:88-90`, which
   substitutes a "load into the game world first" description when `Player::Ready()` is false),
   Super Run / Super Jump / Free Flight and the fly keybinds have no readiness check
   (`src/gui/menu.cpp:150-155`, `:2839-2842`), so an unsupported revision presents working-looking
   toggles that do nothing.
4. **`Teleport::Remove()` leaves locator/catalog state behind.** It clears the marker hooks and
   the two MinHook targets plus `g_posValid` (`src/game/teleport.cpp:1941-1948`) but does **not**
   reset `g_travelFn`, `g_sceneResolver`, `g_registryGlobal`, `g_lvlResolver`,
   `g_lvlRegistryGlobal`, `g_categories`, `g_catalogReady`, `g_catalogRequested`,
   `g_markerDestinationGlobal`, `g_markerOriginAddress` or `g_markerReady`'s inputs.
   `g_markerReady` itself is explicitly cleared (`:1944`), and `InitMarkerSubsystem` re-zeroes
   the marker globals on the next install (`:507-522`), but a re-install after `Remove()` would
   keep the previous build's catalog (`g_catalogReady` still true).

---

## F. Runtime validation hooks (exact log lines)

All Trinity log output goes through the macros in `src/core/logger.h:285-292`
(`LOG`, `LOG_DEBUG`, `LOG_OK`, `LOG_WARN`, `LOG_ERR`, `LOG_ONCE`, `LOG_WARN_ONCE`,
`LOG_THROTTLE`) and is written to `Trinity.log` next to the ASI module
(`src/core/logger.h:36`, `:237`). Lines are deduplicated if the *same* message repeats within
3 s of the identical previous message (`src/core/logger.h:167-173`), which matters when
comparing a static list against a log file.

### F.1 Travel — install time

```
LOG_OK  "teleport: movement-update hook installed @ %p"                                        src/game/teleport.cpp:1842
LOG_OK  "teleport: PE 2944 selection @ %p, confirmation dispatcher @ %p."                      src/game/teleport.cpp:1859
LOG_ERR "teleport: PE 2944 native Fast Travel contract incomplete (selection=%p dispatcher=%p) - menu disabled."   src/game/teleport.cpp:1864
LOG_OK  "teleport: native fast-travel trigger resolved @ %p%s"                                 src/game/teleport.cpp:1880
LOG_ERR "teleport: fast-travel trigger signature NOT FOUND - fast-travel menu disabled."       src/game/teleport.cpp:1885
LOG_ERR "teleport: scene-registry resolver NOT FOUND - fast-travel menu disabled."             src/game/teleport.cpp:1889
LOG_WARN "teleport: area-name resolver not found - waypoint names fall back to indices."       src/game/teleport.cpp:1898
LOG_WARN "teleport: direct map-destination reference not found; legacy capture hooks are required."                src/game/teleport.cpp:541
LOG_WARN "teleport: marker capture signature count mismatch (markers=%zu exp=%zu); direct reader will be used when available."  src/game/teleport.cpp:546
LOG_WARN "teleport: marker origin signature count mismatch (origins=%zu); marker teleport disabled."               src/game/teleport.cpp:551
LOG_ERR  "teleport: origin address could not be resolved from prefix matches."                 src/game/teleport.cpp:581
LOG_WARN "teleport: marker player hook failed (fallback to move-owner)."                       src/game/teleport.cpp:589
LOG_WARN "teleport: marker hook index %zu skipped (best-effort)."                              src/game/teleport.cpp:603
LOG_WARN "teleport: no marker hooks could be installed; using direct map destination when available."              src/game/teleport.cpp:610
LOG_WARN "teleport: no usable marker source with a verified origin; marker teleport disabled." src/game/teleport.cpp:622
LOG_OK  "teleport: map marker teleport subsystem initialized (origin=0x%p, direct=%s, hooks=%zu/%zu, protection=%s)."  src/game/teleport.cpp:626
```

Hook-installer generic lines that also appear for these features (`context` = the first
argument, `consequence` = the third):

```
LOG_ERR  "%s signature NOT FOUND - %s."                    src/mem/hooks.h:35
LOG_WARN "%s signature ambiguous (%zu); hooking first."    src/mem/hooks.h:41
LOG_ERR  "%s: MH_CreateHook failed (%s) - %s."             src/mem/hooks.h:48
LOG_ERR  "%s: MH_EnableHook failed (%s) - %s."             src/mem/hooks.h:57
```

Concrete instantiations: `"teleport: movement-update signature NOT FOUND - position tracking disabled."`
(`src/game/teleport.cpp:1839`), `"teleport: locomotion-stepper signature NOT FOUND - Super Run/Free Flight disabled."`
and `"teleport: locomotion-stepper (PE 2944) …"` / `"teleport: locomotion-stepper (pre-2.01) …"`
(`:1907-1928`).

### F.2 Travel — runtime

```
LOG_DEBUG "teleport: native fast-travel dispatch scene=%d node=%u context=%p."                 src/game/teleport.cpp:1676
LOG_OK    "teleport: native fast-travel accepted scene=%d node=%u."                           src/game/teleport.cpp:1681
LOG_WARN  "teleport: native fast-travel refused scene=%d node=%u."                            src/game/teleport.cpp:1684
LOG_ERR   "teleport: native fast-travel raised an exception (scene=%d node=%u)."               src/game/teleport.cpp:1689
LOG       "teleport: map marker queued for (%.2f, %.2f, %.2f)."                                src/game/teleport.cpp:2159
LOG_OK    "teleport: map marker applied and verified at (%.2f, %.2f, %.2f)."                   src/game/teleport.cpp:1614
LOG_WARN  "teleport: map marker write failed verification after 3 attempts."                   src/game/teleport.cpp:1629
```

The user-visible toasts (not log lines) that pair with the async marker result:
`LOC("Teleported to destination")` / `LOC("Destination teleport failed")`
(`src/hooks/dx12_hook.cpp:957-959`), and the status-specific toasts
`"No destination found on map"`, `"Player not ready"`, `"Invalid destination coordinates"`,
`"Unsafe destination context"`, `"Destination teleport failed"` (`src/hooks/dx12_hook.cpp:996-1009`,
mirrored at `src/gui/menu.cpp:1192-1205`). Fast-travel success toast `"Warping to %s"` at
`src/gui/menu.cpp:1351`.

### F.3 Locomotion

```
LOG_ERR "teleport: locomotion-stepper contract unavailable for PE %u - Super Run/Free Flight disabled."  src/game/teleport.cpp:1923
LOG_OK  "teleport: locomotion-stepper hook installed @ %p"                                     src/game/teleport.cpp:1931
LOG_ERR "teleport: locomotion-stepper hook NOT installed - Super Run/Free Flight disabled."    src/game/teleport.cpp:1933
LOG     "teleport: loco diag comp=%p vel=%p player=%p isPlayer=%d moveOwnerOffsetFound=0x%llX." src/game/teleport.cpp:1371
```

`F.3` is exhaustive for locomotion: there are **no** per-frame, per-write or per-toggle log
lines in `hkLocoStep`, `ApplyJumpScaling`, `PollFlyInputs` or `Teleport::GetFlightEngaged`.
The only runtime-observable locomotion signals are the one-shot `loco diag` line and the HUD
`"FLY"` suffix (`src/gui/menu.cpp:58`). This is the main gap for a static↔runtime comparison:
speed/jump/flight activity cannot be confirmed from the log alone.

### F.4 `player.cpp` (combat/stats; includes the locomotion couplings)

```
LOG_WARN "player: char-manager anchors DISAGREE (%d distinct values); using %p with %d/%d votes - re-derive the anchors."  src/game/player.cpp:96-98
LOG      "player: char-manager successfully resolved (%d anchors verified)."                   src/game/player.cpp:100
LOG_OK   "player: current-character fallback resolved @ %p (active player only)."              src/game/player.cpp:349
LOG_ERR  "player: char-manager global NOT FOUND (no anchor matched) - God Mode / Infinite Stamina / Infinite Spirit limited to the current-character fallback."  src/game/player.cpp:814-815
LOG_OK   "player: modern continuous stat-pin guard active (all resolved characters)."          src/game/player.cpp:823
LOG_OK   "player: damage-apply hook installed @ %p"                                            src/game/player.cpp:839 and :848
LOG_ERR  "player: damage-apply signature NOT FOUND (tried primary + alt) - infinite stamina drain block disabled."  src/game/player.cpp:843
LOG_OK   "player: combat-timing hook installed @ %p"                                           src/game/player.cpp:856
```

Plus the generic `"player: stat-commit signature NOT FOUND - direct write guard unavailable; current-character pins remain active."`
(`src/game/player.cpp:827-829` + `src/mem/hooks.h:35`) and
`"player: combat-timing signature NOT FOUND - Easy Parry & Easy Evade helper timing disabled."`
(`src/game/player.cpp:852-853`).

Other game-thread work dispatched from `hkMoveUpdate` logs under its own subsystems'
contexts (not reproduced here): `Player::Tick`, `World::Tick`, `Inventory::Tick`, `Dye::Tick`,
`Equipment::Tick`, `Friendly::Tick`.

---

## G. Symbols referenced but not defined / unresolved in the assigned scope

1. `kOff_Container_Sub` — used at `src/game/player.cpp:749` (`ReadPtr(sourceCtx + kOff_Container_Sub, &sub)`);
   defined at `src/game/offsets.h:748` as `0x68` (`// container+0x68 -> sub-object`). It is outside the
   assigned file set but is used by `equipment.cpp`, `dye.cpp` and `inventory.cpp` too.
2. `kExpected_OriginMatches = 9` (`src/game/offsets.h:565`) — **declared but never referenced**
   by `teleport.cpp`, which instead hard-codes the threshold `origins.size() < 6`
   (`src/game/teleport.cpp:549`).
3. `kOff_Registry_SceneTable = 0x50` (`src/game/offsets.h:488`) — **declared but never referenced**
   by `teleport.cpp`; the code always goes through the resolver instead (documented rationale at
   `src/game/offsets.h:472-475`).
4. `kSig_TableResolverPrologue` (`src/game/offsets.h:534-535`) — **declared but not used**;
   `FindResolverPrologueAbove` embeds the same bytes as local `kPrologue32`/`kPrologue16`/
   `kPrologueShort` arrays (`src/game/teleport.cpp:1744-1757`).
5. `kExpected_MarkerMatches` is used (`:544`, `:593`), but `kSig_MarkerProtection` resolves to
   the disabled hook H5 and `kSig_MarkerOriginPrefix`'s expected count constant is unused (item 2).
6. `InstallMarkerProtectionHook`'s `target` parameter is unnamed in the definition
   (`uintptr_t /*target*/`, `src/game/teleport.cpp:461`), so the historical address
   `0x14C4542E2` exists only in the comment at `:463`.
7. `Player::DumpCharacters()` is declared (`src/game/player.h:69`) but **has no definition in
   `player.cpp`** — the file contains no such function. Presumably defined elsewhere or dead
   declaration; not part of travel/locomotion.
8. `oLocoStep`'s `dt` parameter: `hkLocoStep` never reads `dt`; it exists only so the XMM1 value
   survives the trampoline (comment `src/game/teleport.cpp:84-86`).
9. `sub_505140` / `sub_5019D0` / `qword_6185008` / `qword_619E708` / `sub_396CC0` / `sub_B7B6850`
   / `sub_B58680` / `sub_2F49550` / `sub_2F4A720` / `sub_2F4DE00` / `sub_3A3E140` /
   `sub_145B2A0` / `sub_BED7820` / `sub_2393AA0` / `sub_1406550B0` / `sub_140654ED0` are **IDB
   names in comments only** — no runtime code references them; every runtime address comes from
   a signature or registry scan.

## H. Uncertainties

1. `kSig_LocoStepper` (`src/game/offsets.h:407-409`) and `kSig_LocoStepper_PE2944`
   (`:401-403`) are **byte-identical** strings, and both differ from `kSig_LocoStepper_Pre201`
   (`:410-412`) only in the `push rcx`/frame-size bytes (`48 89 48 08` vs absent, `78 F8`/`50 08`
   vs `68 F8`/`60 08`). The PE-2944-vs-Modern distinction is therefore a *version-selector and
   naming* distinction, not a signature distinction, despite the comment at `:405-406`
   ("Do not use it as a PE 2944 fallback").
2. `kOff_Node_Gimmick = 0x10` is explicitly marked UNVERIFIED for PE 2944
   (`src/game/offsets.h:492`), and `ReadNodeLabel` (`src/game/teleport.cpp:763-771`) treats it as
   a pointer to an engine string. POI (non-`_useTeleport`) node labels can therefore be wrong or
   empty and fall back to `#<index>` (`:1169-1174`). Not verified at runtime here.
3. The `MoveComponentOwnerOffsetForRevision` value used for player identification in
   `hkLocoStep` is derived from a *one-shot diagnostic log* (`:1356-1375`), not from a validated
   chain; if `isPlayer` fails to match, Free Flight and Super Run silently do nothing (only
   observable via the `loco diag` line's `isPlayer=` field).
4. `hkMoveUpdate` treats RCX as a `uint64_t` "moveOwner" pointer and its return type as
   `uint64_t`. The hook is declared with 7 integer parameters; `kSig_MoveUpdate`'s prologue shows
   `mov rax,rsp; mov [rax+20h],r9; mov [rax+10h],rdx; push rbp; push r14`
   (`src/game/offsets.h:323-324`), consistent with 4 register args + stack args, but Trinity does
   not read a2–a7 and cannot detect an ABI drift beyond re-matching the prologue bytes.
5. Marker candidate selection (`FindActiveMarker`, `:696-733`) picks the highest `seq` among
   slots whose `valid` flag is set; if a marker slot was captured once and the marker was then
   removed in game, the stale value can still win when `g_markerDestinationGlobal` is
   unavailable. `ClearMarker()` clears the slots but is only called on a successful
   map-marker-initiated teleport (`:1610-1611`).
6. `Teleport::TeleportToCoordinates` (LM/TV8) queues **raw** coordinates without subtracting the
   world origin (`:2172-2194`), unlike `TeleportToMarker` which does (`:2142-2146`). Whether the
   saved-location coordinates are in the same space as the destination fields is not asserted by
   any comment in these files.
7. `hkCombatTimingEval` currently returns the original result unchanged (`src/game/player.cpp:801-804`)
   while its install log claims the hook is active; the "Easy Parry & Easy Evade" behaviour
   referenced by the fail-safe string is not implemented in this file version.
