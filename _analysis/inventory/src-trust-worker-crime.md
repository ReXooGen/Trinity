# Trinity — source-side consumers: TRUST/PET, WORKER, CRIME/WANTED/BOUNTY/MONEY

Scope: static documentation of every place the Trinity mod touches the Crimson Desert
executable for three feature areas — **(1) NPC/PET/TRUST/FRIENDSHIP**, **(2) WORKER level &
skills**, **(3) CRIME/WANTED/BOUNTY/MONEY** — with exact `source file:line` citations,
verbatim byte patterns/offsets, and the ABI each touch point assumes.

Sources read in full: `src/game/friendly.cpp` (354 lines), `src/game/friendly.h`,
`src/game/friendly_logic.h`, `src/game/friendly_logic.cpp`, `src/game/worker.cpp` (194 lines),
`src/game/worker.h`, `src/game/worker_logic.h`, `src/game/crime_hook_contract.h`,
`src/mem/hooks.h`, `tests/verify_worker_feature_contract.ps1`.
Grepped and read by range (not full reads): `src/game/offsets.h`, `src/game/inventory.cpp`,
`src/game/inventory.h`, `src/gui/menu.cpp`, `src/core/state.h`, `src/core/settings.cpp`,
`src/core/mod.cpp`, `src/core/version_mapping.{h,cpp}`, `src/core/state.h`, `src/mem/scanner.{h,cpp}`,
`src/mem/safe_memory.h`, `src/game/world.cpp`, `src/game/teleport.cpp`, `src/dllmain.cpp`,
`src/hooks/dx12_hook.cpp`, `tests/readiness_tests.cpp`, `config/Trinity.ini.example`.

Grep sweep over the whole `src/` tree for `Bounty|Wanted|Crime|wanted|bounty|crime|friendship|Friendship|trust|Trust|worker|Worker|money|Money|gold|Gold`
is what produced the consumer list below. Hits that are *not* consumers (pure item-name
strings such as `item_names.cpp` `"BountyHunter_Fabric_Armor"` / `"Quest_WantedPaper_0001"`
display names, the Dye editor's `"Gold"` material name at `src/gui/framework.cpp:1330`, the
UI theme `"Royal Gold"` at `src/core/state.h:154`) are called out rather than documented as
feature consumers.

Convention: `f:NNN` = `path/file:line`. **No source file was modified.** No replacement AOBs
are proposed anywhere in this document — it only records what the source says.

---

## 0. Entry points and wiring summary

| Item | Value | Source |
|---|---|---|
| Install order | `game::Player` → `game::Teleport` → `game::Inventory` → `game::World` → `game::Equipment` → `game::Friendly` → `game::Worker` | `src/core/mod.cpp:129-135` |
| Worker install comment | `game::Worker::Install();    // Reversible worker level/ability patch (PE 2850/2944)` | `src/core/mod.cpp:135` |
| Friendly install comment | `game::Friendly::Install();  // Trust Multiplier (gift/feed/tame)` | `src/core/mod.cpp:134` |
| Install is non-fatal | all `Install()` return values are discarded; overlay still runs | `src/core/mod.cpp:127-135` |
| Shutdown order | `game::Friendly::Remove();` then `game::Worker::Remove();` (Inventory::Remove earlier) | `src/core/mod.cpp:159-163` |
| MinHook init | `MH_Initialize()` precedes every install | `src/core/mod.cpp:97-101` |
| Gameplay readiness probe (presence only) | `kSig_EvaluateCrimeWantedState` is a **required** sentinel on both readiness profiles | `src/core/mod.cpp:41`, `src/core/mod.cpp:52` |
| `Friendly::Tick` driver | game thread, inside the movement/game-tick dispatch | `src/game/teleport.cpp:1659-1660` |
| `Worker` has no Tick | `Worker` exposes only `Install/Remove/Ready/Enabled/SetEnabled` | `src/game/worker.h:9-17` |
| Crime hooks live in `Inventory` | installed from `Inventory::Install()`, removed from `Inventory::Remove()` | `src/game/inventory.cpp:2062-2083`, `src/game/inventory.cpp:2342-2343` |
| Shared hook helper | `mem::InstallHook(...)` (find → warn-if-ambiguous → `MH_CreateHook` → `MH_EnableHook`) and `mem::RemoveHook(...)` | `src/mem/hooks.h:26-65`, `src/mem/hooks.h:70-76` |

Note on the `maxMatches` argument: the two crime hooks pass `0` as `maxMatches`
(`src/game/inventory.cpp:2068`, `:2074`). `CountMatches` → `Scan(pattern, mod, 0)` pushes one
hit and then returns at the `out.size() >= maxResults` test (`src/mem/scanner.cpp:145`,
`src/mem/scanner.cpp:219-222`), so the `signature ambiguous` warning branch is effectively
unreachable for those two hooks. This is an observation about the code path, not a claim
about intent.

---

## A. Feature list — user-visible menu features and exact C++ entry points

### A.1 NPC / PET / TRUST / FRIENDSHIP

Menu tab: `PLAYER` — page body `RenderPlayer()` `src/gui/menu.cpp:119-164`.

| # | Menu feature (label) | C++ entry point(s) | Game-side effect |
|---|---|---|---|
| TR1 | `"Trust Multiplier"` toggle + multiplier slider (1.0–25.0x, step 0.25, default 3.0x, format `"%.2fx"`) | `ui::ToggleFloat(LOC("Trust Multiplier"), &st.trustMult, &st.trustMultVal, 1.0f, 25.0f, 0.25f, 3.0f, "%.2fx", …)` `src/gui/menu.cpp:156-159`; state `src/core/state.h:102-103` | Scales the **gain** written into the relationship record through the two trust setters / two lookup paths (see §B rows T1–T9) |
| TR2 | Availability text for TR1 | `game::Friendly::Ready()` `src/gui/menu.cpp:157` → `src/game/friendly.cpp:350-353` | `Ready()` is `g_hooksInstalled` only |
| TR3 | Engaged/disengaged runtime state (log only, no menu row) | `Friendly::Tick()` `src/game/teleport.cpp:1660` → `src/game/friendly.cpp:302-313` | logs `ENGAGED`/`DISENGAGED`; does **not** enable/disable hooks despite the header comment at `src/game/friendly.h:37-39` |
| TR4 | Persistence | `trustMult` / `trustMultVal` keys `src/core/settings.cpp:98-99`; applied `src/core/settings.cpp:246-247` (`ClampF(vals.trustMultVal, 1.0f, 25.0f)`); written `src/core/settings.cpp:326-327`, `:390-391`; reset `src/core/settings.cpp:475-476` | INI defaults `trustMult=0`, `trustMultVal=20.000` `config/Trinity.ini.example:43-44` |

**There are no NPC-action or pet-action menu features in the 2.01/2.02/2944 revision of the
source.** The only user-visible feature in this area is TR1. "NPC actions" and "pet actions"
exist solely as the four hooked engine functions (NPC setter, pet/mount setter, NPC lookup,
pet/mount lookup) that TR1 observes. The header comment describes the design intent as
purely reactive:

> `// Purely reactive: the hook does all the work, so there is no per-frame Tick. Inert (the`
> `// hook passes everything through) unless the toggle is on and the multiplier is above 1.0x.`
> — `src/game/friendly.h:25-27`

The Trust Multiplier scopes both mechanics — NPC gifting and animal/mount feeding-to-tame:

> `//  - "Friendly" is the engine's single trust value behind both mechanics -`
> `//    gifting an NPC and feeding a wild animal both raise it, and it is the`
> `//    gauge that fills to tame a mount. It is a 0..100 value; reaching 100`
> `//    completes the tame.`
> — `src/game/friendly.h:11-14`

Policy limits: the multiplier only ever scales a **gain**, and the result is re-clamped to the
game's max:

```cpp
// src/game/friendly_logic.cpp:23-40
int64_t ScaleTrustValue(int64_t previous, bool hasPrevious,
                        int64_t incoming, float multiplier, bool enabled)
{
    constexpr int64_t kTrustMax = 100;
    if (!enabled || multiplier <= 1.0f || incoming < 0 || incoming > kTrustMax)
        return incoming;

    const int64_t baseline = hasPrevious ? previous : 0;
    const int64_t gain = incoming - baseline;
    if (gain <= 0)
        return incoming;

    int64_t result = baseline + static_cast<int64_t>(
        static_cast<double>(gain) * static_cast<double>(multiplier));
    if (result > kTrustMax) result = kTrustMax;
    if (result < 0) result = 0;
    return result;
}
```

Baseline selection (cached observation wins over the live map, because the setter's source can
alias its destination):

```cpp
// src/game/friendly_logic.cpp:5-21
bool SelectTrustBaseline(int64_t stored, bool hasStored,
                         int64_t cached, bool hasCached,
                         int64_t* outBaseline)
{
    if (!outBaseline) return false;
    if (hasCached)
    {
        *outBaseline = cached;
        return true;
    }
    if (hasStored)
    {
        *outBaseline = stored;
        return true;
    }
    return false;
}
```

The scaling condition is re-evaluated on every observed write:
`const bool scale = Player::Ready() && st.trustMult && st.trustMultVal > 1.0f;`
(`src/game/friendly.cpp:135`, `src/game/friendly.cpp:178`). There is no separate
"enable/disable the hook" step anywhere — the detour always runs and passes through when the
condition is false.

### A.2 WORKER level & skills

Menu tab: `PLAYER` — page body `RenderPlayer()` `src/gui/menu.cpp:119-164`.

| # | Menu feature (label) | C++ entry point(s) | Game-side effect |
|---|---|---|---|
| WK1 | `"Max Worker Level & Skills"` toggle (single boolean, no submenu, no level/skill numeric editor) | `ui::Toggle(LOC("Max Worker Level & Skills"), &st.workerMaxLevelAndSkills, …)` `src/gui/menu.cpp:134-137`; state `src/core/state.h:152` | `game::Worker::SetEnabled(st.workerMaxLevelAndSkills)` `src/gui/menu.cpp:139` → 6-byte code patch (§C) |
| WK2 | Apply-failure rollback + toast | `src/gui/menu.cpp:138-143`: on `SetEnabled` false, restores `st.workerMaxLevelAndSkills = workerBefore` and toasts `"Worker patch could not be applied"` | toggle reverts |
| WK3 | Availability text | `game::Worker::Ready()` `src/gui/menu.cpp:135` → `src/game/worker.cpp:132-135` (`g_ready`); else `"Worker patch unavailable for this game revision."` `src/gui/menu.cpp:137` | — |
| WK4 | Persistence | `workerMaxLevelAndSkills` key `src/core/settings.cpp:104`; applied `src/core/settings.cpp:252`; written `src/core/settings.cpp:332`, `:396` | INI default `workerMaxLevelAndSkills=0` `config/Trinity.ini.example:45` |
| WK5 | Saved-state re-apply at install | `src/game/worker.cpp:95-104`: if `state.workerMaxLevelAndSkills` and not already patched, calls `SetEnabled(true)`; on failure resets the toggle to OFF and logs | patch enabled at startup |
| WK6 | Restore on unload | `game::Worker::Remove()` `src/core/mod.cpp:163` → `src/game/worker.cpp:109-130` | restores original bytes |

The class deliberately exposes **no** worker object layout, enumeration, or data editor:

> `// Reversible controller for the worker level/ability check patch (PE 2850`
> `// and PE 2944 share the same grade-selector branch bytes) supplied as an`
> `// Auto Assembler script. It deliberately exposes no worker object layout,`
> `// enumeration, or direct data editor API.`
> — `src/game/worker.h:5-8`

The legacy worker UI is contract-tested to be **absent**. `tests/verify_worker_feature_contract.ps1`
requires the new toggle and forbids the old routes and keys:

```powershell
# tests/verify_worker_feature_contract.ps1:19-36
Require $state 'workerMaxLevelAndSkills' 'State must expose the new worker toggle.'
Require $settings 'workerMaxLevelAndSkills' 'Settings must persist the new worker toggle.'
Require $example '(?m)^workerMaxLevelAndSkills=' 'The example INI must document the new worker toggle.'

$legacyKeyPatterns = @(
    '(?m)^\s*workerMaxLevelHook\s*[=]',
    '(?m)^\s*workerAutoApply\s*[=]',
    '(?m)^\s*workerEdit(?:Level|Exp)\s*[=]',
    '(?m)^\s*workerTargetSkillCount\s*[=]',
    '(?m)^\s*workerSkillId\d+\s*[=]'
)
foreach ($pattern in $legacyKeyPatterns) {
    Forbid $settings $pattern "Settings still contains legacy worker key pattern: $pattern"
    Forbid $example $pattern "Example INI still contains legacy worker key pattern: $pattern"
}

Require $menu 'Max Worker Level & Skills' 'Player menu must expose the new worker toggle.'
Forbid $menu 'RenderWorkers|RenderMountOptions|mount_options|world_workers' 'Legacy worker or mount menu route remains.'
```

