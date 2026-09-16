# Trinity — Title Update 2.02.00 Offset & Data Reference (PE 1.0.0.2850)

> **Game Build**: Crimson Desert TU **2.02.00** · PE Revision **`1.0.0.2850`**  
> **Revision Ladder** (runtime fingerprint source, `src/core/version_detect.cpp`): `1.0.0.2474` = TU 1.18.02 · `1.0.0.2625` = TU 2.00.00 · `1.0.0.2658` = TU 2.00.01 · `1.0.0.2692` = TU 2.00.02 · `1.0.0.2760` = TU 2.01.00 · **`1.0.0.2850` = TU 2.02.00**  
> **Preferred Image Base**: `0x140000000` · PE timestamp **`0x6AA22ABB`** for the inspected game build. Addresses below are preferred VAs, not reusable heap pointers.
>
> **Updated**: 2026-09-16, through the material-control fix, mod v1.4.1. The filename retains its TU200 name for existing links.
>
> **Scanner**: `src/mem/scanner.cpp` primarily selects committed, readable spans with `VirtualQuery`, excluding guard/no-access pages. If no spans are found, a PE-section-header fallback is used. Readable spans can still contain unrelated matching code: a unique AOB is not an ABI or ownership proof.

---

## 1. Array of Bytes (AOB) signatures and current call sites

The cross-version scan counts below are historical audit observations, not a new full-image scan performed for this documentation update. Recent source and live-log corrections take precedence where older notes disagree.

### Changed & Re-Verified for 2.02

| Signature | TU 2.02 Status | Pattern & Analysis |
|---|---|---|
| `kSig_StatCommit` | Installed in the inspected TU 2.02 session | Runtime log: `0x14C45BAC0`. Earlier notes listed `sub_14C4E6A80`; do not use that older VA as the current target. Stat ownership/count validation is separate from hook installation. |
| `kSig_GameCoreGlobal` | Unique (new 22-byte pinned form) | The old 11-byte form matches 2 sites on 2.02 (true site `0x14035C0BB` + false positive `0x140663A49`). The new form pins the true site by its call-tail `90 40 38 74 24 40`; RIP target = `0x146C2D9F0` = charMgr global (`0x146C2DF18`) − `0x528`, exactly the relationship the TU 2.01 IDB recorded. |
| `kSig_EnvManager` | Unique @ `0x14340CA3B` | Virtual-call slot moved `0x40` → **`0x60`**; the field-manager member **`+0xEE0` is UNCHANGED** (TU 1.18–2.01); global `g_env = 0x146B52FF0`. A new trailing pin (`mov rax,[rcx+0x90]`) isolates the site; all 16 equivalent `mov rcx,[rip]` / `call [rax+0x60]` / `mov rcx,[rax+0xEE0]` runs in the 2.02 build resolve to the same global. Old (TU 1.18–2.01) form fails on 2.02. |
| `kSig_InvSetExpandSlots` | Unique @ `0x1420801E0` | **ABI changed in 2.02**: the dead 3rd pointer arg was dropped — `void f(holder, int* outErr, u16 bucketType, u16 count)` (bucketType now in `r8`, count in `r9`). Calling with the old 5-arg prototype leaves `nullptr` in `r8`, the type never matches, and the engine's re-stamps are never intercepted. The detour prototype must match or MinHook misroutes registers. |
| `kSig_MarkerPlayer` | Unique @ `0x1435CA7CB` | Source register of the player-pointer load changed `rsi` → `rdi` (`48 8B 06` → `48 8B 07`). Dest2 stays at `+0x1B0`; the replayed bytes and the hook contract are unchanged from TU 2.01. |
| `kSig_MarkerProtection` | Unique @ `0x14118A46E` | Protection flag displacement moved `0x454` → **`0x458`**. The trailing `48 8D 55` (`lea rdx,[rbp-..]`) discriminates the true marker site from an unrelated `C6 80 58 04` site @ `0x14117DCCF`. |
| `kSig_MarkerPattern` | Exactly 5 hits (= `kExpected_MarkerMatches`) | Sites: `0x140A29475`, `0x14170602D`, `0x141764AF3`, `0x141E4EB47`, `0x141E4EC5A`. |
| `kSig_MarkerOriginPrefix` | MULTI by design (26 raw hits image-wide) | Consumed only via `FindAllMatches(.., 64)` + origin voting in `teleport.cpp`: 21 votes → `0x146C1AE10` vs 5 votes → `0x146767930`. Never used as a unique pattern. |
| `kSig_FriendlyTrustSiteA` / `kSig_FriendlyTrustSiteB` | Historical copy-site observations `0x141E2C1F8` / `0x14D87BE28` | Site A is inside SetNpc (`0x141E2C090`, +0x168), site B inside SetPet (`0x14D87BCB0`, +0x178). These body-store observations are not the current hook installation points: `friendly.cpp` installs SetNpc/SetPet prologue detours. |
| `kSig_LeaR8Rip` / `kSig_TableResolverPrologue` / `kSig_MovR8Rip` | MULTI by design | Image-wide exact counts: 150,637 / 24 / 25,786 raw hits. Consumed ONLY through `FindPatternIf` + `IsTableRef` anchor hunts (`inventory.cpp:2229`); the FieldLevelNameTableInfo and Inventory sites resolve correctly. Do not pin. |

