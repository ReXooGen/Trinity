# Equipment and appearance (dye) — feature dossier

Trinity scope: the **PLAYER → Edit Equipment** pages (`src/gui/menu.cpp:595`
area, plus `equipedit` / `equipswap` / `equipgear` sub-pages) — Infinite Item
Durability, Repair All, Max Refine All, socket unlock, per-piece refinement,
abyss-gear add/clear, Equip Item, the character selector, and the dye /
appearance editor.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, FileVersion `1.0.0.2944`.
Binary Ninja database `CrimsonDesert.exe.bndb`, view `view_1` (PE).
See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**. No breakpoints, no debugger, no injection,
no memory writes, no game launch.

---

## 1. Headline finding: this area has almost no resolvable game anchors

**Equipment and dye contain zero hooks and zero byte patches.** All work is done
by guarded heap reads/writes on the equipment table plus a small number of
native calls. That design is why most of this area keeps working even though
almost every signature in it fails to resolve on PE 2944.

Measured over all 12 PE sections:

| Locator | Length | Matches | Consequence |
|---|---|---|---|
| `kSig_EquipBatch` | 50 tok | **0** | the equip-rebuild batch routine is **unresolvable** |
| `kSig_EquipBatch_Legacy` | 50 tok | **0** | no legacy fallback either |
| `kSig_EquipEffectRefresh` | 40 tok | **0** | the post-edit effect refresh is **unresolvable** |
| `kSig_EquipEffectRefresh_Legacy` | 36 tok | **2** | **ambiguous — must not be bound** (first hit `0x140659510` = `sub_140659510`, 40 blocks) |
| `kSig_DyeApplySlot` | 25 tok | **0** | force-nulled for `revision >= 2625` anyway |
| `kSig_DyeApplyBatch` / `_Legacy` | 34 / 39 tok | **0** / **0** | dye batch apply unresolvable |
| `kSig_DyeUpsert` / `_Legacy` | 18 / 15 tok | **0** / **0** | dye record upsert unresolvable |
| `kSig_DyeVisualSet` | 41 tok | **0** | unresolvable |
| `kSig_DyeVisualClear` | 41 tok | **0** | unresolvable |
| `kSig_DyeRecordRemove` | 17 tok | **0** | unresolvable |

**No dye or equipment signature resolves uniquely on PE 2944.** The only two
usable anchors in either file are inline AOB literals (§2), and one of them
belongs to teleport, not equipment.

**Operational consequence.** `Equipment::Install()` **returns `true`
unconditionally, even when both of its signatures miss.** A failed equipment
install is therefore completely silent — no `LOG_ERR`, no menu gate — and the
menu rows stay visible. This is the most dangerous defect in this dossier and
should be fixed in Trinity source before the next PE.

---

## 2. Function dossiers

### `sub_14488C468` — socket-vector resize  *(candidate name; retained as `sub_*`)*

- Feature: **Unlock All Sockets** (batch and per-piece) and per-socket abyss
  gear add/clear.
- Location: RVA `0x488C468` / VA `0x14488C468` / `.code` / 144 bytes / 9 basic blocks
- Locator: **inline AOB literal at `src/game/equipment.cpp:1574`** (no `kSig_`
  constant — this is the only equipment locator that resolves):
  `48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00`
  → **exactly 1 match in the whole image**, at VA `0x14488C46D` — which is
  **`sub_14488C468 + 0x5`**, i.e. the match sits just past a 5-byte prologue
  (`48 89 5C 24 08`) and the signature begins at the next instruction
  (`mov [rsp+0x10], rsi; push rdi; sub rsp,0x20; cmp qword [rcx+0x60], 0`).
- Confidence: **strong candidate** — the locator→address binding is measured and
  exact-unique, and the instruction `cmp qword [rcx+0x60], 0` is consistent with
  a "does the socket vector exist / is it sized" test, but the function was
  **not** decompiled or renamed this pass. It stays `sub_*` per the plan's rule.
- ABI: `void*(void* arg1, int32_t arg2)` per the Binary Ninja prototype.
- Data model: the body tests `[rcx+0x60]` before proceeding — the socket vector
  pointer. Trinity's socket writes target the equipment table directly:
  `equipment.cpp:404-573` (socket write/read), with per-realm sync at
  `SyncSocketAllRealms` `:606`.
- Trinity consumer: `src/game/equipment.cpp` — native call, resolved from the
  literal AOB at `:1574`. Role: **native call** (not a hook, not a patch).