So: `RenderWorkers`, `RenderMountOptions`, `mount_options`, `world_workers`, and the INI keys
`workerMaxLevelHook`, `workerAutoApply`, `workerEditLevel`, `workerEditExp`,
`workerTargetSkillCount`, `workerSkillId<n>` are **explicitly forbidden / removed** on this
revision (see §H).

### A.3 CRIME / WANTED / BOUNTY / MONEY

| # | Menu feature (label) | C++ entry point(s) | Game-side effect |
|---|---|---|---|
| CR1 | `"No Bounty"` toggle — description `"Crimes stop adding to your bounty or alerting faction guards (session-only, safe for save files)."` | `ui::Toggle(LOC("No Bounty"), &st.noBounty, …)` `src/gui/menu.cpp:103-108` → `game::Inventory::SetNoBounty(st.noBounty)` `src/gui/menu.cpp:106`; state `src/core/state.h:52` | WantedInfo `_increasePrice` zero/restore plus two crime hooks (§B rows C1–C5) |
| CR2 | No Bounty upkeep across loads / re-apply retry | `World::Tick()` `src/game/world.cpp:611-630` → `game::Inventory::SetNoBounty(st.noBounty)` `src/game/world.cpp:623` (driven from `src/game/teleport.cpp:1645`) | re-applies once player is in world and after every `Player::Ready()` transition |
| CR3 | `"Bounty Notices (Wanted Posters)"` restore category (`"Browse all 50+ Bounty Notices and Wanted Posters (Simon, Ulzok, Haldin, etc.)."`) | `kRestoreBountyKeys` `src/gui/menu.cpp:2284-2300`; `RenderRestoreBounty()` `src/gui/menu.cpp:2492-2495`; submenu entry `src/gui/menu.cpp:2538-2539`; route `"invrestore_bounty"` `src/gui/menu.cpp:3219` | item re-grant through the shared `AddItemByKey`/`AddItem` path (`src/game/inventory.cpp:5089-5101`) — item data, not crime state |
| CR4 | `"Restore Items"` parent submenu description mentioning wanted notices | `src/gui/menu.cpp:1674` | — |
| MO1 | `"Money & Currency"` submenu (INVENTORY tab) | `ui::Submenu(LOC("Money & Currency"), "invmoney", …)` `src/gui/menu.cpp:1671`; route `src/gui/menu.cpp:3227`; body `RenderInventoryMoney()` `src/gui/menu.cpp:2073-2150` | silver/copper slot writes + spoof |
| MO2 | `"Direct Silver Amount"` numeric option (0–20,000,000, step 1,000,000, default 20,000,000) | `src/gui/menu.cpp:2079-2080`; storage `s_directSilverInput` `src/gui/menu.cpp:2049` | — |
| MO3 | `">> Set Wallet to Exact Amount <<"` | `Inventory::SetDirectSilver(s_directSilverInput)` `src/gui/menu.cpp:2082-2092` → `src/game/inventory.cpp:4551-4702` | writes `Money_Copper` slot quantity in every holder + `g_walletSpoofValue` |
| MO4 | `">> Add Amount to Existing Wallet <<"` | `Inventory::AddDirectSilver(...)` `src/gui/menu.cpp:2095-2102` → `src/game/inventory.cpp:4705-4730` | reads current copper, delegates to `SetDirectSilver` `src/game/inventory.cpp:4728-4729` |
| MO5 | Presets `"Set to 1,000,000 Silver (1 Million)"`, `"Set to 10,000,000 Silver (10 Million)"`, `"Set to 20,000,000 Silver (20 Million)"` (`"(Safe Cap)"`) | `src/gui/menu.cpp:2105-2121` (`SetDirectSilver(1000000/10000000/20000000)`) | same as MO3 |
| MO6 | `"Full Silver Pouches (Count)"` (10–10,000, step 100, default 1,000) + `">> Spawn Full Silver Pouches <<"` | `src/gui/menu.cpp:2124-2134` → `Inventory::SpawnSilverPouches(count)` `src/game/inventory.cpp:4733-4743` (`FindTypeIdByKey("Silver_Pack")` then `AddItem`) | item add of `Silver_Pack` |
| MO7 | `">> Cash In All Pouches (Instant Liquidate) <<"` | `src/gui/menu.cpp:2136-2144` → `Inventory::CashInAllSilverPouches(&added)` `src/game/inventory.cpp:4746-4835` | clears `Silver_Pack` slots, `addedCopper += it.qty * 1000000LL` `src/game/inventory.cpp:4769`, converts at `addedCopper / 100` `src/game/inventory.cpp:4829` |
| MO8 | `"Optional"` submenu (`"invmoney_opt"`) | `src/gui/menu.cpp:2147`, route `src/gui/menu.cpp:3228`; body `RenderInventoryMoneyOptional()` `src/gui/menu.cpp:2052-2071` | — |
| MO9 | `">> Clear Bugged/Fake Wallet Coins <<"` | `src/gui/menu.cpp:2056-2061` → `SetDirectSilver(0)` | `g_walletSpoofValue = -1` (`src/game/inventory.cpp:4691-4692`) and zeroes Money slots |
| MO10 | `"Consolidate All Money Stacks"` — `"Merges all scattered silver slots into 1 single master slot in your bag."` | `src/gui/menu.cpp:2063-2068` → `Inventory::ConsolidateMoney()` `src/game/inventory.cpp:4858-4990` | merges duplicate `Money_Copper` slots in the storage snapshot + client/server holders |
| MO11 | Crash-report feature line `NoBounty: %s` | `src/dllmain.cpp:267`, `src/dllmain.cpp:275` | status only |

**`AddCampCurrency` is declared and defined but has no menu call site** —
`src/game/inventory.h:209`, `src/game/inventory.cpp:4838-4856`; the grep sweep found no caller
(`AddCampProvisions` appears nowhere in `src/`). It is dead code in this revision. It is the
only producer of `g_campSpoofAddedValue` (`src/game/inventory.cpp:54`, `:4843`), which the
item-quantity hook consumes for camp currencies (`src/game/inventory.cpp:1517-1539`).

`noBounty` **is persisted** (`src/core/settings.cpp:86`, `:234`, `:314`, `:378`, `:462`) but is
**not present in `config/Trinity.ini.example`** — the grep sweep over `config/` found only
`trustMult`, `trustMultVal`, `workerMaxLevelAndSkills`.

---

## B. Game-side function contracts

Legend for "How obtained": *locator constant* = an `inline constexpr` AOB string in
`src/game/offsets.h` resolved by `mem::FindPattern`/`FindAllMatches`; *resolution routine* = a
function in `src/game/inventory.cpp` that walks the game module; *baked RVA* = `gameBase + N`.

### B.1 TRUST / FRIENDSHIP (feature area 1)

| # | Trinity role | Game function or address expression | How the address is obtained (locator constant / resolution routine / pointer chain) | Exact ABI Trinity assumes (registers/args, return) | Object/structure it operates on | Structure offsets read or written | Source file:line | Fail-safe if the address/locator is missing |
|---|---|---|---|---|---|---|---|---|
| T1 | hook (detour `hkSetNpc`) | NPC relationship-record setter (TU 2.01), IDB `sub_…` not named in source; site comment `0x141BDA967` is for the *unused* Site A sig, not this one | locator `kSig_FriendlySetNpc201` = `4C 8B DC 53 55 56 57 41 56 41 57 48 83 EC 68 48 8B FA 48 8B F1 0F B7 42 04` `src/game/offsets.h:1797-1798`, via `mem::FindPattern` `src/game/friendly.cpp:239` | `void* __fastcall(void* mapOwner /*rcx*/, void* record /*rdx*/)`, returns the setter's value untouched | the NPC relationship map owner + a 0x68-stride source record | reads record `+0x00` u32 key, `+0x04` u16 group, `+0x20` i64 trust; writes record `+0x20` i64 trust | `src/game/friendly.cpp:26`, `:197-202`, `:239`, `:247-257` | `CreateAndEnable` failure → `g_npcTarget = nullptr`, hook not installed; if **both** NPC and pet 2.01 sigs miss, falls back to the TU 2.00 sigs (`src/game/friendly.cpp:241-245`); if nothing installs: `LOG_ERR("friendly: Trust Multiplier setters NOT FOUND - feature disabled.")` `src/game/friendly.cpp:298` |
| T2 | hook (detour `hkSetPet`) | pet/mount relationship-record setter (TU 2.01); the source documents the pet setter's IDB name as `sub_1AD4710` for the older prologue (`src/game/offsets.h:1758`) — the 2.01 signature below is a distinct site and is **not** given an IDB name anywhere in the source | locator `kSig_FriendlySetPet201` = `49 89 E3 53 55 56 57 41 56 41 57 48 83 EC 68 48 89 D7 48 89 CE 0F B7 42 04 66 41 89 43 08 49 8D 4B 08 E8 ? ? ? ? 31 ED 39 6E 1C` `src/game/offsets.h:1799-1801`, via `mem::FindPattern` `src/game/friendly.cpp:240` | same as T1 | pet/vehicle relationship map owner + source record | same as T1 | `src/game/friendly.cpp:28`, `:204-209`, `:240`, `:259-269` | same as T1 (`g_petTarget = nullptr`) |
| T3 | hook (detour `hkSetNpc`, TU ≤ 2.00 fallback locator) | `SetNpc` prologue "for TU 2.00"; IDB `sub_DBE1000` per offsets comment | locator `kSig_FriendlySetNpc` = `4C 8B DC 53 55 56 57 41 56 48 83 EC 60 48 8B FA 48 8D 69 38` `src/game/offsets.h:1813-1814`; used only when **both** 2.01 setters missed `src/game/friendly.cpp:241-245` | `void* __fastcall(void* mapOwner, void* record)` | NPC map owner (component `+0x18` owner per `src/game/offsets.h:1757`) + record | same as T1 | `src/game/friendly.cpp:243`, `:247-257` | if this also fails → `g_npcTarget = nullptr`; feature may still be available through the other three hooks |
| T4 | hook (detour `hkSetPet`, TU ≤ 2.00 fallback locator) | `SetPet` prologue "for TU 2.00"; IDB `sub_1AD4710` | locator `kSig_FriendlySetPet` = `49 89 E3 53 55 56 57 41 56 48 83 EC 60 48 89 D7 48 8D 69 18` `src/game/offsets.h:1816-1817`; same fallback trigger | `void* __fastcall(void* mapOwner, void* record)` | pet/vehicle map owner (component `+0x38` owner per `src/game/offsets.h:1758`) + record | same as T1 | `src/game/friendly.cpp:244`, `:259-269` | as T3 |
| T5 | hook (detour `hkGetNpc`) | NPC relationship-map lookup; sig includes the map-layout discriminator `83 7F 1C 00` (`+0x1C` realm) | locator `kSig_FriendlyGetNpc201` = `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 1C 00` `src/game/offsets.h:1807-1808`, via `mem::FindPattern` `src/game/friendly.cpp:271` | `void* __fastcall(void* mapOwner /*rcx*/, void* actor /*rdx*/)`, returns the game's record pointer | map owner + actor; returned record | reads record `+0x00` u32, `+0x04` u16, `+0x20` i64; writes `+0x20` i64 in place | `src/game/friendly.cpp:29`, `:211-217`, `:271-283` | `g_npcGetTarget = nullptr`; the pet-lookup block at `:284` is independent (only its own `if (petGetAddr)` gates it), so a missing NPC lookup does not block the pet lookup |
| T6 | hook (detour `hkGetPet`) | pet/vehicle relationship-map lookup; discriminator `83 7F 3C 00` (`+0x3C` realm) | locator `kSig_FriendlyGetPet201` = `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 3C 00` `src/game/offsets.h:1809-1810`, via `mem::FindPattern` `src/game/friendly.cpp:272` | as T5 | as T5 | as T5 | `src/game/friendly.cpp:31`, `:219-225`, `:272`, `:284-294` | `g_petGetTarget = nullptr` |
| T7 | memory read (map traversal, no call) | relationship map of the setter's owner; layout per comment: `0x100`-byte hash buckets at `+0x48`, node pointers at `+0x50`, `+0x3C` bucket count, `0x68`-stride record vector at node `+0x08` | pointer chain from the `mapOwner` argument (`owner +0x3C`, `+0x48`, `+0x50`; `bucket +0x00` entry count, `bucket +0x0C + i*8` node index; `nodes + idx*8`; `node +0x08` records, `node +0x10` record count; `records + i*0x68`) `src/game/friendly.cpp:58-116` | no call — pure guarded reads (`mem::Read32`/`Read16`/`Read64`/`ReadPtr`) | NPC/pet relationship map | reads `+0x3C` u32, `+0x48` ptr, `+0x50` ptr, bucket `+0x00` u32, bucket `+0x0C` u32, node `+0x08` ptr, node `+0x10` u32, record `+0x00` u32, `+0x04` u16, `+0x20` i64 | `src/game/friendly.cpp:52-116` | every read is guarded and any failure returns `false` → `SelectTrustBaseline` falls back to the cache; no write occurs |
| T8 | memory write | the source record handed to the setter (`rdx`) — the game copies it into the map itself | address = `record` argument of T1/T2/T3/T4 | `Write64(r + kOff_FriendlyRec_Value, newValue)` `src/game/friendly.cpp:153` | 0x68-stride relationship record | writes `+0x20` i64 (`kOff_FriendlyRec_Value = 0x20`, `src/game/offsets.h:1829-1832`) | `src/game/friendly.cpp:118-154`, `:199`, `:206` | write only happens when `newValue != incoming`; `Write64` is guarded by `mem::IsValidUserPtr` + SEH (`src/mem/safe_memory.h:43-54`); detour body is additionally wrapped in `__try/__except(EXCEPTION_EXECUTE_HANDLER)` `src/game/friendly.cpp:199-200`, `:206-207` |
| T9 | memory write (in-place path) | record **returned by** the lookup T5/T6 (game mutates it directly) | address = return value of `oGetNpc`/`oGetPet` | `Write64(r + kOff_FriendlyRec_Value, newValue)` `src/game/friendly.cpp:194` | same record layout | writes `+0x20` i64; reads `+0x00`, `+0x04`, `+0x20` | `src/game/friendly.cpp:156-195`, `:214`, `:222` | first observation for a key only seeds the cache and returns without writing (`src/game/friendly.cpp:182-186`) — "this deliberately avoids turning pre-existing trust into a gain" `src/game/friendly.cpp:159`; SEH-wrapped `:214-215`, `:222-223` |

