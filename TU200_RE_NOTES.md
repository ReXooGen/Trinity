# Title Update 2.00.00 (PE 1.0.0.2625) Reverse-Engineering Notes

> **Steam Build ID**: `24934353`  
> **Binary Memory**: Live code resides in `.xpdata` section (VA `0x140001000`–`0x14496C000`).  
> ⚠️ **Warning**: `.debug$P` section contains stale, legacy build code (NEVER scan this section).  
> **Image Base**: `0x140000000`.

---

## 1. Feature Status (Post-Bisection)

- **World-entry Crash**: **FIXED** — Cause: `Inventory::Tick` was overwriting `Money_Copper` `ItemDef+0x18/+0x111` every second without toggle gating (offset shifted in 2.00). Now protected under `revision >= 2625`.
- **Add Item Freeze**: **MITIGATED & FIXED** — `BucketForItem` offset updated from `+0x418` to `+0x428`. Catalog browsing is fully safe.
- **MoveUpdate Hook**: **OPERATIONAL** — Legacy signature matches uniquely and accurately (`0x143BAF7E0`).
- **World::Tick**: **SAFE** (Tested and verified in isolation).
- **Player::Tick**: **SAFE**.
- **Inventory::Tick**: **SAFE** (Protected with version gates).
- **Pattern Scanner**: **SAFE** — Exclusively scans executable sections and `.link`; `.debug*` sections are strictly ignored.

---

## 2. New 2.00 Signatures (Integrated into `offsets.h`)

- **`kSig_GameSpeed`** `@ 0x140948158` (Unique): `vmovss` value offset shifted from 37 $\rightarrow$ 35; item field shifted from `+0x58` $\rightarrow$ `+0x60`.
- **`kSig_TodEngineGlobal`** `@ 0x14282011A` (Unique).
- **`kSig_EnvManager`**: Field manager shifted from `+0xEF0` $\rightarrow$ `+0xEE0`; Global address = `0x14625AF90`.  
  *Fix Applied*: `ResolveRipAt(envSig, 7)` instead of `envSig+3` (legacy off-by-3 resolved garbage pointers).
- **`kSig_LocStringGet`** `@ 0x1410D5200` (Unique): `provider+0x18`, pool `mgr+0x58` (`char*`), size `mgr+0x60` (`uint32_t`).
- **`kSig_JustCore`**: Candidate `@ 0x140AC0FB0` (Unique) — under verification.

---

## 3. Deprecated / Relocated in 2.00

- **`pa_StatCommit`**: Legacy pattern removed from executable (God Mode / Infinite Stamina / Spirit disabled).
- **`FriendlySetNpc`**: Architecture changed: caller $\rightarrow$ wrapper `0x140648390` $\rightarrow$ leaf `0x141BDA250`. Helper map NPC = `0x141BDA120` (`+0x18`), Pet = `0x141BDB390` (`+0x38`).
- **`MarkerProtection`**: No match in 2.00.
- **`area-name resolver`**: No match in 2.00.
- **`Legacy Money Getters`**: Hardcoded addresses `0x16077B0 / 0x16078C0 / 0x16081D0` are invalid and disabled.

---

## 4. Structure Layout Discoveries (From `TrItemValueCtor` `@ 0x142093010`)

`TrItemValue` in 2.00 (Compatible with 1.18 for modded fields):
```
+0x00: int64_t InstanceId (-1)
+0x08: uint16_t TypeId
+0x0A: uint16_t Refine/Subtype (source def+0x218)
+0x40: uint16_t Durability (source def+0x400)
+0x48 / +0x50: int64_t = [def+0x1C8 / def+0x1D0] * 1000
+0x60: Socket Vector Block: qword ptr = 0, dword @ +0x68 = size, dword @ +0x6C = cap
+0x70: Unlocked Count byte (Low byte only; upper bytes are flags: 0xFFFFFF02)
+0x78: qword dye-data pointer
+0x80: qword dye-count
```

**`ItemDef` Structure (Partial)**:
- Default socket array: `def + 0x220` (pointer) / `def + 0x228` (count), 6-byte records.
- Bucket Type: `def + 0x428` (uint16_t).

---

## 5. Candidate Money Getters in 2.00 (For Future UI Display Hooking)

- **Wrappers**: `0x144899FB0` (global key `0x146276CF0`), `0x144899FE0` (global key `0x146276D40`), repeating pattern.
- **Lookup Helper**: `0x1402ED7A0` · **Worker Function**: `0x14115BB10`.
- Requires live-testing before permanent hook insertion.

---

## 6. Analysis & Reverse Engineering Tools

- `tools/scan_signatures_200.py`: Audits all `kSig_*` patterns against the game binary.
- `tools/audit_live_200.py`: Audits `.xpdata` execution section and verifies RIP targets.
- `tools/deep_analysis_200.py`: Function mapping, call-graph analysis, and semantic hunting.
- `tools/find_sigs_200.py`: Pattern hunter with relaxed matching.
- `tools/disasm.py`: Capstone disassembler with automatic RIP-relative target annotations.

---

## 7. Safe Mode Diagnostics (`Trinity_SafeMode.txt` in `bin64`)

```
Subsystem Bits:
  1: Player       | 2: Teleport    | 4: Inventory    | 8: World
  16: Dye         | 32: Equipment  | 64: Friendly

Tick Bypass Bits:
  128: Skip MoveUpdate             | 256: Skip LocoStepper
  512: Skip World::Tick            | 1024: Skip Player::Tick
  2048: Skip Inventory::Tick       | 4096: Skip Dye::Tick
  8192: Skip Equipment::Tick       | 16384: Skip Jump Scaling & Position Read
```

**Test History Matrix**:
- `127` = Safe, `31` = Safe, `15` = Safe.
- `11` = Freeze during Add Item (Prior to `+0x428` fix).
- `128` = Safe (Final conclusion: Hook itself is valid).
- `0` + Money write fix = **Completely Safe & Stable**.

---

## 8. Live Session Findings (Socket / Dye)

- **Kliff Live Component Walk**: `CharMgr` traversal followed by `*(*(owner + 0x68) + 0x38)` remains **VALID** in 2.00.
- **6-byte Socket Record Injection via RPM**: Verified successful and stable in-game (Socketing Gem `0x0C24` into Tag 0, Slot 3).
- **Multi-Layer Realm Synchronization in 1.18**:
  1. Client equipment component.
  2. Realm mirror copies (`CharacterAddrs`).
  3. Inventory holder copies (`FindAndApplyAllHolders`).
  4. Server authority realm (`TLS RealmFlag = 1` $\rightarrow$ `ServerComp`).
- **Unlocked Field `+0x70`**: Read values of `65283` (`0xFFFFFF02`) show upper bytes contain engine flags. When modifying socket count, write to the **LOW BYTE ONLY** to avoid clobbering engine metadata.
- **Companion Identification**: Damiane and Oongka are identified through their unique equipment `TypeId` ranges.
