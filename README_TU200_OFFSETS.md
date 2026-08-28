# Trinity — Title Update 2.00.00 Offset & Data Reference (PE 1.0.0.2625)

> **Game Build**: Steam Build ID `24934353` · PE Revision **`1.0.0.2625`**  
> **Image Base**: `0x140000000` · Live Code Section: **`.xpdata`** (VA `0x140001000`–`0x14496C000`)  
> ⚠️ **Critical Notice**: The `.debug$P` section (~256 MB, flagged code+execute) contains **legacy build code** — NEVER scan or hook inside this section.

---

## 1. Array of Bytes (AOB) Signatures — Status vs 1.18.02

### Still Matching (37 Signatures) — Unchanged
`DamageApply`, `DyeApplyBatch`, `DyeApplySlot`, `DyeUpsert`, `DyeVisualSet`/`Clear`, `DyeRecordRemove`, `EquipBatch`, `EquipEffectRefresh`, `FieldTimeRealm`/`Tick`, `FriendlySetPet`, `InvCommit`, `InvCommitPlacement`, `InvCoreGlobal`, `InvFreePlacements`, `InvGetHolder`, `InvGetItemQty` (+Legacy), `InvHolderInsert`, `InvSetExpandSlots`, `LocoStepper`, `MarkerPattern`/`Player`/`OriginPrefix`, `MovR8Rip`, `LeaR8Rip`, `MoveUpdate`, `TableResolverPrologue`, `TrItemValueCtor`, `TravelToNode`, `WeatherRain`/`Snow`/`Dust`, `WindPack`.

### NEW Signatures (Required for Title Update 2.00)
| Signature Name | Virtual Address (VA) | Pattern & Analysis |
|---|---|---|
| `kSig_GameSpeed` | `0x140948158` (Unique) | `80 3D ?? ?? ?? ?? 01 75 30 48 8B 4F 60 41 8B C7 C5 78 2F 61 64 0F 97 C0 85 C0 74 09 80 3D ?? ?? ?? ?? 01 75 14 C5 FA 10 05 ?? ?? ?? ?? C5 FA 11 41 64 C6 05 ?? ?? ?? ?? 00` — `vmovss` value is now at **match+35** (formerly 37). |
| `kSig_TodEngineGlobal` | `0x14282011A` (Unique) | `83 3D ?? ?? ?? ?? FF 75 ?? 48 89 1D ?? ?? ?? ?? 48 89 3D ?? ?? ?? ?? 44 89` |
| `kSig_EnvManager` | First-hit `0x140AEBA93` | `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 40 48 8B 88 E0 0E 00 00` — Field manager `+0xEE0` (formerly `+0xEF0`); Global pointer = `0x14625AF90`. |
| `kSig_LocStringGet` | `0x1410D5200` (Unique) | `8B 41 18 48 8B 0D ? ? ? ? 3B 41 60 72 08 48 8D 05 ? ? ? ? C3 48 03 41 58 C3` |
| `kSig_JustCore` | `0x140AC0FB0` (Unique, live test) | `48 8B C4 55 41 56 48 81 EC ?? ?? ?? ?? C5 FC 10 89` |

### Deprecated / Relocated in 2.00
| Name | Impact | Technical Notes |
|---|---|---|
| `pa_StatCommit` | God Mode / Infinite Stamina / Spirit OFF | Legacy pattern completely eliminated across all executable sections. |
| `FriendlySetNpc` | NPC Trust Multiplier off (Pet safe) | Redesigned architecture: caller → wrapper `0x140648390` → leaf `0x141BDA250`; NPC map helper `0x141BDA120` (+0x18), Pet `0x141BDB390` (+0x38). |
| `MarkerProtection` | Marker protection off | Non-fatal |
| `area-name resolver` | Waypoint name → Index | Non-fatal |
| `marker origins` | 8 matches (expected 9/11) → Marker teleport disabled | Non-fatal |

---

## 2. `TrItemValue` Structure Layout (Derived from ctor `0x142093010`)

| Offset | Field Type | Comparison vs 1.18 |
|---|---|---|
| `+0x00` | `int64_t` InstanceId (`-1`) | Identical |
| `+0x08` | `uint16_t` TypeId | Identical |
| `+0x0A` | `uint16_t` Refine/Subtype (source `def+0x218`) | Identical |
| `+0x40` | `uint16_t` Durability (source `def+0x400`) | Identical |
| `+0x48/+0x50` | `int64_t` ×1000 (source `def+0x1C8/0x1D0`) | New |
| `+0x60` | Socket vector pointer | **Identical** |
| `+0x68` | `uint32_t` Size | **Identical** |
| `+0x6C` | `uint32_t` Capacity | **Identical** |
| `+0x70` | Unlocked count — ⚠️ **LOW BYTE ONLY**; upper bytes contain independent flags (live value: `0xFFFFFF02`) | ⚠️ Writing a full DWORD will corrupt flags |
| `+0x78/+0x80` | Dye data pointer / count | Identical |
| `Record 6B` | `GearId` (u16) · `Marker` (u16) · `Index` (u8) · `State` (u8) | Identical |

