# Crime, wanted, bounty, money and legacy systems — feature dossier

Trinity scope: **No Bounty** (`st.noBounty`), the **Bounty Notices** restore
category, and the whole **Money & Currency** subtree, plus the two legacy
systems that this revision forces to stay disabled.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, FileVersion `1.0.0.2944`.
Binary Ninja database `CrimsonDesert.exe.bndb`, view `view_1` (PE).
See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**. No breakpoints, no debugger, no injection,
no memory writes, no game launch.

---

## 1. Summary — what is available and what is not

| Sub-feature | Menu location | Status on PE 2944 | Mechanism |
|---|---|---|---|
| No Bounty (wanted state) | `menu.cpp:103-108` | **active** | MinHook on the wanted-state evaluator, forced to return `7` |
| No Bounty upkeep | `world.cpp:611-630` (`World::Tick`) | **active** | per-tick re-assert |
| WantedInfo `_increasePrice` zero/restore | `inventory.cpp:5681-5755` | **active** | data write on the WantedInfo table rows |
| Bounty Notices restore category | `menu.cpp:2284-2300`, `2492-2495`, `3218` | **active** | inventory machinery (item grants) |
| Money & Currency (set/add silver, presets, pouches, cash-in, clear bugged coins, consolidate) | `menu.cpp:2048-2150` | **active** | inventory machinery + item-count hook |
| **Crime UI banner / minimap wanted circle / guard hostility suppression** | — | **UNAVAILABLE — deliberately not probed** | legacy dispatcher absent on this revision |
| **Legacy money getter hooks** | — | **DISABLED — offsets invalid on this revision** | hardcoded PE 2625-era RVAs |

The single most important statement in this dossier:

> On PE 2944 the legacy 4-argument crime-event dispatcher no longer exists.
> No Bounty remains fully supported through WantedInfo and wanted-state
> evaluation. **Only** crime-banner / minimap / guard-dispatch suppression is
> absent.

Trinity states this itself, verbatim (`src/game/inventory.cpp:2078-2080`):

> `PE 2944's prior event dispatcher no longer exists. No Bounty remains supported through WantedInfo and wanted-state evaluation; only crime-banner/minimap/guard-dispatch suppression is absent.`

and logs it at runtime (`inventory.cpp:2081-2082`):

> `world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.`

---

## 2. Function dossiers

### VIBE_WantedState_Evaluate  *(candidate name — function retained as `sub_1425d7660`)*

