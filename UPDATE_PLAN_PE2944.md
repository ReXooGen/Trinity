# Trinity — план оновлення під Crimson Desert PE 1.0.0.2944

> **Статус:** діагностичний план (тільки аналіз, нічого ще не змінено в коді).
> **Джерело:** `Trinity.log` (запуск Trinity v1.3.5.4, build Sep 13 2026).
> **Гра за логом:** `Crimson Desert 1.18.02 (Active) [PE: 1.0.0.2944]`.
> **Орієнтир по кодовій базі:** commit `34d1c50`, `src/game/offsets.h` — єдине джерело правди для сигнатур/офсетів.

---

## 0. Головний висновок (TL;DR)

1. **PE revision 2944 — це НОВА, невідома збірка**, новіша за все, що знає Trinity
   (карта ревізій закінчується на `2850 = TU 2.02.00`). Рядок
   **«Crimson Desert 1.18.02» у логу — це hardcoded fallback**, а не реальна
   версія: справжня TU 1.18.02 мала PE `2474` (див. коментар у `version_detect.cpp`).
   Тому ВСІ ревізійно-залежні гілки в коді вибрали «legacy»/неправильну гілку.

2. У логу **два незалежні класи поломок**, які треба лікувати окремо:
   - **(A) `signature NOT FOUND`** — старі AOB-сигнатури більше не збігаються
     у новому бінарнику → треба пере-сканити (re-find).
   - **(B) `MH_CreateHook failed (MH_ERROR_MEMORY_ALLOC)`** — **системний** збій
     алокатора MinHook, а НЕ окремих сигнатур. Він однаково вбив **усі** хуки
     MinHook, поки всі «нативні» резолви (pointer-walk / direct call) спрацювали.
     Поки це не пофікшено, пере-скан сигнатур нічого не дасть для MinHook-хуків.

3. Два «ранні return» в інсталяторах (`Teleport::Install`, `Inventory::Install`)
   **приховують додаткові поломки**: коли падає перший хук, увесь решта
   інсталяції цієї підсистеми навіть не виконується.

---

## 1. Мапа помилок із логу → конкретні символи

Кожен рядок логу розкладено на: символ у `offsets.h` / файл-споживач /
клас поломки / що ламається.

