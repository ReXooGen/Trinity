# Trinity — Dokumentasi Arsitektur Edit Equipment, Dye Equipment & Mount Equipment

Dokumen teknis ini menjelaskan secara mendalam prinsip desain, struktur data, alur eksekusi memori, serta mekanisme penanganan bug untuk fitur **Edit Equipment**, **Dye Equipment**, dan **Mount Equipment & Dye** pada seluruh varian Trinity (v1.2.4+ untuk Title Update 1.14.00 Legacy hingga TU 1.18.02+ Modern).

---

## 1. Edit Equipment Architecture

### A. Alur Resolusi Karakter Aktif & Companion
1. **Prioritas Live Component (Zero Cross-Pollution)**:
   - `ActivePlayerCharacterIdx()` bertugas menentukan secara akurat siapa karakter yang sedang dikontrol pemain di dunia 3D (*active player*).
   - **Tier 1 (Live Component)**: Menginspeksi tabel slot pada `Dye::ActiveClientComp()` melalui `IdentifyCharacterFromComp()`.
   - **Tier 2 (Client Container)**: Menginspeksi container utama dari `ResolveClientContainer()` / `ClientCharacterAddr()`.
   - **Tier 3 (Player Actor 0)**: Menginspeksi aktor pertama `Player::GetActor(0)`.
2. **Identifikasi Signature Karakter (Triple-Match: TypeID + Key + Localized Name)**:
   - **Damiane (`1`)**:
     - *TypeIDs*: `53935` (White Wind Rapier), `6324` (Spencer Pistol), `6041` (Fist Damian), `5306`, `5300`, `5297`, `5277`, `3463`, rentang `5450..5468`, `5270..5310`.
     - *Keywords*: `"Damian"`, `"Demian"`, `"Demeniss"`, `"Rapier"`, `"Dewhaven"`, `"Carmine"`, `"Uniform"`, `"Spear"`, `"Lance"`, `"Halberd"`, `"Grace"`, `"Sun"`, `"Sydmon"`, `"White Wind"`.
   - **Oongka (`2`)**:
     - *TypeIDs*: `6560` (Dekarr Greataxe), `6042`, rentang `6550..6570`.
     - *Keywords*: `"Oongka"`, `"Big Horn"`, `"BigHorn"`, `"Giant"`, `"Tynion"`.
   - **Kliff (`0`)**:
     - *TypeIDs*: `6303`, rentang `5330..5350`.
     - *Keywords*: `"Kliff"`, `"Sword of the Wolf"`, `"Darkness King"`, `"Balgran"`, `"Aeserion"`, `"Odeck"`.
3. **Pencarian Companion Party (`CharacterAddr(index)`)**:
   - Jika `index == ActivePlayerCharacterIdx()`, fungsi langsung mengembalikan container karakter aktif live.
   - Jika `index != ActivePlayerCharacterIdx()`, sistem mencari aktor companion di:
     1. **Party Container Manager Array (`holder + 0x18`)**: Membaca seluruh container companion party yang terdaftar di engine.
     2. **Tracked Player Actors (`Player::GetActor(1..3)`)**: Memvalidasi aktor companion live di dunia.
     3. **Snapshot Candidate Pool**: Memindai kandidat pool memori.
   - Jika companion tidak ada di party/dunia, menu menampilkan **`Character not loaded`** secara aman tanpa mencampuradukkan data dengan karakter aktif.

### B. Fitur Edit Equipment
- **1-Click Repair All Gear**: Memulihkan durability seluruh gear aktif ke 100% (`10000`).
- **1-Click Max Refinement (+10) All**: Meningkatkan level tempa/refine seluruh gear ke level maksimal (+10).
- **1-Click Unlock All Sockets**: Membuka seluruh 5 slot socket Abyss Gear pada semua equipment yang dikenakan.
- **Per-Socket Abyss Gear Socketing & Extraction**: Memasang dan mencabut batu abyss gear dengan sinkronisasi dual-realm server-client.

---

## 2. Dye Equipment Architecture (Dual-Realm & Persistent Storage)

