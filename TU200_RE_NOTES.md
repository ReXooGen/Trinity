# Title Update 2.02.00 (PE 1.0.0.2850) Reverse-Engineering Notes

> **Binary**: ~375 MB EXE (Sep 2026 build) · **Image Base**: `0x140000000`.  
> **Revision Ladder** (runtime fingerprint source, `src/core/version_detect.cpp`): `1.0.0.2474` = TU 1.18.02 · `1.0.0.2625` = TU 2.00.00 · `1.0.0.2658` = TU 2.00.01 · `1.0.0.2692` = TU 2.00.02 · `1.0.0.2760` = TU 2.01.00 · **`1.0.0.2850` = TU 2.02.00** (detected at `revision >= 2800`).  
> **Scanner**: `VirtualQuery`-based committed-span scan (MEM_COMMIT + readable, guard pages skipped) with a PE-header fallback — stale legacy build code in uncommitted/unreadable regions is bypassed by construction; the 2.00-era "exclude `.debug*` sections" rule is superseded.  
> **Source of truth**: every fact below is annotated in place in `src/game/offsets.h`.

---

## 1. Feature Status (2.02)

- **God Mode / Infinite Stamina / Spirit**: **RESTORED** — the TU 2.00-era `pa_StatCommit` elimination is resolved by the TU 2.01+ signature (`kSig_StatCommit`, IDB `sub_14C4E6A80`, unique match; the function lives in the `.link` section).
- **Slot-Expansion Override (Add-slot)**: **OPERATIONAL** — the 2.02 setter (`kSig_InvSetExpandSlots` @ `0x1420801E0`) is hooked with the new 4-arg ABI (see §2). Substituting the count inside the hook makes the engine's own re-stamps apply the override.
- **MoveUpdate / World::Tick / Player::Tick / Inventory::Tick**: **OPERATIONAL / SAFE** — carried from TU 2.01; `kSig_MoveUpdate` and the tick hooks were not flagged obsolete in the final 2.02 scan.
- **Pattern Scanner**: **SAFE** — committed-span scanning only; guarded probes (`IsCompFaulted` / `IsRenderComp`) added in front of the deeper 2.02 dye render chains (previously 90k+ caught `0xC0000005` per session on stale companion/mount bodies).
- **Perfect Parry / Perfect Dodge toggles**: **REMOVED from UI** — `kSig_JustCore` (both forms) is unresolvable on 2.02 (see §3); no blind re-hook shipped.
- **Game Speed**: driven by the direct `FrameTimerUpdate` hook (`kSig_FrameTimerBody` @ `0x140A541C0`, TU 2.01+ form) — the TU 2.00 BSS-override approach (`kSig_GameSpeed`) is retired.

---

## 2. Signature Changes (2.01 → 2.02)

- **`kSig_GameCoreGlobal`**: re-pinned with a new 22-byte form. The old 11-byte shape (`48 8B 0D ?? ?? ?? ?? 48 8B 49 58 E8`) matches 2 sites on 2.02 — true site `0x14035C0BB` + false positive `0x140663A49`. The new form pins the call tail (`90 40 38 74 24 40`); RIP target `0x146C2D9F0` = charMgr global (`0x146C2DF18`) − `0x528`, confirming the `[rcx+0x58]` deref + rip-resolve contract is unchanged.
- **`kSig_EnvManager`**: new form (unique @ `0x14340CA3B`). The virtual-call slot moved **`0x40` → `0x60`**; the field-manager member **`+0xEE0` is unchanged**; global `g_env = 0x146B52FF0`. A trailing pin (`mov rax,[rcx+0x90]`) isolates the site; all 16 equivalent runs in the image resolve to the same global.
- **`kSig_InvSetExpandSlots`**: **ABI swapped** — TU ≤2.01 was `void f(holder, int* outErr, void* unused, u16 bucketType, u16 count)` (5 args); TU 2.02 dropped the dead 3rd pointer arg: `void f(holder, int* outErr, u16 bucketType, u16 count)` — bucketType in `r8`, count in `r9`. The detour prototype must match or MinHook misroutes registers.
- **`kSig_MarkerPlayer`**: source register of the player-pointer load changed `rsi` → `rdi` (`48 8B 06` → `48 8B 07`); unique @ `0x1435CA7CB`. Dest2 stays `+0x1B0`; hook contract unchanged.
- **`kSig_MarkerProtection`**: protection flag displacement moved `0x454` → **`0x458`**; unique @ `0x14118A46E`. (No match in 2.00 — restored since 2.01 with the updated pattern.)
- **`kSig_FriendlyTrustSiteA/B`**: hook store now at sig+0x14; the record-stride growth widened the `+0x40` copy from XMM to YMM (`0xF8` → `0xFC`). Site A `0x141E2C1F8` (SetNpc `0x141E2C090` +0x168), Site B `0x14D87BE28` (SetPet `0x14D87BCB0` +0x178).
- **`kSig_MarkerPattern`**: re-verified — exactly 5 hits (`0x140A29475`, `0x14170602D`, `0x141764AF3`, `0x141E4EB47`, `0x141E4EC5A`). **`kSig_MarkerOriginPrefix`**: MULTI by design (26 raw hits) — consumed via `FindAllMatches(.., 64)` + origin voting (21 votes → `0x146C1AE10`, 5 votes → `0x146767930`).
- **Carried unchanged from TU 2.01** (unique-match hooks): `kSig_DamageApply`, `kSig_LocoStepper` (`0x1435C40A0` on 2.02), `kSig_TravelToNode`, `kSig_InvGetItemQty`, `kSig_InvGetHolder`, `kSig_InvHolderInsert` (`0x142A18960`, `.data2`), `kSig_InvCommit` (`0x142A9A260`), `kSig_InvCommitPlacement` (`0x14207A2C0`), `kSig_InvCoreGlobal`, `kSig_TrItemValueCtor` (`0x14234F210`), `kSig_EquipBatch` (`0x142B474B0`), `kSig_EquipEffectRefresh` (`0x140E7BAC0`), `kSig_DyeApplyBatch` (VA `0x1409165D0`), `kSig_DyeUpsert` (`0x142355870`), `kSig_DyeVisualSet` (`0x1409170C0`), `kSig_DyeVisualClear` (`0x140918770`), `kSig_FieldTimeRealm`/`Tick` (`0x1409BC623`), `kSig_TodEngineGlobal`, `kSig_WindPack` (`0x143CC9B10`), `kSig_WeatherRain`/`Snow`/`Dust`, `kSig_LocStringGet`, the SetDestinationMarker family, the crime hooks, the Friendly setter prologues, the Barber UI trio, and the 9-anchor `kCharMgrAnchors` consensus (verified against `qword_146C29C88`).