### Recently observed TU 2.02 targets

| Function / global | Preferred VA | Evidence / role |
|---|---|---|
| Damage dispatcher | `0x141719850` | Runtime hook log; outgoing HP status is resolved from native realm tables |
| Just-window evaluator | `0x1407FCC80` | `kSig_JustWindowEval`; current Easy Parry/Evade path |
| Item-value constructor | `0x1423507B0` | Runtime log and socket-layout notes; supersedes older `0x14234F210` entry |
| Dye batch | `0x1409165D0` | Client dye-ack path |
| Dye upsert | `0x142355870` | Native item-record insert/replacement |
| Dye visual set / clear | `0x1409170C0` / `0x140918770` | Client render leaves, gated by native RTTI/render chain |
| Dye client-registry global | `0x146C2D9E8` | Unique dye-ack anchor; runtime discovery still resolves it |
| Dye record → material parameters | `0x14074A930` | Reads record material at +4, RGB/alpha +7..+10, condition +11 |
| Material lookup consumer | `0x14074AA60` | Uses converted material key and per-part definition |
| Frame timer update | `0x140A54C10` | Recent runtime log; older `0x140A541C0` is not the latest observed entry |

Earlier scan notes recorded inventory insert/commit/placement at `0x142A18960` / `0x142A9A260` / `0x14207A2C0`, equip batch at `0x142B474B0`, and wind pack at `0x143CC9B10`. These are version-specific historical anchors, not permission to call them without validating the current signature, caller, and prototype. Nine-anchor manager consensus does not make an old absolute global portable.

### Obsolete / False Positives in 2.02 — DO NOT HOOK

| Name | Status |
|---|---|
| `kSig_JustCore` / `kSig_JustCore_Alt` | Obsolete path. This does **not** disable the current Easy Parry/Evade controls: `kSig_JustWindowEval` is installed separately. |
| `kSig_EquipEffectRefresh` on TU 2.02 | Disabled. The hit at `0x140E7C580` is **Challenge Description UI**, not equipment refresh. Earlier notes listing `0x140E7BAC0` as a carried-over refresh contract are superseded. |
| `kSig_RegisterCrimeEvent` | Not installed. The earlier `uint32_t` argument truncated a native `const char*`, causing a read fault at game `+0x1E4F217`. No Bounty retains the separate wanted evaluator and price-table handling. |
| `kSig_DyeApplySlot` | **False positive.** `0x142B41E60` is a hash/registry utility with a different output-pointer contract. `CallDyeApplySlot` remains disabled. Character visuals use the validated client batch/leaves; mount visuals use the mount's own client leaves, not a player batch. |
| `kSig_DyeRecordRemove` (both forms) | Historical scan found an inlined channel search and a different `(vecWrapper, INDEX)` shift helper at `0x140F1D1E0`; it is not a replacement for `(entry, channel)`. Current explicit clears store a clear record through the request pipeline and use the client visual-clear leaf, preserving other channels. |
| `kSig_TrItemValueDtor` | Unresolved/empty. Cleanup is skipped when null; do not interpret this as a proof of complete allocation cleanup in the add-item path. |
| `kSig_InvSetExpandSlots_Bad20202` | **WARNING — do not hook.** Published earlier as "TU 2.02 verified @ `0x143804870`", but that hit is an UNRELATED function (reads `[rcx+0x48]`, never writes `+0x16/+0x1A/+0x14`). Hooking it detoured the wrong target and left the real setter unhooked — the actual root cause of the 2.02 "engine re-stamps vanilla slot expansion" symptom. Kept as a reference/warning only. |
| `kSig_ResizeSocketVector_TU201` | The linker cloned the function 51× in 2.02 — every clone is BYTE-IDENTICAL for the entire 31-byte body, so a unique signature is not derivable statically. Unused by any `.cpp`; any future consumer must disambiguate via a caller-side anchor. |
| Legacy forms in earlier audits | Earlier scans reported zero hits for many legacy variants. Several remain active source fallbacks on primary-resolution failure, so they are not all reference-only. Recheck the current caller and version gate; especially do not fall back to a wrong-ABI target when the modern signature resolves. |