---

## 3. `ItemDef` Layout (2.00)

| Offset | Field | Comparison vs 1.18 |
|---|---|---|
| `+0x18` | `int64_t` MaxStackCount | ✅ Valid (live confirmed) |
| `+0x111` | `uint8_t` ApplyMaxStackCap | ✅ Valid (live confirmed) |
| `+0x428` | `uint16_t` **BucketType** | ⚠️ Relocated from `+0x418` to **`+0x428`** (NOT `+0x420`). Confirmed via binary audit: `InvHolderInsert` (`0x142091150`) and `InvCommitPlacement` (`0x141DF9CF0`) both execute `movzx r, word [def+0x428]` followed by `cmp [bucket+0x10], r`. 225 unique hits in executable. Field `+0x420` serves another purpose. |
| `+0x220/+0x228` | Default socket array ptr/count | New in 2.00 |
| `+0x08/+0x20/+0x90/+0x210/+0x350` | Key / Name / Icons / Tier / Groups | ✅ Fully valid (Catalog browsing normal) |

---

## 4. In-Game Localization Engine (2.00)

```cpp
off  = *(uint32_t*)(provider + 0x18);      // Formerly +0x10
data = *(char**)(locMgr + 0x58);           // Formerly blob=[mgr+8]; data=[blob+0]
size = *(uint32_t*)(locMgr + 0x60);         // Formerly [blob+8]
name = (off < size) ? (data + off) : "";
```

---

## 5. Money Getters in 2.00 (Candidate Functions)

- **Wrappers**: `0x144899FB0` (global key `0x146276CF0`), `0x144899FE0` (global key `0x146276D40`), repeating pattern.
- **Lookup Helper**: `0x1402ED7A0` · **Worker Function**: `0x14115BB10`.
- ⚠️ **Legacy Hooks** at `gameBase + 0x16077B0 / 0x16078C0 / 0x16081D0` are **INVALID** and disabled.

---

## 6. Resolved Crashes & Stability Fixes in 2.00

1. **World-entry Freeze / Crash**:
   - *Cause*: `Inventory::Tick` was overwriting `Money_Copper` `ItemDef+0x18/+0x111` every second without toggle gating (offset shifted in 2.00).
   - *Fix*: Gated under `revision >= 2625`.
2. **Add Item Failure Across All Items**:
   - *Cause*: `BucketForItem` was reading garbage from `+0x418`.
   - *Fix*: Updated to `+0x428` (binary-confirmed).
3. **Scanner False Positives**:
   - *Cause*: `.debug$P` section contained stale build code.
   - *Fix*: Scanner explicitly excludes `.debug*` sections.
4. **EnvManager Off-by-3 Pointer**:
   - *Cause*: Legacy `ResolveRipAt(envSig+3)` returned garbage addresses.
   - *Fix*: Corrected to `ResolveRipAt(envSig, 7)`.

---

## 7. Community & Testing Notes (2.00)

- "Mod works smoothly with version 2.0, no bugs" $\rightarrow$ Base engine hooks are solid and fully operational.
- "Add Item no longer works for boots and helmets" $\rightarrow$ Caused by the bucket offset shifting to `+0x428` (Fixed in this release).
- Persistent freezes on individual machines after dirty crashes are typically caused by **corrupted save files**, outside the mod's scope.

---

## 8. Safe Mode Diagnostic Flags (`Trinity_SafeMode.txt` in `bin64`)

```
1: Player Subsystem      | 2: Teleport Subsystem   | 4: Inventory Subsystem
8: World Subsystem       | 16: Dye Subsystem       | 32: Equipment Subsystem
64: Friendly Subsystem   | 128: Skip MoveUpdate    | 256: Skip LocoStepper
512: Skip World::Tick    | 1024: Skip Player::Tick | 2048: Skip Inv::Tick
4096: Skip Dye::Tick     | 8192: Skip Equip::Tick  | 16384: Skip Jump Scale & Pos Read
```

---

## 9. Reverse Engineering & Binary Inspection Tools

- `tools/scan_signatures_200.py`: Audits all `kSig_*` patterns against the binary.
- `tools/audit_live_200.py`: Audits `.xpdata` execution section and verifies RIP targets.
- `tools/deep_analysis_200.py`: Function mapping, call-graph analysis, and semantic hunting.
- `tools/find_sigs_200.py`: Pattern hunter with relaxed matching.
- `tools/disasm.py`: Capstone disassembler with automatic RIP-relative target annotations.
- `tools/find_unlocked_field.py` & `tools/dump_bucket_types.py`: Hex structure inspectors.
- **Skill**: `.agents/skills/crimson-binary-inspector` (`inspect_pe`, `deep_entity_pointer_routing`, etc.).