| Рядок логу (скорочено) | Символ(и) | Файл | Клас | Наслідок |
|---|---|---|---|---|
| `Gameplay-code readiness timed out after 180 seconds` | sentinels у `mod.cpp::GameplayCodeReady()` (legacy-профіль) | `src/core/mod.cpp`, `src/core/readiness.cpp` | **ревізійний gate** | 3 хв затримка; хуки ставляться наосліп |
| `player: char-manager successfully resolved (1 anchors verified)` | `kCharMgrAnchors[0..5]` | `src/game/offsets.h`, `player.cpp` | **частковий дрейф** (1 з 6) | God Mode / Stamina / Spirit працюють лише через слабкий fallback |
| `player: stat-commit signature NOT FOUND` | `kSig_StatCommit` | `player.cpp:827` | **(A) очікувано** | гілка legacy намагається хукнути видалений (у TU 2.01+) funnel |
| `player: damage-apply: MH_CreateHook failed` | `kSig_DamageApply` | `player.cpp:833` | **(B)** | damage-множники мертві |
| `player: damage-apply (alt): MH_CreateHook failed` | `kSig_DamageApply_Alt` | `player.cpp:836` | **(B)** | те саме |
| `player: damage-apply signature NOT FOUND (tried primary + alt)` | (підсумкове повідомлення) | `player.cpp:843` | — | infinite stamina drain block вимкнено |
| `player: combat-timing: MH_CreateHook failed` | `kSig_CombatTimingEval` | `player.cpp:852` | **(B)** | Easy Parry / Easy Evade вимкнено |
| `teleport: movement-update: MH_CreateHook failed` | `kSig_MoveUpdate` | `teleport.cpp:1772` | **(B)** | позиційний трекінг мертвий + **early return** гасить весь teleport |
| `world: evaluate-wanted-state: MH_CreateHook failed` | `kSig_EvaluateCrimeWantedState` | `inventory.cpp:2064` | **(B)** | Witness/Assault crime bypass вимкнено |
| `world: register-crime-event signature NOT FOUND` | `kSig_RegisterCrimeEvent` | `inventory.cpp:2068` | **(A)** | Crime event dispatch + UI-банер вимкнено |
| `inventory: item-count accessor: MH_CreateHook failed` | `kSig_InvGetItemQty` | `inventory.cpp:2072` | **(B)** | — |
| `inventory: item-count accessor legacy signature NOT FOUND` | `kSig_InvGetItemQty_Legacy` | `inventory.cpp:2075` | **(A)** | **early return** → весь inventory вимкнено |
| `world: Failed to install FrameTimerUpdate hook.` | `kSig_FrameTimerBody` (+`_Pre201`) + проходка до прологу | `world.cpp:468-499` | **(B)** | Game Speed вимкнено |
| `world: field-time tick: MH_CreateHook failed` | `kSig_FieldTimeTick` | `world.cpp:526` | **(B)** | — |
| `world: field-time tick (pre-2.01) signature NOT FOUND` | `kSig_FieldTimeTick_Pre201` | `world.cpp:529` | **(A)** | Freeze Time of Day вимкнено |
| `world: rain intensity: MH_CreateHook failed` | `kSig_WeatherRain` | `world.cpp:565` | **(B)** | Rain control вимкнено |
| `world: snow intensity: MH_CreateHook failed` | `kSig_WeatherSnow` | `world.cpp:568` | **(B)** | Snow control вимкнено |
| `world: dust intensity: MH_CreateHook failed` | `kSig_WeatherDust` | `world.cpp:571` | **(B)** | Dust control вимкнено |
| `world: wind pack: MH_CreateHook failed` | `kSig_WindPack` | `world.cpp:574` | **(B)** | — |
| `world: wind pack (pre-2.01) signature NOT FOUND` | `kSig_WindPack_Pre201` | `world.cpp:577` | **(A)** | Cloud and Fog control вимкнено |
| `friendly: Trust Multiplier setters NOT FOUND` | `kSig_FriendlySetNpc201/Pet201`, `kSig_FriendlySetNpc/Pet`, `kSig_FriendlyGetNpc201/Pet201`, `kSig_FriendlyNpcTrustWriter`, `kSig_FriendlyAlertDisp` | `friendly.cpp:237-299` | **(A) + (B)** | Trust Multiplier вимкнено |
| `worker: supplied level/ability patch disabled for unsupported PE revision 2944` | `WorkerPatchSupportedForRevision()` | `worker_logic.h:15`, `worker.cpp:51` | **ревізійний gate** | worker level/ability patch вимкнено |

### Що в логу **працює** (це важливо для діагнозу)

| Лог | Чому працює |
|---|---|
| DX12/overlay/Present — все OK | мануальні патчі + pointer-walk, **не** MinHook |
| `inventory: table 'iteminfo' / 'categorygroupinfo' / 'stringinfo' / 'Inventory' resolved`, `catalog built (6815/44)` | string-anchor + pointer-walk, **не** MinHook |
| `equipment: EquipEffectRefresh resolved @ 0x140659510` | native call (`g_refresh = FindPattern(...)`), **не** хук |
| `equipment: native ResizeSocketVector resolved @ 0x14488C46D` | native call, **не** хук |
| (немає рядків `field-clock signature NOT FOUND` / `TOD engine-global signature NOT FOUND`) | `kSig_FieldTimeRealm`, `kSig_TodEngineGlobal` резолвляться (pointer resolution) → Advance Time / Sun freeze мають глобали |

**Висновок:** ламається рівно те, що йде через `MH_CreateHook`. Все, що
резолвиться як адреса/global і викликається напряму, — живе. Це підтверджує
системність проблеми (B).

---

## 2. Проблема (B): системний збій MinHook — `MH_ERROR_MEMORY_ALLOC`

### Механіка (з `build-clean/_deps/minhook-src/src/buffer.c`)

- MinHook шукає вільний блок розміром `0x1000` (`PAGE_EXECUTE_READWRITE`)
  **лише у вікні ±1 ГБ** навколо цільової адреси
  (`MAX_MEMORY_RANGE = 0x40000000`, функції `FindPrevFreeRegion` / `FindNextFreeRegion`).
- Якщо у цьому вікні немає жодної вільної сторінки → `GetMemoryBlock` повертає
  `NULL` → `AllocateBuffer` → `NULL` → `MH_CreateHook` повертає
  **`MH_ERROR_MEMORY_ALLOC`**.