---

## 2. Item, equipment, and socket layout

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
| `Record 6B` | `gear` u16 +0 · `durability` u16 +2 · `index` u8 +4 · `padding` u8 +5 | +5 is not a filled/empty flag; preserve +2/+5 |

**Equipment (TU 2.02):** `equipment_table.h` requires exact MSVC RTTI for Client/Server/Common equipment, a primary COL, and the current backlink `comp+8 → owner+0x68 → sub+0x38 == comp`. The table is **comp+0x90 → descriptor; descriptor+8 → array; descriptor+0x10 → u32 count**, count 1–64, stride **0xD0**, tag **+0xC8**. Native consumers do not use a scored alternate layout. Only immutable vtable classification is cached; heap ownership is reread.

**Inventory slots** use `GetSlotStride()` (`0xC8` on this build); the item construction buffer is `0x108`. These are distinct from equipped-entry stride.

**Sockets:** `socket_layout.h` selects modern `+0x60` or explicit legacy `+0x58`. Require `unlocked <= size <= 5`, `size <= capacity`, a valid allocation range, and a fresh header check. The implementation bounds capacity at 1024; it does not manufacture vector size/capacity or assume every item has five constructed records. Unlock writes use a byte at `+0x70`; neighboring bytes are preserved.

**Mount slots:** native mount owner descriptor tag **5**; local equipment tags are **0 Chamfron, 1 Horse Armor, 2 Saddle, 3 Stirrups, 4 Horseshoes**. A native mount owner plus positive exact item instance is required. Old character-table tags 14/22–25 are not the TU 2.02 mount-local table.

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

Conceptual layout excerpt only; production reads use guarded access and validate the provider/pool pointers and bounds:

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
   - *Fix*: wrong ApplySlot disabled; mount channels use exact mount client targets and visual leaves, one per visit at least 16 ms apart. Character batch calls stay on validated client render components.
3. **Easy Parry / Evade**:
   - Current `kSig_JustWindowEval` at `0x1407FCC80` supplies a separate timing-assist path; obsolete JustCore signatures are not reused.
4. **Render-chain faults (90k+ caught exceptions per session)**:
   - *Cause*: 2.02 `DyeVisualSet`/`DyeVisualClear` and `DyeApplyBatch` dereference deeper chains (`[actor+0x88]`, `[actor+0x68]`, `[pawn+0x68]`) before their first NULL test; stale companion/mount bodies fault `0xC0000005`.
   - *Fix*: guarded probes of the exact qwords the leaves touch (`IsCompFaulted` / `IsRenderComp`) before calling.
5. **Crash-cleanup dtor inert**:
   - *Cause*: no derivable unique `TrItemValueDtor` on 2.02.
   - *Current handling*: skip when null; allocation cleanup is still an unresolved part of that path.
6. **Refine / Abyss Gear retries and frame drops**:
   - Successful unchanged component readbacks previously invalidated the whole equipment snapshot on each retry. UI rebuilds now remain capped at 200 ms; source/selection/world validation still applies to writes.
   - `EquipmentEditScan` resumes up to 2048 slots/8 headers per slice with a cooperative ~1 ms scan budget. Realms alternate; progressing requests resume on a pump at least 16 ms apart, unavailable/invalid paths back off 500 ms. This is not a hard bound on the whole tick.
   - Completion and match counts reset on mutation/layout changes. `Synced` requires distinct client/server verification and both holder traversals complete, not a save acknowledgement. Historical equipment-profile writes are debounced to a value-copy worker.
7. **Material / Condition controls**:
   - `touched` was ignored and the old retouch flag was never armed. All zones was also skipped. Controls now queue a 350 ms debounced retouch with a per-zone payload mask and field-preserving updates; see §10.

---

## 7. Community & Testing Notes (2.02)