Record field offsets, verbatim:

```cpp
// src/game/offsets.h:1827-1833
    inline constexpr uintptr_t kOff_FriendlyRec_Key   = 0x00; // u32 record key
    inline constexpr uintptr_t kOff_FriendlyRec_Group = 0x04; // u16 group/bucket key
    // TU 2.01 still stores _varyFriendly in the second 32-byte copy block.
    // The +0x28 field is a different member; reading it made the multiplier
    // silently reject every real trust update.
    inline constexpr uintptr_t kOff_FriendlyRec_Value = 0x20; // i64 trust value
    inline constexpr int64_t   kFriendly_Max          = 100;  // the taming/NPC cap (0..100)
```

Cache key derivation (not a game touch, recorded for completeness):

```cpp
// src/game/friendly.cpp:46-50
        uint64_t TrustCacheKey(void* mapOwner, uint16_t group, uint32_t key)
        {
            const uint64_t owner = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mapOwner));
            return (owner >> 4) ^ (static_cast<uint64_t>(group) << 48) ^ key;
        }
```

### B.2 WORKER (feature area 2)

| # | Trinity role | Game function or address expression | How the address is obtained (locator constant / resolution routine / pointer chain) | Exact ABI Trinity assumes (registers/args, return) | Object/structure it operates on | Structure offsets read or written | Source file:line | Fail-safe if the address/locator is missing |
|---|---|---|---|---|---|---|---|---|
| W1 | byte patch (read + write, 6 bytes) | worker grade-selector's first `jne`; source-documented addresses `CrimsonDesert.exe+20967CC` (PE 2850) and `CrimsonDesert.exe+214BE8C` (PE 2944) | locator `kSig_WorkerMaxLevelAndSkills` = `0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5` `src/game/offsets.h:1849-1850`, resolved with `mem::FindAllMatches(kSig_WorkerMaxLevelAndSkills, 2)` and required to yield **exactly one** match `src/game/worker.cpp:57-63`; the banner bytes are the patch itself, and `FindAllMatches` returns the **start of the match**, which is the branch | no call; raw code bytes. Patch/revert via `mem::PatchMemory(addr, data, 6)` = `VirtualProtect(PAGE_EXECUTE_READWRITE)` → `memcpy` → restore protection → `FlushInstructionCache` `src/mem/safe_memory.h:162-180` | game code section, one `jne rel32` + continuation | writes bytes `+0x00..+0x05`; reads the same 6 bytes for validation | `src/game/worker.cpp:42-107`, `:142-193`; `src/game/offsets.h:1835-1850` | **fail-closed at five points**: unsupported revision → `LOG_WARN("worker: supplied level/ability patch disabled for unsupported PE revision %d.", revision)` `src/game/worker.cpp:51-55`; match count ≠ 1 → `LOG_WARN("worker: level/ability patch signature expected one match, found %zu; feature disabled.", …)` `src/game/worker.cpp:58-63`; unreadable target → `LOG_WARN("worker: exact patch target at %p could not be read; feature disabled.", …)` `src/game/worker.cpp:67-73`; unexpected bytes → `LOG_WARN("worker: target at %p contains unexpected bytes; feature disabled.", …)` `src/game/worker.cpp:83-89`; any `PatchMemory` failure → `SetEnabled` returns false and the menu reverts the toggle `src/gui/menu.cpp:139-143` |
| W2 | memory read (validation only, same address as W1) | W1's target | same | 6 × `mem::Read8` after `mem::IsValidUserPtr(g_patchTarget)` **and** `mem::IsValidUserPtr(g_patchTarget + kWorkerPatchSize - 1)` | game code bytes | reads 6 bytes | `src/game/worker.cpp:21-33`, `:113-114`, `:147-149` | any read failure → `SetEnabled` returns false; on shutdown the target is left untouched (`src/game/worker.cpp:120-124`) |

The patch changes **code**, not an object: the `jne` (conditional branch) becomes an
unconditional `jmp` to the same destination, so the grade selector always takes the "top
tier" path regardless of the comparison. See §C for the byte-level contract.

### B.3 CRIME / WANTED / BOUNTY (feature area 3a)

| # | Trinity role | Game function or address expression | How the address is obtained (locator constant / resolution routine / pointer chain) | Exact ABI Trinity assumes (registers/args, return) | Object/structure it operates on | Structure offsets read or written | Source file:line | Fail-safe if the address/locator is missing |
|---|---|---|---|---|---|---|---|---|
| C1 | hook (detour `hkEvaluateCrimeWantedState`) | "Evaluates crime records on actors; returning 7 (eWantedState_None) blocks Witness/Pursuit/Bounty" `src/game/offsets.h:1182` | locator `kSig_EvaluateCrimeWantedState` = `48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 48 8B DA 4C 6B D9 38 41 B0 07` `src/game/offsets.h:1183-1184`, installed with `mem::InstallHook(..., 0)` `src/game/inventory.cpp:2066-2068` | `uint8_t __fastcall(void* wantedMgr /*rcx*/, void* actorCtx /*rdx*/)` `src/game/inventory.cpp:2028`; when `st.noBounty` the detour **returns `7` without calling the original** `src/game/inventory.cpp:2035-2039`; otherwise forwards and, if the trampoline is null, returns `0` `src/game/inventory.cpp:2040` | crime/wanted evaluator inputs (wanted manager + actor context); prologue reads `[rcx+0x40]` and `[rcx+0x48]` per the sig bytes | no structure offsets written; the function's own reads are those in the quoted signature (`+0x40`, `+0x48`) | `src/game/inventory.cpp:2028-2041`, `:2066-2068` | `mem::InstallHook` logs `LOG_ERR("world: evaluate-wanted-state signature NOT FOUND - Witnessed/Assault crime bypass disabled.")` `src/mem/hooks.h:35`; the hook target stays null and removal is a no-op `src/mem/hooks.h:70-76`. This signature is also a **required readiness sentinel** — if it is absent the mod waits up to 180 s and then logs `"Gameplay-code readiness timed out after 180 seconds; installing available hooks only."` `src/core/mod.cpp:116-125` |
| C2 | hook (detour `hkRegisterCrimeEvent`) — **revision-gated** | "Central Crime Event Dispatcher & Territory Wanted State Register (sub_141595BC0): Intercepts and completely suppresses Murder, Assault, Theft, and Property Destruction events. Suppresses the on-screen "Crime: Murder / Assault" UI banner, minimap red wanted circle, and guard hostility." `src/game/offsets.h:1186-1188` | locator `kSig_RegisterCrimeEvent` = `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC C0 00 00 00 4D 8B F0` `src/game/offsets.h:1189-1190`; only probed when `core::MayProbeLegacyCrimeEventDispatcherForRevision(revision)` `src/game/inventory.cpp:2070` (true for `revision <= 2850`, `src/core/version_mapping.cpp:67-70`) | `void __fastcall(void* dispatcher, const char* eventName, void* eventData, void* eventContext)`; typedef `RegisterCrimeEvent_t` `src/game/crime_hook_contract.h:8-11`; returns `void`. Source comment on why arg 2 is pointer-sized: "PE 1.0.0.2760 reads argument 2 as a C-string pointer immediately after entry. Keeping it pointer-sized is load-bearing: declaring it uint32_t truncates 64-bit game addresses when the detour forwards to the original." `src/game/crime_hook_contract.h:5-7`; static-asserted in `tests/readiness_tests.cpp:37-39` | the crime-event dispatcher (rcx) and its 3 arguments; when `st.noBounty` the detour **returns without forwarding** `src/game/inventory.cpp:2050-2056` | no offsets read or written by Trinity | `src/game/inventory.cpp:2043-2059`, `:2070-2075` | on PE 2944+ the probe is **not attempted at all** and the fallback log is emitted: `LOG("world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.", revision)` `src/game/inventory.cpp:2076-2083`; on ≤ 2850 a missing signature logs `LOG_ERR("world: register-crime-event signature NOT FOUND - Crime event dispatch & UI banner bypass disabled.")` `src/mem/hooks.h:35` |
| C3 | memory write | WantedInfo row definition field `_increasePrice` | table global from `FindTableGlobal(kStr_WantedInfoTable)` `src/game/inventory.cpp:5703`; row def via `DefForRow(s_wantedGlobal, row, &def)` `src/game/inventory.cpp:5729` | `Write64(def + 0x18, 0)` `src/game/inventory.cpp:5740` | `WantedInfo` def row (`kStr_WantedInfoTable = "WantedInfo"` `src/game/offsets.h:1177`) | writes `+0x18` i64 (`kOff_WantedDef_IncreasePrice = 0x18; // i64` `src/game/offsets.h:1178`) | `src/game/inventory.cpp:5726-5741` | only runs when the table global resolved, the table pointer is ≥ `kMinPointer`, and the row count is `> 0 && <= kWantedRows_Max` (4096) `src/game/inventory.cpp:5711-5716`; otherwise `changed == 0` and `SetNoBounty` returns false `src/game/inventory.cpp:5752-5754` |
| C4 | memory read (original capture) | same as C3 | same as C3 | `Read64(def + 0x18, &orig)` `src/game/inventory.cpp:5736` | WantedInfo def row | reads `+0x18` i64 | `src/game/inventory.cpp:5733-5739` | a failed read `continue`s to the next row; the row is left uncaptured and never restored `src/game/inventory.cpp:5736` |
| C5 | memory write (restore) | same as C3 | same as C3 | `Write64(def + 0x18, static_cast<uint64_t>(s_origPrice[row]))` `src/game/inventory.cpp:5744` | WantedInfo def row | writes `+0x18` i64 | `src/game/inventory.cpp:5742-5746` | restore happens only for rows flagged in `s_wantedCaptured` (captured the first time the toggle was enabled this session) `src/game/inventory.cpp:5719-5724` |
| C6 | resolution routine (table global) | `WantedInfo` table global (`lea r8, "WantedInfo"` site) | `FindTableGlobal(kStr_WantedInfoTable)` `src/game/inventory.cpp:5703` → `FindTableGlobal` `src/game/inventory.cpp:1920-1965`: `mem::FindPatternIf(kSig_LeaR8Rip /* "4C 8D 05 ?? ?? ?? ??" src/game/offsets.h:533 */, &IsTableRef, &hunt)`, then walks up ≤ 0x120 bytes for the table-resolver prologue (`FindItemPrologueAbove` `src/game/inventory.cpp:1875-1893`) and forward ≤ 0x60 bytes for the first `48 8B 05`/`48 8B <reg>, cs:` RIP-relative global (`src/game/inventory.cpp:1926-1940`). **Fallback:** `if (core::GetGameVersion().revision >= 2625) { uintptr_t g = gameBase + 0x6350EE8; }` `src/game/inventory.cpp:1947-1955` | no call — RIP-relative resolution via `mem::ResolveRipAt(p, 7)` | the table global slot (a pointer to the table object) | reads the global slot pointer; the table itself is then read at `+0x08` (u32 count) and `+0x50`/`+0x58` (def array) by `DefForRow` `src/game/inventory.cpp:260-279` | `src/game/inventory.cpp:1920-1965`, `:260-279` | returns `0`; caller logs `"world: WantedInfo table not found - bounty price left alone."` `src/game/inventory.cpp:5704-5706` and `SetNoBounty` performs no writes |
| C7 | observer (presence probe only, no hook) | `kSig_EvaluateCrimeWantedState` presence | `trinity::mem::FindPattern(required[i])` inside the readiness predicate `src/core/mod.cpp:70-72` | none | none | none | `src/core/mod.cpp:41`, `:52`, `:70-72` | returns `false` from `GameplayCodeReady()` → readiness keeps polling (180 s cap) `src/core/mod.cpp:116-125` |
| C8 | observer / driver (game-thread upkeep) | none (no address) — drives C3/C5 and the `st.noBounty` reads seen by C1/C2 | n/a | n/a | n/a | n/a | `src/game/world.cpp:607-630` (re-applies on toggle change, on `Player::Ready()` transition, and after every map load because `s_lastPlayerReady` resets), `src/game/teleport.cpp:1645` | `SetNoBounty` refuses while `!Player::Ready() && enable` `src/game/inventory.cpp:5683-5688` |