### A. Dual-Realm Architecture & Save Persistence
1. **Client Component (`Dye::ClientComp()`)**:
   - Digunakan untuk merender warna langsung pada layar secara real-time.
   - Record warna 16-byte ditulis ke entri slot via `CallDyeUpsert(entry, rec)` dan `g_dyeApply(comp, &err, batch)`.
   - Memanggil `g_refresh(comp, &refreshErr)` untuk memaksa GPU me-refresh material dan shader seketika.
2. **Server Component (`Dye::ServerComp()`) & Server Mirror**:
   - `MirrorToServer(tag, instId, recs, mask)` mencari instance ID slot di `Inventory::ServerHolderAddr()` dan `ServerCharacterAddr()`.
   - Record warna disalin ke server realm sehingga konfigurasi dye tersimpan di file save game.
3. **Persistent Disk Storage (`Trinity_DyeProfile.ini`) & Auto-Restore**:
   - Setiap perubahan warna pada karakter (Kliff, Damiane, Oongka) dan Mount secara otomatis disimpan ke file disk `Trinity_DyeProfile.ini`.
   - Saat game dibuka kembali (*cold restart*), file INI dimuat otomatis ke memori pada `Dye::Install()`.
   - Loop `Dye::Tick()` secara periodik mengecek apakah ada item yang dikenakan pemain/kuda dengan `liveCount == 0`, dan langsung menerapkan kembali (*auto-restore*) seluruh warna dye secara instan dan mulus!

### B. Optimasi Kinerja & Anti-Lag (Snapshot Throttling)
- Penerapan throttle 100ms (`s_lastDyeSnapTick` dan `s_lastEquipSnapTick`) pada `RebuildSnapshot()`.
- Mencegah pembacaan tabel memori berulang-ulang 240+ kali per detik saat menu terbuka, mengembalikan frame rate stabil di 60+ FPS.

---

## 3. Mount Equipment & Mount Dye Architecture

### A. Multi-Tier Mount Component Resolution (`FindMountComp`)
Sistem pencarian komponen mount kuda dirancang berlapis untuk menjamin deteksi instan:
1. **Hook Capture (`g_mountComp`)**: Ditangkap otomatis saat ada perubahan equip pada mount melalui hook `hkEquipBatch`.
2. **Direct Mount Actor (`Player::GetMountActor(index)`)**: Membaca aktor kendaraan/kuda yang terdaftar di `Player`.
3. **Multi-Actor CharMgr Fallback**: Memindai seluruh aktor mount aktif di dunia.

### B. Pemetaan Slot & TypeID Equipment Kuda
Engine Crimson Desert menggunakan kombinasi tag slot dan rentang TypeID khusus untuk perlengkapan kuda:
| Slot Name | Tag ID | TypeID Utama / Rentang | Keterangan |
| :--- | :---: | :---: | :--- |
| **Saddle** | `14` | `43408` (`43400..43408`) | Pelana kuda (*Horse Saddle*) |
| **Chamfron** | `22` | `43409` | Pelindung kepala kuda (*Chamfron / Helm*) |
| **Horse Armor** | `23` | `43410` | Zirah tubuh kuda (*Barding / Plating*) |
| **Stirrups** | `24` | `43411` | Pijakan kaki pelana (*Stirrups*) |
| **Horseshoes** | `25` | `43412` | Ladam / Sepatu kuda (*Horseshoes*) |

### C. Mekanisme Pewarnaan & Auto-Restoration Kuda
1. **Multi-Actor Server Sync**:
   - Menulis konfigurasi warna ke seluruh aktor kuda aktif di `CharMgr` dengan manipulasi `RealmFlag = 1`.
2. **Auto-Restoration Loop pada `Tick()`**:
   - `SaveMountSlot(tag, entry, clear)` menyimpan profil warna mount di cache memori dan file disk `Trinity_DyeProfile.ini`.
   - Loop periodik pada `Tick()` memantau komponen kuda live. Jika kuda baru saja dipanggil (*summoned* via peluit) atau pemain baru saja *load game*, sistem otomatis mendeteksi `liveCount == 0` dan menerapkan kembali (*auto-restore*) seluruh record warna ke kuda seketika.