- Earlier reload-stat and mount-visual changes were confirmed working by the user. The later frame-time and material patches have local build/fixture coverage; their latest visual/FPS results still need in-game confirmation.
- `DyeVisualSet` and `DyeVisualClear` remain active, including staged mount updates. Disabling the unrelated `DyeApplySlot` does not disable all live per-channel rendering.
- Diagnose freezes from a matching build stamp, log, and reproduction. Neither an incomplete edit nor a freeze alone establishes a corrupted save.
- Material-fix build `Sep 16 2026 20:47:43`: 8/8 CTest suites, including 21 source guards, passed. See [the report](2026-09-16_fix-dye-material-report.md).

---

## 8. Safe Mode Diagnostic Flags — HISTORICAL (TU 2.00 era)

The `Trinity_SafeMode.txt` bitfield diagnostic shipped with the TU 2.00 emergency build and is **not part of the current 2.02 source tree**. The subsystem/tick-bypass bits documented in the 2.00 revision of this file no longer exist; do not rely on them. Current diagnostics are the `Trinity.log` signature-resolution lines (`version:`, `inventory:`, per-hook install results).

---

## 9. Reverse Engineering & Binary Inspection Tools

- [scripts/inspect_dye_material_native.py](scripts/inspect_dye_material_native.py): read-only disk disassembly with mmap/Capstone; accepts `--exe`, `--va`, and `--bytes`.
- `scripts/gen_dye_data.py` / `scripts/gen_dye_slots.py`: dye data generation utilities; inspect their inputs before running.
- `re_scripts/`, `scratch/`, and `tools/`: historical research helpers; check actual availability and target version before use.
- **Implementation references**: `equipment_table.h`, `socket_layout.h`, `stat_view.h`, `actor_registry.h`, `dye_record.h`, and their consumers. `offsets.h` retains older commentary that must be checked against these contracts and current binary evidence.

## 10. Dye record, retouch, and profiles

| Record offset | Type / role |
|---|---|
| +0 | u32 color-group key |
| +4 | u16 material template; UI 0 maps to **0xFFFF natural**, UI 1–10 passes through |
| +6 | u8 channel 0–11 |
| +7/+8/+9/+10 | R/G/B/alpha bytes |
| +11 | condition byte, 0 pristine through 127 worn; high-bit legacy sentinel is distinct |
| +12 | retained native payload byte |
| +13..+15 | storage tail, excluded from native replacement equality |

Native upsert replacement at `0x1423558AE..0x1423558EB` copies **13 payload bytes**. `SameDyePayload` ignores tail differences to avoid endless replay. A clear record uses natural material plus zero color/alpha/+12 and a high-bit condition sentinel; natural material alone is not a clear.

`Dye::Retouch` captures the current record set on the game thread after request revalidation, modifies only requested material/condition bytes, and preserves each zone's own RGB/group/alpha. Missing/cleared zones are skipped. `RequestedDyeMask` drives native batch/upsert/visual work and readback, including sparse mount channels. Only an unstarted retouch may be coalesced during the 350 ms debounce.

Named profiles use `Trinity_DyeProfiles.dat`: magic `TRDYPR01`, 300 bytes per profile, FNV32 integrity checksum, 64-profile limit. They store 13 bytes per channel and restore all 12 zones, explicitly clearing absent saved zones. Compatibility is exact game revision/typeId/player-or-mount mode. File replacement uses a temporary file, `FlushFileBuffers`, and `MoveFileExW`; a checksum is an integrity check, not a security signature.

## 11. Reload-aware stats and client registry

`StatView` follows `owner+0x68 → actor+0x20 → marker+0x18 → root`, requiring `*root == marker`. Array at **root+0x58**, u32 count at **root+0x60**, entry stride **0x90**, and **u16 status IDs**. Current samples had 20 entries; count is read each time and bounded to 1–256. Retained pointers must still belong to the current array before a write. HP dispatcher IDs are resolved from native realm tables; IDs 48/49 must not be treated as mount stamina.

Client registry: `*global → root+0x38 → registry+8 → map`. Map bucket count +0x88, bucket array +0x98, node-pointer array +0xA0. Bucket stride **0x100**, up to 31 `{u32 key,u32 index}` pairs at +8; node+4 repeats key, node+8 is owner. Validate the current pair/index/node/owner binding, not just a readable cached owner. On TU 2.02 **owner+0x50 is an entity ID, not a protagonist index**.

## 12. Performance diagnostics

`perfLogging=0` by default; enable through SYSTEM → Performance Diagnostics. Ten-second reports show per-subsystem **maximum measured invocation time**, emitted when a maximum reaches 2 ms. They are not frame averages and their columns must not be summed. The overlay uses four independently fenced submission/upload slots (`overlay_sync.h`); synchronization fixture success is not validation of every Frame Generation/HDR setup.
