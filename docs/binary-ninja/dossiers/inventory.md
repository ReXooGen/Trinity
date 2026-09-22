# Inventory, items and money — feature dossier

Trinity scope: the **INVENTORY** tab (`src/gui/menu.cpp`) — Add Item, the Item
Editor, storage/category browsers, the Abyss item tools, the Restore Items
categories, the catalog archive, and the whole Money & Currency subtree.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, FileVersion `1.0.0.2944`.
Binary Ninja database `CrimsonDesert.exe.bndb`, view `view_1` (PE).
See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**. No breakpoints, no debugger, no injection,
no memory writes, no game launch.

---

## 1. The two contracts that govern everything here

### 1.1 The realm system — every mutation is written twice

Crimson Desert keeps a **client mirror** and a **server authority** copy of
inventory state. Any write Trinity makes must be applied to **both**, with the
process-local realm flag flipped around the server write:

```text
Inventory::RealmFlagAddress()   (src/game/inventory.cpp:1281)
   revision-dependent flag offset   (src/core/version_mapping.cpp:77-90)
       PE 2850 / PE 2944 -> 0x1EC
       PE 2760           -> 0x1FD
       otherwise         -> 0x1F2

write path:  client copy  ->  flip realm flag  ->  server copy  ->  flip back
```

The TEB / TLS reads used to find that flag **deliberately bypass
`kMinPointer` (0x10000000)** — TLS addresses live far below that floor, so the
normal `mem::IsValidUserPtr` guard would reject them. Do not "fix" those reads
by adding the guard back.

### 1.2 Add Item is strictly fail-closed

`CanCommitAuthoritativeAdd` (`src/game/inventory_logic.cpp:5-10`) requires
**all** of:

- the item-value constructor resolved,
- the holder-insert planner resolved,
- the transaction commit resolved,
- the placement-free resolved,
- `clientHolder != 0` **and** `serverHolder != 0` **and**
  `serverHolder != clientHolder`.

Trinity therefore **never creates a client-only item**. If the server holder is
unknown the add is refused rather than half-applied.

The server holder is only learnable from the **transaction-COMMIT hook**
(`kSig_InvCommit`), because loading a save commits the server containers
**before** the client container exists. For that reason the capture list is
deliberately **unfiltered** at capture time — filtering it loses the load-time
capture. This is a subtle, load-bearing design decision: do not "clean up" the
unfiltered capture during an update.

---

## 2. Function dossiers

### Inventory core anchors

| Function | RVA / VA | Locator | Matches | Prototype | Trinity role |
|---|---|---|---|---|---|
| item-value constructor | `0x2409910` / `0x142409910` | `kSig_TrItemValueCtor` (34 tok) | **1** | `int64_t*(int64_t*, int16_t*, int64_t)` | native call |
| holder-insert planner | `0x2407770` / `0x142407770` | `kSig_InvHolderInsert2944` (46 tok) | **1** | `int32_t*(int64_t*, int32_t*, int64_t*, int64_t*, int16_t, int64_t*, char, char, char)` | hook (trampoline) |
| transaction commit | `0x212E160` / `0x14212E160` | `kSig_InvCommit` (47 tok) | **1** | `int32_t*(void*, int32_t*, int64_t*, int16_t, int64_t*, char, char, char)` — **8-arg ABI** | hook |
| placement commit | `0x212DDF0` / `0x14212DDF0` | `kSig_InvCommitPlacement201` (40 tok) | **1** | `int32_t*(void*, int32_t*, int64_t*, int16_t)` | native call |
| placement free (modern) | `0x4880C0` / `0x1404880C0` | `kSig_InvFreePlacements` (45 tok) | **1** | `void*(int64_t*)` | native call |
| placement free (2.01) | `0x89FF210` / `0x1489FF210` | `kSig_InvFreePlacements201` (41 tok) | **1** | — | native call |
| holder resolver | `0x212A0F0` / `0x14212A0F0` | `kSig_InvGetHolder` (21 tok) | **1** | `int64_t(void* arg1)` | hook |
| item quantity read | `0x17F8E70` / `0x1417F8E70` | `kSig_InvGetItemQty` (14 tok) | **1** | `void*(void*, int16_t)` | hook |
| localized string get | `0x12C0240` / `0x1412C0240` | `kSig_LocStringGet` (28 tok) | **1** | — | native call (item names) |

