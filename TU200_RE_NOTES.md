# Title Update 2.02.00 (PE 1.0.0.2850) Reverse-Engineering Notes

> **Inspected binary**: TU 2.02.00, PE timestamp `0x6AA22ABB` · **Preferred image base**: `0x140000000`.
>
> **Revision Ladder** (runtime fingerprint source, `src/core/version_detect.cpp`): `1.0.0.2474` = TU 1.18.02 · `1.0.0.2625` = TU 2.00.00 · `1.0.0.2658` = TU 2.00.01 · `1.0.0.2692` = TU 2.00.02 · `1.0.0.2760` = TU 2.01.00 · **`1.0.0.2850` = TU 2.02.00** (detected at `revision >= 2800`).  
> **Updated**: 2026-09-16, mod v1.4.1 through the material-control fix; the TU200 filename is retained for existing links.
>
> **Scanner**: primarily VirtualQuery-based readable committed spans, with a PE-section fallback if no spans are found. A match still needs caller/ABI validation.
>
> **Evidence order**: verified native behavior and current validators/consumers take precedence over historical offsets commentary. Use [the offset reference](README_TU200_OFFSETS.md) and [architecture guide](REVERSE_ENGINEERING_GUIDE.md) together.

---

## 1. Feature Status (2.02)

- **God Mode / Infinite Stamina / Spirit**: reload-aware client/server stat discovery follows actual array/count and validates retained entries. Recent stat-commit hook log: `0x14C45BAC0`; an earlier IDB VA is not the current runtime target. User confirmed the reload-stat fixes working.
- **Slot-Expansion Override (Add-slot)**: **OPERATIONAL** — the 2.02 setter (`kSig_InvSetExpandSlots` @ `0x1420801E0`) is hooked with the new 4-arg ABI (see §2). Substituting the count inside the hook makes the engine's own re-stamps apply the override.
- **Game-thread work**: movement-driven Player/World/Inventory/Dye/Equipment pumps remain active. Bounded scan counts and nonblocking guards reduce work; they do not certify all paths as hitch-free.
- **Render gating**: exact native client equipment, current item identity, render chains and fault cooldowns gate dye leaves. SEH is fault containment, not ownership validation.
- **Easy Parry / Easy Evade**: current controls use `kSig_JustWindowEval` at `0x1407FCC80` plus existing damage-assist branches. The old JustCore path is obsolete, not the current feature.
- **Game Speed**: direct FrameTimerUpdate hook, recently logged at `0x140A54C10`. The earlier `0x140A541C0` entry is superseded for this session.
- **Refine / Abyss Gear**: queued edits, fair bounded holder slices, capped UI refresh and worker profile I/O. Local tests passed; the latest frame-time change still needs user gameplay confirmation.
- **Material / Condition**: disconnected controls now queue a 350 ms debounced per-zone retouch; single/all-zone colors are preserved. Local payload tests passed; native appearance still needs retesting on the latest build.
- **Dye Profiles**: explicit capture/apply/CRUD, exact revision/type/mode compatibility, including natural zones. This is separate from automatic dye replay and from the disabled historical equipment-profile replay.

---

## 2. Signature Changes (2.01 → 2.02)

Counts and broad carry-over statements from the original scan are historical. This documentation update did not rerun every signature across the whole EXE.

