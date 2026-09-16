# Trinity architecture and reverse-engineering guide
> **Target Game**: Crimson Desert (BlackSpace Engine)  
> **Current mod**: v1.4.1 (vTweak by Lian), updated 2026-09-16 through the material-control fix.
>
> **Current evidence target**: **TU 2.02.00**, PE **1.0.0.2850**, timestamp **0x6AA22ABB**, preferred base **0x140000000**. Legacy branches are implementation history, not universal compatibility certification.
>
> **Author / Reference**: Lian

---

## Table of Contents
1. [Engine Architecture & Execution Environment](#1-engine-architecture--execution-environment)
2. [Dual-Realm State Management (Client vs Server Authority)](#2-dual-realm-state-management-client-vs-server-authority)
3. [Memory Safety & Structured Exception Handling (SEH)](#3-memory-safety--structured-exception-handling-seh)
4. [AOB Pattern Scanning & Multi-Anchor Consensus](#4-aob-pattern-scanning--multi-anchor-consensus)
5. [Actor Graph & Local Player Resolution](#5-actor-graph--local-player-resolution)
6. [Combat Subsystem & Assembly Interception (God Mode, Parry, Stats)](#6-combat-subsystem--assembly-interception-god-mode-parry-stats)
7. [Physics, Locomotion & Free Flight Mechanics](#7-physics-locomotion--free-flight-mechanics)
8. [Inventory Architecture, Hex Layout Drift & Safe Spawning](#8-inventory-architecture-hex-layout-drift--safe-spawning)
9. [Equipment Modification, Abyss Sockets & Dye Pipeline](#9-equipment-modification-abyss-sockets--dye-pipeline)
10. [World Simulation, Atmosphere & Time Control](#10-world-simulation-atmosphere--time-control)
11. [Runtime Binary Fingerprinting & Game Version Auto-Detection](#11-runtime-binary-fingerprinting--game-version-auto-detection)
12. [Diagnostics, verification, and unresolved paths](#12-diagnostics-verification-and-unresolved-paths)

Use [the offset reference](README_TU200_OFFSETS.md) for field widths and [the investigation notes](TU200_RE_NOTES.md) for evidence boundaries. Current validators and verified native callers take precedence over older comments in `offsets.h`.

---

## 1. Engine Architecture & Execution Environment

Crimson Desert is built upon Pearl Abyss's proprietary next-generation **BlackSpace Engine**. Unlike standard Unreal or Unity titles:
* **Object model**: The engine has custom owners, descriptors, and component graphs. Equipment components also expose **MSVC x64 RTTI**; current validators require exact Client/Server/Common equipment class names and primary Complete Object Locator data.
* **DirectX 12 Hooking & Compositing**: Trinity hooks the swapchain creation (`CreateSwapChainForHwnd`) and `Present` dispatcher in `dxgi.dll`, allowing ImGui overlays to composite cleanly before Frame Generation / DLSS 3 frame interpolation.
* **Work scheduling**: UI reads use snapshots; refinement/socket/dye requests run through the game-thread pump. Scans are bounded and selected profile I/O uses value-only workers. Some older inventory/bulk/direct-swap paths still execute inline. Thread separation reduces contention but does not guarantee zero stuttering.

---

## 2. Dual-Realm State Management (Client vs Server Authority)

The engine executes two parallel internal worlds inside a single `CrimsonDesert.exe` process:
1. **Client Realm**: Responsible for visual rendering, animations, particle effects, HUD, and audio.
2. **Server Authority Realm**: Responsible for authoritative inventory storage, save-game persistence, quest milestones, and attribute states.

```text
       [ Single Game Process: CrimsonDesert.exe ]
                     |
       +-------------+-------------+
       |                           |
[ Client Realm ]           [ Server Realm ]
  - Renders Graphics         - Authoritative Data
  - Local Mirror Memory      - Save-File Disk IO
  - UI & Animation State     - Transaction Validation
       |                           |
       +==== Per-Frame Sync =======+ (Server reconciles & overwrites Client)
```

### The Per-Thread TLS Realm Flag
The engine determines which realm a thread is operating on via a flag in Thread Local Storage (TLS). **The flag slot moved in TU 2.01 (PE rev >= 2760 / 1.0.0.2760+)**:
```cpp
inline constexpr uintptr_t kOff_Teb_TlsPointer = 0x58;  // TEB.ThreadLocalStoragePointer
inline constexpr uintptr_t kTls_RealmFlag_TU201 = 509;  // u8: 0 = Client, 1 = Server (TU 2.01+ / PE rev >= 2760, 0x1FD)
inline constexpr uintptr_t kTls_RealmFlag       = 498;  // u8: 0 = Client, 1 = Server (TU 2.00 and earlier, 0x1F2)
```

> [!IMPORTANT]
> **The Dual-Realm Write Rule**:
> Resolve and validate **distinct objects for each realm**. Toggling TLS does not turn a server component into a client renderer. Writes are sequential, can partially succeed, and are not an atomic save transaction. Visual native calls require client RTTI/render state; allocation-capable native writes must execute in the appropriate realm with the previous TLS value restored.

`Inventory::RealmFlagAddress` obtains the calling thread's TEB via `NtQueryInformationThread`, then reads the TLS pointer chain. It probes the current and legacy flag locations and requires a boolean value. `DyeRealmGuard` restores the prior byte. A read of `gs:[0x58]` is not itself the final realm-byte address.

---

## 3. Memory Safety & Structured Exception Handling (SEH)

Dereferencing invalid pointers can trigger access violations (`0xC0000005`); readable stale allocations can also return plausible but wrong data. Trinity combines address/range checks, guarded accesses, and native identity/lifetime validation:

### A. Virtual Address Floor Validation
Pointers below `0x10000000` (`kMinPointer`) are discarded immediately to eliminate small integers, error codes, and unmapped low-memory:
```cpp
inline constexpr uintptr_t kMinPointer = 0x10000000;
```

### B. Hardware-Guarded SEH Wrappers
The shared guarded accessors catch access violations. Some native/legacy paths also use raw access; review each caller rather than assuming all accesses are covered. Conceptually:
```cpp
bool ReadPtr(uintptr_t addr, uintptr_t* out)
{
    if (addr < kMinPointer) return false;
    __try {
        *out = *reinterpret_cast<const uintptr_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
```

### C. Native lifetime and identity checks

- Equipment heap pointers use the stricter `IsEquipmentPointer` floor (`0x100000000`). Neither pointer floor nor `VirtualQuery` proves ownership.
- Equipment requires RTTI + owner backlink + the current native table descriptor. Cache only immutable module class information, not permission to reuse heap objects.
- Snapshot rows contain copied display values. An edit must rediscover and compare world, selection, type, instance, component, owner, and table/entry identity before writing.
- Inventory traversal checks holder/bucket/slot allocation and mutation epoch. Completed scan evidence is invalidated by churn.
- Expected SEH failures can still be expensive; avoid turning failed discovery into repeated per-frame exceptions.

---

## 4. AOB Pattern Scanning & Multi-Anchor Consensus

Because game updates shift static Relative Virtual Addresses (RVAs), Trinity locates functions and data tables dynamically using **Array of Bytes (AOB)** pattern matching with single-byte wildcards (`??`).

### Multi-Anchor Voting Consensus
Single signatures can occasionally match sibling globals (e.g., matching the server Character Manager instead of the client Character Manager). Trinity uses an array of independent call-site anchors:
```cpp
struct CharMgrAnchor {
    const char* sig;
    uintptr_t   movOff;
};

inline constexpr CharMgrAnchor kCharMgrAnchors[] = {
    // Nine caller-side anchors. Resolve the global for the inspected build;
    // a global address from an earlier IDB is not portable.
    // Each anchor keys only on ABI-fixed bytes between the load and the call
    // (`mov rcx,[rax]` = manager is arg1) plus literal struct offsets.
    // movOff = offset of the 7-byte `mov rax,cs:<global>` within the match.
    {"4D 8B 00 49 C1 E8 20 48 8D 54 24 ?? 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 12},
    {"44 8B 82 90 00 00 00 48 8D 54 24 ?? 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 12},
    {"44 8B 81 80 01 00 00 48 8D 55 ?? 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 11},
    {"45 8B 07 48 8D 55 ?? 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 7},
    {"44 8B 45 C0 48 8D 55 C8 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 8},
    {"44 8B C3 48 8D 54 24 58 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 8},
    {"45 8B 06 48 8D 54 24 30 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 8},
    {"44 8B 03 48 8D 54 24 30 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 8},
    {"C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", 8},
};
```
During initialization, matching anchors vote on the global. Multiple anchors reduce ambiguity; they do not replace validating the resolved graph. On the inspected TU 2.02 image, a longer `kSig_GameCoreGlobal` pattern resolves the core at `0x146C2D9F0`. Old relative-address relationships are corroboration, not discovery rules.

`ReadableSpans` uses VirtualQuery within SizeOfImage, with a section-header fallback when no spans are found. A unique match must still be checked against disassembly and callers. In particular, the old equipment-refresh and DyeApplySlot signatures matched unrelated functions despite plausible prologues.

---

## 5. Actor Graph & Local Player Resolution

The player entity is not a static object. When transforming, mounting, or transitioning cutscenes, the engine dynamically respawns actor objects.

```text
[ Character Manager Global ]
            |
            v
[ Character Vector: character*[] ] (Array of ~400 live entities)
            |
            v
[ Owner Object (SelfPlayer) ]  <----+ (Round-Trip Verified)
  + 0x88 -> Type Descriptor (Tag=1) |
  + 0xA0 -> Possessor / Controller  |
              |                     |
              v                     |
       [ Possessor Object ]         |
         + 0xD0 -> Pawn Reference --+
```

### The Possessor Round-Trip Identity Proof
During combat, multiple transient clone bodies may carry `ObjectType == 1`. To identify the **single true controlled body**, the engine verifies a bidirectional pointer round-trip:
$$\mathbf{*(*(owner + 0xA0) + 0xD0) == owner}$$
Use the round-trip to establish a particular owner/possessor relationship, then combine it with current-world membership and type checks. Client and server realms can have distinct live bodies; companion bodies can share a possessor. A single round-trip does not assign a protagonist index to every related object.

### Protagonists, mounts, and client replicas

- **TU 2.02 owner+0x50 is an entity ID**, not Kliff/Damiane/Oongka numbering. Primary identity paths gate the older party-index interpretation to revision <2800.
- `Inventory::IdentifyCharacterFromComp` uses native equipment plus known item IDs and key/name fallbacks. It must not identify an object by consulting the very Player cache it populates. A changed outfit can leave identity unknown; unknown must not silently become Kliff.
- `Player::GetControlledOwner` is separate from indexed party owners/actors. Shared-possessor humanoid bodies may still need stat upkeep without a successful equipment-derived character index.
- Mount discovery requires native descriptor tag 5, a validated equipment table, and positive-instance gear. Local mount tags 0–4 map to Chamfron, Horse Armor, Saddle, Stirrups, Horseshoes.
- Client stat and dye discovery use the native registry: root+0x38 → registry+8 → map, 0x100-byte buckets with at most 31 key/index pairs. A positive cache retains the actual pair/node/owner binding and rechecks it. The registry is not a guessed mapping from party index to entity key.

---

## 6. Combat Subsystem & Assembly Interception (God Mode, Parry, Stats)

All damage calculations, hit reactions, and vital modifications pass through a central dispatch choke point: `pa_StatApplyDelta` (`kSig_DamageApply`).

```cpp
int64_t __fastcall hkDamageApply(void* targetOwner, uint16_t statusId,
                                 int64_t time, int64_t delta, uintptr_t sourceCtx,
                                 char a6, char a7, char a8, char a9, char a10, void* out)
```

### A. Register Calling Conventions & Semantics
* `targetOwner` (`RCX`): The victim's vital-owner component (`marker + 0x18`).
* `statusId` (`DX`): Native dispatcher status ID. Outgoing health classification resolves the HP ID from the appropriate native realm tables; it is not a universal hardcoded zero.
* `delta` (`R9`): Signed integer representing stat change (negative = incoming damage, positive = healing).
* `a6..a10`: Positional native flags. Older gameplay-assist branches manipulate some of them, but a numeric tuple alone does not establish lethal/nonlethal semantics. In particular, both lethal and NoDead skills can use cause/type `(8,10)`.

### B. Perfect Parry (Just Guard) Implementation
Current `hkJustWindowEval` calls the original evaluator first, then can promote a failed timing result when Easy Parry/Evade is enabled and the matching guard/evade input was observed within **700 ms**. The verified evaluator is `0x1407FCC80`. Damage-dispatch assist branches also remain in `ApplyClassifiedDamage`; the old snippet that changed only reaction bytes is not the complete implementation. Obsolete `kSig_JustCore` patterns are not used.

### C. God Mode, No Fall Damage & Infinite Stamina
* **God Mode / stamina / spirit**: ownership-aware hooks and upkeep validate current stat entries rather than retaining a stale root after reload. See `TrackedStatEntry`, `ClientStatEntry`, and `PinValidatedEntry`.
* **No Fall Damage**: the branch also requires the player target, no enemy attacker, null source context, and landing/cause checks. A null source alone does not classify every damage event as a fall.
* **Stat view**: owner+0x68 → actor+0x20 → marker+0x18 → root; require `*root == marker`. Array at root+0x58, u32 count at root+0x60, entry stride 0x90, **u16 entry IDs**. Native observations had 20 entries; `ReadStatView` accepts counts 1–256 and never scans a fixed 64 records.
* **Gauge IDs**: the implementation retains version-specific stamina/spirit alias predicates. **48/49 are heat/frost accumulation**, not mount stamina. Do not copy older fixed-ID tables into new hooks.

### D. One-hit kill and nonlethal damage

The native damage-event wrapper (`0x141FE7470` on the inspected build) supplies a scoped source/victim/time context. `DamageBuffData+0x118` is the NoDead byte, checked with its native class identity. Only the verified HP dispatcher return (`0x141FE7FDE`) can consume that context. `damage_policy::ApplyOutgoing` requires a verified lethal controlled-player HP hit, valid health/floor state, and matching event identity. NoDead, unknown, scripted, and non-HP cases keep native behavior; no forced floor reset is used.

---

## 7. Physics, Locomotion & Free Flight Mechanics

### Why Integrator Velocity Scaling Fails
The Havok character proxy does not accept naive velocity multiplication. The locomotion engine runs a closed-loop feedback servo:
1. Calculates drive velocity.
2. Passes drive velocity into sub-step driver `sub_2F49550`.
3. Measures actual displacement: $\Delta P = (P_{\text{after}} - P_{\text{before}}) / dt$.
4. Clamps any overshoot back to expected limits.

Directly modifying position in the Havok integrator causes the servo to detect an illegal displacement, causing severe rubberbanding and stuttering.

### The Sub-Step Arg3 Velocity Injection
Trinity hooks the locomotion sub-step driver (`kSig_LocoStepper`):
```cpp
void hkLocoStep(void* comp, float dt, float* vel, char a4, char a5, char a6, char a7)
```
* **Super Run**: Directly scales horizontal drive velocity components (`vel[0] * superRunMult`, `vel[2] * superRunMult`) in `arg3` before the servo processes it.
* **Free Flight**: When airborne, holding ascend (Caps Lock / RB) or descend (Ctrl / RT) directly overrides vertical velocity `vel[1] = flightSpeed`. When buttons are released, normal Havok gravity and physics instantly resume without hover clamps.

---

## 8. Inventory Architecture, Hex Layout Drift & Safe Spawning

### Cross-Version Binary Layout Comparison Table

| Parameter / Offset | TU 1.10 – 1.15 | TU 1.16 | TU 1.17 – 1.18+ | TU 2.00 | TU 2.01 – 2.02 | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Slot Stride** | `0xC0` (192 bytes) | `0xC8` (200 bytes) | `0xC8` (200 bytes) | `0xC8` (200 bytes) | `0xC8` (200 bytes) | Memory distance between items in bag array |
| **`TrItemValue` Buffer** | `0xC0` (192 bytes) | `0x108` (264 bytes)| `0x108` (264 bytes)| `0x108` (264 bytes)| `0x108` (264 bytes)| Working allocation size for item values |
| **Item Definition Array**| `table + 0x50` | `table + 0x50` | `table + 0x58` | `table + 0x58` | `table + 0x58` | Pointer array holding item definition rows |
| **Abyss Sockets Pointer**| `+0x58` | `+0x58` | `+0x60` | `+0x60` | `+0x60` | Offset to 5-slot socket vector in item value |
| **Bucket Type Offset** | `+0x42` | `+0x42` | `+0x418` | **`+0x428`** | **`+0x428`** | Item definition's default storage selector; source version branches |
| **Equip Entry Stride** | — | — | Historical layouts need per-build validation | Historical layouts need per-build validation | **TU 2.02: `0xD0`, tag `+0xC8`, no scored fallback** | Distinct from the bag slot stride |
| **TLS Realm Flag** | `498` | `498` | `498` | `498` | **`509` (`0x1FD`)** | Client/server selector byte in TLS |

### Dynamic Slot Stride Dispatcher
```cpp
uintptr_t GetSlotStride()
{
    const GameVersionInfo& info = GetGameVersion();
    if (info.tu <= GameTU::TU_1_15)
        return 0xC0; // 192 bytes on TU <= 1.15
    return 0xC8;     // 200 bytes on TU >= 1.16
}
```

### Bucket Type & Slot-Expansion Setter on TU 2.00+
Since TU 2.00.00 (PE rev >= 2625) `GetItemDefBucketTypeOffset()` returns `0x428` (confirmed from `InvHolderInsert` / `InvCommitPlacement` binary audits). The engine's own slot-expansion setter kept its 5-arg prototype `f(holder, int* outErr, void* unused, u16 bucketType, u16 count)` through TU 2.01, then **TU 2.02 dropped the dead 3rd pointer arg**: `f(holder, int* outErr, u16 bucketType, u16 count)` — bucketType arrives in `r8` and count in `r9`. The detour prototype must match the target convention or MinHook's trampoline misroutes the registers. The legacy 5-arg signatures must never be tried while the 2.02 signature resolves (one of them lands on an unrelated function — the root cause of the "engine re-stamps vanilla expansion" bug).

### The Free-Space Gate (Never Hooked)
The "inventory full" pre-check before the insert planner (free = `(i16)cap(+0x14) - (i16)used(+0x12)` for non-stackables, a stack-max formula for stackables) has **no unique byte shape** in the 2.02 image (static hunts on the ~375 MB EXE found none — the refusal error is runtime-resolved). The mod keeps the gate inputs healthy instead: `RepairUsedSlots` recounts `used` downward to physical occupancy, and the anti-shrink guards in `ApplySlotCapToHolder` / `StampExpandOverride` keep expansion overrides in place.

### Item creation and current limitations

The primary path allocates an instance ID, constructs `TrItemValue`, calls the native insertion planner, and commits server/client placements in their respective realms. Recent runtime logs resolve the constructor to `0x1423507B0`.

The actual implementation also has fallbacks: synthetic instance IDs/constructor data, main-bag or first-valid-bucket selection, direct stacking/copying when native placement fails, and additional post-commit field writes. `CommitAdd` reports success when **either** realm succeeds (`okClient || okServer`). Consequently, **Added is not proof of a complete native acquisition or later transfer/retrieval operation**. The Kuku Pot report remains a candidate item-state/synchronization issue until the exact spawned item and a naturally obtained control are compared. No pot-specific repair is implemented by the material or equipment-frame-time fixes.

### Real-Time Differential Inventory Change Tracker & Buyback System
To prevent displaying unobtained master database items as "missing", Trinity employs a real-time auto-diff snapshot engine:
1. **Baseline**: build an aggregate type/quantity snapshot across resumable inventory slices; publish only after a complete traversal with current mutation checks.
2. **Comparison**: at most 32 type comparisons and one history update per visit. A completed lap schedules the next after 3000 ms; this is not a full scan every 1500 ms.
3. **Loss classification**: quantity reductions can be labeled Sold / Discarded after equipped-item checks. This is an inferred quantity change, not a captured proof of the exact native sale event.
4. **Ledger**: persist history in `Trinity_LostItems.txt`. Restoration uses the add-item path and inherits its limitations.

---

## 9. Equipment Modification, Abyss Sockets & Dye Pipeline

### A. Equipment Component Access
Worn equipment sits inside the character's actor component:
$$\text{Actor} \longrightarrow \text{*(*(actor + 0x68) + 0x38)} = \text{Equip Component}$$

That route is only a candidate. `ReadNativeEquipmentTable` requires Client/Server/Common MSVC RTTI and `comp+8 → owner+0x68 → sub+0x38 == comp`. The descriptor is at comp+0x90, array at descriptor+8, count at descriptor+0x10; count is 1–64, equipped-entry stride 0xD0 and tag +0xC8. A table-shaped allocation alone is insufficient.

### B. Dye Rendering Pipeline
* **Palette Encoding**: 10 distinct color families with a 10x10 shade matrix + neutral gradient tones.
* **Custom RGB Mixer**: Directly injects 24-bit RGB values (`R, G, B: 0..255`) into the material channel descriptor.
* **Material/condition**: UI material 0 maps to `0xFFFF` natural; 1–10 are template IDs, not a promise of a particular metallic/glossy appearance on every part. UI condition 100 = pristine, 0 = worn, encoded as `(100 - percent) * 127 / 100`.
* **Cross-Version Fallback**:
  * Modern TU 1.17+: Stack frame size `0x50` (`kSig_DyeApplyBatch`).
  * Legacy TU 1.10–1.16: Stack frame size `0x120` (`kSig_DyeApplyBatch_Legacy`).
* **TU 2.02 Status**:
  * `kSig_DyeApplyBatch` (VA `0x1409165D0`), `kSig_DyeUpsert` (`0x142355870`), `kSig_DyeVisualSet` (`0x1409170C0`) and `kSig_DyeVisualClear` (`0x140918770`) all carry over.
   * `kSig_DyeApplySlot` resolves to an unrelated hash/registry utility at `0x142B41E60` and remains disabled. This does not disable the genuine per-channel visual leaves. Mount requests use the mount's own client target, one channel per visit at least 16 ms apart, without player dye-ack batches or automatic re-equipping.
   * Old standalone DyeRecordRemove signatures are unresolved; the native indexed shift helper has a different contract. Current explicit clears store per-channel clear records and invoke the validated client clear leaf, preserving the rest of the item payload.

### C. Abyss Sockets Architecture
Constructed socket count varies by item and must be read from its vector:
* **Record Stride**: 6 bytes per socket (`kSocketRec_Stride = 6`).
* **Record Field**: Offset `+0x00` stores the `uint16_t` Abyss Rune Type ID (`0xFFFF` = empty socket).
* **Other fields**: durability u16 +2, index u8 +4, padding +5. Preserve durability/padding rather than inventing a marker/state value.
* **Explicit layout selection**: modern vector at +0x60, size +0x68, capacity +0x6C, unlocked **u8** +0x70; legacy +0x58 only via its version branch. An empty modern vector is not a reason to probe another layout.
* **Bounds**: unlocked <= size <= 5 and size <= capacity, with fresh header/range validation. Never resize metadata to fabricate five sockets.

### D. Refinement/socket queue and frame-time work

`RefineRequest` stores world/selection/type/instance/source/table identity and per-field serials. The queue has a 64-request bound and a 15-second lifetime. New values supersede older values for the same field; superseded completions must not overwrite the new result.

One request is pumped at least 16 ms apart. Both component realms are freshly verified, and holder cursors alternate: at most 2048 records/8 headers with a cooperative ~1 ms slice budget, versus the old 256-record/50 ms retry. Invalid/unavailable paths back off 500 ms. The budget applies to the scan portion, not every operation in the tick. Completion/matches reset on mutation or allocation changes. UI publication stays capped at 200 ms even while retries run; an unchanged value is not a reason to rebuild all display metadata.

`Synced` requires verified client/server copies plus complete traversal of both holders. It does not acknowledge a game-save write. Equipment-profile I/O takes a value copy and runs in a worker; automatic replay of historical character/tag profiles remains disabled. Bulk bag/direct equip-swap paths still have older inline work and are not covered by this queue claim.

### E. Material retouch and payload preservation

The former Material/Condition `touched` flag was unused, and `PumpDyeRetouch` never became active; it also skipped All zones. The replacement is `Dye::Retouch` → `EnqueueDye` → `ProcessRequest`:

1. Queue identity is established immediately, with a 350 ms debounce. Only an unstarted retouch can be coalesced; latest value wins separately for material and condition.
2. On the game thread, revalidate the source and read current zone records. `BuildDyeRetouchRecords` changes only material +4/+5 and/or condition +11, keeping each zone's RGB/group/alpha/other payload.
3. Missing or explicitly cleared zones are skipped. All zones uses the existing dyed-zone mask, not a copy of zone 1's color. Empty selection produces `NoDyedZones` and a UI prompt to choose a color.
4. `RequestedDyeMask` controls batch, upsert, visual leaves and targeted readback. Mounts retain staged sparse-channel processing. Recheck identity throughout native operations.

Native `0x14074A930` copies material from record+4 into the render parameter block at +0x28; `0x14074AA60` performs the downstream material/part lookup. `BuildSetRecord` now preserves natural `0xFFFF` instead of substituting template 1. Clear detection additionally requires zero RGB/alpha/+12 and a high-bit condition sentinel: natural material by itself is not a request to clear color.

The native upsert's replacement branch copies bytes 0–12. `SameDyePayload` compares those 13 bytes, ignoring storage tail 13–15. Readback confirms data agreement, not the visual effect of every template on every mesh.

### F. Named Dye Profiles

`SaveProfile` captures all current channels from the loaded equipment on the game thread, then saves a value-only copy asynchronously. It does not parse raw game save files. Apply restores all 12 zones, with explicit clear records for zones absent from the saved mask. Player/mount mode, revision and typeId must match; instance IDs may differ between uses, but each queued application is still tied to a current exact item.

`Trinity_DyeProfiles.dat` beside the ASI uses magic `TRDYPR01`, fixed 300-byte records, FNV32 integrity checksum, and a 64-profile limit. Names occupy 64 bytes including terminator. Writes use unique temporary files, FlushFileBuffers and MoveFileExW; memory publication follows successful replacement. Corrupt/unsupported files are not overwritten as empty libraries. This library is distinct from `Trinity_DyeCache.dat` and the historical equipment INI.

---

## 10. World Simulation, Atmosphere & Time Control

### A. Time of Day Freezing
Freezing time requires locking both layers:
1. **Numeric Simulation Clock**: Hooks `kSig_FieldTimeTick` and forces elapsed delta time to `0.0`.
2. **Sun Celestial Position**: Clamps the render manager's lower and upper azimuth limits to freeze sun position without stopping physics.

### B. Environment & Weather Override
* Locates the global `EnvManager` via safe pointer scan.
* Directly drives rain intensity, snow density, wind multipliers, turbulence lift, fog density, and cloud altitude parameters.

---

## 11. Runtime Binary Fingerprinting & Game Version Auto-Detection

### The Static PE Header Trap
Some older releases shared the PE resource version `1.0.0.2474`, making that value insufficient to distinguish their layouts. Do not generalize that limitation to every title update: the inspected TU 2.02 PE reports `1.0.0.2850`.

### The PE Revision Ladder (TU 2.00+)
For the recorded TU 2.00+ releases the revision moves per update. Current `DetectVersion` reads PE metadata and evaluates dye signature fingerprints, then gives revision thresholds precedence in its decision ladder:

| PE Revision | Title Update |
| :--- | :--- |
| `1.0.0.2474` | TU 1.18.02 (static header value) |
| `1.0.0.2625` | TU 2.00.00 |
| `1.0.0.2658` | TU 2.00.01 |
| `1.0.0.2692` | TU 2.00.02 |
| `1.0.0.2760` | TU 2.01.00 |
| `1.0.0.2850` | **TU 2.02.00** (detected at `revision >= 2800`) |

### The Live Machine Code Fingerprint Solution (legacy fallback, TU <= 1.18)
Trinity uses the following decision structure (excerpt; omitted cases are documented in `src/core/version_detect.cpp`):
```cpp
const bool hasModernDyeBatch = (mem::FindPattern(game::kSig_DyeApplyBatch) != 0);
const bool hasLegacyDyeBatch = (mem::FindPattern(game::kSig_DyeApplyBatch_Legacy) != 0);

if (g_versionInfo.revision >= 2800) {
    g_versionInfo.tu = GameTU::TU_1_18_01_Plus;
    snprintf(g_versionInfo.displayStr, sizeof(g_versionInfo.displayStr),
             "Crimson Desert TU 2.02.00 (Active)");
} else if (g_versionInfo.revision >= 2750) {
    /* ... TU 2.01.00 ... */
} else if (g_versionInfo.revision >= 2690) {
    /* ... TU 2.00.02 ... */
} else if (g_versionInfo.revision >= 2650) {
    /* ... TU 2.00.01 ... */
} else if (g_versionInfo.revision >= 2625) {
    /* ... TU 2.00.00 ... */
} else if (hasModernDyeBatch) {
    g_versionInfo.tu = GameTU::TU_1_18_01_Plus;
    snprintf(g_versionInfo.displayStr, sizeof(g_versionInfo.displayStr),
             "Crimson Desert TU 1.18.02 (Active)");
} else if (hasLegacyDyeBatch) {
    g_versionInfo.tu = GameTU::TU_1_14;
    snprintf(g_versionInfo.displayStr, sizeof(g_versionInfo.displayStr),
             "Crimson Desert TU 1.14 - 1.15 (Legacy Compatible)");
}
```

## 12. Diagnostics, verification, and unresolved paths

### A. Disabled contracts must stay disabled

- **Equipment refresh**: TU 2.02's old signature lands in Challenge Description UI at `0x140E7C580`. `Equipment::Tick` skips the legacy refresh path for revision >=2800. Refinement/socket updates do not call this function or fabricate vector allocations to force refresh.
- **DyeApplySlot**: the `0x142B41E60` hash/registry function is not a dye API. Native visual set/clear leaves are separate functions and remain available through validated client targets.
- **RegisterCrimeEvent**: the removed hook used a `uint32_t` where the callee consumed a `const char*`, truncating pointers and faulting at `+0x1E4F217`. The current No Bounty feature uses the wanted-state evaluator and WantedInfo prices.

### B. Interpret status and logs narrowly

| Signal | What it establishes | What it does not establish |
|---|---|---|
| Equipment `Synced` | Current client/server values verified and both holder scans complete | Game save committed to disk |
| Old `components=2 holders=0` | Two component readbacks succeeded on that visit; no handled holder item match counted | That no values changed, or that there are no inventory holders |
| New incomplete `reason/cursor/restarts` | Where the request stopped and recorded scan progress | A single universal cause for every incomplete edit |
| Dye `visual=FFF` | Wrappers completed for the 12 requested channels | Every part/pixel has a visibly different material |
| Add Item `[server=1 client=1]` | Both add paths reported success | That both used native placement rather than direct fallback |

`Trinity.log` records the build stamp. Match reports to that stamp and artifact hash, not only the mod's numeric version. A stale pointer in a log is not a live inspection target after a process restart or reload.

### C. Frame-time budgets and overlay ownership

Enable **SYSTEM → Performance Diagnostics** when investigating hitches. `perfLogging=0` is the default; disabled metric calls skip the QPC sampling path. Every ten seconds, reports show the maximum measured invocation per subsystem and overlay fence wait when a maximum reaches 2 ms. Values are not averages, and column maxima may be from different frames.

The DX12 overlay keeps **four submission slots**, also matching ImGui's frames-in-flight allocation. Fence completion is required before allocator/upload reuse, independently of back-buffer index. Wait timeout, stale event, API failure, and device removal do not authorize unsafe reuse. WARP fixture success demonstrates synchronization behavior, not compatibility with every Frame Generation/HDR configuration.

### D. Inventory and trust caveats

`RepairInventorySlice` resumes occupancy counting and applies downward used-slot corrections only after validation. It does not reset item attributes or prove a save was repaired. UI refresh does not run the exhaustive RepairUsedSlots path. Some explicit legacy operations still invoke that helper.

The earlier guide's proposed `ScaleGain` with a hard delta-20 clamp is **not the current implementation**. `friendly.cpp` hooks SetNpc/SetPet prologues, maintains previous values by key, and clamps resulting trust to 0–100; it currently writes fields at +0x20/+0x28. Error `298648703` alone does not prove a specific trust threshold, anti-cheat event, or corrupted save. Diagnose the actual transaction and build before assigning a cause.

Kuku Pot retrieval remains an unconfirmed report. Check the exact item/type, spawned quantity, trainer version, error, and whether a naturally obtained copy works under the same conditions. Current Trinity spawning has direct-placement and partial-realm fallbacks (§8); generic item visibility is insufficient to diagnose pot-specific metadata requirements.

### E. Reproduce local checks

From the project directory, with CTest on PATH:

```powershell
powershell.exe -ExecutionPolicy Bypass -File "Build_Trinity.ps1" -Configuration Release -BuildOnly
ctest --test-dir "build-clean" --output-on-failure
python -B "tests/reload_source_tests.py"
python -B "scripts/inspect_dye_material_native.py" --va 0x14074A930 --bytes 304
```

The normal artifact is `build-clean/Trinity.asi`; extended hooks default OFF. Omitting BuildOnly also packages and attempts deployment, so it is not equivalent to this command. Record the resolved dependency commits: ImGui uses `v1.91.5-docking`, while MinHook currently tracks `master`.

For build **Sep 16 2026 22:27:27**, all **10 CTest suites** passed. SHA-256: `54D518FB2C26C1ACCCFF3421DC20C123E27049B90F8D98B545E9513B29A92772`, MD5: `4BB83BE9C2E1594B0B565E706E7209E2`. Tests cover payload preservation, sparse retouch, profile storage, current native layouts, scan churn/yield, damage policy, overlay synchronization, dye-editor target-locking, and native character identity resolution. They do not execute an end-to-end gameplay test. See [the material fix report](2026-09-16_fix-dye-material-report.md) for scope, evidence and the call path.