- Confidence: **confirmed** for every locator→address binding above (each exact
  unique over all 12 PE sections). The **ABIs** are the Binary Ninja prototypes;
  they were **not** re-derived by decompilation in this pass, so treat the
  register-level contract as *strong candidate* until a runtime test confirms.
- Trinity consumers: `src/game/inventory.cpp` — install and hooks `:2062-2326`;
  holder resolution `:1163-1466`; server-holder discovery `:1369-1443`;
  `hkGetItemQty` `:1476`; `hkGetHolder` `:1613`; `hkCommit201` `:1665`;
  `hkHolderInsert` `:1685`; `hkSetExpandSlots` `:1737`; realm flag `:1281`;
  the add-item native path `:3838-4153`; the catalog `:4296-4460`.
- Role summary: **MinHook detours + guarded heap reads/writes. There are zero
  byte patches in this area** — Trinity never writes game code bytes for
  inventory.

### Two locators, one address — record it

`kSig_InvHolderInsert` (46 tokens) and `kSig_InvHolderInsert2944` (46 tokens)
**both match exactly once, and at the same VA `0x142407770`**. As with
`kSig_LocoStepper` vs `kSig_LocoStepper_PE2944` (see `locomotion.md` §3), the
"PE 2944 variant exists so a later build fails closed" intent is **not backed by
different bytes** on this build. The real discrimination on PE 2944 comes from
`kSig_InvHolderInsert201` and `kSig_InvHolderInsert_Legacy`, which both measure
**0**. Record this so a future update does not assume the two constants differ.

### Absent locators — fail-closed by construction

| Locator | Matches | Consequence |
|---|---|---|
| `kSig_InvCoreGlobal` | **0** | inventory-core global must be resolved another way (`ResolveRipAt` path) |
| `kSig_InvCoreGlobal_Pre201` | **0** | no legacy fallback available |
| `kSig_InvSetExpandSlots` | **0** | the 5-arg expansion setter is **gone** on TU 2.01+, and Trinity does **not** install it on PE 2944 (replaced by a per-tick continuous guard) |
| `kSig_InvGetItemQty_Legacy` | **0** | modern quantity read required |
| `kSig_InvCommitPlacement` | **0** | only the `…201` form resolves |
| `kSig_TrItemValueCtor_Pre201` | **0** | modern constructor required |
| `kSig_TrItemValueDtor` | **0 (empty pattern)** | see below |
| `kSig_LocStringGet_Alt1` / `_Alt2` / `_Legacy` | **0** | only the primary localisation getter resolves |

**`kSig_TrItemValueDtor` is the empty string** (`src/game/offsets.h:942`).
`mem::FindPattern` returns `0` for an empty pattern *by construction*
(`src/mem/scanner.cpp:156-157`), so it is a **silent no-op, not an error**. The
item-value destructor reference at `src/game/inventory.cpp:3932` is therefore
**dead code**. This should be fixed in `offsets.h` (delete the constant or give
it real bytes); until then, no future update should expect a destructor to
resolve.

---

## 3. Hooks and gating

### 3.1 Hook inventory

| # | Hook | Locator / address | Gate |
|---|---|---|---|
| 1 | item quantity read | `kSig_InvGetItemQty` → `0x1417F8E70` | — |
| 2 | holder resolver | `kSig_InvGetHolder` → `0x14212A0F0` | — |
| 3 | transaction commit | `kSig_InvCommit` → `0x14212E160` | — |
| 4 | holder insert | `kSig_InvHolderInsert*` → `0x142407770` | — |
| 5 | expansion setter | `kSig_InvSetExpandSlots` | **not installed on PE 2944** (0 matches; TU 2.01 path removed it) |
| 6–8 | legacy money getters | `gameBase + 0x16077B0 / +0x16078C0 / +0x16081D0` (raw RVAs) | `revision < 2625` → **not created on PE 2944** |