WantedInfo definition offsets, verbatim:

```cpp
// src/game/offsets.h:1175-1180
    // --- Wanted level & Bounty (WantedInfo table & Crime Evaluator hook) ----
    // _increasePrice is how much a crime adds to your bounty (WantedInfo+0x18).
    inline constexpr const char* kStr_WantedInfoTable = "WantedInfo";
    inline constexpr uintptr_t kOff_WantedDef_IncreasePrice = 0x18; // i64
    inline constexpr uintptr_t kOff_WantedDef_IsBlocked     = 0x10; // u8
    inline constexpr uint32_t  kWantedRows_Max = 4096;
```

`kOff_WantedDef_IsBlocked` is **defined but never referenced** anywhere in `src/` (grep over
the whole tree: only `src/game/offsets.h:1179`). Likewise the header comment promises TribeInfo
handling that the implementation does not perform — see §I.

### B.4 MONEY (feature area 3b)

| # | Trinity role | Game function or address expression | How the address is obtained (locator constant / resolution routine / pointer chain) | Exact ABI Trinity assumes (registers/args, return) | Object/structure it operates on | Structure offsets read or written | Source file:line | Fail-safe if the address/locator is missing |
|---|---|---|---|---|---|---|---|---|
| M1 | hook (detour `hkGetMoney1`) — legacy revision only | `gameBase + 0x16077B0` | baked RVA, no signature: `MH_CreateHook(reinterpret_cast<void*>(gameBase + 0x16077B0), hkGetMoney1, …)` `src/game/inventory.cpp:2098`, `gameBase` from `GetModuleHandleA(nullptr)` `src/game/inventory.cpp:2093` | `int64_t __fastcall(void* rcx)` `src/game/inventory.cpp:55`; returns `g_walletSpoofValue` when `!= -1`, else forwards to trampoline `src/game/inventory.cpp:60` | wallet money getter #1 (`rcx` = its object) | none | `src/game/inventory.cpp:55-62`, `:2093-2101` | **gated off entirely** for `revision >= 2625` `src/game/inventory.cpp:2096`: `LOG("inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid).")` `src/game/inventory.cpp:2104` |
| M2 | hook (detour `hkGetMoney2`) — legacy revision only | `gameBase + 0x16078C0` | baked RVA `src/game/inventory.cpp:2099` | as M1 | wallet money getter #2 | none | `src/game/inventory.cpp:57`, `:61`, `:2099` | as M1 |
| M3 | hook (detour `hkGetMoney3`) — legacy revision only | `gameBase + 0x16081D0` | baked RVA `src/game/inventory.cpp:2100` | as M1 | wallet money getter #3 | none | `src/game/inventory.cpp:58`, `:62`, `:2100` | as M1 |
| M4 | hook (detour `hkGetItemQty`) | `GetItemQuantity`; comment: "GetItemQuantity (IDB sub_14A1330) is called by the HUD every time it shows a count, so hooking it captures the live inventory CONTAINER (its 1st arg) within a frame of loading" `src/game/offsets.h:580-583`; "1.18.0.2 Native GetItemQuantity (sub_1582880 @ 0x141582880). Called by HUD wallet, vendor shops, and crafting recipes" `src/game/offsets.h:588-589` | locator `kSig_InvGetItemQty` = `66 89 54 24 10 53 57 48 83 EC 28 0F B7 DA` `src/game/offsets.h:590-591`; `mem::InstallHook("inventory: item-count accessor", …, 4)` `src/game/inventory.cpp:2085-2086` | `int64_t __fastcall(void* container /*rcx*/, uint16_t typeId /*dx*/, void* keyPtr /*r8*/)` `src/game/inventory.cpp:64`; **this hook is mandatory** — if it fails the whole inventory subsystem returns false `src/game/inventory.cpp:2085-2091` | the live inventory container argument | reads/writes nothing itself for money; returns a spoofed count: `if (s_moneyTid && typeId == s_moneyTid && g_walletSpoofValue != -1) { if (g_walletSpoofValue > realQty) return g_walletSpoofValue; }` `src/game/inventory.cpp:1506-1515`; camp currencies `return realQty + g_campSpoofAddedValue;` `src/game/inventory.cpp:1531-1538` | `src/game/inventory.cpp:64`, `:1496-1541`, `:2085-2086` | modern sig missing → falls back to M5; both missing → `mem::InstallHook(..., "inventory disabled")` fails and `Inventory::Install()` returns false `src/game/inventory.cpp:2088-2090` |
| M5 | hook (detour `hkGetItemQty`, legacy locator) | legacy `GetItemQuantity` | locator `kSig_InvGetItemQty_Legacy` = `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B E8 0F B7 DA` `src/game/offsets.h:592-593`; `mem::InstallHook("inventory: item-count accessor legacy", …, 4)` `src/game/inventory.cpp:2088-2089` | as M4 | as M4 | as M4 | `src/game/inventory.cpp:2088-2090` | both failing disables all of `Inventory` (including the crime hooks' `State` consumers) `src/game/inventory.cpp:2088-2090` |
| M6 | memory write | `Money_Copper` item-definition `_maxStackCount` | `FindTypeIdByKey("Money_Copper")` `src/game/inventory.cpp:4557`, `:4861`, `:3163`; def via `DefForRow(g_itemTableGlobal, moneyTid, &moneyDef)` | `Write64(moneyDef + kOff_ItemDef_MaxStackCount, 999999999999ULL)` `src/game/inventory.cpp:4867`, `src/game/inventory.cpp:4576`, `src/game/inventory.cpp:3167` | `ItemInfo` row for `Money_Copper` | writes `+0x18` i64 (`kOff_ItemDef_MaxStackCount = 0x18;  // i64` `src/game/offsets.h:1096`) | `src/game/inventory.cpp:4864-4868` (`ConsolidateMoney`), `:4572-4578` (`SetDirectSilver`), `:3160-3170` (legacy `TU <= 1.18 only`, gated by `revision < 2625`) | skipped when `moneyTid == 0` or `DefForRow` fails; no log, the slot write path continues |
| M7 | memory write | `Money_Copper` item-definition `_applyMaxStackCap` | as M6 | `Write8(def + kOff_ItemDef_ApplyMaxStackCap, 0)` `src/game/inventory.cpp:4577`, `:3168`; also set to 0 for `Money_Copper` / `Money_Camp_Money` / `Money_Exchange` inside the global stack override `src/game/inventory.cpp:2669-2680` | `ItemInfo` row | writes `+0x111` u8 (`kOff_ItemDef_ApplyMaxStackCap = 0x111; // u8 bool` `src/game/offsets.h:1097`) | `src/game/inventory.cpp:4572-4578`, `:3160-3170`, `:2666-2681` | skipped if the def does not resolve |
| M8 | memory write | inventory slot fields (`_typeId` / quantity) in every resolved holder | holder pointers from the hook-captured container (`CurrentHolder()`, `ServerHolder()`), plus `SnapshotCandidates` → `HolderForContainer` | `Write64(slot + kOff_InvSlot_Quantity, …)` `src/game/inventory.cpp:4606`, `:4961`, `:4982`; `Write16(slot + kOff_InvSlot_TypeId, kInvSlot_EmptyType)` `src/game/inventory.cpp:4896`, `:4951`, `:4806`; `Write64(slot + kOff_InvSlot_Quantity, 0)` `src/game/inventory.cpp:4897`, `:4952` | inventory holder → buckets `+0x18` (ptr[]) / `+0x20` (u32 count) → bucket `+0x00` slots ptr / `+0x08` u16 slot-array size → slot stride `core::GetSlotStride()` | writes slot `+0x08` u16 typeId, `+0x10` i64 quantity (`src/game/offsets.h:850-852`; holder/bucket `src/game/offsets.h:767-769`, `:776`) | `src/game/inventory.cpp:4582-4673`, `:4876-4990`, `:3230-3347` | `HolderLooksValid` / `buckets < kMinPointer` / `bcount` bounds checks reject every malformed holder and return without writing `src/game/inventory.cpp:3249-3255`, `:4912-4914` |
| M9 | memory write (server mirror) | server-realm mirror of the same slot (idempotence guard against the per-frame reconcile) | `ServerHolder()` (or the first valid non-client candidate) `src/game/inventory.cpp:3233-3249` | `WriteServerMirror(bucketIdx, slotIdx, typeId, oldQty, value)` `src/game/inventory.cpp:3230-3231` | server holder → bucket → slot | writes the mirrored slot's quantity | `src/game/inventory.cpp:3226-3347`, money call sites `:4668`, `:4772`, `:4806`, `:4898`, `:4983` | returns `false` when no valid server holder exists; the client-side write stands, and the reconcile may revert it |
| M10 | native call chain (`CommitAdd` → game functions) reached from the money paths | `oItemValueCtor` (`kSig_TrItemValueCtor`), `oHolderInsert` (`kSig_InvHolderInsert`, the add-item **planner**, same function as the holder-insert hook), `oCommitPlacement` / `oCommitPlacement201` (`kSig_InvCommitPlacement` / `…201`), `oFreePlacements` (`kSig_InvFreePlacements` / `…201`), `oItemValueDtor` (`kSig_TrItemValueDtor`); the TEB lookup uses `NtQueryInformationThread` from `ntdll.dll` | resolved via `mem::FindPattern` + `mem::CountMatches` uniqueness checks `src/game/inventory.cpp:2121-2178` | `ItemValueCtor_t = void*(__fastcall*)(void* itemVal, uint16_t* typeId, int64_t qty)`; `CommitPlacement_t = void*(__fastcall*)(void* holder, int* err, void* unused, void* placement, uint16_t slotIdx)`; `CommitPlacement201_t = void*(__fastcall*)(void* holder, int* err, void* placement, uint16_t slotIdx)`; `FreePlacements_t = void(__fastcall*)(void* vec)` | holder / placement records inside the game's inventory transaction machinery | not enumerated here (see the inventory add-item note in `src/game/offsets.h`) | typedefs `src/game/inventory.cpp:81-98`; readiness gate `src/game/inventory.cpp:4026-4046`; money entry points `src/game/inventory.cpp:4678` (`CommitAdd(moneyTid, copperAmount)`), `:4684` (`AddItem`), `:4742` (`SpawnSilverPouches` → `AddItem`), `AddItem` queue `:5089-5101` | `CanCommitAuthoritativeAdd(ready, haveDef, clientH, serverH)` refuses when any primitive is missing and logs `LOG_WARN("inventory: add item %u x%lld refused - authoritative server holder unavailable (…")` `src/game/inventory.cpp:4035-4046` |
| M11 | hook (`hkGetHolder`, supporting) | `GetInventoryHolder` (IDB `sub_1CDD520` per `src/game/offsets.h:583`) | locator `kSig_InvGetHolder` = `40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41 30` `src/game/offsets.h:594-595`; `FindPattern` first to also get a **callable** pointer (`oGetHolder = reinterpret_cast<GetHolder_t>(holderAddr)` `src/game/inventory.cpp:2113`), then `mem::InstallHook("inventory: holder observer", kSig_InvGetHolder, …, 2)` `src/game/inventory.cpp:2194-2196` | `void* __fastcall(void* container)` (`GetHolder_t` `src/game/inventory.cpp:65`) | container → holder | reads `[rcx+0x68] → +0x20 → +0x30` per the signature | `src/game/inventory.cpp:2107-2113`, `:2194-2204` | missing signature → hard fail: `LOG_ERR("inventory: holder resolver signature NOT FOUND - inventory disabled."); return false;` `src/game/inventory.cpp:2108-2112`; hook failure alone is soft: `LOG_WARN("inventory: passive holder observer unavailable - waiting for an inventory transaction.")` `src/game/inventory.cpp:2203` |
| M12 | hook (`hkCommit` family, supporting) | transaction commit (`kSig_InvCommit` modern / `kSig_InvCommit_Pre201` legacy) and holder-insert (`kSig_InvHolderInsert` / `…_Legacy`) | `mem::InstallHook(...)` `src/game/inventory.cpp:2244`, `:2252`, `:2269`, `:2273`, `:2276` | `Commit_t = void*(__fastcall*)(void* holder, void* err, void* container, void* items, void* out, uint8_t, uint8_t)` `src/game/inventory.cpp:71`; `HolderInsert_t = void*(__fastcall*)(void*, void*, void*, void*, uint16_t, void*, uint8_t, uint8_t, uint8_t)` `src/game/inventory.cpp:68-69` | inventory transactions (these are what capture the client/server holders the money writes target) | not enumerated here | `src/game/inventory.cpp:2244-2280` | hook failures are soft; holder capture then depends on the observer M11 |

Money write behaviour — the "passive mirror" caveat, verbatim from the offsets comments:

> `// Both have unique byte signatures. (Live-confirmed: a walk from here lists`
> `// every item, and writing a slot's quantity sticks - the game even re-stacks`
> `// it. Money is the exception: its inventory slot is a passive mirror, so`
> `// editing it does not change spendable currency.)`
> — `src/game/offsets.h:584-587`

That is why the money path additionally uses the display spoof (`g_walletSpoofValue`), and why
the source says so explicitly:

```cpp
// src/game/inventory.cpp:4689-4694
        // The memory injection has been delegated to Wallet Native Hooks.
        // We just update the HUD value through the spoof so the engine saves it automatically upon any transaction.
        if (copperAmount == 0)
            g_walletSpoofValue = -1; // Disable spoof
        else
            g_walletSpoofValue = copperAmount; // Enable spoof
```

The heap-scanner alternative was deliberately removed:

```cpp
// src/game/inventory.cpp:4540-4543
            if (safe)
            {
                // Heap Scanner disabled: Removed unsafe WeMod memory scanning that caused memory corruption.
            }
```

and `BackgroundCurrencyScan` (`src/game/inventory.cpp:4512-4548`) is now a scan-shaped no-op:
it computes `modBase`/`modEnd`, walks regions with `VirtualQuery`, and never reads or writes
game memory. It has **no caller** in `src/` (grep sweep).

Observation about M1–M3 (source-derived, not a claim about intent): `MH_CreateHook` alone
leaves a MinHook detour disabled; the only enable-all call in the tree is
`MH_EnableHook(MH_ALL_HOOKS)` at `src/hooks/dx12_hook.cpp:1570`, inside
`hooks::InstallDX12Hooks()`, which `Mod::Initialize` calls at `src/core/mod.cpp:103` —
**before** `game::Inventory::Install()` at `src/core/mod.cpp:131`. No later
`MH_EnableHook`/`MH_ApplyQueued` exists in `src/` (grep over the whole tree). The three legacy
money getters are therefore created but never enabled; the wallet spoof still reaches the HUD
through M4/M5, which use `mem::InstallHook` (which does call `MH_EnableHook`,
`src/mem/hooks.h:54`).

---

## C. Byte patch contract — the WORKER patch

### C.1 The contract, verbatim from the source

```cpp
// src/game/offsets.h:1835-1850
    // --- Worker level and ability unlock patch -----------------------------
    // Exact injection contract from the user-supplied AA script: force the
    // worker grade-selector's first `jne` branch to always return the top
    // tier (5). PE 2850: CrimsonDesert.exe+20967CC; PE 2944:
    // CrimsonDesert.exe+214BE8C (unique match, verified 2026-09-19).
    // Original `JNZ +0x95` -> patched `JMP +0x96 / NOP`.
    inline constexpr size_t kWorkerPatchSize = 6;
    inline constexpr uint8_t kWorkerPatchOriginal[kWorkerPatchSize] =
        { 0x0F, 0x85, 0x95, 0x00, 0x00, 0x00 };
    inline constexpr uint8_t kWorkerPatchEnabled[kWorkerPatchSize] =
        { 0xE9, 0x96, 0x00, 0x00, 0x00, 0x90 };

    // Include the continuation after the injection bytes so an accidental
    // matching conditional branch elsewhere cannot be patched.
    inline constexpr const char* kSig_WorkerMaxLevelAndSkills =
        "0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5";
```

### C.2 Target address expression and derivation

- **Target address expression:** the address returned by the **single** match of
  `kSig_WorkerMaxLevelAndSkills`, i.e. `matches[0]` of
  `mem::FindAllMatches(kSig_WorkerMaxLevelAndSkills, 2)` (`src/game/worker.cpp:57`, `:65`).
  Note the match starts **at the branch bytes themselves**, so `g_patchTarget` points at the
  `0F 85 …` opcode, not at a function entry.
- **Derivation:** locator constant (AOB), never a baked address. The only addresses in the
  source are documentation of where the pattern lives on two known builds:
  "PE 2850: CrimsonDesert.exe+20967CC; PE 2944: CrimsonDesert.exe+214BE8C (unique match,
  verified 2026-09-19)" `src/game/offsets.h:1838-1839`; and the `worker_logic.h` comment
  "PE 2944 keeps the same grade-selector branch bytes (verified unique at 0x14214BE8C on
  2026-09-19)" `src/game/worker_logic.h:15-17`.