- **`kSig_GameCoreGlobal`**: re-pinned with a new 22-byte form. The old 11-byte shape (`48 8B 0D ?? ?? ?? ?? 48 8B 49 58 E8`) matches 2 sites on 2.02 — true site `0x14035C0BB` + false positive `0x140663A49`. The new form pins the call tail (`90 40 38 74 24 40`); RIP target `0x146C2D9F0` = charMgr global (`0x146C2DF18`) − `0x528`, confirming the `[rcx+0x58]` deref + rip-resolve contract is unchanged.
- **`kSig_EnvManager`**: new form (unique @ `0x14340CA3B`). The virtual-call slot moved **`0x40` → `0x60`**; the field-manager member **`+0xEE0` is unchanged**; global `g_env = 0x146B52FF0`. A trailing pin (`mov rax,[rcx+0x90]`) isolates the site; all 16 equivalent runs in the image resolve to the same global.
- **`kSig_InvSetExpandSlots`**: **ABI swapped** — TU ≤2.01 was `void f(holder, int* outErr, void* unused, u16 bucketType, u16 count)` (5 args); TU 2.02 dropped the dead 3rd pointer arg: `void f(holder, int* outErr, u16 bucketType, u16 count)` — bucketType in `r8`, count in `r9`. The detour prototype must match or MinHook misroutes registers.
- **`kSig_MarkerPlayer`**: source register of the player-pointer load changed `rsi` → `rdi` (`48 8B 06` → `48 8B 07`); unique @ `0x1435CA7CB`. Dest2 stays `+0x1B0`; hook contract unchanged.
- **`kSig_MarkerProtection`**: protection flag displacement moved `0x454` → **`0x458`**; unique @ `0x14118A46E`. (No match in 2.00 — restored since 2.01 with the updated pattern.)
- **`kSig_FriendlyTrustSiteA/B`**: historical body-copy sites `0x141E2C1F8` (SetNpc +0x168) and `0x14D87BE28` (SetPet +0x178). Current `friendly.cpp` installs SetNpc/SetPet **prologue** detours rather than those copy-tail hooks.
- **`kSig_MarkerPattern`**: re-verified — exactly 5 hits (`0x140A29475`, `0x14170602D`, `0x141764AF3`, `0x141E4EB47`, `0x141E4EC5A`). **`kSig_MarkerOriginPrefix`**: MULTI by design (26 raw hits) — consumed via `FindAllMatches(.., 64)` + origin voting (21 votes → `0x146C1AE10`, 5 votes → `0x146767930`).
- **Recent dye evidence**: batch `0x1409165D0`, upsert `0x142355870`, visual set `0x1409170C0`, visual clear `0x140918770`; client registry global `0x146C2D9E8` from the unique dye-ack anchor. Record converter `0x14074A930` and downstream material lookup `0x14074AA60` were disassembled for the material task.
- **Corrected constructor**: current runtime logs resolve `TrItemValue` construction to `0x1423507B0`, not the older `0x14234F210` note.
- **Historical carry-over anchors**: locomotion `0x1435C40A0`, inventory insert/commit/placement `0x142A18960` / `0x142A9A260` / `0x14207A2C0`, equip batch `0x142B474B0`, and wind pack `0x143CC9B10`. Validate current callers/prototypes before using these addresses; a scan count does not establish safe engine-call arguments.
- **Manager consensus**: nine caller-side anchors vote on the current global. Earlier absolute globals must not be copied across builds.

---

## 3. Deprecated / False Positives in 2.02

