# TU 2.00.00 (PE 1.0.0.2625) Reverse-Engineering Notes
# Build id Steam: 24934353. Binary: .xpdata code section, .debug$P = stale old-build code (JANGAN discan).
# Image base 0x140000000, kode di VA 0x140001000..0x14496C000.

## STATUS FITUR (setelah bisect)
- World entry crash   : FIXED - penyebab: Inventory::Tick menulis Money_Copper ItemDef+0x18/+0x111 SETIAP DETIK tanpa gate toggle (offset bergeser di 2.00). Sekarang di-guard revision >= 2625.
- Add Item freeze     : MITIGATED - RunPendingAdd di-guard revision >= 2625 (engine layout belum dipetakan). Browsing katalog aman.
- MoveUpdate          : SIGMASA - signature lama match unik & benar (0x143BAF7E0), tapi hkMoveUpdate menjalankan SEMUA tick; crash sebenarnya dari tick lain. Bit safeMode 128 = skip hook ini.
- World::Tick         : AMAN (dites terisolasi).
- Player::Tick        : AMAN.
- Inventory::Tick     : BAHAYA tanpa fix money-write (sudah digate).
- Scanner             : hanya scan section exec + .link; .debug* dikecualikan.

## SIGNATURE BARU 2.00 (sudah diterapkan di offsets.h)
- kSig_GameSpeed        @0x140948158 (unik); vmovss value offset 37->35; field item +0x58 -> +0x60
- kSig_TodEngineGlobal  @0x14282011A (unik)
- kSig_EnvManager       field manager +0xEF0 -> +0xEE0; global = 0x14625AF90
                        FIX BUG: ResolveRipAt(envSig, 7) bukan envSig+3 (off-by-3 lama = resolve sampah)
- kSig_LocStringGet     @0x1410D5200 (unik): provider+0x18, pool mgr+0x58 (char*), size mgr+0x60 (u32)
- kSig_JustCore         kandidat @0x140AC0FB0 (unik) - butuh verifikasi in-game

## MASIH RUSAK / BELUM DITEMUKAN
- pa_StatCommit        : pola lama hilang total (God Mode/Stamina/Spirit off)
- FriendlySetNpc       : arsitektur berubah: caller -> wrapper 0x140648390 -> leaf 0x141BDA250
                         helper map NPC=0x141BDA120 (+0x18), PET=0x141BDB390 (+0x38)
- MarkerProtection     : tidak match; marker origins 8 (exp 9/11)
- area-name resolver   : tidak match
- Money getters LAMA   : hardcode 0x16077B0/78C0/81D0 sudah tidak valid (dinonaktifkan)

## TEMUAN STRUKTUR (dari TrItemValueCtor @0x142093010, resmi)
TrItemValue 2.00 (KOMPATIBEL dengan 1.18 untuk field mod):
  +0x00 i64 InstanceId(-1) | +0x08 u16 TypeId | +0x0A u16 Refine/Subtype (src def+0x218)
  +0x40 u16 Durability (src def+0x400) | +0x48/+0x50 i64 = [def+0x1C8/0x1D0]*1000
  +0x60 blok socket: qword=0, dword@+0x68=0, byte@+0x70=0  -> OFFSET SOCKET SAMA (+0x60/68/6C/70)
  +0x78 qword dye-data | +0x80 qword dye-count  (sesuai DyeUpsert yang masih match)
ItemDef parsial: default socket array def+0x220(ptr)/+0x228(count), record 6-byte

## MONEY GETTER KANDIDAT 2.00 (untuk menghidupkan kembali money display)
- Wrapper: 0x144899FB0 (key global 0x146276CF0), 0x144899FE0 (key 0x146276D40), pola berulang
- Lookup helper: 0x1402ED7A0 ; pekerja: 0x14115BB10
- Perlu verifikasi live sebelum di-hook

## ALAT
- tools/scan_signatures_200.py : audit semua kSig vs exe (45/54 pass)
- tools/audit_live_200.py      : audit .xpdata-only + resolve RIP
- tools/deep_analysis_200.py   : function map + call graph + semantic hunt
- tools/find_sigs_200.py       : relaxed pattern hunter ("dump:VA" mode)
- tools/disasm.py              : capstone disassembler annotasi RIP-target

## SAFE MODE (Trinity_SafeMode.txt di bin64)
Bit: 1 player | 2 teleport | 4 inventory | 8 world | 16 dye | 32 equipment | 64 friendly
Sub: 128 skip MoveUpdate | 256 skip LocoStepper | 512 skip World::Tick | 1024 skip Player::Tick
     2048 skip Inv::Tick | 4096 skip Dye::Tick | 8192 skip Equip::Tick | 16384 skip jump-scale+posread
Riwayat tes: 127=aman, 31=aman, 15=aman, 11=freeze saat Add Item, 1536=freeze, 32256=aman,
             128=aman (kesimpulan akhir), 0+fix money=aman kecuali add-item path (digate)

## LIVE SESSION FINDINGS (socket/dye)
- Live scan comp Kliff: CharMgr walk + *(*(owner+0x68)+0x38) MASIH VALID di 2.00.
- Socket record write 6-byte via external RPM: BERHASIL & game stabil (gem 0x0C24 di Tag0 slot3).
- UI/tooltip TIDAK menampilkan gem: karena UI membaca copy lain. SyncSocketAllRealms 1.18 menulis 4 lapisan:
  1) client equip comp 2) realm copies (CharacterAddrs) 3) inventory holder copies (FindAndApplyAllHolders)
  4) server realm (TLS RealmFlag=1 -> ServerComp).
- Freeze saat edit via Trinity kemungkinan di langkah 2-4 (resolver realm/holder offset bergeser), BUKAN di tulisan record.
- Field +0x70 unlocked: terbaca 65283/0xFFFFFF02 pada beberapa item = upper bytes dipakai flag lain.
  UnlockedCount() yang reject >5 lalu menulis ulang +0x68/6C/70 penuh (5,5,5) berisiko clobber -> perlu
  tulis LOW BYTE saja atau validasi lebih lanjut.
- Identifikasi karakter: script skill pakai daftar TypeID equipment (Damiane/Oongka ranges) - bisa di-port
  ke Trinity untuk memperbaiki deteksi Oongka.
- Live tools yang terbukti jalan di 2.00: deep_entity_pointer_routing.py --scan / --inject-socket,
  find_unlocked_field.py (tools).
