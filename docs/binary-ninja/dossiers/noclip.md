# No Clip — feasibility dossier

Status: **implemented, compiled, unit-tested — NOT yet verified in game.**
Routes A (§4) and C (§4) remain analysis only; the shipped feature is route B.

> ## What actually shipped
>
> Route B — the incremental position step — implemented in `hkMoveUpdate`:
>
> | File | Change |
> |---|---|
> | `src/game/noclip_logic.h` | **new** — pure step maths, no engine contact |
> | `src/game/teleport.cpp` | heading publish in `hkLocoStep`; `ApplyNoClipStep` in `hkMoveUpdate`; dt/diag/engaged state |
> | `src/game/teleport.h` | `GetNoClipEngaged()`, `MoveHooksReady()` |
> | `src/core/state.h` | `noClip`, `noClipSpeed`, `noClipUp/DownKeyVk`, `noClipUp/DownPadMask` |
> | `src/core/settings.cpp` | persists the **speed and the binds only** — the enabled bit always starts false |
> | `src/gui/menu.cpp` | PLAYER-tab row (fail-closed gate), Keybinds rows, HUD `NOCLIP` suffix |
> | `tests/noclip_logic_tests.cpp` | **new** — 17 assertions, all passing |
>
> Design decisions worth knowing:
>
> - **No new offsets.** The step reuses `ApplyMarkerTeleportDestination()`
>   verbatim — the only position write in Trinity with live proof behind it.
> - **Standalone.** No flight mode required, works on foot, and it has its OWN
>   rise/sink binds (Space / Ctrl, D-Pad Up/Down) rather than borrowing Free
>   Flight's, so the two features never compete for a key.
> - **Heading comes from R8 in the loco-stepper** — the drive velocity, i.e. the
>   same camera-relative vector Free Flight drives successfully — normalised and
>   published for the integrator hook that runs later in the same tick.
> - **Inert when idle.** No direction held ⇒ no write at all.
> - **Two clamps.** dt to 100 ms, speed to 35 m/s.
> - **The enabled bit is deliberately not persisted**, unlike every other
>   feature in Trinity.
> - **Diagnostics.** `ApplyNoClipStep` logs the opening frames of every session
>   (`Trinity.log`): heading valid or not, proxy position, computed step,
>   `applied`, and whether the write survived its read-back. This was added
>   after the first live test failed, because the failure was not reachable
>   statically.
>
> ### Live test history
>
> | Round | Result |
> |---|---|
> | 1 | **Failed.** Heading was read from the proxy's `+0xC0`; the integrator both reads that field and books a *measured* value back into it, so it is not reliably the player's intent at an observable moment. Replaced with the loco-stepper's R8 drive vector. |
> | 2 | **Direction solved, collision not.** The log proved the rework works as designed: `dt=0.038` (seconds, not ms), `heading=(-0.901,-0.433)` camera-relative and matching the step exactly, `applied=1 survived=1`. But the character still never passed through geometry — a ~0.27 m per-tick nudge lost to the engine's collision resolution. |
> | 3 | **WORKS.** Pre-integrator pinning fixed it — the player passed through collision and ended up wedged inside a cliff. `drift=0.0000` and `repinned=0` on every sample: the write survives a full engine tick and the integrator does not undo it. Route B is sound; round 2's failure was purely write ordering. |
>
> **Round 3 found No Clip's real limit.** While wedged, `from=` was
> byte-identical across samples 30+ seconds apart *with a valid heading* — once
> the body is fully inside solid geometry, the collision solver's depenetration
> undoes every per-tick step. So No Clip moves you *through* geometry but cannot
> tunnel you back *out* of a solid mass; that needs a jump too large to drag
> back, which is what the teleport path is. Shipped as the **"Escape: Back To
> Last Safe Spot"** button — it warps to the position latched when No Clip was
> switched on (the last spot collision accepted), through the same verified
> write. This is the "return to last safe position" §5.2 called for.
>
> **Round 2 also exposed a real usability constraint:** the heading is only
> published while the player is holding a direction (that is what keeps the
> feature inert when idle), so No Clip moves horizontally **only while WASD /
> stick is held**. Holding only Ctrl / D-Pad Down gives a purely vertical step.
>

> Outstanding: **the §5 risks are unchanged.** The `isPlayer` gating is still an
> assumption (§5.1), and route A remains the better long-term answer.