- **`kSig_JustCore` / `_Alt`**: both forms FAIL — the `mov rax,rsp; push rbp; push r14` prologue family is gone; 30 generic sites share the old body anchor, so no byte-distinguishable replacement exists. Unreferenced by any `.cpp`.
- **`kSig_DyeApplySlot`**: false-positive hash/registry function at `0x142B41E60`, with a different output-pointer contract. `CallDyeApplySlot` stays disabled. Genuine client visual leaves are separate; mount requests do not use the player dye batch.
- **`kSig_EquipEffectRefresh`**: disabled for revision >=2800. The `0x140E7C580` hit is Challenge Description UI. Earlier claims that `0x140E7BAC0` was a valid carried-over equipment refresh are superseded.
- **`kSig_RegisterCrimeEvent`**: removed from installation after a `uint32_t`/`const char*` ABI mismatch truncated pointers and faulted at `+0x1E4F217`. Keep the independent wanted evaluator and WantedInfo logic; do not reinstall the old wrapper.
- **`kSig_DyeRecordRemove`**: earlier scans found an inlined search and a different indexed-shift helper at `0x140F1D1E0`; that helper is not a substitute for the old API. Current manual clears use explicit per-channel clear records and the native client clear leaf.
- **`kSig_TrItemValueDtor`**: empty/unresolved on this build. The null check skips cleanup; this is not evidence that every native allocation is otherwise freed correctly.
- **`kSig_InvSetExpandSlots_Bad20202`**: WARNING — published earlier as "TU 2.02 verified @ `0x143804870`", but that hit is an unrelated function (reads `[rcx+0x48]`, never writes `+0x16/+0x1A/+0x14`). Hooking it was the actual root cause of the 2.02 "engine re-stamps vanilla expansion" symptom. Kept as a reference/warning only.
- **`kSig_ResizeSocketVector_TU201`**: cloned 51× by the linker in 2.02, all byte-identical for the full 31-byte body — no unique signature derivable statically. Unused; future consumers need a caller-side anchor.
- **Legacy variants**: many had zero hits in earlier audits, but several remain code fallbacks. A zero-hit observation for one EXE is not a guarantee that a future fallback will resolve to the right ABI; inspect consumers/version gates and validate callers.
- **Legacy Money Getters**: hardcoded addresses `0x16077B0 / 0x16078C0 / 0x16081D0` remain skipped on all TU 2.00+ (`revision < 2625` gate); the 2.00 candidate wrappers (`0x144899FB0`/`0x144899FE0`, helper `0x1402ED7A0`, worker `0x14115BB10`) were never re-derived on 2.02 — treat as unresolved.

---

## 4. Structure Layout Discoveries (2.02)

