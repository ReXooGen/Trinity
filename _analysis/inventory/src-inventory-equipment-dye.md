# Trinity — Source-Side Consumers: Inventory / Equipment / Dye

Scope: SOURCE ONLY. Every claim below is anchored to an exact `file:line` in the Trinity
repository at `C:\Users\mul0\Documents\GitHub\Trinity`. No source file was modified.

Files read for this report:

| File | How it was read |
|---|---|
| `src/game/inventory.cpp` (276 KB / 5761 lines) | full read of all game-touching regions (1–330, 331–630, 1160–2350, 2610–2730, 2835–3230, 3305–3430, 3360–3760, 3800–4300, 4300–4462, 4462–4760, 4838–5200, 5485–5761) + exhaustive greps |
| `src/game/inventory.h` | full |
| `src/game/inventory_logic.h` / `.cpp` | full |
| `src/game/inventory_hook_contract.h` | full |
| `src/game/item_names.h` / `item_names.cpp` | header full; cpp head/tail + greps (439 KB generated table) |
| `src/game/equipment.cpp` (97 KB / 2183 lines) | full read of 1–400, 400–1040, 1040–1420, 1556–2183 + greps |
| `src/game/equipment.h` / `equipment_logic.h` / `.cpp` | full |
| `src/game/dye.cpp` (92 KB / 2037 lines) | full read of 1–280, 470–720, 820–940, 1025–1240, 1240–1650, 1770–2037 + greps |
| `src/game/dye.h` | full |
| `src/game/dye_slots_table.h` | head/tail (generated) |
| `src/game/dye_data.h` | head + greps only (generated) |
| `src/game/offsets.h` (122 KB) | targeted reads: 585–764 (inventory), 1370–1500 (dye records), 1500–1800 (equip/sockets/refine) + greps |
| `src/mem/hooks.h` | full |
| `src/core/version_detect.cpp`, `src/core/version_mapping.cpp` | targeted reads |
| `src/gui/menu.cpp`, `src/game/teleport.cpp`, `src/game/player.h`, `src/mem/scanner.h` | targeted greps/reads (to name UI entry points and the per-frame driver) |

Terminology used below:

* **hook** = MinHook detour installed via `mem::InstallHook` (`src/mem/hooks.h:26-65`) or a raw `MH_CreateHook`.
* **native call** = a resolved game address that Trinity *calls* (never detours).
* **byte patch** = Trinity overwriting executable *code* bytes in the game image. **There are none in these three areas** — see §D.
* **memory read/write** = guarded `mem::ReadXX` / `mem::WriteXX` (`src/mem/safe_memory.h`) on game heap objects.

---

# 0. Shared plumbing (all three areas depend on it)

These are not "features" but every row in §B/C depends on them.

| Thing | Where | Notes |
|---|---|---|
| `mem::InstallHook(context, sig, consequence, detour, original, target, maxMatches)` | `src/mem/hooks.h:26-65` | `FindPattern` → `CountMatches` (warn if `!= 1`) → `MH_CreateHook` → `MH_EnableHook`. On success `*original` = trampoline, `*target` = hooked address. |
| `mem::RemoveHook(void** target)` | `src/mem/hooks.h:70-76` | `MH_DisableHook` + `MH_RemoveHook`, then nulls the target. Idempotent on null. |
| `mem::FindPattern(sig)` | `src/mem/scanner.h:32-33` | Whole-module AOB scan of the game module. |
| `mem::FindPatternIf(sig, visit, ctx)` | `src/mem/scanner.h:50-55` | Same but with a visitor predicate (used for string-anchored table hunts). |
| `mem::CountMatches(sig, max)` | `src/mem/scanner.h:41-42` | Ambiguity guard. |
| `mem::ResolveRipAt(instr, instrLen)` | `src/mem/scanner.h:69` | RIP-relative operand resolution. |
| `kMinPointer = 0x10000000` | `src/game/offsets.h:27` | Every guarded pointer read is rejected below this floor. **Exception:** TEB/TLS reads use floorless `__try` readers (`inventory.cpp:1254-1271`, `dye.cpp:713-718`, `equipment.cpp:463-468`). |
| Per-frame game-thread driver | `src/game/teleport.cpp:1641-1657` | `Player::Tick(); World::Tick(); Inventory::Tick(); Dye::Tick(); Equipment::Tick();` |
| `Player::GetTrackedPlayerCount/GetOwner/GetActor` | declared `src/game/player.h:55-57` | Used by `ServerHolder()` (`inventory.cpp:1412-1437`), `CharacterAddrs` (`inventory.cpp:3685-3690`), equip/dye component walks. |

---

# 1. INVENTORY / ITEMS / MONEY

## 1.A Feature list (user-visible) with exact C++ entry points

Tab labels: `src/gui/menu.cpp:35` — `{ "PLAYER", "INVENTORY", "TRAVEL", "WORLD", "SYSTEM" }`.
Inventory home submenus: `src/gui/menu.cpp:1671-1675`.

