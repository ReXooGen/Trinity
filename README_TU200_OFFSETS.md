# Trinity — Title Update 2.02.00 Offset & Data Reference (PE 1.0.0.2850)

> **Game Build**: Crimson Desert TU **2.02.00** · PE Revision **`1.0.0.2850`**  
> **Revision Ladder** (runtime fingerprint source, `src/core/version_detect.cpp`): `1.0.0.2474` = TU 1.18.02 · `1.0.0.2625` = TU 2.00.00 · `1.0.0.2658` = TU 2.00.01 · `1.0.0.2692` = TU 2.00.02 · `1.0.0.2760` = TU 2.01.00 · **`1.0.0.2850` = TU 2.02.00**  
> **Image Base**: `0x140000000` · Binary: ~375 MB EXE (Sep 2026 build)  
> **Scanner**: `VirtualQuery`-based committed-span scan (MEM_COMMIT + readable, guard pages skipped) with a PE-header fallback. The 2.00-era "explicitly exclude `.debug*` sections" rule is superseded — only committed, readable pages of the module image are ever touched, so stale legacy build code in non-committed/unreadable regions is bypassed by construction.

---

## 1. Array of Bytes (AOB) Signatures — Status vs TU 2.00 / 2.01

### Changed & Re-Verified for 2.02

| Signature | TU 2.02 Status | Pattern & Analysis |
|---|---|---|
| `kSig_StatCommit` | Unique (TU 2.01+ form) | `pa_StatCommit` lives in the `.link` section (IDB `sub_14C4E6A80`). God Mode / Infinite Stamina / Spirit run through this new form; the TU 2.00 pattern (`kSig_StatCommit_Legacy`) is gone (0 hits). |
| `kSig_GameCoreGlobal` | Unique (new 22-byte pinned form) | The old 11-byte form matches 2 sites on 2.02 (true site `0x14035C0BB` + false positive `0x140663A49`). The new form pins the true site by its call-tail `90 40 38 74 24 40`; RIP target = `0x146C2D9F0` = charMgr global (`0x146C2DF18`) − `0x528`, exactly the relationship the TU 2.01 IDB recorded. |
| `kSig_EnvManager` | Unique @ `0x14340CA3B` | Virtual-call slot moved `0x40` → **`0x60`**; the field-manager member **`+0xEE0` is UNCHANGED** (TU 1.18–2.01); global `g_env = 0x146B52FF0`. A new trailing pin (`mov rax,[rcx+0x90]`) isolates the site; all 16 equivalent `mov rcx,[rip]` / `call [rax+0x60]` / `mov rcx,[rax+0xEE0]` runs in the 2.02 build resolve to the same global. Old (TU 1.18–2.01) form fails on 2.02. |
| `kSig_InvSetExpandSlots` | Unique @ `0x1420801E0` | **ABI changed in 2.02**: the dead 3rd pointer arg was dropped — `void f(holder, int* outErr, u16 bucketType, u16 count)` (bucketType now in `r8`, count in `r9`). Calling with the old 5-arg prototype leaves `nullptr` in `r8`, the type never matches, and the engine's re-stamps are never intercepted. The detour prototype must match or MinHook misroutes registers. |
| `kSig_MarkerPlayer` | Unique @ `0x1435CA7CB` | Source register of the player-pointer load changed `rsi` → `rdi` (`48 8B 06` → `48 8B 07`). Dest2 stays at `+0x1B0`; the replayed bytes and the hook contract are unchanged from TU 2.01. |
| `kSig_MarkerProtection` | Unique @ `0x14118A46E` | Protection flag displacement moved `0x454` → **`0x458`**. The trailing `48 8D 55` (`lea rdx,[rbp-..]`) discriminates the true marker site from an unrelated `C6 80 58 04` site @ `0x14117DCCF`. |
| `kSig_MarkerPattern` | Exactly 5 hits (= `kExpected_MarkerMatches`) | Sites: `0x140A29475`, `0x14170602D`, `0x141764AF3`, `0x141E4EB47`, `0x141E4EC5A`. |
| `kSig_MarkerOriginPrefix` | MULTI by design (26 raw hits image-wide) | Consumed only via `FindAllMatches(.., 64)` + origin voting in `teleport.cpp`: 21 votes → `0x146C1AE10` vs 5 votes → `0x146767930`. Never used as a unique pattern. |
| `kSig_FriendlyTrustSiteA` / `kSig_FriendlyTrustSiteB` | Unique `0x141E2C1F8` / `0x14D87BE28` | Site A inside SetNpc (`0x141E2C090`, +0x168), Site B inside SetPet (`0x14D87BCB0`, +0x178). The 2.02 record stride growth widened the `+0x40` copy from XMM to YMM (bytes `0xF8` → `0xFC`, both sites identical); the hooked `vmovups [rcx+0x20],ymm1` store sits at sig+0x14. |
| `kSig_LeaR8Rip` / `kSig_TableResolverPrologue` / `kSig_MovR8Rip` | MULTI by design | Image-wide exact counts: 150,637 / 24 / 25,786 raw hits. Consumed ONLY through `FindPatternIf` + `IsTableRef` anchor hunts (`inventory.cpp:2229`); the FieldLevelNameTableInfo and Inventory sites resolve correctly. Do not pin. |