`TrItemValue` fields used on this build (current constructor observation `0x1423507B0`):
```text
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

**Equip component table**: exact Client/Server/Common MSVC RTTI and primary COL, plus `comp+8 → owner+0x68 → sub+0x38 == comp`. Native table at comp+0x90, descriptor+8 array and descriptor+0x10 count (1–64), stride **0xD0**, tag **+0xC8**. Native consumers no longer score alternate fields/strides.

**Socket record**: 6 bytes, gear u16 +0, durability u16 +2, index u8 +4, padding +5. Preserve durability/padding; do not treat +5 as state. Require unlocked <= size <=5 and size <=capacity. Use explicit modern/legacy layout, never invent vector size/capacity.

**Stat array**: owner+0x68 → actor+0x20 → marker+0x18 → root, with root backlink to marker. Array root+0x58, count u32 root+0x60, stride **0x90**, status **WORD**. Samples had 20 entries; scanner reads count rather than assuming 64. Do not classify 48/49 as mount stamina.

**Identity**: TU 2.02 owner+0x50 is an entity ID, not protagonist index. Current-world registry membership/backlinks and equipment-derived identity are separate checks.

**Mount gear**: descriptor tag 5 with positive-instance native equipment. Local tags **0 Chamfron, 1 Horse Armor, 2 Saddle, 3 Stirrups, 4 Horseshoes** supersede character-style tags 14/22–25 for mount tables.

**Trust record, historical field notes**: key u32 +0, group u16 +4, trust i64 +0x28 were recorded in earlier audits. The old 0x58 total-size claim is not a current layout guarantee. Current source reads/writes +0x20 and +0x28 in setter wrappers; this documentation update does not revalidate that entire record layout.

**TLS realm flag**: TU 2.01+ (PE rev >= 2760) = **509 (`0x1FD`)**; TU 2.00 and earlier = 498 (`0x1F2`).

---

## 5. Analysis & Reverse Engineering Tools

- [scripts/inspect_dye_material_native.py](scripts/inspect_dye_material_native.py): read-only mmap/Capstone disassembly; specify the current EXE and preferred VA.
- `tests/dye_record_tests.cpp`: 13-byte payload comparison, sparse/all-zone retouch, natural/empty zones and field preservation.
- `tests/inventory_scan_tests.cpp`: bounded traversal, time-yield, mutation/reallocation rejection, large-holder progress.
- `tests/equipment_table_tests.cpp`, `tests/socket_layout_tests.cpp`: native equipment/stat/registry/socket fixtures.
- `tests/reload_source_tests.py`: lifecycle and no-unverified-native-call guards.
- `re_scripts/`, `scratch/`, `tools/`: research artifacts may be session-specific or absent in another checkout. Check the actual file before citing a command as reproducible.

---

## 6. Live Session Findings (2.02)

- **Mount dye**: manual updates and background replay resolve the mount's client renderer and stage one channel per visit. The user confirmed the earlier mount-visual fix working. `visual=FFF` records wrapper completion for 12 channels, not proof all materials/pixels changed.
- **"Engine re-stamps vanilla slot expansion" (root-caused)**: the earlier 2.02 build hooked the wrong function via a misleading 5-arg signature (`0x143804870`); the real setter stayed unhooked, so the engine's server-side expansion sync kept re-stamping vanilla values. Fixed by hooking the true 4-arg setter and substituting the count inside the hook.
- **Free-space gate**: remains unhooked. Inspect holder used/cap/array size and native planner errors rather than assigning every refusal to a corrupted save. Background accounting is sliced and only corrects used count downward after validation.
- **Socket / dye containers unchanged**: `kSig_DyeUpsert` @ `0x142355870` reads `[entry+0x78]` data / `[entry+0x80]` count with 16-byte records and the channel byte at record+6; the 6-byte socket-record injection path and the unlocked-count LOW-BYTE rule (`+0x70`, flags `0xFFFFFF02`) both remain valid.
- **Equipment routing**: source discovery uses tracked/live/profile roots plus bounded client replica discovery. Queued refinement/socket edits use resumable holder cursors, not exhaustive CharacterAddrs/FindAndApplyAllHolders loops. TLS does not establish a different realm for the same component pointer.
- **Barber UI, historical investigation**: the earlier bind observation alone did not open the screen. It is not a current supported-feature claim or a portable handle value.

## 7. Refinement and Abyss Gear frame-time investigation

The 19:30–19:32 session (build 18:16:38, PID 38520) logged Damiane Main Hand type 6382, instance `0x99`, with two component readbacks but incomplete synchronization around the 15-second deadline. A later read-only snapshot showed refine +10 on both exact component copies. The current holder had **18 buckets ×1460 constructed slots =26280 records**; visible cap 700 was not the array size.

Confirmed source issue: unchanged component readbacks incremented a counter that forced UI snapshot invalidation at each retry. The old 256-record slice followed by 50 ms backoff also prolonged large-holder scans; realm 0 could delay realm 1. The log did not record enough detail to prove the cause of each individual timeout.

Current changes:

- Separate changed payload from successful readback; keep UI publication capped at 200 ms.
- Alternate holder realms, up to 2048 records/8 headers and a cooperative ~1 ms scan budget per slice. Resume progressing requests at the next eligible 16 ms pump, back off invalid/missing paths 500 ms.
- Reset completion/match evidence on mutation/allocation changes. Both traversals and client/server values must be verified for `Synced`; this is not a save acknowledgement.
- Move historical equipment-profile I/O to a value-only worker; no automatic equipment-profile replay.
- Incomplete log includes op/value/reason/age/attempts, lastClient/lastServer, completion, cursor and restarts. Old `level=0` in a socket request did not mean refine +0.

Fixture with two 26280-record holder laps completed in **26 count-budget slices** without time-yield. This is a work-count test, not an in-game frame-time benchmark. Whole-tick time includes discovery, callbacks, I/O outside this path, and OS scheduling.

## 8. Material/Condition control investigation

**Root cause:** `RenderDyeEdit` accumulated `touched` but did not act on it. The old `s_dyeRetouch` flag was never set true; its helper also exited for All zones. Material values only accompanied the next explicit color choice.

**Native evidence:** `0x1409171E7` calls `0x14074A930` with the dye record; `0x14074A9E3..E7` copies record+4 (u16 material) into render parameters+0x28. RGB/alpha are read from +7..+10, condition from +11. `0x14074AA60` uses the material key in downstream part/palette lookup. This does not establish that templates 1–10 differ visibly on every mesh.

**Patch:** `Dye::Retouch` queues immediately with a **350 ms debounce**, binds exact item/world/selection identity, and captures current records on the game thread. `BuildDyeRetouchRecords` changes only fields requested by a bitmask, preserving each zone's group/RGB/alpha/other payload. Sparse All zones leaves undyed/explicitly clear zones alone; empty selections produce a choose-color prompt. Only an unstarted retouch can coalesce; started mount requests keep their immutable per-zone payload and one-channel/16 ms staging.

Material **0** maps to **0xFFFF natural**, no longer silently to template 1. A clear record additionally has zero RGB/alpha/+12 and a high-bit condition sentinel. Native upsert replacement copies bytes **0–12**, so comparison and profile payloads exclude tail **13–15**. Data readback is verified independently of native-call completion.

See [the material-fix report](2026-09-16_fix-dye-material-report.md) for Evidence → Finding → Path and scope.

## 9. Profiles, diagnostics, and open reports

- **Named Dye Profiles**: `Trinity_DyeProfiles.dat`, magic `TRDYPR01`, 300-byte records, FNV32 checksum, up to 64 profiles. Exact revision/type/mode compatibility; capture after the save is loaded, not raw-save parsing. Apply restores all 12 zones and explicit natural clears. Temporary-file commit and worker value copies protect the library; the format is separate from `Trinity_DyeCache.dat` and historical equipment INI.
- **Performance logs**: SYSTEM → Performance Diagnostics, default `perfLogging=0`. `game-tick max/10s` is a maximum invocation per subsystem, not average/frame cost; maxima from different frames must not be summed. Reports use the 2 ms threshold.
- **Kuku Pot retrieval**: still a reported scenario needing exact item/version/error and a naturally obtained control. AddInRealm can fall back to direct slot mutation; CommitAdd accepts either-realm success. Those are plausible trainer-side contributors, not a confirmed pot-specific root cause.
- **Trust/disconnect**: the current setter implementation is not the older proposed delta-20 clamp. Do not use error 298648703 alone to diagnose a particular trust threshold or save corruption.

## 10. Latest verification record

| Item | Result |
|---|---|
| Artifact | `build-clean/Trinity.asi` |
| Build stamp | `Sep 16 2026 22:27:27` |
| SHA-256 | `54D518FB2C26C1ACCCFF3421DC20C123E27049B90F8D98B545E9513B29A92772` |
| MD5 | `4BB83BE9C2E1594B0B565E706E7209E2` |
| CTest | 10/10 passed (adds `TrinityDyeEditorTests` + `TrinityCharacterIdentityTests` for Dye Equipment NPC-misidentification fix) |
| In-game material/FPS retest | Not yet verified for this artifact |
| Build workflow | `-Configuration Release -BuildOnly`; no deployment by the agent |

An earlier run of DyeProfileTests failed with Windows 5 in build-clean but passed from the temp directory. The latest full run passed; that does not establish the earlier failure's cause. No antivirus or folder-permission diagnosis follows from that contrast alone.

```powershell
# Run from the project; CTest must be on PATH.
powershell.exe -ExecutionPolicy Bypass -File "Build_Trinity.ps1" -Configuration Release -BuildOnly
ctest --test-dir "build-clean" --output-on-failure
python -B "scripts/inspect_dye_material_native.py" --va 0x14074A930 --bytes 304
```

Older binary addresses, test counts, and results above are observations for their recorded build. Future builds must record a fresh stamp/hash and rerun the relevant checks.
