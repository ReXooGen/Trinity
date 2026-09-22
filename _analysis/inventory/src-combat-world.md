# Trinity — source-side consumers: PLAYER/COMBAT, WORLD/TIME/WEATHER, PET/MOUNT

Scope: static documentation of every place the Trinity mod touches the Crimson Desert
executable for three feature areas:

1. **PLAYER / COMBAT** — health, stamina, spirit, God Mode, Easy Parry, One-Hit Kill, damage.
2. **WORLD / TIME / WEATHER** — game speed, freeze time, time presets, weather, fog, wind.
3. **PET / MOUNT** — mount stamina, mount damage immunity, pet/mount taming (trust), mount gear.

Every claim carries an exact `source file:line`. Byte patterns and structure offsets are copied
verbatim from the source. Nothing here was inferred from the binary, and **no source file was
modified** — the only file written is this one.

Sources **read in full**: `src/game/player.cpp` (984 lines), `src/game/player.h`,
`src/game/player_logic.h`, `src/game/player_logic.cpp`, `src/game/world.cpp` (994 lines),
`src/game/world.h`, `src/core/state.h`, `src/mem/hooks.h`, `src/mem/safe_memory.h`,
`src/mem/scanner.h`, `src/game/friendly.cpp` (354 lines), `src/game/friendly.h`,
`src/core/readiness.cpp`, `src/core/readiness.h`, `src/core/version_mapping.cpp`,
`src/core/version_detect.h`.
Sources **grepped / read in relevant regions only**: `src/game/offsets.h` (1851 lines),
`src/gui/menu.cpp` (3235 lines), `src/core/mod.cpp`, `src/core/settings.cpp`,
`src/core/logger.h`, `src/game/teleport.cpp`, `src/game/teleport.h`, `src/game/inventory.h`,
`src/game/inventory.cpp`, `src/game/equipment.cpp`, `src/game/equipment.h`, `src/dllmain.cpp`,
`config/Trinity.ini.example`, `languages/Trinity_*.ini`.

Convention: `f:NNN` means `path/file:line`. All line numbers are against the working tree at the
time of writing.

---

## 0. Entry points and wiring summary