**Defect to fix before those legacy paths ever matter again.** The three legacy
money getters are created with raw `MH_CreateHook` (`inventory.cpp:2098-2100`)
but **never enabled**: the only `MH_EnableHook(MH_ALL_HOOKS)` lives in
`InstallDX12Hooks` (`src/hooks/dx12_hook.cpp:1570`), which `mod.cpp:103` calls
**before** `Inventory::Install` at `mod.cpp:131`, and no later enable or queued
apply exists anywhere in `src/`. They are also **not removed** in
`Inventory::Remove()`. See also `crime-money.md` §3.2.

### 3.2 Revision gating (pervasive on this build)

| Behaviour | Gate | Effect on PE 2944 |
|---|---|---|
| legacy money hooks | `revision < 2625` | skipped |
| money max-stack pin | `revision < 2625` | skipped |
| 5-arg expansion setter | TU 2.01 removed it | skipped, replaced by a per-tick continuous guard |
| transaction commit ABI | TU 2.01 uses an **8-arg** commit | the `kSig_InvCommit` (47-token) form is the one that matches |
| holder-insert AOB | PE 2944 needs its own | satisfied, but see §2 "two locators, one address" |
| four hardcoded table globals (`+0x6350EE8` WantedInfo, `+0x63307A8` tribeinfo, `+0x634DDB8`/`+0x634DDD0` category group/info) | `revision >= 2625` | **active** — these are raw RVAs, the most fragile addresses in this dossier |

---

## 4. Item IDs, the catalog, and item names

- Item IDs are **`int16`** throughout: the quantity read is
  `void*(void*, int16_t)` and the constructor takes an `int16_t*` id.
- `kItemMap` in `src/game/item_names.cpp` holds **6,258 rows** mapping item id
  to display name. `ResolveItemDisplayName` **is** consumed — `inventory.cpp:997`
  inside `Prettify`, and `menu.cpp:2437`.
- The catalog browse list is built in `src/game/inventory.cpp:4296-4460`.
- `kSig_LocStringGet` (`0x1412C0240`) is the game's localised-string getter used
  for item names. The `_Alt1` / `_Alt2` / `_Legacy` variants all measure **0**,
  so the localisation contract has no fallback on this build.

---

## 5. Locator evidence (measured over the whole image)

Method: `_analysis/aob_scan2.ps1` reproduces Trinity's scanner against the
on-disk image, all 12 sections.

| Locator | Length | Matches | VA | Result |
|---|---|---|---|---|
| `kSig_TrItemValueCtor` | 34 tok | **1** | `0x142409910` | exact unique |
| `kSig_InvHolderInsert` | 46 tok | **1** | `0x142407770` | exact unique |
| `kSig_InvHolderInsert2944` | 46 tok | **1** | `0x142407770` | exact unique — **same VA** |
| `kSig_InvHolderInsert201` | 46 tok | **0** | — | absent (required) |
| `kSig_InvHolderInsert_Legacy` | 46 tok | **0** | — | absent (required) |
| `kSig_InvCommit` | 47 tok | **1** | `0x14212E160` | exact unique |
| `kSig_InvCommit_Pre201` | 45 tok | **0** | — | absent (required) |
| `kSig_InvCommitPlacement` | 21 tok | **0** | — | absent |
| `kSig_InvCommitPlacement201` | 40 tok | **1** | `0x14212DDF0` | exact unique |
| `kSig_InvFreePlacements` | 45 tok | **1** | `0x1404880C0` | exact unique |
| `kSig_InvFreePlacements201` | 41 tok | **1** | `0x1489FF210` | exact unique — **different VA** |
| `kSig_InvGetHolder` | 21 tok | **1** | `0x14212A0F0` | exact unique |
| `kSig_InvGetItemQty` | 14 tok | **1** | `0x1417F8E70` | exact unique |
| `kSig_InvGetItemQty_Legacy` | 26 tok | **0** | — | absent |
| `kSig_InvCoreGlobal` | 25 tok | **0** | — | absent |
| `kSig_InvCoreGlobal_Pre201` | 40 tok | **0** | — | absent |
| `kSig_InvSetExpandSlots` | 19 tok | **0** | — | absent |
| `kSig_TrItemValueDtor` | **0 tok (empty)** | **0** | — | silent no-op — fix the constant |
| `kSig_LocStringGet` | 28 tok | **1** | `0x1412C0240` | exact unique |
| `kSig_LocStringGet_Alt1/_Alt2/_Legacy` | 14 tok each | **0** | — | absent |