### Carried from TU 2.01 (unique-match hooks, unchanged on 2.02)

`kSig_DamageApply`, `kSig_LocoStepper` (sub-step driver @ `0x1435C40A0` on 2.02), `kSig_TravelToNode`, `kSig_MoveUpdate`, `kSig_CombatTimingEval`, `kSig_InvGetItemQty`, `kSig_InvGetHolder`, `kSig_InvHolderInsert` (unique @ `0x142A18960`, `.data2`), `kSig_InvCommit` (@ `0x142A9A260`), `kSig_InvCommitPlacement` (@ `0x14207A2C0`), `kSig_InvCoreGlobal`, `kSig_TrItemValueCtor` (@ `0x14234F210`), `kSig_EquipBatch` + `kSig_EquipBatch_Hooked` (@ `0x142B474B0`), `kSig_EquipEffectRefresh` (@ `0x140E7BAC0`), `kSig_DyeApplyBatch` (VA `0x1409165D0`), `kSig_DyeUpsert` (@ `0x142355870`), `kSig_DyeVisualSet` (`0x1409170C0`), `kSig_DyeVisualClear` (`0x140918770`), `kSig_FrameTimerBody` (@ `0x140A541C0`), `kSig_FieldTimeRealm` / `kSig_FieldTimeTick` (@ `0x1409BC623`), `kSig_TodEngineGlobal`, `kSig_WeatherRain`/`Snow`/`Dust`, `kSig_WindPack` (@ `0x143CC9B10`), `kSig_LocStringGet`, `kSig_SetDestinationMarker_Fn` / `_Dispatcher` / `_Legacy`, `kSig_Authoritative_SetPin` / `ClearPin`, `kSig_EvaluateCrimeWantedState`, `kSig_RegisterCrimeEvent`, `kSig_FriendlySetNpc` / `SetPet` / `NpcTrustWriter`, `kSig_BarberUIThunk` / `BarberUIResolver` / `BarberUIResolveObject`, and the 9-anchor `kCharMgrAnchors` consensus (all 9 uniquely verified against `qword_146C29C88`).

### Obsolete / False Positives in 2.02 — DO NOT HOOK

| Name | Status |
|---|---|
| `kSig_JustCore` / `kSig_JustCore_Alt` | Both FAIL. The `mov rax,rsp; push rbp; push r14` prologue family is gone from the 2.02 build and no byte-distinguishable replacement exists (30 generic `vmovups xmm1,[rcx+0xB8]` sites share the old body anchor). Unreferenced by any `.cpp` — the Perfect Parry / Perfect Dodge toggles are hidden in the UI. |
| `kSig_DyeApplySlot` | **False positive.** Resolves to `0x142B41E60`, which on 2.02 is a hash/registry utility (its only caller passes `(container, u16 key, out: lea r8, counter)` and it WRITES `[out]=counter`). Calling it with dye arguments corrupts the record buffer and free-runs its inner loop — the 2.02 mount-dye freeze/crash. The per-slot path is disabled on 2.02 (`CallDyeApplySlot` returns false); the safe pipeline is `DyeUpsert` + `DyeApplyBatch` + the visual leaves. |
| `kSig_DyeRecordRemove` (both forms) | 2.02 INLINED the channel scan into `DyeApplyBatch`'s clear branch (`cmp byte [r9+rcx*8+6], r10b` @ `0x140916835`) and factored the record shift-out into a helper @ `0x140F1D1E0` taking `(vecWrapper{data,count}, INDEX)` — NOT `(entry, channel)` — so it cannot substitute the old contract. Container layout unchanged (proven by `kSig_DyeUpsert` @ `0x142355870`). Covered by the in-place removal fallback in `CallDyeRecordRemove` (`dye.cpp`). |
| `kSig_TrItemValueDtor` | Empty on 2.02. The 2.00 dtor form is not byte-identical and its generic prologue alone matches 419 sites image-wide. `inventory.cpp` safely skips the dtor when null (`inventory.cpp:4412`) — only the crash-cleanup path is inert. |
| `kSig_InvSetExpandSlots_Bad20202` | **WARNING — do not hook.** Published earlier as "TU 2.02 verified @ `0x143804870`", but that hit is an UNRELATED function (reads `[rcx+0x48]`, never writes `+0x16/+0x1A/+0x14`). Hooking it detoured the wrong target and left the real setter unhooked — the actual root cause of the 2.02 "engine re-stamps vanilla slot expansion" symptom. Kept as a reference/warning only. |
| `kSig_ResizeSocketVector_TU201` | The linker cloned the function 51× in 2.02 — every clone is BYTE-IDENTICAL for the entire 31-byte body, so a unique signature is not derivable statically. Unused by any `.cpp`; any future consumer must disambiguate via a caller-side anchor. |
| All `*_Legacy` forms | `StatCommit`, `LocoStepper`, `TravelToNode_TU200`/`_Legacy`, `InvGetItemQty`, `InvHolderInsert`, `InvCommit`, `InvCoreGlobal`, `TrItemValueCtor`, `InvCommitPlacement`, `LocStringGet_Alt1/Alt2/Legacy`, `FrameTimerBody`, `FieldTimeTick`, `WindPack`, `EquipBatch_TU200`/`_Legacy`, `DyeApplySlot`, `DyeApplyBatch_Legacy`, `DyeUpsert_TU116`, `DyeVisualSet`, `DyeVisualClear`, `FriendlySetNpc`/`SetPet`, `FriendlyAlertDisp`, `EquipEffectRefresh`, `GameCoreGlobal_TU201` — 0 hits in the final 2.02 scan; kept in `offsets.h` for reference only. |

