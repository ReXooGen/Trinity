# World, time and weather — feature dossier

Trinity scope: the **WORLD** tab (`src/gui/menu.cpp:1090-1137`) — Game Speed,
Freeze Time of Day, Advance / Rewind time, the Time of Day Presets sub-page,
and the Weather & Atmosphere sub-page (presets, fog, clouds, rain / snow / dust,
wind).

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, FileVersion `1.0.0.2944`.
Binary Ninja database `CrimsonDesert.exe.bndb`, view `view_1` (PE).
See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**. No breakpoints, no debugger, no injection,
no memory writes, no game launch.

---

## 1. How the WORLD tab is wired

```text
WORLD tab  (RenderWorld, menu.cpp:1090-1137)
  Game Speed                menu.cpp:1097
  Freeze Time of Day        menu.cpp:1103
  Advance / Rewind          menu.cpp:1110-1128
  Time of Day Presets       menu.cpp:977-1022
  Weather & Atmosphere      menu.cpp:1024-1088
        |
        +-- Game Speed ------------------------------------------------+
        |     World::Tick (per-frame, driven from hkMoveUpdate)        |
        |       -> VIBE_World_GetTimeScale  @ 0x140952080  <-- hook     |
        |            (only 3 callers: World::FrameTimerUpdate)         |
        |       -> Frame timer body inside World::FrameTimerUpdate     |
        |            @ 0x140AD1370 (kSig_FrameTimerBody @ 0x140AD13B3) |
        |                                                              |
        +-- Freeze / time of day --------------------------------------+
        |     clock globals (TimeOfDayReady, world.cpp:803-806)        |
        |     + g_todEngineGlobal  (kSig_TodEngineGlobal @ 0x142CE535A)|
        |     + render manager                                         |
        |       -> VIBE_World_EvalTimeCurve @ 0x140951440              |
        |       -> VIBE_Engine_GetTimeValue @ 0x141416B70              |
        |       -> VIBE_Time_ComputeDeltaTicks @ 0x1417AF690           |
        |       -> field-time realm  @ 0x1420E55A4                     |
        |       -> field-time tick   @ 0x140A38440                     |
        |                                                              |
        +-- Weather & Atmosphere ---------------------------------------+
              weather leaves:  rain @ 0x143DC39B0
                               snow @ 0x143DC3A60
                               dust @ 0x143DC3B10
              wind pack:       @ 0x143DBCCC0
              shader-pack hook: applies the fog / cloud half
              live injection:   world.cpp:667-751 (needs World::Tick)
```

**Two hooks, two different failure domains.** That split is the single most
important operational fact in this dossier:

- The **shader-pack** hook applies the fog / cloud half independently.
- The **live injection** half — sun clamp for Freeze, and *all* weather and wind
  injection (`world.cpp:667-751`) — runs from `World::Tick()`, which is
  dispatched **only** from `VIBE_Locomotion_MoveUpdateIntegrator`
  (`src/game/teleport.cpp:1637-1645`). If `kSig_MoveUpdate` fails to install, the
  sun-clamp half of Freeze and every live weather / wind write silently stop
  while the menu still shows the controls as available. See
  [`locomotion.md`](locomotion.md) §1.

---

## 2. Function dossiers

### VIBE_World_GetTimeScale — VA `0x140952080` *(pre-existing VIBE_ name, kept)*