- Static proof: unique 15-token inline AOB measured over all 12 sections; the
  containing function boundary (`0x14488C468`–`0x14488C4F8`) and prototype read
  from the database.
- Live proof: none recorded.
- OFF/restore or fail-safe result: nothing is patched. If the AOB is absent the
  resize path cannot be called; socket edits then depend on the vector already
  being large enough — which is exactly the case Trinity's writes assume.
- Update notes: re-run the inline AOB (expect exactly 1 match). **The signature
  starts at function offset `+0x5`, so on a new revision compute the containing
  function by subtracting the prologue length — do not assume the match address
  is a function start.** Then re-verify that `[rcx+0x60]` is still the socket
  vector.

### `sub_140DEF890` — map destination reference site  *(candidate; in `travel.md` scope)*

- Feature: **Map Marker Teleport** — this is the site of the direct
  map-destination global reader.
- Location: RVA `0xDEF890` / VA `0x140DEF890` / `.code` / 1242 bytes / 63 basic blocks
- Locator: inline AOB literal at `src/game/teleport.cpp:529` (20 tokens):
  `48 8B 05 ?? ?? ?? ?? 48 8B 98 A8 00 00 00 C4 C1 78 10 04 24`
  → **exactly 1 match in the whole image**, at VA `0x140DEFCFE`.
- Trinity consumer: `Teleport::Install()` — `mem::ResolveRipAt(hit, 7)` turns the
  `mov rax,[rip+disp32]` into `g_markerDestinationGlobal` (`teleport.cpp:528-532`).
  Role: **locator only** (the global is read, not the function).
- **Why this matters:** this is the anchor that keeps map-marker teleport working
  on PE 2944 **even though `kSig_MarkerPlayer` has 0 matches** and the
  marker-player proxy hook therefore never installs. See
  [`locomotion.md`](locomotion.md) §5.
- Update notes: re-run the 20-token wildcard AOB (expect exactly 1 match) and
  re-resolve the RIP displacement. If it is absent, the `players`/`markers`
  capture detours are the only fallback and the log will say
  `teleport: direct map-destination reference not found; legacy capture hooks are required.`

### Equipment field contract (`src/game/equipment.cpp`)

| Item | Value / behaviour |
|---|---|
| socket data offset | `equipment.cpp:410` / `:418` use **literals `0x60` / `0x58`** instead of the named `kOff_ItemVal_SocketData` / `kOff_ItemVal_SocketData_Legacy` |
| legacy split | `equipment.cpp:440` uses `isLegacy ? 0x68 : 0x70` |
| refine / socket sync | `SyncSocketAllRealms` `:606`, `SyncRefineAllRealms` `:674` — both must honour the realm contract (see `inventory.md` §1.1) |
| install / tick | `Equipment::Install()` `:1564`, `Equipment::Tick()` `:2025` |
| table read | `ReadEquipTableDesc` `:62` |
| durability | `equipment.cpp:2033-2042` (Infinite Item Durability) |

**Trinity-computed, not game-read.** `Equipment::SlotInfo`'s `attack`,
`defense`, `reinforceExp` and `reinforceBonus` are **Trinity-side estimates**
(`equipment.cpp:1240-1247`, with `reinforceExp` hardcoded to `72`), not values
read from the game. Any UI discrepancy here is a Trinity modelling issue, not a
game contract change — do not chase it as a PE regression.

**Dead offsets** (defined in `offsets.h`, zero consumers): `kOff_ItemVal_SocketData`,
`kOff_ItemVal_SocketSize`, `kOff_ItemVal_SocketCap`, `kOff_ItemVal_SocketUnlocked`,
`kEquipEntry_Stride`, `kOff_EquipComp_Table`, `kOff_EquipEntry_SlotTag`,
`kOff_ItemVal_DyeData`. Several were functionally replaced by the literals above.

---

## 3. Dye / appearance — status: **dead on this revision**

Dye is **not** a supported-but-undocumented feature on PE 2944. It is dead for
two independent reasons:

1. **The UI is unreachable.** `RenderDyeSlots` / `RenderDyeEdit` /
   `RenderDyeCustom` (`src/gui/menu.cpp:281`, `:400`, `:513`; 20 controls) are
   orphaned: there is **no `"dyeslots"` string anywhere** and no dispatch case.
   The push sites were deleted in commit `7c05de4`
   ("update Trinity for Crimson Desert 2.02.00").