---

## 2. `TrItemValue` Structure Layout (Derived from ctor `0x14234F210`; unchanged from 2.00 for modded fields)

| Offset | Field Type | Comparison vs 2.00 |
|---|---|---|
| `+0x00` | `int64_t` InstanceId (`-1` out of the ctor) | Identical |
| `+0x08` | `uint16_t` TypeId | Identical |
| `+0x0A` | `uint16_t` Refine/Subtype (source `def+0x218`) | Identical |
| `+0x40` | `uint16_t` Durability (source `def+0x400`) | Identical |
| `+0x48/+0x50` | `int64_t` ×1000 (source `def+0x1C8/0x1D0`) | Identical |
| `+0x60` | Socket vector pointer | **Identical** |
| `+0x68` | `uint32_t` Size | **Identical** |
| `+0x6C` | `uint32_t` Capacity | **Identical** |
| `+0x70` | Unlocked count — ⚠️ **LOW BYTE ONLY**; upper bytes contain independent flags (live value: `0xFFFFFF02`) | ⚠️ Writing a full DWORD will corrupt flags |
| `+0x78/+0x80` | Dye data pointer / count | Identical |
| `Record 6B` | `GearId` (u16) · `Marker` (u16) · `Index` (u8) · `State` (u8) | Identical |

**Equip-entry stride note (2.02)**: the equip component table is detected at runtime by scoring two candidate layouts — primary **208-byte stride (`0xD0`) with slot tag at `+0xC8`** (TU 2.00 / 2.01 / 2.02 family) and secondary 200-byte stride (`0xC8`) with tag at `+0xC0`. The same dual-layout detection runs in `dye.cpp`, `equipment.cpp`, and `player.cpp`. Working allocation stays `0x108`.

---

## 3. `ItemDef` Layout (2.02)

| Offset | Field | Comparison vs 2.00 |
|---|---|---|
| `+0x18` | `int64_t` MaxStackCount | ✅ Valid |
| `+0x111` | `uint8_t` ApplyMaxStackCap | ✅ Valid |
| `+0x428` | `uint16_t` **BucketType** | ✅ **Unchanged in 2.02** — the `revision >= 2625` gate and the `+0x428` offset carry over. The 2.02 setter at `0x1420801E0` re-confirms the bucket layout is untouched: type `+0x10`, used `+0x12`, cap `+0x14`, delta accumulators `+0x16/+0x18`, expansion `_varyExpandSlotCount` `+0x1A`. |
| `+0x220/+0x228` | Default socket array ptr/count | Unchanged |
| `+0x08/+0x20/+0x90/+0x210/+0x350` | Key / Name / Icons / Tier / Groups | ✅ Fully valid (Catalog browsing normal) |

---

## 4. In-Game Localization Engine (2.02 — unchanged)

```cpp
off  = *(uint32_t*)(provider + 0x18);      // 0xFFFFFFFF until interned
data = *(char**)(locMgr + 0x58);           // blob pointer
size = *(uint32_t*)(locMgr + 0x60);         // used bytes
name = (off < size) ? (data + off) : "";
```