---

## 3. Deprecated / False Positives in 2.02

- **`kSig_JustCore` / `_Alt`**: both forms FAIL — the `mov rax,rsp; push rbp; push r14` prologue family is gone; 30 generic sites share the old body anchor, so no byte-distinguishable replacement exists. Unreferenced by any `.cpp`.
- **`kSig_DyeApplySlot`**: **false positive** — resolves to `0x142B41E60`, a hash/registry utility (its only caller passes `(container, u16 key, out, counter)` and it writes `[out]=counter`). Calling it with dye arguments corrupts the record buffer and free-runs its inner loop — the mount-dye freeze/crash. `CallDyeApplySlot` is disabled on 2.02; the safe pipeline is `DyeUpsert` + `DyeApplyBatch` + visual leaves.
- **`kSig_DyeRecordRemove`** (both forms): 2.02 inlined the channel scan into `DyeApplyBatch`'s clear branch (`cmp byte [r9+rcx*8+6], r10b` @ `0x140916835`); the record shift-out moved to a helper @ `0x140F1D1E0` taking `(vecWrapper{data,count}, INDEX)` — a different contract. Covered by the in-place removal fallback in `CallDyeRecordRemove`.
- **`kSig_TrItemValueDtor`**: empty on 2.02 — the 2.00 form is not byte-identical and the generic prologue alone matches 419 sites. `inventory.cpp` skips the dtor when null; only the crash-cleanup path is inert.
- **`kSig_InvSetExpandSlots_Bad20202`**: WARNING — published earlier as "TU 2.02 verified @ `0x143804870`", but that hit is an unrelated function (reads `[rcx+0x48]`, never writes `+0x16/+0x1A/+0x14`). Hooking it was the actual root cause of the 2.02 "engine re-stamps vanilla expansion" symptom. Kept as a reference/warning only.
- **`kSig_ResizeSocketVector_TU201`**: cloned 51× by the linker in 2.02, all byte-identical for the full 31-byte body — no unique signature derivable statically. Unused; future consumers need a caller-side anchor.
- **All `*_Legacy` forms**: 0 hits in the final 2.02 scan (`StatCommit`, `LocoStepper`, `TravelToNode_TU200`/`_Legacy`, `InvGetItemQty`, `InvHolderInsert`, `InvCommit`, `InvCoreGlobal`, `TrItemValueCtor`, `InvCommitPlacement`, `LocStringGet_Alt1/2/Legacy`, `FrameTimerBody`, `FieldTimeTick`, `WindPack`, `EquipBatch_TU200`/`_Legacy`, `DyeApplySlot`, `DyeApplyBatch_Legacy`, `DyeUpsert_TU116`, `DyeVisualSet`, `DyeVisualClear`, `FriendlySetNpc`/`SetPet`, `FriendlyAlertDisp`, `EquipEffectRefresh`, `GameCoreGlobal_TU201`).
- **Legacy Money Getters**: hardcoded addresses `0x16077B0 / 0x16078C0 / 0x16081D0` remain skipped on all TU 2.00+ (`revision < 2625` gate); the 2.00 candidate wrappers (`0x144899FB0`/`0x144899FE0`, helper `0x1402ED7A0`, worker `0x14115BB10`) were never re-derived on 2.02 — treat as unresolved.

---

## 4. Structure Layout Discoveries (2.02)

