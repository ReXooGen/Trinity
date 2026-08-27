# Trinity — TU 2.00.00 Offset & Data Reference (PE 1.0.0.2625)

> Game build: Steam buildid 24934353 · PE revision **1.0.0.2625**
> Image base `0x140000000` · Kode live di section **`.xpdata`** (VA `0x140001000`–`0x14496C000`)
> ⚠️ Section `.debug$P` (256 MB, flag code+execute) berisi **kode build lama** — JANGAN pernah discan/hook.

---

## 1. Signature (AOB) — status vs 1.18.02

### Masih match (37) — tidak berubah
DamageApply, DyeApplyBatch, DyeApplySlot, DyeUpsert, DyeVisualSet/Clear, DyeRecordRemove,
EquipBatch, EquipEffectRefresh, FieldTimeRealm/Tick, FriendlySetPet, InvCommit,
InvCommitPlacement, InvCoreGlobal, InvFreePlacements, InvGetHolder, InvGetItemQty(+Legacy),
InvHolderInsert, InvSetExpandSlots, LocoStepper, MarkerPattern/Player/OriginPrefix, MovR8Rip,
LeaR8Rip, MoveUpdate, TableResolverPrologue, TrItemValueCtor, TravelToNode, WeatherRain/Snow/Dust, WindPack.

### Signature BARU (wajib untuk 2.00)
| Nama | VA | Pola |
|---|---|---|
| `kSig_GameSpeed` | `0x140948158` (unik) | `80 3D ?? ?? ?? ?? 01 75 30 48 8B 4F 60 41 8B C7 C5 78 2F 61 64 0F 97 C0 85 C0 74 09 80 3D ?? ?? ?? ?? 01 75 14 C5 FA 10 05 ?? ?? ?? ?? C5 FA 11 41 64 C6 05 ?? ?? ?? ?? 00` — vmovss value kini di **match+35** (dulu 37) |
| `kSig_TodEngineGlobal` | `0x14282011A` (unik) | `83 3D ?? ?? ?? ?? FF 75 ?? 48 89 1D ?? ?? ?? ?? 48 89 3D ?? ?? ?? ?? 44 89` |
| `kSig_EnvManager` | first-hit `0x140AEBA93` | `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 40 48 8B 88 E0 0E 00 00` — field manager `+0xEE0` (dulu `+0xEF0`); global = `0x14625AF90` |
| `kSig_LocStringGet` | `0x1410D5200` (unik) | `8B 41 18 48 8B 0D ? ? ? ? 3B 41 60 72 08 48 8D 05 ? ? ? ? C3 48 03 41 58 C3` |
| `kSig_JustCore` | `0x140AC0FB0` (unik, uji live) | `48 8B C4 55 41 56 48 81 EC ?? ?? ?? ?? C5 FC 10 89` |

### Hilang / belum ditemukan di 2.00
| Nama | Dampak | Catatan |
|---|---|---|
| `pa_StatCommit` | God Mode / Inf Stamina / Spirit OFF | pola lama hilang total dari semua section |
| `FriendlySetNpc` | Trust Multiplier NPC off (pet aman) | arsitektur baru: caller → wrapper `0x140648390` → leaf `0x141BDA250`; map helper NPC `0x141BDA120` (+0x18), PET `0x141BDB390` (+0x38) |
| `MarkerProtection` | proteksi marker off | — |
| area-name resolver | nama waypoint → index | — |
| marker origins | 8 match (exp 9/11) → marker teleport off | — |

---

## 2. Struktur `TrItemValue` (dari ctor `0x142093010`, resmi)

| Offset | Field | vs 1.18 |
|---|---|---|
| `+0x00` | i64 InstanceId (-1) | sama |
| `+0x08` | u16 TypeId | sama |
| `+0x0A` | u16 Refine/Subtype (src `def+0x218`) | sama |
| `+0x40` | u16 Durability (src `def+0x400`) | sama |
| `+0x48/+0x50` | i64 ×1000 (src `def+0x1C8/0x1D0`) | baru |
| `+0x60` | socket vector ptr | **sama** |
| `+0x68` | u32 size | **sama** |
| `+0x6C` | u32 cap | **sama** |
| `+0x70` | unlocked — ⚠️ **hanya LOW BYTE**; upper bytes = flag lain (live: `0xFFFFFF02`) | ⚠️ tulis dword penuh = korupsi |
| `+0x78/+0x80` | dye data / count | sama |
| record 6B | GearId u16 · Marker u16 · Index u8 · State u8 | sama |