- Feature: **Game Speed** — the best (and narrowest) hook for it.
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x952080` / VA `0x140952080` / `.code` / 108 bytes / 4 basic blocks
- Confidence: **confirmed** (pre-existing name kept; callers verified this pass)
- ABI: `RCX` = world object; **return in `XMM0`** — the prototype is
  `int64_t(void* arg1) __location("zmm0")`, i.e. the time scale comes back as a
  float/double in `XMM0`, not in `RAX`.
- Call flow: **exactly 3 callers, all `World::FrameTimerUpdate` (`0x140AD1370`)**.
  That single-caller-set property is what makes this the safest game-speed hook
  in the build — it cannot affect unrelated systems.
- Trinity consumer: `src/game/world.cpp` — Game Speed (`menu.cpp:1097`).
  Role: **MinHook inline detour**.
- Static proof: the 3-caller set, all inside the frame-timer update.
- Live proof: none recorded; `progress.md` classifies time-scale as
  "STATIC READY, not semantic PASS".
- OFF/restore or fail-safe result: MinHook detour; no bytes patched.
- Update notes: re-resolve via `World::FrameTimerUpdate`. If the caller set is no
  longer exactly the frame timer, **do not** hook it — a wider caller set means
  the function now scales something else, and a global time-scale override would
  be far more dangerous.
- Open questions: the returned value's unit (multiplier vs seconds) was not
  re-derived.

### `World::FrameTimerUpdate` — VA `0x140AD1370` *(pre-existing game-side symbol)*

- Feature: Game Speed — the frame-timer body Trinity locates by signature.
- Location: RVA `0xAD1370` / VA `0x140AD1370` / `.code` / 1839 bytes / 85 basic blocks
- Locator: `kSig_FrameTimerBody` (13 tokens) → **exactly 1 match** at
  `0x140AD13B3`, which is **inside this function at offset `+0x43`** — not a
  function start. Live hook state: not hooked (this is a locator anchor, not a
  detour target).
- Confidence: **confirmed** for the locator→function binding.
- Call flow: contains the body that calls `VIBE_World_GetTimeScale` three times.
- Trinity consumer: `src/game/world.cpp` — resolved as the anchor that proves
  the frame-timer path is present. Role: **locator only**.
- Update notes: `kSig_FrameTimerBody_Pre201` (17 tokens) has **0 matches** on
  PE 2944, so the modern body is required and there is no silent legacy
  fallback. Re-run the 13-token signature and confirm the hit is still inside
  `World::FrameTimerUpdate` (i.e. that the containing function still calls
  `VIBE_World_GetTimeScale` three times).

### `VIBE_World_FrameUpdate` — VA `0x140AD23F0` *(pre-existing VIBE_ name, kept)*

- Feature: the world frame pump adjacent to Game Speed.
- Location: RVA `0xAD23F0` / VA `0x140AD23F0` / `.code` / 3432 bytes / 167 basic blocks
- Locator: documented 48-byte prologue AOB
  `48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC D8 00 00 00 4C 8B E9 48 8B 41 28 4C 8B B8 F8 10 00 00 4C 89 7D 7F 49`
  → **exactly 1 match in the whole image**, at this VA, over all 12 sections.
- Confidence: **confirmed** for the locator binding; the function's full role
  was not re-derived this pass.
- Call flow: **exactly 1 caller — `sub_140617230`**.
- Trinity consumer: adjacent to Game Speed; not hooked.
- Update notes: re-run the AOB (expect exact unique). The 48-byte form contains
  no RIP-relative displacement in its first 40 bytes, so it is comparatively
  robust — but it is a *new record created in this pass* and is not yet wired
  into any Trinity source or test.

### Time-of-day and field-time anchors

| Function / anchor | VA | Locator | Matches | Role |
|---|---|---|---|---|
| `VIBE_World_EvalTimeCurve` | `0x140951440` | documented 48-byte AOB | **1** | time-dilation curve; `(int64_t, char, float, int32_t[4] @ zmm5)` |
| `VIBE_Engine_GetTimeValue` | `0x141416B70` | documented 48-byte AOB | **1** | engine time; `int64_t()` |
| `VIBE_Time_ComputeDeltaTicks` | `0x1417AF690` | documented 48-byte AOB | **1** | delta ticks; `(int64_t, int64_t*, int64_t*, int64_t*)` |
| field-time realm | `0x1420E55A4` | `kSig_FieldTimeRealm` (34 tok) | **1** | inside `sub_1420E5540` (`0x1420E5540`–`0x1420E565D`, 11 blocks) |
| field-time tick | `0x140A38440` | `kSig_FieldTimeTick` (40 tok) | **1** | `sub_140A38440`, 23 blocks, `int64_t(pa::IEvent::VTable*, int128_t @ zmm1) __location("zmm0")` |
| ToD engine global | `0x142CE535A` | `kSig_TodEngineGlobal` (25 tok) | **1** | inside `sub_142CE5300` (`0x142CE5300`–`0x142CE5624`, 35 blocks) |

**Fragility note.** `VIBE_World_GetTimeScale`, `VIBE_World_EvalTimeCurve` and
`VIBE_Engine_GetTimeValue` prologues contain **RIP-relative displacements**
(`C5 FA 10 05 …`, `C5 7A 10 0D …`, `39 05 …` / `48 8D 0D …`) and
`VIBE_Time_ComputeDeltaTicks` is fine but short. A RIP-relative byte run moves
on **any** PE relayout even when the function is unchanged. Prefer the shorter
stable prefix for these, or re-derive the locator from a caller/table anchor.
`kSig_FieldTimeTick_Pre201` has **0 matches**, so there is no legacy fallback.

### Weather and wind anchors

| Anchor | VA | Locator | Matches | Prototype |
|---|---|---|---|---|
| rain | `0x143DC39B0` | `kSig_WeatherRain` (38 tok) | **1** | `int32_t(void* arg1) __location("zmm0")[0x4]` |
| snow | `0x143DC3A60` | `kSig_WeatherSnow` (38 tok) | **1** | same shape as rain |
| dust | `0x143DC3B10` | `kSig_WeatherDust` (32 tok) | **1** | same shape as rain |
| wind pack | `0x143DBCCC0` | `kSig_WindPack` (35 tok) | **1** | `int32_t(int64_t*, int32_t*, int32_t[4] @ zmm1) __location("zmm0")[0x4]` |

- Confidence: **confirmed** for every locator→address binding (each exact unique
  over all 12 sections); the functions' internal semantics were **not**
  decompiled this pass.
- Trinity consumer: `src/game/world.cpp` — the Weather & Atmosphere sub-page
  (`menu.cpp:1024-1088`); the live writes go through `world.cpp:667-751`, which
  needs `World::Tick`.
- **Hazard — unbounded magic indices.** `hkWindPack` indexes the packed array
  with magic numbers and **no length bound** (`world.cpp:285-359`). A PE update
  that changes the packed-array length would read or write out of bounds. Bound
  the indices before re-enabling on a new revision.
- **Hazard — unit ambiguity.** `CN::CLOUD_THICK` (`world.cpp:710-717`, computed
  as `cloudTop * 0.001f`) and the packed index `0x2F` (`:334-342`, used as a raw
  multiplier) may be the same physical parameter in different units. This is
  **not asserted anywhere in the source**; if they are the same, one of the two
  writes is wrong.
- **Known gap — snow intensity is unreachable.** No weather preset can reach it
  (`world.cpp:896-986`). Presets are also **not self-contained**: unassigned
  fields persist from whatever the previous preset set.

---

## 3. Locator evidence (measured over the whole image)

Method: `_analysis/aob_scan2.ps1` reproduces Trinity's scanner against the
on-disk image, all 12 sections.

| Locator | Length | Matches | VA | Result |
|---|---|---|---|---|
| `kSig_FrameTimerBody` | 13 tok | **1** | `0x140AD13B3` | exact unique (function offset `+0x43`) |
| `kSig_FrameTimerBody_Pre201` | 17 tok | **0** | — | absent (expected) |
| `kSig_FieldTimeRealm` | 34 tok | **1** | `0x1420E55A4` | exact unique |
| `kSig_FieldTimeTick` | 40 tok | **1** | `0x140A38440` | exact unique |
| `kSig_FieldTimeTick_Pre201` | 35 tok | **0** | — | absent (expected) |
| `kSig_TodEngineGlobal` | 25 tok | **1** | `0x142CE535A` | exact unique |
| `kSig_WeatherRain` | 38 tok | **1** | `0x143DC39B0` | exact unique |
| `kSig_WeatherSnow` | 38 tok | **1** | `0x143DC3A60` | exact unique |
| `kSig_WeatherDust` | 32 tok | **1** | `0x143DC3B10` | exact unique |
| `kSig_WindPack` | 35 tok | **1** | `0x143DBCCC0` | exact unique |
| `kSig_WindPack_Pre201` | 35 tok | **0** | — | absent (expected) |
| `kSig_EnvManager` | 28 tok | **0** | — | **absent — environment manager unavailable** |
| `kSig_EnvManager_Legacy` | 23 tok | **0** | — | absent |

`kSig_EnvManager` failing on both the modern and legacy forms means Trinity has
**no** environment-manager anchor on PE 2944. Do not substitute the PE 2850
drift site (`0x14391E920`) or `pEnvManager` (`0x146C1ADC0`) — both were recorded
against a different revision and neither is reconciled to this build.

---

## 4. Fail-closed and gating behaviour

1. **Freeze's menu gate is narrower than the feature.** `TimeOfDayReady()`
   checks only the clock globals (`world.cpp:803-806`), but the feature also
   needs `g_todEngineGlobal` and the render manager. The row can therefore look
   available while part of the feature is inert.
2. **`World::Ready()` is Game-Speed-only** (`world.cpp:798`), so it does not
   gate the time or weather controls.
3. **No byte patches in this area** — every mechanism is a MinHook detour or a
   guarded data write, so "OFF" is always "stop writing / remove the detour",
   and there is nothing to restore byte-wise.
4. **`World::Tick` dependency** (§1) — the largest silent-failure risk in this
   dossier, and it is not visible in the menu.

---

## 5. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. **Confirm `kSig_MoveUpdate` first** (`locomotion.md`). Without it, nothing in
   the live-injection half of this dossier runs, and the failure is silent.
3. Re-run `kSig_FrameTimerBody` (expect 1 match inside `World::FrameTimerUpdate`)
   and then re-resolve `VIBE_World_GetTimeScale` from the frame-timer caller set.
   Require the caller set to still be **exactly** the frame timer.
4. Re-run the four weather/wind signatures (rain, snow, dust, wind pack) and the
   three field-time/TOD signatures. All expect exactly 1 match.
5. Re-run `kSig_EnvManager` and `kSig_EnvManager_Legacy`. If both are still 0,
   record that the environment manager remains unavailable — do **not** import
   the PE 2850 addresses.
6. Re-read `hkWindPack`'s array indices in `src/game/world.cpp` and add bounds
   before enabling; then decide whether `CN::CLOUD_THICK` and packed `0x2F` are
   the same parameter.
7. If any locator is absent or ambiguous, leave that control fail-closed and
   record the mismatch here. Never fall back to a `_Pre201` locator — every one
   of them measured 0 on this build.
8. Runtime test, one control at a time with an OFF leg:
   - Game Speed: set 2×, observe, restore 1×.
   - Freeze Time of Day: freeze, observe the clock stop, unfreeze.
   - Advance / Rewind: step the hour, confirm the clock and the sun both move
     (the sun clamp is the half that depends on `World::Tick`).
   - Weather preset: apply, observe, apply a different preset and check for
     leakage from the previous one.
   - Rain / snow / dust / wind: one at a time, with OFF legs.