`kSig_LocStringGet` still matches uniquely; the `_Alt1/_Alt2/_Legacy` forms are obsolete (0 hits on 2.02).

---

## 5. Money Getters in 2.02

- **Legacy display hooks** at `gameBase + 0x16077B0 / 0x16078C0 / 0x16081D0` are **skipped on all TU 2.00+ builds** (`revision < 2625` gate in `inventory.cpp` — on 2.00+ these offsets point at arbitrary/invalid code).
- The 2.00-era "candidate wrappers" (`0x144899FB0` / `0x144899FE0`, lookup helper `0x1402ED7A0`, worker `0x14115BB10`) were **never re-derived on 2.02** — treat them as unresolved. Wallet display hooks remain disabled on TU 2.00+; the inventory quantity editor path is the supported route.

---

## 6. Resolved Crashes & Stability Fixes (2.02)

1. **"Engine re-stamps vanilla slot expansion"**:
   - *Cause*: an earlier 2.02 build hooked `0x143804870` via the misleading 5-arg sig — an unrelated function; the real setter stayed unhooked and the engine's own server-side expansion sync kept re-stamping vanilla values.
   - *Fix*: the 4-arg 2.02 setter (`kSig_InvSetExpandSlots` @ `0x1420801E0`) is hooked with a matching detour prototype; substituting the count inside the hook makes the engine's own re-stamps apply the override.
2. **Mount-dye freeze / crash**:
   - *Cause*: `kSig_DyeApplySlot` resolves to a hash/registry utility on 2.02 (false positive); calling it with dye arguments corrupted the record buffer.
   - *Fix*: per-slot apply path disabled on 2.02; visuals ride on `DyeApplyBatch` (possessed player) + `DyeVisualSet` leaves, data writes on `DyeUpsert`.
3. **Perfect Parry / Dodge toggles unavailable**:
   - *Cause*: `kSig_JustCore` family gone from the 2.02 build.
   - *Fix*: toggles hidden in the UI; no blind re-hook shipped.
4. **Render-chain faults (90k+ caught exceptions per session)**:
   - *Cause*: 2.02 `DyeVisualSet`/`DyeVisualClear` and `DyeApplyBatch` dereference deeper chains (`[actor+0x88]`, `[actor+0x68]`, `[pawn+0x68]`) before their first NULL test; stale companion/mount bodies fault `0xC0000005`.
   - *Fix*: guarded probes of the exact qwords the leaves touch (`IsCompFaulted` / `IsRenderComp`) before calling.
5. **Crash-cleanup dtor inert**:
   - *Cause*: no derivable unique `TrItemValueDtor` on 2.02.
   - *Fix*: guarded skip when null (crash-cleanup path only).

---

## 7. Community & Testing Notes (2.02)

- Base engine hooks (damage/locomotion/inventory/teleport) are solid on 2.02; the signature set was re-audited with a full-image final scan.
- Dye on companions/mounts is **data-persistent** but visual refresh on 2.02 comes from the auto-restore replay on summon/reload (per-slot live apply is disabled — see §6.2).
- Persistent freezes on individual machines after dirty crashes are typically caused by **corrupted save files**, outside the mod's scope.

---

## 8. Safe Mode Diagnostic Flags — HISTORICAL (TU 2.00 era)

The `Trinity_SafeMode.txt` bitfield diagnostic shipped with the TU 2.00 emergency build and is **not part of the current 2.02 source tree**. The subsystem/tick-bypass bits documented in the 2.00 revision of this file no longer exist; do not rely on them. Current diagnostics are the `Trinity.log` signature-resolution lines (`version:`, `inventory:`, per-hook install results).

---

## 9. Reverse Engineering & Binary Inspection Tools

- `scripts/audit_tu201_fixes.py`: audits the load-bearing signature set against the live EXE (uniqueness per section) — update the pattern list and re-run after every game patch.
- `scripts/`: live-session inspectors (party/equip resolution, mount entities, RTTI, dye slots, table dumps).
- `re_scripts/`: static disassembly and pattern hunters (Capstone-based, per-target one-off scripts).
- `scratch/` + `scratch/scripts/`: session probes, struct-layout dumps, and binary diffs.
- `tools/deploy_master.ps1`: deployment helper.
- **Skill**: `.agents/skills/crimson-binary-inspector` (`inspect_pe`, `deep_entity_pointer_routing`, etc.).
- **Source of truth**: `src/game/offsets.h` — every signature/offset fact above is annotated in place, including per-TU status and the obsolete-pattern warnings.