`TrItemValue` (from ctor `0x14234F210`; unchanged from 2.00 for modded fields):
```
+0x00: int64_t InstanceId (-1 out of the ctor)
+0x08: uint16_t TypeId
+0x0A: uint16_t Refine/Subtype (source def+0x218)
+0x40: uint16_t Durability (source def+0x400)
+0x48 / +0x50: int64_t = [def+0x1C8 / def+0x1D0] * 1000
+0x60: Socket vector data ptr; u32 size @ +0x68; u32 cap @ +0x6C
+0x70: Unlocked count byte (LOW BYTE ONLY; upper bytes are engine flags: 0xFFFFFF02)
+0x78: dye-data pointer (16-byte records)   +0x80: u32 dye count
```

**`ItemDef` (partial)**:
- Default socket array: `def + 0x220` (ptr) / `def + 0x228` (count), 6-byte records.
- Bucket Type: `def + 0x428` (u16) — **unchanged in 2.02** (`revision >= 2625` gate carries over).

**Bucket layout (re-confirmed by the 2.02 setter @ `0x1420801E0`)**: type `+0x10`, used `+0x12`, cap `+0x14`, delta accumulators `+0x16/+0x18`, expansion `_varyExpandSlotCount` `+0x1A`. The setter writes `bucket[0x16] = count`, `bucket[0x1A] = count`, `bucket[0x14] = row._defaultSlotCount + count`; `count` is the EXPANSION, not the cap.

**Equip component table**: primary layout 208-byte stride (`0xD0`) with slot tag at `+0xC8`; secondary 200-byte stride (`0xC8`) with tag `+0xC0`. Runtime-scored dual detection in `dye.cpp` / `equipment.cpp` / `player.cpp`.

**Trust record** (0x58 bytes, copied by SIMD stores at `+0x00/+0x20/+0x40/+0x50`): key u32 `+0x00`, group u16 `+0x04`, **trust value i64 `+0x28`** (confirmed QWORD @ `+0x28` in TU 2.00; carried through 2.02).

**TLS realm flag**: TU 2.01+ (PE rev >= 2760) = **509 (`0x1FD`)**; TU 2.00 and earlier = 498 (`0x1F2`).

---

## 5. Analysis & Reverse Engineering Tools

- `scripts/audit_tu201_fixes.py`: audits the load-bearing signature set against the live EXE — update the pattern list and re-run after every game patch.
- `scripts/`: live-session inspectors (party/equip resolution, mount entities, RTTI, dye slots, entry-layout dumps).
- `re_scripts/`: static disassembly and pattern hunters (Capstone-based one-off scripts).
- `scratch/` + `scratch/scripts/`: session probes, struct dumps, and binary diffs.
- `tools/deploy_master.ps1`: deployment helper.
- **Skill**: `.agents/skills/crimson-binary-inspector` (`inspect_pe`, `deep_entity_pointer_routing`, etc.).

---

## 6. Live Session Findings (2.02)

- **Mount-dye freeze/crash (root-caused)**: `kSig_DyeApplySlot` resolving to `0x142B41E60` is a false positive (hash/registry utility). The per-slot path is disabled on 2.02; data writes go through `DyeUpsert`, visuals through `DyeApplyBatch` / visual leaves with guarded render-chain probes. Companion/mount dye is data-persistent; visual refresh comes from the auto-restore replay on summon/reload.
- **"Engine re-stamps vanilla slot expansion" (root-caused)**: the earlier 2.02 build hooked the wrong function via a misleading 5-arg signature (`0x143804870`); the real setter stayed unhooked, so the engine's server-side expansion sync kept re-stamping vanilla values. Fixed by hooking the true 4-arg setter and substituting the count inside the hook.
- **Free-space gate**: still not hooked — static hunts on the 2.02 image found no unique byte shape (the refusal error `eErrNoInventorySlotNotExist` is resolved at runtime; its immediate appears nowhere). Mitigated via `hkHolderInsert` logging of the refusing bucket's used/cap/arr/occ plus `RepairUsedSlots` and the anti-shrink guards.
- **Socket / dye containers unchanged**: `kSig_DyeUpsert` @ `0x142355870` reads `[entry+0x78]` data / `[entry+0x80]` count with 16-byte records and the channel byte at record+6; the 6-byte socket-record injection path and the unlocked-count LOW-BYTE rule (`+0x70`, flags `0xFFFFFF02`) both remain valid.
- **Kliff live component walk**: `CharMgr` traversal followed by `*(*(owner + 0x68) + 0x38)` remains VALID on 2.02; multi-layer realm sync (client component → `CharacterAddrs` mirrors → `FindAndApplyAllHolders` → server realm via TLS flag) unchanged.
- **Barber UI (Portable Barber)**: on 2.02 (PE 1.0.0.2850) the `UIGamePlayControlRootBarberShop` thunk resolves and the bind lands (handle `0xCF6` in the slot), but binding alone does NOT show the screen — the game's own open glue resolves and event-invokes the object through the script VM. v2 adds a logging hook on `UiBindObject` to capture the exact per-root glue site when the game naturally opens a gameplay root.