Both `kSig_InvFreePlacements` and `kSig_InvFreePlacements201` resolve to
**different addresses** and each is unique. Which one the add path uses is a
source-side decision (`CanCommitAuthoritativeAdd` must have *a* free function
resolved); record both, and do not assume they are interchangeable.

---

## 6. Fail-closed summary

| Feature | Fail-closed condition | Log |
|---|---|---|
| Add Item | any of ctor / planner / commit / free unresolved, **or** `clientHolder == 0`, **or** `serverHolder == 0`, **or** `serverHolder == clientHolder` | refusal, no partial write |
| Money (modern) | inventory machinery unavailable | inherits Add Item's gate |
| Money (legacy hooks) | `revision >= 2625` | `worker`-style disabled warning class |
| Expansion setter | locator absent on this revision | not installed, per-tick guard instead |
| Item names | localisation getter absent | falls back through `ResolveItemDisplayName` |

There are **no byte patches** in inventory, so there is no OFF-restoration
obligation — "OFF" means "stop issuing the transaction".

---

## 7. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. Re-run the eight core signatures (`kSig_TrItemValueCtor`, the two
   `kSig_InvHolderInsert*`, `kSig_InvCommit`, `kSig_InvCommitPlacement201`,
   both `kSig_InvFreePlacements*`, `kSig_InvGetHolder`, `kSig_InvGetItemQty`,
   `kSig_LocStringGet`). Each must be **exactly 1 match**; anything ambiguous
   means the feature goes fail-closed.
3. Re-read the **8-argument** commit ABI in `sub_14212E160` and the
   holder-insert planner's 9-argument ABI in `sub_142407770`. A changed argument
   count is the most likely break in this dossier.
4. Re-resolve the inventory-core global. `kSig_InvCoreGlobal` measures **0** on
   this build, so it is found through the RIP-relative resolver path
   (`src/game/inventory.cpp`) — confirm that path still works rather than
   reaching for the constant.
5. Re-verify the **realm flag offset** for the new revision in
   `src/core/version_mapping.cpp:77-90`. If the new revision's offset is unknown,
   **do not** default to `0x1EC`; leave the server-write path disabled.
6. Re-verify the four hardcoded table globals (`+0x6350EE8`, `+0x63307A8`,
   `+0x634DDB8`, `+0x634DDD0`). These are raw RVAs and cannot be signature-
   scanned — resolve them from the database and confirm the table shape.
7. Fix `kSig_TrItemValueDtor` (empty pattern) and note whether the destructor
   call at `inventory.cpp:3932` is still dead.
8. Fix the `MH_CreateHook`-without-enable ordering (§3.1) before any revision
   where the legacy money hooks would be created.
9. Runtime test, one feature at a time with an OFF leg:
   - Add Item: single add, then bulk add, then a full-catalog add; verify the
     item appears, survives a save/reload, and that the quantity is correct.
   - Item Editor: change a quantity, delete a row, "Set All".
   - Money: set silver, add silver, each preset, pouch spawn/cash-in, clear
     bugged coins, consolidate stacks — each with an OFF-leg observation.
   - Restore Items / catalog archive: one category each.
   - Abyss items: quick-add one artifact.
   Record observed results; the realm system means a client-only success is
   indistinguishable in the UI from a committed one until a reload.