### Чому це системне

У логу **кожен** `MH_CreateHook` (10+ різних адрес у `player`/`teleport`/
`world`/`inventory`/`friendly`) впав з однаковою помилкою, а всі native-call
резолви (`EquipEffectRefresh @ 0x140659510`, `ResizeSocketVector @ 0x14488C46D`,
string-anchor таблиці) — спрацювали. Це означає: **алокатор MinHook не може
знайти вільне місце біля коду гри**, а не що кожна сигнатура «з'їхала».

Найімовірніші причини (потрібно підтвердити live-діагностикою):
1. Новий EXE резервує/коммітить великий суцільний діапазон навколо
   `0x140000000..0x149000000` (великий `SizeOfImage`, packed-розкладка або
   велика arena), закриваючи все ±1 ГБ вікно.
2. Антитампер/пекер гри резервує цей регіон ще до ініціалізації ASI.

### Діагностика (перший крок, до будь-якого re-find)

1. Прикріпити CE до процесу (MCP bridge має бути живим) і зняти
   `enum_memory_regions_full` навколо бази модуля (`0x140000000`).
2. Знайти, де саме є вільні 4 КБ сторінки у вікні ±1 ГБ і що займає регіон.
3. Перевірити `SizeOfImage` нового EXE та `EnumProcessMemory` (VirtualQuery)
   безпосередньо в процесі.

### Варіанти фіксу (вибрати після діагностики)

- **А. Змінити хук-бекенд** (рекомендовано як резервний варіант): замінити
  MinHook на кастомний inline-jump (як уже зроблено для DX12) або на бібліотеку
  з дальнім 64-бітним переходом (напр. абсолютний `mov rax, imm64; jmp rax`),
  якій не потрібен трамплін поруч.
- **Б. Підказати MinHook розмістити пул раніше**: викликати алокацію буфера
  біля цільового регіону до того, як гра зарезервує простір (але це ненадійно,
  якщо вікно вже зайняте).
- **В. Розслідувати резервацію регіону** і, якщо це пекер/антитампер, врахувати
  в `section_filter` / readiness (наприклад, чекати, поки код розпакується в
  інший діапазон).

> ⚠️ Навіть після фіксу (B) кожну з адрес, що «збіглася», треба перевірити на
> **унікальність і правильність** (див. §4): зараз невідомо, чи збігся AOB на
> правильному місці, чи на випадковому false-positive у новому бінарнику.

---

## 3. Проблема (A): сигнатури для пере-скану (re-find)

Сигнатури, які в логу дали `NOT FOUND` (або дали лише legacy/alt fallback) і
мають бути **пере-знайдені проти нового EXE**. Поточні значення (з `offsets.h`):