- Feature: **No Bounty** — the wanted-state evaluator Trinity hooks.
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x25D7660` / VA `0x1425D7660` / `.code` / 176 bytes / 18 basic blocks
- Locator: `kSig_EvaluateCrimeWantedState` (25 tokens, `src/game/offsets.h`)
  `48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 48 8B DA 4C 6B D9 38 41 B0 07 4C 03 D8 49 3B C3 74 56 48 83 78 18 00 75 46 48 83 78 30 00 74 08 80 78 34 00 74 3A EB 06 48 39 58 10 72 32`
  → **exactly 1 match in the whole image**, at this VA, section `.code`,
  measured over all 12 PE sections. Live hook state: installed (MinHook detour).
- Confidence: **confirmed** (locator + disassembly + Trinity's documented
  forced return value all agree)
- ABI: `RCX` = wanted-state context object; `RDX` = `int64` comparison key
  (`rbx` after `mov rbx, rdx`); **return = `AL`, a wanted-state enum byte**.
  No stack arguments, no XMM inputs on the paths observed.
- Data model (read straight from the disassembly):
  | Expression | Meaning |
  |---|---|
  | `ctx+0x40` | pointer to the first wanted-record |
  | `ctx+0x48` | record count (`dword`) |
  | record stride | **`0x38`** (`imul r11, rcx, 0x38`) |
  | `rec+0x10` | `int64` key compared against `RDX` (`cmp [rax+0x10], rbx` / `jb` skip) |
  | `rec+0x18` | `ptr`; non-zero ⇒ skip the record |
  | `rec+0x20` | `int64` weight, accumulated into `r10` when the kind is `1` |
  | `rec+0x2c` | `byte` record kind; `1` ⇒ accumulate, `7` ⇒ ignore |
  | `rec+0x30` | `dword` gate; non-zero combined with `rec+0x34 == 0` ⇒ skip |
  | `data_146ce3b88` | `int32` threshold compared against the accumulated weight |
  The function returns `7` when the accumulated weight exceeds the threshold
  **and** no other kind was seen; otherwise it returns `max(kind, 1)` clamped, or
  the observed kind, or `7` on the empty path.
- Call flow: **10 callers** — `sub_14066b300` `0x14066b387`, `sub_14172ea70`
  `0x14172ebc9`, `sub_14172ecb0` `0x14172ee18`, `sub_14179b1a0` `0x14179b2cf`,
  `sub_1417ed600` `0x1417ed7b5`, `sub_1422e5690` `0x1422e579f`,
  `sub_14198ed56` `0x1422e579f`, `sub_1420b6d00` `0x1420b7103`,
  `sub_142959860` `0x1429598fd`, `sub_14cc42990` `0x14cc42abd`.
  All ten were inspected during the crime investigation; **none was promoted**
  as a replacement for the missing crime UI dispatcher.
- Trinity consumer: `src/game/inventory.cpp` — the No Bounty hook
  (`:2032-2041`, `:2066-2068`) forces the evaluator to return `7`
  (`eWantedState_None`). Role: **MinHook inline detour** (not a patch).
- Static proof: unique 25-token AOB; full disassembly; the `0x38` stride and
  every record field above read directly from the instruction stream.
- Live proof: none recorded in this pass. `progress.md` records that No Bounty
  works via this hook, but there is no per-feature ON/OFF/persistence log entry.
- OFF/restore or fail-safe result: MinHook detour, removed with the other
  inventory hooks; no bytes patched. If the locator is absent the hook is not
  installed and the evaluator returns the game's own value — i.e. the failure
  mode is "No Bounty silently does nothing", which is the safe direction.
- Update notes: re-run `kSig_EvaluateCrimeWantedState` (expect exact unique).
  Then re-verify the record stride `0x38` and the field offsets `+0x10`,
  `+0x18`, `+0x20`, `+0x2c`, `+0x30`, `+0x34`, and that the function still
  returns its enum byte in `AL`. Trinity's forced value `7` is only correct if
  `7` still means "no wanted state" — re-derive the enum from the branch
  structure before re-enabling.
- Open questions: the enum's full meaning is inferred from the branch structure
  (`kind == 7` is the "ignore" case and the empty-path return). The identity of
  the `ctx` object and the `data_146ce3b88` threshold global were not chased to
  their owners.
- Name: kept as `sub_1425d7660` because only the *role* is proven, not the exact
  game-side class. `VIBE_WantedState_Evaluate` is the candidate name once the
  `ctx` object is identified.

### VIBE_GetActiveSlotIndexForRecord — WantedInfo path  *(pre-existing name, kept)*

The WantedInfo money/price path writes `_increasePrice` at **`+0x18` (i64)** on
WantedInfo rows (`src/game/inventory.cpp:5681-5755`) and restores the captured
originals on OFF. The WantedInfo table global is one of the four hardcoded
module-relative addresses, **`gameBase + 0x6350EE8`**, gated `revision >= 2625`
(`src/game/inventory.cpp`). This is the only crime-related address in the tree
that is a raw RVA rather than a signature, so it is the most fragile item in
this dossier.

---

## 3. Unavailable / fail-closed features — explicit record

### 3.1 Crime UI banner bypass — **UNAVAILABLE on PE 2944, and correctly so**

| Item | Value |
|---|---|
| Locator | `kSig_RegisterCrimeEvent` (31 tokens, `src/game/offsets.h`) |
| Measured matches on PE 2944 | **0 over all 12 sections** |
| Trinity gate | `MayProbeLegacyCrimeEventDispatcherForRevision(revision)` = `revision <= 2850` (`src/core/version_mapping.cpp:67-70`; rationale `src/core/version_mapping.h:37-39`) |
| On PE 2944 | **not probed at all** — the gate short-circuits before the scan |

**Do not force a replacement.** The plan's own anchor note is confirmed here:
the wanted-state evaluator is a valid No Bounty path but is **not** a
substitute for the removed dispatcher. Every one of the evaluator's nine/ten
callers was inspected and none was promoted into that role. The historical
dispatcher address `0x141595BC0` belongs to unrelated code on this build.

**Fail-closed result:** the crime banner, the minimap wanted circle and guard
hostility dispatch keep their normal game behaviour. Only wanted-state and
WantedInfo are suppressed. This is the intended, documented outcome — not a
regression to be fixed by guessing.

### 3.2 Legacy money getter hooks — **DISABLED, offsets invalid on PE 2944**

| Item | Value |
|---|---|
| Addresses | `gameBase + 0x16077B0`, `+0x16078C0`, `+0x16081D0` (raw RVAs, `src/game/inventory.cpp:2096-2105`) |
| Trinity gate | `revision < 2625` — so **not created on PE 2944** |
| Historical note | these three are recorded as **INVALID** for TU 2.00+ |
| Additional defect | they are created with raw `MH_CreateHook` (`inventory.cpp:2098-2100`) but **never enabled**: the only `MH_EnableHook(MH_ALL_HOOKS)` lives in `InstallDX12Hooks` (`src/hooks/dx12_hook.cpp:1570`), called at `mod.cpp:103` — *before* `Inventory::Install` at `mod.cpp:131`, and no later enable or queued-apply exists in `src/`. They are also **not removed** in `Inventory::Remove()`. |

The wallet spoof still reaches the HUD through the item-count hook, which *is*
enabled. Treat the three legacy getters as dead code on this revision, and fix
the create/enable ordering before relying on them on any revision that does
enable them.

### 3.3 Known functional gap — TribeInfo `_wantedCrimeType`

`src/game/inventory.h:319-320` promises that No Bounty also zeroes
**TribeInfo's `_wantedCrimeType`**, but `SetNoBounty` never touches TribeInfo,
and `_wantedCrimeType` appears nowhere else in `src/`.
`kOff_WantedDef_IsBlocked` (`offsets.h:1179`) is defined but has zero consumers.
Either implement it or correct the header comment — as written the header
overstates what No Bounty does.

---

## 4. Money & Currency

The whole subtree (`menu.cpp:2048-2150`) runs on **inventory machinery**, not on
a dedicated wallet writer:

- Set / Add Direct Silver, the 1M / 10M / 20M presets, Silver Pouch spawn and
  cash-in, "clear bugged coins" and "consolidate money stacks" all resolve to
  item grants, item-count reads and stack manipulation.
- The stack-size pin is gated `revision < 2625` and is therefore **inactive** on
  PE 2944.
- No dedicated money function was found in the database: database-wide searches
  for `Wanted` / `Crime` / `Bounty` / money-specific symbols return nothing
  beyond the standard library's `std::money_put`.

Consequently the money contract is documented in
[`inventory.md`](inventory.md) (§ item creation / quantity / commit ABI). This
dossier records only that **money has no independent game-side contract** — do
not go looking for one during a PE update.

Two dead ends to record so they are not rediscovered:

- `Inventory::AddCampCurrency` (`inventory.cpp:4838`) has **no call site
  anywhere**, and it feeds the key `Money_Camp_Wood` while the spoof recognises
  `Money_Camp_Timber`.
- `BackgroundCurrencyScan` (`inventory.cpp:4513`) is fully inert — its body was
  replaced by a `Heap Scanner disabled: Removed unsafe WeMod memory scanning
  that caused memory corruption.` comment — and appears never to be called.

---

## 5. Locator evidence (measured over the whole image)

Method: `_analysis/aob_scan2.ps1` (wildcard-capable) reproduces Trinity's
scanner against the on-disk image, all 12 sections.

| Locator | Length | Matches | Result |
|---|---|---|---|
| `kSig_EvaluateCrimeWantedState` | 25 tok | **1** @ `0x1425D7660` | exact unique |
| `kSig_RegisterCrimeEvent` | 31 tok | **0** | **absent — feature unavailable (by design)** |
| `kSig_FriendlyAlertDisp` | 32 tok | **0** | absent (faction alert dispatcher; unused constant) |

Raw RVA (not a signature, cannot be measured by the scanner):
`WantedInfo = gameBase + 0x6350EE8` — re-verify by resolving it in the database
and confirming it still points at a table whose rows carry the `+0x18` i64 price
field, rather than by trusting the constant.

---

## 6. Trinity source consumers

| Source | Symbol | Role |
|---|---|---|
| `src/game/inventory.cpp` | No Bounty hook `:2032-2041`, `:2066-2068` | MinHook detour forcing return `7` |
| `src/game/inventory.cpp` | WantedInfo `_increasePrice` zero/restore `:5681-5755` | data write + restore |
| `src/game/inventory.cpp` | legacy crime dispatcher probe `:2078-2082` | gated off for `revision > 2850` |
| `src/game/inventory.cpp` | No Bounty upkeep | driven from `World::Tick` (`world.cpp:611-630`) |
| `src/core/version_mapping.cpp` | `MayProbeLegacyCrimeEventDispatcherForRevision` `:67-70` | the `<= 2850` gate |
| `src/game/inventory.h` | `:319-320` | header promise that is not implemented (§3.3) |
| `src/game/offsets.h` | `kSig_EvaluateCrimeWantedState`, `kSig_RegisterCrimeEvent`, `kOff_WantedDef_IsBlocked` | locators |
| `src/gui/menu.cpp` | `:103-108`, `:2284-2300`, `:2492-2495`, `:3218`, `:2048-2150` | the visible controls |

**Important coupling:** No Bounty upkeep runs from `World::Tick`, which is
dispatched **only** from `VIBE_Locomotion_MoveUpdateIntegrator` (the
`kSig_MoveUpdate` hook). If that hook fails to install, No Bounty's per-tick
re-assert silently stops even though the evaluator hook may still be live. See
[`locomotion.md`](locomotion.md) §1 and §5.

---

## 7. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. Re-run `kSig_EvaluateCrimeWantedState` (expect exact unique). Re-read the
   record stride (`0x38`) and fields `+0x10` / `+0x18` / `+0x20` / `+0x2c` /
   `+0x30` / `+0x34`, and re-derive the returned enum before re-enabling the
   forced `7`.
3. Re-run `kSig_RegisterCrimeEvent`. **An exact unique match does not mean the
   feature should be re-enabled** — the `revision <= 2850` gate encodes a
   semantic judgement, not just a byte fact. Re-derive the dispatcher's argument
   layout and prove it is the crime banner path before touching the gate.
4. Re-verify the WantedInfo global by resolving `gameBase + 0x6350EE8`; if it
   moved, find the table through the WantedInfo accessor rather than by
   re-hardcoding an RVA.
5. Confirm the `revision < 2625` money gates still exclude the new revision, and
   fix the `MH_CreateHook`-without-enable defect (§3.2) if those paths are ever
   meant to work.
6. If either the evaluator locator is absent or the WantedInfo table cannot be
   found: leave No Bounty `OFF`, record the mismatch here, and **do not** point
   the evaluator's callers at a guessed replacement dispatcher.
7. Runtime test (one feature at a time, each with an OFF leg): commit a crime
   with No Bounty **OFF** and confirm the wanted state appears; switch it **ON**
   and confirm the wanted state clears and stays clear across a save/reload;
   switch it **OFF** and confirm the game's own state returns. Record the
   *observed* result — the evaluator hook has no dedicated log line of its own
   beyond the crime-dispatch-unavailable warning.