| Menu path (label at `menu.cpp:line`) | Trinity entry point | Source |
|---|---|---|
| INVENTORY ▸ **Money & Currency** (`2075`) | | |
| · Direct Silver Amount + `>> Set Wallet to Exact Amount <<` (`2079`, `2082`) | `Inventory::SetDirectSilver(int64_t)` | `inventory.cpp:4551` |
| · `>> Add Amount to Existing Wallet <<` (`2095`) | `Inventory::AddDirectSilver(int64_t)` | `inventory.cpp:4705` |
| · presets 1M / 10M / 20M Silver (`2105`,`2111`,`2117`) | `Inventory::SetDirectSilver(1000000/10000000/20000000)` | `menu.cpp:2107,2113,2119` |
| · `>> Spawn Full Silver Pouches <<` (`2127`) | `Inventory::SpawnSilverPouches(int64_t)` → `AddItem(typeId("Silver_Pack"))` | `inventory.cpp:4733-4743` |
| · `>> Cash In All Pouches (Instant Liquidate) <<` (`2136`) | `Inventory::CashInAllSilverPouches(int64_t*)` | `inventory.cpp:4746` |
| · Optional ▸ `>> Clear Bugged/Fake Wallet Coins <<` (`2056`) | `Inventory::SetDirectSilver(0)` | `menu.cpp:2059` |
| · Optional ▸ `Consolidate All Money Stacks` (`2063`) | `Inventory::ConsolidateMoney()` | `inventory.cpp:4858` |
| · Camp currency (**no menu call site found** — declared and defined but never called from `menu.cpp`; see uncertainty #6) | `Inventory::AddCampCurrency(int64_t)` | declared `inventory.h:209`; defined `inventory.cpp:4838` |
| INVENTORY ▸ **Abyss Items & Artifacts** (`2158`) | | |
| · `Sealed Artifacts Collected: n / 150` status row (`2167`) | `Inventory::GetSealedArtifactStatus()` | `inventory.cpp:5493` |
| · `>> Add Missing Sealed Artifacts to Target <<` (`2181`) | `Inventory::AddMissingSealedArtifacts(int targetTotal=150)` → `AddItemsBulk` | `inventory.cpp:5524` |
| · `>> Add All 150 Sealed Artifacts <<` (`2193`) | `Inventory::AddMissingSealedArtifacts(150)` | `menu.cpp:2193` |
| · clean duplicates (`2203`) | `Inventory::CleanDuplicateSealedArtifacts()` → `RemoveItem` | `inventory.cpp:5555` |
| · `Abyss Artifacts (Count)` + add (`2211`,`2217`) | `Inventory::AddItemByKey("Abyss_Artifact", n)` | `inventory.cpp:4500` |
| · hardcoded quick-spawn buttons (`2226`-`2274`) | `AddItemByKey` for `Boss_Reward_SuperSkill`, `SkillPoint_Book_02`, `AbyssStone_Seed`, `AbyssRuinsCreatureCore`, `Abyss_InfiniteStat_Hp30/Mp2/Sp3` | `menu.cpp:2226-2274` |
| INVENTORY ▸ **Add Item** (`1673`) | | |
| · single row add (`1912`) | `Inventory::AddItem(uint16_t typeId, int64_t qty)` (queues) + `AddStatus()` | `inventory.cpp:5089`, `5103` |
| · `Add All` for a whole category (`1989`, `2005`) | `Inventory::AddItemsBulk(const uint16_t*, int count, int64_t qtyEach)` + `BulkAddStatus()` | `inventory.cpp:5108`, `5140` |
| · catalog browse (`1943`) | `CatalogCategoryCount/Name/Tab/Icon`, `CatalogItemCount`, `GetCatalogItem` | `inventory.cpp:4417-4460` |
| INVENTORY ▸ **Item Editor** (`1675`) | | |
| · storage/category/item browsing (`1747`-`1792`) | `StorageCount/Name/Key/Slots`, `CategoryCount/Name/Tab/Icon`, `ItemCount`, `GetItem`, `GetItemInfo` | `inventory.cpp:2536-2621` |
| · per-row quantity edit (`1646`) | `Inventory::SetQuantity(int st,int cat,int idx,int64_t)` | `inventory.cpp:3333` |
| · per-row delete (`1650`) | `Inventory::RemoveItem(int st,int cat,int idx)` | `inventory.cpp:5150` |
| · `Set All` in category (`1815`, `1823`) | `SetQuantity` in a loop | `menu.cpp:1823` |
| · manual Refresh | `Inventory::Refresh()` / `ForceRefresh()` | `inventory.cpp:2533-2534` |
| · Edit-Persist warning gate | `Inventory::EditsPersist()` | `inventory.cpp:3353` |
| INVENTORY — top-level toggles (`1678`, `1682`, `1685`) | | |
| · `Slot Size` toggle + `Set Max Slot Value` | `Inventory::SetAllSlotSizes(bool,int)` | `inventory.cpp:2980` |
| · `Max Stack Size` toggle + `Set Max Stack Value` | `Inventory::SetAllMaxStackSizes(bool,int64_t)` | `inventory.cpp:2623` |
| INVENTORY ▸ **Restore Items** (`1674`) + 8 category submenus (`2538`-`2552`) | `Inventory::RestoreCategoryMissing(const char* const*, int)` | `inventory.cpp:5465` |
| · Quest & Special Item Catalog Archive (`2658`) | `AddItemByKey(key, 1)` loop | `menu.cpp:2478` |
| · Lost & Sold tracker / buyback (`2581`, `2645`) | `GetLostItemsCount`, `GetLostItem`, `RestoreLostItem`, `RestoreAllLostItems`, `ClearLostItems`, `LoadLostItems`, `SaveLostItems` | `inventory.cpp:5332-5423` |
| PLAYER ▸ `No Bounty` (`menu.cpp:103`) | `Inventory::SetNoBounty(bool)` | `inventory.cpp:5681` |
| (implicit, always-on) used-slot repair | `RepairUsedSlots(uintptr_t)` from `Inventory::Tick` | `inventory.cpp:1807`, driven at `3150-3158` |
| (implicit, always-on) sold/discarded tracker | `TrackInventoryChanges()` from `Inventory::Tick` | `inventory.cpp:3028`, driven at `3136` |
| (implicit, always-on) money max-stack pin (TU ≤ 1.18 only) | `Inventory::Tick` | `inventory.cpp:3160-3170` |

## 1.B Game-side function contracts (inventory)

> **Role legend:** H = hook, N = native call, R = memory read, W = memory write, O = observer (read-only, no call).

| # | Trinity role | Game function / address expression | How the address is obtained | Exact ABI Trinity assumes | Object operated on | Offsets read/written | Source file:line | Fail-safe if missing |
|---|---|---|---|---|---|---|---|---|
| 1 | H | Evaluated-wanted-state function | `mem::FindPattern(kSig_EvaluateCrimeWantedState)` | `uint8_t __fastcall(void* wantedMgr, void* actorCtx)` | wanted manager | none | `inventory.cpp:2066-2068`; detour `2032-2041` | Returns `oEvaluateCrimeWantedState(...)` unchanged; No Bounty's wanted-state half lost. `LOG_ERR` from `hooks.h:35` |
| 2 | H | Crime-event dispatcher | `mem::FindPattern(kSig_RegisterCrimeEvent)`, only if `core::MayProbeLegacyCrimeEventDispatcherForRevision(revision)` | `void __fastcall(void* dispatcher, const char* eventName, void* eventData, void* eventContext)` | crime dispatcher | none | `inventory.cpp:2070-2074`; detour `2046-2059` | Logs `"world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active."` (`2081-2082`) |
| 3 | H | `GetItemQuantity` (1.18.0.2 `sub_1582880`) | `mem::InstallHook(kSig_InvGetItemQty, maxMatches=4)`; fallback `kSig_InvGetItemQty_Legacy` with consequence `"inventory disabled"` | `int64_t __fastcall(void* container, uint16_t typeId, void* keyPtr)` | inventory container | reads `container+8`? no — calls `oGetHolder(container)`; reads nothing itself | `inventory.cpp:2085-2091`; detour `1476-1542`; typedef `64` | If **both** patterns miss, `Inventory::Install()` returns **false** → whole inventory subsystem disabled. `LOG_ERR("inventory: item-count accessor signature NOT FOUND - inventory disabled.")` via consequence string |
| 4 | H | Legacy money getter #1 @ `gameBase + 0x16077B0` | hardcoded RVA, `gameBase = GetModuleHandleA(nullptr)`; **only if `revision < 2625`** | `int64_t __fastcall(void* rcx)` | wallet object | none | gated `inventory.cpp:2096-2101`; detour `60`; typedef `55` | On TU 2.00+: `LOG("inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid).")` (`2104`). Return value is `MH_CreateHook`'s status — **not checked** |
| 5 | H | Legacy money getter #2 @ `gameBase + 0x16078C0` | same | `int64_t __fastcall(void* rcx)` | wallet object | none | `inventory.cpp:2099`; detour `61` | same as #4 |
| 6 | H | Legacy money getter #3 @ `gameBase + 0x16081D0` | same | `int64_t __fastcall(void* rcx)` | wallet object | none | `inventory.cpp:2100`; detour `62` | same as #4 |
| 7 | N | `GetInventoryHolder` (`kSig_InvGetHolder`) — **resolved, then also hooked** | `mem::FindPattern(kSig_InvGetHolder)` → `oGetHolder` | `void* __fastcall(void* container)` | container → holder | none (returns holder) | resolve `inventory.cpp:2107-2113`; call sites `1422`, `1487`, `1578` | If pattern missing: `LOG_ERR("inventory: holder resolver signature NOT FOUND - inventory disabled.")`; **`Install()` returns false** (`2108-2112`) |
| 8 | H | same function, passive observer | `mem::InstallHook("inventory: holder observer", kSig_InvGetHolder, "server holder will be learned only from later transactions", maxMatches=2)` | `void* __fastcall(void* container)` | container → holder | reads `holder+0x18` (buckets), `holder+0x20` (count) for `HolderBucketCount` | `inventory.cpp:2194-2204`; detour `1613-1651` | `LOG_WARN("inventory: passive holder observer unavailable - waiting for an inventory transaction.")` (`2203`); `oGetHolder` restored to the pre-hook resolve (`2202`) |
| 9 | H | Slot-expansion setter `sub_1CE8190` (`kSig_InvSetExpandSlots`) | `mem::InstallHook(..., maxMatches=4)` **only on pre-TU-2.01** | `void* __fastcall(void* holder, int* outErr, void* unused, uint16_t bucketType, uint16_t count)` | holder → bucket | W `bucket+0x1A` (`kOff_InvBucket_ExpandSlots`), W `bucket+0x14` (`kOff_InvBucket_MaxSlots`), R `bucket+0x10`, R `bucket+0x14` | install `inventory.cpp:2222-2224`; detour `1737-1783` | Fallback: resolve call-only via `mem::FindPattern(kSig_InvSetExpandSlots)` → `LOG_WARN("inventory: slot-expansion setter hook failed - Slot Size applies call-only and may briefly revert when the game recomputes it.")` (`2226-2232`). TU 2.01+: hook not installed at all; `LOG_OK("inventory: modern continuous slot-expansion guard active.")` (`2220`) |
| 10 | N | Slot-expansion setter (call-only path) | same pattern, `oSetExpandSlots` | same | holder → bucket | as #9 | call `inventory.cpp:2925` | `ApplySlotCapToHolder` returns false early if `!oSetExpandSlots && !UsesTu201CompatibleRevision` (`2847-2848`) |
| 11 | H | Transaction commit, TU 2.01 ABI (`kSig_InvCommit`) | `mem::InstallHook(..., maxMatches=2)`, only if `UsesTu201CompatibleRevision` | `void*(__fastcall*)(void* holder, void* outError, void* placements, uint16_t mode, void* outEvents, uint8_t notify, uint8_t reconcile, uint8_t replicate)` (`inventory_hook_contract.h:9-12`) | holder, container at `[holder+8]` | R `holder+8` | install `inventory.cpp:2244-2248`; detour `1665-1682` | `LOG_ERR` + `"quantity edits will not persist (revert on reconcile)"` |
| 12 | H | Transaction commit, pre-2.01 ABI (`kSig_InvCommit_Pre201`) | `mem::InstallHook(..., maxMatches=4)` | `void* __fastcall(void* holder, void* err, void* container, void* items, void* out, uint8_t a6, uint8_t a7)` (`inventory.cpp:71`) | holder + container (r8) | reads nothing | install `inventory.cpp:2252-2254`; detour `1654-1663` | same consequence string |
| 13 | H | Per-holder insert planner `sub_1F850C0` (pre-2.01) | `mem::InstallHook(kSig_InvHolderInsert, ..., maxMatches=2)`; fallback `kSig_InvHolderInsert_Legacy` | `void* __fastcall(void* bucket, void* err, void* container, void* itemArr, uint16_t a5, void* a6, uint8_t a7, uint8_t a8, uint8_t a9)` (typedef `68-69`) | bucket + container (r8) | reads nothing | `inventory.cpp:2273-2278`; detour `1685-1694` | Legacy fallback consequence: `"server holder relies on the commit hook alone"` |
| 14 | H | Insert planner, TU 2.01 (`kSig_InvHolderInsert201`), PE 2944 uses `kSig_InvHolderInsert2944` | `mem::InstallHook(holderInsertSig, ..., maxMatches=2)`; sig chosen by `revision == 2944` | same 9-arg ABI | bucket + container | reads nothing | `inventory.cpp:2259-2271` | `"Add Item will be refused and server holder capture is limited"` |
| 15 | N | Insert **planner** = trampoline of the same planner | `oHolderInsert` (set by #13/#14) | `void*(__fastcall*)(void* bucket, void* err, void* container, void* itemArr, uint16_t, void* out, uint8_t, uint8_t, uint8_t)` | bucket, container, in-vector `{ptr,count@8,cap@12}`, out-vector | writes nothing itself; fills `out` | call `inventory.cpp:3892-3893` | `AddInRealm` guarded by `if (oItemValueCtor)` / commit count; `addItemReady` (`2281-2295`) |
| 16 | N | `TrItemValue` ctor (`kSig_TrItemValueCtor`, TU ≥ 2.00) | `mem::FindPattern` + `mem::CountMatches(sig, 2) == 1` required | `void*(__fastcall*)(void* itemVal, uint16_t* typeId, int64_t qty)` (typedef `81`) | caller-owned `uint8_t itemVal[kItemVal_Size]` (`kItemVal_Size = 0x108`, `offsets.h:981`) | W `itemVal+0x0A` (`kOff_ItemVal_Subtype`), W `itemVal+0x00` (`kOff_ItemVal_InstanceId`) | `inventory.cpp:2129-2136`; call `3871-3874` | If unresolved, tries 5 legacy fuzzy sigs (`2121-2127`, `2137-2151`); then `LOG_WARN("inventory: native TrItemValue constructor unavailable - Add Item will be refused.")` (`2154`) |
| 17 | N | `TrItemValue` ctor, legacy fuzzy patterns | 5 literal AOBs, `mem::FindPattern` + `CountMatches(sig,4)` accepting 1 or 2 matches | same | same | same | `inventory.cpp:2121-2151` | `allowLegacyFuzzy = core::MayUseLegacyFuzzySignaturesForRevision(revision)` = `!UsesTu201CompatibleRevision && revision < 2760` (`version_mapping.cpp:62-65`) |
| 18 | N | Commit-placement (TU 2.01, 4-arg) `kSig_InvCommitPlacement201` | `mem::FindPattern(usesTu201CompatibleAbi ? kSig_InvCommitPlacement201 : kSig_InvCommitPlacement)` | `void*(__fastcall*)(void* holder, int* err, void* placement, uint16_t slotIdx)` (typedef `84-85`) | holder; placement record | R `placement` at stride `core::GetPlacementStride()` (0xE0 / 0xD8), slot index at `+core::GetPlacementSlotIdxOffset()` (0xD8 / 0xD0) | resolve `2157-2169`; read+call `3901-3911` | `addItemReady` requires `oCommitPlacement || oCommitPlacement201` (`2281-2282`); else `LOG_WARN("inventory: add-item path incomplete ... Add Item will be refused.")` (`2291-2295`) |
| 19 | N | Commit-placement (legacy, 5-arg) `kSig_InvCommitPlacement` | same | `void*(__fastcall*)(void* holder, int* err, void* unused, void* placement, uint16_t slotIdx)` (typedef `82-83`); `unused` passed as `nullptr` | same | same | call `3913-3914` | same as #18 |
| 20 | N | Free-placements `kSig_InvFreePlacements201` / `kSig_InvFreePlacements` | `mem::FindPattern` by ABI | `void(__fastcall*)(void* vec)` (typedef `86`) | `out` placement vector | none | resolve `2159-2170`; call `3931` | `addItemReady` requires it (`2282`) |
| 21 | N | `TrItemValue` dtor `kSig_TrItemValueDtor` | `mem::FindPattern` | `void(__fastcall*)(void* itemVal)` (typedef `87`) | stack `itemVal` | none | resolve `2161,2171`; call `3932` | **`kSig_TrItemValueDtor` is defined as the EMPTY STRING `""` (`offsets.h:942`)**, so this locator can never resolve; `oItemValueDtor` stays null and the destructor call is skipped. This is a *referenced but not defined* locator — noted as an uncertainty, not invented behaviour. |
| 22 | N | TLS realm flag consumer | `GetProcAddress(ntdll, "NtQueryInformationThread")` → `oNtQueryInfoThread` | `LONG NTAPI(HANDLE, ULONG, PVOID, ULONG, PULONG)` (`inventory.cpp:1251`) | current thread TEB | R `teb+0x58` (`kOff_Teb_TlsPointer`) → TLS array → `tls + RealmFlagOffsetForRevision(rev)`; R/W that byte | resolve `2176-2178`; use `1281-1303` | `RealmFlagAddr` returns 0 when the chain breaks or the byte is not a bool (`v > 1` → fail closed, `1301`). `addItemReady` requires `oNtQueryInfoThread` (`2282`) |
| 23 | R | Core global singleton `qword_6180C28` | `mem::FindPattern(kSig_InvCoreGlobal | _Pre201)` then `mem::ResolveRipAt(anchor + core::InventoryCoreGlobalMovOffsetForRevision(revision), 7)` (offset is `0` on TU2.01+, `0x15` otherwise — `version_mapping.cpp:72-75`) | n/a (address only) | `g_coreGlobal` | n/a | `inventory.cpp:2300-2306` | Optional: `g_coreGlobal = 0` → `ResolveHolderByWalk()` returns 0 (`1194`), holder resolution falls back to the hook-published `g_holder` (`1464-1465`) |
| 24 | R | `iteminfo` table singleton | `FindTableGlobal(kStr_ItemInfoTable)` (`inventory.cpp:5757-5760` → `1920-1965`): `mem::FindPatternIf(kSig_LeaR8Rip, &IsTableRef, &hunt)` where `IsTableRef` resolves RIP, compares the target C-string to `"iteminfo"`, then `FindItemPrologueAbove` + scans 0x60 bytes for `48 8B (mod 0xC7 == 0x05)` → `ResolveRipAt` | n/a | `g_itemTableGlobal` | used by `DefForRow` | `inventory.cpp:2309`, `1920-1965`, `260-279` | Optional. Retried lazily every 1000 ms via `ShouldAttemptCatalogResolve` (`4294`, `4301-4306`, `inventory_logic.cpp:36-44`); `LOG_WARN("inventory: catalog wait - iteminfo resolver global is missing.")` (`4312`) |
| 25 | R | `ItemGroupInfo` category tree | `FindTableGlobal(kStr_ItemGroupInfoTable)` | n/a | `g_grpTableGlobal` | R `grp+0x68` (`_orderIndex`), R `grp+0x08` (key str obj), R `grp+0x18` (loc struct), R `grp+0x6C` (icon row) | `inventory.cpp:2312`, `359-364`, `474-508`, `934` | Optional → single "Uncategorised" group |
| 26 | R | `categorygroupinfo` / `categoryinfo` (fallback anchors) | literal names via `FindTableGlobal`; on `revision >= 2625` also hardcoded `gameBase+0x634DDB8` / `gameBase+0x634DDD0` | n/a | `g_grpTableGlobal` | as #25 | `inventory.cpp:1977-1998` | Optional |
| 27 | R | `stringinfo` icon table | `FindTableGlobal(kStr_StringInfoTable)` | n/a | `g_strTableGlobal` | R `def+0x18` (`kOff_StrDef_Buffer`) | `inventory.cpp:2315`, `898` | Optional → no icons |
| 28 | R | `Inventory` (storage) table | `FindTableGlobal(kStr_InventoryInfoTable, /*indirect=*/true)` → `mem::FindPatternIf(kSig_MovR8Rip, ...)` | n/a | `g_invTableGlobal` | R `def+0x08` key, `+0x70` loc name, `+0x48` `_defaultSlotCount`, `+0x4A` `_maxSlotCount` (W in `SetAllTableMaxSlots`) | `inventory.cpp:2318`, `1081-1100`, `2957`, `2973` | Optional → storages labelled by engine key |
| 29 | R | Localisation manager | `mem::FindPattern(kSig_LocStringGet)` → `Alt1` → `Alt2` → `Legacy`; then scan 0x30 bytes for `48 8B (mod 0xC7 == 0x05)` → `ResolveRipAt` | n/a | `g_locMgrGlobal` | R `structAddr` → provider, R `provider+0x18`, R `mgr+0x58` as `char*` pool base with `mgr+0x60` size (TU ≥ 2.00); legacy `mgr+0x58` → blob (`+0x00` data, `+0x08` size) | install `2321-2323`; lazy `2003-2025`; consumer `308-342` | Optional → falls back to prettified engine keys |
| 30 | R | Hardcoded TU 2.00.01 table globals | `gameBase + 0x6350EE8` (`WantedInfo`), `gameBase + 0x63307A8` (`tribeinfo`), only when `revision >= 2625` | n/a | `s_wantedGlobal` | R `def+0x18` (`kOff_WantedDef_IncreasePrice`), W same | `inventory.cpp:1946-1962` | Only reached when the string-anchor hunt already failed |
| 31 | W | `WantedInfo` rows (No Bounty) | `FindTableGlobal(kStr_WantedInfoTable)` | n/a | table rows | W `def+0x18` i64 `_increasePrice` → 0 (restore on disable) | `inventory.cpp:5697-5754` | `LOG("world: WantedInfo table not found - bounty price left alone.")` (`5705`) |
| 32 | O | Holder bucket table (structural validation) | walk from `g_coreGlobal` | n/a | holder | R `holder+0x18` (`ptr[]`), `holder+0x20` (u32 count) | `inventory.cpp:1166-1174` | `HolderLooksValid` false → callers bail |
| 33 | W | Bucket occupancy counter | n/a | n/a | bucket | W `bucket+0x12` (`kOff_InvBucket_UsedSlots`) ← recount of occupied slots | `inventory.cpp:1846-1847` | Skipped when `g_commitActive` is set (`1814`) |
| 34 | W | Bucket slot cap / expansion / delta | n/a | n/a | bucket | W `+0x1A` expand, `+0x16` `DeltaRaw`, `+0x18` `DeltaClamped`, `+0x14` `MaxSlots` | `inventory.cpp:2929-2932`, `1755-1756`, `1774-1775` | Per-bucket; guarded reads throughout |
| 35 | W | Inventory item slot | n/a | n/a | slot (`SlotStride()` = `core::GetSlotStride()`: `0xC0` TU1.13-1.15, `0xC8` TU1.16+) | W `slot+0x08` typeId, `slot+0x10` i64 quantity | `inventory.cpp:3341`, `4606`, `4770-4771`, `4951-4952`, `5019-5026`, `5073-5076`, `5165`, `5176` | `WriteXX` returns false on a faulting page |
| 36 | W | `ItemInfo` definition rows (Max Stack) | via `g_itemTableGlobal` | n/a | def row | W `def+0x18` i64 `_maxStackCount`, W `def+0x111` u8 `_applyMaxStackCap` | `inventory.cpp:2668-2686`, `3167-3168`, `4576-4577`, `4867` | `SetAllMaxStackSizes` returns false if `!g_itemTableGlobal` (`2625`) |
| 37 | W | `InventoryInfo` definition rows (slot caps) | via `g_invTableGlobal` | n/a | def row | W `def+0x4A` u16 `_maxSlotCount` | `inventory.cpp:2973` | `SetAllTableMaxSlots` returns false if `!g_invTableGlobal` (`2940`) |
| 38 | W | ID allocator counter | `IdAllocator(container)`: `container+0x68` → `+0x10` (`kOff_Sub_IdAllocator`) | n/a | allocator | W `alloc+0x20` (`kOff_IdAlloc_Counter`) via `_InterlockedIncrement64` | `inventory.cpp:3792-3802`, `4084-4085` | Falls back to a Trinity-local sequence starting at `≥ 1000000` (`4087-4095`) |
| 39 | W | TLS realm flag | `RealmFlagAddr()` | n/a | TLS byte | W 1 / restore previous | `inventory.cpp:3968`, `3978`; `dye.cpp:871`, `1295`, `1456`, `1968`, `2034`; `equipment.cpp:645`, `667`, `711`, `733`, `775`, `790`, `829`, `844`, `861`, `869`, `1861`, `1869`, `1956`, `1964` | `RawWrite8` guarded; caller bails if the flag address is 0 |

## 1.C Pointer chains / object resolution (inventory)

All chains start from a module-level global or a hook argument.

| # | Chain (base → offset list) | Yields | Source file:line |
|---|---|---|---|
| C1 | `[g_coreGlobal]` → `+0x30` (`kOff_Global_Mid`) → `+0x50` (`kOff_Mid_Container`) = **client inventory container** → `+0x68` (`kOff_Container_Sub`) → `+0xB8` (`kOff_Sub_Holder`) = **client holder** | client holder | `inventory.cpp:1192-1200` (`ResolveHolderByWalk`), `1209-1224` (`ResolveClientContainer`), `1180-1187` (`HolderForContainer`) |
| C2 | `holder` → `+0x18` (`kOff_InvHolder_Buckets`, `ptr[]`) → `+0x20` (`kOff_InvHolder_Count`, u32) → bucket `i` = `[buckets + i*8]` | bucket list | `inventory.cpp:1171-1173`, `1724-1725`, `3036-3037` |
| C3 | `bucket` → `+0x00` (`kOff_InvBucket_Slots`, `ptr[]`) → `+0x08` (`kOff_InvBucket_Count`, u16 size) → slot `j` = `slots + j*SlotStride()` | item slot | `inventory.cpp:1832-1833`, `3324-3326`, `3998-4005` |
| C4 | `bucket` → `+0x10` (`kOff_InvBucket_Type`, u16 `InventoryType`) | which storage this bucket is | `inventory.cpp:1732`, `1827`, `2379`, `3764` |
| C5 | `slot` → `+0x08` typeId (`kOff_InvSlot_TypeId`), `+0x10` i64 quantity (`kOff_InvSlot_Quantity`), `+0x00` i64 instanceId (`kOff_ItemVal_InstanceId`), `+0x0A` u16 subtype/refine (`kOff_ItemVal_Subtype`) | item identity | `inventory.cpp:1841-1842`, `4007-4009`; `offsets.h:850-851`, `982-983` |
| C6 | `g_itemTableGlobal` → `[global]` = table → `+0x08` u32 count (`kOff_ItemTable_Count`) → `+0x58` `ptr[]` defs (**else `+0x50`** for TU 1.10–1.16) → `[defs + row*8]` = def | item definition | `inventory.cpp:260-279` (`DefForRow`), `1858-1868` (`ValidateTableGlobal`) |
| C7 | `def` → `+0x08` key string object → `[keyObj]` = `char*` (`StringField`) | engine key | `inventory.cpp:283-299` |
| C8 | `def` → `+0x20` loc-string struct → `[struct]` = provider → `provider+0x18` = u32 offset; `g_locMgrGlobal` → `[mgr]` → `mgr+0x58` `char*` + `mgr+0x60` u32 (TU ≥ 2.00), else `mgr+0x58` → blob `{+0x00 char*, +0x08 u32}` | localised display name | `inventory.cpp:308-342` |
| C9 | `def` → `+0x350` (`kOff_ItemDef_Groups`) vector `{+0x00 data, +0x08 count}` → u16 row ids; each row → `g_grpTableGlobal` → `DefForRow` → `grp+0x68` `_orderIndex` | category + top tab | `inventory.cpp:366-391`, `359-364` |
| C10 | `def` → `+0x90` (`kOff_ItemDef_Icons`) vector → `+0x00` u16 stringinfo row → `stringinfo` def → `+0x18` → str obj → `char*` | icon sprite name | `inventory.cpp:898`, `905-940` |
| C11 | `def` → `+0x210` u8 `_itemTier` | rarity 0..5 | `offsets.h:1090`; reader `inventory.cpp:950` |
| C12 | `def` → `+0x418` u16 bucket type (TU 1.17–1.18.02) / `+0x428` (PE ≥ 2625) / `+0x42` (legacy), fallback `+66`, final default `1` | destination bucket type | `inventory.cpp:3746-3751`; constant `offsets.h:949` |
| C13 | `holder` → `+0x08` (`kOff_InvHolder_Container`) = container | holder → container backref | `inventory.cpp:3942`, `4060`; `offsets.h:944` |
| C14 | container → `+0x88` (`kOff_Owner_TypeDesc`) → `+1` type tag; container → `+0xA0` (`kOff_Owner_Possessor`) → `+0xD0` (`kOff_Possessor_Pawn`) | liveness test `pawn == container` | `inventory.cpp:1325-1333`; `offsets.h:285-286` |
| C15 | container → `+0x68` (`kOff_Container_Sub`) → `+0x10` (`kOff_Sub_IdAllocator`) → `+0x20` (`kOff_IdAlloc_Counter`) | instance-id allocator | `inventory.cpp:3792-3802`; `offsets.h:975-976` |
| C16 | container → `+0x68` → `+0x38` (`kOff_Sub_EquipComp`) = equip component; `comp+0x08` (`kOff_EquipComp_Owner`) must point back | equip component (self-validating) | `inventory.cpp:3582-3585`; `offsets.h:1521-1522` |
| C17 | character container → `+0x68` → `+0xB8` (alternate path in multi-copy sync) → container → holder | alternate holder reach | `inventory.cpp:4635-4641`, `5653-5659` |
| C18 | TEB: `NtQueryInformationThread(..., ThreadBasicInformation, tbi[48], ...)` → `tbi+0x08` = TEB → `teb+0x58` (`kOff_Teb_TlsPointer`) → `[tlsArray]` = TLS block → `tls + RealmFlagOffsetForRevision(rev)` | realm flag address | `inventory.cpp:1281-1303`; `version_mapping.cpp:77-90` |
| C19 | `partyOwner = Player::GetOwner(index)` (fallback when no container identifies) | companion container | `inventory.cpp:3713-3717` |

## 1.D Hook table and Patch table (inventory)

### Hooks

| # | Install log context | Locator | Original bytes | Patched bytes | Callback signature | Apply condition | Restore |
|---|---|---|---|---|---|---|---|
| H1 | `"world: evaluate-wanted-state"` | `kSig_EvaluateCrimeWantedState` | *not recorded in source* — MinHook installs a 5-byte `jmp` at the resolved address; Trinity stores no original-byte copy | MinHook-generated detour (`jmp`), bytes not literals in source | `uint8_t __fastcall(void*, void*)` | Always attempted at `Install()` | `mem::RemoveHook(&g_evalWantedTarget)` (`inventory.cpp:2342`) |
| H2 | `"world: register-crime-event"` | `kSig_RegisterCrimeEvent` | as H1 | as H1 | `void __fastcall(void*, const char*, void*, void*)` | Only if `core::MayProbeLegacyCrimeEventDispatcherForRevision(revision)` (`revision <= 2850`, `version_mapping.cpp:67-70`) | `mem::RemoveHook(&g_registerCrimeTarget)` (`2343`) |
| H3 | `"inventory: item-count accessor"` / `"... legacy"` | `kSig_InvGetItemQty` / `_Legacy` | as H1 | as H1 | `int64_t __fastcall(void* container, uint16_t typeId, void* keyPtr)` | Always; legacy only if modern fails | `mem::RemoveHook(&g_qtyTarget)` (`2335`) |
| H4-H6 | *(no context string — raw `MH_CreateHook`)* | `gameBase+0x16077B0`, `+0x16078C0`, `+0x16081D0` | as H1 | as H1 | `int64_t __fastcall(void* rcx)` | `revision < 2625` only | **NOT removed** in `Inventory::Remove()` — `oGetMoney1/2/3` are never passed to `RemoveHook` (see `2328-2349`) |
| H7 | `"inventory: holder observer"` | `kSig_InvGetHolder`, `maxMatches=2` | as H1 | as H1 | `void* __fastcall(void* container)` | Always attempted | `mem::RemoveHook(&g_holderTarget)` (`2339`) |
| H8 | `"inventory: slot-expansion setter"` | `kSig_InvSetExpandSlots`, `maxMatches=4` | as H1 | as H1 | `void* __fastcall(void* holder, int* outErr, void* a3, uint16_t type, uint16_t count)` | Only if `!core::UsesTu201CompatibleRevision(revision)` | `mem::RemoveHook(&g_expandTarget)` (`2340`), **after** the restore calls that still use its trampoline |
| H9 | `"inventory: modern transaction commit"` | `kSig_InvCommit`, `maxMatches=2` | as H1 | as H1 | 8-arg TU2.01 ABI (see §B#11) | Only if `UsesTu201CompatibleRevision` | `mem::RemoveHook(&g_commit201Target)` (`2338`) |
| H10 | `"inventory: transaction commit"` | `kSig_InvCommit_Pre201`, `maxMatches=4` | as H1 | as H1 | 7-arg pre-2.01 ABI | Only if `!UsesTu201CompatibleRevision` | `mem::RemoveHook(&g_commitTarget)` (`2337`) |
| H11 | `"inventory: modern holder-insert"` / `"(PE 2944)"` | `kSig_InvHolderInsert201` or `kSig_InvHolderInsert2944`, `maxMatches=2` | as H1 | as H1 | 9-arg ABI | `UsesTu201CompatibleRevision` | `mem::RemoveHook(&g_insTarget)` (`2336`) |
| H12 | `"inventory: holder-insert"` / `"... legacy"` | `kSig_InvHolderInsert` / `_Legacy`, `maxMatches=2` | as H1 | as H1 | 9-arg ABI | `!UsesTu201CompatibleRevision` | `mem::RemoveHook(&g_insTarget)` (`2336`) |

### Byte patches

**There are no byte patches in `inventory.cpp`, `equipment.cpp` or `dye.cpp`.** Trinity never writes code bytes in these areas: it works exclusively via MinHook detours (§D hooks), direct native calls, and guarded heap reads/writes. The only code-space writes are the MinHook trampolines themselves, whose bytes are generated by MinHook and therefore not literals in Trinity's source.

For completeness, byte-level *code* comparison does exist elsewhere in the tree (`src/game/worker.cpp:21,67,114,148` — `ReadPatchBytes` / `kWorkerPatchEnabled`), but `worker.cpp` is outside the three feature areas and only *reads* those bytes.

The nearest thing to a "patch" inside inventory is the hardcoded-RVA MinHook install at `inventory.cpp:2098-2100` (§B rows 4–6), which is a hook, not a patch.

## 1.E Fail-closed / version-gated behaviour (inventory)

Exact conditions and the exact log text:

| Condition (source file:line) | Effect | Log message (verbatim format string) |
|---|---|---|
| `if (!mem::InstallHook("inventory: item-count accessor", kSig_InvGetItemQty, …)) { if (!mem::InstallHook(… kSig_InvGetItemQty_Legacy, "inventory disabled", …)) return false; }` — `inventory.cpp:2085-2091` | **Whole inventory subsystem aborts**: `Install()` returns false | via `mem::InstallHook`: `"%s signature NOT FOUND - %s."` → `"inventory: item-count accessor signature NOT FOUND - inventory disabled."` / `"inventory: item-count accessor legacy signature NOT FOUND - inventory disabled."` (`hooks.h:35`) |
| `if (!holderAddr) { LOG_ERR(...); return false; }` — `inventory.cpp:2107-2112` | `Install()` returns false, **entire inventory disabled** (no holder resolver at all) | `"inventory: holder resolver signature NOT FOUND - inventory disabled."` |
| `if (core::GetGameVersion().revision < 2625) { MH_CreateHook(…) } else { LOG(…); }` — `inventory.cpp:2096-2105` | The three legacy wallet hooks are simply not installed on TU ≥ 2.00. Wallet spoofing via `g_walletSpoofValue` then only survives through the `hkGetItemQty` coin branch (`1511-1515`) | `"inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid)."` |
| `if (!oItemValueCtor)` — `inventory.cpp:2152-2155` | `AddItem` / `AddItemsBulk` / `RestoreLostItem` / `AddMissingSealedArtifacts` / `SetDirectSilver`'s create path will be refused at `CanCommitAuthoritativeAdd` (`inventory_logic.cpp:5-10`) | `"inventory: native TrItemValue constructor unavailable - Add Item will be refused."` |
| `const bool addItemReady = oItemValueCtor && oHolderInsert && (oCommitPlacement || oCommitPlacement201) && oFreePlacements && oNtQueryInfoThread;` — `inventory.cpp:2281-2295` | If any primitive is missing, Add Item is refused | success: `"inventory: native Add Item path ready (ctor=%p planner=%p commit=%p free=%p)."` — failure: `"inventory: add-item path incomplete (ctor=%d planner=%d commit=%d free=%d teb=%d) - Add Item will be refused."` |
| `if (mem::InstallHook("inventory: holder observer", …)) … else { oGetHolder = resolvedGetHolder; LOG_WARN(…); }` — `inventory.cpp:2194-2204` | Server holder is learned only from later transactions | `"inventory: passive holder observer unavailable - waiting for an inventory transaction."` |
| `if (usesTu201CompatibleAbi) { … LOG_OK(…); } else if (!mem::InstallHook("inventory: slot-expansion setter", …)) { … }` — `inventory.cpp:2215-2233` | TU 2.01+ has **no** expansion-setter hook at all (replaced by the continuous per-tick guard). Pre-2.01 without the hook → call-only, may briefly revert | `"inventory: modern continuous slot-expansion guard active."` / `"inventory: slot-expansion setter hook failed - Slot Size applies call-only and may briefly revert when the game recomputes it."` |
| Consequence strings for the commit hooks — `inventory.cpp:2244-2254` | Edits still apply to the client mirror but revert on reconcile | `"quantity edits will not persist (revert on reconcile)"` (via `hooks.h:35`) |
| Consequence strings for holder-insert — `inventory.cpp:2269-2278` | Add Item refused; server capture narrowed | `"Add Item will be refused and server holder capture is limited"` / `"server holder relies on the commit hook alone"` |
| `if (!CanCommitAuthoritativeAdd(ready, haveDef, clientH, serverH))` — `inventory.cpp:4035-4046`, predicate `inventory_logic.cpp:5-10` | Refuses to add: requires ctor+planner+commit+free **and** a resolvable item def **and** a server holder **distinct from** the client holder. Never creates a client-only item | `"inventory: add item %u x%lld refused - authoritative server holder unavailable (ready=%d client=%p server=%p def=%p ctor=%d ins=%d commit=%d free=%d teb=%d)"` |
| `if (IsWearableGear(itemKey)) qty = 1;` — `inventory.cpp:4052-4055` | Wearables are always added one at a time (predicate `3805-3832`) | none |
| `if (!DefForRow(g_itemTableGlobal, typeId, &def)) return false;` — `inventory.cpp:5094`, `5123` | Unknown item IDs are dropped before queueing | none |
| `if (g_commitActive.load(std::memory_order_acquire)) return;` — `inventory.cpp:1814` (in `RepairUsedSlots`); also `3193`, `equipment.cpp:2033`, `2050` | **No container metadata writes while a commit is in flight** | none |
| `if (!ready) { … } / ShouldRetryAuthoritativeAdd(..., maxAttempts = 120)` — `inventory.cpp:4260-4274`; predicate `inventory_logic.cpp:12-18` | Bounded 120-tick retry when only the server holder is missing; a real commit error fails immediately | none |
| `if (!Player::Ready() && enable) return false;` — `inventory.cpp:5685-5688` | No Bounty refuses to touch `WantedInfo` before the world is loaded | none |
| `if (core::GetGameVersion().revision < 2625)` around the `Money_Copper` stack pin — `inventory.cpp:3161-3170` | Money max-stack pin only on TU ≤ 1.18 | none |
| `const uint16_t safeMax = (maxSlots > 0 && maxSlots <= 700) ? maxSlots : 700;` — `inventory.cpp:1708`; `if (value > 700) value = 700;` — `2983`, `1770`, `2888` | Slot-size requests are hard-clamped to 700 | none |
| `if (!g_itemTableGlobal) return false;` — `inventory.cpp:2625`; `if (!g_invTableGlobal) return false;` — `2940` | Stack/Slot overrides silently no-op when their tables did not resolve | none |
| `ShouldAttemptCatalogResolve` gate — `inventory.cpp:4301-4306`, `inventory_logic.cpp:36-44` | Catalog resolve is retried at most once per 1000 ms | `"inventory: catalog wait - iteminfo resolver global is missing."` / `"… global=%p has no runtime table (value=%p)."` / `"… table=%p has invalid row count %u at +0x%llX."` |
| `if (value < 1) value = 1;` / `if (value < 0) value = 0;` — `inventory.cpp:2640`, `4553`, `3337` | Input clamping only | none |
| `if (core::GetGameVersion().revision >= 2625)` fallbacks — `inventory.cpp:1948`, `1983` | Hardcoded table globals only exist for PE ≥ 2625 | `"inventory: table 'WantedInfo' resolved via TU 2.00 fallback -> %p"` / `"inventory: table 'tribeinfo' resolved via TU 2.00 fallback -> %p"` / `"inventory: table 'categorygroupinfo' resolved via fallback -> %p"` |

## 1.F Runtime validation hooks — exact Trinity log lines (inventory)

* `"inventory: table '%s' resolved via string-anchor -> %p"` — `inventory.cpp:1936-1937` (`LOG_OK`)
* `"inventory: captured transaction holder=%p container=%p."` — `inventory.cpp:1604-1605`
* `"inventory: passive holder observer installed @ %p"` — `inventory.cpp:2198` (`LOG_OK`)
* `"inventory: modern transaction commit hook installed @ %p"` — `inventory.cpp:2247-2248` (`LOG_OK`)
* `"inventory: TrItemValue modern native ctor resolved at %p"` — `inventory.cpp:2134-2135` (`LOG_OK`)
* `"inventory: TrItemValue legacy native ctor resolved at %p (matches=%zu)"` — `inventory.cpp:2146-2147` (`LOG_OK`)
* `"inventory: native Add Item path ready (ctor=%p planner=%p commit=%p free=%p)."` — `inventory.cpp:2284-2289` (`LOG_OK`)
* `"inventory: add-item path incomplete (ctor=%d planner=%d commit=%d free=%d teb=%d) - Add Item will be refused."` — `inventory.cpp:2291-2295` (`LOG_WARN`)
* `"inventory: native TrItemValue constructor unavailable - Add Item will be refused."` — `inventory.cpp:2154` (`LOG_WARN`)
* `"inventory: holder resolver signature NOT FOUND - inventory disabled."` — `inventory.cpp:2110` (`LOG_ERR`)
* `"inventory: passive holder observer unavailable - waiting for an inventory transaction."` — `inventory.cpp:2203` (`LOG_WARN`)
* `"inventory: slot-expansion setter hook failed - Slot Size applies call-only and may briefly revert when the game recomputes it."` — `inventory.cpp:2230-2231` (`LOG_WARN`)
* `"inventory: modern continuous slot-expansion guard active."` — `inventory.cpp:2220` (`LOG_OK`)
* `"inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid)."` — `inventory.cpp:2104`
* `"inventory: catalog wait - iteminfo resolver global is missing."` — `inventory.cpp:4312` (once, `g_catalogDiagState` gated)
* `"inventory: catalog wait - iteminfo global=%p has no runtime table (value=%p)."` — `inventory.cpp:4323-4324`
* `"inventory: catalog wait - table=%p has invalid row count %u at +0x%llX."` — `inventory.cpp:4334-4336`
* `"inventory: catalog table ready - global=%p table=%p rows=%u."` — `inventory.cpp:4341-4343`
* `"inventory: catalog built - named=%u groups=%u."` — `inventory.cpp:4400-4401`
* `"inventory: item '%s' not found in item table."` — `inventory.cpp:4506` (`LOG_WARN`)
* `"inventory: Silver_Pack not found in item table."` — `inventory.cpp:4739` (`LOG_WARN`)
* `"inventory: SetDirectSilver -> %lld Silver (%lld Copper) [status=%d]."` — `inventory.cpp:4700`
* `"inventory: Added %lldx '%s' (TypeID %u, InstID 0x%llX) [server=%d client=%d]."` — `inventory.cpp:4130-4132`
* `"inventory: add item %u x%lld refused - authoritative server holder unavailable (ready=%d client=%p server=%p def=%p ctor=%d ins=%d commit=%d free=%d teb=%d)"` — `inventory.cpp:4037-4044` (`LOG_WARN`)
* `"inventory: add item %u x%lld FAILED (server=%d client=%d) bucketWant=%u clientH=%p serverH=%p"` — `inventory.cpp:4146-4150` (`LOG_WARN`)
* `"inventory: add[%s] tid=%u EXCEPTION in engine path (built=%d planned=%d committed=%d)"` — `inventory.cpp:3924-3925` (`LOG_WARN`)
* `"inventory: add[%s] tid=%u FAILED: err=%d nPlaced=%u firstErr2=%d (built=%d planned=%d)"` — `inventory.cpp:3927-3928` (`LOG_WARN`)
* `"inventory: add[%s] %u: holder has no container"` — `inventory.cpp:3945`
* `"inventory: add[%s] %u: no bucket of type %u in holder"` — `inventory.cpp:3953-3954`
* `"inventory: add[%s] %u: realm flag unresolved (byte=0x%02X)"` — `inventory.cpp:3962-3963`
* `"inventory: add[%s] %u: realm flag not writable"` — `inventory.cpp:3970`
* `"world: WantedInfo table @ %p - No Bounty available."` / `"world: WantedInfo table not found - bounty price left alone."` — `inventory.cpp:5704-5706`
* `"world: No Bounty %s - price zeroed on %d/%u wanted row(s)."` — `inventory.cpp:5752-5753` (`LOG_OK`)
* `"world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active."` — `inventory.cpp:2081-2082`

## 1.G Item ID / catalog tables (inventory)

* **Item ID representation.** Item type ids are **`uint16_t`**, and the id **is the row index** into the `iteminfo` table: `DefForRow(g_itemTableGlobal, typeId, &def)` reads `[defs + typeId*8]` (`inventory.cpp:260-279`). The catalog builder literally does `const uint16_t tid = static_cast<uint16_t>(row);` (`inventory.cpp:4351`) and `FindTypeIdByKey`'s fallback scan iterates rows as ids (`4484-4492`).
* **Sentinels.** `kInvSlot_EmptyType = 0xFFFF` (`offsets.h:852`); `0` also treated as empty throughout (e.g. `inventory.cpp:4007`, `4352`). `kSock_Empty = 0xFFFF` for sockets (`offsets.h:1666`).
* **ID range.** The table's own row count is validated as `count > 0 && count <= 65536` for the catalog (`inventory.cpp:4330`) and `<= 500000` in `ValidateTableGlobal` (`1860`). Slot arrays accept `scount` up to `8192` (`1842`, `4001`), buckets up to `4096` (`1173`, `1725`). Item-value stride is `core::GetSlotStride()` = `0xC0` (TU 1.13–1.15) / `0xC8` (TU 1.16+) (`version_detect.cpp:122-128`).
* **Where the catalog/name table comes from.**
  1. **Live game tables (primary).** `iteminfo` (`kStr_ItemInfoTable = "iteminfo"`, `offsets.h:1023`) supplies the engine key (`def+0x08`), the localised name (`def+0x20` through the loc manager), the icon (`def+0x90` vector → `stringinfo` row), the tier (`def+0x210`) and the group list (`def+0x350`). Categories come from `ItemGroupInfo` (`kStr_ItemGroupInfoTable`, `offsets.h:1128`) with the game's own `_orderIndex` ordering (`grp+0x68`; `<= 5` is a top tab, `0xFFFF` never displays — `offsets.h:1131-1132`). Storage names come from the `Inventory` table (`kStr_InventoryInfoTable`, `offsets.h:1059`). **Nothing about the live catalog is hardcoded.**
  2. **Heuristic fallback categorisation.** When the group table is missing, `DeduceCategoryFromItem(key, name)` (`inventory.cpp:526-...`) classifies by substring into a fixed set of English labels ("Abyss Gear", "Sealed Artifacts", "Special Boss Quest Equipment", "Controls", "Kuku Pot", "Lures", "Recipe Books", "Crafting Recipes", "Treasure Maps", "Wanted Posters", "Wall Documents", "Quest Memories", "Books", "Documents", "Packed Trade Goods", "Unpacked Trade Goods", "Animal Items", "Collection", …). `CleanCategoryFallback` (`inventory.cpp:395-472`) then renames ~50 engine keys to prose labels via the `exact[]` table (e.g. `"Ammo"→"Ammunition"`, `"Equip Weapon One Hand"→"One-Handed Weapons"`, `"Money"→"Currency"`, `"Sealed Artifact"→"Sealed Artifacts"`).
  3. **Baked-in display-name map (`src/game/item_names.cpp`).** A generated `ItemPair { const char* key; const char* name; }` array named `kItemMap` (`item_names.cpp:11`) of **6,258 rows** (measured: 6,258 lines match `^\s*\{\s*"`), loaded into an `std::unordered_map<std::string, const char*>` by a static `MapInit` (`item_names.cpp` tail, `MapInit s_itemMapInit;`), queried by `ResolveItemDisplayName(const char* key)` (`item_names.cpp:6285-6291`) which returns `nullptr` for keys with no mapping. Examples: `{ "Abyss_Artifact", "Abyss Artifact" }`, `{ "AbyssReward_Drake_Metal_OneHandShield", "Drake Shield" }`, `{ "Aant_PlateArmor_Helm", "Awrain Plate Helm" }`, `{ "Bamboo_Branch_01", "Bamboo Stalk" }`.
     **It IS consumed** by these areas — it is the *first* thing `Prettify` tries. `Prettify(const char* key, char* out, size_t n)` (`inventory.cpp:994-1018`) calls `ResolveItemDisplayName(key)` at `inventory.cpp:997`, and on a hit copies the mapped name through `CleanItemName` and returns (`998-1003`). Only when the map misses does it fall back to the `_`→space + camel-case splitter (`1005-1017`). `CleanItemName` (`inventory.cpp:962-988`) applies a 5-entry rename table (`"Ziane Diary"→"Jian's Journal"`, `"Ziane_Diary"→"Jian's Journal"`, `"Ziane One Hand Sword"→"Jian's One-Handed Sword"`, `"Ziane_One_Hand_Sword"→"Jian's One-Handed Sword"`, `"Visione Chip Ziane Tomb"→"Vision Chip - Jian's Tomb"`) and then rewrites a leading `"Ziane "` to `"Jian's "`.
     `Prettify` is the universal fallback name source, used by `GroupName` (`inventory.cpp:492`), `DisplayNameForType`'s callers via `Inventory::NameForTypeId` (`3370`), the catalog builder (`4361`), `CommitAdd` (`4129`) and `TrackInventoryChanges` (`3066`). There is also a second, independent call site in the UI at `menu.cpp:2437`.
     Ordering therefore is: **live localised name (`def+0x20` via the loc manager) → baked `kItemMap` → heuristic prettify of the engine key → `"Item #%u"`.**
* **Hardcoded id lists that DO exist.**
  * **Character identity by equipped gear** — `IdentifyCharacterIdentity` (`inventory.cpp:3500-3513`) and `Equipment::IsItemForCharacter` (`equipment.cpp:1385-1415`) both hardcode:
    * Damiane (1): `tid == 53935 || 6324 || 6041 || 5306 || 5300 || 5297 || 5277 || 3463`, ranges `5450..5468`, `5270..5310`, `6320..6330`
    * Oongka (2): `tid == 6560 || 6042 || 6305`, ranges `6550..6570`, `3762..3777`, `1090..1094`, plus `2299`, `3740`, `1390`
    * Kliff (0): `tid == 6303 || 6040`, range `5330..5350`
  * **Engine-key string lists** (not ids) — lost-item/currency keys: `"Money_Copper"` (`inventory.cpp:1509`, `4557`, `4711`, `4751`, `4861`, `3163`), `"Silver_Pack"` (`4736`, `4752`), `"Money_Camp_Money" / _Food / _Weapon / _Timber / _Stone` (`1523-1527`) plus `"Money_Camp_Wood"` in `AddCampCurrency` (`4846`) — note `Timber` and `Wood` are **different keys used in different places**, which is an observed inconsistency. Dollar-suffix keys for camp currency in `SetAllMaxStackSizes`: `"Money_Camp_Money"`, `"Money_Exchange"` (`2673`).
  * **Sealed Abyss Artifact ids are derived, not listed**: key format `"Sealed_Abyss_Artifact_%04d"` for `1..150` (`inventory.cpp:5506`, `5537`, `5568`; `totalMax = 150` at `5498`).
  * **Menu-side hardcoded quick-add keys** (`menu.cpp:2226-2274`, `2478`) — listed in §1.A.
* **Catalog shape.** `BuildCatalog()` (`inventory.cpp:4296-4402`) walks the whole table once, skips rows with no resolvable name (`4363`), skips `0`/`0xFFFF` (`4352`) and un-resolvable defs (`4354`), and logs `"inventory: catalog built - named=%u groups=%u."`. Items within a group are sorted case-insensitively by name (`4392-4394`); groups are sorted by game `order` then label (`4396-4399`). `qty` is always 0 for a catalog entry (documented `inventory.h:184-186`).

---

# 2. EQUIPMENT

## 2.A Feature list (user-visible) with exact C++ entry points

UI labels live in the PLAYER tab (`menu.cpp:129-130` gate the submenu on `Equipment::Ready()`).

| Menu label (`menu.cpp:line`) | Trinity entry point | Source |
|---|---|---|
| PLAYER ▸ **Infinite Item Durability** toggle (`91`) | `Equipment::RepairAll()` re-driven from `Equipment::Tick()` | `equipment.cpp:2033-2042`, `1823` |
| PLAYER ▸ gear submenu (`129`-`130`) | `Equipment::Ready()`, `Equipment::EditsPersist()` | `equipment.cpp:1592-1593` |
| · **Repair All Gear** (`606`) | `Equipment::RepairAll(int* repairedCount)` | `equipment.cpp:1823` |
| · **Max Refine All (+10)** (`615`) | `Equipment::RefineAll(int level=10, int* refinedCount)` → `SetRefine` per slot | `equipment.cpp:1880` |
| · **Unlock All Sockets** (`624`) | `Equipment::UnlockAllGears(int* unlockedCount)` → `UnlockAll` per slot | `equipment.cpp:1903` |
| · per-piece: **Unlock all sockets** (`690`) | `Equipment::UnlockAll(uint16_t tag)` | `equipment.cpp:1760` |
| · per-piece: **Clear all sockets** (`700`) | `Equipment::ClearAll(uint16_t tag)` | `equipment.cpp:1793` |
| · per-piece: **Refinement** 0..10 (`715`) | `Equipment::SetRefine(uint16_t tag, int level, bool* persisted)` | `equipment.cpp:1722` |
| · per-socket: add abyss gear (`766`, `958`) | `Equipment::AddGear(uint16_t tag, int socketIdx, uint16_t gearTypeId, bool* persisted)` | `equipment.cpp:1651` |
| · per-socket: clear (`922`) | `Equipment::ClearGear(uint16_t tag, int socketIdx, bool* persisted)` | `equipment.cpp:1688` |
| · **Equip Item** picker + `Equip to active slot - bypasses quest & class lock` (`782`, `882`, `887`) | `Equipment::EquipItemToSlot(uint16_t tag, uint16_t typeId, int64_t instId = 0)` | `equipment.cpp:1926` |
| · abyss-gear picker (`835`) | `Equipment::GearCount()`, `GetGear`, `GetGearBuffDescription`; filter `IsItemForSlot` | `equipment.cpp:1627-1648`, `1457` |
| · character selector | `SetActiveCharacter/GetActiveCharacter/CharacterName` | `equipment.cpp:1595-1607`, `1356` |
| (implicit) refinable-slot safety filter | `Equipment::IsRefinableTag(uint16_t)` | `equipment.cpp:1329` |
| (implicit) equipped-item effect refresh after every edit | `Equipment::Tick()` → `g_refresh(comp, &err)` | `equipment.cpp:2130-2147` |
| (implicit) persistent disk profiles (auto save/restore) | `SaveEquipProfilesToDisk` / `LoadEquipProfilesFromDisk` / `SavePlayerEquipSlot` / `ClearPlayerEquipSlot` / `HasCustomProfile`; INI `Trinity_EquipmentProfile.ini` next to the module | `equipment.cpp:1983-2019`, `898-1050` |
| (implicit) lost-item false-positive guard | `Equipment::IsItemEquippedOnAnyCharacter(uint16_t)` used from `TrackInventoryChanges` | `equipment.cpp:2150`; caller `inventory.cpp:3093` |

## 2.B Game-side function contracts (equipment)

`equipment.cpp` installs **no hooks**. Its only two direct game-side touch points are two resolved native calls; everything else is guarded heap access through the shared inventory plumbing.

| # | Trinity role | Game function / address expression | How the address is obtained | Exact ABI Trinity assumes | Object operated on | Offsets read/written | Source file:line | Fail-safe if missing |
|---|---|---|---|---|---|---|---|---|
| E1 | N | Equipped-item **effect refresh** `sub_7C88A0` (`0x140AEBE70` in TU 1.17/1.18) | `mem::FindPattern(kSig_EquipEffectRefresh)`, fallback `kSig_EquipEffectRefresh_Legacy` | `void*(__fastcall*)(void* equipComponent, int* out)` (typedef `equipment.cpp:45`) | client equip component | none directly (engine re-reads the item values) | resolve `equipment.cpp:1566-1568`; call `2142` | `if (!g_refresh) return;` in `Tick` (`2131`) → socket/refine edits still write, but effects apply only on reload. Init logs `LOG_WARN("equipment: EquipEffectRefresh signature not found.")` (`1572`) |
| E2 | N | Native `ResizeSocketVector` — literal pattern `"48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00"` (**no `kSig_` constant; literal at the call site**) | `mem::FindPattern(<literal>)` | `bool(__fastcall*)(void* itemVal, uint32_t count)` (typedef `equipment.cpp:49`) | equipped item value | allocates the 5-slot socket vector at `itemVal+0x60` (legacy `+0x58`) | resolve `equipment.cpp:1574`; call `494` | `EnsureSocketVector` returns 0 → `WriteSocketToEntry` returns false (`505-506`) and the whole socket edit is reported as not persisted. `g_resizeSocket` null-checked at `492` |
| E3 | R/W | Equip table descriptor | read `comp+0x90` (TU 2.01+), else `comp+0x80` (TU 1.17–2.00), else `comp+0x88` (legacy TU 1.14), else probe `{0x90,0x80,0x50,0x38,0x40,0x48,0x60,0x70}` | n/a | equip component | R `desc+0x08` (`kOff_EquipTable_Array`), R `desc+0x10` (`kOff_EquipTable_Count`); entry stride `0xD0` (legacy `0xC8`), slot tag at `entry+0xC8` (legacy `0xC0`) | `equipment.cpp:62-158` (`ReadEquipTableDesc`), validation `67-80` | `CompValid()` false → `ClientComp()`/`ServerComp()` return 0 → every edit returns false early |
| E4 | W | Equipped item value — **socket records** | n/a | n/a | equipped entry / inventory slot | W `rec+0x00` u16 gear id (`kOff_SockRec_GearId`), `rec+0x02` u16 marker (`0xFFFF` filled / `0x0000` empty), `rec+0x04` u8 index, `rec+0x05` u8 state (`0x05` / `0x00`); record stride `6` (`kSocketRec_Stride`) | `equipment.cpp:472-483` (`WriteRecord`); constants `offsets.h:1659-1666` | Guarded `mem::WriteXX`; `ok &=` accumulated but the return value of `WriteRecord` is only used for the aggregate |
| E5 | W | Equipped item value — **socket vector geometry** | n/a | n/a | equipped entry / inventory slot | W `+0x68` size=5, `+0x6C` cap=5, `+0x70` unlocked (`isLegacy ? 0x60/0x64/0x68`) | `equipment.cpp:522-530`, `552-559` | — |
| E6 | W | Equipped item value — **refine level** | n/a | n/a | equipped entry / inventory slot | W `entry+0x0A` u16 (`kOff_ItemVal_RefineLevel`, == `kOff_ItemVal_Subtype`), clamped to `kRefine_Max = 10` | `equipment.cpp:683`, `694`, `703`, `717`, `728`, `2093`; constants `offsets.h:1682-1683` | `IsRefinableTag` gate (`1725`) |
| E7 | W | Equipped item value — **durability** | n/a | n/a | equipped entry | W `entry+0x40` u16 (`kOff_ItemVal_Durability`) ← `10000` | `equipment.cpp:1839`; constant `offsets.h:1684` | `IsDummyOrUnarmed` gate (`1838`) |
| E8 | W | Equipped item value — **full equip stamp** | n/a | n/a | equipped entry (client + server realm) | W `+0x00` i64 instanceId, `+0x08` u16 typeId, `+0x40` u16 durability, `+0x10` i64 quantity=1, `+0x0A` u16 refine=10; then `EnsureSocketVector` | `equipment.cpp:1935-1943` | — |
| E9 | R/W | Equip component owner back-reference | n/a | n/a | equip component | R `comp+0x08` (`kOff_EquipComp_Owner`) | `equipment.cpp:297`, `641`; `offsets.h:1522` | — |
| E10 | R | Equip component reached by walk `*(*(actor+0x68)+0x38)` | n/a | n/a | actor → sub → comp | R `actor+0x68` (`kOff_Container_Sub`), R `sub+0x38` (`kOff_Sub_EquipComp`) | `equipment.cpp:168-178`; `offsets.h:1521` | Falls through to the alternate-offset probes (`181-201`) then returns 0 |
| E11 | R | Profile auto-restore safety check | n/a | n/a | equipped entry | R `+0x08` typeId, R `+0x68/0x6C/0x70` (legacy `+0x60/0x64/0x68`) size/cap/unlocked; requires `sz<=5 && cap<=5 && cap>=sz && unlocked<=5` and `IsItemForSlot(tag, tid, name, name)` | `equipment.cpp:580-604` (`ProfileTargetValid`) | Returning false skips that tag (`2086`) |
| E12 | W | TLS realm flag (server mirror) | `Inventory::RealmFlagAddress` | n/a | TLS byte | W 1 / restore | `equipment.cpp:644-667`, `710-733`, `774-791`, `828-845`, `1860-1869`, `1955-1964` | `if (flagAddr && RawWrite8(flagAddr, 1))` — the server half is simply skipped when the flag is unavailable |
| E13 | R/W | All inventory holders by instance id (socket/refine mirror) | `Inventory::FindAndApplyAllHolders(instId, cb, ud)` | callback `void(*)(uintptr_t slotEntry, void* userData)` | holder slots | R/W `slotEntry+0x0A`, `slotEntry+0x60..0x70` | `equipment.cpp:640`, `666`, `706`, `732`, `770`, `789`, `824`, `843`; implementation `inventory.cpp:5607-5679` | Returns the matched-holder count; callbacks only run on resolvable slots |
| E14 | R | `Abyss Gear` catalog category | `Inventory::CatalogCategoryCount/Name` with `_stricmp(name, "Abyss Gear")`, then a `strstr(low, "abyss gear") || strstr(low, "abyss geer")` fallback | n/a | Trinity catalog | none | `equipment.cpp:858-884` | `g_gearCat = -1` → `GearCount()/GetGear()` return 0/false (`1629-1636`) |

## 2.C Pointer chains / object resolution (equipment)

| # | Chain | Yields | Source file:line |
|---|---|---|---|
| EC1 | `actor` → `+0x68` (`kOff_Container_Sub`) → `+0x38` (`kOff_Sub_EquipComp`) = **equip component**; validated by the table probe (EC2) | equip component (primary) | `equipment.cpp:168-178`, `206-227` |
| EC2 | `comp` → `+0x90` / `+0x80` / `+0x88` (or one of `{0x50,0x38,0x40,0x48,0x60,0x70}`) = desc → `+0x08` array, `+0x10` count | equip entry table | `equipment.cpp:82-157` |
| EC3 | `array + i*stride` → `+0xC8` slot tag (legacy `+0xC0`), `+0x08` typeId, `+0x10` quantity, `+0x00` instanceId | equipped entry | `equipment.cpp:380-397`, `1187-1204` |
| EC4 | `entry` → `+0x60` socket vector data (legacy `+0x58`) → `+ i*6` records; `+0x68` size, `+0x6C` cap, `+0x70` unlocked (legacy `+0x60/0x64/0x68`) | socket array | `equipment.cpp:404-434`, `436-449`, `451-460` |
| EC5 | `actor` → alternate sub-object offsets `{0x60,0x68,0x70,0x58,0x78,0x80,0x88,0x90,0x98,0xA0}` → component offsets `{0x38,0x30,0x40,0x28,0x48,0x50,0x58,0x60,0x68}` | component fallback | `equipment.cpp:181-193` |
| EC6 | `actor` → direct component offsets `{0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80,0x88,0x90,0x98,0xA0,0x168}` | component fallback | `equipment.cpp:196-201` |
| EC7 | owner object → `+0x68` (`kOff_Owner_Actor`) → inner actor → `FindEquipCompFromActor` | component fallback for owner objects | `equipment.cpp:218-225` |
| EC8 | `Player::GetOwner(p)` / `Player::GetActor(p)` → `PreferEquipmentOwner(owner, actor)` = `owner ? owner : actor` (`equipment_logic.cpp:16-19`) → `CompForCharacter` | tracked-party component | `equipment.cpp:229-243` |
| EC9 | `Inventory::ClientCharacterAddr()` (`inventory.cpp:3388-3400`) → `CompForCharacter` | live client component | `equipment.cpp:267-275`, `2063-2064` |
| EC10 | `Inventory::ServerCharacterAddr()` (`inventory.cpp:3402-3415`) → `CompForCharacter` | server component | `equipment.cpp:334-342` |
| EC11 | `Inventory::CharacterAddr(c)` (`inventory.cpp:3702-3720`) → `CompForCharacter` | off-screen character component | `equipment.cpp:311-320`, `345-353`, `2070-2071` |
| EC12 | `Inventory::ClientHolderAddr()` → `+0x08` = owner → `CompForCharacter` | fallback live component | `equipment.cpp:278-289` |
| EC13 | `Dye::HookedClientComp()` (equip-batch capture) → validate `comp+0x08 == liveChar` and identity | last-resort live component | `equipment.cpp:292-303` |
| EC14 | `Inventory::IdentifyCharacterFromComp(comp)` → 0/1/2/-1 (`inventory.cpp:3611-3614`) feeds `AcceptCharacterComponent(selected, identified, party)` (`equipment_logic.cpp:5-14`): a recognised identity **wins** over the party index; the party index is only a fallback when `identified < 0` | per-character routing | `equipment.cpp:238-239`, `271-273`, `317-318`, `338-340` |
| EC15 | Character identity by equipped gear: for each entry `tid = entry+0x08`; first matching hardcoded id or key/name substring wins (see §1.G for the id list) | identity 0/1/2 | `inventory.cpp:3432-3575` (`IdentifyCharacterIdentity`), `3578-3609` (`IdentifyCharacterFromEquip`) |

## 2.D Hook table and Patch table (equipment)

**Hooks: none.** `Equipment::Install()` (`equipment.cpp:1564-1582`) only resolves two native addresses (E1, E2) and loads the profile INI. `Equipment::Remove()` (`1584-1590`) nulls those pointers and resets `g_gearCat`/`g_dirty`; there is nothing to `MH_RemoveHook`.

**Byte patches: none.**

## 2.E Fail-closed / version-gated behaviour (equipment)

| Condition (source file:line) | Effect | Log message |
|---|---|---|
| `if (!IsRefinableTag(tag)) return false;` — `equipment.cpp:1725`; tag list `1329-1354` | Refine is refused on tags 14 (Saddle), 15 (Lantern), 17 (Glasses), 18 (Mask), 19 (Backpack), 20 (Bracelet), 21–25 (mount gear) — comment: *"These items have no enhancement curve in engine ItemInfo; refining them causes 0xC0000005 crash."* | none |
| `if (!comp) return false;` / `if (!entry) return false;` / `if (instId <= 0) return false;` / `if (tid == 0 || tid == kInvSlot_EmptyType) return false;` — e.g. `equipment.cpp:1656-1664`, `1693-1701`, `1727-1735`, `1765-1773`, `1946-1951` | Every edit is a no-op when the component, entry, instance id or type id does not read back sane | none |
| `if (IsDummyOrUnarmed(tid, itemName)) return false;` — `equipment.cpp:1667`, `1704`, `1738`, `1776`, `1806`, `1838` (helper `360-372`) | Placeholder "Ordinary Gloves"/"Unarmed" gear is never edited | none |
| `if (IsDummyOrUnarmed(tid, itemName)) continue;` — `equipment.cpp:1201`, `393`-`395` in `FindEntryByTag` | Placeholder gear is excluded from the menu snapshot | none |
| `if (!ProfileTargetValid(entry, t)) continue;` — `equipment.cpp:2086` (impl `580-604`) | Auto-restore refuses an entry whose socket vector is structurally wrong or whose item does not belong to that slot — *"blindly writing refine/socket bytes into a quest item or container is what produced the random menu crashes on newer game versions"* | none |
| `if (st.infDurability && !Inventory::IsTransactionActive())` + `now - s_lastRepair >= 1500` — `equipment.cpp:2033-2042` | Durability pin is skipped while an engine commit is in flight and is throttled to 1.5 s | none |
| `nowEquipRestore - s_lastEquipRestore >= 1500 && !Inventory::IsTransactionActive()` — `equipment.cpp:2050` | Profile auto-restore is skipped during a transaction (*"to avoid corrupting container metadata and triggering Error 298648703"*) | none |
| `if (!Player::Ready()) return;` — `equipment.cpp:2027` | `Tick` does nothing before the world is loaded | none |
| `if (!comp) { g_dirty.store(true, …); return; }` — `equipment.cpp:2133-2138` | A pending effect refresh is retried next frame instead of being dropped | none |
| `__except (EXCEPTION_EXECUTE_HANDLER) { LOG_WARN("equipment: effect refresh faulted - the gear will apply on reload."); }` — `equipment.cpp:2144-2147` | An engine fault during the refresh is contained | `"equipment: effect refresh faulted - the gear will apply on reload."` |
| `if (!g_refresh) return;` — `equipment.cpp:2131` | No refresh at all if E1 did not resolve | `"equipment: EquipEffectRefresh signature not found."` (`1572`) at install |
| `if (!g_resizeSocket)` — `equipment.cpp:492-499` | Socket writes fail when the vector is unallocated and cannot be allocated | none |
| `versionOk = strcmp(line + 14, core::GetGameVersionDisplay()) == 0;` — `equipment.cpp:965-968`, enforced at `1016-1018` | Profiles written under a **different game build** are refused on load — *"a patch can reshuffle the tag -> item mapping, and re-applying a stale profile would then corrupt whatever now lives in that slot (the 'crash after update, fixed by deleting the INI' report)"* | `"equipment: profile saved under a different game build - ignored (file will re-save under %s)."` |
| `if (charIdx < 0 || charIdx >= 3 || tag >= 32) return;` — `equipment.cpp:1995`, `2010`, `2017` | Profile slots are bounded to `[3][32]` (`896`) | none |
| `if (off < defSlots) …` / `prof.refineLevel > 10 ? 10` / `maxSock > 5 ? 5` — `equipment.cpp:1999-2000` | Clamping | none |
| `if (socketIdx < 0 || socketIdx >= kMaxSockets || gearTypeId == kSock_Empty) return false;` — `equipment.cpp:1654`, `1691` | Out-of-range socket index refused | none |
| `if (typeId == 0 || typeId == kInvSlot_EmptyType) return false;` — `equipment.cpp:1928` | `EquipItemToSlot` refuses empty ids | none |
| `if (total <= 0) return false;` — `equipment.cpp:1884`, `1907` | Batch operations refuse to run on an empty snapshot | none |

## 2.F Runtime validation hooks — exact Trinity log lines (equipment)

* `"equipment: EquipEffectRefresh resolved @ %p."` — `equipment.cpp:1570`
* `"equipment: EquipEffectRefresh signature not found."` — `equipment.cpp:1572` (`LOG_WARN`)
* `"equipment: native ResizeSocketVector resolved @ %p."` — `equipment.cpp:1576`
* `"equipment: loaded persistent profiles from '%s'."` — `equipment.cpp:1021`
* `"equipment: profile saved under a different game build - ignored (file will re-save under %s)."` — `equipment.cpp:1016-1017` (`LOG_WARN`)
* `"equipment: active character switched to '%s' (index %d)."` — `equipment.cpp:1599`
* `"equipment: [%s] Slot [%s (Tag %u)] Socket %d -> Added '%s' (0x%04X) [persisted=%d]."` — `equipment.cpp:1682-1683`
* `"equipment: [%s] Slot [%s (Tag %u)] Socket %d -> Cleared [persisted=%d]."` — `equipment.cpp:1716-1717`
* `"equipment: [%s] Slot [%s (Tag %u)] -> Refinement set to +%u [persisted=%d]."` — `equipment.cpp:1754-1755`
* `"equipment: [%s] Slot [%s (Tag %u)] -> All %d sockets unlocked."` — `equipment.cpp:1787-1788`
* `"equipment: [%s] Slot [%s (Tag %u)] -> Cleared all socket gems."` — `equipment.cpp:1817-1818`
* `"equipment: repaired %d equipped items to 100%% durability (10,000)."` — `equipment.cpp:1876` (note the escaped `%%`, emitted as `100%`)
* `"equipment: [%s] Refined all %d eligible equipped pieces to +%d."` — `equipment.cpp:1899`
* `"equipment: [%s] Unlocked all sockets on %d pieces."` — `equipment.cpp:1922`
* `"equipment: [%s] Slot [%s (Tag %u)] -> Directly Equipped '%s' (TypeID %u, InstID 0x%llX)."` — `equipment.cpp:1977-1978`
* `"equipment: effect refresh faulted - the gear will apply on reload."` — `equipment.cpp:2146` (`LOG_WARN`)

## 2.G Item ID / catalog tables (equipment)

* The abyss-gear picker does **not** have its own table: `Equipment::GearCount()/GetGear()` proxy into the inventory catalog, filtered to the category whose localised label is `"Abyss Gear"` (`equipment.cpp:858-884`, `1627-1643`). Type ids are therefore the same `uint16_t` `iteminfo` row indices as in §1.G.
* **Slot tags** are `uint16_t`, validated `tag < 32` (`equipment.cpp:75-76`), with the display name map at `equipment.cpp:1295-1327`: 0 Main Hand, 1 Off-Hand, 2 Ranged Weapon, 3 Helmet, 4 Chest, 5 Gloves, 6 Boots, 7/8 Earring 1/2, 9 Necklace, 10/11 Ring 1/2, 12 Dagger, 13 Two-Handed Weapon, 14 Saddle, 15 Lantern, 16 Cloak, 17 Glasses, 18 Mask, 19 Backpack, 20 Bracelet, 21 Rocket, 22 Chamfron, 23 Horse Armor, 24 Stirrups, 25 Horseshoes; `26..31` (and anything ≥ 32) return `nullptr` → the UI shows `"Slot #%u"` (`equipment.cpp:1209`).
* **Socket record format** (6 bytes, `offsets.h:1659-1666`): `+0` u16 gear typeId (`0xFFFF` = empty), `+2` u16 marker (`0xFFFF` filled / `0x0000` empty), `+4` u8 socket index (`0xFF` = the game's "locked" record), `+5` u8 state (`0x05` filled / `0x00` empty). Vector is pre-allocated: size `+0x68` and cap `+0x6C` are always 5; `+0x70` is the unlocked count.
* **Hardcoded item-id lists** used to route/identify equipment are the same ones listed in §1.G (Damiane/Oongka/Kliff sets in `inventory.cpp:3500-3513` and `equipment.cpp:1385-1415`).
* **Buff-description table** is a hardcoded substring→prose map, `ResolveGearBuff` at `equipment.cpp:1063-1175` (e.g. `"Destruction I"` → `"Attack 1"`, `"Abyssbane III"` → `"Abyss Damage +15%"`, `"Malicebane"` → `"Boss Damage +10%"`). Materials-specific variants map e.g. `"Insight Gear" + "Fabric"` → `"Crit Rate (Fabric)"`.

---

# 3. DYE / APPEARANCE

## 3.A Feature list (user-visible) with exact C++ entry points

| Menu label (`menu.cpp:line`) | Trinity entry point | Source |
|---|---|---|
| PLAYER ▸ dye submenu (gated on `Dye::Ready()`) | `Dye::Ready()`, `Dye::ActiveClientComp()` | `dye.cpp:1654-1659`, `1684-1687` |
| · equipped-slot list / per-slot select (`371-377`, `409`, `521`) | `Dye::SlotCount()`, `Dye::GetSlot(int, SlotInfo*)` | `dye.cpp:1696-1707` |
| · channel selector + live RGB preview (`443`, `543`) | `Dye::GetChannel(uint16_t tag, int channel, Channel*)` | `dye.cpp:1709-1743` |
| · `Apply` color / `Apply This Color` (`218`, `251`, `535`) | `Dye::Apply(uint16_t tag, int channel, const Channel&)` (queued) | `dye.cpp:1745-1755` |
| · per-channel clear (`476`) | `Dye::Clear(uint16_t tag, int channel)` | `dye.cpp:1756-1768` |
| · `Remove All Dye (Reset)` (`489`, `491`) | `Dye::Clear(uint16_t tag, -1)` (channel `-1` = all 12) | `dye.cpp:1756`, `1276-1277` |
| · `Inject All Dyes to Save Data` (`307`, `310`) | `Dye::InjectAllToSave()` | `dye.cpp:1963` |
| · `Material` 0..10 (`496`) | `Channel::materialId` → `BuildSetRecord` `+4` u16 | `dye.cpp:570-584` |
| · `Condition %` 0..100 (`498`) | `Channel::repair` → `BuildSetRecord` `+11` | `dye.cpp:581` |
| · channel-mask / zone count per item | `SlotInfo::dyeCount`, `SlotInfo::maxZones`, `SlotInfo::dyeable` | `dye.cpp:1124-1245` |
| · bag-item dye mode (Mode 2) | `Dye::SetInventoryMode(bool)` / `GetInventoryMode()` | `dye.cpp:1673-1682`; mode branch `1129-1189`, `1256-1317` |
| · character selector (Kliff/Damiane/Oongka) | `Dye::SetActiveCharacter(int)` / `GetActiveCharacter()` | `dye.cpp:1661-1671` |
| (implicit) per-frame auto-restore of saved dye after reload / fast travel / gear change | `Dye::Tick()` | `dye.cpp:1777-1961` |
| (implicit) raw capture of the live equip component | `Dye::HookedClientComp()` | `dye.cpp:1689-1694` |
| (implicit) on-disk dye cache | `SaveDyeCacheToFile` / `LoadDyeCacheFromFile`, file `Trinity_DyeCache.bin` (**`GetDyeCachePath` — path is resolved from the module directory; the exact filename literal is inside `GetDyeCachePath` at `dye.cpp:799` and was not fully read**) with magic `"TRDYE03"` (`dye.cpp:843`) | `dye.cpp:799-853` |
| (implicit) diagnostic trace file | `DyeWatchFile(...)` → `Trinity_DyeWatch.txt`, timestamped `HH:MM:SS ` prefix | `dye.cpp:80-104` |

## 3.B Game-side function contracts (dye)

| # | Trinity role | Game function / address expression | How the address is obtained | Exact ABI Trinity assumes | Object operated on | Offsets read/written | Source file:line | Fail-safe if missing |
|---|---|---|---|---|---|---|---|---|
| D1 | H | **BatchEquip** `sub_7C98D0` (`kSig_EquipBatch`) | `mem::InstallHook("dye: equip-batch", kSig_EquipBatch, nullptr, &hkEquipBatch, &oEquipBatch, &g_equipTarget)`; fallback `kSig_EquipBatch_Legacy` | `void*(__fastcall*)(void* a1 /*comp*/, void* a2, void* a3, void* a4)` (typedef `dye.cpp:46`) | equip component (rcx) | R `comp` structurally via `CompValid` → `ReadEquipTable`; the hook *stores* the pointer in `g_comp` | install `dye.cpp:1568-1573`; detour `476-498` | Explicitly optional: *"Optional gear-change listener (dye apply/upsert works directly regardless)"* (`1567`). `mem::InstallHook` logs `LOG_ERR("dye: equip-batch signature NOT FOUND - .")` (empty consequence). Also used by `Inventory::IdentifyCharacterFromComp` → `LiveCharacterIdentity` (`inventory.cpp:3629`) |
| D2 | N | **Equip batch rebuild** `0x1403AAF40` — the D1 trampoline `oEquipBatch` | reused from D1 | `void*(__fastcall*)(void* comp, void* a2, void* a3, void* a4)`, called as `oEquipBatch(comp, nullptr, nullptr, nullptr)` after validating `comp+0x08 → actor → actor+0x08` | equip component | R `comp+0x08`, R `actor+0x08` | call `dye.cpp:649` (`TriggerEquipRefresh`), used at `1513` | `if (!oEquipBatch || comp < kMinPointer) return false;` (`637`) |
| D3 | N | **Dye ack applier** `sub_814BD0` (TU 1.17+), legacy `sub_7D9C50` | `mem::FindPattern(kSig_DyeApplyBatch)`, fallback `kSig_DyeApplyBatch_Legacy` | `int*(__fastcall*)(void* equipComponent, int* outErr, void* batch1960)` (typedef `dye.cpp:47`) | equip component + 1960-byte batch blob | reads `comp+0x80` internally; Trinity pre-validates the possessor chain `[comp+8] → +0xA0 → +0xD0 → +0x68 → +0x110` before calling | resolve `dye.cpp:1575-1579`; guard `598-620`; call `616` | `CallDyeApply` returns false if the chain breaks; the apply path then relies on upsert/visual leaves (`1427-1432`) |
| D4 | N | **Dye record upsert** `sub_1F8CB40` | `mem::FindPattern(kSig_DyeUpsert)`, fallback `kSig_DyeUpsert_Legacy` | `void*(__fastcall*)(void* itemVal, const void* record16)` (typedef `dye.cpp:48`) | any TrItemValue (equipped entry or inventory slot) | W the 16-byte record inside `itemVal+0x78` vector; `count < 12` gate is the engine's | resolve `dye.cpp:1581-1592`; call `622-630`; sites `885`, `914`, `935`, `950`, `963`, `1287`, `1306`, `1386`, `1411`, `1478`, `1506`, `1925` | `if (!g_dyeUpsert) { LOG_WARN("dye: visual test only - durable upsert unresolved; server entry untouched."); return false; }` (`858-862`) |
| D5 | N | **Per-slot equipped dye applier** `sub_847D24` (1.17+ verified) | `mem::FindPattern(kSig_DyeApplySlot)` | `void*(__fastcall*)(void* comp, uint16_t slotTag, const uint8_t rec[16], int channel)` (typedef `dye.cpp:68-69`) | equip component | writes the record into the entry's GPU material buffer | resolve `dye.cpp:1606`; call `661-670`; sites `1420`, `1951` | `LOG_WARN("dye: per-slot applier not found - companion dye falls back to render leaves.")` (`1611`). **On `revision >= 2625` this is force-nulled** (`1630-1636`) |
| D6 | N | **Visual SET leaf** `sub_8154A0` | `mem::FindPattern(kSig_DyeVisualSet)` | `void*(__fastcall*)(void* comp, void* entry, const void* rec, uint16_t tag, uint64_t stackCh, uint64_t stackZero)` (typedef `dye.cpp:52-54`) | equip component + entry | builds the material parameter block and pushes it into the item's GPU material instance | resolve `dye.cpp:1598`; call `675-686`; sites `1423`, `1952` | If any of the three visual leaves is missing: `LOG_WARN("dye: per-slot visual leaves not found - companion dye will be data-only.")` and all three are nulled (`1612-1626`). Force-nulled on `revision >= 2625` (`1630-1636`) |
| D7 | N | **Visual CLEAR leaf** `sub_817310` | `mem::FindPattern(kSig_DyeVisualClear)` | `void*(__fastcall*)(void* comp, void* entry, uint16_t tag, uint8_t channel, uint64_t stackZero)` (typedef `dye.cpp:55-57`) | equip component + entry | drops the rendered override for one channel | resolve `dye.cpp:1599`; call `688-698`; site `1409` | as D6 |
| D8 | N | **Data remove-by-channel** `sub_206EBF0` | `mem::FindPattern(kSig_DyeRecordRemove)` | `void(__fastcall*)(void* entry, uint8_t channel)` (typedef `dye.cpp:59`) | entry | shifts out the record whose `+6 == channel` | resolve `dye.cpp:1600`; call `700-709`; site `1410` | as D6 |
| D9 | R/W | Equip table descriptor (own copy of the walk) | read `comp+0x90` → `comp+0x80` → `comp+0x88` → probe `{0x50,0x38,0x40,0x48,0x60,0x70}` | n/a | equip component | R `desc+0x08` array, `desc+0x10` count; entry stride `0xD0`/tag `0xC8`/dyeData `0x78`/dyeCount `0x80` (legacy `0xC8`/`0xC0`/`0x70`/`0x78`) | `dye.cpp:154-222` (`ReadEquipTable`), `224-230` (`CompValid`) | `CompValid` false → `ClientComp()` returns 0 → `Dye::Ready()` false, apply refused |
| D10 | W | Item value dye vector | n/a | n/a | entry or inventory slot | W `entry+0x80` (`kOff_ItemVal_DyeCount`) ← 0 before re-writing a channel set (sites `882`, `911`, `932`, `1300`, `1472`, `1498`, `1502`) | `dye.cpp:882`, `911`, `932`, `1300`, `1472`, `1498`, `1502`; constant `offsets.h:1638` | Guarded writes |
| D11 | W | TLS realm flag around the durable write | `Inventory::RealmFlagAddress(&oldFlag)` | n/a | TLS byte | W 1 / restore last | `dye.cpp:864-871`, `1294-1310`, `1455-1484`, `1966-1968`, `2034` | `if (!flagAddr) { LOG_WARN("dye: realm flag unresolved - skipping the durable write."); return false; }` (`866-870`) |
| D12 | R | Item value dye records (read path) | n/a | n/a | entry | R `itemVal+0x78` data / `+0x80` count (modern), `+0x70`/`+0x78` (legacy); record stride 16, channel byte at `rec+6`, max 12 | `dye.cpp:526-564` (`ReadRecords`) | Returns mask 0 → treated as "no records" |
| D13 | R | Equipped entry lookup by slot tag | n/a | n/a | equip table | R `entry+tagOffset`, `entry+0x08` typeId, `entry+0x10` quantity | `dye.cpp:502-522` (`FindEntryByTag`) | returns 0 → `LOG_WARN("dye: no live equipped entry for slot tag %u.", req.tag)` (`1330`) |
| D14 | R | Inventory slot lookup by instance id | n/a | n/a | holder | R `holder+0x18` buckets, `holder+0x20` count, `bucket+0x00` slots, `bucket+0x08` size, `slot + i*GetSlotStride()`, `slot+0x00` instanceId | `dye.cpp:1095-1122` | returns 0 |
| D15 | R | Possessor-chain pre-validation for D3 | n/a | n/a | component → actor → possessor → pawn | R `comp+8`, `actor+0xA0`, `possessor+0xD0`, `pawn+0x68`, `sub+0x110` | `dye.cpp:601-612` | Any break → `CallDyeApply` returns false (crash guard) |
| D16 | R | Dyeable-prefab registry (baked) | `kDyeablePrefabHashes` from `dye_data.h` (generated by `scripts/gen_dye_data.py`) | n/a | Trinity's own sorted hash array | n/a | `dye.cpp:1029-1040` (`DyeRegistryHas`), used by `1042-1073` (`IconPrefabDyeable`) | Unknown icon name → `true` (item kept visible) |
| D17 | R | Zone-count lookup (baked) | `LookupExactZoneCount(hash)` in `dye_slots_table.h` (1105 entries, binary search) | n/a | Trinity's own sorted table | n/a | `dye.cpp:118-143` (`LookupExactZoneForIcon`) | returns 0 → "use fallback" |

> Note on D3's ABI drift: `offsets.h:1542-1546` documents the applier as `int* f(void* equipComponent, int* outErr, void* batch1960)`, while the actual call site (`dye.cpp:616`) passes `(comp, outErr, batch)`. These agree; the typedef at `dye.cpp:47` is `(void*, int*, void*)`.

## 3.C Pointer chains / object resolution (dye)

| # | Chain | Yields | Source file:line |
|---|---|---|---|
| DC1 | `actor` → `+0x68` (`kOff_Container_Sub`) → `+0x38` (`kOff_Sub_EquipComp`) = comp, validated by `CompValid` (DC2) | equip component | `dye.cpp:277-290` (`CompForCharacter`), `236-241` |
| DC2 | `comp` → `+0x90`/`+0x80`/`+0x88` (or `{0x50,0x38,0x40,0x48,0x60,0x70}`) = desc → `+0x08` array, `+0x10` count; count must be `1..64` | equip entry table | `dye.cpp:154-222`, `224-230` |
| DC3 | `array + i*stride` → `+0xC8` tag (legacy `+0xC0`), `+0x08` typeId, `+0x10` quantity, `+0x00` instanceId | equipped entry | `dye.cpp:502-522`, `1203-1234` |
| DC4 | `itemVal` → `+0x78` data / `+0x80` u32 count (legacy `+0x70`/`+0x78`) → `+ ch*16` records; channel at `rec+6` | dye record array | `dye.cpp:526-564`; `offsets.h:1637-1639` |
| DC5 | `itemVal` → `+0x78` data pointer, `+0x80` u32 count, capacity `+0x84` | dye vector | `offsets.h:1637-1638` |
| DC6 | `comp` → `+0x08` (`kOff_EquipComp_Owner`) = owning actor — used as a back-reference test (`hookedOwner == liveChar`) | ownership proof | `dye.cpp:339-341`, `1361`; `offsets.h:1522` |
| DC7 | `actor` → `+0x68` → `+0xB8` (inventory holder path, shared with inventory.cpp) | holder | `inventory.cpp:1180-1187` |
| DC8 | `actor` → alternate sub-objects `{0x60,0x68,0x70,0x58,0x78,0x80,0x88,0x90,0x98,0xA0}` → component offsets `{0x38,0x30,0x40,0x28,0x48,0x50,0x58,0x60,0x68}` | component fallback | `dye.cpp:245-257` |
| DC9 | `actor` → direct component offsets `{0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80,0x88,0x90,0x98,0xA0,0x168}` | component fallback | `dye.cpp:259-265` |
| DC10 | `Inventory::ClientCharacterAddr()` (`inventory.cpp:3388`) → `CompForCharacter` | live client component (walk **leads** over the hook capture, because a server-realm capture "has no controller and no render state") | `dye.cpp:325-330` |
| DC11 | `g_comp` (equip-batch hook capture) → `ReadPtr(comp+0x08)` must equal the live char (or live char unknown) → `IdentifyCharacterFromComp` must be `< 0` or the target | fallback live component | `dye.cpp:335-346` |
| DC12 | `Player::GetActor(liveIdx)` → `CompForCharacter` | fallback for the live index | `dye.cpp:349-357` |
| DC13 | `Inventory::CharacterAddr(targetIdx)` → `CompForCharacter` | off-screen selection | `dye.cpp:362-367` |
| DC14 | `Player::GetActor(targetIdx)` → `CompForCharacter` | off-screen fallback | `dye.cpp:370-375` |
| DC15 | `Inventory::ServerCharacterAddr()` → `CompForCharacter` | server component (`ServerComp`, `dye.cpp:384-422`) | `dye.cpp:391-392` |
| DC16 | `Inventory::CharacterAddrs(idx, copies, 16)` (`inventory.cpp:3648-3700`) → `CompForCharacter(act)` for each | every realm copy of the selected character | `dye.cpp:898-917`, `1459-1482` |
| DC17 | `Inventory::ClientHolderAddr()` → `FindSlotByInstance` (D14) | bag-item entry (Mode 2) | `dye.cpp:1275`, `1721` |
| DC18 | `Inventory::ServerHolderAddr()` → `FindSlotByInstance` | durable bag-item entry | `dye.cpp:1297` |
| DC19 | `Inventory::FindAndApplyAllHolders(instId, cb, ud)` | every holder copy of the item | `dye.cpp:1493-1509` |
| DC20 | `Inventory::RealmFlagAddress(&oldFlag)` | realm flag | `dye.cpp:865`, `1294`, `1455`, `1966` |

## 3.D Hook table and Patch table (dye)

### Hooks

| # | Install log context | Locator | Original bytes | Patched bytes | Callback signature | Apply condition | Restore |
|---|---|---|---|---|---|---|---|
| DH1 | `"dye: equip-batch"` | `kSig_EquipBatch` | *not recorded in source* — MinHook-generated detour; Trinity keeps no original-byte copy | MinHook-generated `jmp`; bytes are not literals in Trinity source | `void* __fastcall(void* a1, void* a2, void* a3, void* a4)` (`dye.cpp:476`) | Always attempted | `mem::RemoveHook(&g_equipTarget)` (`dye.cpp:1643`); trampoline nulled at `1644` |
| DH2 | `"dye: equip-batch legacy"` | `kSig_EquipBatch_Legacy` | as DH1 | as DH1 | as DH1 | Only if DH1 fails | same as DH1 |

### Byte patches

**None.** `dye.cpp` contains no code-byte patching.

## 3.E Fail-closed / version-gated behaviour (dye)

| Condition (source file:line) | Effect | Log message |
|---|---|---|
| `if (!mem::InstallHook("dye: equip-batch", …)) { mem::InstallHook("dye: equip-batch legacy", …); }` — `dye.cpp:1568-1573` | Gear-change listener absent; dye apply/upsert still works (the walk is the primary path) | `mem::InstallHook` emits `LOG_ERR("dye: equip-batch signature NOT FOUND - .")` (consequence is `nullptr`, so the second `%s` is empty) |
| `if (!upsert) LOG_WARN(...);` — `dye.cpp:1585-1592` | Dye applies visually but is **not durable** | `"dye: upsert signature not found - dye will apply but not persist."` |
| `if (g_dyeApplySlot) LOG(...) else LOG_WARN(...)` — `dye.cpp:1607-1611` | Companion dye falls back to the render leaves | `"dye: per-slot applier not found - companion dye falls back to render leaves."` |
| `if (g_dyeVisualSet && g_dyeVisualClear && g_dyeRecRemove) LOG(...) else { LOG_WARN(...); g_dyeVisualSet = nullptr; g_dyeVisualClear = nullptr; g_dyeRecRemove = nullptr; }` — `dye.cpp:1612-1626` | All three leaves are dropped together (all-or-nothing) | `"dye: per-slot visual leaves not found - companion dye will be data-only."` |
| `if (core::GetGameVersion().revision >= 2625) { g_dyeApplySlot = nullptr; g_dyeVisualSet = nullptr; g_dyeVisualClear = nullptr; g_dyeRecRemove = nullptr; }` — `dye.cpp:1628-1636` | **TU 2.00+ (PE ≥ 2625) deliberately disables the TU-1.18 leaf hooks**; only `DyeApplyBatch` + `DyeUpsert` are used — comment: *"avoid calling outdated TU 1.18 leaf hooks"* | none (silent) |
| `if (!g_dyeUpsert) { LOG_WARN(...); return false; }` — `dye.cpp:858-862` | Server mirror refused; the client still renders | `"dye: visual test only - durable upsert unresolved; server entry untouched."` |
| `if (!flagAddr) { LOG_WARN(...); return false; }` — `dye.cpp:866-870` | The whole durable write is skipped | `"dye: realm flag unresolved - skipping the durable write."` |
| `if (!g_dyeApply && !hasLocalController) …` and `if (!applyOk && !upsertOk && !visualOk)` — `dye.cpp:1374-1377`, `1427-1432` | Apply is reported as `Failed` only when **all three** paths failed | `"dye: applier refused (err=%d, slot tag %u)."` |
| `if (!comp) { LOG_WARN(...); state = Failed; return; }` — `dye.cpp:1319-1325` | No equip component → refuse | `"dye: apply refused - equip component not resolved."` |
| `if (!entry) { LOG_WARN(...); state = Failed; return; }` — `dye.cpp:1327-1333` | No equipped entry for the tag → refuse | `"dye: no live equipped entry for slot tag %u."` |
| `if (instId <= 0) { LOG_WARN(...); state = Failed; return; }` (Mode 2) — `dye.cpp:1268-1273` | Bag-item dye refuses without a live instance id | `"dye: inventory item instance not found for tag %u."` |
| `if (tag == 14 || tag == 22 || tag == 23 || tag == 24 || tag == 25) continue;` — `dye.cpp:1222-1223` | Mount/vehicle gear is excluded from the player-mode dye list | none |
| `if (IsDummyOrUnarmed(tid, itemName)) continue;` — `dye.cpp:1218`, `1165` (helper `1075-1087`) | Placeholder gear excluded | none |
| `if (!IconPrefabDyeable(icon)) continue;` — `dye.cpp:1167` (helper `1042-1073`) | Bag-item dye (Mode 2) lists **only** prefabs present in the baked `partprefabdyeslotinfo` registry. **Explicitly fail-open on an unparseable icon name**: `if (!icon || !icon[0]) return true;` and `if (!p || !p[0]) return true;` (`1047`, `1057`) | none |
| `if (!Player::Ready()) return;` — `dye.cpp:1779` | `Dye::Tick` does nothing before the world loads | none |
| `if (!comp) return;` — `dye.cpp:1193` / `1692` / `1729` | Snapshot / channel read / hook-capture validation fail closed to 0 | none |
| `if (ReadPtr(comp + 8, &actor) … ) return false;` chain — `dye.cpp:603-612` | `CallDyeApply` refuses to call the applier unless the whole render chain resolves | none |
| `__except (EXCEPTION_EXECUTE_HANDLER) { return false; }` — `dye.cpp:619`, `629`, `652-655`, `669`, `685`, `697`, `708` | Every engine call is fault-isolated | none |
| `if (now - s_lastRestore > 2500)` — `dye.cpp:1788` | Auto-restore runs at most every 2.5 s | none |
| `if (!needsVisual && liveDyeCount > 0 && s_lastEquipChangeMs != 0 && GetTickCount64() - s_lastEquipChangeMs < 3000) needsVisual = true;` — `dye.cpp:1912-1917` | The forced visual replay is bounded to a 3 s window after the last equip-batch capture | none |
| `if (c == 0 && c == Inventory::ActivePlayerCharacterIdx())` guards in `InjectAllToSave` — `dye.cpp:1982`, `1987` | The server component and the routed live component are only read as "Kliff's" when Kliff is actually the live selection | none |
| `if (strcmp(hdr.magic, "TRDYE03") == 0)` — `dye.cpp:843` | A dye cache with an unknown magic is silently ignored | none |

## 3.F Runtime validation hooks — exact Trinity log lines (dye)

Trinity `LOG`/`LOG_WARN` lines:

* `"dye: batch apply @ %p, durable upsert @ %p."` — `dye.cpp:1589-1590`
* `"dye: per-slot applier @ %p (companion-safe universal apply)."` — `dye.cpp:1608-1609`
* `"dye: per-slot visual set @ %p, clear @ %p, record remove @ %p (companion-safe live apply)."` — `dye.cpp:1614-1618`
* `"dye: upsert signature not found - dye will apply but not persist."` — `dye.cpp:1586` (`LOG_WARN`)
* `"dye: per-slot applier not found - companion dye falls back to render leaves."` — `dye.cpp:1611` (`LOG_WARN`)
* `"dye: per-slot visual leaves not found - companion dye will be data-only."` — `dye.cpp:1622` (`LOG_WARN`)
* `"dye: visual test only - durable upsert unresolved; server entry untouched."` — `dye.cpp:860` (`LOG_WARN`)
* `"dye: realm flag unresolved - skipping the durable write."` — `dye.cpp:868` (`LOG_WARN`)
* `"dye: inventory item instance not found for tag %u."` — `dye.cpp:1270` (`LOG_WARN`)
* `"dye: apply refused - equip component not resolved."` — `dye.cpp:1322` (`LOG_WARN`)
* `"dye: no live equipped entry for slot tag %u."` — `dye.cpp:1330` (`LOG_WARN`)
* `"dye: applier refused (err=%d, slot tag %u)."` — `dye.cpp:1429` (`LOG_WARN`)
* `"dye: [%s] Slot [%s (Tag %u)] -> Cleared dye color."` — `dye.cpp:1548-1549`
* `"dye: [%s] Slot [%s (Tag %u)] Channel %d -> Applied RGB=(%u,%u,%u) Material=0x%04X."` — `dye.cpp:1553-1555`

Separate diagnostic channel (appended to `Trinity_DyeWatch.txt`, not the main log), from `dye.cpp:80-104`:

* `"chain realm=%s actor=%p subOk=%u sub=%p legacyOff=0x%llX compOk=%u comp=%p ownerOk=%u owner=%p valid=%u hooked=%p"` — `dye.cpp:439-441`
* `"candidate realm=%s subOff=0x%llX comp=%p owner=%p tableOff=0x%llX desc=%p array=%p count=%u"` — `dye.cpp:462-465`
* `"candidate realm=%s subOff=0x%llX comp=%p owner=%p table=not-found"` — `dye.cpp:470-472`
* `"ProcessRequest: player tag=%u comp=%p err=%d applyOk=%d upsertOk=%d visualOk=%d"` — `dye.cpp:1434-1436`

## 3.G Item ID / catalog tables (dye)

* **Dye record format (16 bytes)** — canonical map at `offsets.h:1442-1456`; Trinity's writer is `BuildSetRecord` (`dye.cpp:570-584`) and its clearer is `BuildClearRecord` (`dye.cpp:589-595`):
  * `+0` u32 colour-group key (`dyecolorgroupinfo._key`)
  * `+4` u16 material template `1..10` into `partprefabdyetexturepalleteinfo`; `0xFFFF` = natural
  * `+6` u8 channel (`"mod"`) `0..11` — which colorable mesh zone. Records are keyed by this byte; a value with the high bit set (`0xFF`) means "skip this record" (`offsets.h:1475-1477`)
  * `+7` u8 r, `+8` u8 g, `+9` u8 b, `+10` `0xFF`
  * `+11` u8 repair/weathering: `0` pristine … `0x7F` weathered; `0xFF` legacy "no override"
  * `+13` u8 `0x04` on channels 0 and 3 in natural records (Trinity mirrors it — `dye.cpp:582-583`)
  * `+12`, `+14`, `+15` are left zero by Trinity.
  * `BuildSetRecord` rewrites `materialId == 0xFFFF` to `0x0001` (`dye.cpp:574`) — i.e. a "natural" request is stored as material 1, not `0xFFFF`.
  * `BuildClearRecord` shape: `+4/+5 = 0xFFFF`, `+6 = channel`, `+11 = 0xFF`, everything else zero.
* **Channel count**: `kDye_MaxChannels = 12` (`offsets.h:1639`). `channel == -1` means "all 12" (`dye.cpp:1276-1277`, `1350-1351`).
* **Batch blob geometry** (`DyeApplyBatch`): `kDyeBatch_Blocks = 10`, `kDyeBatch_BlockSize = 196`, `kDyeBatch_RecordsOff = 4`, `kDyeBatch_Size = 10*196 = 1960` (`offsets.h:1734-1737`; builder `dye.cpp:1339-1357`). Block 0 carries the target slot tag; the other nine are disabled with tag `0xFFFF` (`dye.cpp:1341-1348`).
* **Colour families (baked)**: `dye_data.h` — `kDyeFamilyCount = 10`, `kDyeShadeCount = 109`, `kDyeNeutrals = 9`, `kDyeGridCols = kDyeGridRows = 10`. `struct DyeFamily { uint32_t key; const char* name; const char* stringKey; DyeShadeRGB shades[109]; }`. Example row: `{ 0xC88211F5u, "Red", "Her_Color_Group_I", { {255,255,255}, … } }`. Generated from the game's `dyecolorgroupinfo.pabgb` by `scripts/gen_dye_data.py`.
* **Dyeable-prefab registry (baked)**: `kDyeablePrefabCount` + sorted `kDyeablePrefabHashes[]` in `dye_data.h`, from `partprefabdyeslotinfo.pabgb`; queried by FNV-1a-32 over the lowercased prefab name (`HashPrefabLower`, `dye.cpp:106-116`) with up to 4 trailing `_token` strips (`dye.cpp:1062-1071`).
* **Zone count table (baked)**: `dye_slots_table.h` — `kDyeZoneTableCount = 1105`, `kDyeZoneTable[1105]` of `{ uint32_t hash; uint8_t zones; }`, binary-searched by `LookupExactZoneCount(uint32_t)` (no match → 0 → "use fallback"). Generated by `scripts/gen_dye_slots.py`.
* Note: the runtime snapshot hardcodes `const int maxZones = 12;` for both player mode (`dye.cpp:1225`) and bag mode (`1170`) rather than consuming `LookupExactZoneForIcon` — that helper is defined and used elsewhere but is **not called from the two snapshot loops**. Stated as observed, not as intent.
* **Hardcoded item-id list**: the same character-identity sets as §1.G, reached through `Inventory::IdentifyCharacterFromComp` (`inventory.cpp:3500-3513`) for the per-character routing in `ClientComp`/`ServerComp` (`dye.cpp:343-344`, `405`, `417`).
* **On-disk cache**: `DyeCacheHeader` with magic `"TRDYE03"` (`dye.cpp:813-818`, `843`); arrays `s_savedPlayerSlots[3][32]` (`dye.cpp:745`) and a per-item map of up to `kMaxSavedItemDyes = 512` `SavedItemDyeRecord`s (`dye.cpp:757-758`).

---

# 4. Cross-area totals and uncertainties

## 4.1 Count of game-side touch points

Counting every distinct **(role, engine entry point / address expression)** row from §B and §D, plus the shared plumbing rows that the three areas actually use:

| Area | Hooks | Native calls | Address locators / resolvers | Memory read/write sites (distinct objects) | Total rows |
|---|---|---|---|---|---|
| Inventory (§1.B rows 1–39) | 12 (rows 1–14, of which 3 are the hardcoded-RVA money hooks) | 9 (rows 7, 10, 15–22) | 9 (rows 23–30, plus the lazy re-resolvers inside them) | 10 (rows 31–39) — counting `WantedInfo` rows, bucket fields, slots, `ItemInfo` defs, `InventoryInfo` defs, ID allocator, TLS flag | **39** |
| Equipment (§2.B rows E1–E14) | **0** | 2 (E1, E2) | 1 (E3, the equip-table probe with 3 named + 8 probed descriptor offsets) | 11 (E4–E14, counting the entry fields, owner backref, realm flag, holder mirror, catalog category) | **14** |
| Dye (§3.B rows D1–D17) + §3.D | 2 (DH1, DH2) | 7 (D2–D8) | 2 (D9 equip-table probe; D12 record vector) | 8 (D10, D11, D13, D14, D15, D16, D17 + snapshot) | **19** |
| Shared plumbing genuinely consumed by all three | — | — | — | — | (listed in §0; not double-counted) |
| **Total distinct game-side touch points** | **14** | **18** | **12** | **29** | **72** |

Summarised: **14 hooks (12 inventory + 2 dye + 0 equipment)**, **18 native-call sites into the game executable**, and **40 distinct memory read/write object contracts**. **Zero byte patches.**

The 18 native calls, explicitly:

1. `kSig_InvGetHolder` raw address (called from `hkGetItemQty`, `NoteContainer`, `ServerHolder`) — `inventory.cpp:2113`
2. `kSig_TrItemValueCtor` (+ legacy fallbacks) — `inventory.cpp:2133/2145`
3. `oHolderInsert` trampoline used as the insert planner — `inventory.cpp:3892`
4. `kSig_InvCommitPlacement201` — `inventory.cpp:2166`
5. `kSig_InvCommitPlacement` — `inventory.cpp:2168`
6. `kSig_InvFreePlacements201` — `inventory.cpp:2170`
7. `kSig_InvFreePlacements` — `inventory.cpp:2170`
8. `kSig_TrItemValueDtor` — `inventory.cpp:2171` (never resolves; empty pattern)
9. `kSig_InvSetExpandSlots` call-only fallback — `inventory.cpp:2229`
10. `ntdll!NtQueryInformationThread` — `inventory.cpp:2177`
11. `kSig_EquipEffectRefresh` — `equipment.cpp:1566`
12. `kSig_EquipEffectRefresh_Legacy` — `equipment.cpp:1568`
13. literal `"48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00"` (ResizeSocketVector) — `equipment.cpp:1574`
14. `kSig_DyeApplyBatch` / `_Legacy` — `dye.cpp:1575-1579`
15. `kSig_DyeUpsert` / `_Legacy` — `dye.cpp:1581-1592`
16. `kSig_DyeVisualSet` — `dye.cpp:1598`
17. `kSig_DyeVisualClear` — `dye.cpp:1599`
18. `kSig_DyeRecordRemove` — `dye.cpp:1600`
19. `kSig_DyeApplySlot` — `dye.cpp:1606`
20. `oEquipBatch` trampoline as the equip-batch rebuild — `dye.cpp:649`

(That enumeration lists 20 address expressions but only **18 distinct game functions**, because `oEquipBatch`/`oHolderInsert` are trampolines of already-counted hooks and `kSig_InvFreePlacements`/`_201` are the same logical function on two ABIs. The table count of 18 is the "distinct game functions called" figure.)

## 4.2 Uncertainties / things referenced but not defined

1. **`kSig_TrItemValueDtor` is an empty string** — `offsets.h:942`: `inline constexpr const char* kSig_TrItemValueDtor = "";`. `mem::FindPattern("")` cannot match, so `oItemValueDtor` (declared `inventory.cpp:98`, resolved `2161`/`2171`, called `3932`) is always null and the destructor is never run. Source comment at `inventory.cpp:3930` says *"Freed in the target realm, exactly once each, whatever happened"* — the dtor half of that is currently dead. Reported as-is; no inference about intent.
2. **`ResolveItemDisplayName` (`item_names.h:8`) *is* consumed inside this area** — at `inventory.cpp:997` (inside `Prettify`, see §1.G) and, from the UI, at `menu.cpp:2437`. It is **not** referenced from `equipment.cpp` or `dye.cpp`, which reach names through `Inventory::NameForTypeId`. An earlier draft of this report wrongly stated it had no callers; the grep result is 4 matches total (2 call sites + declaration + definition).
3. **`Equipment::SaveEquipProfilesToDisk` / `LoadEquipProfilesFromDisk` / `SavePlayerEquipSlot` / `ClearPlayerEquipSlot` / `HasCustomProfile` are declared in `equipment.h:146-150` as public members and defined at `equipment.cpp:1983-2019`, where they delegate to file-local `trinity::game::SaveEquipProfilesToDisk()` etc. (`equipment.cpp:898`, `942`) living in an **anonymous namespace**. The free functions are not declared in any header seen. This is legal (the member definitions are in the same TU) but means the free functions have no external linkage — worth noting if another TU ever needs them.
4. **`Equipment::Install()` returns `true` unconditionally** (`equipment.cpp:1581`) even when both signatures miss. Only `Equipment::Ready()` (component resolvable) gates the menu.
5. **`HookRemoval` asymmetry in inventory:** `Inventory::Remove()` (`inventory.cpp:2328-2349`) removes 8 hook targets but **not** the three legacy money hooks installed by raw `MH_CreateHook` (`oGetMoney1/2/3`, `inventory.cpp:2098-2100`). Stated as observed.
6. **`Inventory::AddCampCurrency` appears to be dead code, and its keys are inconsistent with the spoof it feeds.** A grep for `AddCampCurrency` across the tree returns exactly two matches: the declaration (`inventory.h:209`) and the definition (`inventory.cpp:4838`) — **no call site exists in `menu.cpp` or anywhere else**. Additionally, `AddCampCurrency` adds to `"Money_Camp_Wood"` (`inventory.cpp:4846`) while `hkGetItemQty`'s camp-currency spoof recognises `"Money_Camp_Timber"` (`inventory.cpp:1526`). Both keys appear; whether the game defines both was not verified.
7. **`BackgroundCurrencyScan` is entirely inert.** Every body path is either an `__try` header or the comment `"Heap Scanner disabled: Removed unsafe WeMod memory scanning that caused memory corruption."` (`inventory.cpp:4542`). It is defined at `inventory.cpp:4513` and, as far as the greps show, never called. Declared `static` in a non-anonymous region, so it is TU-local.
8. **`ReadEquipTable`'s legacy branch and `ReadRecords` use different offsets for the same fields.** `ReadEquipTable` reports legacy `dyeData = 0x70`, `dyeCount = 0x78` (`dye.cpp:200-201`), and `ReadRecords` independently hardcodes the same pair (`dye.cpp:539-540`) — consistent, but the two are not wired together (the snapshot uses the `ReadEquipTable` outputs at `1233`, while `ReadRecords` re-derives its own). Also `ReadEquipTable`'s "alternate offsets" branch (`dye.cpp:205-220`) hardcodes `0xD0/0xC8/0x78/0x80` regardless of which descriptor offset matched, unlike the three named branches.
9. **`DyeWatchFile` output path**: the file name literal `"Trinity_DyeWatch.txt"` is at `dye.cpp:91`; the **exact filename inside `GetDyeCachePath` (`dye.cpp:799`) was not read** — only that it is derived from the module directory and validated by the magic `"TRDYE03"`.
10. **`Equipment::SlotInfo::attack/defense/reinforceExp/reinforceBonus` are Trinity-computed estimates, not game reads.** `equipment.cpp:1240-1247` derives base attack/defense from the tag and refine level, then adds per-socket buffs by substring (`1281-1287`), and hardcodes `s.reinforceExp = 72;` (`1244`). These are not read from the engine.
11. **`Dye::Tick`'s auto-restore calls `CallDyeApplySlot`/`CallDyeVisualSet` only** — deliberately **not** `DyeApplyBatch`, because that would pop the game's own "Item dyed successfully" toast every pass (`dye.cpp:1941-1945`).
12. **`kDye_MaxChannels` is 12 everywhere, but the snapshot unconditionally sets `maxZones = 12`** (`dye.cpp:1170`, `1225`) rather than consulting `LookupExactZoneForIcon` (`dye.cpp:118-143`). Whether the real per-item zone count is used anywhere in the dye UI was not traced into `menu.cpp`.
13. **No byte patches exist in these three files.** The "Patch table" sections are therefore empty by construction, not by omission. `grep` for `InstallPatch|ApplyPatch|PatchBytes|WriteCode|NopBytes|kPatch_` across `src/` returns matches only in `src/game/worker.cpp` (read-only byte comparison), outside the three areas.
14. **Run-time crash reports referenced in comments** (Error `298648703` / `0x11CD047F`, `0xC0000005`) are quoted from source comments, not independently verified.