Trinity scope: a new PLAYER-tab **No Clip** feature — collision-free movement for
the local player.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, Binary Ninja database
`E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe.bndb`
(view `view_1`). See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**, through Binary Ninja's UI MCP endpoint
(`binaryninja_ui_mcp`, `127.0.0.1:24642/mcp`). String census, data xrefs,
function decompilation and memory reads. **No live session was run** — no
breakpoints, no debugger, no injection, no game launch.

> Endpoint note: the pre-wired `binaryninja` MCP tool in this environment fails
> with `WinError 10061` even though the endpoint is up. Two working clients are
> checked in at `_analysis/bn_mcp.py` (raw MCP JSON-RPC client) and
> `_analysis/bn_batch.py` (runs a local script against the endpoint).

---

## 1. Verdict

**Yes — No Clip is implementable in Trinity, and Trinity is materially better
positioned for it than the Cheat Engine table is.**

The reason is that Trinity already owns the exact point in the frame where the
local player's Havok character proxy is driven (the two MinHook detours in
[`locomotion.md`](locomotion.md)), and it already contains a live-verified
position-write path (map-marker teleport, [`travel.md`](travel.md) §4). No Clip
is a recombination of machinery Trinity already has, plus one new piece:
**taking collision out of the loop**.

What is *not* yet established is **which** of the three routes below is the right
one — that needs one offset-hunting pass and one in-game validation round. §5
lists exactly what to verify.

---

## 2. What the Cheat Engine table actually does

The reference table already ships a No Clip, so it is worth being precise about
what it is, because it is **the weakest of the three viable routes** (see §4 B).

Records in `CrimsonDesert.CT`:

| ID | Description | What it is |
|---|---|---|
| 372 | `no clip speed` | AA script: `alloc(noclipSpeed_addr, 4)`, `dd (float)0.5` |
| 373 | `Noclip speed (units/tick)` | float view of `noclipSpeed_addr`, default `0.5` |
| 195 | `4. Generate scripts` | the enabler — allocates `tpData` and calls the Lua below |
| 186 | `2. Move speed multiplier / crow fly mode alt` | **hard dependency** of 372 |

Record 195 runs, from its own `[ENABLE]` Lua block:

```lua
local tableFile = findTableFile("celua_teleport.lua")
-- ... load the stream, then:
if celua_setupTeleport then
  celua_setupTeleport()
  celua_setupNoclip()      -- <-- the No Clip driver
end
```

and `[DISABLE]` calls `celua_teardownTeleport()` / `celua_teardownNoclip()`. So
No Clip lives in the table's embedded `celua_teleport.lua`, and it is a **Lua
timer loop**, not an injection.

Record 186 must be on first — record 195's sibling script refuses to run
otherwise (`showMessage("Enable SymbolScanner first")` / `"Please enable Move
speed multiplier"`), because 186 is what captures the player entity pointer:

```asm
aobscanmodule(INJECT_ENTCAP, $process, C4 ?? ?? 18 ?? ?? 00 00 00 C5 ?? 59 ?? ?? 8B ??)
alloc(entityCapturePtr, 8)
...
  mov  rax, rbp            ; rbp = the entity
  ...
  push rcx
  mov  rcx, entityCapturePtr
  mov  [rcx], rax          ; publish the entity pointer
  pop  rcx
```

186 also scales the desired-velocity vector in place: `x`/`z` by
`vf_move_speed_multi = 1.3333`, and the height lane by `×2.0` going up /
`×0.333` going down (`vf_move_z_mul`, `vf_vec_down_factor`). Its own comment
names the intent: *"Move speed multiplier / crow fly mode alt"* — i.e. it is
reproducing the game's own crow-flight feel via a velocity multiplier.

**Conclusion:** the table's No Clip = *capture the entity pointer, then have Lua
move the entity's position by `noclipSpeed` units per tick, with a velocity
multiplier hook supplying the "fly" half.* That is route B in §4, driven from
outside the process.

**Gap (recorded honestly):** the body of `celua_teleport.lua` was **not**
recovered. The CT stores it as `<File Name="celua_teleport.lua"
Encoding="Ascii85">`, and three independent reads were attempted:

1. The blob's 85-symbol alphabet was recovered exactly — it is
   `0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%()*+,-./:;=?@[]^_{}`
   (RFC1924-style, XML-unsafe characters `" & ' < > \ ` |` dropped). This was
   confirmed against the literal string table inside
   `C:\Program Files\Cheat Engine\cheatengine-x86_64.exe` at file offset
   `0xC9CDEF`, and it decodes with **0 out-of-range groups** across all 1738
   base-85 groups — so the alphabet and radix are right.
2. The 6955 decoded bytes have **7.97 bits/byte entropy** — indistinguishable
   from random. A plain-source payload would be ~4.5–5.
3. No permutation of the five digit weights (all 120 tried) yields text, and no
   zlib / raw-deflate / gzip stream starts at offsets 0–8.

So the file is stored **compressed or encrypted** and the algorithm text is not
recoverable from the CT statically. It is recoverable live in seconds — CE is
installed and running; see §7 step 0.

---

## 3. What the game code offers (measured in the `.bndb`)

All findings below are string-table + data-xref results. Confidence is marked.

### 3.1 The character controller is Havok 2023 (`hknp*`) — **confirmed**

| String | VA |
|---|---|
| `hknpCharacterProxy` | `0x145cbd858` |
| `hknpCharacterProxyCinfo` | `0x1452f8ab8` |
| `hknpCharacterProxyManager` | `0x14530c138` |
| `hknpCharacterProxyListener` | `0x14531b060` |
| `hknpCharacterSurfaceInfo` | `0x145301ca0` |
| `hknpCharacterProxyViewer` | `0x145319878` |
| `hknpCharacterState::hknpCharacterStateType` | `0x145303bd8` |
| `LtCharacterProxyIntegrate` | `0x14530db68` |
| `TtCharacterProxyDeferredUpdate` | `0x14530dbe0` |
| `CharacterProxyMatchingTable` | `0x1455c5e88` |

This matters because it fixes the object model: Trinity's `moveOwner` — the
integrator's `RCX`, `[component + 0x2C0]` on PE 2944 (`locomotion.md` §2) — is the
BlackSpace-side handle on an **`hknpCharacterProxy`**. That object is the thing
the world is swept against, so it is the correct place to attack collision.

### 3.2 Havok's own character states — **confirmed**

`hknpCharacterStateType` enumerates:

```text
HK_CHARACTER_ON_GROUND   @0x145303ce0
HK_CHARACTER_JUMPING     @0x145303cf8
HK_CHARACTER_IN_AIR      @0x145303d10
HK_CHARACTER_CLIMBING    @0x145303d48
HK_CHARACTER_FLYING      @0x145303d60
HK_CHARACTER_USER_STATE_0..5
HK_CHARACTER_MAX_STATE_ID / HK_CHARACTER_INVALID_STATE
```

`HK_CHARACTER_FLYING` proves the engine's character controller has a first-class
**flying state** — the character is not "on ground" and not merely "in air". That
is the cleanest possible hook for a noclip: if the player can be put in the
flying state, the engine itself stops applying ground and gravity authority.

### 3.3 Collision-filter machinery — **confirmed present, semantics assumed**

| String | VA | Meaning |
|---|---|---|
| **`hknpDisableCollisionFilter`** | `0x145308a90` | Havok's dedicated "collides with nothing" filter |
| `hknpSetBodyCollisionFilterInfoCommand` | `0x1453015b8` | per-body filter change command |
| `hknpSetWorldCollisionFilterCommand` | `0x1453049c0` | world-wide filter change |
| `collisionFilterInfo` | `0x145302aa0` | field name (xref'd from 12 sites) |
| `hknpGroupCollisionFilter` | `0x14530a050` | group-mask filter |
| `hknpPairCollisionFilter` | `0x1453083d0` | pair-exclusion filter |
| `hknpConstraintCollisionFilter` | `0x145308718` | constraint-driven filter |

`hknpDisableCollisionFilter` is the important one: Havok ships a filter type whose
entire purpose is to make a body collide with nothing. **This is the true-noclip
primitive.**

> Caveat (**assumed, not proven**): these are Havok type-registration strings.
> The classes exist in the binary, but this pass did **not** prove the game ever
> instantiates `hknpDisableCollisionFilter`, nor did it find the filter slot
> offset inside the live `hknpCharacterProxy`. Neither is required for
> feasibility — a class present in the image can be instantiated by us — but the
> offset is required to ship.

### 3.4 BlackSpace-side move controller and a fly move-option — **confirmed**

| String | VA |
|---|---|
| `CharacterPhysicsMoveController` | `0x145cbd888` |
| `::pa::engineScript::allocateCharacterPhysicsMoveControllerScript` | `0x14560de30` |
| `PACharacterPhysicsMoveControllerScript` | `0x14560df58` |
| `MoveControlOptionType` | `0x1455740d0` |
| **`FlyVehicleMoveControlOptionType`** | `0x1455740e8` |
| `GimmickEventHandlerData_MoveControl` | `0x1457c4810` |
| `FreeCamera` | `0x1455ca200` |

Two things follow. First, the character's movement is genuinely driven by a
named `CharacterPhysicsMoveController` with a **script-side allocator**, i.e.
there is an engine-script surface for it — and the CE table's "crow fly mode"
wording now reads as the author's name for the game's own flight movement mode,
which `FlyVehicleMoveControlOptionType` corroborates.

Second, `FreeCamera` exists as a separate engine camera. That is a *camera*
feature (photo/cinematic), not a player noclip, and is out of scope here.

---

## 4. The three viable routes

### A. Free Flight + collision off — **recommended**

Keep Trinity's existing, live-verified velocity-based Free Flight, and separately
stop the proxy from colliding. Free Flight already gives 3-D movement with
camera-relative horizontal, vertical up/down, hover and forced landing
(`teleport.cpp:1326-1573`). Collision is the **only** remaining thing standing
between that and true noclip — today you slide along walls instead of passing
through them.

**Why this one:** it composes two mechanisms that are *already proven stable in
this codebase*. The velocity path does not desync physics (`offsets.h:365-380`
records the servo trace and why velocity injection is the survivable axis), and
the collision disable is a single body-level write that no servo books back.

**What it needs:** the `collisionFilterInfo` / filter slot offset inside the live
`hknpCharacterProxy` on PE 2944, and a per-body write — *never* a write to the
shared filter object, which would also disable collision for every other body
using it.

**Restore is mandatory.** Noclip off must put the original filter back, or the
player falls through the world.

### B. Position-pin noclip — **what the CE table does; quick win, known-risky**

Every tick: advance `moveOwner + 0x90` along the camera basis by the noclip
speed, and zero `+0xC0` / `+0xD0` so the integrator does not re-derive the move.

**Trinity already has all of this.** `ApplyMarkerTeleportDestination()` with
`WriteTeleportPosition` / `ReadTeleportPosition` (`teleport.cpp:1594-1635`) is
exactly this operation with verification and a 3-attempt retry — map-marker
teleport re-aimed every frame instead of once.

- **Pro:** implementable today. **No new offsets.** The write primitive is
  live-verified working on PE 2944.
- **Con — and it is serious:** this repo has already concluded that *"every
  memory-write approach desyncs the Havok body"* (`offsets.h:414-420`,
  `travel.md` opening), which is precisely why native fast travel exists at all.
  Per-frame position writes fight streaming and the broadphase; the failure mode
  is falling out of the world, getting stuck in a loading volume, or a CTD.

Ship B only as a fallback, only with small per-tick deltas (the table's own
default is a modest `0.5` units/tick, which is consistent with exactly this
concern), and only with a "return to last safe position" escape.

### C. Engine-native flying state — **most robust, most work**

Put the player into `HK_CHARACTER_FLYING` (or drive the move controller through
`allocateCharacterPhysicsMoveControllerScript` /
`FlyVehicleMoveControlOptionType`) so the engine itself handles collision-free
3-D movement.

- **Pro:** nothing fights the engine. No desync, no servo, animations and camera
  behave, and it is almost certainly what the CE table's "crow fly mode" means.
- **Con:** needs the state-transition entry point, which is not identified, and
  the game's own flight is bound to a mount/gimmick — it may drag in mount,
  stamina and camera systems that Trinity would then have to suppress.

---

## 5. What must be verified before writing code

Ordered by risk. Items 1 and 2 are the ones that can hurt the player.

> Status after the implementation: **2 and 3 are handled** (there is no filter to
> restore on route B, and both the dt and the speed are clamped in
> `noclip_logic.h`). **1, 4 and 5 are open** — item 1 is the one that still needs
> a live answer before this can be called safe rather than merely inert-when-idle.

1. **`isPlayer` gating is unproven** — `locomotion.md` §6.4 and
   `offsets.h:382-386` both record that the identity match rests on a one-shot
   diagnostic log, not a validated pointer chain. The loco-stepper *fires for
   NPCs too* and is gated only by a "this dispatch only reaches the player in
   practice" assumption. **That assumption is tolerable for a speed multiplier
   and unacceptable for a collision disable**: a wrong guess disables collision
   for NPCs, which can corrupt the world. A No Clip must be gated on a
   *positively validated* local-player identity, or not ship.
2. **Restore path and escape hatch.** Every path that turns collision off must
   have an OFF leg that restores the original filter, plus a "return to last
   safe position" for the case where the player is already inside geometry when
   it is re-enabled.
3. **The speed ceiling still applies.** `teleport.cpp:1486-1487` records that
   above ~35 m/s while gliding the Havok broadphase overflows and the game CTDs
   (Trinity clamps flight to `35.0f`, ground to `50.0f`). Noclip must inherit the
   same ceiling — "noclip" is not a licence to raise it.
4. **Filter slot offset** inside the live `hknpCharacterProxy` on PE 2944, and
   whether the game re-applies the filter every frame (which would require the
   write to be held per-tick, e.g. from the existing `hkMoveUpdate` detour,
   rather than set once).
5. **Streaming behaviour with collision off.** Whether the world streams around
   the player when it is not being swept against terrain. This decides A vs. C.

---

## 6. Where the feature goes in Trinity

Infrastructure already exists, so the wiring is small:

| Concern | Existing anchor |
|---|---|
| Velocity write + input plumbing | `hkLocoStep` ← `VIBE_Locomotion_StepDriveVelocity` |
| Proxy pointer + per-tick game thread | `hkMoveUpdate` ← `VIBE_Locomotion_MoveUpdateIntegrator`, `moveOwner` = `[component+0x2C0]` |
| Fields | `+0x90` pos, `+0xC0` desired vel, `+0xD0` frame vel, `+0x1A0` secondary dest |
| Position write + verify + retry | `ApplyMarkerTeleportDestination`, `WriteTeleportPosition`, `ReadTeleportPosition` |
| Menu row | `menu.cpp:150-154` — `ui::ToggleFloat(...)` next to Super Run / Super Jump / Free Flight |
| Settings | `state.h:63-77` — `st.superRun` / `st.freeFlight` / `st.flightSpeed` |
| Keybinds | `menu.cpp:2909-2913` — Fly Up / Fly Down already bound |
| Fail-closed precedent | `locomotion.md` §2 "OFF/restore" — contract `Unsupported` → `LOG_ERR` + hook not installed |

Note the standing hazard recorded in `locomotion.md`: **`menu.cpp:150-155`
performs no readiness check**, so on an unsupported revision the toggles stay
visible and settable but inert, with only the install-time `LOG_ERR` as
evidence. A No Clip toggle must not copy that — it should follow the **God Mode**
pattern (`menu.cpp:88-90`), which gates on `game::Player::Ready()`.

---

## 7. Recipe

0. **Recover the CE table's algorithm (seconds, not required but cheap).**
   `celua_teleport.lua` is encrypted in the CT but CE decodes it at runtime. With
   the table loaded in CE, either run record `195` and then read
   `findTableFile("celua_teleport.lua")` from CE's Lua console, or have the CE
   MCP bridge dump it. This gives a working reference implementation and its
   exact per-tick write order. *(In this session the CE MCP bridge timed out on
   every call, so this step is outstanding.)*
1. Decompile `VIBE_Locomotion_MoveUpdateIntegrator` `0x144282080` and
   `VIBE_Locomotion_StepDriveVelocity` `0x14369FF60` — **not done in this pass** —
   and map where the proxy's collision filter and character state are read.
2. Find the filter slot on the live proxy: xref the `hknpCharacterProxy` /
   `CharacterProxyInfo` class tables, or trace the proxy construction from
   `hknpCharacterProxyCinfo` `0x1452f8ab8`.
3. Decide A vs. C on the §5.5 streaming answer.
4. Implement with a hard `isPlayer` gate (**§5.1**), a tested restore path
   (**§5.2**), the inherited 35 m/s ceiling (**§5.3**), and God-Mode-style menu
   gating (**§6**).
5. Live test, each with an OFF leg: toggle on inside a wall, inside terrain, in
   a town, in a dungeon; toggle off inside geometry; toggle off mid-air; then
   confirm NPCs are unaffected and that a save/load after noclip is clean.

---

## 8. Open questions

1. Is the collision filter on the proxy re-applied per frame by the game?
2. Does the `hknpCharacterProxy` share its filter object with other bodies? (If
   yes, the per-body command object is mandatory, not optional.)
3. What is the entry point into `HK_CHARACTER_FLYING`, and does it require a
   mount/gimmick context?
4. `CharacterPhysicsMoveController` script allocator
   `0x14560de30` — is it a usable runtime surface, or load-time only?
5. Does `hknpDisableCollisionFilter` get instantiated anywhere in the shipping
   game, or is it dead registration?
6. What exactly does the CE table's `celua_teleport.lua` write per tick? (§7.0)