| Item | Value | Source |
|---|---|---|
| Install order (this area's subsystems highlighted) | `Player::Install()` → `Teleport::Install()` → `Inventory::Install()` (owns the No-Bounty crime hooks) → **`World::Install()`** → `Equipment::Install()` (owns Infinite Durability) → **`Friendly::Install()`** → `Worker::Install()` | `src/core/mod.cpp:129-135` |
| Install results discarded | every `Install()` return value is ignored; the overlay always starts | `src/core/mod.cpp:127-135` |
| Removal | `Player::Remove(); … World::Remove(); … Friendly::Remove();` | `src/core/mod.cpp:157-163` |
| MinHook init prerequisite | `MH_Initialize()` precedes every install; failure aborts the whole mod | `src/core/mod.cpp:97-101` |
| Readiness probe (address presence only, no hook, no call) | revision ≤ 2.02.00 profile requires `kCharMgrAnchors[0].sig`, `kSig_StatCommit`, `kSig_DamageApply_Alt`, `kSig_CombatTimingEval`, `kSig_MoveUpdate`, `kSig_InvGetItemQty`, `kSig_EvaluateCrimeWantedState`, `kSig_FrameTimerBody`, `kSig_FieldTimeTick`, `kSig_TodEngineGlobal`, `kSig_WeatherRain`, `kSig_EquipEffectRefresh`; the TU 2.01-compatible profile requires `kSig_DamageApply_Alt`, `kSig_CombatTimingEval`, `kSig_MoveUpdate`, `kSig_InvGetItemQty`, `kSig_EvaluateCrimeWantedState`, `kSig_TodEngineGlobal`, `kSig_WeatherRain` | `src/core/mod.cpp:36-73` |
| Readiness profile selection | `UsesTu201CompatibleRevision(rev)` → `Tu201KnownCompatible`, else `LegacyComplete`; the TU-2.01 set is `{2760, 2850, 2944}` | `src/core/readiness.cpp:8-13`, `src/core/version_mapping.cpp:22-28` |
| Readiness timeout | 180 000 ms, 2000 ms poll; on timeout the mod installs "available hooks only" and continues | `src/core/mod.cpp:116-125` |
| **Game-thread driver for this whole area** | `Player::Tick()` and `World::Tick()` are called **only** from inside `hkMoveUpdate` (the movement-update hook in `teleport.cpp`) | `src/game/teleport.cpp:1637-1645` |
| Player/mount/stat ownership | `game::Player` | `src/game/player.h:33-70` |
| World/time/weather ownership | `game::World` | `src/game/world.h:43-78` |
| Pet/mount trust ownership | `game::Friendly` | `src/game/friendly.h:28-44` |

**Consequence of the single driver:** if the `kSig_MoveUpdate` hook fails to install,
`Player::Tick()` and `World::Tick()` never run. The *hook-driven* parts of these features still
work (stat-commit guard, damage-apply, field-time freeze, weather intensity getters, wind pack,
game-speed timer hook), but **the per-frame pin half of God Mode / Infinite Stamina / Infinite
Spirit, the sun-clamp half of Freeze Time of Day, and the entire live weather/atmosphere
injection are absent** — `src/game/teleport.cpp:1637-1645`, `src/core/mod.cpp:130` (return value
ignored).

---

## A. Feature list (user-visible menu features + exact C++ entry points)

Menu tabs: `kTabs`/`Tab` at `src/gui/menu.cpp:35-36`; top-level dispatch at
`src/gui/menu.cpp:3191-3199`; sub-page routing at `src/gui/menu.cpp:3230-3232`.
The PLAYER tab page is `RenderPlayer()` `src/gui/menu.cpp:119-164`; the combat sub-page is
`RenderCombatOptions()` `src/gui/menu.cpp:79-117`; the WORLD tab page is `RenderWorld()`
`src/gui/menu.cpp:1090-1137`, with sub-pages `RenderTimePresets()` `:977-1022` and
`RenderWeatherAtmosphere()` `:1024-1088`.

### A.1 PLAYER / COMBAT

Every combat toggle lives in the `PLAYER` tab → `"Combat & Gameplay Options"`
(`src/gui/menu.cpp:124-125`, routed at `:3230`).

| # | Menu feature (label) | C++ entry point(s) | State | Game-side effect |
|---|---|---|---|---|
| PC1 | `"One-Hit Kill"` | `ui::Toggle(..., &st.oneHitKill, ...)` `src/gui/menu.cpp:85-86` | `st.oneHitKill` `src/core/state.h:46` | substitutes a **10000.0f** outgoing multiplier inside `ScaleDamage` `src/game/player.cpp:713` (the label says "1,000x", the code uses 10 000×) |
| PC2 | `"God Mode"` | `ui::Toggle(..., &st.godMode, ...)` `src/gui/menu.cpp:87-90`; description switches on `Player::Ready()` `:88-90` | `st.godMode` `src/core/state.h:45` | four independent guards: stat-commit target override `src/game/player.cpp:585-587`; damage-apply zeroing `:758-761`; `ScaleDamage` early `return 0` `:660`; per-frame `PinEntry` of the HP entry `:539-546` (char-manager path) and `:374` (fallback path). Covers the **mount** too, via `ShouldBlockPlayerDamage(..., mountTarget)` `src/game/player_logic.cpp:5-9` |
| PC3 | `"Infinite Item Durability"` | `ui::Toggle(..., &st.infDurability, ...)` `src/gui/menu.cpp:91-92` | `st.infDurability` `src/core/state.h:47` | `Equipment::Tick()` every 1500 ms → `Equipment::RepairAll()` → `Write16(entry + kOff_ItemVal_Durability /*0x40*/, 10000)` `src/game/equipment.cpp:2031-2042`, `:1823-1841`, `:1839`, `:1939` |
| PC4 | `"No Fall Damage"` | `ui::Toggle(..., &st.noFallDamage, ...)` `src/gui/menu.cpp:93-94` | `st.noFallDamage`, **default `true`** `src/core/state.h:48` | `hkDamageApply` zeroes any negative HP delta attributed to the player with a non-enemy source `src/game/player.cpp:762-767` |
| PC5 | `"Infinite Stamina & Mount"` | `ui::Toggle(..., &st.infStamina, ...)` `src/gui/menu.cpp:95-100`; sets `st.infMountStamina = st.infStamina` `:98` | `st.infStamina` + `st.infMountStamina` `src/core/state.h:49-50` | stat-commit override `src/game/player.cpp:586`; per-frame pins `:873-880`; zeroed stamina drain `:775-782` |
| PC6 | `"Infinite Spirit"` | `ui::Toggle(..., &st.infSpirit, ...)` `src/gui/menu.cpp:101-102` | `st.infSpirit` `src/core/state.h:51` | stat-commit override `src/game/player.cpp:587`; per-frame pins `:881-885`; zeroed spirit drain `:784-787` |
| PC7 | `"No Bounty"` | `ui::Toggle(..., &st.noBounty, ...)` `src/gui/menu.cpp:103-108` → `game::Inventory::SetNoBounty(st.noBounty)` `:106` | `st.noBounty` `src/core/state.h:52` | `Inventory::SetNoBounty` `src/game/inventory.cpp:5681-5755` writes `WantedInfo+0x18` to 0 per row `:5740`; plus two hooks installed by `Inventory::Install` with `"world: …"` contexts `src/game/inventory.cpp:2066-2074`. Applied/refreshed from `World::Tick()` `src/game/world.cpp:611-630` |
| PC8 | `"Outgoing Damage"` slider (0.00–20.00×, step 0.25, default 1.00×) | `ui::FloatOption(..., &st.dmgOutMult, 0.0f, 20.0f, 0.25f, 1.0f, "%.2fx", ...)` `src/gui/menu.cpp:109-110` | `st.dmgOutMult` `src/core/state.h:56` | `ScaleDamage` outgoing branch `src/game/player.cpp:711-721` |
| PC9 | `"Incoming Damage"` slider (0.00–10.00×, step 0.25, default 1.00×) | `ui::FloatOption(..., &st.dmgInMult, 0.0f, 10.0f, 0.25f, 1.0f, "%.2fx", ...)` `src/gui/menu.cpp:111-112` | `st.dmgInMult` `src/core/state.h:57` | `ScaleDamage` incoming branch `src/game/player.cpp:658-669` |

Adjacent PLAYER-tab item driven by the same subsystem: `"Trust Multiplier"`
`src/gui/menu.cpp:156-159` — see §A.3.

**There is no `"Easy Parry"`, `"Easy Evade"`, `"Perfect Parry"`, `"Just Guard"` or
`"Just Evade"` menu row, state field, or settings key anywhere in `src/`** — see §H.1.

### A.2 WORLD / TIME / WEATHER

Menu tab `WORLD` (`src/gui/menu.cpp:35-36`, dispatched at `:3196`).

| # | Menu feature (label) | C++ entry point(s) | State | Game-side effect |
|---|---|---|---|---|
| WD1 | `"Game Speed"` toggle + multiplier (0.1–5.0×, step 0.05, default 1.0×) | `ui::ToggleFloat(..., &st.gameSpeed, &st.gameSpeedMult, 0.1f, 5.0f, 0.05f, 1.0f, "%.2fx", ...)` `src/gui/menu.cpp:1097-1100`; description gated on `game::World::Ready()` `:1095`, `:1098-1100` | `st.gameSpeed` / `st.gameSpeedMult` `src/core/state.h:109-110` | `hkFrameTimerUpdate` scales the engine's frame delta `src/game/world.cpp:43-76` |
| WD2 | `"Freeze Time of Day"` | `ui::Toggle(..., &st.timeFrozen, ...)` `src/gui/menu.cpp:1103-1106`, gated on `game::World::TimeOfDayReady()` `:1102` | `st.timeFrozen` `src/core/state.h:117` | **two layers**: (1) `hkFieldTimeTick` forces `delta = 0` `src/game/world.cpp:115-121`; (2) `World::Tick` clamps the render manager `lower == upper == g_todTargetHour` `:639-655` |
| WD3 | `"Advance Time (+)"` (1–240 h, default 1) | `ui::IntAction(...)` `src/gui/menu.cpp:1110-1117` → `World::AdvanceTimeOfDayHours(s_advHours)` `:1115` | one-shot (not persisted) `src/core/state.h:116` | `src/game/world.cpp:808-849` — writes both realm clock globals, `g_fieldTimeMgr + 0x2C`, render `+0x3D0`, and the clamp if frozen |
| WD4 | `"Rewind Time (-)"` (1–240 h, default 1) | `ui::IntAction(...)` `src/gui/menu.cpp:1120-1128` → `World::AdvanceTimeOfDayHours(-s_rewHours)` `:1126` | one-shot | same routine with a negative `hours`; `total` is floored at 0 `src/game/world.cpp:820` |
| WD5 | `"Current Time: Day N, HH:MM"` readout | `World::GetCurrentTimeOfDay(&curDay, &curHour, &curMin)` `src/gui/menu.cpp:983` | n/a | reads `g_timeClient + 0x00 / +0x04 / +0x08` `src/game/world.cpp:883-894` |
| WD6 | `"Time of Day Presets"` sub-page | `ui::Submenu(..., "world_time_presets", ...)` `src/gui/menu.cpp:1130`, routed `:3231` | n/a | `RenderTimePresets()` `src/gui/menu.cpp:977-1022` |
| WD7 | `"Dawn / Morning (06:00)"` | `World::SetTimeOfDay(6)` `src/gui/menu.cpp:992` | n/a | `src/game/world.cpp:851-881` |
| WD8 | `"Midday / Noon (12:00)"` | `World::SetTimeOfDay(12)` `src/gui/menu.cpp:998` | n/a | as WD7 |
| WD9 | `"Sunset / Golden Hour (18:00)"` | `World::SetTimeOfDay(18)` `src/gui/menu.cpp:1004` | n/a | as WD7 |
| WD10 | `"Midnight / Night (00:00)"` | `World::SetTimeOfDay(0)` `src/gui/menu.cpp:1010` | n/a | as WD7 |
| WD11 | `"Set Exact Hour"` (0–23, default 12) | `ui::IntAction(...)` + `World::SetTimeOfDay(s_customHour)` `src/gui/menu.cpp:1014-1019` | `s_customHour` (file-static) | as WD7 |
| WD12 | `"Weather & Atmosphere"` sub-page | `ui::Submenu(..., "world_weather", ...)` `src/gui/menu.cpp:1131`, routed `:3232` | n/a | `RenderWeatherAtmosphere()` `src/gui/menu.cpp:1024-1088` |
| WD13 | `"Weather Preset"` combo — `Dynamic (Game Default)`, `Clear Sky (Sunny)`, `Overcast (Cloudy)`, `Rainy (Light Rain)`, `Thunderstorm (Storm)`, `Dense Fog / Mist` | `ui::Combo(...)` `src/gui/menu.cpp:1032-1047` → `World::SetWeatherPreset(wIdx)` `:1044` | `st.weatherPreset` `src/core/state.h:138` | `src/game/world.cpp:896-986` — sets the whole `State` weather block (preset table quoted in §F.4); the actual engine writes happen in the hooks / `World::Tick` |
| WD14 | `"Clear Distant Fog"` | `ui::Toggle(..., &st.clearDistantFog, ...)` `src/gui/menu.cpp:1049` (touches `st` directly; `World::SetClearDistantFog` `src/game/world.cpp:988-993` is only called by WD15) | `st.clearDistantFog` `src/core/state.h:136` | `hkWindPack` `packedOut[0x11] = packedOut[0x17] = 0` `src/game/world.cpp:296-300`; `World::Tick` `CN::FOG_A`/`CN::FOG_B` = 0 `:675-679` |
| WD15 | `"Instant Clear Sky"` (labelled `"Instant Clear Weather"`) | `ui::Option(...)` `src/gui/menu.cpp:1052-1061` | n/a | forces `forceClearSky = clearDistantFog = true`, `weatherPreset = 1`, then calls `SetWeatherPreset(1)` + `SetClearDistantFog(true)` `:1054-1058` |
| WD16 | `"Force Clear Sky"` | `ui::Toggle(..., &st.forceClearSky, ...)` `src/gui/menu.cpp:1050` | `st.forceClearSky` `src/core/state.h:120` | rain/snow/dust hooks return 0 `src/game/world.cpp:187`, `:213`, `:236`; `hkWindPack` zeroes cloud amount + both fog params `:288-294`; `World::Tick` zeroes five cloud-node fields `:686-693` |
| WD17 | `"Rain Intensity"` (0–5, step 0.10, default 0) | `ui::FloatOption(..., &st.rainIntensity, 0.0f, 5.0f, 0.10f, 0.0f, "%.2f", ...)` `src/gui/menu.cpp:1064` | `st.rainIntensity` `src/core/state.h:121` | `hkGetRainIntensity` `src/game/world.cpp:188`; `World::Tick` writes `CN::STORM_THRESH` + `CN::CLOUD_THICK` `:696-700`; storm grading in `hkWindPack` `:315-332` |
| WD18 | `"Snow Intensity"` (0–5, step 0.10, default 0) | `ui::FloatOption(..., &st.snowIntensity, ...)` `src/gui/menu.cpp:1065` | `st.snowIntensity` `src/core/state.h:122` | `hkGetSnowIntensity` `src/game/world.cpp:214` — **no preset sets this field**; not touched by `World::Tick` either |
| WD19 | `"Dust / Sandstorm"` (0–5, step 0.10, default 0) | `ui::FloatOption(..., &st.dustIntensity, ...)` `src/gui/menu.cpp:1066` | `st.dustIntensity` `src/core/state.h:123` | `hkGetDustIntensity` returns `dustIntensity * 15.0f * windMultiplier` `src/game/world.cpp:237-241`; `World::Tick` writes `CN::DUST_BASE` and `CN::DUST_THRESH` `:701-705` |
| WD20 | `"Cloud Thickness"` (0–5, step 0.10, default 1) | `src/gui/menu.cpp:1069` | `st.cloudThick` `src/core/state.h:129` | `hkWindPack` `packedOut[0x1B] / [0x1E] / [0x32] *=` `src/game/world.cpp:307-312`; `World::Tick` `CN::CLOUD_THICK` `:706-709` |
| WD21 | `"Cloud Top Altitude"` (0.1–3, step 0.05, default 1) | `src/gui/menu.cpp:1070` | `st.cloudTop` `src/core/state.h:130` | `hkWindPack` `packedOut[0x2F] *=` `src/game/world.cpp:334-337`; `World::Tick` `CN::CLOUD_TOP = cloudTop * 0.001f` `:710-713` |
| WD22 | `"Cloud Base Altitude"` (0.1–3, step 0.05, default 1) | `src/gui/menu.cpp:1071` | `st.cloudBase` `src/core/state.h:131` | `hkWindPack` `packedOut[0x30] *=` `src/game/world.cpp:339-342`; `World::Tick` `CN::CLOUD_BASE = cloudBase * 0.001f` `:714-717` |
| WD23 | `"Cloud Drift Speed"` (0–5, step 0.1, default 1) | `src/gui/menu.cpp:1072` | `st.cloudScrollSpeed` `src/core/state.h:132` | `hkWindPack` `packedOut[0x23] / [0x24] *=` `src/game/world.cpp:349-353`; `World::Tick` `WN::CLOUD_SCROLL_X/Z` `:745-749` |
| WD24 | `"Fog Scattering (A)"` (0–5, step 0.10, default 1) | `src/gui/menu.cpp:1075` | `st.fogA` `src/core/state.h:134` | `hkWindPack` `packedOut[0x11] *= fogA` `src/game/world.cpp:303`; `World::Tick` `CN::FOG_A = fogA` `:682` |
| WD25 | `"Fog Horizon Blend (B)"` (0–5, step 0.10, default 1) | `src/gui/menu.cpp:1076` | `st.fogB` `src/core/state.h:135` | `hkWindPack` `packedOut[0x17] *= fogB` `src/game/world.cpp:304`; `World::Tick` `CN::FOG_B = fogB` `:683` |
| WD26 | `"Wind Speed Multiplier"` (0–5, step 0.1, default 1) | `src/gui/menu.cpp:1079` | `st.windMultiplier` `src/core/state.h:124` | `hkGetDustIntensity` scaling `src/game/world.cpp:239`, `:249-252`; `World::Tick` `WN::SPEED = mult * 2.0f` and `CN::DUST_WIND_SCALE = mult` `:732-736` |
| WD27 | `"Wind Gust Strength"` (0–3, step 0.1, default 1) | `src/gui/menu.cpp:1080` | `st.windGust` `src/core/state.h:125` | `World::Tick` `WN::GUST = gust * 1.5f` `src/game/world.cpp:737-740` |
| WD28 | `"Turbulence Lift"` (0–3, step 0.1, default 1) | `src/gui/menu.cpp:1081` | `st.windTurbLift` `src/core/state.h:126` | `World::Tick` `WN::TURB_LIFT = turbLift` `src/game/world.cpp:741-744` |
| WD29 | `"No Wind"` | `ui::Toggle(..., &st.noWind, ...)` `src/gui/menu.cpp:1082` | `st.noWind` `src/core/state.h:127` | `hkGetDustIntensity` returns 0 `src/game/world.cpp:236`; `hkWindPack` zeroes `packedOut[0x23] / [0x24]` `:344-348`; `World::Tick` zeroes `WN::SPEED / GUST / TURB_LIFT` + `CN::DUST_WIND_SCALE` `:723-729` |

`st.weatherPreset`, `st.forceClearSky`, `st.clearDistantFog`, `st.rainIntensity`,
`st.snowIntensity`, `st.dustIntensity`, `st.windMultiplier`, `st.windGust`, `st.windTurbLift`,
`st.noWind`, `st.cloudThick`, `st.cloudTop`, `st.cloudBase`, `st.cloudScrollSpeed`, `st.fogA`,
`st.fogB` are all persisted (`src/core/settings.cpp:105-120`), as are `gameSpeed`,
`gameSpeedMult` (`:89-90`) and `timeFrozen` (`:91`).

### A.3 PET / MOUNT

There is **no pet-action menu** (no summon / dismiss / feed / pet-command row anywhere in
`src/gui/menu.cpp`). The pet/mount surface is exactly four things:

| # | Menu feature (label) | C++ entry point(s) | State | Game-side effect |
|---|---|---|---|---|
| PM1 | `"Infinite Stamina & Mount"` (mount half) | `src/gui/menu.cpp:95-100`; `st.infMountStamina` is only ever set as a mirror of `st.infStamina` `:98` (and at load: `src/core/settings.cpp:232`) | `st.infMountStamina` `src/core/state.h:50` | mount stamina entries discovered per tick `src/game/player.cpp:471-514`; stat-commit override `:586`; per-frame pins `:555-562`, `:873-880`; zeroed mount stamina drain `:779-782` |
| PM2 | `"God Mode"` (mount half) | `src/gui/menu.cpp:87-90` | `st.godMode` | `ShouldBlockPlayerDamage(st.godMode, isPlayerTarget, isMountTarget)` → mount damage set to 0 `src/game/player_logic.cpp:5-9`, consumer `src/game/player.cpp:758-761` |
| PM3 | `"Trust Multiplier"` (feeding / taming animals and mounts) | `ui::ToggleFloat(..., &st.trustMult, &st.trustMultVal, 1.0f, 25.0f, 0.25f, 3.0f, "%.2fx", ...)` `src/gui/menu.cpp:156-159`; description gated on `game::Friendly::Ready()` `:157-159` | `st.trustMult` / `st.trustMultVal` `src/core/state.h:102-103` | four hooks on the pet and NPC relationship setters/getters `src/game/friendly.cpp:237-300`; record rewrite `Write64(r + kOff_FriendlyRec_Value /*0x20*/, newValue)` `:153`, `:194` |
| PM4 | `"Mount, Mecha & Vehicle Gear"` restore page (INVENTORY tab) | `ui::Submenu(LOC("7. Mount, Mecha & Vehicle Gear"), "invrestore_mount", ...)` `src/gui/menu.cpp:2550-2551`; page `RenderRestoreMount()` `:2522-2525`; routed `:3225` | static item-key list `kRestoreMountKeys` `src/gui/menu.cpp:2369-2377` | inventory-side only (item grants); **no mount-specific game function is touched** |

Public API declared for mounts but **unused by any consumer in `src/`**:
`Player::GetMountActor` `src/game/player.cpp:968-972`, `Player::GetMountOwner` `:974-978`,
`Player::GetTrackedMountCount` `:980-983` (declarations `src/game/player.h:60-62`). See §H.5.

### A.4 Features explicitly *not* present in these areas

* No `Easy Parry` / `Easy Evade` / `Just Guard` / `Just Evade` (no menu row, no state field, no
  settings key) — §H.1.
* No sprint/walk/climb/dodge speed feature (movement, out of scope here; see the note at
  `src/game/offsets.h:115-119`: writing the type-30/type-74 "rate" stat entries "has NO effect on
  locomotion").
* No pet summon / pet command / mount spawn feature.
* No "damage multiplier for a specific element/weapon" — only the two global signed-delta
  multipliers PC8/PC9.
* `kSig_JustCore` and `kSig_JustCore_Alt` (`src/game/offsets.h:181-184`) exist as alternates for
  the Just-Guard/Just-Evade entry point but have **no consumer anywhere in `src/`**.

---

## B. Game-side function contracts

Every place Trinity touches the game executable in these three areas.

### B.1 PLAYER / COMBAT (17 rows)

| Trinity role | Game function or address expression | How the address is obtained | Exact ABI Trinity assumes | Object/structure it operates on | Structure offsets read or written | Source file:line | Fail-safe if the address/locator is missing |
|---|---|---|---|---|---|---|---|
| C1 | memory read (locator resolution, no call) | gameplay-character-manager global — 6 independent call-site anchors in `kCharMgrAnchors[]`, each followed by `mov rcx,[reg]` feeding the char-manager API | `mem::FindPattern(a.sig)` per anchor, then `mem::ResolveRipAt(m + a.movOff, 7)` (7-byte instruction); majority vote across distinct resolved values | the manager global slot (`qword_61830F8` in the current dump; `qword_6181090` pre-update — comment only) | reads: none at resolution time | `src/game/player.cpp:65-103`; anchors `src/game/offsets.h:229-255`; `ResolveRipAt` `src/mem/scanner.h:69-73` | no anchor matches → `g_charMgrGlobal = 0`; `LOG_ERR("player: char-manager global NOT FOUND (no anchor matched) - God Mode / Infinite Stamina / Infinite Spirit limited to the current-character fallback.")` `src/game/player.cpp:814-816`; `TickResolveSelf` routes to `TickResolveCurrentPlayerFallback` `:385-389` |
| C2 | memory read (pointer chain) | `g_charMgrGlobal` → `*(slot)` → `*(P)` = manager → `manager+0xB8` data / `+0xC0` count | `Read64`, `Read32` (`kOff_CharMgr_ListData = 0xB8`, `kOff_CharMgr_ListCount = 0xC0`, sanity bound `kCharList_MaxCount = 8192`) | the manager's `pa` vector of `character*` | read `+0xB8` (data ptr), `+0xC0` (u32 count); element *i* = `data + 8*i` | `src/game/player.cpp:390-399`; consts `src/game/offsets.h:262-264` | any hop `< kMinPointer` or `count == 0 \|\| count > 8192` → `return` without resolving (sets keep their previous values) `:391-399` |
| C3 | memory read (per-character walk) | owner → actor → status marker → vital/target owner → stat array | `mem::Read64` at each hop; validated by type-checking entry 0 as Health | a gameplay character ("owner", documented vtable `0x50B9A10`) | read `owner+0x68` (`kOff_Owner_Actor`) → `actor+0x20` (`kOff_Actor_StatusMarker`) → `marker+0x18` (`kOff_Marker_TargetOwner`) → `root+0x58` (`kOff_Root_StatArray`) | `src/game/player.cpp:238-255` (`WalkSelfChain`); consts `src/game/offsets.h:43-45`, `:189`, `:303` | any hop `< kMinPointer`, or entry 0 not type `StatType_Health (0)` → `WalkSelfChain` returns false, that character is skipped `:241-249` |
| C4 | memory read (class gate) | engine local-player accessor's predicate (IDB `sub_2393AA0` / `sub_30DF50`) reproduced as `IsPlayerClass` | `Read64(owner + 0x88)` then `Read8(td + 1)`; test `((tag - 1) & 0xF7) == 0` | the character's type descriptor | read `owner+0x88` (`kOff_Owner_TypeDesc`) → tag byte at `+1` | `src/game/player.cpp:222-228`; const `src/game/offsets.h:294`; rationale `:288-293` | read failure → false (character not considered player-class) `:226-227` |
| C5 | memory read (vtable identity) | the protagonist class vtable, taken from any player-class character in the vector | `Read64(ch)` (the owner's vtable) | character object | read `owner+0x00` (vtable); compared for equality against `anchorVt` `src/game/player.cpp:433` | `src/game/player.cpp:401-421`, `:426-433` | no player-class character found → `ClearPlayerSets()` and `return` `:417-421` |
| C6 | memory read (stat-entry typing) | stat entry `+0x00` int32 type id | `Read32(entry + kOff_StatEntry_Type)`; `PlausibleStatType(t)` = `t >= 0 && t < 256` | 0x90-byte stat entry | read `+0x00` (`kOff_StatEntry_Type = 0x00`); stride `kSizeof_StatEntry = 0x90`; scan `kStatArray_ScanEntries = 64` slots from Health | `src/game/player.cpp:160-166`, `:169`, `:362-371`, `:452-468`, `:502-510`; consts `src/game/offsets.h:72-88` | any type read failure or implausible type → `break` out of the scan `:366`, `:456-457`, `:506-507` |
| C7 | memory write (stat pin) | `PinEntry(uintptr_t e)` — pure memory writes, no game call | `Write64(e + kOff_StatEntry_Current, full)`, `Write64(e + kOff_StatEntry_Norm, full - base)` | 0x90-byte stat entry | **writes** `+0x08` current, `+0x20` normalized; **reads** `+0x18` base, `+0x30` cap, `+0x08` current | `src/game/player.cpp:197-210`; consts `src/game/offsets.h:73-77` | `e < kMinPointer` → return `:199`; `base > 1e9` or `cap > 1e9` → return `:204`; `full == 0` → return `:207` |
| C8 | **hook** (MinHook detour) — *conditionally installed* | stat-commit funnel `pa_StatCommit` (IDB `sub_BED7820`) — `kSig_StatCommit` = `"48 89 5C 24 10 55 56 57 48 83 EC 20 48 8B 59 18 41 0F B7 E9 48 03 59 20 48 89 D6 48 89 CF 4C 39 C3"` | `mem::InstallHook("player: stat-commit", kSig_StatCommit, "direct write guard unavailable; current-character pins remain active", …)` → `FindPattern` + `CountMatches(…, 8)` + `MH_CreateHook` + `MH_EnableHook`. **Skipped entirely** when `core::UsesTu201CompatibleRevision(core::GetGameVersion().revision)` | `int64_t __fastcall hkStatCommit(void* entry /*rcx*/, int64_t time /*rdx*/, int64_t target /*r8*/, uint16_t flag /*r9w*/)`; returns `fullTarget` when locked, else the trampoline's `int64_t` in RAX | stat entry | **reads** `+0x18` base, `+0x30` cap, `+0x08` current; **writes** via `PinEntry` `+0x08`/`+0x20` | `src/game/player.cpp:141-146` (typedef), `:575-611` (body), `:821-830` (install); sig `src/game/offsets.h:145-146`; ABI doc `:123-144`; installer `src/mem/hooks.h:26-65` | revision gate → `LOG_OK("player: modern continuous stat-pin guard active (all resolved characters).")` `:823`; signature failure → `LOG_ERR("player: stat-commit signature NOT FOUND - direct write guard unavailable; current-character pins remain active.")` via `src/mem/hooks.h:35` (`:827-829`) |
| C9 | **hook** (MinHook detour, primary + alt signature) | damage-apply dispatcher `pa_StatApplyDelta` (IDB `sub_145B2A0`) — `kSig_DamageApply` = `"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9"`, fallback `kSig_DamageApply_Alt` = `"48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9"` | `mem::InstallHook("player: damage-apply", kSig_DamageApply, "", …)`; the **primary** call's `consequence` is `""`, which suppresses the NOT-FOUND log (`src/mem/hooks.h:33-36`); fallback `mem::InstallHook("player: damage-apply (alt)", kSig_DamageApply_Alt, "damage multipliers disabled", …)` | `int64_t __fastcall hkDamageApply(void* targetOwner /*rcx*/, uint16_t statusId /*rdx*/, int64_t time /*r8*/, int64_t delta /*r9*/, uintptr_t sourceCtx /*stack*/, char a6, char a7, char a8, char a9, char a10, void* out /*stack*/)`; tail-calls the trampoline and forwards its `int64_t` | victim vital-owner + attacker context + `out` buffer | **reads** `sourceCtx+0x68` actor, `sourceCtx+0x20` marker, `sourceCtx+0xA0` possessor, `sourceCtx + kOff_Container_Sub (0x68)`; classification helpers read the cached player/mount sets | `src/game/player.cpp:148-157` (typedef), `:613-792` (body), `:832-849` (install); sigs `src/game/offsets.h:167-170`; ABI doc `:148-166`; `kOff_Container_Sub` `src/game/offsets.h:748` | both signatures fail → `LOG_ERR("player: damage-apply signature NOT FOUND (tried primary + alt) - infinite stamina drain block disabled.")` `:843` — this also silently disables God Mode's damage zeroing, No Fall Damage, both multipliers and mount immunity, none of which are named in that message |
| C10 | **hook** (MinHook detour) — **installed but inert** | combat timing / hitbox evaluator `sub_1407219c0` — `kSig_CombatTimingEval` = `"48 8B C4 41 55 41 56 41 57 48 83 EC 70 C5 78 29 40 A8"` | `mem::InstallHook("player: combat-timing", kSig_CombatTimingEval, "Easy Parry & Easy Evade helper timing disabled", …)` | `bool __fastcall hkCombatTimingEval(void* combatComp /*rcx*/, void* hitData /*rdx*/, float distance /*xmm2*/, uint8_t isGuardMode /*r9b*/, void* outResult /*stack, arg5*/)`. The body calls the trampoline (if non-null) and **returns `orig` unchanged**; nothing is written to `outResult` | combat component / hit data | **none** — the callback reads no offsets and writes nothing | `src/game/player.cpp:794-805`, install `:851-857`; sig `src/game/offsets.h:172-176` | signature failure → `LOG_ERR("player: combat-timing signature NOT FOUND - Easy Parry & Easy Evade helper timing disabled.")` via `src/mem/hooks.h:35` (`:852-854`). Note the hook's `orig` result is `false` when the trampoline pointer is null `:801` |
| C11 | memory read (fallback player owner) | `Inventory::ClientCharacterAddr()` (inventory subsystem), then the C3 walk | cross-subsystem call `src/game/player.cpp:344`; implementation `src/game/inventory.cpp:3388-3400` → `ResolveClientContainer()` `:1209-1224` → `IsLiveCharacter()` `:1325-1333` | live client-realm player character (the inventory "container") | `ResolveClientContainer` reads `g_holder+8`, else `*(g_coreGlobal)` → `+0x30` → `+0x50`; `IsLiveCharacter` reads `c+0xA0` → `+0xD0` and requires `pawn == c` | `src/game/player.cpp:339-381`; `src/game/inventory.cpp:1209-1224`, `:1325-1333`, `:3388-3400`; offsets `src/game/offsets.h:285-286`, `:746-748` | `owner < kMinPointer` or chain failure → `ClearPlayerSets()` + `return` `:341-346`; success logs `LOG_OK("player: current-character fallback resolved @ %p (active player only).")` once `:348-350` |
| C12 | memory read/write (mount discovery) | character-manager vector, filtered by the type-descriptor tag | inside `TickResolveSelf`; `for (uint8_t wantedTag : { Obj_Vehicle (5), Obj_Pet (6) })`; `WalkMountVitalChain` (which tolerates a missing `owner+0x68` actor by falling back to `owner` itself, `:261-262`) | vehicle/pet character objects and their stat arrays | read `owner+0x88` → tag at `+1`; `owner+0x68` → `actor+0x20` → `marker+0x18` → `root+0x58`; scan `mountStatArray + 0x90*k` for `k = 0..63` | `src/game/player.cpp:471-514`, `:258-273`; consts `src/game/offsets.h:47-57`, `:189`, `:294`, `:303` | tag mismatch or chain failure → that candidate is skipped; no log |
| C13 | memory read (readiness probe, install-time) | `kSig_DamageApply_Alt` and `kSig_CombatTimingEval` **presence only** | `trinity::mem::FindPattern(required[i])` inside `GameplayCodeReady()` | n/a — no hook, no call | none | `src/core/mod.cpp:36-44`, `:70-73` | the probe loops until every required signature matches, up to 180 000 ms; on timeout the mod installs whatever it can and logs `LOG_WARN("Gameplay-code readiness timed out after 180 seconds; installing available hooks only.")` `src/core/mod.cpp:125` |
| C14 | hook (MinHook detour, *owned by `inventory.cpp`, menu label lives in the PLAYER tab*) | crime wanted-state evaluator — `kSig_EvaluateCrimeWantedState` = `"48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 48 8B DA 4C 6B D9 38 41 B0 07"` | `mem::InstallHook("world: evaluate-wanted-state", kSig_EvaluateCrimeWantedState, "Witnessed/Assault crime bypass disabled", …, 0)` (maxMatches = 0 → uniqueness check effectively disabled) | `uint8_t __fastcall hkEvaluateCrimeWantedState(void* wantedMgr /*rcx*/, void* actorCtx /*rdx*/)`; returns the constant `7` (= `eWantedState_None`) when `st.noBounty`, else the trampoline's byte (or `0` when the trampoline is null) | the wanted/crime manager | none | `src/game/inventory.cpp:2028-2041`, install `:2066-2068`; sig `src/game/offsets.h:1182-1184` | signature failure → generic `LOG_ERR("world: evaluate-wanted-state signature NOT FOUND - Witnessed/Assault crime bypass disabled.")` via `src/mem/hooks.h:35` |
| C15 | hook (MinHook detour, revision-gated) | central crime-event dispatcher `sub_141595BC0` — `kSig_RegisterCrimeEvent` = `"48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC C0 00 00 00 4D 8B F0"` | `mem::InstallHook("world: register-crime-event", …, 0)`, only when `core::MayProbeLegacyCrimeEventDispatcherForRevision(revision)` i.e. `revision <= 2850` | `void __fastcall hkRegisterCrimeEvent(void* dispatcher /*rcx*/, const char* eventName /*rdx*/, void* eventData /*r8*/, void* eventContext /*r9*/)`; returns immediately (suppresses the event) when `st.noBounty` | the global crime dispatcher | none | `src/game/inventory.cpp:2043-2059`, install `:2070-2075`; sig `src/game/offsets.h:1186-1190`; gate `src/core/version_mapping.cpp:67-70` | on PE 2944+ the hook is not attempted and the code logs `LOG("world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.", revision)` `src/game/inventory.cpp:2081-2082` |
| C16 | memory write (No-Bounty table rewrite) | `WantedInfo` table rows; `kOff_WantedDef_IncreasePrice = 0x18` (i64) | `Inventory::SetNoBounty(bool)` → `EnsureTablesResolved()` + `FindTableGlobal(kStr_WantedInfoTable)` + `DefForRow`; originals captured per row in `s_origPrice[]` and restored on disable | `WantedInfo` definition rows | **writes** `def + 0x18` (0 when enabling, original i64 when disabling); reads `table + kOff_ItemTable_Count (0x08)` for the row count | `src/game/inventory.cpp:5681-5755` (`:5740` write-enable, `:5744` restore); consts `src/game/offsets.h:1176-1180` | `!Player::Ready() && enable` → returns false without touching anything `:5685-5688`; table not found → `LOG("world: WantedInfo table not found - bounty price left alone.")` `:5705` |
| C17 | memory write (durability, owned by `equipment.cpp`) | equipped-item entries from the client & server equip components | `Equipment::RepairAll()` called every 1500 ms from `Equipment::Tick()` when `st.infDurability && !Inventory::IsTransactionActive()` | equip-component item entries (the same 192-byte `TrItemValue` rows the inventory code edits) | **writes** `entry + kOff_ItemVal_Durability (0x40)` = `10000` (u16); reads `+0x08` typeId, `+0x10` quantity, `+0x00` instanceId | `src/game/equipment.cpp:2025-2042`, `:1823-1841`, `:1839`; consts `src/game/offsets.h:1682-1684`, `:850-851`, `:982` | `!Player::Ready()` → `Equipment::Tick` returns immediately `src/game/equipment.cpp:2027`; per-entry guard rejects empty/dummy/unarmed rows `:1831-1838` |

### B.2 WORLD / TIME / WEATHER (20 rows)

| Trinity role | Game function or address expression | How the address is obtained | Exact ABI Trinity assumes | Object/structure it operates on | Structure offsets read or written | Source file:line | Fail-safe if the address/locator is missing |
|---|---|---|---|---|---|---|---|
| W1 | **hook** (raw `MH_CreateHook`, *not* via `mem::InstallHook`) | the engine's per-frame frame-timer update; body matched by `kSig_FrameTimerBody` = `"48 8B F9 48 8B 51 60 8B 42 64 89 42 60"`, fallback `kSig_FrameTimerBody_Pre201` = `"48 8B F9 48 8B 41 60 C5 FA 10 40 64 C5 FA 11 40 60"`; the **function entry** is found by scanning **backwards** from the body match, `p` from `bodyAddr - 0x10` down to `bodyAddr - 0x80`, for prologue bytes `48 8B C4` | hand-written: `FindPattern` → backward prologue scan → `MH_CreateHook(entry, &hkFrameTimerUpdate, &oFrameTimerUpdate)` + `MH_EnableHook` | `void __fastcall hkFrameTimerUpdate(void* appMgr /*rcx*/)`; the trampoline is called first, then the delta is rewritten; returns `void` | the app/game manager | reads `appMgr + 0x60` (time struct ptr); **writes** `timeStruct + 0x64` (`kOff_TimeStruct_Delta`) and `timeStruct + 0x68` (`kOff_TimeStruct_ScaledDelta`) as f32 bit patterns | `src/game/world.cpp:38-76`, install `:468-511`; sigs + offsets `src/game/offsets.h:1273-1278`; mechanism doc `:1240-1272` | body signature missing → `LOG_ERR("world: FrameTimerBody signature not found - Game Speed disabled.")` `:509`; prologue not found in the 0x80-byte window → `LOG_ERR("world: FrameTimerUpdate prologue not found from body match.")` `:503`; MinHook failure → `LOG_ERR("world: Failed to install FrameTimerUpdate hook.")` `:496` and `g_frameTimerUpdateTarget = nullptr` `:497` |
| W2 | memory write | game-speed multiplier application, inside W1, wrapped in `__try/__except` | `mem::Read32`/`mem::Write32` on the f32 bit patterns; multiplier clamped `Clamp(st.gameSpeedMult, 0.1f, 10.0f)`; applied only when `dt > 0.0001f && dt < 1.0f` | the engine's time struct | **writes** `+0x64` and `+0x68`; **reads** `appMgr+0x60`, `timeStruct+0x64` | `src/game/world.cpp:51-75` | `gameSpeed == false` or `\|mult - 1\| <= 0.01f` → nothing happens `:51`; `appMgr == nullptr` → return `:48`; SEH swallows any fault `:74`. **The menu offers 0.1–5.0× (`src/gui/menu.cpp:1097`) but the code clamps to 0.1–10.0×** |
| W3 | memory read (locator resolution) | master field-clock realm globals — `kSig_FieldTimeRealm` = `"BA ?? 01 00 00 48 8B 08 0F B6 04 0A 84 C0 74 0A C5 FC 10 05 ?? ?? ?? ?? EB 08 C5 FC 10 05 ?? ?? ?? ??"` | `mem::FindPattern(kSig_FieldTimeRealm)`, then **two** `mem::ResolveRipAt` calls: server at `match + 0x10`, client at `match + 0x1A`, each 8 bytes (`kLen_FieldTime_Vmovups`) | the two BSS realm clock globals (`qword_…`) | none at resolution time | `src/game/world.cpp:449-461`; sig + offsets `src/game/offsets.h:1300-1307`; struct layout + discovery recipe `:1280-1299` | either resolved value `< kMinPointer` → both are zeroed and the function returns false; caller logs `LOG_WARN("world: field-clock signature NOT FOUND - Advance Time disabled.")` `:517` and sets `ok = false` `:519` |
| W4 | **hook** (MinHook detour, primary + pre-2.01 signature) | per-frame field-time tick `sub_871360` — `kSig_FieldTimeTick` = `"48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 64 24 20 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C"`, fallback `kSig_FieldTimeTick_Pre201` = `"48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C"` | `mem::InstallHook("world: field-time tick", kSig_FieldTimeTick, "", …)`; fallback `mem::InstallHook("world: field-time tick (pre-2.01)", kSig_FieldTimeTick_Pre201, "Freeze Time of Day disabled", …)` | `void __fastcall hkFieldTimeTick(void* mgr /*rcx*/, float delta /*xmm1*/, float d2 /*xmm2*/)`. The `float` parameter exists solely so XMM1 survives the trampoline (`:106-109`) | the field-time manager | **writes** nothing; publishes `g_fieldTimeMgr = mgr` `:117`, and later writers use `g_fieldTimeMgr + 0x2C` (WD3/WD7, `:828`, `:865`) | `src/game/world.cpp:99-121`, install `:526-533`; sigs `src/game/offsets.h:1314-1338` | both signatures fail → `ok = false` `:532`; the first call passes `consequence = ""` so **no NOT-FOUND line is emitted for the primary** (`src/mem/hooks.h:33-36`); the fallback emits `"world: field-time tick (pre-2.01) signature NOT FOUND - Freeze Time of Day disabled."` |
| W5 | memory read (locator resolution) | engine-object global `qword_648F688` (engine-console registrar store) — `kSig_TodEngineGlobal` = `"83 3D ?? ?? ?? ?? FF 75 ?? 48 89 1D ?? ?? ?? ?? 48 89 3D ?? ?? ?? ?? 44 89"` | `mem::FindPattern`, then **uniqueness check** `mem::CountMatches(kSig_TodEngineGlobal, 2) != 1`, then `mem::ResolveRipAt(g + kOff_TodEngineGlobal_Mov /*9*/, kLen_TodEngineGlobal_Mov /*7*/)` | the engine global | none | `src/game/world.cpp:538-562`; sig/offsets `src/game/offsets.h:1370-1374`; derivation `:1357-1369` | signature missing → `LOG_WARN("world: TOD engine-global signature NOT FOUND - sun freeze disabled.")` `:542`; ambiguous → `LOG_WARN("world: TOD engine-global signature ambiguous - sun freeze disabled.")` `:547`; resolved out of range → `LOG_ERR("world: TOD engine-global resolved out of range - sun freeze disabled.")` `:557`; all three set `g_todEngineGlobal = 0` and `ok = false` |
| W6 | memory read (pointer chain) | `ResolveTodManager()` — `engine = *g_todEngineGlobal`, `manager = *(engine + 0x2F8)` | `mem::ReadPtr` twice | the render "TimeOfDay" manager (engine vtable slot 8 getter `sub_36341F0` = `return *(engine + 0x2F8)`, comment only) | read `engine + 0x2F8` (`kOff_Tod_Manager`) | `src/game/world.cpp:139-152`; const `src/game/offsets.h:1375`; chain doc `:1357-1364` | returns 0 (callers no-op) when `g_todEngineGlobal == 0` or either hop is `< kMinPointer` `:144-151` |
| W7 | memory write (freeze the visible sun) | render manager clamp — `lower`/`upper` limits | inside `World::Tick()`, once per captured enable and then every tick while `st.timeFrozen && mgr`; originals captured once into `g_todOrigLower`/`g_todOrigUpper` (`:645-646`) | render TimeOfDay manager | **writes** `mgr + 0x3D4` (`kOff_Tod_LowerLimit`) and `mgr + 0x3D8` (`kOff_Tod_UpperLimit`) = `FloatBits(g_todTargetHour)`; **reads** both originals and `mgr + 0x3D0` (`kOff_Tod_CurrentHour`) | `src/game/world.cpp:639-665`; consts `src/game/offsets.h:1376-1378`; rationale `:1340-1356` | `mgr == 0` → the whole block is skipped and `g_todClampApplied` stays false `:640-641`; on disable the originals are written back once `:656-665`; `World::Remove()` restores them again if still applied `:770-779` |
| W8 | memory read/write (`AdvanceTimeOfDayHours`) | field clock globals + `g_fieldTimeMgr + 0x2C` + render manager | `if (!g_timeClient) return false;` then `ReadI32` of day and hour; hour arithmetic carries into the day (`total = day*24 + hour + hours`, floored at 0) | master field-clock struct (client realm read, both written) + field-time manager accumulator + render manager | **reads** `g_timeClient + 0x00` day, `+0x04` hour; **writes** both realm globals `+0x00`/`+0x04` (`WriteClockDayHour` `:162-170`), `g_fieldTimeMgr + 0x2C` (f32 `newHour * 3600`), `renderMgr + 0x3D0`, and if frozen `+0x3D4`/`+0x3D8` | `src/game/world.cpp:808-849`, `:162-170`; field offsets `src/game/offsets.h:1309-1312`; accumulator doc `:1314-1322` | `g_timeClient == 0` → return false `:810`; either `ReadI32` fails → return false `:813-814`. **Bug note:** the minute field is written by nobody but is read at `+0x08` in W10 — advancing hours does **not** touch min/sec |
| W9 | memory read/write (`SetTimeOfDay`) | same three objects as W8 | `if (!g_timeClient) return false;` reads only `day`; `targetHour` normalised to `0..23` by `%`/`+24` | as W8 | **reads** `g_timeClient + 0x00`; **writes** both realm globals `+0x00` (unchanged day) / `+0x04`, `g_fieldTimeMgr + 0x2C`, `renderMgr + 0x3D0`, and the clamp pair when frozen | `src/game/world.cpp:851-881` | `g_timeClient == 0` → false `:853`; day read failure → false `:856`. **Asymmetry:** unlike W8, this path does not update `g_todTargetHour` when `renderMgr == 0` |
| W10 | memory read (`GetCurrentTimeOfDay`) | field-clock global | `ReadI32` ×3 | master field-clock struct (client realm) | **reads** `g_timeClient + 0x00` day, `+0x04` hour, `+0x08` minute (hard-coded literal `0x08`, **not** `kOff_FieldTime_Min`) | `src/game/world.cpp:883-894` (`:889`) | `g_timeClient == 0` → false `:885`; day/hour read failure → false `:887-888`; minute read failure is ignored (`m` keeps 0) `:889` |
| W11 | **hook** (MinHook detour) | rain intensity getter — `kSig_WeatherRain` = `"48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 6C 01 00 00"` | `mem::InstallHook("world: rain intensity", kSig_WeatherRain, "Rain control disabled", …)`. **Return value ignored** (`src/game/world.cpp:565-567`) | `__m128 __fastcall hkGetRainIntensity(void* ws /*rcx*/)`; the f32 result rides **XMM0** (`_mm_set_ss`). The trampoline's `__m128` is returned directly | weather state object | none read/written by Trinity; `ws` is only null/range-checked | `src/game/world.cpp:172-205`, install `:565-567`; sig `src/game/offsets.h:1382-1383` | null/`< kMinPointer` `ws` → returns `0.0f` `:183-184`; a trampoline fault is caught and returns `0.0f` `:199-202`; a missing signature only logs via `src/mem/hooks.h:35` — the feature silently becomes "rain is always 0" if the detour still installed (it cannot, if the pattern never matched) |
| W12 | **hook** (MinHook detour) | snow intensity getter — `kSig_WeatherSnow` = `"48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 68 01 00 00"` | `mem::InstallHook("world: snow intensity", kSig_WeatherSnow, "Snow control disabled", …)`; return value ignored | `__m128 __fastcall hkGetSnowIntensity(void* ws)` | weather state object | none | `src/game/world.cpp:207-228`, install `:568-570`; sig `src/game/offsets.h:1385-1386` | as W11. Note: **no weather preset writes `st.snowIntensity`**, so this hook's only input is the manual slider |
| W13 | **hook** (MinHook detour) | dust intensity getter — `kSig_WeatherDust` = `"48 8B 41 ?? 41 B8 40 00 00 00 48 85 C0 41 B9 60 01 00 00 48 8D 50 18 B8 CC 01 00 00 49 0F 44 D0"` | `mem::InstallHook("world: dust intensity", kSig_WeatherDust, "Dust control disabled", …)`; return value ignored | `__m128 __fastcall hkGetDustIntensity(void* ws)` | weather state object | none | `src/game/world.cpp:230-261`, install `:571-573`; sig `src/game/offsets.h:1388-1389` | as W11; the multiplier path returns `dustIntensity * 15.0f * windMultiplier` `:240` |
| W14 | **hook** (MinHook detour, primary + pre-2.01) | cloud/fog/wind shader parameter pack — `kSig_WindPack` = `"48 89 5C 24 08 57 48 83 EC 20 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1"`, fallback `kSig_WindPack_Pre201` = same with `48 83 EC 30` | `mem::InstallHook("world: wind pack", kSig_WindPack, nullptr, …)` then `mem::InstallHook("world: wind pack (pre-2.01)", kSig_WindPack_Pre201, "Cloud and Fog control disabled", …)`. The primary passes `consequence = nullptr` → no NOT-FOUND log | `void __fastcall hkWindPack(void* windNodePtr /*rcx*/, float* packedOut /*rdx*/)`; the trampoline is called first (inside `__try`), then the pack is edited **in place** by index | the packed shader parameter block (a `float[]`) | **writes** indices `0x00` (sun light), `0x05` (moon light), `0x11` (fog density / `CN::FOG_A`), `0x17` (`CN::FOG_B`), `0x1B` (cloud amount / `CN::CLOUD_THICK`), `0x1E` (cloud alpha / `CN::CLOUD_BASE`), `0x20` (cloud scattering), `0x23`/`0x24` (cloud scroll / `WN::CLOUD_SCROLL_X`/`_Z`), `0x2F` (`CN::CLOUD_TOP`), `0x30` (`CN::CLOUD_BASE`), `0x32` (`CN::DUST_ADD`) — **all by raw literal index, never by a named constant** | `src/game/world.cpp:263-363`, install `:574-580`; sigs `src/game/offsets.h:1391-1396` | `packedOut == nullptr` or `< kMinPointer` → return without writing `:282-283`; every write is inside one `__try/__except` that swallows faults `:285-362`; a trampoline fault returns early without editing the pack `:272-279` |
| W15 | memory read (locator resolution, "zero hooks") | `EnvManager` global — `kSig_EnvManager` = `"48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 60 C5 78 2F C7 72 ?? 48 8B 88 E0 0E 00 00 E8 62"`, fallback `kSig_EnvManager_Legacy` = `"48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 40 48 8B D7 48 8B 88 E0 0E 00 00"` | `mem::FindPattern` then `mem::ResolveRipAt(envSig, kLen_EnvManager_Mov /*7*/)`. The comment at `:589-591` records that the match's **first** instruction is the `mov rcx, cs:<pEnvManager>` (7 bytes) and that resolving from `+3` produced garbage on TU 2.00 | the environment manager global | none | `src/game/world.cpp:582-602`; sigs/offsets `src/game/offsets.h:1398-1405` | neither signature matches → `g_pEnvManager` stays 0 and **nothing is logged** (`:587` skips the whole block); resolved `< kMinPointer` → `g_pEnvManager = 0` silently `:598-600`; success → `LOG("world: safe EnvManager pointer resolved: 0x%llX", g_pEnvManager)` `:595` |
| W16 | memory read (multi-hop pointer chain) | `ResolveWeatherEnv()` — "the entity getter is at `vt[0x60 / 8]` (which directly returns `[envMgr + 0x68]`)" | manual `mem::ReadPtr` chain with **two vtable-call fallbacks**: `vt[0x60/8]` then `vt[0x40/8]`, each cast to `uintptr_t(__fastcall*)(uintptr_t)`, guarded by range checks; the whole body is inside `__try/__except` | `g_pEnvManager` → env manager → entity → weather state → result → cloud/wind nodes | read `envMgr + 0x68`; `entity + 0xEF0`, fallback `+0xEE0`, fallback `+0xED8`; `ws + 0x60`, fallback `+0x50`, fallback `+0x20`; `result + 0x18` (cloud node), `result + 0x20` (wind node) | `src/game/world.cpp:366-443` | `g_pEnvManager == 0` → empty `ResolvedWeatherEnv` `:380`; any hop `< kMinPointer` → returns the partially-filled struct with `valid` false `:385-419`; `valid = (cloudNode >= kMinPointer \|\| weatherState >= kMinPointer)` `:436`; an exception resets the whole struct `:438-441` |
| W17 | memory write (live atmosphere injection) | cloud node fields via the `CN::` namespace | inside `World::Tick()`, only when `env.valid`, all inside one `__try/__except` | the environment cloud/flight node | **writes** `env.cloudNode + CN::FOG_A (0x134)`, `DUST_BASE (0x138)`, `CLOUD_TOP (0x13C)`, `CLOUD_THICK (0x140)`, `CLOUD_BASE (0x144)`, `DUST_WIND_SCALE (0x158)`, `DUST_THRESH (0x198)`, `STORM_THRESH (0x19C)`, `FOG_B (0x1A0)` — all f32 bit patterns | `src/game/world.cpp:667-719`; namespace `src/game/offsets.h:1407-1418` | `!env.valid` → the entire block is skipped `:669`; SEH swallows faults `:753-755`. No log line is emitted in either case |
| W18 | memory write (live wind injection) | wind node fields via the `WN::` namespace | as W17 | the environment wind node | **writes** `env.windNode + WN::SPEED (0x88)`, `GUST (0x9C)`, `TURB_LIFT (0x68)`, `CLOUD_SCROLL_X (0xD0)`, `CLOUD_SCROLL_Z (0xD4)` — all f32 bit patterns | `src/game/world.cpp:721-751`; namespace `src/game/offsets.h:1420-1430` | as W17. Note `WN::DIR_X`, `DIR_Z`, `TURB_DENS`, `TURB_SCALE` are declared but **never written** |
| W19 | state-only (no game touch) | `World::SetWeatherPreset(int)` / `World::SetClearDistantFog(bool)` | direct calls from the menu; the functions write `State` fields and log | n/a | none | `src/game/world.cpp:896-993` | always returns `true` `:985`, `:992` — the return value is not a readiness signal |
| W20 | observer (Trinity-internal cross-subsystem call) | `World::Tick()` upkeep of No Bounty | `Player::Ready()` then `game::Inventory::SetNoBounty(st.noBounty)`, re-armed when the toggle changes or the player transitions from not-ready to ready | n/a (delegates to row C16) | none here | `src/game/world.cpp:611-630` | `!curReady` → the upkeep block is skipped and the cached state resets `:626-630` |

### B.3 PET / MOUNT (6 rows)

| Trinity role | Game function or address expression | How the address is obtained | Exact ABI Trinity assumes | Object/structure it operates on | Structure offsets read or written | Source file:line | Fail-safe if the address/locator is missing |
|---|---|---|---|---|---|---|---|
| M1 | memory read (mount discovery) | character-manager vector entries tagged `Obj_Vehicle (5)` / `Obj_Pet (6)` | see C12 | vehicle/pet character objects | read `owner+0x88` → tag at `+1`; `owner+0x68` → `actor+0x20` → `marker+0x18` → `root+0x58`; scan `+0x90*k` for `k = 0..63` | `src/game/player.cpp:471-514`, `:258-273`; tag predicate `src/game/player_logic.cpp:11-14`; `ObjectType` enum `src/game/offsets.h:47-57` | no candidate passes → `g_mountCount = 0`, all mount arrays cleared `:514`, `:530-535`; no log |
| M2 | memory write (mount stat pin) | mount stamina entries (`IsStaminaType` match) | `PinEntry` per entry, every tick while `st.infMountStamina` | mount 0x90-byte stat entries | **writes** `+0x08` / `+0x20`; reads `+0x18`, `+0x30` | `src/game/player.cpp:555-562`, `:873-880`, `:892-898` (also `Player::RefreshSelf` `:892-898`) | `e < kMinPointer` guard inside `PinEntry` `:199` |
| M3 | hook body (mount branch of C9) | mount victim classification inside `hkDamageApply` | `IsMountEntity(owner)` → `IsStrictPlayerTarget(owner)`; `ShouldBlockPlayerDamage` | cached mount identity sets | reads `g_mountTargetOwners`, `g_mountActors`, `g_mountOwners`, `g_mountStamEntries` (linear scans) | `src/game/player.cpp:642-650`, `:734`, `:758-761`, `:779-782`; predicate `src/game/player_logic.cpp:5-9` | empty mount sets → `IsMountEntity` false → mount takes normal damage and drains normally (silent) |
| M4 | **hook** (MinHook detour ×2, raw `MH_CreateHook`) | relationship-record setters — `kSig_FriendlySetPet201` = `"49 89 E3 53 55 56 57 41 56 41 57 48 83 EC 68 48 89 D7 48 89 CE 0F B7 42 04 66 41 89 43 08 49 8D 4B 08 E8 ? ? ? ? 31 ED 39 6E 1C"` (pet/vehicle) and `kSig_FriendlySetNpc201` = `"4C 8B DC 53 55 56 57 41 56 41 57 48 83 EC 68 48 8B FA 48 8B F1 0F B7 42 04"`; TU 2.00 fallbacks `kSig_FriendlySetPet` / `kSig_FriendlySetNpc` | `mem::FindPattern(kSig_FriendlySetPet201)`; if **both** the NPC and pet 2.01 patterns are absent, re-probe with the TU 2.00 pair `src/game/friendly.cpp:239-245`; then a private `CreateAndEnable` (`MH_CreateHook` + `MH_EnableHook`, with `MH_RemoveHook` rollback) `:227-234` | `void* __fastcall hkSetPet(void* mapOwner /*rcx*/, void* record /*rdx*/)` → returns the trampoline's `void*` in RAX | the per-relationship trust map | **writes** `record + kOff_FriendlyRec_Value (0x20)` as u64 `:153`; **reads** `record + 0x00` key, `+0x04` group, `+0x20` value `:126-128` | `src/game/friendly.cpp:204-209`, `:259-269`, install `:237-300`; sigs `src/game/offsets.h:1794-1817`; record layout `:1744-1768`, `:1827-1833` | no setter address at all → `LOG_ERR("friendly: Trust Multiplier setters NOT FOUND - feature disabled.")` `:298` and `Friendly::Ready()` stays false `:350-353`. The menu gates its description on `Friendly::Ready()` `src/gui/menu.cpp:157-159` |
| M5 | **hook** (MinHook detour ×2, raw `MH_CreateHook`) | in-place trust lookups — `kSig_FriendlyGetPet201` = `"48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 3C 00"` and `kSig_FriendlyGetNpc201` = the same bytes ending `83 7F 1C 00`; **no TU 2.00 fallback is probed for the getters** | `mem::FindPattern(kSig_FriendlyGetPet201)` `src/game/friendly.cpp:272` | `void* __fastcall hkGetPet(void* mapOwner /*rcx*/, void* actor /*rdx*/)` → returns the record pointer from the trampoline | the per-relationship trust map | **reads** the returned record's `+0x00` / `+0x04` / `+0x20`; **writes** `+0x20` when it scales `:194` | `src/game/friendly.cpp:219-225`, `:284-294`; sig `src/game/offsets.h:1803-1810` | getter signature missing → that hook is simply not installed, `g_petGetTarget` stays null `:293`; the setters (M4) may still cover the gain |
| M6 | memory read (trust map walk) | `FindStoredTrust` — recovers the pre-write value from the live hash map so the first event after enabling has a baseline | `mem::ReadPtr` / `Read32` / `Read16` / `Read64`; comment records the TU 2.01 layout (`0x100`-byte hash buckets at `+0x48`, node pointers at `+0x50`, `0x68`-stride relationship-record vector at node `+0x08`) | the trust ("Friendly") relationship map | reads `owner + 0x3C` (u32 bucketCount, bounded `1..4096`), `owner + 0x48` (buckets), `owner + 0x50` (nodes); `bucket_i = buckets + i*0x100`; `bucket + 0x00` entryCount (`<= 31`); `bucket + 0x0C + j*8` nodeIndex (`<= 0xFFFF`); `nodes + nodeIndex*8` node; `node + 0x08` records; `node + 0x10` recordCount (`<= 4096`); record `records + i*0x68` `+0x00` key / `+0x04` group / `+0x20` value | `src/game/friendly.cpp:46-116`; consts `src/game/offsets.h:1827-1833` | any read failure or out-of-range bound → returns false; the caller then falls back to its own `s_lastTrustMap` baseline (`SelectTrustBaseline` `:143-146`) or treats the event as a seed |

**Game-side touch-point count: 17 (PLAYER/COMBAT, rows C1–C17) + 20 (WORLD/TIME/WEATHER, W1–W20)
+ 6 (PET/MOUNT, M1–M6) = 43.**

---

## C. Hook table

| # | Hook name (log context) | Target address source | Hook type | Callback signature | What it does | Removed / restored | Source |
|---|---|---|---|---|---|---|---|
| H1 | `"player: stat-commit"` — **not installed on TU 2.01+** | `kSig_StatCommit` → `mem::InstallHook` | MinHook inline detour (`MH_CreateHook` + `MH_EnableHook` via `src/mem/hooks.h:26-65`) | `int64_t __fastcall hkStatCommit(void* entry, int64_t time, int64_t target, uint16_t flag)` | if the entry is a tracked player-HP / stamina / mount-stamina / spirit entry **and** its toggle is on: replaces `target` with `full` before the commit, calls the trampoline, then `PinEntry(e)`; returns `fullTarget` instead of the engine result | `mem::RemoveHook(&g_commitTarget)` `src/game/player.cpp:909` ← `Player::Remove()` ← `Mod::Shutdown` `src/core/mod.cpp:157` | `src/game/player.cpp:575-611`, install `:821-830`, remove `:907-933`; `src/mem/hooks.h:70-76` |
| H2 | `"player: damage-apply"` then `"player: damage-apply (alt)"` | `kSig_DamageApply` → fallback `kSig_DamageApply_Alt` | MinHook inline detour | `int64_t __fastcall hkDamageApply(void* targetOwner, uint16_t statusId, int64_t time, int64_t delta, uintptr_t sourceCtx, char a6, char a7, char a8, char a9, char a10, void* out)` | God-Mode/mount damage zeroing, No-Fall-Damage nullification, incoming/outgoing multipliers, One-Hit-Kill (10000×), stamina & spirit drain zeroing; then tail-calls the trampoline with the rewritten `delta` | `mem::RemoveHook(&g_damageHookTarget)` `src/game/player.cpp:910` | `src/game/player.cpp:726-792`, install `:832-849` |
| H3 | `"player: combat-timing"` — **inert** | `kSig_CombatTimingEval` | MinHook inline detour | `bool __fastcall hkCombatTimingEval(void* combatComp, void* hitData, float distance, uint8_t isGuardMode, void* outResult)` | **nothing.** Calls the trampoline (if non-null) and returns its `bool` unchanged; `outResult` is never written | `mem::RemoveHook(&g_combatTimingTarget)` `src/game/player.cpp:911` | `src/game/player.cpp:794-805`, install `:851-857` |
| H4 | no name; logs `"world: FrameTimerUpdate hook installed @ 0x%p (true game time scale engine control)."` | `kSig_FrameTimerBody` → fallback `kSig_FrameTimerBody_Pre201` → backward prologue scan for `48 8B C4` within `[body-0x80, body-0x10]` | **raw** `MH_CreateHook` + `MH_EnableHook` (does **not** use `mem::InstallHook`) | `void __fastcall hkFrameTimerUpdate(void* appMgr)` | Game Speed: scales `timeStruct+0x64` and `+0x68` by `Clamp(gameSpeedMult, 0.1f, 10.0f)` when `st.gameSpeed` and `\|mult-1\| > 0.01f` | `mem::RemoveHook(&g_frameTimerUpdateTarget)` in `World::Remove()` `src/game/world.cpp:762-766`, then `oFrameTimerUpdate = nullptr` | `src/game/world.cpp:38-76`, install `:468-511`, remove `:761-766` |
| H5 | `"world: field-time tick"` then `"world: field-time tick (pre-2.01)"` | `kSig_FieldTimeTick` → fallback `kSig_FieldTimeTick_Pre201` | MinHook inline detour | `void __fastcall hkFieldTimeTick(void* mgr, float delta, float d2)` | Freeze Time of Day (layer 1): sets `delta = 0` while `st.timeFrozen`, then calls the trampoline; always publishes `g_fieldTimeMgr = mgr` | `mem::RemoveHook(&g_fieldTimeTickTarget)` + `oFieldTimeTick = nullptr` `src/game/world.cpp:793-794` | `src/game/world.cpp:115-121`, install `:526-533`, remove `:792-795` |
| H6 | `"world: rain intensity"` | `kSig_WeatherRain` | MinHook inline detour | `__m128 __fastcall hkGetRainIntensity(void* ws)` | short-circuits to `_mm_set_ss(0.0f)` for `forceClearSky` / preset 1, to `rainIntensity`, or to `1.50f` (preset 3) / `4.00f` (preset 4); otherwise returns the trampoline value inside `__try/__except` | `mem::RemoveHook(&g_rainIntensityTarget)` + `oGetRainIntensity = nullptr` `src/game/world.cpp:783-787` | `src/game/world.cpp:181-205`, install `:565-567`, remove `:782-790` |
| H7 | `"world: snow intensity"` | `kSig_WeatherSnow` | MinHook inline detour | `__m128 __fastcall hkGetSnowIntensity(void* ws)` | short-circuits to 0 for `forceClearSky`, or to `snowIntensity` when `> 0.001f`; otherwise the trampoline value | `mem::RemoveHook(&g_snowIntensityTarget)` `src/game/world.cpp:784` | `src/game/world.cpp:207-228`, install `:568-570` |
| H8 | `"world: dust intensity"` | `kSig_WeatherDust` | MinHook inline detour | `__m128 __fastcall hkGetDustIntensity(void* ws)` | short-circuits to 0 for `forceClearSky \|\| noWind`; otherwise returns `dustIntensity * 15.0f * windMultiplier`, or scales the trampoline value by `windMultiplier` | `mem::RemoveHook(&g_dustIntensityTarget)` `src/game/world.cpp:785` | `src/game/world.cpp:230-261`, install `:571-573` |
| H9 | `"world: wind pack"` then `"world: wind pack (pre-2.01)"` | `kSig_WindPack` → fallback `kSig_WindPack_Pre201` | MinHook inline detour | `void __fastcall hkWindPack(void* windNodePtr, float* packedOut)` | calls the trampoline first (inside `__try`), then edits the packed shader parameter block by raw index: fog/cloud/wind/cloud-scroll, plus storm and overcast grading | `mem::RemoveHook(&g_windPackTarget)` + `oWindPack = nullptr` `src/game/world.cpp:786`, `:790` | `src/game/world.cpp:268-363`, install `:574-580`, remove `:786-790` |
| H10 | `"friendly: NPC Trust Multiplier observer"` | `kSig_FriendlySetNpc201` → fallback `kSig_FriendlySetNpc` (fallback only when **both** 2.01 patterns are absent) | **raw** `MH_CreateHook` + `MH_EnableHook` via `CreateAndEnable` | `void* __fastcall hkSetNpc(void* mapOwner, void* record)` | observes and rescales the incoming record's trust gain, then calls the trampoline | `MH_DisableHook` + `MH_RemoveHook(&g_npcTarget)` in `Friendly::Remove()` `src/game/friendly.cpp:317-322` | `src/game/friendly.cpp:197-202`, install `:247-257`, remove `:315-348` |
| H11 | `"friendly: pet/mount Trust Multiplier observer"` | `kSig_FriendlySetPet201` → fallback `kSig_FriendlySetPet` | raw MinHook detour | `void* __fastcall hkSetPet(void* mapOwner, void* record)` | same as H10, for the pet/vehicle map (feeds/tames) | `MH_DisableHook` + `MH_RemoveHook(&g_petTarget)` `src/game/friendly.cpp:323-328` | `src/game/friendly.cpp:204-209`, install `:259-269` |
| H12 | `"friendly: NPC in-place trust observer"` | `kSig_FriendlyGetNpc201` (**no TU 2.00 fallback**) | raw MinHook detour | `void* __fastcall hkGetNpc(void* mapOwner, void* actor)` | observes records mutated in place by the lookup path (no distinct source record) | `MH_DisableHook` + `MH_RemoveHook(&g_npcGetTarget)` `src/game/friendly.cpp:329-334` | `src/game/friendly.cpp:211-217`, install `:271-283` |
| H13 | `"friendly: pet/mount in-place trust observer"` | `kSig_FriendlyGetPet201` | raw MinHook detour | `void* __fastcall hkGetPet(void* mapOwner, void* actor)` | same as H12 for pet/vehicle | `MH_DisableHook` + `MH_RemoveHook(&g_petGetTarget)` `src/game/friendly.cpp:335-340` | `src/game/friendly.cpp:219-225`, install `:284-294` |
| H14 | `"world: evaluate-wanted-state"` (installed by `Inventory::Install`) | `kSig_EvaluateCrimeWantedState` | MinHook inline detour through `mem::InstallHook`, **`maxMatches = 0`** | `uint8_t __fastcall hkEvaluateCrimeWantedState(void* wantedMgr, void* actorCtx)` | returns `7` (`eWantedState_None`) whenever `st.noBounty`, blocking Witness / Suspect / Assault / Pursuit | **not removed by `Inventory::Remove()`**; only `Player`/`World`/`Friendly` targets are cleared in `Mod::Shutdown` (`src/core/mod.cpp:157-163`) — the question of whether `Inventory::Remove()` covers it is outside this scope | `src/game/inventory.cpp:2032-2041`, install `:2066-2068` |
| H15 | `"world: register-crime-event"` (installed by `Inventory::Install`, **revision ≤ 2850 only**) | `kSig_RegisterCrimeEvent` | MinHook inline detour, `maxMatches = 0` | `void __fastcall hkRegisterCrimeEvent(void* dispatcher, const char* eventName, void* eventData, void* eventContext)` | returns immediately when `st.noBounty`, suppressing Murder / Assault / Theft / Property-Destruction crime events, their UI banner, the minimap wanted circle and guard hostility | as H14 | `src/game/inventory.cpp:2046-2059`, install `:2070-2075`; gate `src/core/version_mapping.cpp:67-70`; rationale `src/game/inventory.cpp:2078-2082` |

`mem::RemoveHook` clears `*target` and is idempotent, so `Remove()` can call it unconditionally
(`src/mem/hooks.h:70-76`). `Player::Remove()` additionally zeroes every cached identity set and
set-array slot (`src/game/player.cpp:912-932`) and resets `g_currentFallbackLogged`
(`:926`). `World::Remove()` also zeroes `g_todEngineGlobal` (`src/game/world.cpp:780`) and both
clock globals (`:795`). `Friendly::Remove()` clears the trust cache and resets all four target
pointers plus the trampolines (`src/game/friendly.cpp:341-347`).

---

## D. Patch table

**There are no byte patches in `player.cpp`, `world.cpp`, `player_logic.cpp` or `friendly.cpp`.**
Every write those files perform is a *data* write to a live engine object or a MinHook
trampoline, never an instruction-stream edit. The only `mem::PatchMemory` consumer in the whole
tree is `src/game/worker.cpp:116`, `:165`, `:186` (the Worker level/ability patch, out of scope).

For completeness, the data writes that a reader might mistake for patches:

| # | Target locator | Value written | Applied when | Reverted | Source |
|---|---|---|---|---|---|
| D1 | stat entry `e + 0x08` and `e + 0x20` | `full` (max of cap/base) and `full - base` | `PinEntry`, driven by `hkStatCommit`, `Tick`, `RefreshSelf`, `TickResolveSelf`, `TickResolveCurrentPlayerFallback` | never — the game re-derives these on its own next write | `src/game/player.cpp:197-210`, `:607-608` |
| D2 | render TOD manager `+0x3D4` / `+0x3D8` | `FloatBits(g_todTargetHour)` | every `World::Tick` while `st.timeFrozen` and the manager resolves | `World::Tick` on disable `src/game/world.cpp:656-665`; `World::Remove()` `:770-779` | `src/game/world.cpp:639-665` |
| D3 | render TOD manager `+0x3D0` | `FloatBits(hour)` | `AdvanceTimeOfDayHours` / `SetTimeOfDay` | never (it is a live clock field) | `src/game/world.cpp:831-842`, `:868-879` |
| D4 | field-time manager `+0x2C` | `FloatBits(hour * 3600)` | `AdvanceTimeOfDayHours` / `SetTimeOfDay`, only if `g_fieldTimeMgr != 0` | never | `src/game/world.cpp:826-829`, `:863-866` |
| D5 | both realm clock globals `+0x00` (day) / `+0x04` (hour) | the computed day/hour | `AdvanceTimeOfDayHours` / `SetTimeOfDay` | never | `src/game/world.cpp:162-170`, `:824`, `:861` |
| D6 | cloud node (`CN::*`) | fog/cloud/dust/storm f32 values | every `World::Tick` while `env.valid` | never — re-written every tick from `State` | `src/game/world.cpp:671-719` |
| D7 | wind node (`WN::*`) | wind speed/gust/lift/cloud-scroll f32 values | every `World::Tick` while `env.valid` | never | `src/game/world.cpp:721-751` |
| D8 | packed shader parameter block (indices `0x00`…`0x32`) | scaled fog/cloud/light values | every call to the hooked wind-pack function | never — recomputed per call | `src/game/world.cpp:285-359` |
| D9 | `WantedInfo` row `def + 0x18` | `0` (enable) / captured original (disable) | `Inventory::SetNoBounty`, driven by the menu and by `World::Tick` | per-row `s_origPrice[]` captured on first enable | `src/game/inventory.cpp:5733-5746` |
| D10 | trust record `record + 0x20` | the scaled u64 trust value | inside `hkSetPet` / `hkSetNpc` / `hkGetPet` / `hkGetNpc` | never — the game re-writes it on its own next update | `src/game/friendly.cpp:153`, `:194` |
| D11 | equip entry `+0x40` (durability) | `10000` | `Equipment::RepairAll` every 1500 ms while `st.infDurability` | never (turning the toggle off simply stops repairing) | `src/game/equipment.cpp:1839`, `:1939`, `:2033-2042` |

---

## E. Pointer chains

Every multi-level dereference used to reach the local player, character status, world/time and
weather objects. `kMinPointer = 0x10000000` is the floor every hop is checked against
(`src/game/offsets.h:27`, enforced by `mem::IsValidUserPtr` `src/mem/safe_memory.h:20-23`).

**E.1 Gameplay-character manager → character vector** (`src/game/player.cpp:390-399`)

```
manager = *( *( g_charMgrGlobal ) )                 // *(slot) then *(P); player.cpp:391-392
data    = *( manager + 0xB8 )                       // kOff_CharMgr_ListData;  player.cpp:393-395
count   =  (u32)*( manager + 0xC0 )                 // kOff_CharMgr_ListCount; player.cpp:396-399
character[i] = *( data + 8*i )                      // player.cpp:408, :430, :482
```

`g_charMgrGlobal` itself is the RIP target of `kCharMgrAnchors[i].sig + movOff`
(`src/game/player.cpp:74-77`; anchors `src/game/offsets.h:235-255`).

**E.2 Character ("owner") → health stat entry** (`WalkSelfChain`, `src/game/player.cpp:238-255`)

```
actor       = *( owner  + 0x68 )   // kOff_Owner_Actor;        player.cpp:241
marker      = *( actor  + 0x20 )   // kOff_Actor_StatusMarker; player.cpp:242
targetOwner = *( marker + 0x18 )   // kOff_Marker_TargetOwner; player.cpp:244
statArray   = *( targetOwner + 0x58 ) // kOff_Root_StatArray;  player.cpp:246
entry[k]    = statArray + 0x90 * k // kSizeof_StatEntry;      player.cpp:364, :454
                                   // entry[0] must type-check as StatType_Health (0)
```

Type read: `*(int32*)(entry + 0x00)` (`kOff_StatEntry_Type`, `src/game/player.cpp:163`).

**E.3 Character → possessor round-trip (identity validation)**
(`src/game/player.cpp:222-228` for the class gate; `src/game/inventory.cpp:1325-1333` for the live check)

```
typeDesc = *( owner + 0x88 ); tag = *(u8*)(typeDesc + 1)   // kOff_Owner_TypeDesc
player-class  <=>  ((tag - 1) & 0xF7) == 0                 // tag 1 (SelfPlayer) or 9 (OtherPlayer)
live character <=>  *( *( owner + 0xA0 ) + 0xD0 ) == owner // kOff_Owner_Possessor / kOff_Possessor_Pawn
```

**E.4 Mount / pet vital chain** (`WalkMountVitalChain`, `src/game/player.cpp:258-273`)

```
actor       = *( owner + 0x68 )  ; if that read fails or is < kMinPointer, actor = owner  // :261-262
marker      = *( actor + 0x20 )                                                           // :263
targetOwner = *( marker + 0x18 )                                                          // :265
statArray   = *( targetOwner + 0x58 )   // NO Health type-check (mounts are not humanoid)  // :267
```

**E.5 TU 2.01 current-character fallback → live client character**
(`Inventory::ClientCharacterAddr`, `src/game/inventory.cpp:3388-3400`)

```
// path A — captured holder
owner = *( g_holder + 8 )                        // inventory.cpp:1215, :3396
        accept only if IsLiveCharacter(owner)
// path B — core global
g       = *( g_coreGlobal )                      // inventory.cpp:1220
mid     = *( g + 0x30 )                          // kOff_Global_Mid;  inventory.cpp:1221
cont    = *( mid + 0x50 )                        // kOff_Mid_Container; inventory.cpp:1222
        accept cont only if IsLiveCharacter(cont)
```

Constants: `src/game/offsets.h:746-748`.

**E.6 Engine → render TimeOfDay manager** (`ResolveTodManager`, `src/game/world.cpp:142-152`)

```
engine  = *( g_todEngineGlobal )        // world.cpp:146
manager = *( engine + 0x2F8 )           // kOff_Tod_Manager; world.cpp:149
```

**E.7 Environment manager → cloud / wind nodes** (`ResolveWeatherEnv`, `src/game/world.cpp:377-443`)

```
envMgr  = *( g_pEnvManager )                                  // :385
vt      = *( void** )envMgr                                   // :388
entity  = *( envMgr + 0x68 )                                  // :396
          fallback: entity = ((uintptr_t(*)(uintptr_t))vt[0x60/8])(envMgr)   // :398-400
          fallback: entity = ((uintptr_t(*)(uintptr_t))vt[0x40/8])(envMgr)   // :403-405
ws      = *( entity + 0xEF0 )                                 // :414
          fallback: *( entity + 0xEE0 )                       // :416
          fallback: *( entity + 0xED8 )                       // :417
result  = *( ws + 0x60 )                                      // :424
          fallback: *( ws + 0x50 )                            // :426
          fallback: *( ws + 0x20 )                            // :427
cloudNode = *( result + 0x18 )                                // :432
windNode  = *( result + 0x20 )                                // :433
valid  <=>  cloudNode >= kMinPointer || weatherState >= kMinPointer   // :436
```

**E.8 Game-speed time struct** (`hkFrameTimerUpdate`, `src/game/world.cpp:55-70`)

```
timeStruct = *( appMgr + 0x60 )    // world.cpp:57
delta      = *(f32*)( timeStruct + 0x64 )   // kOff_TimeStruct_Delta
write        (f32*)( timeStruct + 0x64 ) = delta * mult
write        (f32*)( timeStruct + 0x68 ) = delta * mult   // kOff_TimeStruct_ScaledDelta
```

**E.9 Trust relationship map** (`FindStoredTrust`, `src/game/friendly.cpp:58-116`)

```
bucketCount = (u32)*( owner + 0x3C )            // 1..4096; friendly.cpp:66
buckets     = *( owner + 0x48 )                 // friendly.cpp:68
nodes       = *( owner + 0x50 )                 // friendly.cpp:70
bucket_i    = buckets + i * 0x100               // friendly.cpp:75
entryCount  = (u32)*( bucket_i + 0x00 )         // <= 31; friendly.cpp:77
nodeIndex   = (u32)*( bucket_i + 0x0C + j*8 )   // <= 0xFFFF; friendly.cpp:82-83
node        = *( nodes + nodeIndex*8 )          // friendly.cpp:88
records     = *( node + 0x08 )                  // friendly.cpp:91
recordCount = (u32)*( node + 0x10 )             // <= 4096; friendly.cpp:93
record      = records + i * 0x68                // friendly.cpp:98
   +0x00 key (u32) | +0x04 group (u16) | +0x20 trust (i64)
```

Note the comment at `src/game/offsets.h:1829-1832`: the TU 2.01 record grew to `0x68` while
key/group/trust stayed at `+0`/`+4`/`+0x20`, and reading `+0x28` "made the multiplier silently
reject every real trust update".

---

## F. Fail-closed / version-gated behaviour

### F.1 Revision gates

| Gate | Condition | Effect | Log | Source |
|---|---|---|---|---|
| Stat-commit hook | `core::UsesTu201CompatibleRevision(GetGameVersion().revision)` — true for `2760`, `2850`, `2944` | `kSig_StatCommit` is **not searched or hooked**; the guard is the per-frame `PinEntry` loop only | `LOG_OK("player: modern continuous stat-pin guard active (all resolved characters).")` | `src/core/version_mapping.cpp:22-28`; `src/game/player.cpp:818-824` |
| Readiness signature set | `ReadinessProfileForRevision(revision)` → `Tu201KnownCompatible` vs `LegacyComplete` | selects which 7 vs 12 signatures must all be present before feature hooks install | `LOG_OK("Gameplay code ready - installing feature hooks.")` / `LOG_WARN("Gameplay-code readiness timed out after 180 seconds; installing available hooks only.")` | `src/core/readiness.cpp:8-13`; `src/core/mod.cpp:36-65`, `:116-125` |
| Crime-event dispatcher | `core::MayProbeLegacyCrimeEventDispatcherForRevision(revision)` = `revision <= 2850` | the `kSig_RegisterCrimeEvent` hook is not even probed on PE 2944+ | `LOG("world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.", revision)` | `src/core/version_mapping.cpp:67-70`; `src/game/inventory.cpp:2070-2083` |
| Game-speed signature | `kSig_FrameTimerBody` fails → try `kSig_FrameTimerBody_Pre201` | both fail → Game Speed inert (`World::Ready()` false) | `LOG_ERR("world: FrameTimerBody signature not found - Game Speed disabled.")` | `src/game/world.cpp:468-511`, `:798-801` |
| Field-time tick signature | `kSig_FieldTimeTick` fails → try `kSig_FieldTimeTick_Pre201` | both fail → Freeze Time of Day inert (the numeric-clock half) | `LOG_ERR("world: field-time tick (pre-2.01) signature NOT FOUND - Freeze Time of Day disabled.")` (primary passes `consequence = ""`, so no line for it) | `src/game/world.cpp:526-533`; `src/mem/hooks.h:33-36` |
| Wind-pack signature | `kSig_WindPack` fails → try `kSig_WindPack_Pre201` | both fail → Clear Distant Fog / cloud / fog via the shader pack inert | `LOG_ERR("world: wind pack (pre-2.01) signature NOT FOUND - Cloud and Fog control disabled.")` (primary passes `consequence = nullptr`) | `src/game/world.cpp:574-580` |
| Friendly setters | neither `kSig_FriendlySetNpc201` nor `kSig_FriendlySetPet201` resolves → re-probe the TU 2.00 pair | both TU 2.00 patterns also missing → no hooks at all | `LOG_ERR("friendly: Trust Multiplier setters NOT FOUND - feature disabled.")` | `src/game/friendly.cpp:239-245`, `:296-299` |
| Friendly getters | `kSig_FriendlyGetNpc201` / `kSig_FriendlyGetPet201` | **no fallback probe** for either getter; each is silently skipped if absent | none | `src/game/friendly.cpp:271-294` |

### F.2 Runtime guards (all in these files)

| Condition (exact source) | Result | Log |
|---|---|---|
| `!g_charMgrGlobal` — `src/game/player.cpp:385` | `TickResolveCurrentPlayerFallback()` is used instead of the party-wide resolve (active protagonist only; party + mount discovery are off) | `LOG_ERR("player: char-manager global NOT FOUND (no anchor matched) - God Mode / Infinite Stamina / Infinite Spirit limited to the current-character fallback.")` `:814-815` |
| `distinct > 1` in the anchor vote — `:95` | the winning value is used anyway, with the disagreement surfaced | `LOG_WARN("player: char-manager anchors DISAGREE (%d distinct values); using %p with %d/%d votes - re-derive the anchors.", …)` `:96-98` |
| `matched < kN` (some anchors lost) — `:99` | the resolve still succeeds | `LOG("player: char-manager successfully resolved (%d anchors verified).", matched)` `:100` |
| `!ShouldRefreshTrackedCharacters(statFeatureActive, now, requestedUntil)` — `:869`, predicate `src/game/player_logic.cpp:21-27` | `Player::Tick` **returns early without re-resolving and without clearing the sets** — the previously resolved pointers stay live in the atomics and the damage-apply hook continues to match them | none |
| `AnyStatFeatureActive` is true — `src/game/player.cpp:323-329` | the whole-character-list walk runs every tick; it is also forced true while `Teleport::IsProtected()` or `Teleport::GetFlightEngaged()` | none |
| `!env.valid` — `src/game/world.cpp:669` | the live cloud/wind injection is skipped | none |
| `!g_pEnvManager` — `:380` | `ResolveWeatherEnv` returns an invalid struct | none (`:587` skips the block silently) |
| `st.gameSpeed && \|gameSpeedMult - 1.0f\| > 0.01f` false — `:51` | the delta is left alone | none |
| `!(dt > 0.0001f && dt < 1.0f)` — `:65` | the delta is not rewritten (guards against a loading-screen / paused delta) | none |
| No-Bounty applied before the world loads — `src/game/inventory.cpp:5685-5688` | `SetNoBounty(true)` returns false and touches nothing | none (the `World::Tick` upkeep re-arms once `Player::Ready()`) `src/game/world.cpp:617-625` |

### F.3 `Player::Ready()` / `World::Ready()` semantics

```
Player::Ready()        = g_hpEntries[0] >= kMinPointer && g_actors[0] >= kMinPointer   // player.cpp:935-939
World::Ready()         = g_frameTimerUpdateTarget != nullptr                          // world.cpp:798-801
World::TimeOfDayReady()= g_timeClient >= kMinPointer && g_timeServer >= kMinPointer   // world.cpp:803-806
Friendly::Ready()      = g_hooksInstalled                                             // friendly.cpp:350-353
```

`World::Ready()` is the **Game Speed** readiness only. `TimeOfDayReady()` is the **clock**
readiness only and is what gates the Freeze toggle, the Advance/Rewind actions and all six
`RenderTimePresets` rows (`src/gui/menu.cpp:980`, `:990`, `:996`, `:1002`, `:1008`, `:1015`,
`:1102-1124`). Freeze needs *three* things (clock globals for the readout, the field-time tick
hook and the TOD engine global) but its menu gate checks only the first.

### F.4 Weather-preset table (verbatim, `World::SetWeatherPreset`, `src/game/world.cpp:896-986`)

| preset | label (menu) | forceClearSky | rainIntensity | dustIntensity | cloudThick | cloudTop | cloudBase | cloudScrollSpeed | fogA | fogB | windMultiplier | windGust | noWind |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Dynamic (Game Default) | false | 0.0 | 0.0 | 1.0 | — | — | — | 1.0 | 1.0 | 1.0 | 1.0 | false |
| 1 | Clear Sky (Sunny) | **true** | 0.0 | 0.0 | **0.0** | — | — | — | — | — | 1.0 | — | false |
| 2 | Overcast (Cloudy) | false | 0.0 | 0.0 | 2.50 | 1.20 | 1.00 | 1.20 | 1.30 | 1.20 | 1.20 | 1.00 | false |
| 3 | Rainy (Light Rain) | false | 1.50 | 0.0 | 2.00 | 1.20 | 1.00 | 1.50 | 1.40 | 1.30 | 1.80 | 1.50 | false |
| 4 | Thunderstorm (Storm) | false | 4.00 | 0.0 | 4.00 | 1.50 | 0.80 | 3.00 | 2.20 | 2.00 | 3.50 | 2.50 | false |
| 5 | Dense Fog / Mist | false | 0.0 | 0.0 | 2.00 | 1.00 | 0.50 | — | 3.50 | 3.00 | 0.50 | — | false |

`—` = the field is **not assigned** by that branch. Consequence: selecting preset 1, 2, 3 or 4
leaves `st.cloudTop` / `st.cloudBase` / `st.cloudScrollSpeed` / `st.fogA` / `st.fogB` /
`st.windGust` at whatever the *previous* preset (or the ini) left them — presets are not
self-contained. `st.snowIntensity` is never assigned by any preset.

### F.5 Menu-vs-code gate mismatches (findings)

1. **Damage-apply failure message under-reports.** `LOG_ERR(… "infinite stamina drain block
   disabled.")` `src/game/player.cpp:843` is the only message, yet the same hook also carries
   God Mode, No Fall Damage, mount immunity and both damage multipliers (rows PC2, PC4, PC8, PC9,
   PM2).
2. **`World::Install()`'s failure is discarded** (`src/core/mod.cpp:132`), and the returning
   `ok` mixes four independent sub-features (`src/game/world.cpp:464-604`), so a partial failure
   is invisible.
3. **Freeze's menu gate is narrower than the feature.** `TimeOfDayReady()` checks only the clock
   globals, while the sun clamp also needs `g_todEngineGlobal` and `mgr`; if either is missing the
   toggle reads "available" and silently only freezes the numeric clock.
4. **`st.infMountStamina` has no independent control.** It is only ever set as a copy of
   `st.infStamina` (`src/gui/menu.cpp:98`, `src/core/settings.cpp:232`), so the "Mount" half of
   `"Infinite Stamina & Mount"` cannot be toggled separately.

---

## G. Runtime validation hooks (exact log format strings)

All log output goes through `src/core/logger.h:285-292` and is written to `Trinity.log` next to the
ASI module. Identical messages repeated within 3 s are deduplicated
(`src/core/logger.h:167-173`, `:254-255`).

### G.1 Install-time — `player.cpp`

```
LOG_ERR  "player: char-manager global NOT FOUND (no anchor matched) - God Mode / Infinite Stamina / Infinite Spirit limited to the current-character fallback."   src/game/player.cpp:814-815
LOG_WARN "player: char-manager anchors DISAGREE (%d distinct values); using %p with %d/%d votes - re-derive the anchors."                                        src/game/player.cpp:96-98
LOG      "player: char-manager successfully resolved (%d anchors verified)."                                                                                     src/game/player.cpp:100
LOG_OK   "player: modern continuous stat-pin guard active (all resolved characters)."                                                                            src/game/player.cpp:823
LOG_ERR  "player: stat-commit signature NOT FOUND - direct write guard unavailable; current-character pins remain active."                                       src/game/player.cpp:827-829 + src/mem/hooks.h:35
LOG_OK   "player: damage-apply hook installed @ %p"                                                                                                              src/game/player.cpp:839 and :848
LOG_ERR  "player: damage-apply signature NOT FOUND (tried primary + alt) - infinite stamina drain block disabled."                                               src/game/player.cpp:843
LOG_OK   "player: combat-timing hook installed @ %p"                                                                                                             src/game/player.cpp:856
LOG_ERR  "player: combat-timing signature NOT FOUND - Easy Parry & Easy Evade helper timing disabled."                                                           src/game/player.cpp:852-853 + src/mem/hooks.h:35
```

Generic installer lines that also appear with these contexts (`context` = 1st argument,
`consequence` = 3rd):

```
LOG_ERR  "%s signature NOT FOUND - %s."                    src/mem/hooks.h:35
LOG_WARN "%s signature ambiguous (%zu); hooking first."    src/mem/hooks.h:41
LOG_ERR  "%s: MH_CreateHook failed (%s) - %s."             src/mem/hooks.h:48
LOG_ERR  "%s: MH_EnableHook failed (%s) - %s."             src/mem/hooks.h:57
```

### G.2 Install-time — `world.cpp`

```
LOG_OK   "world: FrameTimerUpdate hook installed @ 0x%p (true game time scale engine control)."   src/game/world.cpp:492
LOG_ERR  "world: Failed to install FrameTimerUpdate hook."                                        src/game/world.cpp:496
LOG_ERR  "world: FrameTimerUpdate prologue not found from body match."                            src/game/world.cpp:503
LOG_ERR  "world: FrameTimerBody signature not found - Game Speed disabled."                       src/game/world.cpp:509
LOG_WARN "world: field-clock signature NOT FOUND - Advance Time disabled."                        src/game/world.cpp:517
LOG_WARN "world: TOD engine-global signature NOT FOUND - sun freeze disabled."                    src/game/world.cpp:542
LOG_WARN "world: TOD engine-global signature ambiguous - sun freeze disabled."                    src/game/world.cpp:547
LOG_ERR  "world: TOD engine-global resolved out of range - sun freeze disabled."                  src/game/world.cpp:557
LOG      "world: safe EnvManager pointer resolved: 0x%llX"                                        src/game/world.cpp:595
LOG_ERR  "world: wind pack (pre-2.01) signature NOT FOUND - Cloud and Fog control disabled."      src/game/world.cpp:577-578 + src/mem/hooks.h:35
LOG_ERR  "world: field-time tick (pre-2.01) signature NOT FOUND - Freeze Time of Day disabled."   src/game/world.cpp:529-531 + src/mem/hooks.h:35
LOG_ERR  "world: rain intensity signature NOT FOUND - Rain control disabled."                     src/game/world.cpp:565-566 + src/mem/hooks.h:35
LOG_ERR  "world: snow intensity signature NOT FOUND - Snow control disabled."                     src/game/world.cpp:568-569 + src/mem/hooks.h:35
LOG_ERR  "world: dust intensity signature NOT FOUND - Dust control disabled."                     src/game/world.cpp:571-572 + src/mem/hooks.h:35
```

### G.3 Runtime — `player.cpp`

```
LOG_OK   "player: current-character fallback resolved @ %p (active player only)."   src/game/player.cpp:349   (once per session; reset by Player::Remove, :926)
```

That is the **only** runtime log line in `player.cpp`. God Mode, Infinite Stamina, Infinite
Spirit, One-Hit Kill, No Fall Damage, the multipliers, mount immunity and the Easy-Parry hook emit
**nothing** per toggle, per write or per frame — the only runtime evidence for them is the crash
report's state block (§G.6).

### G.4 Runtime — `world.cpp`

```
LOG      "world: weather preset set to %d"                                src/game/world.cpp:984
LOG      "world: clear distant fog set to %s"   (ENABLED / DISABLED)      src/game/world.cpp:991
```

Both are called only from `World::SetWeatherPreset` / `World::SetClearDistantFog`. The menu calls
`SetWeatherPreset` for the combo and for WD15, but WD14's `"Clear Distant Fog"` toggle writes
`st.clearDistantFog` **directly** (`src/gui/menu.cpp:1049`) and therefore logs nothing. Game Speed,
Freeze, Advance/Rewind, SetTimeOfDay and the whole live weather/wind injection emit **no** log
lines at all.

### G.5 Install-time + runtime — `friendly.cpp`

```
LOG_OK   "friendly: NPC Trust Multiplier observer installed @ %p"                 src/game/friendly.cpp:253
LOG_OK   "friendly: pet/mount Trust Multiplier observer installed @ %p"           src/game/friendly.cpp:265
LOG_OK   "friendly: NPC in-place trust observer installed @ %p"                   src/game/friendly.cpp:279
LOG_OK   "friendly: pet/mount in-place trust observer installed @ %p"             src/game/friendly.cpp:290
LOG_ERR  "friendly: Trust Multiplier setters NOT FOUND - feature disabled."       src/game/friendly.cpp:298
LOG_OK   "friendly: Trust Multiplier (%.1fx) ENGAGED."                            src/game/friendly.cpp:310
LOG      "friendly: Trust Multiplier DISENGAGED."                                 src/game/friendly.cpp:312
```

(`Friendly::Tick` runs from `hkMoveUpdate` `src/game/teleport.cpp:1660` and only logs on a state
*change*, `src/game/friendly.cpp:306-312`.)

### G.6 No-Bounty / crime hooks (installed by `inventory.cpp`, consumed by the PLAYER tab)

```
LOG      "world: WantedInfo table @ %p - No Bounty available."                                  src/game/inventory.cpp:5704
LOG      "world: WantedInfo table not found - bounty price left alone."                         src/game/inventory.cpp:5705
LOG      "world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active."   src/game/inventory.cpp:2081-2082
LOG_OK   "world: No Bounty %s - price zeroed on %d/%u wanted row(s)."   (applied / reverted)    src/game/inventory.cpp:5752-5753
```

### G.7 Crash-report state block (the only always-on feature-state dump)

```
"--- ACTIVE MOD FEATURES ---\n"
"  GodMode: %s | InfStamina: %s | InfMountStamina: %s | InfSpirit: %s\n"
"  OneHitKill: %s | NoBounty: %s | NoFallDamage: %s\n"
"  Damage Mult Out: %.1fx | Damage Mult In: %.1fx\n"
```

`src/dllmain.cpp:263-279`. There is **no** `GameSpeed`, `TimeFrozen`, weather, wind or
`TrustMult` field in this dump.

---

## H. Explicitly disabled / fail-closed features

This is the section that matters when comparing the menu against what actually runs.

### H.1 Easy Parry / Easy Evade — hook installed, **behaviour removed**

The hook is installed and its install log claims success, but the callback does nothing.

* `offsets.h` still documents the feature:
  `// --- Combat Timing & Hitbox Evaluator: Perfect Parry & Perfect Dodge (sub_1407219c0) ---`
  and `// Evaluating incoming attack timing windows for Perfect Parry (r9b == 1) and Perfect Dodge (r9b == 0).`
  and `// Returning true and setting *outResult = 1 natively triggers deflect/counter reactions and slow-motion.`
  — `src/game/offsets.h:172-176`.
* The callback is a pure pass-through:
  ```cpp
  bool __fastcall hkCombatTimingEval(void* combatComp, void* hitData, float distance, uint8_t isGuardMode, void* outResult)
  {
      const bool orig = oCombatTimingEval ? oCombatTimingEval(combatComp, hitData, distance, isGuardMode, outResult) : false;
      const State& st = State::Get();

      return orig;
  }
  ```
  — `src/game/player.cpp:799-805`. `st` is fetched and never used; `outResult` is never written.
* The install site still advertises the feature in its fail-safe string:
  `mem::InstallHook("player: combat-timing", kSig_CombatTimingEval, "Easy Parry & Easy Evade helper timing disabled", …)`
  — `src/game/player.cpp:852-854`, and logs `LOG_OK("player: combat-timing hook installed @ %p", …)`
  on success `:856`.
* There is **no** `easyParry` / `easyEvade` / `justGuard` field in `State`
  (compare the combat block `src/core/state.h:44-57`) and **no** settings key for either
  (`src/core/settings.cpp:78-99`). `config/Trinity.ini.example` has no such key either.
* There is **no** menu row: the combat sub-page ends at the two damage sliders
  (`src/gui/menu.cpp:109-112`, `ui::End()` at `:116`).
* `kSig_JustCore` and `kSig_JustCore_Alt` — the alternative "Just Guard (Perfect Parry) & Just
  Evade (Perfect Dodge)" entry point documented as
  `// Overriding returns true and *a5 = true, triggering native slow-mo and counters.`
  (`src/game/offsets.h:178-184`) — have **zero consumers anywhere in `src/`**. They are referenced
  only by documentation (`README_TU200_OFFSETS.md:31`, `GAME_UPDATE_PLAYBOOK.md:91-92`,
  `TU200_RE_NOTES.md:38`, where the candidate `0x140AC0FB0` was still "under verification").
* The only remaining trace of the feature in the shipping product is leftover localization keys
  in `languages/Trinity_*.ini` (`Easy Parry=`, `Easy Parry (Just Guard)=`, `Easy Evade (Just
  Evade)=` — e.g. `languages/Trinity_zh.ini:46`, `:697-700`), which no code reads. The audit notes
  in `docs/superpowers/plans/2026-09-11-pe-2850-full-menu-audit-handoff.md:23` confirm the
  removal is user-owned: "Current working-tree dye pages and Easy Parry/Easy Evade are outside the
  active menu surface."

**Reason (from the source):** none is stated. The code simply does not implement the override;
the install-time consequence string is the only surviving description of the intended behaviour.

### H.2 Stat-commit guard disabled on TU 2.01+ revisions

```cpp
// TU 2.01 removed the old single stat-commit funnel.  The resolved
// character manager plus the per-frame entry pins are the current
// guard on this build; do not search/hook a stale ABI.
if (core::UsesTu201CompatibleRevision(core::GetGameVersion().revision))
{
    LOG_OK("player: modern continuous stat-pin guard active (all resolved characters).");
}
```

`src/game/player.cpp:818-824`. Exact comment quoted; the reason is "TU 2.01 removed the old single
stat-commit funnel" and the instruction "do not search/hook a stale ABI". The revision gate is
`{2760, 2850, 2944}` (`src/core/version_mapping.cpp:22-28`). Consequence: on modern builds the
"HP never registers at a lethal value" guarantee documented at `src/game/offsets.h:133-139` and
`src/game/player.h:23-32` no longer holds — HP is restored on the *next* `Player::Tick`, i.e. there
is a between-write window again.

### H.3 Party-wide and mount discovery disabled on the TU 2.01 fallback path

```cpp
// TU 2.01.00 no longer exposes the gameplay-character manager through
// any of the pre-2.01 call-site anchors.  The inventory subsystem does,
// however, resolve the currently controlled live character through a
// separately validated client-realm root.  Use that owner as a narrow
// fallback so the active player's stat features remain available while
// deliberately leaving party-wide and mount discovery disabled.
```

`src/game/player.cpp:333-338`, quoted verbatim. Reached whenever `g_charMgrGlobal == 0`
(`:385-389`). In that mode `TickResolveCurrentPlayerFallback` fills **only index 0** of every set
(`:352-355`) and never populates `g_mountStamEntries` or the mount identity arrays — so
`st.infMountStamina`, mount God-Mode coverage and mount attacker detection all silently do
nothing, even though the menu still shows `"Infinite Stamina & Mount"` as available.

### H.4 `st.infMountStamina` has no independent user control

`src/gui/menu.cpp:95-100` sets `st.infMountStamina = st.infStamina` on every toggle; the settings
loader does the same (`src/core/settings.cpp:232`: `st.infMountStamina = vals.infMountStamina || vals.infStamina;`).
The per-feature `infMountStamina` ini key is therefore write-only in practice.

### H.5 Public API that is declared but never consumed

| Symbol | Where declared | Status |
|---|---|---|
| `Player::GetMountActor(int)` | `src/game/player.h:60`, defined `src/game/player.cpp:968-972` | **no caller anywhere in `src/`** |
| `Player::GetMountOwner(int)` | `src/game/player.h:61`, defined `src/game/player.cpp:974-978` | **no caller** |
| `Player::GetTrackedMountCount()` | `src/game/player.h:62`, defined `src/game/player.cpp:980-983` | **no caller** |
| `Player::RefreshSelf()` | `src/game/player.h:49`, defined `src/game/player.cpp:888-905` | **no caller** — the header comment says "Safe to call from render thread", but nothing calls it; the stat pins are actually driven by `Player::Tick` instead |
| `Player::DumpCharacters()` | `src/game/player.h:64-69` | **declared with no definition** — `player.cpp` contains no such function, so any caller would fail to link |
| `g_isRidingMount` | `src/game/player.cpp:136` | declared and never read or written again |
| `s_lastResolveMs` | `src/game/player.cpp:331` | declared and never used |
| `kSig_JustCore`, `kSig_JustCore_Alt` | `src/game/offsets.h:181-184` | **no consumer** (see §H.1) |
| `kOff_StatEntry_Floor` (`0x28`) | `src/game/offsets.h:76` | never used by any of these files |
| `kOff_Owner_ObjectType` (`0x48`) | `src/game/offsets.h:45` | explicitly "(documentation only)"; `IsMountTypeTag`/`IsTrackedProtagonistTypeTag` use the `+0x88` descriptor tag instead |
| `WN::DIR_X`, `WN::DIR_Z`, `WN::TURB_DENS`, `WN::TURB_SCALE`, `CN::DUST_ADD` | `src/game/offsets.h:1409-1429` | declared but **never written** — no menu control reaches them |

### H.6 Types explicitly excluded from the stamina/spirit sets

```cpp
// Match authoritative stamina gauges in Crimson Desert (Sprint 20, Pool 22, Mount Gallop/Flight 19).
// Strictly purged 17 & 18 (internal Heat/Combustion gauges) and 48 (Fire Breath) to completely prevent character auto-ignition.
bool IsStaminaType(int32_t t) {
    return t == StatType_SprintSt || t == StatType_StaminaPool117 ||
           t == StatType_MountSprint || t == 19 || t == 20 || t == 22;
}
```

`src/game/player.cpp:173-178`, quoted verbatim. `StatType_Stamina (17)`,
`StatType_Spirit (18)` and `StatType_MountAbility (48)` are defined (`src/game/offsets.h:95-113`)
but **deliberately never matched** by this predicate, so the Wyvern/Dragon fire-breath gauge is not
pinned by Infinite Stamina. `IsSpiritType` matches only `21` and `23` (`src/game/player.cpp:181-183`),
so type 18 is also outside Infinite Spirit. The explicit list `t == 19 || t == 20 || t == 22`
duplicates the three named constants.

### H.7 Other fail-closed / inert details worth recording

* **`st.noFallDamage` defaults to `true`** (`src/core/state.h:48`) — fall damage is nullified out
  of the box, `src/game/player.cpp:762-767`.
* **The `delta < 0` gate** at `src/game/player.cpp:754` means the damage hook never sees heals;
  `ScaleDamage`'s defensive `if (scaled >= 0.0) return 0;` branches (`:665`, `:718`) are therefore
  unreachable through the menu (which clamps both multipliers to `>= 0.0`,
  `src/gui/menu.cpp:109-112`).
* **`statusId == StatType_Health || statusId == 0`** (`:756`) is a duplicate comparison, since
  `StatType_Health == 0` (`src/game/offsets.h:94`).
* **`IsMountTypeTag(typeTag)` in the mount loop is redundant** with `typeTag != wantedTag`
  (`src/game/player.cpp:489`): `wantedTag` is already constrained to `{5, 6}` and
  `IsMountTypeTag` is exactly `tag == 5 || tag == 6` (`src/game/player_logic.cpp:11-14`).
* **`st.snowIntensity` is unreachable from any preset** (§F.4) — only the manual slider.
* **`World::Ready()` is not a whole-feature readiness check** — it only reflects the Game Speed
  hook (§F.3).
* **`kCharMgrAnchors`' last two entries** are documented as legacy ("1.17+" / "1.14-1.16",
  `src/game/offsets.h:246-249`) and the weakest entry is kept only as a fallback
  (`:250-254`); they are still probed on every revision, so on a modern build they contribute
  stale votes to the consensus of row C1.

---

## I. Symbols referenced but not defined in the assigned file set

1. `kOff_Container_Sub` — used at `src/game/player.cpp:749`; **defined** at
   `src/game/offsets.h:748` as `0x68`.
2. `Inventory::ClientCharacterAddr()` / `Inventory::SetNoBounty()` — declared
   `src/game/inventory.h:359` / `:321`, defined `src/game/inventory.cpp:3388-3400` /
   `:5681-5755`. Used from `player.cpp:344` and `world.cpp:623` / `menu.cpp:106`.
3. `kOff_ItemVal_Durability` (`0x40`) — defined `src/game/offsets.h:1684`; used by
   `equipment.cpp`, which is **not** in the assigned read set (only the relevant regions were read).
4. `Teleport::IsProtected()` / `Teleport::GetFlightEngaged()` — declared `src/game/teleport.h:106`
   / `:26`; consumed at `src/game/player.cpp:325`, `:762`, `:775`, `:779`. Per the sibling
   analysis `_analysis/inventory/src-travel-locomotion.md` §E.3, `ActivateProtection` is a hard
   no-op, so `IsProtected()` is effectively always false.
5. All IDB names in comments — `sub_BED7820`, `sub_145B2A0`, `sub_1407219c0`, `sub_2393AA0`,
   `sub_30DF50`, `sub_871360`, `sub_8719B0`, `sub_1CA3890`, `sub_1D44970`, `sub_2F616F0`,
   `sub_2F61690`, `sub_2F617B0`, `sub_2F61750`, `sub_31FB810`, `sub_31FB860`, `sub_36341F0`,
   `sub_31FAB10`, `sub_24AA890`, `sub_589D00`, `sub_DBE1000`, `sub_1AD4710`, `sub_613220`,
   `sub_145A5D0`, `sub_C19E1A0`, `sub_1459D30`, `sub_145B9E0`, `sub_145C0F0`, `sub_145FE10`,
   `sub_1459400`, `sub_141595BC0`, `qword_61830F8`, `qword_6181090`, `qword_648F688`,
   `dword_648F680`, `byte_606B9CE`, `dword_615A4F0`, `0x8FC348`, `0xDBE114F`, `0x141E2AD40`,
   `0x140AC0FB0` — **comments only**; no runtime code references them. Every runtime address comes
   from a signature scan, a RIP resolution or a pointer chain.

---

## J. Uncertainties

1. **`Easy Parry` intent.** `hkCombatTimingEval` is a pass-through with an unused `State&`
   (`src/game/player.cpp:802`) and an empty body, while the install string still says
   "Easy Parry & Easy Evade helper timing disabled". Whether the implementation was deleted
   deliberately or reverted is not stated anywhere in `src/`. `REVERSE_ENGINEERING_GUIDE.md:170-181`
   documents the *intended* mechanism (a "Just Guard Deflection" reaction flag `a6 = 2`), which
   does not match the current callback signature at all.
2. **`Player::Tick` early-return leaves stale sets live.** When no stat feature is active and no
   consumer has called `GetTrackedPlayerCount()` in the last 2000 ms
   (`src/game/player.cpp:139`, `:866-870`, predicate `src/game/player_logic.cpp:21-27`), the resolve
   stops but the atomics are **not** cleared. The damage-apply hook keeps matching those pointers,
   so `dmgInMult`/`dmgOutMult`/`oneHitKill` can still act on a character that has since been
   reallocated. No comment in the file acknowledges this.
3. **Damage-apply hook ABI.** `sourceCtx` and the trailing `out` pointer are stack arguments in the
   declared `__fastcall` prototype (`src/game/player.cpp:152-155`); Trinity never validates them
   beyond a `kMinPointer` check, so a stack-layout change would be undetectable except by
   misbehaviour. The offsets.h comment claims the layout was "cross-checked against an earlier game
   build … and re-validated in our dump" (`src/game/offsets.h:164-166`).
4. **`hkStatCommit` is never exercised on TU 2.01+** (§H.2), so its ABI (`uint16_t flag` in R9W,
   documented at `src/game/offsets.h:143-144`) is unverified on the revisions this mod actually
   targets.
5. **`packedOut` indices in `hkWindPack` are magic numbers** (`0x00`, `0x05`, `0x11`, `0x17`,
   `0x1B`, `0x1E`, `0x20`, `0x23`, `0x24`, `0x2F`, `0x30`, `0x32`) with no named constant and no
   bounds check on the array length; only the pointer itself is range-checked
   (`src/game/world.cpp:282-283`). The `CN::`/`WN::` namespaces describe a *different* structure
   (the environment nodes) and cannot be used to verify the pack layout.
6. **Two overlapping writes to the same concept.** `CN::CLOUD_THICK` (0x140) and `CN::CLOUD_BASE`
   (0x144) in `World::Tick` receive `cloudTop * 0.001f` / `cloudBase * 0.001f` (`:710-717`), while
   the packed block's `0x2F`/`0x30` receive the raw multipliers (`:334-342`). Whether these are the
   same physical parameter expressed in different units is not asserted anywhere.
7. **`World::Tick` is the only writer of the live atmosphere.** If the movement-update hook fails
   (`src/core/mod.cpp:130` ignores `Teleport::Install`'s result), every row-W17/W18 write stops —
   including "Force Clear Sky" and "No Wind" — while the shader-pack hook (H9) still applies the
   fog/cloud half. The two paths are inconsistent by construction.
8. **`g_pEnvManager` resolution logs nothing on failure** (`src/game/world.cpp:587`), so a missing
   `kSig_EnvManager` is only observable as "the atmosphere sliders do nothing".
9. **`Player::GetTrackedPlayerCount()` has a side effect** — it extends the 2000 ms tracking
   demand (`src/game/player.cpp:955-958`). Consumers in `equipment.cpp:231` and
   `inventory.cpp:1412`, `:3685` therefore keep the character-manager scan alive for up to 2 s
   after their last call, which is by design (`src/game/player_logic.h:22-27`) but means the
   "stat features are off, so stop scanning" intent is only approximate.
10. **Line numbers are working-tree line numbers.** They will drift if the file is edited; every
    claim above also names the enclosing function or log string so it can be re-located.