2. **Every engine anchor fails to resolve.** All eight dye signatures measure
   **0** on this build (§1). In addition, `DyeApplySlot` and the three visual
   leaves (`DyeVisualSet`, `DyeVisualClear`, `DyeRecordRemove`) are
   **deliberately force-nulled for `revision >= 2625`**, so even a future build
   where they resolve would not use them.

**Status classification: legacy / dormant / not safe to enable.** Document it,
do not attempt to revive it, and do not treat the 20 orphaned controls as menu
features in the inventory counts.

The dye code that *is* still referenced (`Dye::Install` `dye.cpp:1563`,
`Dye::Tick` `:1777`, `hkEquipBatch` `:476`, the engine-call wrappers `:598-709`,
`MirrorToServer` `:855`, `ClientComp` `:310`, `ProcessRequest` `:1251`) depends
on `kSig_EquipBatch`, which measures **0** — so the equip-rebuild driver cannot
install either.

Two on-disk artefacts to be aware of if dye is ever revived:
`Dye::GetDyeCachePath` (`dye.cpp:799`) writes a cache with the magic `"TRDYE03"`
(the exact filename literal was not read this pass), and the `DyeWatchFile`
side-channel writes `Trinity_DyeWatch.txt`.

---

## 4. Locator evidence summary (measured over the whole image)

Method: `_analysis/aob_scan2.ps1` (wildcard-capable) plus
`_analysis/extract_inline_aobs.ps1`, which pulls byte-pattern literals written
directly at the call site. All 12 PE sections scanned.

**Equipment**

| Locator | Kind | Matches | Result |
|---|---|---|---|
| socket-vector resize | inline AOB `equipment.cpp:1574` (15 tok) | **1** @ `0x14488C46D` | exact unique (function `+0x5`) |
| `kSig_EquipBatch` | constant | **0** | absent — feature fail-closed |
| `kSig_EquipBatch_Legacy` | constant | **0** | absent |
| `kSig_EquipEffectRefresh` | constant | **0** | absent — post-edit refresh unavailable |
| `kSig_EquipEffectRefresh_Legacy` | constant | **2** | **ambiguous — reject for binding** |

**Dye** — all eight constants measure **0**: `kSig_DyeApplySlot`,
`kSig_DyeApplyBatch`, `kSig_DyeApplyBatch_Legacy`, `kSig_DyeUpsert`,
`kSig_DyeUpsert_Legacy`, `kSig_DyeVisualSet`, `kSig_DyeVisualClear`,
`kSig_DyeRecordRemove`.

---

## 5. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. Re-run the socket-resize inline AOB (expect exactly 1 match). Remember the
   match is at **function offset `+0x5`** — subtract the prologue length to get
   the function start, then confirm `[rcx+0x60]` is still the socket vector.
3. Re-run `kSig_EquipBatch` and `kSig_EquipEffectRefresh`. If either becomes
   exact-unique, that is **new capability** — record it in this dossier before
   wiring it up. Until then, treat post-edit equipment effect refresh as
   unavailable and say so in the log.
4. Re-run `kSig_EquipEffectRefresh_Legacy`. It currently has **2 matches**; do
   not bind on the first hit. If it ever becomes unique, re-derive which of the
   two was the real refresh.
5. **Fix `Equipment::Install()`'s unconditional `return true`.** It must report
   failure when its signatures miss, otherwise a silent regression ships.
6. Re-verify the socket field offsets. Replace the literals at
   `equipment.cpp:410` / `:418` / `:440` with the named constants, or delete the
   dead constants — having both is how a PE update picks the wrong one.
7. Re-verify the realm flag offset for the equipped-item sync paths
   (`SyncSocketAllRealms`, `SyncRefineAllRealms`) — see `inventory.md` §1.1.
8. **Do not attempt to revive dye.** Confirm instead that the eight dye
   signatures are still 0 and that the dye editor is still unreachable, and
   record that as the expected state.
9. Runtime test, one feature at a time with an OFF leg:
   - Infinite Item Durability: damage an item with it OFF, then ON.
   - Repair All: use a damaged item, repair, confirm.
   - Max Refine All: refine to +10, confirm the value and that it survives a
     reload.
   - Unlock All Sockets / per-socket abyss gear: unlock, add a socket, clear it.
   - Equip Item: equip a quest/class-locked item.
   - Character switch: switch, confirm the equipment page follows.