- **Uniqueness requirement:** exactly one match, else the feature is disabled
  (`src/game/worker.cpp:57-63`). The trailing continuation bytes
  `48 8B 7C 24 20 41 0F B7 D5` are part of the signature **specifically** so that "an
  accidental matching conditional branch elsewhere cannot be patched"
  (`src/game/offsets.h:1847-1848`).

### C.3 Original vs patched bytes

| | Bytes (6) | Disassembly (as annotated by the source) |
|---|---|---|
| ORIGINAL | `0F 85 95 00 00 00` | `JNZ +0x95` (`jne rel32`, displacement `0x00000095`) — `src/game/offsets.h:1840` |
| PATCHED | `E9 96 00 00 00 90` | `JMP +0x96 / NOP` (`jmp rel32` + one `nop`) — `src/game/offsets.h:1840`, `src/game/offsets.h:1844-1845` |

Arithmetic check on the quoted bytes (both forms land on the same target and occupy the same
6 bytes): original `jne` target = 6 (length) + 0x95 = 0x9B from the branch start; patched
`jmp` target = 5 (length) + 0x96 = 0x9B from the branch start, with the 6th byte turned into
`0x90` so the following instruction is not displaced.

### C.4 Surrounding instruction sequence

The signature supplies the immediate continuation (`src/game/offsets.h:1849-1850`):

```
0F 85 95 00 00 00        jne  +0x95            <- patch site (6 bytes; replaced wholesale)
48 8B 7C 24 20           mov  rdi, [rsp+20h]   <- continuation included in the AOB
41 0F B7 D5              movzx edx, r15w       <- continuation included in the AOB
```

Trinity never disassembles this site and never relocates instructions: it overwrites exactly
`kWorkerPatchSize` (6) bytes at the match address and restores the same 6 bytes.

### C.5 Install-time validation (fail-closed)

```cpp
// src/game/worker.cpp:50-93
        const int revision = static_cast<int>(core::GetGameVersion().revision);
        if (!WorkerPatchSupportedForRevision(revision))
        {
            LOG_WARN("worker: supplied level/ability patch disabled for unsupported PE revision %d.", revision);
            return false;
        }

        const auto matches = mem::FindAllMatches(kSig_WorkerMaxLevelAndSkills, 2);
        if (matches.size() != 1)
        {
            LOG_WARN("worker: level/ability patch signature expected one match, found %zu; feature disabled.",
                     matches.size());
            return false;
        }

        g_patchTarget = matches[0];
        uint8_t current[kWorkerPatchSize] = {};
        if (!ReadPatchBytes(current))
        {
            LOG_WARN("worker: exact patch target at %p could not be read; feature disabled.",
                     reinterpret_cast<void*>(g_patchTarget));
            g_patchTarget = 0;
            return false;
        }

        if (BytesEqual(current, kWorkerPatchOriginal))
        {
            g_enabled = false;
        }
        else if (BytesEqual(current, kWorkerPatchEnabled))
        {
            g_enabled = true;
        }
        else
        {
            LOG_WARN("worker: target at %p contains unexpected bytes; feature disabled.",
                     reinterpret_cast<void*>(g_patchTarget));
            g_patchTarget = 0;
            return false;
        }
```

Bounded read helper and the validity condition it enforces (both ends of the 6-byte window
must be valid user pointers):

```cpp
// src/game/worker.cpp:21-39
        bool ReadPatchBytes(uint8_t* out)
        {
            if (!out || !mem::IsValidUserPtr(g_patchTarget) ||
                !mem::IsValidUserPtr(g_patchTarget + kWorkerPatchSize - 1))
                return false;

            for (size_t i = 0; i < kWorkerPatchSize; ++i)
            {
                if (!mem::Read8(g_patchTarget + i, &out[i]))
                    return false;
            }
            return true;
        }

        bool BytesEqual(const uint8_t* left, const uint8_t* right)
        {
            return left && right &&
                   std::memcmp(left, right, kWorkerPatchSize) == 0;
        }
```

`mem::IsValidUserPtr` accepts `kMinPointer (0x10000000) … 0x00007FFFFFFFFFFF`
(`src/mem/safe_memory.h:18-23`, `src/game/offsets.h:27`).

### C.6 Enable / disable transitions and the safety (immutability) condition

```cpp
// src/game/worker.cpp:142-193
    bool Worker::SetEnabled(bool enabled)
    {
        if (!g_ready || !g_patchTarget)
            return false;

        uint8_t current[kWorkerPatchSize] = {};
        if (!ReadPatchBytes(current))
            return false;

        if (enabled)
        {
            if (BytesEqual(current, kWorkerPatchEnabled))
            {
                g_enabled = true;
                return true;
            }

            if (!CanTransitionWorkerPatch(WorkerPatchState::Original,
                                          WorkerPatchState::Patched,
                                          current, kWorkerPatchOriginal,
                                          kWorkerPatchSize))
                return false;

            if (!mem::PatchMemory(g_patchTarget, kWorkerPatchEnabled, kWorkerPatchSize))
                return false;

            g_enabled = true;
            LOG_OK("worker: max level and all abilities patch enabled @ %p.",
                   reinterpret_cast<void*>(g_patchTarget));
            return true;
        }

        if (BytesEqual(current, kWorkerPatchOriginal))
        {
            g_enabled = false;
            return true;
        }

        if (!CanTransitionWorkerPatch(WorkerPatchState::Patched,
                                      WorkerPatchState::Original,
                                      current, kWorkerPatchEnabled,
                                      kWorkerPatchSize))
            return false;

        if (!mem::PatchMemory(g_patchTarget, kWorkerPatchOriginal, kWorkerPatchSize))
            return false;

        g_enabled = false;
        LOG_OK("worker: max level and all abilities patch disabled @ %p.",
               reinterpret_cast<void*>(g_patchTarget));
        return true;
    }
```

The immutability condition is the byte-equality predicate:

```cpp
// src/game/worker_logic.h:23-45
    // Validate the bytes that are currently at the injection point before a
    // controller writes. The expected buffer is selected by the caller for
    // the requested transition, so every unexpected state fails closed.
    inline bool CanTransitionWorkerPatch(WorkerPatchState from,
                                         WorkerPatchState to,
                                         const uint8_t* current,
                                         const uint8_t* expected,
                                         size_t size)
    {
        if (!current || !expected || size != 6)
            return false;

        if (from == to)
            return std::memcmp(current, expected, size) == 0;

        if (from == WorkerPatchState::Original && to == WorkerPatchState::Patched)
            return std::memcmp(current, expected, size) == 0;

        if (from == WorkerPatchState::Patched && to == WorkerPatchState::Original)
            return std::memcmp(current, expected, size) == 0;

        return false;
    }
```

Note the caller passes the **original** buffer as `expected` when enabling
(`src/game/worker.cpp:159-163`) and the **enabled** buffer as `expected` when disabling
(`src/game/worker.cpp:180-184`) — i.e. a write only happens from a byte state that is exactly
known. `size != 6` is rejected outright, which pins `CanTransitionWorkerPatch` to
`kWorkerPatchSize`.

### C.7 Exact restoration logic

```cpp
// src/game/worker.cpp:109-130
    void Worker::Remove()
    {
        if (g_ready && g_patchTarget)
        {
            uint8_t current[kWorkerPatchSize] = {};
            if (ReadPatchBytes(current) && BytesEqual(current, kWorkerPatchEnabled))
            {
                if (!mem::PatchMemory(g_patchTarget, kWorkerPatchOriginal, kWorkerPatchSize))
                    LOG_WARN("worker: could not restore original patch bytes at shutdown @ %p.",
                             reinterpret_cast<void*>(g_patchTarget));
            }
            else if (!BytesEqual(current, kWorkerPatchOriginal))
            {
                LOG_WARN("worker: shutdown found unexpected bytes at %p; left target untouched.",
                         reinterpret_cast<void*>(g_patchTarget));
            }
        }

        g_patchTarget = 0;
        g_ready = false;
        g_enabled = false;
    }
```

So restoration is byte-conditional: restore **only** when the site currently holds exactly
`kWorkerPatchEnabled`; leave the site untouched (with a warning) in every other state,
including the already-original case. `Remove()` is called from `Mod::Shutdown()`
(`src/core/mod.cpp:163`).

The byte write itself is guarded:

```cpp
// src/mem/safe_memory.h:161-180
    // Safely unprotects, modifies, reprotects, and flushes instructions for a memory region
    inline bool PatchMemory(uintptr_t addr, const void* data, size_t size)
    {
        if (!IsValidUserPtr(addr) || !data || size == 0) return false;
        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        __try
        {
            memcpy(reinterpret_cast<void*>(addr), data, size);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            VirtualProtect(reinterpret_cast<void*>(addr), size, oldProtect, &oldProtect);
            return false;
        }
        VirtualProtect(reinterpret_cast<void*>(addr), size, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), size);
        return true;
    }
```

### C.8 What behaviour the patch changes

- Per the source: it forces "the worker grade-selector's first `jne` branch to always return
  the top tier (5)" `src/game/offsets.h:1836-1838`. The user-visible label is
  `"Max Worker Level & Skills"` with description `"Unlocks maximum level and all worker
  abilities."` `src/gui/menu.cpp:134-136`.
- Mechanically: the conditional branch is replaced by an unconditional jump to the **same**
  destination (see §C.3 arithmetic), so the branch is taken irrespective of the flags the
  selector computed; the trailing `NOP` keeps the 6-byte footprint so the following
  `mov rdi,[rsp+20h]` / `movzx edx,r15w` sequence is not displaced.
- Supported revisions only: `WorkerPatchSupportedForRevision` is true for **2850 and 2944**
  only (`src/game/worker_logic.h:18-21`), and the static tests assert 2760 and 0 must **not**
  inherit it (`tests/readiness_tests.cpp:475-478`).

---

## D. Hook table

| Hook name | Target | Type | Callback signature | Behaviour | Removal / restore |
|---|---|---|---|---|---|
| `friendly: NPC Trust Multiplier observer` | NPC relationship-record setter (TU 2.01 `kSig_FriendlySetNpc201`; TU 2.00 fallback `kSig_FriendlySetNpc`) | MinHook detour, created+enabled by the local `CreateAndEnable` helper `src/game/friendly.cpp:227-234` (not `mem::InstallHook`) | `void* __fastcall hkSetNpc(void* mapOwner, void* record)` `src/game/friendly.cpp:26`, `:197-202` | `ObserveAndScaleTrust` then forward to trampoline; SEH-wrapped | `MH_DisableHook` + `MH_RemoveHook` + null target `src/game/friendly.cpp:317-322`; also clears `s_lastTrustMap` and the trampolines `:341-347` |
| `friendly: pet/mount Trust Multiplier observer` | pet/vehicle relationship-record setter (TU 2.01 `kSig_FriendlySetPet201`; TU 2.00 fallback `kSig_FriendlySetPet`) | MinHook detour via `CreateAndEnable` | `void* __fastcall hkSetPet(void* mapOwner, void* record)` `src/game/friendly.cpp:28`, `:204-209` | as above | `src/game/friendly.cpp:323-328` |
| `friendly: NPC in-place trust observer` | NPC relationship-map lookup `kSig_FriendlyGetNpc201` | MinHook detour via `CreateAndEnable` | `void* __fastcall hkGetNpc(void* mapOwner, void* actor)` `src/game/friendly.cpp:29`, `:211-217` | forwards **first**, then `ObserveLiveTrust` on the returned record; SEH-wrapped | `src/game/friendly.cpp:329-334` |
| `friendly: pet/mount in-place trust observer` | pet/vehicle relationship-map lookup `kSig_FriendlyGetPet201` | MinHook detour via `CreateAndEnable` | `void* __fastcall hkGetPet(void* mapOwner, void* actor)` `src/game/friendly.cpp:31`, `:219-225` | as above | `src/game/friendly.cpp:335-340` |
| `world: evaluate-wanted-state` | `kSig_EvaluateCrimeWantedState` | `mem::InstallHook` with `maxMatches = 0` `src/game/inventory.cpp:2066-2068` | `uint8_t __fastcall hkEvaluateCrimeWantedState(void* wantedMgr, void* actorCtx)` `src/game/inventory.cpp:2032` | if `st.noBounty` → `return 7` (`eWantedState_None`) without calling the original; else forward (and `return 0` if the trampoline is null) | `mem::RemoveHook(&g_evalWantedTarget)` `src/game/inventory.cpp:2342` → `MH_DisableHook`+`MH_RemoveHook`, target cleared `src/mem/hooks.h:70-76` |
| `world: register-crime-event` | `kSig_RegisterCrimeEvent` — **only when `revision <= 2850`** `src/core/version_mapping.cpp:67-70` | `mem::InstallHook` with `maxMatches = 0` `src/game/inventory.cpp:2072-2074` | `void __fastcall hkRegisterCrimeEvent(void* dispatcher, const char* eventName, void* eventData, void* eventContext)` `src/game/inventory.cpp:2046-2047`; type `RegisterCrimeEvent_t` `src/game/crime_hook_contract.h:8-11` | if `st.noBounty` → `return` (fully suppresses the event); else forward | `mem::RemoveHook(&g_registerCrimeTarget)` `src/game/inventory.cpp:2343` |
| `inventory: item-count accessor` (mandatory) | `kSig_InvGetItemQty` | `mem::InstallHook`, `maxMatches = 4` `src/game/inventory.cpp:2085-2086` | `int64_t __fastcall hkGetItemQty(void* container, uint16_t typeId, void* keyPtr)` `src/game/inventory.cpp:64` | captures the live container/holder; returns spoofed counts for `Money_Copper` and camp currencies | `mem::RemoveHook(&g_qtyTarget)` `src/game/inventory.cpp:2335` |
| `inventory: item-count accessor legacy` (fallback) | `kSig_InvGetItemQty_Legacy` | `mem::InstallHook`, `maxMatches = 4` `src/game/inventory.cpp:2088-2089` | as above | as above | `src/game/inventory.cpp:2335` |
| `hkGetMoney1/2/3` (legacy money display) | `gameBase + 0x16077B0` / `+0x16078C0` / `+0x16081D0` | raw `MH_CreateHook` only, `revision < 2625` `src/game/inventory.cpp:2096-2101` | `int64_t __fastcall(void* rcx)` `src/game/inventory.cpp:55` | return `g_walletSpoofValue` when active, else forward | **no removal path** — `Inventory::Remove()` (`src/game/inventory.cpp:2328-2349`) removes the `mem::InstallHook` hooks but never touches `oGetMoney1/2/3`; `MH_DisableHook(MH_ALL_HOOKS)` at shutdown covers them (`src/hooks/dx12_hook.cpp:1593`) |
| `inventory: holder observer` (supporting) | `kSig_InvGetHolder` | `mem::InstallHook`, `maxMatches = 2` `src/game/inventory.cpp:2194-2196` | `GetHolder_t = void* __fastcall(void* container)` `src/game/inventory.cpp:65` | passive capture of both realms' holders | `mem::RemoveHook(&g_holderTarget)` `src/game/inventory.cpp:2339` |
| `inventory: transaction commit` (supporting) | `kSig_InvCommit` (modern) or `kSig_InvCommit_Pre201` | `mem::InstallHook` `src/game/inventory.cpp:2244`, `:2252` | `Commit_t` `src/game/inventory.cpp:71` | captures container/holder pairs during commits | `mem::RemoveHook(&g_commitTarget)` / `(&g_commit201Target)` `src/game/inventory.cpp:2337-2338` |
| `inventory: holder-insert` (supporting) | `kSig_InvHolderInsert` (+ `kLegacyHolderInsertSigs` path / `kSig_InvHolderInsert_Legacy`) | `mem::InstallHook` `src/game/inventory.cpp:2269`, `:2273`, `:2276` | `HolderInsert_t` `src/game/inventory.cpp:68-69` | insert planner; also the callable `oHolderInsert` used by `CommitAdd` | `mem::RemoveHook(&g_insTarget)` `src/game/inventory.cpp:2336` |
| `inventory: slot-expansion setter` (supporting) | `kSig_InvSetExpandSlots` | `mem::InstallHook` `src/game/inventory.cpp:2222` | `SetExpandSlots_t` `src/game/inventory.cpp:72-75` | drives the engine's own expansion setter for the slot-size override | `mem::RemoveHook(&g_expandTarget)` **after** the restore, "which still calls its trampoline" `src/game/inventory.cpp:2340-2341` |

Worker installs **no hooks**: it is a pure byte patch (`src/game/worker.cpp:42-107`), so it does
not appear in this table.

---

## E. Crime / wanted investigation status

This section records everything the source says about the three crime-related game touch
points, and why one of them is unavailable on the current revision. All comments are quoted
verbatim.

### E.1 The two signatures and what each was believed to do

```cpp
// src/game/offsets.h:1182-1190
    // Evaluates crime records on actors; returning 7 (eWantedState_None) blocks Witness/Pursuit/Bounty
    inline constexpr const char* kSig_EvaluateCrimeWantedState =
        "48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 48 8B DA 4C 6B D9 38 41 B0 07";

    // Central Crime Event Dispatcher & Territory Wanted State Register (sub_141595BC0):
    // Intercepts and completely suppresses Murder, Assault, Theft, and Property Destruction events.
    // Suppresses the on-screen "Crime: Murder / Assault" UI banner, minimap red wanted circle, and guard hostility.
    inline constexpr const char* kSig_RegisterCrimeEvent =
        "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC C0 00 00 00 4D 8B F0";
```

- **Wanted-state evaluator** (`kSig_EvaluateCrimeWantedState`): hooked on every revision
  Trinity supports, unconditionally, at `src/game/inventory.cpp:2066-2068`. The no-bounty
  branch returns the enum value the comment names:
  `// 7 = eWantedState_None (completely blocks Witness, Suspect, Assault, and Pursuit)`
  `src/game/inventory.cpp:2037`.