## 3. `ItemDef` (2.00)
| Offset | Field | vs 1.18 |
|---|---|---|
| `+0x18` | i64 MaxStackCount | ✅ masih valid (live) |
| `+0x111` | u8 ApplyMaxStackCap | ✅ masih valid |
| `+0x428` | u16 **BucketType** | ⚠️ pindah dari `+0x418` ke **`+0x428`** (BUKAN +0x420 seperti di-RE sebelumnya). Dikonfirmasi binary audit: `InvHolderInsert` (VA `0x142091150`) dan `InvCommitPlacement` (VA `0x141DF9CF0`) keduanya `movzx r, word [def+0x428]` lalu `cmp [bucket+0x10], r`. 225 unique hits di binary. Field `+0x420` adalah sesuatu yang lain. |
| `+0x220/+0x228` | default socket array ptr/count | baru |
| `+0x08/+0x20/+0x90/+0x210/+0x350` | Key/Name/Icons/Tier/Groups | ✅ masih valid (browsing katalog normal) |

## 4. Lokalisasi (2.00)
```
off  = *(u32*)(provider + 0x18)      // dulu +0x10
data = *(char**)(locMgr + 0x58)      // dulu blob=[mgr+8]; data=[blob+0]
size = *(u32*)(locMgr + 0x60)        // dulu [blob+8]
name = off < size ? data + off : ""
```

## 5. Money getters 2.00 (kandidat, belum di-hook)
- Wrapper: `0x144899FB0` (key `0x146276CF0`), `0x144899FE0` (key `0x146276D40`), pola berulang
- Lookup: `0x1402ED7A0` · Pekerja: `0x14115BB10`
- Hook lama `gameBase+0x16077B0/78C0/81D0` = **TIDAK VALID** (dinonaktifkan)

## 6. Crash yang sudah diperbaiki di 2.00
1. **World-entry freeze/crash** — `Inventory::Tick` menulis Money_Copper `ItemDef+0x18/+0x111` tiap detik → digate `revision >= 2625`.
2. **Add Item gagal semua item** — `BucketForItem` baca `+0x418` garbage → fix `+0x428` (confirmed binary).
3. **False-positive scanner** — `.debug$P` berisi kode build lama → scanner exclude `.debug*`.
4. **EnvManager off-by-3** — `ResolveRipAt(envSig+3)` lama menghasilkan alamat sampah; kini `ResolveRipAt(envSig, 7)`.

## 7. Catatan komunitas (2.0)
- "Mod fonctionne avec la version 2.0, aucun bug" → base mod kompatibel.
- "Add Item no longer works for boots and helms" → = bug bucket `+0x428` (sudah difix di build ini — offset `+0x420` yang di-RE sebelumnya salah, konfirmasi dari binary audit `InvHolderInsert`/`InvCommitPlacement`).
- Freeze yang persisten di satu mesin setelah serangan crash awal = **savegame terkorupsi** (di luar jangkauan mod).

## 8. Safe mode (Trinity_SafeMode.txt di bin64)
```
1 player | 2 teleport | 4 inventory | 8 world | 16 dye | 32 equipment | 64 friendly
128 skip MoveUpdate | 256 skip LocoStepper | 512 skip World::Tick | 1024 skip Player::Tick
2048 skip Inv::Tick | 4096 skip Dye::Tick | 8192 skip Equip::Tick | 16384 skip jump-scale+pos
```

## 9. Tools
`tools/scan_signatures_200.py` · `audit_live_200.py` · `deep_analysis_200.py` ·
`find_sigs_200.py` · `disasm.py` · `find_unlocked_field.py` · `dump_bucket_types.py`
Skill: `.agents/skills/crimson-binary-inspector` (inspect_pe, deep_entity_pointer_routing, dll.)