| # | Символ | Поточний AOB (для порівняння) | Примітка |
|---|---|---|---|
| 1 | `kSig_RegisterCrimeEvent` | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC C0 00 00 00 4D 8B F0` | crime dispatch + UI-банер |
| 2 | `kSig_InvGetItemQty_Legacy` | `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B E8 0F B7 DA` | fallback item-count |
| 3 | `kSig_FieldTimeTick_Pre201` | `48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C` | freeze time fallback |
| 4 | `kSig_WindPack_Pre201` | `48 89 5C 24 08 57 48 83 EC 30 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1` | wind/cloud/fog fallback |
| 5 | `kSig_FriendlySetNpc201` | `4C 8B DC 53 55 56 57 41 56 41 57 48 83 EC 68 48 8B FA 48 8B F1 0F B7 42 04` | trust NPC setter |
| 6 | `kSig_FriendlySetPet201` | `49 89 E3 53 55 56 57 41 56 41 57 48 83 EC 68 48 89 D7 48 89 CE 0F B7 42 04 66 41 89 43 08 49 8D 4B 08 E8 ? ? ? ? 31 ED 39 6E 1C` | trust pet/mount setter |
| 7 | `kSig_FriendlyGetNpc201` | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 1C 00` | trust NPC getter |
| 8 | `kSig_FriendlyGetPet201` | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 3C 00` | trust pet/mount getter |
| 9 | `kSig_FriendlySetNpc` | `4C 8B DC 53 55 56 57 41 56 48 83 EC 60 48 8B FA 48 8D 69 38` | legacy setter |
| 10 | `kSig_FriendlySetPet` | `49 89 E3 53 55 56 57 41 56 48 83 EC 60 48 89 D7 48 8D 69 18` | legacy setter |
| 11 | `kSig_FriendlyNpcTrustWriter` | `48 89 5C 24 10 66 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC E0 00` | прямий writer NPC |
| 12 | `kSig_FriendlyAlertDisp` | `48 89 5C 24 10 66 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 B0 48 81 EC 50 01` | faction/UI dispatcher |

### Char-manager anchors (5 з 6 зламані)

```text
anchor[0], mov +0x0C = 4D 8B 00 49 C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8
anchor[1], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 48 8B 08 4D 8B 00 49 C1 E8 20
anchor[2], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 82 90 00 00 00 48 8D 54 24 ?? 48 8B 08 E8
anchor[3], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 81 58 01 00 00 48 8D 55 ?? 48 8B 08 E8
anchor[4], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 81 60 01 00 00 48 8D 55 ?? 48 8B 08 E8
anchor[5], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 07 48 8D 54 24 ?? 48 8B 08 E8
```

Лог каже `1 anchors verified` — вижив лише один. Потрібно **пере-знайти сайти
виклику char-manager у 2944** і перезібрати набір анкерів (консенсус, а не
перший збіг — див. коментар у `offsets.h` і playbook §2D).

> ⚠️ `kSig_StatCommit` (`48 89 5C 24 10 ... 4C 39 C3`) **НЕ треба** re-find:
> його видалили ще в TU 2.01, а для 2944 (новіша) правильною є гілка
> «modern continuous stat-pin» — див. §6.

---

## 4. Сигнатури, що «збіглися, але не хукнулись» (потрібна перевірка ПІСЛЯ фіксу MinHook)

Ці AOB у логу дали `MH_CreateHook failed`, тобто *щось* знайшли. Після вирішення
проблеми (B) їх треба **перевірити на унікальність/правильність адреси** —
частина може виявитись false-positive, і тоді їх теж re-find:

| Символ | Поточний AOB | Фіча |
|---|---|---|
| `kSig_DamageApply` | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9` | damage множники |
| `kSig_DamageApply_Alt` | `48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9` | damage множники (alt) |
| `kSig_CombatTimingEval` | `48 8B C4 41 55 41 56 41 57 48 83 EC 70 C5 78 29 40 A8` | Easy Parry/Evade |
| `kSig_MoveUpdate` | `48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56` | позиція / Super Jump |
| `kSig_EvaluateCrimeWantedState` | `48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 48 8B DA 4C 6B D9 38 41 B0 07` | crime bypass |
| `kSig_InvGetItemQty` | `66 89 54 24 10 53 57 48 83 EC 28 0F B7 DA` | item-count accessor |
| `kSig_FrameTimerBody` / `_Pre201` | `48 8B F9 48 8B 51 60 8B 42 64 89 42 60` / `48 8B F9 48 8B 41 60 C5 FA 10 40 64 C5 FA 11 40 60` | Game Speed |
| `kSig_FieldTimeTick` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 64 24 20 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C` | Freeze Time |
| `kSig_WeatherRain` | `48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 6C 01 00 00` | Rain |
| `kSig_WeatherSnow` | `48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 68 01 00 00` | Snow |
| `kSig_WeatherDust` | `48 8B 41 ?? 41 B8 40 00 00 00 48 85 C0 41 B9 60 01 00 00 48 8D 50 18 B8 CC 01 00 00 49 0F 44 D0` | Dust |
| `kSig_WindPack` | `48 89 5C 24 08 57 48 83 EC 20 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1` | Wind/Cloud/Fog |

---

## 5. Офсети / ABI, які треба пере-верифікувати для 2944

Навіть якщо AOB виживе, зміна ревізії могла зрушити структури/ABI. Перевірити:

### Ревізійно-залежні значення (зараз для 2944 вибирається «legacy»)

| Що | Зараз для 2944 | Має бути перевірено | Файл |
|---|---|---|---|
| TLS realm flag | `0x1F2` (legacy) | реальний offset у 2944 (відомі: 2760→0x1FD, 2850→0x1EC) | `version_mapping.cpp::RealmFlagOffsetForRevision` |
| Move-owner offset | `0x298` (legacy) | реальний (2760/2850→0x2B8) | `version_mapping.cpp::MoveComponentOwnerOffsetForRevision` |
| Inventory core-global `mov` offset | `0x15` (legacy) | реальний (2760/2850→0) | `version_mapping.cpp::InventoryCoreGlobalMovOffsetForRevision` |
| Item def bucket type | `0x428` (бо rev≥2625) | підтвердити, що 2944 тримає 0x428 | `version_detect.cpp::GetItemDefBucketTypeOffset` |
| Worker patch support | `false` (тільки 2850) | пере-RE під 2944 або залишити вимкненим | `worker_logic.h::WorkerPatchSupportedForRevision` |

### Player / character

- stat entry: stride `0x90`, поля `+0x08/+0x18/+0x20/+0x28/+0x30`, `root+0x58`.
- char list `manager+0xB8/+0xC0`; possessor round-trip `owner+0xA0 → +0xD0`;
  type descriptor `owner+0x88`; actor `owner+0x68`; marker `actor+0x20`; root `marker+0x18`.

### Movement / teleport

- position `+0x90`, desiredVel `+0xC0`, velocity `+0xD0`, dest1 `+0x1A0`.
- (невидимі через early-return, але обов'язкові): `kSig_LocoStepper`,
  `kSig_TravelToNode(+Pre201/Legacy)`, map-marker (`kSig_MarkerPattern/Player/Protection`,
  `map_marker.h`: global→`+0xA8`→dest `+0x20/0x24/0x28`), area-name resolver.

### Inventory / crime

- slot stride `0xC8`, quantity `+0x10`, typeId `+0x08`; placement stride
  **перевірити** (відомі: 0xD8 legacy / 0xE0 modern), slotIdx `+0xD8`.
- holder buckets `+0x18/+0x20`, bucket `type 0x10 / maxSlots 0x14 / used 0x12 / expand 0x1A`.
- TrItemValue ctor / commit / holderInsert / commitPlacement / freePlacements
  (нативні call-примітиви Add Item — зараз **не досягаються** через early-return).

### World

- time struct delta `+0x64`, scaledDelta `+0x68`; TOD manager `+0x2F8`,
  currentHour `+0x3D0`, lower/upper `+0x3D4/+0x3D8`.
- weather CN/WN офсети (`CN::FOG_A…`, `WN::DIR_X…`) — перевірити, якщо
  atmosphere-фічі важливі.

### Trust / friendly

- record: trust value `+0x20`, cap 100 (0..100). Перевірити layout після re-find
  сетерів/гетерів.

---

## 6. Ревізійні гейти, які треба оновити для PE 2944

Це — **перше, що треба зробити в коді** (ще до re-find сигнатур), бо від цього
залежить, які гілки взагалі виберуться.

1. **`src/core/version_mapping.cpp`**
   - `ModernTitleUpdateForRevision(2944)` → додати мітку реального TU
     (потрібно дізнатись назву; інакше fallback бреше «1.18.02»).
   - `UsesTu201CompatibleRevision(2944)` → скоріш за все має стати `true`
     (2944 новіша за 2850, яка вже «modern»), але **тільки після live-перевірки**
     ABI (stat-pin, inventory transaction, realm).
   - `MoveComponentOwnerOffsetForRevision(2944)`, `RealmFlagOffsetForRevision(2944)`,
     `InventoryCoreGlobalMovOffsetForRevision(2944)` → реальні значення з діагностики.
   - `MayUseLegacyFuzzySignaturesForRevision(2944)` → залишити `false` (не legacy).

2. **`src/core/version_detect.cpp`**
   - Прибрати/уточнити fallback `"Crimson Desert 1.18.02 (Active)"` для невідомих
     ревізій, щоб не вводити в оману (лог зараз показує несправжню версію).

3. **`src/core/readiness.cpp` + `src/core/mod.cpp`**
   - `ReadinessProfileForRevision(2944)` зараз дає `LegacyComplete` → набір
     legacy-sentinels, які не існують у 2944 → 3-хвилинний timeout.
   - Створити профіль/набір sentinels під 2944 (тільки ті AOB, що реально
     існують у новому бінарнику; перевірити наявність КОЖНОГО до внесення).

4. **`src/game/player.cpp` (Install)**
   - `UsesTu201CompatibleRevision(2944)==false` змушує шукати `kSig_StatCommit`
     (видалений). Після пункту 1 ця гілка сама перемкнеться на modern
     continuous stat-pin. `kSig_StatCommit` re-find **не потрібен**.

5. **`src/game/worker_logic.h`**
   - `WorkerPatchSupportedForRevision(2944)` — або пере-RE патч під 2944
     (нові байти `kWorkerPatchOriginal/Enabled` + `kSig_WorkerMaxLevelAndSkills`),
     або свідомо залишити вимкненим.

---

## 7. Каскадні early-return (треба лагодити структуру, не тільки сигнатури)

- **`src/game/teleport.cpp:1772-1774`** — якщо `movement-update` не хукнувся,
  `return false`, тому fast-travel trigger, scene/area resolver, loco-stepper і
  map-marker підсистема **взагалі не ініціалізуються** (в логу немає їх рядків).
  → Зробити movement-update нефатальним (продовжити інсталяцію) або перевпорядкувати.

- **`src/game/inventory.cpp:2072-2078`** — якщо `item-count accessor` (primary +
  legacy) не хукнувся, `return false`, тому holder-resolver, Add Item примітиви
  і money-хуки **не запускаються** (хоча catalog уже побудований).
  → Те саме: нефатальний item-count або re-order.

Ці два `return false` приховують реальний масштаб поломки; після їхнього
виправлення в логу з'явиться ще кілька рядків (fast-travel, scene-resolver,
holder-resolver), які треба буде додатково оцінити.

---

## 8. Порядок робіт (рекомендований)

1. **Зафіксувати базлайн** нового EXE: шлях, PID, PE-версію, SHA-256, module
   base, `SizeOfImage`. (playbook §1)
2. **Підняти CE MCP / прикріпитись** до процесу (зараз bridge недоступний).
3. **Діагностувати (B) MinHook**: зняти memory regions навколо `0x140000000`,
   знайти вільні 4 КБ у вікні ±1 ГБ; вирішити backend / pool placement.
4. **Оновити ревізійні гейти** (§6) — інакше все нижче вибирає legacy-гілки.
5. **Пере-скан AOB** (§3, §4): для кожної — match count, RVA, межі функції,
   ABI (Win64 args), RIP-резолв, унікальність. Записувати у record (§9).
6. **Пере-верифікувати офсети/ABI** (§5) навіть для тих AOB, що вижили.
7. **Розблокувати early-return** (§7) і заново оцінити приховані поломки.
8. **Зібрати** (`cmake -S . -B build-update -G Ninja -DCMAKE_BUILD_TYPE=Release
   -DBUILD_TESTING=ON`), прогнати CTest, потім live-валідацію по feature ledger
   (playbook §5).

---

## 9. Форма запису доказів (на кожну змінену сигнатуру)

З playbook §2D — не замінювати AOB, поки не зібрано:

```text
Символ / фіча / споживач:
Старий AOB / новий AOB / причина wildcard:
EXE SHA-256 / PE / module base / секція:
Кількість збігів / кожен RVA:
Байти до/після кандидата / межі функції:
RIP-дисплейсмент / довжина інструкції / розрезолвлений target:
Win64 ABI (RCX/RDX/R8/R9, стек, XMM, return):
Таймінг оригінального виклику / потік:
Розкладка об'єкта / ланцюг вказівників / realm:
Форма хука (MinHook / inline) / overwrite/replay/jump-back:
Live-дія, що підтверджує правильну функцію:
```

---

## 10. Що НЕ робити

- ❌ Не шукати заново `kSig_StatCommit` (видалений у TU 2.01+).
- ❌ Не вважати, що «AOB збігся» = «адреса правильна» — після проблеми (B)
  обов'язкова перевірка унікальності.
- ❌ Не приймати перший збіг char-manager anchor за консенсус (playbook §2D).
- ❌ Не копіювати старі абсолютні VA / TLS-константи з README_TU200_OFFSETS.md
  чи TU200_RE_NOTES.md без свіжих доказів.
- ❌ Не скорочувати readiness timeout / не видаляти sentinels «щоб швидше» —
  спершу підтвердити, які sentinels існують у 2944.

---

*План згенеровано аналізом `Trinity.log` проти `src/game/offsets.h`,
`src/core/version_*.cpp`, `src/core/readiness.*`, `src/core/mod.cpp` та
MinHook `buffer.c`. Жодних змін у код внесено не було.*