- **Crime UI banner dispatcher** (`kSig_RegisterCrimeEvent`, "the legacy crime dispatcher
  AOB"): the four-argument dispatcher whose suppression is described as preventing "the
  on-screen \"Crime: Murder\" / \"Crime: Assault\" banner", "the minimap red wanted circle",
  and keeping "guards 100% peaceful!"
  (`src/game/inventory.cpp:2052-2054`).

### E.2 Why the banner dispatcher is unavailable on the current revision

The gate is a revision predicate, defined and documented as follows:

```cpp
// src/core/version_mapping.h:37-40
    // The four-argument crime-event dispatcher was verified only through PE
    // 2850. PE 2944 no longer contains that function contract, so probing its
    // old signature would report an expected incompatibility as an error.
    bool MayProbeLegacyCrimeEventDispatcherForRevision(uint16_t revision);
```

```cpp
// src/core/version_mapping.cpp:67-70
    bool MayProbeLegacyCrimeEventDispatcherForRevision(uint16_t revision)
    {
        return revision <= 2850;
    }
```

The call site and its verbatim fallback comment:

```cpp
// src/game/inventory.cpp:2070-2083
        if (core::MayProbeLegacyCrimeEventDispatcherForRevision(revision))
        {
            mem::InstallHook("world: register-crime-event", kSig_RegisterCrimeEvent,
                             "Crime event dispatch & UI banner bypass disabled",
                             &hkRegisterCrimeEvent, &oRegisterCrimeEvent, &g_registerCrimeTarget, 0);
        }
        else
        {
            // PE 2944's prior event dispatcher no longer exists.  No Bounty
            // remains supported through WantedInfo and wanted-state evaluation;
            // only crime-banner/minimap/guard-dispatch suppression is absent.
            LOG("world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.",
                static_cast<unsigned>(revision));
        }
```

Static tests pin the boundary:

```cpp
// tests/readiness_tests.cpp:172-182
    void Pe2944SkipsTheObsoleteCrimeEventDispatcherProbe()
    {
        using trinity::core::MayProbeLegacyCrimeEventDispatcherForRevision;

        Expect(!MayProbeLegacyCrimeEventDispatcherForRevision(2944),
               "PE 2944 must not probe the removed legacy crime dispatcher or emit a misleading missing-signature error");
        Expect(!MayProbeLegacyCrimeEventDispatcherForRevision(2945),
               "an unknown newer revision must not inherit the legacy crime-dispatcher probe");
        Expect(MayProbeLegacyCrimeEventDispatcherForRevision(2760),
               "the verified legacy dispatcher probe must remain available to its original PE family");
    }
```

### E.3 The ABI preservation requirement (why the forward is pointer-sized)

```cpp
// src/game/crime_hook_contract.h:5-11
    // PE 1.0.0.2760 reads argument 2 as a C-string pointer immediately after
    // entry. Keeping it pointer-sized is load-bearing: declaring it uint32_t
    // truncates 64-bit game addresses when the detour forwards to the original.
    using RegisterCrimeEvent_t = void(__fastcall*)(void* dispatcher,
                                                   const char* eventName,
                                                   void* eventData,
                                                   void* eventContext);
```

Compile-time enforcement: `tests/readiness_tests.cpp:37-39` static-asserts this type.

### E.4 The WantedInfo path (what remains on 2944)

- Table: `WantedInfo` (`src/game/offsets.h:1177`), located by `FindTableGlobal`
  (`src/game/inventory.cpp:5703`) with a hardcoded TU 2.00+ fallback
  `gameBase + 0x6350EE8` for `revision >= 2625`
  (`src/game/inventory.cpp:1947-1955`).
- Field: `_increasePrice` at `+0x18` i64 — "how much a crime adds to your bounty"
  (`src/game/offsets.h:1176-1178`).
- Behaviour: zero on enable, restore each row's captured original on disable
  (`src/game/inventory.cpp:5731-5746`).
- Gate: refused entirely while the player is not in world —
  `// Gating safety: Do NOT modify table definitions during startup / main menu.`
  `// Wait until player is loaded into world so entity combat flags are not corrupted.`
  `src/game/inventory.cpp:5683-5684`; `if (!Player::Ready() && enable) { return false; }`
  `src/game/inventory.cpp:5685-5688`.
- Idempotence: an internal `s_activeState` short-circuits repeat calls with the same target
  state (`src/game/inventory.cpp:5690-5693`).

### E.5 Header promise not implemented (explicit gap)

`src/game/inventory.h:319-320` claims TribeInfo participation:

> `// No Bounty: zero every WantedInfo row's _increasePrice and TribeInfo's _wantedCrimeType`
> `// so crimes do not accumulate bounties or trigger wanted aggression. Session-only.`

The implementation of `SetNoBounty` (`src/game/inventory.cpp:5681-5755`) contains **no**
TribeInfo access and no `_wantedCrimeType` write: the grep sweep over all of `src/` finds
`_wantedCrimeType` only in that header comment, and `tribeinfo` only in the generic table
fallback at `src/game/inventory.cpp:1956-1961`, which nothing in the No Bounty path calls.
So the TribeInfo half of the documented contract **is not implemented in this revision**.
Likewise `kOff_WantedDef_IsBlocked` (`src/game/offsets.h:1179`) is defined but never used.

### E.6 Session-only semantics and cleanup

- Toggle description: `"Crimes stop adding to your bounty or alerting faction guards (session-only, safe for save files)."`
  `src/gui/menu.cpp:104`.
- `Inventory::Remove()` does **not** call `SetNoBounty(false)`: it restores the stack/slot
  table overrides and removes hooks only (`src/game/inventory.cpp:2328-2349`). The zeroed
  `_increasePrice` values therefore survive until the toggle is switched off or the process
  exits; nothing in the source restores them on unload.
- The evaluator detour's `return 7` and the dispatcher's early `return` are both driven purely
  by `State::Get().noBounty`, so removing the hooks (or never installing the dispatcher)
  returns the game to native behaviour immediately.

---

## F. Fail-closed / version-gated behaviour

| Condition | Exact source | Log message emitted |
|---|---|---|
| Worker patch on any revision other than 2850/2944 | `src/game/worker.cpp:50-55`; predicate `src/game/worker_logic.h:18-21` | `LOG_WARN("worker: supplied level/ability patch disabled for unsupported PE revision %d.", revision)` |
| Worker AOB match count ≠ 1 | `src/game/worker.cpp:57-63` | `LOG_WARN("worker: level/ability patch signature expected one match, found %zu; feature disabled.", matches.size())` |
| Worker target unreadable (either end of the 6-byte window invalid) | `src/game/worker.cpp:66-73`; `ReadPatchBytes` `:21-33` | `LOG_WARN("worker: exact patch target at %p could not be read; feature disabled.", …)` |
| Worker target bytes neither original nor patched | `src/game/worker.cpp:83-89` | `LOG_WARN("worker: target at %p contains unexpected bytes; feature disabled.", …)` |
| Worker saved ON state cannot be applied at install | `src/game/worker.cpp:95-100` | `LOG_WARN("worker: saved max level/skills state could not be applied; reset to OFF.")`; `state.workerMaxLevelAndSkills = false` |
| Worker restore impossible at shutdown | `src/game/worker.cpp:114-124` | `LOG_WARN("worker: could not restore original patch bytes at shutdown @ %p.", …)` / `LOG_WARN("worker: shutdown found unexpected bytes at %p; left target untouched.", …)` |
| Worker `SetEnabled` rejected (any byte-state mismatch, unreadable target, or `PatchMemory` failure) | `src/game/worker.cpp:144-149`, `:159-166`, `:180-187` | none from `SetEnabled`; the menu reverts the toggle and toasts `"Worker patch could not be applied"` `src/gui/menu.cpp:139-143` |
| Crime-event dispatcher on PE 2944+ (or any revision > 2850) | `src/game/inventory.cpp:2070-2083`; `src/core/version_mapping.cpp:67-70` | `LOG("world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.", …)` — **not** an error; the probe is deliberately skipped so no misleading missing-signature error is produced (`src/core/version_mapping.h:37-39`) |
| `kSig_EvaluateCrimeWantedState` missing | `src/game/inventory.cpp:2066-2068` → `src/mem/hooks.h:31-37` | `LOG_ERR("world: evaluate-wanted-state signature NOT FOUND - Witnessed/Assault crime bypass disabled.")` |
| `kSig_RegisterCrimeEvent` missing (≤ 2850 only) | `src/game/inventory.cpp:2072-2074` → `src/mem/hooks.h:31-37` | `LOG_ERR("world: register-crime-event signature NOT FOUND - Crime event dispatch & UI banner bypass disabled.")` |
| Any `mem::InstallHook` AOB resolves to more than one site | `src/mem/hooks.h:39-41` | `LOG_WARN("%s signature ambiguous (%zu); hooking first.", context, matches)` |
| `MH_CreateHook` / `MH_EnableHook` failure inside `mem::InstallHook` | `src/mem/hooks.h:44-61` | `LOG_ERR("%s: MH_CreateHook failed (%s) - %s.", …)` / `LOG_ERR("%s: MH_EnableHook failed (%s) - %s.", …)` |
| WantedInfo table global unresolved | `src/game/inventory.cpp:5700-5707` | `LOG("world: WantedInfo table not found - bounty price left alone.", …)` |
| WantedInfo row count out of range (`0` or `> 4096`) | `src/game/inventory.cpp:5716` | none; the whole write loop is skipped |
| No Bounty requested before the player is in world | `src/game/inventory.cpp:5683-5688` | none; returns `false`, retried by `World::Tick` `src/game/world.cpp:619-624` |
| Legacy money hooks on `revision >= 2625` | `src/game/inventory.cpp:2094-2105` | `LOG("inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid).")` |
| Money `typeId` unresolved | `src/game/inventory.cpp:4557-4558`, `:4711-4712`, `:4736-4740`, `:4751-4754`, `:4861` | `LOG_WARN("inventory: Silver_Pack not found in item table.")` `:4739`; the other paths return `false` silently |
| No pouches to cash in | `src/game/inventory.cpp:4823-4826` | `LOG_WARN("inventory: no Full Silver Pouches found to cash in.")` |
| Add-item primitives incomplete (reached by pouch spawn / empty-wallet seed) | `src/game/inventory.cpp:4035-4046` | `LOG_WARN("inventory: add item %u x%lld refused - authoritative server holder unavailable (…")` |
| Native item ctor unavailable | `src/game/inventory.cpp:2152-2155` | `LOG_WARN("inventory: native TrItemValue constructor unavailable - Add Item will be refused.")` |
| Holder resolver missing (hard-fails all of `Inventory`, including the crime hooks) | `src/game/inventory.cpp:2107-2112` | `LOG_ERR("inventory: holder resolver signature NOT FOUND - inventory disabled.")` |
| Holder observer hook failure | `src/game/inventory.cpp:2200-2204` | `LOG_WARN("inventory: passive holder observer unavailable - waiting for an inventory transaction.")` |
| Trust setters/lookups all missing | `src/game/friendly.cpp:296-299` | `LOG_ERR("friendly: Trust Multiplier setters NOT FOUND - feature disabled.")` |
| Trust 2.01 setters missing (both) | `src/game/friendly.cpp:241-245` | falls back to the TU 2.00 signatures before giving up |
| Gameplay-code readiness (which requires `kSig_EvaluateCrimeWantedState`) | `src/core/mod.cpp:36-58`, `:116-125` | `LOG_WARN("Gameplay-code readiness timed out after 180 seconds; installing available hooks only.")` |
| `Settings` clamps | `st.trustMultVal` clamped to `1.0f … 25.0f` `src/core/settings.cpp:247`; UI slider identical range `src/gui/menu.cpp:156` | — |

---

## G. Runtime validation hooks — exact Trinity log format strings per feature

### Trust / Friendly (`src/game/friendly.cpp`)

| Format string | Line | When |
|---|---|---|
| `"friendly: NPC Trust Multiplier observer installed @ %p"` | `:253` | NPC setter hook installed |
| `"friendly: pet/mount Trust Multiplier observer installed @ %p"` | `:265` | pet setter hook installed |
| `"friendly: NPC in-place trust observer installed @ %p"` | `:279` | NPC lookup hook installed |
| `"friendly: pet/mount in-place trust observer installed @ %p"` | `:290` | pet lookup hook installed |
| `"friendly: Trust Multiplier setters NOT FOUND - feature disabled."` | `:298` | nothing installed |
| `"friendly: Trust Multiplier (%.1fx) ENGAGED."` | `:310` | `Friendly::Tick` edge: became active |
| `"friendly: Trust Multiplier DISENGAGED."` | `:312` | `Friendly::Tick` edge: became inactive |

The engage/disengage log is the **only** trust-path log after install: there is no per-event
log for a scaled write anywhere in `friendly.cpp`.

### Worker (`src/game/worker.cpp`)

| Format string | Line | When |
|---|---|---|
| `"worker: supplied level/ability patch disabled for unsupported PE revision %d."` | `:53` | revision gate |
| `"worker: level/ability patch signature expected one match, found %zu; feature disabled."` | `:60-61` | match count ≠ 1 |
| `"worker: exact patch target at %p could not be read; feature disabled."` | `:69-70` | unreadable target |
| `"worker: target at %p contains unexpected bytes; feature disabled."` | `:85-86` | unknown byte state |
| `"worker: level/ability patch target resolved @ %p (%s)."` with `"already enabled"` / `"original"` | `:92-93` | install succeeded |
| `"worker: saved max level/skills state could not be applied; reset to OFF."` | `:99` | saved ON could not be applied |
| `"worker: could not restore original patch bytes at shutdown @ %p."` | `:117-118` | shutdown restore failed |
| `"worker: shutdown found unexpected bytes at %p; left target untouched."` | `:122-123` | shutdown state unknown |
| `"worker: max level and all abilities patch enabled @ %p."` | `:169-170` | toggle on |
| `"worker: max level and all abilities patch disabled @ %p."` | `:190-191` | toggle off |

### Crime / Wanted (`src/game/inventory.cpp`)

| Format string | Line | When |
|---|---|---|
| `"world: WantedInfo table @ %p - No Bounty available."` | `:5704` | table resolved |
| `"world: WantedInfo table not found - bounty price left alone."` | `:5705` | table missing |
| `"world: No Bounty %s - price zeroed on %d/%u wanted row(s)."` with `"applied"` / `"reverted"` | `:5752-5753` | every `SetNoBounty` that reaches the write stage |
| `"world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active."` | `:2081-2082` | revision > 2850 |
| `"world: evaluate-wanted-state signature NOT FOUND - Witnessed/Assault crime bypass disabled."` | via `src/mem/hooks.h:35` | evaluator AOB missing |
| `"world: register-crime-event signature NOT FOUND - Crime event dispatch & UI banner bypass disabled."` | via `src/mem/hooks.h:35` | dispatcher AOB missing (≤ 2850) |
| `"NoBounty: %s"` inside the crash-report state block | `src/dllmain.cpp:267`, value `src/dllmain.cpp:275` | crash dump only |

### Money (`src/game/inventory.cpp`)

| Format string | Line | When |
|---|---|---|
| `"inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid)."` | `:2104` | `revision >= 2625` |
| `"inventory: SetDirectSilver -> %lld Silver (%lld Copper) [status=%d]."` | `:4700` | end of every `SetDirectSilver` |
| `"inventory: Cashed in %d Full Silver Pouches for +%lld Silver."` | `:4833` | `CashInAllSilverPouches` success |
| `"inventory: no Full Silver Pouches found to cash in."` | `:4825` | cash-in with nothing to liquidate |
| `"inventory: Silver_Pack not found in item table."` | `:4739` | pouch spawn with unresolved item |
| `"inventory: Added %lldx '%s' (TypeID %u, InstID 0x%llX) [server=%d client=%d]."` | `:4130-4132` | shared add path (pouch spawn / wallet seed) |
| `"inventory: add item %u x%lld refused - authoritative server holder unavailable (…)"` | `:4037-4044` | shared add path refused |
| `"inventory: add item %u x%lld FAILED (server=%d client=%d) bucketWant=%u clientH=%p serverH=%p"` | `:4146-4147` | shared add path failed |

UI-side toasts for money (user-visible, `src/gui/menu.cpp`): `"Wallet set to %d Silver!"`
`:2086`, `:2108`, `:2114`, `:2120`; `"Added +%d Silver to wallet!"` `:2099`; `"Silver added to
bag!"` `:2101`; `"ENGINE BLOCK: Wallet is empty or too low (0 Silver)."` `:2089`;
`"Please sell 1 junk item to any merchant to get at least 2 Silver first, then try again!"`
`:2090`; `"Spawned %d Full Silver Pouches!"` `:2131`; `"Failed to spawn pouches."` `:2133`;
`"Liquidated pouches for +%lld Silver!"` `:2141`; `"No Full Silver Pouches found in bag."`
`:2143`; `"Fake coins cleared! Now sell 1 item to the merchant, then try Set Money."` `:2060`;
`"Consolidated money into 1 stack (cleaned %d duplicate slots)."` `:2067`.

UI-side toast for the worker: `"Worker patch could not be applied"` `src/gui/menu.cpp:142`.

---

## H. Explicitly disabled / fail-closed features in these areas

| Feature | Status on this revision | Exact source comment / evidence | Reason given in source |
|---|---|---|---|
| Legacy crime-event dispatcher hook (crime UI banner / minimap wanted circle / guard hostility suppression) | **Disabled for revision > 2850** (not probed at all on PE 2944) | `// PE 2944's prior event dispatcher no longer exists.  No Bounty` `// remains supported through WantedInfo and wanted-state evaluation;` `// only crime-banner/minimap/guard-dispatch suppression is absent.` `src/game/inventory.cpp:2078-2080`; gate `src/core/version_mapping.cpp:67-70`; rationale `src/core/version_mapping.h:37-39` | "PE 2944 no longer contains that function contract, so probing its old signature would report an expected incompatibility as an error." `src/core/version_mapping.h:38-39` |
| Legacy money display hooks (`gameBase + 0x16077B0/0x16078C0/0x16081D0`) | **Disabled for `revision >= 2625`** | `// Legacy money display hooks: only valid on TU 1.18.02 (PE rev < 2625).` `// On TU 2.00+ these offsets point to arbitrary/invalid code - skip them.` `src/game/inventory.cpp:2094-2095` | "offsets point to arbitrary/invalid code" |
| Heap-scanner wallet writes (WeMod-derived `BackgroundCurrencyScan`) | **Removed / inert** | `// Heap Scanner disabled: Removed unsafe WeMod memory scanning that caused memory corruption.` `src/game/inventory.cpp:4542`; `BackgroundCurrencyScan` `src/game/inventory.cpp:4512-4548` has no caller | "caused memory corruption" |
| TribeInfo `_wantedCrimeType` zeroing (part of the No Bounty contract) | **Not implemented** — header comment only | `src/game/inventory.h:319-320` vs. implementation `src/game/inventory.cpp:5681-5755` (no TribeInfo access); `_wantedCrimeType` appears nowhere else in `src/` | not stated; the source simply does not perform it |
| `kOff_WantedDef_IsBlocked` (`WantedInfo+0x10` u8) | **Defined, never used** | `src/game/offsets.h:1179`; grep over `src/` finds no reader/writer | — |
| Trust Site A / Site B inline patches (`kSig_FriendlyTrustSiteA`, `kOff_FriendlyTrustSiteA_Hook`, `kSig_FriendlyTrustSiteB`, `kOff_FriendlyTrustSiteB_Hook`, `kOrig_FriendlyTrustBytes`) | **Defined, never used** — superseded by the four setter/lookup hooks | definitions `src/game/offsets.h:1776-1792`; grep over `src/` finds no consumer of any of these five symbols | the surrounding comment still documents the older 15-byte replacement plan (`src/game/offsets.h:1786-1789`), but no code installs it |
| `kSig_FriendlyNpcTrustWriter` (NpcTrustWriter `0x141BDF910`) | **Defined, never used** | `src/game/offsets.h:1819-1821`; no consumer | — |
| `kSig_FriendlyAlertDisp` (FactionRelationAlertDispatcher `0x142759760`) | **Defined, never used** | `src/game/offsets.h:1823-1825`; no consumer | — |
| Worker legacy UI and keys (`RenderWorkers`, `RenderMountOptions`, `mount_options`, `world_workers`, `workerMaxLevelHook`, `workerAutoApply`, `workerEditLevel|Exp`, `workerTargetSkillCount`, `workerSkillId<n>`) | **Explicitly forbidden by contract test** | `tests/verify_worker_feature_contract.ps1:23-36` | legacy worker/mount menu route must not remain |
| Dye Mount/Horse editor selectors (adjacent pet/mount area, removed) | **Explicitly forbidden by contract test** | `Forbid $dyeHeader 'SetTargetMode|GetTargetMode|SetActiveMount|GetActiveMount' …` and `Forbid $dye 'HorseSlotType|GetHorseSlotType|MountSlotName|FindMountComp|g_mountComp|SavedMountSlot|s_savedMountSlots|s_targetMode == 1|MOUNT MODE|mount equip|mount tag|tracked mounts' …` `tests/verify_worker_feature_contract.ps1:38-39` | removed Mount/Horse editor paths and mount auto-restore |
| Worker patch on PE 2760 / unknown revisions | **Not enabled** (fail-closed) | `src/game/worker_logic.h:18-21`; tests `tests/readiness_tests.cpp:475-478` ("unrelated PE revisions must not inherit the worker patch", "unknown PE revisions must fail closed") | only 2850 and 2944 were verified |
| `AddCampCurrency` / camp-currency spoof UI | **No menu call site** (dead code); the spoof consumer remains | declaration `src/game/inventory.h:209`, definition `src/game/inventory.cpp:4838-4856`, consumer `src/game/inventory.cpp:1517-1539` | — |

---

## I. Symbols referenced but not defined, and definitions with no consumer

**Referenced but not resolvable from this source tree** (the source names them only in
comments; no declaration exists in `src/`): `eWantedState_None` (comment only,
`src/game/inventory.cpp:2037`), `sub_141595BC0` (`src/game/offsets.h:1186`), IDB names
`sub_DBE1000`, `sub_1AD4710`, `sub_613220`, `sub_14A1330`, `sub_1CDD520`, `sub_1CE8190`,
`sub_516050`, `sub_517460`, `sub_1171F30`, `sub_8FBD80`, `sub_34B35D0`
(`src/game/offsets.h:1757-1758`, `:1755`, `:580`, `:583`, `:597`, `:1046-1047`, `:1252`,
`:1259`), `eWantedState_None`-adjacent enum names `Witness/Suspect/Assault/Pursuit`
(comment only, `src/game/inventory.cpp:2037`). Neither the target address of
`CrimsonDesert.exe+20967CC` / `+214BE8C` nor the RVA `0x6350EE8` is verified anywhere in the
source; they are documentation and a fallback constant respectively.

**Defined in `offsets.h` with no consumer in `src/`** (grep over the whole tree):
`kSig_FriendlyTrustSiteA`, `kOff_FriendlyTrustSiteA_Hook`, `kSig_FriendlyTrustSiteB`,
`kOff_FriendlyTrustSiteB_Hook`, `kOrig_FriendlyTrustBytes`, `kSig_FriendlyNpcTrustWriter`,
`kSig_FriendlyAlertDisp`, `kOff_WantedDef_IsBlocked`.

**Defined in code with no call site**: `Inventory::AddCampCurrency`
(`src/game/inventory.cpp:4838`), `BackgroundCurrencyScan`
(`src/game/inventory.cpp:4513`), `Friendly::Ready()` is called only from the menu
(`src/gui/menu.cpp:157`), `Worker::Enabled()` (`src/game/worker.cpp:137-140`) has no caller —
the menu only uses `Ready()` and `SetEnabled()`.

---

## J. Touch-point count

§B contains **31 rows**: 9 trust/friendship (T1–T9), 2 worker (W1–W2, the same address read
and written), 8 crime/wanted (C1–C8), 12 money (M1–M12, of which M10–M12 are shared
inventory machinery reached from the money paths).

By role: **4 trust detours** (T1/T3 share `hkSetNpc`, T2/T4 share `hkSetPet`, T5, T6 — the
T3/T4 rows are the same two detours reached through the TU 2.00 fallback locators),
**2 crime detours** (C1, C2 — C2 only on `revision <= 2850`), **3 legacy money detours**
(M1–M3, created by raw `MH_CreateHook` and never enabled, see §B.4), **1 item-count detour**
(M4/M5 are one detour with two locator paths), **supporting inventory detours** (M11 holder
observer; M12 commit/holder-insert/expand-slots), **1 code byte patch** (W1), and the
remainder are guarded memory reads/writes (T7–T9, W2, C3–C5, M6–M9), a table-global
resolution routine (C6), a presence-only readiness probe (C7), and observer/upkeep drivers
(C8).

Worker patch summary: original `0F 85 95 00 00 00` (`JNZ +0x95`), patched
`E9 96 00 00 00 90` (`JMP +0x96 / NOP`), 6 bytes, at the single match of
`0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5`, supported only on PE revisions 2850 and 2944.

Crime availability summary: wanted-state evaluator (returns `7`) and the WantedInfo
`_increasePrice` zero/restore path are active on every supported revision; the legacy
four-argument crime-event dispatcher (crime UI banner / minimap circle / guard hostility) is
**unavailable and deliberately not probed on PE 2944**, with the source's own explanation
quoted in §E.2; TribeInfo `_wantedCrimeType` zeroing promised by `src/game/inventory.h:319-320`
is not implemented.
