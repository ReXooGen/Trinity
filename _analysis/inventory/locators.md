# Trinity — Complete Game-Executable Locator Inventory

Scope: every signature/AOB, hardcoded address/offset, string anchor and version-gate constant
that Trinity uses to find things inside `CrimsonDesert.exe`.

Primary source: `src/game/offsets.h` (1851 lines, read in full).
Supporting sources: `src/mem/scanner.{h,cpp}`, `src/mem/section_filter.{h,cpp}`, `src/mem/hooks.h`,
`src/core/version.h`, `src/core/version_detect.{h,cpp}`, `src/core/version_mapping.{h,cpp}`,
`src/core/readiness.cpp`, `src/core/mod.cpp`, `src/game/worker_logic.h`, `src/game/travel_logic.h`,
`src/game/map_marker.h`, `src/game/marker_teleport_logic.h`, `src/game/inventory_hook_contract.h`,
`src/game/crime_hook_contract.h`, and the Install()/consumer sites in `src/game/*.cpp`.

Nothing in this document was inferred from the binary; every claim is sourced to a Trinity source line.
Anything not confirmed by a source line is marked **uncertain**.

---

## 1. Scanner semantics

### 1.1 What module is scanned

`mem::GameModule()` resolves the *main game module* — the running `.exe`, i.e. `CrimsonDesert.exe` —
from `GetModuleHandleW(nullptr)` and caches it once (`src/mem/scanner.cpp:10-32`):

> `// The main game module (the running .exe). Resolved once and cached.`
> `// Every signature is expressed relative to this module so the mod keeps`
> `// working across game patches: we never bake in absolute addresses, we scan`
> `// for the surrounding byte pattern at load time.`
> — `src/mem/scanner.h:18-21`

```cpp
// src/mem/scanner.cpp:15-28
const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
...
const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
if (dos->e_magic != IMAGE_DOS_SIGNATURE) return r;
const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
if (nt->Signature != IMAGE_NT_SIGNATURE) return r;
r.base = base;
r.size = nt->OptionalHeader.SizeOfImage;
```

So the scan region is `[module base, base + SizeOfImage)`.

### 1.2 Pattern syntax

From `src/mem/scanner.h:24-27`:

> `// Scan a module for an IDA-style byte pattern, e.g.`
> `//   "48 8B 05 ?? ?? ?? ?? 48 85 C0"`
> `// '?' or '??' are single-byte wildcards. Returns the address of the first`
> `// match, or 0 if not present.`

The parser is `Parse()` at `src/mem/scanner.cpp:50-74`:

* Whitespace (`' '`, `'\t'`) separates tokens and is skipped.
* A `?` emits one wildcard byte; a following second `?` is consumed (`??` == `?` == one wildcard byte) —
  `src/mem/scanner.cpp:57-64`.
* A hex pair `XY` emits one concrete byte. If a hex digit is followed by a non-hex character, a single
  nibble is used as the low nibble (`lo = hi`) — `src/mem/scanner.cpp:65-71`. Trinity patterns always use
  full pairs.
* **Any other character is silently skipped** (`if (hi < 0) { ++i; continue; }` — `src/mem/scanner.cpp:66`).

Because of the last rule, a pattern string containing non-hex text produces a *shorter* pattern than it
looks. Trinity only ever passes pure byte/wildcard strings.

Wildcards are represented as `mask[i] == false`; the matcher skips those positions
(`src/mem/scanner.cpp:138-141`).

### 1.3 Which sections are scanned — **not** PE section flags

The scanner deliberately ignores PE section permissions and uses its own section filter.

`src/mem/section_filter.cpp:7-12` (the whole file):

```cpp
bool ShouldScanSection(const char* name, uint32_t characteristics)
{
    constexpr uint32_t kMemExecute = 0x20000000u; // IMAGE_SCN_MEM_EXECUTE
    return !name || std::strcmp(name, ".debug") != 0 ||
           (characteristics & kMemExecute) != 0;
}
```

i.e. **every section is scanned except a `.debug` section that is not executable.**

Rationale, `src/mem/section_filter.h:7-9`:

> `// A section named .debug may contain either stale non-code data or the`
> `// live executable image, depending on the game build. Only reject the`
> `// non-executable form.`

And `src/mem/scanner.cpp:89-92`:

> `// Some builds ship a non-executable '.debug' data section containing`
> `// stale machine-code bytes. Some modern builds instead put the live`
> `// main code image in an executable section with a nonstandard name.`
> `// Filter by characteristics, never by the section name alone.`

Note the mismatch with the header comment at `src/mem/scanner.h:29-31` which still claims the scan
"walks the module's actually-committed, readable pages (via VirtualQuery)". The *implementation*
(`ReadableSpans`, `src/mem/scanner.cpp:93-120`) is PE-section driven, not `VirtualQuery` driven.
`VirtualQuery` is only used by the diagnostic `LogModuleLayout()` (`src/mem/scanner.cpp:224-252`).
**Treat the VirtualQuery claim in scanner.h as stale documentation.**

### 1.4 Spans, merging, and straddling

`ReadableSpans()` walks `IMAGE_FIRST_SECTION` and, for every section passing `ShouldScanSection`,
emits `[base + VirtualAddress, base + VirtualAddress + VirtualSize)`; **adjacent spans are merged** so a
pattern may straddle a page-protection boundary (`src/mem/scanner.cpp:104-118`):

> `// Merged, contiguous, committed+readable spans within the module image.`
> `// Merging adjacent regions lets a pattern straddle a page-protection`
> `// boundary (e.g. across two .text sub-ranges) without being missed.`
> — `src/mem/scanner.cpp:85-87`

If `Misc.VirtualSize` is 0 it is treated as 1 (`src/mem/scanner.cpp:112`).

### 1.5 Match algorithm and return value

`Scan()` (`src/mem/scanner.cpp:150-170`) finds the first fixed (non-wildcard) byte index as a
single-byte anchor to skip cheaply, then does a naive byte-by-byte compare:

```cpp
// src/mem/scanner.cpp:159-162
size_t firstFixed = 0;
while (firstFixed < patLen && !pat.mask[firstFixed]) ++firstFixed;
const bool    haveAnchor = firstFixed < patLen;
const uint8_t anchor     = haveAnchor ? pat.bytes[firstFixed] : 0;
```

Public API (`src/mem/scanner.h:32-56`, `src/mem/scanner.cpp:173-222`):

| Function | Returns |
|---|---|
| `FindPattern(pattern, mod)` | address of the **first** match, or `0` if absent (`scanner.cpp:173-177`) |
| `FindAllMatches(pattern, mod, maxCount = 64)` | `std::vector<uintptr_t>` of up to `maxCount` matches, in address order |
| `FindPatternIf(pattern, mod, visit, ctx)` | first match for which `visit(match, ctx)` returns `true`, else `0` (`scanner.cpp:184-217`) |
| `CountMatches(pattern, mod, maxCount = 8)` | `Scan(pattern, mod, maxCount).size()` — **saturating**: it cannot report more than `maxCount` (`scanner.cpp:219-222`) |
| `LogModuleLayout()` | diagnostic only; logs base/size/readable/exec byte counts |

Empty pattern ⇒ `patLen == 0` ⇒ no results, `FindPattern` returns `0` (`src/mem/scanner.cpp:156-157`).

RIP-relative helpers (`src/mem/scanner.h:62-78`):

```cpp
inline uintptr_t ResolveRip(uintptr_t dispAddr, uintptr_t instrEnd)
{ return instrEnd + *reinterpret_cast<const int32_t*>(dispAddr); }

inline uintptr_t ResolveRipAt(uintptr_t instr, int instrLen)
{ const uintptr_t disp = instr + instrLen - 4; return ResolveRip(disp, instr + instrLen); }

inline uintptr_t ResolveCall(uintptr_t callInstr)
{ return callInstr + 5 + *reinterpret_cast<const int32_t*>(callInstr + 1); }
```

### 1.6 Hook installation contract

`mem::InstallHook` (`src/mem/hooks.h:26-65`) is the shared find/warn/hook/log sequence:

1. `FindPattern(sig)`; on `0` → logs `"<context> signature NOT FOUND - <consequence>."` and returns `false`.
2. `CountMatches(sig, maxMatches)`, default `maxMatches = 8`; if `!= 1` logs
   `"<context> signature ambiguous (%zu); hooking first."` — **it still hooks the first match.**
3. `MH_CreateHook(target, detour, original)`; failure ⇒ logs and returns `false` with `*original = nullptr`.
4. `MH_EnableHook(target)`; failure ⇒ same.
5. On success sets `*original` (trampoline) and `*target` (hooked address); returns `true`.

`RemoveHook(void** target)` (`src/mem/hooks.h:70-76`) disables + removes and nulls the pointer; a null
target is a no-op, so `Remove()` is idempotent.

Callers that need consensus/uniqueness stricter than this call `mem::FindAllMatches` /
`mem::CountMatches` directly and decide for themselves (e.g. `game/worker.cpp:57-63`,
`game/teleport.cpp:544-548`, `game/player.cpp:72-101`).

---

## 2. Version gate

### 2.1 How the revision is detected — **PE resource file version, not a hash**

`DetectVersion()` (`src/core/version_detect.cpp:20-80`) reads the **VS_FIXEDFILEINFO** of the running
executable via `GetFileVersionInfoSizeW` / `GetFileVersionInfoW` / `VerQueryValueW`:

```cpp
// src/core/version_detect.cpp:39-42
g_versionInfo.major    = static_cast<uint16_t>(HIWORD(pFileInfo->dwFileVersionMS));
g_versionInfo.minor    = static_cast<uint16_t>(LOWORD(pFileInfo->dwFileVersionMS));
g_versionInfo.build    = static_cast<uint16_t>(HIWORD(pFileInfo->dwFileVersionLS));
g_versionInfo.revision = static_cast<uint16_t>(LOWORD(pFileInfo->dwFileVersionLS));
```

`rawVersionStr` is formatted as `"%u.%u.%u.%u"` (`src/core/version_detect.cpp:44-47`).

The **`revision`** field (the 4th component) is the version-gate key used everywhere:
`core::GetGameVersion().revision` (a `uint16_t`, `src/core/version_detect.h:21-29`).

There is **no hash check and no pattern-presence probe** in the version gate itself. The comment at
`src/core/version_detect.cpp:54-56` explains why the PE resource is used:

> `// In-Memory Binary Fingerprinting:`
> `// Pearl Abyss keeps the PE resource version static (1.0.0.2474) across multiple Steam updates.`
> `// We inspect the live machine code signatures in game memory to determine the exact Title Update.`

**Note (uncertain / self-contradictory):** the following paragraph at
`src/core/version_detect.cpp:58-63` states the opposite for TU 2.00.00+, i.e. that the resource version
*does* move per title update:

> `// TU 2.00.00+: the PE revision moves per title update`
> `// (1.0.0.2474 = TU 1.18.02, 1.0.0.2625 = TU 2.00.00,`
> `//  1.0.0.2658 = TU 2.00.01, 1.0.0.2692 = TU 2.00.02,`
> `//  1.0.0.2760 = TU 2.01.00, 1.0.0.2850 = TU 2.02.00).`

The *code* follows the second paragraph: the revision is read from the PE resource and directly drives
`ModernTitleUpdateForRevision()` and every runtime gate. If `ModernTitleUpdateForRevision` returns
`nullptr` (any unrecognised revision), the code takes the fallback default
(`src/core/version_detect.cpp:70-76`):

```cpp
// Fallback default
g_versionInfo.tu = GameTU::TU_1_18_01_Plus;
snprintf(g_versionInfo.displayStr, sizeof(g_versionInfo.displayStr),
         "Crimson Desert 1.18.02 (Active)");
```

`g_versionInfo.isSupported = true;` unconditionally (`src/core/version_detect.cpp:78`). So an unknown
newer revision is *not* rejected at detection; it is rejected feature-by-feature via the mapping
functions below.

### 2.2 Revision identifiers that exist in the code

| Revision (PE 4th component) | Label | Source |
|---|---|---|
| `2944` | `"PE 2944"` (no retail TU name guessed) | `version_mapping.cpp:12` |
| `2850` | `"2.02.00"` | `version_mapping.cpp:13` |
| `2760` | `"2.01.00"` | `version_mapping.cpp:14` |
| `2692` | `"2.00.02"` | `version_mapping.cpp:15` |
| `2658` | `"2.00.01"` | `version_mapping.cpp:16` |
| `2625` | `"2.00.00"` | `version_mapping.cpp:17` |
| anything else | `nullptr` → UI falls back to `"Crimson Desert 1.18.02 (Active)"` | `version_mapping.cpp:18`, `version_detect.cpp:70-76` |

> `// The game's public TU number was not inferred from the PE resource.`
> `// Keep the exact executable identity visible until a retail label is`
> `// independently confirmed.`
> — `src/core/version_mapping.cpp:9-11`

Additional documented-but-not-enumerated revision: `1.0.0.2474 = TU 1.18.02`
(comment only, `src/core/version_detect.cpp:59`). It has **no** `case` in the switch, so it falls to the
`nullptr` fallback path. **Uncertain** whether that is intentional.

The legacy `GameTU` enum (`src/core/version_detect.h:7-17`) still lists `TU_1_13` … `TU_1_18_01_Plus`,
but `DetectVersion()` only ever assigns `TU_1_18_01_Plus` (`version_detect.cpp:66`, `:73`).
Therefore `IsLegacyTU()` (`version_detect.cpp:100-104`) can never return true from the live detector, and
the `GameTU`-keyed accessors (`GetPlacementStride`, `GetSlotStride`, `GetItemValSocketOffset`, …,
`version_detect.cpp:106-154`) always take their **modern** branch. **This is a live behavioural fact:
the TU-1.13…1.16 code paths in `version_detect.cpp` are dead.**

### 2.3 Version-gate predicates (`src/core/version_mapping.cpp`)

| Predicate | Rule | Line |
|---|---|---|
| `ModernTitleUpdateForRevision(r)` | switch over 2944/2850/2760/2692/2658/2625, else `nullptr` | `:5-20` |
| `UsesTu201CompatibleRevision(r)` | `r == 2760 \|\| r == 2850 \|\| r == 2944` | `:22-28` |
| `LocoStepperContractForRevision(r)` | `2944 → Pe2944`; `2760,2850 → Modern`; `2625,2658,2692 → Legacy`; else `Unsupported` | `:48-60` |
| `MoveComponentOwnerOffsetForRevision(r)` | `Pe2944 → 0x2C0`; `Modern → 0x2B8`; `Legacy → 0x298`; else `0` | `:30-46` |
| `MayUseLegacyFuzzySignaturesForRevision(r)` | `!UsesTu201CompatibleRevision(r) && r < 2760` | `:62-65` |
| `MayProbeLegacyCrimeEventDispatcherForRevision(r)` | `r <= 2850` | `:67-70` |
| `InventoryCoreGlobalMovOffsetForRevision(r)` | `UsesTu201CompatibleRevision(r) ? 0 : 0x15` | `:72-75` |
| `RealmFlagOffsetForRevision(r)` | `2850 \|\| 2944 → 0x1EC`; `2760 → 0x1FD`; else `0x1F2` | `:77-90` |

Verbatim source for the ABI gate:

```cpp
// src/core/version_mapping.cpp:22-28
bool UsesTu201CompatibleRevision(uint16_t revision)
{
    // PE 2944 retained the modern item ctor, placement commit/free, and
    // transaction-commit ABI. Its changed holder-insert frame uses a
    // dedicated exact AOB rather than inheriting the PE 2850 pattern.
    return revision == 2760 || revision == 2850 || revision == 2944;
}
```

```cpp
// src/core/version_mapping.cpp:62-70
bool MayUseLegacyFuzzySignaturesForRevision(uint16_t revision)
{
    return !UsesTu201CompatibleRevision(revision) && revision < 2760;
}

bool MayProbeLegacyCrimeEventDispatcherForRevision(uint16_t revision)
{
    return revision <= 2850;
}
```

The realm-flag comment carries per-revision live-verification notes:

```cpp
// src/core/version_mapping.cpp:79-89
// Per-revision TLS realm flag offsets, confirmed from live binary analysis:
//   PE 2850 (TU 2.02.00): realm selector reads tls+0x1EC.
//     Audit (2026-09-11) confirmed this gave server=1 client=1 for Add Item.
//     0x1FD is a non-boolean byte at that offset — writing it silently
//     leaves the planner in client-realm, causing err=-771604600 rejections.
//   PE 2760 (TU 2.01.00): realm selector reads tls+0x1FD.
//   Pre-TU 2.01: reads tls+0x1F2.
// PE 2944's live realm selector remains `mov edx, 0x1EC` before the
// TLS byte read (verified at 0x1420E55A4 on 2026-09-19).
if (revision == 2850 || revision == 2944) return 0x1EC;
return (revision == 2760) ? 0x1FD : 0x1F2;
```

### 2.4 Readiness profile — the gate that decides which signatures must resolve at startup

`src/core/readiness.cpp:8-10`:

```cpp
ReadinessProfile ReadinessProfileForRevision(uint16_t revision)
{
    return UsesTu201CompatibleRevision(revision)
        ? ReadinessProfile::Tu201KnownCompatible
        : ReadinessProfile::Legacy;
}
```

`core::GameplayCodeReady()` (`src/core/mod.cpp:29-74`) then requires **only** these to be present,
stopping at the first hit:

```cpp
// src/core/mod.cpp:33-44
// PE 2760 removed several TU 2.00.02 functions. Waiting for those old
// signatures would guarantee a three-minute timeout, so use only the
// independently confirmed 2.01.00 sentinels on that revision.
const char* const currentRequired[] = {
    kSig_DamageApply_Alt,
    kSig_CombatTimingEval,
    kSig_MoveUpdate,
    kSig_InvGetItemQty,
    kSig_EvaluateCrimeWantedState,
    kSig_TodEngineGlobal,
    kSig_WeatherRain,
};
// src/core/mod.cpp:45-58
const char* const legacyRequired[] = {
    kCharMgrAnchors[0].sig,
    kSig_StatCommit,
    kSig_DamageApply_Alt,
    kSig_CombatTimingEval,
    kSig_MoveUpdate,
    kSig_InvGetItemQty,
    kSig_EvaluateCrimeWantedState,
    kSig_FrameTimerBody,
    kSig_FieldTimeTick,
    kSig_TodEngineGlobal,
    kSig_WeatherRain,
    kSig_EquipEffectRefresh,
};
```

Timeout: 180 000 ms with a 2000 ms poll (`src/core/mod.cpp:116-121`).

### 2.5 Which code path each revision family takes

| Area | 2944 | 2760 / 2850 | 2625 / 2658 / 2692 | older / unknown |
|---|---|---|---|---|
| Readiness profile | TU201 | TU201 | Legacy | Legacy |
| Locomotion stepper | `kSig_LocoStepper_PE2944` | `kSig_LocoStepper` | `kSig_LocoStepper_Pre201` | **disabled** (`Unsupported` → error log, `teleport.cpp:1921-1925`) |
| Move-owner offset | `0x2C0` | `0x2B8` | `0x298` | `0` |
| Fast travel | `kSig_TravelToNode_PE2944` + `kSig_TravelDispatcher_PE2944` | `kSig_TravelToNode` → `_Pre201` → `_Legacy` | same chain | same chain |
| Stat commit hook | **not installed** (`player.cpp:821-824`) | **not installed** | installed | installed |
| Slot-expansion setter hook | **not installed** (continuous guard instead, `inventory.cpp:2215-2221`) | **not installed** | installed | installed |
| Holder-insert AOB | `kSig_InvHolderInsert2944` | `kSig_InvHolderInsert201` | `kSig_InvHolderInsert` → `_Legacy` | same as 2625 |
| Transaction commit | `kSig_InvCommit` | `kSig_InvCommit` | `kSig_InvCommit_Pre201` | same |
| Core-global anchor | `kSig_InvCoreGlobal` @ mov offset `0` | `kSig_InvCoreGlobal` @ `0` | `kSig_InvCoreGlobal_Pre201` @ `0x15` | same |
| TrItemValue ctor | modern exact | modern exact | legacy fuzzy set | legacy fuzzy set |
| Placement commit / free | `…201` variants | `…201` variants | base variants | base variants |
| Crime event dispatcher | **not probed** | **not probed** (2850 probed; 2760 probed) | probed | probed |
| Money display hooks | skipped | skipped | skipped (`revision >= 2625`) | installed (`revision < 2625`) |
| Worker patch | enabled | enabled (2850) / **disabled** (2760) | disabled | disabled |
| Dye render leaves | nulled | nulled | nulled | active |
| Realm flag TLS offset | `0x1EC` | `2850 → 0x1EC`, `2760 → 0x1FD` | `0x1F2` | `0x1F2` |

Precise `MayProbeLegacyCrimeEventDispatcherForRevision` semantics: `revision <= 2850`, so it **is**
probed on 2760 and 2850 and **is not** probed on 2944.

---

## 3. The main table

### 3.1 kSig_* / signature constants

`Len` = number of whitespace-separated byte tokens in the pattern (= number of bytes matched;
`?` and `??` each count as **one** token/byte). Computed programmatically from the source text.

| Constant name | Kind | Value (verbatim) | Len | PE revision(s) | Consumed at | What it locates (source comment) | PE 2944? |
|---|---|---|---|---|---|---|---|
| `kSig_StatCommit` | AOB | `48 89 5C 24 10 55 56 57 48 83 EC 20 48 8B 59 18 41 0F B7 E9 48 03 59 20 48 89 D6 48 89 CF 4C 39 C3` | 33 | **not** TU201-compatible → 2625/2658/2692/older/unknown | `game/player.cpp:827` (`mod.cpp:47`) | `pa_StatCommit` (IDB sub_BED7820), the single HP stat-commit choke point; lives in `.link`, not `.text` (`offsets.h:123-146`) | no |
| `kSig_DamageApply` | AOB | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9` | 35 | all (primary) | `game/player.cpp:833` | per-status "apply signed delta" dispatcher `pa_StatApplyDelta` (IDB sub_145B2A0) (`offsets.h:148-168`) | yes |
| `kSig_DamageApply_Alt` | AOB | `48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9` | 35 | all (fallback; also a readiness sentinel) | `game/player.cpp:836`; `core/mod.cpp:37,48` | same dispatcher, TU 2.00 recompile shifted the prologue (`player.cpp:832`) | yes |
| `kSig_CombatTimingEval` | AOB | `48 8B C4 41 55 41 56 41 57 48 83 EC 70 C5 78 29 40 A8` | 18 | all | `game/player.cpp:852`; `core/mod.cpp:38,49` | Combat Timing & Hitbox Evaluator (sub_1407219c0) — Perfect Parry (r9b==1) / Perfect Dodge (r9b==0) (`offsets.h:172-176`) | yes |
| `kSig_JustCore` | AOB | `48 8B C4 55 41 56 48 81 EC ?? ?? ?? ?? C5 FC 10 89` | 17 | **unused** | — (no consumer in `src/`) | "Just Core: Just Guard / Just Evade" (`offsets.h:178-182`) | n/a |
| `kSig_JustCore_Alt` | AOB | `48 8B C4 55 41 56 48 81 EC ?? ?? ?? ?? 44 0F 29` | 16 | **unused** | — (no consumer in `src/`) | alternate Just Core prologue (`offsets.h:183-184`) | n/a |
| `kSig_MoveUpdate` | AOB | `48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56` | 14 | all | `game/teleport.cpp:1839`; `core/mod.cpp:39,50`; decl `game/teleport.h:9` | Havok character-proxy movement integrator (IDB sub_3A3E140); writes position to `[rcx+0x90]` (`offsets.h:305-326`) | yes |
| `kSig_LocoStepper_PE2944` | AOB | `48 8B C4 48 89 58 10 44 88 48 20 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 78 F8 FF FF 48 81 EC 50 08 00 00` | 40 | **2944 only** | `game/teleport.cpp:1911` | locomotion sub-step driver (Super Run / Free Flight); live-installed at `0x14369FF60` (`offsets.h:397-403`) | yes |
| `kSig_LocoStepper` | AOB | `48 8B C4 48 89 58 10 44 88 48 20 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 78 F8 FF FF 48 81 EC 50 08 00 00` | 40 | 2760, 2850 | `game/teleport.cpp:1915` (also `:1226`) | PE 2760/2850 modern locomotion contract (`offsets.h:405-409`) | **no** — explicitly forbidden as a 2944 fallback |
| `kSig_LocoStepper_Pre201` | AOB | `48 8B C4 48 89 58 10 44 88 48 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 68 F8 FF FF 48 81 EC 60 08 00 00` | 36 | 2625, 2658, 2692 | `game/teleport.cpp:1918` | pre-2.01 locomotion stepper (`offsets.h:410-412`) | no |
| `kSig_TravelToNode` | AOB | `48 89 5C 24 18 48 89 74 24 20 89 54 24 10 48 89 4C 24 08 55 57 41 56 48 8D 6C 24 B9 48 81 EC B0 00 00 00 41 8B F0 33 DB 83 FA FF` | 43 | any revision **!= 2944** (primary) | `game/teleport.cpp:1870` | `sub_505140(ignored, sceneId, nodeIndex)` — native fast-travel trigger; unique at `0x1405E2D30` in TU 2.01 (`offsets.h:414-427`) | no |
| `kSig_TravelToNode_Pre201` | AOB | `48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 48 8D 6C 24 B9 48 81 EC B0 00 00 00 41 8B F8 33 DB 83 FA FF` | 37 | TU 1.16 – TU 2.00.02 (fallback, rev != 2944) | `game/teleport.cpp:1872` | pre-2.01 fast-travel trigger (`offsets.h:429-432`) | no |
| `kSig_TravelToNode_Legacy` | AOB | `48 8B C4 48 89 58 18 89 50 10 48 89 48 08 57 48 81 EC 80 00 00 00` | 22 | Legacy TU 1.14 – 1.15 (last fallback, rev != 2944) | `game/teleport.cpp:1874` | legacy fast-travel trigger (`offsets.h:434-436`) | no |
| `kSig_TravelToNode_PE2944` | AOB | `89 54 24 10 48 89 4C 24 08 53 55 56 57 41 54 41 56 41 57 48 81 EC 90 00 00 00 41 8B D8 33 FF` | 31 | **2944 only** | `game/teleport.cpp:1851` | PE 2944 native fast-travel selection gate; unique at `0x140654ED0`; recovered from world-map dispatch `0x140DBC38A` (`offsets.h:438-446`) | yes |
| `kSig_TravelDispatcher_PE2944` | AOB | `48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 41 56 41 57 48 8D AC 24 50 FE FF FF 48 81 EC B0 02 00 00 41 8B F9 45 8B F8 33 DB` | 44 | **2944 only** | `game/teleport.cpp:1852` | PE 2944 final native Fast Travel dispatcher; unique at `0x1406550B0`; ABI RCX ignored, EDX sceneId, R8D nodeIndex, R9D=0 (`offsets.h:448-456`) | yes |
| `kSig_LeaR8Rip` | AOB | `4C 8D 05 ?? ?? ?? ??` | 7 | all | `game/teleport.cpp:1800`; `game/inventory.cpp:1923` | `lea r8,[rip+str]` — the step that ties a table-name string to its resolver (`offsets.h:528-533`) | yes |
| `kSig_TableResolverPrologue` | AOB | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 50 8B 39 48 8B 1D` | 23 | all | `game/teleport.cpp:1743` (as `kPrologue32` byte array, `:1744-1748`) | shared 32-bit-key data-table resolver clone prologue (`offsets.h:534-537`) | yes |
| `kSig_MarkerPattern` | AOB | `C5 FB 10 07 C5 FB 11 02 8B 47 08 89 42 08` | 14 | all | `game/teleport.cpp:525` | map-marker position copy sites; expected match count 5 (`offsets.h:558-561`) | yes |
| `kSig_MarkerOriginPrefix` | AOB | `C5 F8 5C 05` | 4 | all | `game/teleport.cpp:526` | `vsubps xmm0,xmm0,[rip+..]` — map origin vector; expected 9 matches in source, code requires `>= 6` (`offsets.h:563-565`, `teleport.cpp:549`) | yes |
| `kSig_MarkerPlayer` | AOB | `48 8B 06 C5 F8 11 88 B0 01 00 00` | 11 | all | `game/teleport.cpp:524` | marker player-proxy store; hook target is `match + 3` (`teleport.cpp:588`, `offsets.h:567-568`) | yes |
| `kSig_MarkerProtection` | AOB | `48 8B 46 08 48 89 F1` | 7 | all | `game/teleport.cpp:527` | marker "protection" writer — **the hook it feeds is hard-disabled** (`teleport.cpp:461-468`) | yes (but inert) |
| `kSig_InvGetItemQty` | AOB | `66 89 54 24 10 53 57 48 83 EC 28 0F B7 DA` | 14 | all (primary) | `game/inventory.cpp:2085`; `core/mod.cpp:40,51` | native `GetItemQuantity` (sub_1582880 @ `0x141582880`), called by HUD wallet/vendors/crafting (`offsets.h:588-591`) | yes |
| `kSig_InvGetItemQty_Legacy` | AOB | `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B E8 0F B7 DA` | 26 | all (fallback) | `game/inventory.cpp:2088` | legacy item-count accessor (`offsets.h:592-593`) | yes (fallback) |
| `kSig_InvGetHolder` | AOB | `40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41 30` | 21 | all | `game/inventory.cpp:2107`; hook at `:2194`; decl `inventory.h:9` | `GetInventoryHolder` (sub_1CDD520) — container → item holder (`offsets.h:583-595`) | yes |
| `kSig_InvSetExpandSlots` | AOB | `48 89 5C 24 ? 56 48 83 EC 20 48 8B 41 ? 48 8B F2 8B 49` | 19 | **not** TU201-compatible (2625/2658/2692/older) | `game/inventory.cpp:2222`, `:2226` | engine's own slot-expansion setter (sub_1CE8190); hooked to survive vanilla re-stamps (`offsets.h:597-629`) | no |
| `kSig_InvHolderInsert` | AOB | `48 89 5C 24 ? 4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 10 03 00 00` | 46 | **not** TU201-compatible (primary) | `game/inventory.cpp:2273` | per-holder insert planner (sub_1F850C0); 3rd arg is the inventory container (`offsets.h:650-661`) | no |
| `kSig_InvHolderInsert201` | AOB | `48 89 5C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 00 FE FF FF 48 81 EC 00 03 00 00` | 46 | 2760, 2850 | `game/inventory.cpp:2265` (selected by ternary at `:2263-2265`) | TU 2.01.00 insert planner, unique at VA `0x14234D090`; 9-arg ABI, item values 0xC8, placements 0xE0 (`offsets.h:663-668`) | **no** |
| `kSig_InvHolderInsert2944` | AOB | `48 89 5C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 F0 FD FF FF 48 81 EC 10 03 00 00` | 46 | **2944 only** | `game/inventory.cpp:2264` | PE 2944 nine-arg planner; unique live match at `0x142407770` (RVA `0x2407770`), frame `lea rbp,[rsp-210h] / sub rsp,310h` (`offsets.h:670-678`) | yes |
| `kSig_InvHolderInsert_Legacy` | AOB | `48 89 5C 24 ? 4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC F0 02 00 00` | 46 | **not** TU201-compatible (fallback) | `game/inventory.cpp:2276` | legacy insert planner (`offsets.h:680-682`) | no |
| `kSig_InvCommit` | AOB | `48 89 5C 24 18 66 44 89 4C 24 20 48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 00 FF FF FF 48 81 EC 00 02 00 00` | 47 | TU201-compatible (2760/2850/2944) | `game/inventory.cpp:2244`; decl `inventory.h:37` | inventory transaction COMMIT (IDB sub_1CE1E70); unique at VA `0x142077730` in PE 2760; captures the SERVER-authority container at save-load (`offsets.h:684-717`) | yes |
| `kSig_InvCommit_Pre201` | AOB | `4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 48 01 00 00 4D 8B D0 48 8B D1` | 45 | **not** TU201-compatible | `game/inventory.cpp:2252` | unique pre-2.01 commit byte signature (`offsets.h:718-721`) | no |
| `kSig_InvCoreGlobal` | AOB | `48 8B 05 ? ? ? ? 48 8B 48 30 48 8B 49 50 48 89 8D E0 02 00 00 48 85 C9` | 25 | TU201-compatible | `game/inventory.cpp:2301` (via `:2300`); decl `inventory.h:8` | travel-manager wrapper (IDB sub_5019D0) anchoring the **client-realm** core global; `global → +0x30 → +0x50` (`offsets.h:722-742`) | yes |
| `kSig_InvCoreGlobal_Pre201` | AOB | `48 89 54 24 ? 53 48 83 EC 30 48 8B DA C7 44 24 20 00 00 00 00 48 8B 05 ? ? ? ? 48 8B 50 30 48 8B 52 50 48 8B CB E8` | 40 | **not** TU201-compatible | `game/inventory.cpp:2302` | pre-2.01 core-global anchor (`offsets.h:743-745`) | no |
| `kSig_TrItemValueCtor` | AOB | `48 89 5C 24 18 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 4C 8B F2 4C 8B E1` | 34 | all (exact path, requires exactly 1 match) | `game/inventory.cpp:2129` | `TrItemValue` ctor `void f(itemVal, u16* typeId, i64 qty)`; unique at VA `0x14234F210` (PE 2760) (`offsets.h:897-901`) | yes |
| `kSig_TrItemValueCtor_Pre201` | AOB | `48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B EA 48 8B F1 48 C7 01 FF FF FF FF 0F B7 02 66 89 41 08` | 48 | legacy fuzzy only: `!TU201 && revision < 2760` | `game/inventory.cpp:2122` (first entry of `kLegacyCtorSigs[]`) | legacy ctor (IDB sub_1F86FD0) (`offsets.h:902-905`) | no |
| `kSig_InvCommitPlacement` | AOB | `48 89 5C 24 ? 4C 89 44 24 ? 55 56 57 48 83 EC 30 41 0F B7 59` | 21 | **not** TU201-compatible | `game/inventory.cpp:2158` | per-placement COMMIT (IDB sub_1CE1020) `f(holder,outErr,unused,placement,slotIdx)` (`offsets.h:906-914`) | no |
| `kSig_InvCommitPlacement201` | AOB | `48 89 5C 24 10 48 89 6C 24 20 56 57 41 56 48 83 EC 30 41 0F B7 58 08 48 8B F1 48 8D 4C 24 50 66 89 5C 24 50 45 0F B7 F1` | 40 | TU201-compatible | `game/inventory.cpp:2158` | TU 2.01.00 per-placement commit, unique at VA `0x142077410`; ABI `f(holder,outErr,placement,slotIdx)` — placement in r8, slotIdx in r9w (`offsets.h:915-920`) | yes |
| `kSig_InvFreePlacements` | AOB | `48 89 4C 24 08 53 48 83 EC 20 48 8B D9 48 8B 09 8B 43 08 48 69 D0 D8 00 00 00 48 03 D1 E8 ? ? ? ? 90 48 8B 0B 48 8D 43 10 48 3B C8` | 45 | **not** TU201-compatible | `game/inventory.cpp:2160` | free the planner's placement vector; 0xD8-byte records; re-derived against SHA256 `A1DFC032…857C` (`offsets.h:921-933`) | no |
| `kSig_InvFreePlacements201` | AOB | `48 89 5C 24 10 57 48 83 EC 20 48 89 CB 48 83 39 00 74 ? 31 FF 39 79 08 76 ? 66 0F 1F 44 00 00 89 F8 48 69 C8 E0 00 00 00` | 41 | TU201-compatible | `game/inventory.cpp:2160` | TU 2.01.00 placement-vector destructor, unique at VA `0x148871300`; destroys count × 0xE0 (`offsets.h:934-939`) | yes |
| `kSig_TrItemValueDtor` | AOB | `""` (**empty string**) | 0 | **never resolves** — empty pattern → `FindPattern` returns 0 | `game/inventory.cpp:2161` | `TrItemValue` dtor (sub_ED6DF40 via thunk sub_1F88270); declared but deliberately blanked (`offsets.h:940-942`) | no (no-op) |
| `kSig_MovR8Rip` | AOB | `4C 8B 05 ?? ?? ?? ??` | 7 | all | `game/inventory.cpp:1923` | `mov r8, cs:<slot>` — the *indirect* table-name load used only by the "Inventory" table (`offsets.h:1050-1060`) | yes |
| `kSig_EvaluateCrimeWantedState` | AOB | `48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 48 8B DA 4C 6B D9 38 41 B0 07` | 25 | all | `game/inventory.cpp:2066`; `core/mod.cpp:41,52` | crime-record evaluator; returning 7 (`eWantedState_None`) blocks Witness/Pursuit/Bounty (`offsets.h:1182-1184`) | yes |
| `kSig_RegisterCrimeEvent` | AOB | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC C0 00 00 00 4D 8B F0` | 31 | `revision <= 2850` only | `game/inventory.cpp:2072` | central crime-event dispatcher (sub_141595BC0); suppresses Murder/Assault/Theft/Property-Destruction events + UI banner + minimap circle (`offsets.h:1186-1190`) | **no** |
| `kSig_LocStringGet` | AOB | `8B 41 18 48 8B 0D ? ? ? ? 3B 41 60 72 08 48 8D 05 ? ? ? ? C3 48 03 41 58 C3` | 28 | all (primary) | `game/inventory.cpp:2321`, `:2005`; decl `inventory.h:15` | localized-string getter (IDB sub_FF6430); `mov rax, cs:<locMgr>` at `+0x03` (`offsets.h:1218-1237`) | yes |
| `kSig_LocStringGet_Alt1` | AOB | `8B 51 10 48 8B 05 ?? ?? ?? ?? 48 8B 48 08` | 14 | all (fallback 1) | `game/inventory.cpp:2006` | recompiled getter variant (`offsets.h:1238-1239`) | yes |
| `kSig_LocStringGet_Alt2` | AOB | `48 8B 05 ?? ?? ?? ?? 48 8B 48 08 3B 51 08` | 14 | all (fallback 2) | `game/inventory.cpp:2007` | recompiled getter variant (`offsets.h:1240-1241`) | yes |
| `kSig_LocStringGet_Legacy` | AOB | `8B 41 10 48 8B 05 ?? ?? ?? ?? 48 8B 48 08` | 14 | all (fallback 3) | `game/inventory.cpp:2008` | legacy getter variant (`offsets.h:1242-1243`) | yes |
| `kSig_FrameTimerBody` | AOB | `48 8B F9 48 8B 51 60 8B 42 64 89 42 60` | 13 | all (primary) | `game/world.cpp:468`; `core/mod.cpp:53` | body of the per-frame timing update (IDB sub_8FBD80) — Game Speed (`offsets.h:1250-1274`) | yes |
| `kSig_FrameTimerBody_Pre201` | AOB | `48 8B F9 48 8B 41 60 C5 FA 10 40 64 C5 FA 11 40 60` | 17 | all (fallback) | `game/world.cpp:470` | pre-2.01 timing-update body (`offsets.h:1275-1276`) | yes (fallback) |
| `kSig_FieldTimeRealm` | AOB | `BA ?? 01 00 00 48 8B 08 0F B6 04 0A 84 C0 74 0A C5 FC 10 05 ?? ?? ?? ?? EB 08 C5 FC 10 05 ?? ?? ?? ??` | 34 | all | `game/world.cpp:451`; decl `world.h:26` | realm-select read of the master field clock (sub_1CA3890); both `vmovups` RIP operands resolve the server + client globals (`offsets.h:1296-1302`) | yes |
| `kSig_FieldTimeTick` | AOB | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 64 24 20 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C` | 40 | all (primary) | `game/world.cpp:526`; `core/mod.cpp:54`; `world.h:33` | per-frame `FieldTime` tick (sub_871360); freeze = zero the accumulator delta (`offsets.h:1314-1335`) | yes |
| `kSig_FieldTimeTick_Pre201` | AOB | `48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C` | 35 | all (fallback) | `game/world.cpp:529` | pre-2.01 field-time tick (`offsets.h:1336-1338`) | yes (fallback) |
| `kSig_TodEngineGlobal` | AOB | `83 3D ?? ?? ?? ?? FF 75 ?? 48 89 1D ?? ?? ?? ?? 48 89 3D ?? ?? ?? ?? 44 89` | 25 | all (must be unique) | `game/world.cpp:539`, `:545`, `:88`; `core/mod.cpp:42,55`; `world.h:36` | one-time init store of the engine-console global `qword_648F688` (registrar sub_31FAB10) — sun freeze (`offsets.h:1365-1372`) | yes |
| `kSig_WeatherRain` | AOB | `48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 6C 01 00 00` | 38 | all (TU 1.18.00+) | `game/world.cpp:565`; `core/mod.cpp:43,56` | rain-intensity hook (`offsets.h:1381-1383`) | yes |
| `kSig_WeatherSnow` | AOB | `48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 68 01 00 00` | 38 | all (TU 1.18.00+) | `game/world.cpp:568` | snow-intensity hook (`offsets.h:1385-1386`) | yes |
| `kSig_WeatherDust` | AOB | `48 8B 41 ?? 41 B8 40 00 00 00 48 85 C0 41 B9 60 01 00 00 48 8D 50 18 B8 CC 01 00 00 49 0F 44 D0` | 32 | all (TU 1.18.00+) | `game/world.cpp:571` | dust-intensity hook (`offsets.h:1388-1389`) | yes |
| `kSig_WindPack` | AOB | `48 89 5C 24 08 57 48 83 EC 20 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1` | 35 | all (primary) | `game/world.cpp:574` | wind/cloud/fog pack accessor; TU 2.01.00 frame shrank 0x30 → 0x20 (`offsets.h:1391-1394`) | yes |
| `kSig_WindPack_Pre201` | AOB | `48 89 5C 24 08 57 48 83 EC 30 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1` | 35 | all (fallback) | `game/world.cpp:577` | pre-2.01 wind pack (`offsets.h:1395-1396`) | yes (fallback) |
| `kSig_EnvManager` | AOB | `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 60 C5 78 2F C7 72 ?? 48 8B 88 E0 0E 00 00 E8 62` | 28 | all (primary, TU 1.18.00+) | `game/world.cpp:584` | safe `EnvManager` pointer resolution for atmosphere/weather (`offsets.h:1398-1400`) | yes |
| `kSig_EnvManager_Legacy` | AOB | `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 40 48 8B D7 48 8B 88 E0 0E 00 00` | 23 | all (fallback, pre-2.01) | `game/world.cpp:586` | legacy EnvManager anchor (`offsets.h:1401-1403`) | yes (fallback) |
| `kSig_EquipBatch` | AOB | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4D 8B E0 4C 8B EA 4C 8B F1 4C 8B 79 08` | 50 | all (primary) | `game/dye.cpp:1568` | `BatchEquip` (IDB sub_7C98D0); fallback capture of the equip component + gear-change signal (`offsets.h:1524-1534`) | yes |
| `kSig_EquipBatch_Legacy` | AOB | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4D 8B F8 4C 8B E2 4C 8B F1 4C 8B 69 08` | 50 | all (fallback, TU 1.14-1.16) | `game/dye.cpp:1571` | legacy EquipBatch (`offsets.h:1536-1540`) | yes (fallback) |
| `kSig_DyeApplySlot` | AOB | `48 83 EC 30 41 0F B7 D9 48 8B FA 48 8B E9 48 8B 41 08 48 8D 50 08 45 33 FF` | 25 | **nulled when `revision >= 2625`** (`dye.cpp:1630-1636`) → effectively only `revision < 2625` | `game/dye.cpp:1606` | per-slot equipped dye applier (sub_847D24) — companion-safe universal apply (`offsets.h:1547-1551`) | no |
| `kSig_DyeApplyBatch` | AOB | `48 89 5C 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 4D 8B E0 48 8B F2 4C 8B F1` | 34 | all (primary) | `game/dye.cpp:1575` | client dye-ack applier (sub_814BD0, 1.17+ verified) `int* f(comp, outErr, batch1960)` (`offsets.h:1553-1559`) | yes |
| `kSig_DyeApplyBatch_Legacy` | AOB | `48 89 5C 24 ? 48 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 20 01 00 00 4D 8B E0 48 8B F2` | 39 | all (fallback, TU 1.14-1.16) | `game/dye.cpp:1577` | legacy dye-ack applier (`offsets.h:1561-1564`) | yes (fallback) |
| `kSig_DyeUpsert` | AOB | `48 8B 41 78 4C 8B D1 44 8B 81 80 00 00 00 49 C1 E0 04` | 18 | all (primary) | `game/dye.cpp:1581` | dye-record upsert primitive (IDB sub_1F8CB40); `49 C1 E0 04` = 16-byte record stride, "semantic, keep literal" (`offsets.h:1566-1573`) | yes |
| `kSig_DyeUpsert_Legacy` | AOB | `48 8B 41 ? 4C 8B D1 44 8B 41 ? 49 C1 E0 04` | 15 | all (fallback, TU 1.14-1.16) | `game/dye.cpp:1583` | legacy upsert (`offsets.h:1575-1577`) | yes (fallback) |
| `kSig_DyeVisualSet` | AOB | `48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 10 FF FF FF 48 81 EC F0 01 00 00 45 0F B7 F1 49 8B F0 48 8B FA` | 41 | **nulled when `revision >= 2625`** → only `revision < 2625` | `game/dye.cpp:1598` | visual SET leaf (sub_8154A0 @ VA `0x1408160A0`, TU 1.18.02 unique) — pushes the material override into the GPU material instance (`offsets.h:1602-1611`) | no |
| `kSig_DyeVisualClear` | AOB | `48 89 5C 24 18 44 88 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 45 0F B7 F0 48 8B FA 48 8B F1` | 41 | **nulled when `revision >= 2625`** → only `revision < 2625` | `game/dye.cpp:1599` | visual CLEAR leaf (sub_817310 @ VA `0x140817310`, unique) (`offsets.h:1612-1619`) | no |
| `kSig_DyeRecordRemove` | AOB | `44 8B 91 80 00 00 00 33 C0 4C 8B D9 45 85 D2 0F 84` | 17 | **nulled when `revision >= 2625`** → only `revision < 2625` | `game/dye.cpp:1600` | data remove-by-channel (sub_206EBF0 @ VA `0x14206EBF0`, unique) (`offsets.h:1620-1625`) | no |
| `kSig_EquipEffectRefresh` | AOB | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B F2 48 8B F1 80 49 22 20 4C 8D 81 00 01 00 00` | 40 | all (primary) | `game/equipment.cpp:1566`; `core/mod.cpp:57` | equipped-item EFFECT refresh (IDB sub_7C88A0); 100% unique @ `0x140AEBE70`; rebuilds derived effect data so socket writes take hold live (`offsets.h:1686-1700`) | yes |
| `kSig_EquipEffectRefresh_Legacy` | AOB | `48 89 5C 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B F2` | 36 | all (fallback) | `game/equipment.cpp:1568` | legacy effect refresh (`offsets.h:1701-1703`) | yes (fallback) |
| `kSig_FriendlyTrustSiteA` | AOB | `41 8B C0 EB 4E C5 FC 10 07 C5 FC 11 01 C5 FC 10 4F 20 C5 FC 11 49 20` | 23 | **unused** | — | hook site inside `SetNpc` @ `0x141BDA967` (`offsets.h:1776-1779`) | n/a |
| `kSig_FriendlyTrustSiteB` | AOB | `44 89 C0 EB 4E C5 FC 10 07 C5 FC 11 01 C5 FC 10 4F 20 C5 FC 11 49 20` | 23 | **unused** | — | hook site inside `SetPet` @ `0x14D4AEFAF` (`offsets.h:1781-1783`) | n/a |
| `kSig_FriendlySetNpc201` | AOB | `4C 8B DC 53 55 56 57 41 56 41 57 48 83 EC 68 48 8B FA 48 8B F1 0F B7 42 04` | 25 | all (primary; TU 2.01.00) | `game/friendly.cpp:239` | TU 2.01.00 relationship-record setter, NPC map = component +0x38; unique in PE 2760 (`offsets.h:1794-1798`) | yes |
| `kSig_FriendlySetPet201` | AOB | `49 89 E3 53 55 56 57 41 56 41 57 48 83 EC 68 48 89 D7 48 89 CE 0F B7 42 04 66 41 89 43 08 49 8D 4B 08 E8 ? ? ? ? 31 ED 39 6E 1C` | 44 | all (primary; TU 2.01.00) | `game/friendly.cpp:240` | TU 2.01.00 pet/vehicle relationship setter, map = component +0x18 (`offsets.h:1794-1801`) | yes |
| `kSig_FriendlyGetNpc201` | AOB | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 1C 00` | 53 | all (TU 2.01 lookup path) | `game/friendly.cpp:271` | NPC relationship-map lookup; discriminator `+0x1C` (`offsets.h:1803-1808`) | yes |
| `kSig_FriendlyGetPet201` | AOB | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 3C 00` | 53 | all (TU 2.01 lookup path) | `game/friendly.cpp:272` | pet/vehicle relationship-map lookup; discriminator `+0x3C` (`offsets.h:1809-1810`) | yes |
| `kSig_FriendlySetNpc` | AOB | `4C 8B DC 53 55 56 57 41 56 48 83 EC 60 48 8B FA 48 8D 69 38` | 20 | fallback (TU 2.00) — only tried if both 201 setters fail | `game/friendly.cpp:243` | direct `SetNpc` prologue, frame 0x60, `lea rbp,[rcx+38h]` (`offsets.h:1812-1814`) | no |
| `kSig_FriendlySetPet` | AOB | `49 89 E3 53 55 56 57 41 56 48 83 EC 60 48 89 D7 48 8D 69 18` | 20 | fallback (TU 2.00) — only tried if both 201 setters fail | `game/friendly.cpp:244` | direct `SetPet` prologue, frame 0x60, `lea rbp,[rcx+18h]` (`offsets.h:1816-1817`) | no |
| `kSig_FriendlyNpcTrustWriter` | AOB | `48 89 5C 24 10 66 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC E0 00` | 32 | **unused** | — | `NpcTrustWriter` @ `0x141BDF910` (`offsets.h:1819-1821`) | n/a |
| `kSig_FriendlyAlertDisp` | AOB | `48 89 5C 24 10 66 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 B0 48 81 EC 50 01` | 32 | **unused** | — | `FactionRelationAlertDispatcher` @ `0x142759760` (`offsets.h:1823-1825`) | n/a |
| `kSig_WorkerMaxLevelAndSkills` | AOB | `0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5` | 15 | **2850 and 2944 only** (`WorkerPatchSupportedForRevision`) | `game/worker.cpp:57` | worker grade-selector branch to patch (JNZ +0x95). PE 2850 `+20967CC`; PE 2944 `+214BE8C` (`offsets.h:1835-1850`) | yes |

#### 3.1.1 `kCharMgrAnchors[]` — the six character-manager anchors

Type: `CharMgrAnchor { const char* sig; uintptr_t movOff; }`, declared `src/game/offsets.h:229-255`.
Consumed by the consensus resolver `ResolveCharMgrGlobal()` at `src/game/player.cpp:65-101`
(`movOff` = offset of the 7-byte `mov rax,cs:<global>` inside the match, `player.cpp:76`).

| # | sig (verbatim) | Len | movOff | Source comment |
|---|---|---|---|---|
| 1 | `4D 8B 00 49 C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8` | 23 | `0x0C` | "TU 2.01.00, sub_276B340 … The global still owns the manager whose character vector is at +0xB8/+0xC0." (`offsets.h:236-239`) |
| 2 | `48 8B 05 ?? ?? ?? ?? 48 8B 08 4D 8B 00 49 C1 E8 20` | 17 | `0` | "sub_22E6330 … Best of the set - pure ABI arg setup plus a literal shift count." (`offsets.h:240-242`) |
| 3 | `48 8B 05 ?? ?? ?? ?? 44 8B 82 90 00 00 00 48 8D 54 24 ?? 48 8B 08 E8` | 23 | `0` | "sub_251E3B0 … rdx is the incoming arg2 at entry; 0x90 is a struct offset." (`offsets.h:243-245`) |
| 4 | `48 8B 05 ?? ?? ?? ?? 44 8B 81 58 01 00 00 48 8D 55 ?? 48 8B 08 E8` | 22 | `0` | "1.17+: this caller now reads +0x158 before the same manager call." (`offsets.h:246-247`) |
| 5 | `48 8B 05 ?? ?? ?? ?? 44 8B 81 60 01 00 00 48 8D 55 ?? 48 8B 08 E8` | 22 | `0` | "1.14-1.16 (Legacy): this caller read +0x160 before the manager call." (`offsets.h:248-249`) |
| 6 | `48 8B 05 ?? ?? ?? ?? 44 8B 07 48 8D 54 24 ?? 48 8B 08 E8` | 19 | `0` | "sub_2514EB0 / sub_22EBC00 … Weakest of the set … Kept as a fallback." (`offsets.h:250-254`) |

Consensus rules (`src/game/player.cpp:72-100`): each anchor resolves independently; the value with the
most votes wins; `distinct > 1` logs `"player: char-manager anchors DISAGREE (%d distinct values)…"`.
The comment at `offsets.h:220-225` carries an explicit warning:

> `// CAUTION: the same API family is also called with two SIBLING globals`
> `// (0x61830D0 / 0x61830D8 in the current dump) - the client/server realm`
> `// split, see the trinity-engine-architecture notes. Do NOT loosen these`
> `// into "any global passed to the char-manager API": that also matches the`
> `// wrong realm's manager, which resolves fine and then fails silently. Keep`
> `// every anchor tied to a specific call site.`

Also: `// In the current dump all four resolve to qword_61830F8 (was qword_6181090 before the update).`
(`offsets.h:227-228`) — note "all four" vs six entries: **stale comment, uncertain**.

---

### 3.2 String anchors (`kStr_*` and other `const char*` anchors)

All of these are scanned as **raw ASCII bytes** inside the module image (they are found by scanning for
the string bytes, then resolving the `lea r8,[rip+…]` / `mov r8,cs:<slot>` that references them).

| Constant name | Kind | Value (verbatim) | Len (bytes) | PE revision(s) | Consumed at | What it locates | PE 2944? |
|---|---|---|---|---|---|---|---|
| `kStr_GimmickSceneTable` | string | `"LevelGimmickSceneObjectInfo"` | 27 | all | `game/teleport.cpp:1888` | fast-travel destination registry table name (`offsets.h:458-485`) | yes |
| `kStr_LevelNameTable` | string | `"FieldLevelNameTableInfo"` | 23 | all | `game/teleport.cpp:1892` | named-area AABB table for waypoint labels (`offsets.h:515-532`) | yes |
| `kStr_ItemInfoTable` | string | `"iteminfo"` | 8 | all | `game/inventory.cpp:2309`, `:1974`; `inventory.h:9` | ItemInfo table (typeId → item def → key string) (`offsets.h:1012-1023`) | yes |
| `kStr_InventoryInfoTable` | string | `"Inventory"` | 9 | all | `game/inventory.cpp:2318`, `:2002` | InventoryInfo table (one row per storage / InventoryType). Loaded **indirectly** (`mov r8, cs:<slot>`), unlike the others (`offsets.h:1030-1059`) | yes |
| `kStr_ItemGroupInfoTable` | string | `"ItemGroupInfo"` | 13 | all | `game/inventory.cpp:2312`, `:1976`, `:1982` | ItemGroupInfo category tree (`offsets.h:1099-1128`) | yes |
| `kStr_StringInfoTable` | string | `"stringinfo"` | 10 | all | `game/inventory.cpp:2315`, `:2000` | stringinfo table — icon sprite names (`offsets.h:1151-1168`) | yes |
| `kStr_WantedInfoTable` | string | `"WantedInfo"` | 10 | all | `game/inventory.cpp:5703` | WantedInfo table — bounty increase price / blocked flag (`offsets.h:1175-1177`) | yes |
| `kGrpKey_SubCatPrefix` | string | `"ItemGroup_SubCategory_"` | 22 | all | `game/inventory.cpp:939`, `:940` | prefix used to derive a category icon file name from `_stringKey` (`offsets.h:1208-1209`) | yes |
| `kIconPrefix_ItemGroup` | string | `"ItemIcon_ItemGroup_"` | 20 | all | `game/inventory.cpp:941` | replacement prefix for the derived icon name (`offsets.h:1210`) | yes |
| `kIcon_Uncategorised` | string | `"ItemIcon_ItemGroup_special_unknown"` | 35 | all (**defined, no consumer found**) | — | last-resort icon for the synthetic "Uncategorised" bucket (`offsets.h:1211-1216`) | yes |

**Additional string anchors that are inline literals, not named constants** (found by scanning
`src/**/*.cpp|h` for `ResolveTableResolver(...)` and `FindTableGlobal(...)` call sites):

| Literal | File:line | Purpose |
|---|---|---|
| `"regioninfo"` | `game/teleport.cpp:1894` | fallback area-name table if `FieldLevelNameTableInfo` is absent |
| `"fieldinfo"` | `game/teleport.cpp:1896` | second fallback area-name table |
| `"categorygroupinfo"` | `game/inventory.cpp:1978` | fallback category-tree table |
| `"categoryinfo"` | `game/inventory.cpp:1980` | second fallback category tree |
| `"tribeinfo"` | `game/inventory.cpp:1956` | hardcoded-offset fallback table (see §3.5) |

---

### 3.3 `kOff_*` structure offsets

Every entry is `inline constexpr uintptr_t` in `src/game/offsets.h`. "Consumed at" lists the primary
consumer(s); a dash means the constant is **defined but unused** in `src/` (documentation only).

| Constant name | Value | Consumed at | Locates (comment) |
|---|---|---|---|
| `kOff_Owner_Actor` | `0x68` | `game/player.cpp:241,261,682,741`; `game/equipment.cpp:220` | owner → actor (`offsets.h:43`) |
| `kOff_Actor_StatusMarker` | `0x20` | `game/player.cpp:242,263,691` | actor → status marker (`offsets.h:44`) |
| `kOff_Owner_ObjectType` | `0x48` | — | int32 ObjectType — "(documentation only)" (`offsets.h:45`) |
| `kOff_StatEntry_Type` | `0x00` | `game/player.cpp:163` | int32 stat-entry type id (`offsets.h:72`) |
| `kOff_StatEntry_Current` | `0x08` | `game/player.cpp:203,208,595` | int64 absolute current (`offsets.h:73`) |
| `kOff_StatEntry_Base` | `0x18` | `game/player.cpp:201,593` | int64 base value (`offsets.h:74`) |
| `kOff_StatEntry_Norm` | `0x20` | `game/player.cpp:209` | int64 current − base (`offsets.h:75`) |
| `kOff_StatEntry_Floor` | `0x28` | — | int64 lower clamp (`offsets.h:76`) |
| `kOff_StatEntry_Cap` | `0x30` | `game/player.cpp:202,594` | int64 max / cap (`offsets.h:77`) |
| `kOff_Marker_TargetOwner` | `0x18` | `game/player.cpp:244,265` | marker → char vital/target owner (`offsets.h:186-189`) |
| `kOff_CharMgr_ListData` | `0xB8` | `game/player.cpp:393` | character manager → `character*[]` data ptr (`offsets.h:262`) |
| `kOff_CharMgr_ListCount` | `0xC0` | `game/player.cpp:397` | character manager → u32 count (`offsets.h:263`) |
| `kOff_Owner_Possessor` | `0xA0` | `game/player.cpp:358,412,701`; `game/inventory.cpp:1329,3797`; `game/dye.cpp:1364` | owner → possessor/controller (`offsets.h:285`) |
| `kOff_Possessor_Pawn` | `0xD0` | `game/inventory.cpp:1331`; `game/dye.cpp:1367` | possessor → back-ref to owner (`offsets.h:286`) |
| `kOff_Owner_TypeDesc` | `0x88` | `game/player.cpp:226,437,487`; `game/inventory.cpp:3792` | owner → type descriptor (tag byte at +1) (`offsets.h:294`) |
| `kOff_Root_StatArray` | `0x58` | `game/player.cpp:246,267` | root → stat entry[0] (Health) (`offsets.h:303`) |
| `kOff_MoveOwner_Position` | `0x90` | `game/teleport.cpp:1714` | x,y,z,w f32 live position (`offsets.h:327`) |
| `kOff_MoveOwner_Velocity` | `0xD0` | `game/marker_teleport_logic.h:39` | x,y,z,w f32 velocity (`offsets.h:332`) |
| `kOff_MoveOwner_DesiredVel` | `0xC0` | `game/teleport.cpp:1215`; `game/marker_teleport_logic.h:38` | x,y,z,w f32 input/desired velocity (`offsets.h:341`) |
| `kOff_Registry_SceneCount` | `0x08` | `game/teleport.cpp:797,1107` | gimmick-scene registry u32 sceneCount (`offsets.h:487`) |
| `kOff_Registry_SceneTable` | `0x50` | — | registry ptr[] sceneTable (`offsets.h:488`) |
| `kOff_SceneDesc_NodeCount` | `0x28` | `game/teleport.cpp:1123` | scene descriptor u32 nodeCount (`offsets.h:489`) |
| `kOff_SceneDesc_NodeArray` | `0x20` | `game/teleport.cpp:1124` | scene descriptor node[] ptr (`offsets.h:490`) |
| `kOff_Node_Gimmick` | `0x10` | `game/teleport.cpp:1167` | node +0x10 gimmick/type word — `// UNVERIFIED on PE 2944 (observed live as u16, not ptr)` (`offsets.h:492`) |
| `kOff_Node_Position` | `0x84` | `game/teleport.cpp:1152` | node f32 x,y,z world position (`offsets.h:493`) |
| `kOff_SceneDesc_StringKey` | `0x08` | `game/teleport.cpp:1132` | scene `_stringKey` engine string (`offsets.h:509`) |
| `kOff_SceneDesc_IsBlocked` | `0x10` | `game/teleport.cpp:1127` | scene bool blocked (`offsets.h:510`) |
| `kOff_SceneDesc_LevelName` | `0x18` | — | scene engine string (`offsets.h:511`) |
| `kOff_SceneDesc_UseTeleport` | `0x49` | `game/teleport.cpp:1128` | scene bool `_useTeleport` — real fast-travel filter (`offsets.h:512`) |
| `kOff_SceneDesc_IsEmpty` | `0x6C` | — | scene bool (`offsets.h:513`) |
| `kOff_TableResolver_MovGlobal` | `0x14` | `game/teleport.cpp:1805` | offset of the 7-byte `mov rbx, cs:<registry>` inside the 32-bit-key resolver clone (`offsets.h:538`) |
| `kOff_LvlRow_BucketCount` | `0x20` | `game/teleport.cpp:807` | LevelNameTableInfo row u32 bucket count (`offsets.h:546`) |
| `kOff_LvlRow_Size` | `0x24` | `game/teleport.cpp:808` | row u32 entry count (`offsets.h:547`) |
| `kOff_LvlRow_Buckets` | `0x30` | `game/teleport.cpp:813` | row ptr → 0x100-byte buckets (`offsets.h:548`) |
| `kOff_LvlRow_Entries` | `0x38` | `game/teleport.cpp:814` | row ptr → entry* array (`offsets.h:549`) |
| `kOff_LvlBucket_Pairs` | `0x08` | `game/teleport.cpp:824` | bucket (hash u32, entryIdx u32) pairs (`offsets.h:552`) |
| `kOff_LvlEntry_Name` | `0x10` | `game/teleport.cpp:832` | LevelNameInfo entry engine string (`offsets.h:554`) |
| `kOff_LvlEntry_IsSector` | `0x18` | `game/teleport.cpp:829` | entry bool `_isSectorLevel` (`offsets.h:555`) |
| `kOff_LvlEntry_Box` | `0x1C` | `game/teleport.cpp:836,837` | entry f32 min xyz, max xyz (`offsets.h:556`) |
| `kOff_Player_Dest0` | `0x90` | `game/marker_teleport_logic.h:36,46,51` | Vec3 x,y,z destination #0 (`offsets.h:573`) |
| `kOff_Player_Dest1` | `0x1A0` | `game/marker_teleport_logic.h:37,47` | Vec3 x,y,z destination #1 (`offsets.h:574`) |
| `kOff_Global_Mid` | `0x30` | `game/inventory.cpp:1197,1221` | core global +0x30 → mid (`offsets.h:746`) |
| `kOff_Mid_Container` | `0x50` | `game/inventory.cpp:1198,1222` | mid +0x50 → inventory container (`offsets.h:747`) |
| `kOff_Container_Sub` | `0x68` | `game/inventory.cpp:1184,3582,3800,4627,5645`; `game/dye.cpp:238,281,433`; `game/equipment.cpp:174,210`; `game/player.cpp:749` | container +0x68 → sub-object (`offsets.h:748`) |
| `kOff_Sub_Holder` | `0xB8` | `game/inventory.cpp:1185,4630,5648` | sub +0xB8 = item holder (`offsets.h:749`) |
| `kOff_InvHolder_Buckets` | `0x18` | `game/inventory.cpp:1171,1235,1724,1818,2369,2852,3036,3253,3316,3755,3987,4160,4587,4786,4912,5040`; `game/dye.cpp:1100,1136` | holder → bucket ptr[] (`offsets.h:767`) |
| `kOff_InvHolder_Count` | `0x20` | same set as above | holder → u32 bucket count (`offsets.h:768`) |
| `kOff_InvBucket_Slots` | `0x00` | `game/inventory.cpp:1832,2385,…`; `game/dye.cpp:1110,1147` | bucket → slot array ptr[] (`offsets.h:769`) |
| `kOff_InvBucket_Count` | `0x08` | `game/inventory.cpp:1833,2386,…`; `game/dye.cpp:1111,1148` | bucket u16 slot-array SIZE (headroom ~1460, not the cap) (`offsets.h:770-776`) |
| `kOff_InvBucket_Type` | `0x10` | `game/inventory.cpp:1732,1827,2381,2865,3764,3774` | bucket u16 InventoryType (which storage) (`offsets.h:777`) |
| `kOff_InvBucket_MaxSlots` | `0x14` | `game/inventory.cpp:1756,1765,1775,2884,2895,2918,2932,3858`; `inventory.h:285` | bucket u16 LIVE effective slot cap — the write target (`offsets.h:778-791`) |
| `kOff_InvBucket_DeltaRaw` | `0x16` | `game/inventory.cpp:2930` | bucket u16 raw delta accumulator (`offsets.h:795`) |
| `kOff_InvBucket_DeltaClamped` | `0x18` | `game/inventory.cpp:2931` | bucket u16 clamped delta accumulator (`offsets.h:796`) |
| `kOff_InvBucket_UsedSlots` | `0x12` | `game/inventory.cpp:1828,1847,3857` | bucket u16 used-slot count; incremental accumulator (`offsets.h:798-825`) |
| `kOff_InvBucket_ExpandSlots` | `0x1A` | `game/inventory.cpp:177,222,1755,1767,1774,2885,2929`; `inventory.h:293` | bucket u16 `_varyExpandSlotCount` — the real expansion driver (`offsets.h:826-847`) |
| `kOff_InvSlot_TypeId` | `0x08` | `game/inventory.cpp:1841,2413,…`; `game/equipment.cpp:73,386,…`; `game/dye.cpp:517,1155,…` | slot u16 item type id (`offsets.h:850`) |
| `kOff_InvSlot_Quantity` | `0x10` | `game/inventory.cpp:1842,2414,3056,…`; `game/equipment.cpp:389,…`; `game/dye.cpp:518,…` | slot i64 quantity — "edit here" (`offsets.h:851`) |
| `kOff_InvHolder_Container` | `0x08` | `game/inventory.cpp:3942,4060` | holder +8 → container (`offsets.h:944`) |
| `kOff_ItemDef_BucketType` | `0x418` | `game/inventory.cpp:3952` | ItemInfo `_defaultPushInventoryInfo`; TU 1.17–1.18.02 value (`offsets.h:945-953`) |
| `kOff_Sub_IdAllocator` | `0x10` | `game/inventory.cpp:3801` | (owner+0x68)+0x10 → instance-id allocator (`offsets.h:975`) |
| `kOff_IdAlloc_Counter` | `0x20` | `game/inventory.cpp:4085` | i64 `InterlockedIncrement64` target (`offsets.h:976`) |
| `kOff_ItemVal_InstanceId` | `0x00` | `game/inventory.cpp:3874,3880,4009,4181`; `game/equipment.cpp:391,…`; `game/dye.cpp:1117,…` | i64 item instance id (−1 out of the ctor) (`offsets.h:982`) |
| `kOff_ItemVal_Subtype` | `0x0A` | `game/inventory.cpp:3873,3882` | u16 subtype (reconcile zeroes it) (`offsets.h:983`) |
| `kOff_Placement_SlotIdx` | `0xD8` | — | u16 slot index inside a 0xE0 placement record (`offsets.h:988`) |
| `kOff_Teb_TlsPointer` | `0x58` | `game/inventory.cpp:1294` | `TEB.ThreadLocalStoragePointer` (`offsets.h:1004`) |
| `kOff_ItemResolver_MovGlobal` | `0x15` | `game/teleport.cpp:1805` | offset of the 7-byte `mov rbx, cs:<table>` inside the 16-bit-key resolver clone (`offsets.h:1024`) |
| `kOff_ItemTable_Count` | `0x08` | `game/inventory.cpp:266,1860,2629,2733,2769,2944,4330,4336,4481,5716` | table u32 count (`offsets.h:1025`) |
| `kOff_ItemTable_Defs` | `0x58` | — | table ptr[] item defs (`offsets.h:1027`) |
| `kOff_ItemDef_Key` | `0x08` | `game/inventory.cpp:298` | item def ptr → string object (key char*) (`offsets.h:1028`) |
| `kOff_InvDef_Key` | `0x08` | `game/inventory.cpp:1089` | InventoryInfo `_stringKey` (`offsets.h:1064`) |
| `kOff_InvDef_Name` | `0x70` | `game/inventory.cpp:1025,1081` | InventoryInfo `_InventoryNameUIText` (loc-string) (`offsets.h:1070`) |
| `kOff_InvDef_DefSlots` | `0x48` | `game/inventory.cpp:1099` | InventoryInfo u16 `_defaultSlotCount` (`offsets.h:1071`) |
| `kOff_InvDef_MaxSlots` | `0x4A` | `game/inventory.cpp:1100,2957,2971,2973` | InventoryInfo u16 `_maxSlotCount` (`offsets.h:1072`) |
| `kOff_ItemDef_Name` | `0x20` | `game/inventory.cpp:348` | ItemInfo `_itemName` (loc-string struct) (`offsets.h:1088`) |
| `kOff_ItemDef_Groups` | `0x350` | `game/inventory.cpp:372,373` | ItemInfo `_itemGroupInfoList` vector (`offsets.h:1089`) |
| `kOff_ItemDef_Tier` | `0x210` | `game/inventory.cpp:950` | ItemInfo u8 `_itemTier` (rarity 0..5) (`offsets.h:1090`) |
| `kOff_ItemDef_MaxStackCount` | `0x18` | `game/inventory.cpp:2631,2709,3167,4576,4867` | ItemInfo i64 `_maxStackCount` (`offsets.h:1096`) |
| `kOff_ItemDef_ApplyMaxStackCap` | `0x111` | `game/inventory.cpp:2632,2710,3168,4577` | ItemInfo u8 `_applyMaxStackCap` (`offsets.h:1097`) |
| `kOff_GrpDef_Name` | `0x18` | `game/inventory.cpp:501` | ItemGroupInfo `_groupName` (loc-string) (`offsets.h:1129`) |
| `kOff_GrpDef_Order` | `0x68` | `game/inventory.cpp:363` | ItemGroupInfo u16 `_orderIndex` (`offsets.h:1130`) |
| `kOff_Vec_Data` | `0x00` | `game/inventory.cpp:372,911` | engine vector `T*` data (`offsets.h:1134`) |
| `kOff_Vec_Count` | `0x08` | `game/inventory.cpp:373,912` | engine vector u32 count (`offsets.h:1135`) |
| `kOff_StrDef_Buffer` | `0x18` | `game/inventory.cpp:898` | stringinfo `_buffer` ptr → string obj (`offsets.h:1169`) |
| `kOff_ItemDef_Icons` | `0x90` | `game/inventory.cpp:911,912` | ItemInfo `_itemIconList` vector (`offsets.h:1170`) |
| `kOff_IconData_Path` | `0x00` | `game/inventory.cpp:915` | ItemIconData u16 `_iconPath` → stringinfo row (`offsets.h:1171`) |
| `kOff_GrpDef_Icon` | `0x6C` | `game/inventory.cpp:934` | ItemGroupInfo u16 `_iconPath` → stringinfo row (`offsets.h:1172`) |
| `kOff_WantedDef_IncreasePrice` | `0x18` | `game/inventory.cpp:5736,5740,5744` | WantedInfo i64 `_increasePrice` (`offsets.h:1178`) |
| `kOff_WantedDef_IsBlocked` | `0x10` | — | WantedInfo u8 `_isBlocked` (`offsets.h:1179`) |
| `kOff_GrpDef_Key` | `0x08` | `game/inventory.cpp:481,938` | ItemGroupInfo `_stringKey` ptr → string obj (`offsets.h:1208`) |
| `kOff_LocGet_MovGlobal` | `0x03` | `game/inventory.cpp:2323` | offset of `mov rax, cs:<locMgr>` inside the loc-string getter (`offsets.h:1244`) |
| `kOff_LocProv_Offset` | `0x18` | `game/inventory.cpp:314` | u32 offset into the localization blob (`offsets.h:1245`) |
| `kOff_LocMgr_Blob` | `0x58` | `game/inventory.cpp:330` | locMgr ptr → blob (`offsets.h:1246`) |
| `kOff_LocBlob_Data` | `0x00` | `game/inventory.cpp:334` | blob `char*` (`offsets.h:1247`) |
| `kOff_LocBlob_Size` | `0x08` | `game/inventory.cpp:332` | blob u32 used bytes (`offsets.h:1248`) |
| `kOff_TimeStruct_Delta` | `0x64` | `game/world.cpp:62,68` | f32 frame delta (seconds) (`offsets.h:1277`) |
| `kOff_TimeStruct_ScaledDelta` | `0x68` | `game/world.cpp:69` | f32 scaled frame delta (`offsets.h:1278`) |
| `kOff_FieldTime_ServerVmovups` | `0x10` | `game/world.cpp:453` | server-realm `vmovups` inside the `kSig_FieldTimeRealm` match (`offsets.h:1305`) |
| `kOff_FieldTime_ClientVmovups` | `0x1A` | `game/world.cpp:454` | client-realm `vmovups` (`offsets.h:1306`) |
| `kOff_FieldTime_Day` | `0x00` | `game/world.cpp:167,813,856,887` | i32 day (`offsets.h:1309`) |
| `kOff_FieldTime_Hour` | `0x04` | `game/world.cpp:168,814,888` | i32 hour (`offsets.h:1310`) |
| `kOff_FieldTime_Min` | `0x08` | — | i32 minute (`offsets.h:1311`) |
| `kOff_FieldTime_Sec` | `0x0C` | — | i32 second (`offsets.h:1312`) |
| `kOff_TodEngineGlobal_Mov` | `9` | `game/world.cpp:553` | offset of `48 89 1D <disp32>` inside `kSig_TodEngineGlobal` (`offsets.h:1373`) |
| `kOff_Tod_Manager` | `0x2F8` | `game/world.cpp:149` | engine → render manager (`offsets.h:1375`) |
| `kOff_Tod_CurrentHour` | `0x3D0` | `game/world.cpp:648,835,872` | f32 hours 0..24 (`offsets.h:1376`) |
| `kOff_Tod_LowerLimit` | `0x3D4` | `game/world.cpp:645,653,661,775,839,876` | f32 clamp lower (`offsets.h:1377`) |
| `kOff_Tod_UpperLimit` | `0x3D8` | `game/world.cpp:646,654,662,776,840,877` | f32 clamp upper (`offsets.h:1378`) |
| `kOff_EnvManager_Mov` | `3` | — | defined but **unused**; `world.cpp:592` resolves from offset 0 using `kLen_EnvManager_Mov` (`offsets.h:1404`) |
| `kOff_Sub_EquipComp` | `0x38` | `game/dye.cpp:240,283,435,442`; `game/equipment.cpp:176,212`; `game/inventory.cpp:3584` | (actor+0x68)+0x38 → equip component (`offsets.h:1521`) |
| `kOff_EquipComp_Owner` | `0x08` | `game/dye.cpp:340,437,452,641,1361`; `game/equipment.cpp:297`; `game/inventory.cpp:3439` | equip component +8 → owning actor (`offsets.h:1522`) |
| `kOff_EquipComp_Table` | `0x80` | — (literals `0x90`/`0x80`/probe list used instead, `equipment.cpp:86,103,137`) | 1.17 equip table descriptor (`offsets.h:1629`) |
| `kOff_EquipTable_Array` | `0x08` | `game/dye.cpp:165,179,193,209,460`; `game/equipment.cpp:87,104,121,141`; `game/inventory.cpp:3447,3453,3459,3472` | entry[] base (`offsets.h:1630`) |
| `kOff_EquipTable_Count` | `0x10` | same as above | u32 entry count (`offsets.h:1631`) |
| `kOff_EquipEntry_SlotTag` | `0xC8` | — (literal `0xC8`/`0xC0` via `tbl.tagOffset`, `equipment.cpp:58,96,113,130`) | u16 slot tag (helm 3, chest 4, gloves 5, boots 6, cloak 16) (`offsets.h:1633-1634`) |
| `kOff_ItemVal_DyeData` | `0x78` | — (literal `0x78` used in `dye.cpp`) | 16-byte dye record[] (`offsets.h:1637`) |
| `kOff_ItemVal_DyeCount` | `0x80` | `game/dye.cpp:882,911,932,947,960,1178,1300,1472,1498,1502,1537,1832` | u32 dye count (capacity at +0x84) (`offsets.h:1638`) |
| `kOff_ItemVal_SocketData` | `0x60` | — (literal `0x60` at `equipment.cpp:410`) | 1.17+ socket record[] vector data (`offsets.h:1654`) |
| `kOff_ItemVal_SocketData_Legacy` | `0x58` | — (literal `0x58` at `equipment.cpp:418`) | legacy socket record[] (`offsets.h:1655`) |
| `kOff_ItemVal_SocketSize` | `0x68` | — | u32 vector size (always 5) (`offsets.h:1656`) |
| `kOff_ItemVal_SocketCap` | `0x6C` | — | u32 vector capacity (5) (`offsets.h:1657`) |
| `kOff_ItemVal_SocketUnlocked` | `0x70` | — (effective use via `unlockOff = isLegacy ? 0x68 : 0x70`, `equipment.cpp:440`) | u32 unlocked-socket count (`offsets.h:1658`) |
| `kOff_SockRec_GearId` | `0` | `game/equipment.cpp:456,478` | u16 abyss-gear typeId (0xFFFF = empty) (`offsets.h:1662`) |
| `kOff_SockRec_Marker` | `2` | `game/equipment.cpp:479` | u16 0xFFFF filled / 0x0000 empty (`offsets.h:1663`) |
| `kOff_SockRec_Index` | `4` | `game/equipment.cpp:480` | u8 socket index (0xFF = "locked") (`offsets.h:1664`) |
| `kOff_SockRec_State` | `5` | `game/equipment.cpp:481` | u8 0x05 filled / 0x00 empty (`offsets.h:1665`) |
| `kOff_ItemVal_RefineLevel` | `0x0A` | `game/equipment.cpp:683,694,703,717,728,1037,1224,1941,2090,2093` | u16 refinement level, **same field as `kOff_ItemVal_Subtype`** (`offsets.h:1668-1682`) |
| `kOff_ItemVal_Durability` | `0x40` | `game/equipment.cpp:1228,1839,1939` | u16 durability (max ~10000 or 1000) (`offsets.h:1684`) |
| `kOff_FriendlyTrustSiteA_Hook` | `0x12` | — | hook offset for `kSig_FriendlyTrustSiteA` (`offsets.h:1779`) |
| `kOff_FriendlyTrustSiteB_Hook` | `0x12` | — | hook offset for `kSig_FriendlyTrustSiteB` (`offsets.h:1784`) |
| `kOff_FriendlyRec_Key` | `0x00` | `game/friendly.cpp:102,126,168` | u32 relationship record key (`offsets.h:1827`) |
| `kOff_FriendlyRec_Group` | `0x04` | `game/friendly.cpp:103,127,169` | u16 group/bucket key (`offsets.h:1828`) |
| `kOff_FriendlyRec_Value` | `0x20` | `game/friendly.cpp:105,128,153,170,194` | i64 trust value; TU 2.01 keeps `_varyFriendly` here, `+0x28` is a **different** member (`offsets.h:1829-1832`) |

---

### 3.4 Other numeric constants (strides, counts, flags, IDs)

| Constant name | Kind | Value | Len | PE revision(s) | Consumed at | Meaning | PE 2944? |
|---|---|---|---|---|---|---|---|
| `kMinPointer` | threshold | `0x10000000` | n/a | all | ~200 sites (`mem/safe_memory.h:13,22,156,157`; `game/player.cpp`, `game/inventory.cpp`, `game/teleport.cpp`, `game/dye.cpp`, `game/equipment.cpp`, `game/world.cpp`, `game/friendly.cpp`) | Any pointer below this is bogus (`offsets.h:26-27`) | yes |
| `kSizeof_StatEntry` | stride | `0x90` | n/a | all | `game/player.cpp:364,454,504` | stride between stat entries (`offsets.h:78`) | yes |
| `kStatArray_ScanEntries` | count | `64` | n/a | all | `game/player.cpp:362,452,502` | slots scanned from health (`offsets.h:88`) | yes |
| `kCharList_MaxCount` | bound | `8192` | n/a | all | `game/player.cpp:398` | sanity bound on the character vector (live ~388) (`offsets.h:264`) | yes |
| `kIdx_MoveOwner_Up` | index | `1` | n/a | all | `game/teleport.cpp:1218,1220,1221` | vertical (y) component index (`offsets.h:342`) | yes |
| `kSuperJump_RiseThreshold` | float | `1.0f` | n/a | all | `game/teleport.cpp:1218` | minimum upward velocity to amplify (`offsets.h:343-346`) | yes |
| `kNode_Stride` | stride | `0xD8` | n/a | all (**re-derived for 2944**: `stride 0xC0->0xD8`) | `game/teleport.cpp:1150` | gimmick-scene node stride (`offsets.h:479,491`) | yes |
| `kLen_MovGlobalInstr` | length | `7` | n/a | all | `game/teleport.cpp:1806` | length of the `mov reg, cs:<global>` instruction (`offsets.h:539`) | yes |
| `kMax_LeaToPrologue` | bound | `0x180` | n/a | all | `game/teleport.cpp:1760` | how far back the resolver prologue may sit from the `lea r8` (`offsets.h:540-542`) | yes |
| `kLvlBucket_Stride` | stride | `0x100` | n/a | all | `game/teleport.cpp:818` | LevelNameInfo hash-map bucket stride (`offsets.h:550-551`) | yes |
| `kExpected_MarkerMatches` | count | `5` | n/a | all | `game/teleport.cpp:156,544,547,593` | expected `kSig_MarkerPattern` match count (`offsets.h:561`) | yes |
| `kExpected_OriginMatches` | count | `9` | n/a | all (**unused**; code uses literal `6` minimum, `teleport.cpp:549`) | — | documented expected `kSig_MarkerOriginPrefix` count (`offsets.h:565`) | n/a |
| `kMarker_CoordLimit` | float | `1.0e9f` | n/a | all | `game/teleport.cpp:636,637` | coordinate sanity limit (`offsets.h:575`) | yes |
| `kMarker_DestLift` | float | `10.0f` | n/a | all | `game/teleport.cpp:2144` | destination lift (`offsets.h:576`) | yes |
| `kInvSlot_Stride` | stride | `0xC8` | n/a | TU 1.16+ (**unused as a constant**; `equipment.cpp` uses `tbl.stride` literals) | — | 200-byte inventory slots (`offsets.h:849`) | n/a |
| `kInvSlot_EmptyType` | sentinel | `0xFFFF` | n/a | all | `game/inventory.cpp:1827,1841,…`; `game/equipment.cpp:73,362,…`; `game/dye.cpp:517,…` | empty slot type id (`offsets.h:852`) | yes |
| `kItemVal_Size` | size | `0x108` | n/a | 1.17+ | `game/inventory.cpp:3841` | working TrItemValue buffer size; "must not be truncated to 0xC0" (`offsets.h:978-981`) | yes |
| `kPlacement_Stride` | stride | `0xE0` | n/a | 1.17+ | — (unused as a constant) | placement record stride (`offsets.h:984-987`) | yes |
| `kTls_RealmFlag` | offset | `0x1F2` | n/a | pre-TU 2.01 (**documentation only**) | `game/inventory.cpp:1241` | realm flag TLS offset; runtime must use `core::RealmFlagOffsetForRevision()` (`offsets.h:1004-1010`) | no |
| `kGrpOrder_Internal` | sentinel | `0xFFFF` | n/a | all | `game/inventory.cpp:357,381` | `_orderIndex` sentinel for internal groups (`offsets.h:1131`) | yes |
| `kGrpOrder_MaxTopTab` | count | `5` | n/a | all | `game/inventory.cpp:382` | 1..5 = top tabs (`offsets.h:1132`) | yes |
| `kIconPath_None` | sentinel | `0xFFFF` | n/a | all | `game/inventory.cpp:895` | no icon path (`offsets.h:1173`) | yes |
| `kWantedRows_Max` | bound | `4096` | n/a | all | `game/inventory.cpp:5716` | WantedInfo row bound (`offsets.h:1180`) | yes |
| `kLen_FieldTime_Vmovups` | length | `8` | n/a | all | `game/world.cpp:453,454` | `vmovups` instruction length (`offsets.h:1307`) | yes |
| `kLen_TodEngineGlobal_Mov` | length | `7` | n/a | all | `game/world.cpp:554` | `mov cs:<g>, rbx` length (`offsets.h:1374`) | yes |
| `kLen_EnvManager_Mov` | length | `7` | n/a | all | `game/world.cpp:592` | `mov rcx, cs:<pEnvManager>` length (`offsets.h:1405`) | yes |
| `kEquipEntry_Stride` | stride | `0xD0` | n/a | 1.17 | — (unused as a constant; literal `0xD0`) | equip table entry stride; legacy path falls back to `0xC8` (`equipment.cpp:124-130`, `offsets.h:1632`) | yes |
| `kDye_MaxChannels` | count | `12` | n/a | all | 27 sites in `game/dye.cpp`, first at `:526` | max dye channels (`offsets.h:1639`) | yes |
| `kSocketRec_Stride` | stride | `6` | n/a | all | `game/equipment.cpp:456,475` | 6-byte socket record stride (`offsets.h:1659`) | yes |
| `kSocket_Max` | count | `5` | n/a | all | `game/equipment.cpp:453,474,504,508,536,894,929,1001,1044,2001,2108` | absolute max sockets (matches vector capacity) (`offsets.h:1660`) | yes |
| `kSock_Empty` | sentinel | `0xFFFF` | n/a | all | 18 sites in `game/equipment.cpp`, first at `:453` | empty socket gear id (`offsets.h:1666`) | yes |
| `kRefine_Max` | count | `10` | n/a | all | `game/equipment.cpp:1225,1741` | max refinement level (`offsets.h:1683`) | yes |
| `kDyeBatch_Blocks` | count | `10` | n/a | all | `game/dye.cpp:1341` | dye batch blocks (`offsets.h:1734`) | yes |
| `kDyeBatch_BlockSize` | size | `196` | n/a | all | `game/dye.cpp:1343` | u16 tag + pad + 12×16 (`offsets.h:1735`) | yes |
| `kDyeBatch_RecordsOff` | offset | `4` | n/a | all | `game/dye.cpp:1347,1354` | first record inside a block (`offsets.h:1736`) | yes |
| `kDyeBatch_Size` | size | `kDyeBatch_Blocks * kDyeBatch_BlockSize` (= 1960) | n/a | all | `game/dye.cpp:1339` | whole blob size (`offsets.h:1737`) | yes |
| `kOrig_FriendlyTrustBytes[15]` | bytes | `C5 FC 11 49 20 C5 F8 10 47 40 C5 F8 11 41 40` | 15 | **unused** | — | original 15 bytes replaced at the Trust hook site (`offsets.h:1786-1792`) | n/a |
| `kFriendly_Max` | count | `100` | n/a | all | `game/friendly.cpp:107,131,174` | taming/NPC trust cap 0..100 (`offsets.h:1833`) | yes |
| `kWorkerPatchSize` | size | `6` | n/a | 2850, 2944 | `game/worker.cpp:24,27,38,66,113,116,147,162,165,183,186` | patch length (`offsets.h:1841`) | yes |
| `kWorkerPatchOriginal[6]` | bytes | `0F 85 95 00 00 00` | 6 | 2850, 2944 | `game/worker.cpp:75,116,120,161,174,186` | original `JNZ +0x95` (`offsets.h:1842-1843`) | yes |
| `kWorkerPatchEnabled[6]` | bytes | `E9 96 00 00 00 90` | 6 | 2850, 2944 | `game/worker.cpp:79,114,153,165,182` | patched `JMP +0x96 / NOP` (`offsets.h:1844-1845`) | yes |

#### 3.4.1 Namespaced atmosphere/wind node offsets

`src/game/offsets.h:1407-1430`. Both namespaces are `ptrdiff_t` offsets applied to an
`EnvManager`-derived node pointer. Only the `world.cpp` sites below were found.

| Qualified name | Value | Consumed at |
|---|---|---|
| `CN::FOG_A` | `0x134` | `game/world.cpp:677,682,688` |
| `CN::DUST_BASE` | `0x138` | `game/world.cpp:703` |
| `CN::CLOUD_TOP` | `0x13C` | `game/world.cpp:712` |
| `CN::CLOUD_THICK` | `0x140` | `game/world.cpp:692,699,708` |
| `CN::CLOUD_BASE` | `0x144` | `game/world.cpp:716` |
| `CN::DUST_WIND_SCALE` | `0x158` | `game/world.cpp:728,735` |
| `CN::DUST_THRESH` | `0x198` | `game/world.cpp:691,704` |
| `CN::STORM_THRESH` | `0x19C` | `game/world.cpp:690,698` |
| `CN::FOG_B` | `0x1A0` | `game/world.cpp:678,683,689` |
| `CN::DUST_ADD` | `0x1A4` | — (no consumer found) |
| `WN::DIR_X` | `0x18` | — (no consumer found) |
| `WN::DIR_Z` | `0x1C` | — (no consumer found) |
| `WN::TURB_DENS` | `0x60` | — (no consumer found) |
| `WN::TURB_SCALE` | `0x64` | — (no consumer found) |
| `WN::TURB_LIFT` | `0x68` | `game/world.cpp:727,743` |
| `WN::SPEED` | `0x88` | `game/world.cpp:725,734,761` (the `SPEED` hits elsewhere in the tree are unrelated identifiers) |
| `WN::GUST` | `0x9C` | `game/world.cpp:726,739` |
| `WN::CLOUD_SCROLL_X` | `0xD0` | `game/world.cpp:747` |
| `WN::CLOUD_SCROLL_Z` | `0xD4` | `game/world.cpp:748` |

---

### 3.5 Hardcoded addresses / offsets **outside** `offsets.h`

These are the only places Trinity bakes an absolute module-relative address (as opposed to scanning).

| Value | Expression | File:line | Revision gate | Purpose |
|---|---|---|---|---|
| `+0x16077B0` | `gameBase + 0x16077B0` | `game/inventory.cpp:2098` | **`revision < 2625` only** | money getter hook #1 (`hkGetMoney1`) |
| `+0x16078C0` | `gameBase + 0x16078C0` | `game/inventory.cpp:2099` | `revision < 2625` | money getter hook #2 (`hkGetMoney2`) |
| `+0x16081D0` | `gameBase + 0x16081D0` | `game/inventory.cpp:2100` | `revision < 2625` | money getter hook #3 (`hkGetMoney3`) |
| `+0x6350EE8` | `gameBase + 0x6350EE8` | `game/inventory.cpp:1952` | **`revision >= 2625`** | fallback global for table `"WantedInfo"` |
| `+0x63307A8` | `gameBase + 0x63307A8` | `game/inventory.cpp:1958` | `revision >= 2625` | fallback global for table `"tribeinfo"` |
| `+0x634DDB8` | `gameBase + 0x634DDB8` | `game/inventory.cpp:1986` | `revision >= 2625` | `categorygroupinfo` global in PE 1.0.0.2625 (validated by reading the pointer before accepting) |
| `+0x634DDD0` | `gameBase + 0x634DDD0` | `game/inventory.cpp:1987` | `revision >= 2625` | `categoryinfo` global in PE 1.0.0.2625 (same) |

Verbatim gate for the money hooks (`src/game/inventory.cpp:2093-2105`):

```cpp
uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
// Legacy money display hooks: only valid on TU 1.18.02 (PE rev < 2625).
// On TU 2.00+ these offsets point to arbitrary/invalid code - skip them.
if (core::GetGameVersion().revision < 2625)
{
    MH_CreateHook(reinterpret_cast<void*>(gameBase + 0x16077B0), hkGetMoney1, reinterpret_cast<void**>(&oGetMoney1));
    MH_CreateHook(reinterpret_cast<void*>(gameBase + 0x16078C0), hkGetMoney2, reinterpret_cast<void**>(&oGetMoney2));
    MH_CreateHook(reinterpret_cast<void*>(gameBase + 0x16081D0), hkGetMoney3, reinterpret_cast<void**>(&oGetMoney3));
}
else
{
    LOG("inventory: legacy money hooks skipped (TU 2.00+, offsets no longer valid).");
}
```

### 3.6 Inline AOBs and inline byte arrays not declared in `offsets.h`

| Pattern / bytes | File:line | Purpose | Revision gate |
|---|---|---|---|
| `48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00` (13 B) | `game/equipment.cpp:1574` | native `ResizeSocketVector` (declared as `ResizeSocketVector_t` at `equipment.cpp:49-50`) | none (all) |
| `48 8B 05 ?? ?? ?? ?? 48 8B 98 A8 00 00 00 C4 C1 78 10 04 24` (19 B) | `game/teleport.cpp:529` | direct map-destination global reference; `ResolveRipAt(..., 7)` at `:532` | none; if it resolves it replaces the legacy marker-capture hooks (`teleport.cpp:534-542`) |
| `48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC ? 4C 8B` (32 B) | `game/inventory.cpp:2123` | legacy fuzzy ctor candidate 2 | `MayUseLegacyFuzzySignaturesForRevision` |
| `48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC` (26 B) | `game/inventory.cpp:2124` | legacy fuzzy ctor candidate 3 | same |
| `48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC` (26 B) | `game/inventory.cpp:2125` | legacy fuzzy ctor candidate 4 | same |
| `48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 48 8B 09` (18 B) | `game/inventory.cpp:2126` | legacy fuzzy ctor candidate 5 | same |
| `48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC` (21 B) | `game/inventory.cpp:2127` | legacy fuzzy ctor candidate 6 | same |
| `C5 F8 11 88 B0 01 00 00` (8 B expected bytes) | `game/teleport.cpp:438` | expected original bytes at the marker-player hook target | none |
| `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 50 8B 39 48 8B 1D` (23 B, as `uint8_t kPrologue32[]`) | `game/teleport.cpp:1744-1748` | byte-array form of `kSig_TableResolverPrologue` | none |
| `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 50 0F B7 39 48 8B 1D` (23 B, as `uint8_t kPrologue16[]`) | `game/teleport.cpp:1749-1753` | 16-bit-key variant (`movzx edi, word ptr [rcx]`); **no current caller uses key16** (`teleport.cpp:1739-1740`) | none |
| `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56` (15 B, `kPrologueShort[]`) | `game/teleport.cpp:1754-1757` | shortened prologue accepted as a secondary match | none |

### 3.7 Disabled / dead locators (documented but deliberately inert)

| Item | Address / value | File:line | Why disabled (source comment) |
|---|---|---|---|
| Marker protection hook | target `0x14C4542E2` | `game/teleport.cpp:461-468`, comment repeated at `:2094` | "Disabled: target at 0x14C4542E2 is an internal engine streaming/task queue ring buffer, NOT player collision. Overwriting [rsi+8] with [rsi+0x18] actively corrupts engine heap pool allocation, causing crashes in SceneObjectServer@pa (0x14040BCCC)." (`teleport.cpp:463-466`) — `InstallMarkerProtectionHook` always `return false;` |
| `kSig_TrItemValueDtor` | `""` | `game/offsets.h:942`, consumed `game/inventory.cpp:2161` | Empty pattern → `FindPattern` returns 0; `if (dtorAddr) oItemValueDtor = …` never fires |
| Heap-scanner money search | — | `game/inventory.cpp:4512-4544` | "Heap Scanner disabled: Removed unsafe WeMod memory scanning that caused memory corruption." (`inventory.cpp:4542`) |
| Legacy money getters on TU 2.00+ | see §3.5 | `game/inventory.cpp:2096-2105` | Offsets "point to arbitrary/invalid code" |

---

## 4. Legacy vs current

Locators explicitly marked legacy / pre-`<revision>` / must-not-be-used-as-fallback, with the source
comment quoted.

### 4.1 Explicit "must not be used as a fallback" gate

```cpp
// src/game/offsets.h:405-409
// PE 2760/2850 modern contract.  Do not use it as a PE 2944 fallback:
// the PE 2944 branch above is explicit so a later changed build fails closed.
inline constexpr const char* kSig_LocoStepper =
```

Selection is closed-form, not a fallback chain — an unrecognised revision gets **no** hook:

```cpp
// src/game/teleport.cpp:1903-1925
// Locomotion sub-step driver for Super Run and Free Flight.  Select an
// explicitly observed ABI; an unknown newer revision must remain off.
...
case core::LocoStepperContract::Unsupported:
default:
    LOG_ERR("teleport: locomotion-stepper contract unavailable for PE %u - Super Run/Free Flight disabled.",
            static_cast<unsigned>(revision));
```

### 4.2 Broader legacy-fuzzy policy

```cpp
// src/core/version_mapping.h:33-35
// Broad legacy signatures are a compatibility fallback for older builds.
// They must never be used for TU 2.01.00 or an unknown newer revision.
bool MayUseLegacyFuzzySignaturesForRevision(uint16_t revision);
```

```cpp
// src/core/version_mapping.cpp:62-65
bool MayUseLegacyFuzzySignaturesForRevision(uint16_t revision)
{
    return !UsesTu201CompatibleRevision(revision) && revision < 2760;
}
```

This gate guards the five inline legacy ctor AOBs + `kSig_TrItemValueCtor_Pre201`
(`src/game/inventory.cpp:2119-2151`).

### 4.3 Legacy crime dispatcher

```cpp
// src/core/version_mapping.h:37-40
// The four-argument crime-event dispatcher was verified only through PE
// 2850. PE 2944 no longer contains that function contract, so probing its
// old signature would report an expected incompatibility as an error.
bool MayProbeLegacyCrimeEventDispatcherForRevision(uint16_t revision);
```

```cpp
// src/game/inventory.cpp:2076-2083
else
{
    // PE 2944's prior event dispatcher no longer exists.  No Bounty
    // remains supported through WantedInfo and wanted-state evaluation;
    // only crime-banner/minimap/guard-dispatch suppression is absent.
    LOG("world: crime-event dispatch bypass unavailable on PE %u; WantedInfo and wanted-state No Bounty remain active.",
        static_cast<unsigned>(revision));
}
```

### 4.4 Moving-owner offset must not be reused

```cpp
// src/core/version_mapping.cpp:34-37
case LocoStepperContract::Pe2944:
    // PE 2944 live evidence: the installed stepper logged the local
    // move-owner at [component+0x2C0].  +0x2B8 is not that pointer.
    return 0x2C0;
```

```cpp
// src/game/teleport.cpp:1356-1359
// TEMP diagnostic (first fire only): scan the movement component for
// the player's move-owner pointer to recover the correct PE 2944
// offset. The old 0x2B8 now holds floats (a quaternion/vector), so
// the pointer moved; log whichever offset actually stores it.
```

### 4.5 Holder-insert frame must not be inherited

```cpp
// src/game/offsets.h:673-675
// This deliberately has a separate signature so a later frame-only
// recompile cannot silently inherit PE 2850's more specific layout.
```

```cpp
// src/game/inventory.cpp:2261-2265
// PE 2944 retained the planner's argument ABI but changed its
// stack frame; it must not inherit PE 2850's frame-specific AOB.
const char* holderInsertSig = revision == 2944
    ? kSig_InvHolderInsert2944
    : kSig_InvHolderInsert201;
```

### 4.6 Superseded mechanisms

| Superseded locator / mechanism | Replacement | Source |
|---|---|---|
| Per-frame pin of the field-clock globals | `kSig_FieldTimeTick` delta-zeroing | "This supersedes the old pin-the-globals-every-frame freeze, which lost a race against this very function" (`offsets.h:1324-1326`) |
| `kOff_InvBucket_MaxSlots` direct write | `kSig_InvSetExpandSlots` hook, or write `kOff_InvBucket_ExpandSlots` | "Preferred over writing kOff_InvBucket_MaxSlots directly, which only pokes a cache the engine recomputes" (`offsets.h:608-609`); `offsets.h:826-837` |
| `TimeOfDayManager +0x3D0` as the clock | the two BSS realm globals | "the 'TimeOfDayManager' and its +0x3D0 'currentTimeOfDay' float are a RENDER MIRROR that nothing reads back (writing it is a no-op, live-confirmed)" (`offsets.h:1284-1286`) |
| Old `kSig_LocoStepper` for PE 2944 | `kSig_LocoStepper_PE2944` | `offsets.h:405-406` |
| Old char-manager signature keyed on register allocation | `kCharMgrAnchors` ABI-only anchors | `offsets.h:203-209` |
| Pre-1.17/1.18 dye leaf calls | `kSig_DyeApplyBatch` + `kSig_DyeUpsert` on PE ≥ 2625 | "On TU 2.00+ (PE >= 2625), DyeApplyBatch + DyeUpsert is the genuine official pipeline; avoid calling outdated TU 1.18 leaf hooks." (`dye.cpp:1628-1630`) |
| Legacy money display hooks | skipped on PE ≥ 2625 | `inventory.cpp:2094-2105` |
| `kOff_StatEntry_*` direct writes as the God-Mode guard | `kSig_StatCommit` hook (non-TU201) / modern continuous stat-pin guard (TU201) | `offsets.h:137-139`, `player.cpp:818-830` |

### 4.7 Constants named `*_Legacy` / `*_Pre201` (all are fallbacks, tried only after the modern form fails)

`kSig_InvGetItemQty_Legacy`, `kSig_InvHolderInsert_Legacy`, `kSig_InvCommit_Pre201`,
`kSig_InvCoreGlobal_Pre201`, `kSig_TrItemValueCtor_Pre201`, `kSig_LocoStepper_Pre201`,
`kSig_TravelToNode_Pre201`, `kSig_TravelToNode_Legacy`, `kSig_FrameTimerBody_Pre201`,
`kSig_FieldTimeTick_Pre201`, `kSig_WindPack_Pre201`, `kSig_EnvManager_Legacy`,
`kSig_LocStringGet_Legacy`, `kSig_EquipBatch_Legacy`, `kSig_DyeApplyBatch_Legacy`,
`kSig_DyeUpsert_Legacy`, `kSig_EquipEffectRefresh_Legacy`,
`kOff_ItemVal_SocketData_Legacy`.

**Exception:** `kSig_LocoStepper_Pre201` and the `_Legacy` travel sigs are *inside* the
`revision != 2944` chain, so on PE 2944 they are never tried at all.

---

## 5. Structure field offsets

All values from `src/game/offsets.h`. Decimal is exact. "Structure" names follow the source comments.

### 5.1 Actor / character / stat chain

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_Owner_Actor` | `0x68` | 104 | → actor | gameplay character ("owner", vtable `0x50B9A10`) |
| `kOff_Actor_StatusMarker` | `0x20` | 32 | → status marker | actor |
| `kOff_Owner_ObjectType` | `0x48` | 72 | int32 ObjectType — documentation only | owner |
| `kOff_Owner_Possessor` | `0xA0` | 160 | → possessor / controller | owner |
| `kOff_Possessor_Pawn` | `0xD0` | 208 | → back-ref to owner | possessor |
| `kOff_Owner_TypeDesc` | `0x88` | 136 | → type descriptor; tag byte at +1 | owner |
| `kOff_Marker_TargetOwner` | `0x18` | 24 | → vital / target owner | status marker |
| `kOff_Root_StatArray` | `0x58` | 88 | ptr → stat entry[0] (Health) | root / component |

**Stat-entry node layout** (0x90-byte stride, `kSizeof_StatEntry`):

| Constant | Hex | Dec | Meaning |
|---|---|---|---|
| `kOff_StatEntry_Type` | `0x00` | 0 | int32 type id |
| `kOff_StatEntry_Current` | `0x08` | 8 | int64 absolute current |
| `kOff_StatEntry_Base` | `0x18` | 24 | int64 base value |
| `kOff_StatEntry_Norm` | `0x20` | 32 | int64 current − base |
| `kOff_StatEntry_Floor` | `0x28` | 40 | int64 lower clamp |
| `kOff_StatEntry_Cap` | `0x30` | 48 | int64 max / cap |
| `kSizeof_StatEntry` | `0x90` | 144 | **node stride** |

Stat-entry **type IDs** (values at `entry+0x00`, `enum StatType`, `offsets.h:92-114`):
`StatType_Health = 0`, `StatType_Stamina = 17`, `StatType_Spirit = 18`, `StatType_MountSprint = 19`,
`StatType_SprintSt = 20`, `StatType_SpiritPool = 21`, `StatType_StaminaPool117 = 22`,
`StatType_SpiritPool117 = 23`, `StatType_MountAbility = 48`.

`ObjectType` enum (`offsets.h:47-57`): `Obj_SelfUser = 0`, `Obj_SelfPlayer = 1`, `Obj_GamePlayData = 2`,
`Obj_NonPlayerCharacter = 3`, `Obj_Mercenary = 4`, `Obj_Vehicle = 5`, `Obj_Pet = 6`,
`Obj_OtherPlayer = 9` — documentation only, not used for player resolution (`offsets.h:36-42`).

### 5.2 Character manager

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_CharMgr_ListData` | `0xB8` | 184 | `character*[]` data ptr | character manager (`pa` vector) |
| `kOff_CharMgr_ListCount` | `0xC0` | 192 | u32 count | character manager |
| `kCharList_MaxCount` | — | 8192 | sanity bound (live ~388) | character manager |

Element stride: `character*` at `data + 8*i` (`offsets.h:259-261`).

### 5.3 Movement controller ("move owner")

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_MoveOwner_Position` | `0x90` | 144 | x,y,z,w f32 | move controller (Havok character proxy) |
| `kOff_MoveOwner_Velocity` | `0xD0` | 208 | x,y,z,w f32 | move controller |
| `kOff_MoveOwner_DesiredVel` | `0xC0` | 192 | x,y,z,w f32 (input velocity) | move controller |
| `kIdx_MoveOwner_Up` | — | 1 | vertical (y) component index | move controller |
| `kOff_Player_Dest0` | `0x90` | 144 | Vec3 x,y,z destination #0 | player proxy |
| `kOff_Player_Dest1` | `0x1A0` | 416 | Vec3 x,y,z destination #1 | player proxy |

Move-owner offset **inside the locomotion movement component** (revision-dependent, **not** a constant):

| Revision | Offset | Source |
|---|---|---|
| 2944 | `0x2C0` | `version_mapping.cpp:36-37` |
| 2760, 2850 | `0x2B8` | `version_mapping.cpp:38-39` |
| 2625, 2658, 2692 | `0x298` | `version_mapping.cpp:40-41` |

### 5.4 Fast-travel / gimmick-scene registry and area boxes

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_Registry_SceneCount` | `0x08` | 8 | u32 sceneCount | LevelGimmickSceneObjectInfo registry |
| `kOff_Registry_SceneTable` | `0x50` | 80 | ptr[] sceneTable (`desc = *(sceneTable + 8*sceneId)`) | registry |
| `kOff_SceneDesc_NodeCount` | `0x28` | 40 | u32 nodeCount | scene descriptor |
| `kOff_SceneDesc_NodeArray` | `0x20` | 32 | ptr nodeArray | scene descriptor |
| `kOff_SceneDesc_StringKey` | `0x08` | 8 | engine string `_stringKey` | scene descriptor |
| `kOff_SceneDesc_IsBlocked` | `0x10` | 16 | bool | scene descriptor |
| `kOff_SceneDesc_LevelName` | `0x18` | 24 | engine string | scene descriptor |
| `kOff_SceneDesc_UseTeleport` | `0x49` | 73 | bool `_useTeleport` | scene descriptor |
| `kOff_SceneDesc_IsEmpty` | `0x6C` | 108 | bool | scene descriptor |
| `kOff_Node_Position` | `0x84` | 132 | f32 x,y,z | scene node |
| `kOff_Node_Gimmick` | `0x10` | 16 | u16 gimmick/type word (**UNVERIFIED on PE 2944**) | scene node |
| `kNode_Stride` | `0xD8` | 216 | **node stride** (2944 re-derived `0xC0 → 0xD8`) | scene node array |

Scene descriptor total size: 112 bytes (`offsets.h:496`).

LevelNameTableInfo row / bucket / entry (`offsets.h:544-556`):

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_LvlRow_BucketCount` | `0x20` | 32 | u32 | LevelNameTableInfo row (64 B) |
| `kOff_LvlRow_Size` | `0x24` | 36 | u32 entry count | row |
| `kOff_LvlRow_Buckets` | `0x30` | 48 | ptr | row |
| `kOff_LvlRow_Entries` | `0x38` | 56 | ptr (entry* array) | row |
| `kLvlBucket_Stride` | `0x100` | 256 | **bucket stride** | hash-map bucket |
| `kOff_LvlBucket_Pairs` | `0x08` | 8 | (hash u32, entryIdx u32) pairs | bucket |
| `kOff_LvlEntry_Name` | `0x10` | 16 | engine string | LevelNameInfo entry |
| `kOff_LvlEntry_IsSector` | `0x18` | 24 | bool `_isSectorLevel` | entry |
| `kOff_LvlEntry_Box` | `0x1C` | 28 | f32 min xyz, max xyz | entry |

### 5.5 Inventory: container → holder → bucket → slot

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_Global_Mid` | `0x30` | 48 | global → mid | core global singleton (IDB `qword_6180C28`) |
| `kOff_Mid_Container` | `0x50` | 80 | mid → container | mid |
| `kOff_Container_Sub` | `0x68` | 104 | container → sub-object | inventory container |
| `kOff_Sub_Holder` | `0xB8` | 184 | sub → item holder | sub-object |
| `kOff_InvHolder_Container` | `0x08` | 8 | holder → container | item holder |
| `kOff_InvHolder_Buckets` | `0x18` | 24 | ptr[] buckets | item holder |
| `kOff_InvHolder_Count` | `0x20` | 32 | u32 bucket count | item holder |
| `kOff_InvBucket_Slots` | `0x00` | 0 | ptr[] slot array | bucket |
| `kOff_InvBucket_Count` | `0x08` | 8 | u16 slot-array SIZE (headroom ~1460) | bucket |
| `kOff_InvBucket_Type` | `0x10` | 16 | u16 InventoryType | bucket |
| `kOff_InvBucket_UsedSlots` | `0x12` | 18 | u16 used count | bucket |
| `kOff_InvBucket_MaxSlots` | `0x14` | 20 | u16 LIVE cap (derived cache) | bucket |
| `kOff_InvBucket_DeltaRaw` | `0x16` | 22 | u16 raw delta accumulator | bucket |
| `kOff_InvBucket_DeltaClamped` | `0x18` | 24 | u16 clamped delta accumulator | bucket |
| `kOff_InvBucket_ExpandSlots` | `0x1A` | 26 | u16 `_varyExpandSlotCount` (real driver) | bucket |
| `kInvSlot_Stride` | `0xC8` | 200 | **slot stride** (TU 1.16+) | inventory slot (== TrItemValue) |
| `kOff_InvSlot_TypeId` | `0x08` | 8 | u16 item type id | inventory slot |
| `kOff_InvSlot_Quantity` | `0x10` | 16 | i64 quantity | inventory slot |
| `kInvSlot_EmptyType` | `0xFFFF` | 65535 | empty sentinel | inventory slot |

### 5.6 Item-value / placement / realm / tables

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_Sub_IdAllocator` | `0x10` | 16 | → instance-id allocator | (owner+0x68) sub-object |
| `kOff_IdAlloc_Counter` | `0x20` | 32 | i64 `InterlockedIncrement64` target | id allocator |
| `kItemVal_Size` | `0x108` | 264 | working buffer size (must not truncate to 0xC0) | TrItemValue working copy |
| `kOff_ItemVal_InstanceId` | `0x00` | 0 | i64 instance id (−1 out of ctor) | TrItemValue |
| `kOff_ItemVal_Subtype` | `0x0A` | 10 | u16 subtype | TrItemValue |
| `kOff_ItemVal_RefineLevel` | `0x0A` | 10 | u16 refinement level (== subtype) | TrItemValue |
| `kOff_ItemVal_Durability` | `0x40` | 64 | u16 durability | TrItemValue |
| `kPlacement_Stride` | `0xE0` | 224 | **placement-record stride** | planner placement record |
| `kOff_Placement_SlotIdx` | `0xD8` | 216 | u16 slot index | placement record |
| `kOff_Teb_TlsPointer` | `0x58` | 88 | `TEB.ThreadLocalStoragePointer` | TEB |
| `kTls_RealmFlag` | `0x1F2` | 498 | u8 realm flag (pre-TU 2.01 documentation value) | TLS block |
| `kOff_ItemResolver_MovGlobal` | `0x15` | 21 | offset of the 7-byte mov-global in the 16-bit-key resolver clone | resolver code |
| `kOff_TableResolver_MovGlobal` | `0x14` | 20 | same, 32-bit-key clone | resolver code |
| `kLen_MovGlobalInstr` | — | 7 | length of the mov-global instruction | resolver code |
| `kMax_LeaToPrologue` | `0x180` | 384 | back-scan bound from `lea r8` to prologue | resolver code |
| `kOff_ItemTable_Count` | `0x08` | 8 | u32 count | *info table header |
| `kOff_ItemTable_Defs` | `0x58` | 88 | ptr[] defs | *info table header |
| `kOff_ItemDef_Key` | `0x08` | 8 | ptr → string obj | ItemInfo def |
| `kOff_ItemDef_Name` | `0x20` | 32 | loc-string `_itemName` | ItemInfo def |
| `kOff_ItemDef_Icons` | `0x90` | 144 | `_itemIconList` vector | ItemInfo def |
| `kOff_ItemDef_Tier` | `0x210` | 528 | u8 `_itemTier` | ItemInfo def |
| `kOff_ItemDef_Groups` | `0x350` | 848 | `_itemGroupInfoList` vector | ItemInfo def |
| `kOff_ItemDef_BucketType` | `0x418` | 1048 | u16 `_defaultPushInventoryInfo` (TU 1.17–1.18.02) | ItemInfo def |
| `kOff_ItemDef_MaxStackCount` | `0x18` | 24 | i64 `_maxStackCount` | ItemInfo def |
| `kOff_ItemDef_ApplyMaxStackCap` | `0x111` | 273 | u8 `_applyMaxStackCap` | ItemInfo def |
| `kOff_InvDef_Key` | `0x08` | 8 | `_stringKey` | InventoryInfo row |
| `kOff_InvDef_DefSlots` | `0x48` | 72 | u16 `_defaultSlotCount` | InventoryInfo row |
| `kOff_InvDef_MaxSlots` | `0x4A` | 74 | u16 `_maxSlotCount` | InventoryInfo row |
| `kOff_InvDef_Name` | `0x70` | 112 | `_InventoryNameUIText` | InventoryInfo row |
| `kOff_GrpDef_Key` | `0x08` | 8 | ptr → string obj `_stringKey` | ItemGroupInfo row |
| `kOff_GrpDef_Name` | `0x18` | 24 | loc-string `_groupName` | ItemGroupInfo row |
| `kOff_GrpDef_Order` | `0x68` | 104 | u16 `_orderIndex` | ItemGroupInfo row |
| `kOff_GrpDef_Icon` | `0x6C` | 108 | u16 `_iconPath` | ItemGroupInfo row |
| `kGrpOrder_Internal` | `0xFFFF` | 65535 | internal-group sentinel | `_orderIndex` value |
| `kGrpOrder_MaxTopTab` | — | 5 | 1..5 = top tabs | `_orderIndex` value |
| `kOff_StrDef_Buffer` | `0x18` | 24 | `_buffer` ptr → string obj | stringinfo row |
| `kOff_IconData_Path` | `0x00` | 0 | u16 `_iconPath` → stringinfo row | ItemIconData (32 B) |
| `kIconPath_None` | `0xFFFF` | 65535 | no icon | `_iconPath` value |
| `kOff_Vec_Data` | `0x00` | 0 | `T*` | engine vector |
| `kOff_Vec_Count` | `0x08` | 8 | u32 count | engine vector |
| `kOff_WantedDef_IncreasePrice` | `0x18` | 24 | i64 `_increasePrice` | WantedInfo row |
| `kOff_WantedDef_IsBlocked` | `0x10` | 16 | u8 `_isBlocked` | WantedInfo row |
| `kWantedRows_Max` | — | 4096 | row bound | WantedInfo |
| `kOff_LocGet_MovGlobal` | `0x03` | 3 | offset of `mov rax, cs:<locMgr>` | loc-string getter code |
| `kOff_LocProv_Offset` | `0x18` | 24 | u32 offset into blob | loc-string provider |
| `kOff_LocMgr_Blob` | `0x58` | 88 | ptr → blob | locMgr |
| `kOff_LocBlob_Data` | `0x00` | 0 | `char*` | blob |
| `kOff_LocBlob_Size` | `0x08` | 8 | u32 used bytes | blob |

ItemDef `BucketType` is **not** a constant at runtime — `inventory.cpp` selects it:

```cpp
// src/game/inventory.cpp:3740-3746
// TU 2.00.00 (PE rev >= 2625): BucketType confirmed at ItemDef+0x428
// by binary analysis of InvHolderInsert (VA 0x142091150) and
// InvCommitPlacement (VA 0x141DF9CF0) - both read def+0x428 then
// compare with bucket+0x10. 225 hits across binary vs 7 for +0x420.
// TU 1.17/1.18: BucketType at ItemDef+0x418 (game's own bucket lookup).
// Legacy TU <= 1.16: +0x42 (66).
const uintptr_t bucketOff = (core::GetGameVersion().revision >= 2625) ? 0x428 : 0x418;
```

Same expression appears again at `src/game/inventory.cpp:4142` (independent implementation).

### 5.7 Time / weather

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_TimeStruct_Delta` | `0x64` | 100 | f32 frame delta (seconds) | frame-timer object |
| `kOff_TimeStruct_ScaledDelta` | `0x68` | 104 | f32 scaled frame delta | frame-timer object |
| `kOff_FieldTime_ServerVmovups` | `0x10` | 16 | server-realm `vmovups` in the match | `kSig_FieldTimeRealm` match |
| `kOff_FieldTime_ClientVmovups` | `0x1A` | 26 | client-realm `vmovups` | same match |
| `kLen_FieldTime_Vmovups` | — | 8 | instruction length | same match |
| `kOff_FieldTime_Day` | `0x00` | 0 | i32 day | 32-byte field-clock struct |
| `kOff_FieldTime_Hour` | `0x04` | 4 | i32 hour | field-clock struct |
| `kOff_FieldTime_Min` | `0x08` | 8 | i32 minute | field-clock struct |
| `kOff_FieldTime_Sec` | `0x0C` | 12 | i32 second | field-clock struct |
| `kOff_TodEngineGlobal_Mov` | — | 9 | offset of `48 89 1D <disp32>` | `kSig_TodEngineGlobal` match |
| `kLen_TodEngineGlobal_Mov` | — | 7 | instruction length | same match |
| `kOff_Tod_Manager` | `0x2F8` | 760 | engine → render manager | engine object (`qword_648F688`) |
| `kOff_Tod_CurrentHour` | `0x3D0` | 976 | f32 hours 0..24 | TimeOfDay render manager |
| `kOff_Tod_LowerLimit` | `0x3D4` | 980 | f32 clamp lower | TimeOfDay render manager |
| `kOff_Tod_UpperLimit` | `0x3D8` | 984 | f32 clamp upper | TimeOfDay render manager |
| `kOff_EnvManager_Mov` | — | 3 | defined, **unused** | EnvManager anchor |
| `kLen_EnvManager_Mov` | — | 7 | instruction length (used at offset 0) | EnvManager anchor |
| `CN::*` / `WN::*` | see §3.4.1 | | cloud/atmosphere and wind node fields | EnvManager-derived nodes |

### 5.8 Equipment / dye / sockets

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_Sub_EquipComp` | `0x38` | 56 | (actor+0x68)+0x38 → equip component | character sub-object |
| `kOff_EquipComp_Owner` | `0x08` | 8 | → owning actor | equip component |
| `kOff_EquipComp_Table` | `0x80` | 128 | → table descriptor (1.17) | equip component |
| `kOff_EquipTable_Array` | `0x08` | 8 | entry[] base | equip table descriptor |
| `kOff_EquipTable_Count` | `0x10` | 16 | u32 entry count | equip table descriptor |
| `kEquipEntry_Stride` | `0xD0` | 208 | **equip entry stride** (1.17; legacy fallback `0xC8` = 200) | equip table entry |
| `kOff_EquipEntry_SlotTag` | `0xC8` | 200 | u16 slot tag (legacy fallback `0xC0` = 192) | equip table entry |
| `kOff_ItemVal_DyeData` | `0x78` | 120 | → 16-byte dye record[] | TrItemValue |
| `kOff_ItemVal_DyeCount` | `0x80` | 128 | u32 dye count (cap at +0x84) | TrItemValue |
| `kDye_MaxChannels` | — | 12 | max dye channels (16-byte records) | dye vector |
| `kOff_ItemVal_SocketData` | `0x60` | 96 | → socket record[] vector data (1.17+) | TrItemValue |
| `kOff_ItemVal_SocketData_Legacy` | `0x58` | 88 | → socket record[] (legacy) | TrItemValue |
| `kOff_ItemVal_SocketSize` | `0x68` | 104 | u32 vector size (always 5) | TrItemValue socket vector |
| `kOff_ItemVal_SocketCap` | `0x6C` | 108 | u32 vector capacity (5) | TrItemValue socket vector |
| `kOff_ItemVal_SocketUnlocked` | `0x70` | 112 | u32 unlocked-socket count | TrItemValue |
| `kSocketRec_Stride` | — | 6 | **socket record stride** | socket record |
| `kSocket_Max` | — | 5 | absolute max sockets | socket vector |
| `kOff_SockRec_GearId` | `0x00` | 0 | u16 gear typeId (0xFFFF = empty) | socket record (6 B) |
| `kOff_SockRec_Marker` | `0x02` | 2 | u16 0xFFFF filled / 0x0000 empty | socket record |
| `kOff_SockRec_Index` | `0x04` | 4 | u8 socket index (0xFF = locked) | socket record |
| `kOff_SockRec_State` | `0x05` | 5 | u8 0x05 filled / 0x00 empty | socket record |
| `kSock_Empty` | `0xFFFF` | 65535 | empty gear sentinel | socket record |
| `kRefine_Max` | — | 10 | max refinement level | refinement |
| `kDyeBatch_Blocks` | — | 10 | batch blocks | dye batch blob |
| `kDyeBatch_BlockSize` | — | 196 | u16 tag + pad + 12×16 | dye batch block |
| `kDyeBatch_RecordsOff` | — | 4 | first record offset in a block | dye batch block |
| `kDyeBatch_Size` | — | 1960 | `10 * 196` | dye batch blob |

Equip-table probe lists that act as *de facto* offsets (all hardcoded in `equipment.cpp`):
`{ 0x90, 0x80, 0x50, 0x38, 0x40, 0x48, 0x60, 0x70 }` (`equipment.cpp:137`),
`{ 0x60, 0x68, 0x70, 0x58, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0 }` (`equipment.cpp:181`),
`{ 0x38, 0x30, 0x40, 0x28, 0x48, 0x50, 0x58, 0x60, 0x68 }` (`equipment.cpp:182`),
`{ 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0x168 }`
(`equipment.cpp:196`).

### 5.9 Relationship / trust records

| Constant | Hex | Dec | Meaning | Structure |
|---|---|---|---|---|
| `kOff_FriendlyRec_Key` | `0x00` | 0 | u32 record key | relationship record (0x58 B pre-2.01, 0x68 B TU 2.01) |
| `kOff_FriendlyRec_Group` | `0x04` | 4 | u16 group/bucket key | relationship record |
| `kOff_FriendlyRec_Value` | `0x20` | 32 | i64 trust value | relationship record |
| `kFriendly_Max` | — | 100 | taming/NPC trust cap | trust value |
| `kOff_FriendlyTrustSiteA_Hook` | `0x12` | 18 | hook offset, unused | `kSig_FriendlyTrustSiteA` |
| `kOff_FriendlyTrustSiteB_Hook` | `0x12` | 18 | hook offset, unused | `kSig_FriendlyTrustSiteB` |

---

## 6. Feature-to-locator map

Each cell lists the locator constants the feature depends on, in dependency order.
"Resolver chain" entries are the offsets walked, not constants.

### 6.1 Travel / fast-travel — `game/teleport.cpp`

* **Signatures:** `kSig_TravelToNode` → `kSig_TravelToNode_Pre201` → `kSig_TravelToNode_Legacy`
  (all `revision != 2944`); `kSig_TravelToNode_PE2944` + `kSig_TravelDispatcher_PE2944`
  (`revision == 2944`)
* **String anchors / resolver discovery:** `kStr_GimmickSceneTable` (+ fallback literals
  `"regioninfo"`, `"fieldinfo"`), `kStr_LevelNameTable`, `kSig_LeaR8Rip`, `kSig_TableResolverPrologue`,
  `kOff_TableResolver_MovGlobal`, `kLen_MovGlobalInstr`, `kMax_LeaToPrologue`
* **Offsets — registry/scene/node:** `kOff_Registry_SceneCount`, `kOff_Registry_SceneTable`,
  `kOff_SceneDesc_NodeCount`, `kOff_SceneDesc_NodeArray`, `kOff_SceneDesc_StringKey`,
  `kOff_SceneDesc_IsBlocked`, `kOff_SceneDesc_UseTeleport`, `kOff_SceneDesc_IsEmpty`,
  `kOff_SceneDesc_LevelName`, `kNode_Stride`, `kOff_Node_Position`, `kOff_Node_Gimmick`
* **Offsets — area boxes:** `kOff_LvlRow_BucketCount`, `kOff_LvlRow_Size`, `kOff_LvlRow_Buckets`,
  `kOff_LvlRow_Entries`, `kLvlBucket_Stride`, `kOff_LvlBucket_Pairs`, `kOff_LvlEntry_Name`,
  `kOff_LvlEntry_IsSector`, `kOff_LvlEntry_Box`
* **Sites:** `teleport.cpp:1844-1901`, `:1102-1180`, `:795-840`, `:1698-1834`

### 6.2 Marker teleport — `game/teleport.cpp` / `game/marker_teleport_logic.h` / `game/map_marker.h`

* **Signatures:** `kSig_MarkerPlayer`, `kSig_MarkerPattern`, `kSig_MarkerOriginPrefix`,
  `kSig_MarkerProtection` (inert), inline AOB `48 8B 05 ?? ?? ?? ?? 48 8B 98 A8 00 00 00 C4 C1 78 10 04 24`
* **Counts:** `kExpected_MarkerMatches` (5)
* **Offsets:** `kOff_Player_Dest0` (`0x90`), `kOff_Player_Dest1` (`0x1A0`),
  `kOff_MoveOwner_DesiredVel` (`0xC0`), `kOff_MoveOwner_Velocity` (`0xD0`),
  `kOff_MoveOwner_Position` (`0x90`)
* **Inline offsets (map_marker.h):** `uiState + 0xA8`, `destination + 0x20` (`map_marker.h:24-25`)
* **Limits:** `kMarker_CoordLimit`, `kMarker_DestLift`, `kMinPointer`
* **Sites:** `teleport.cpp:507-629`, `:436-468`, `:2144`; `marker_teleport_logic.h:36-39`

### 6.3 Locomotion — Super Run / Super Jump / Free Flight — `game/teleport.cpp`

* **Signatures:** `kSig_MoveUpdate` (position tracking + Super Jump input scaling),
  `kSig_LocoStepper_PE2944` / `kSig_LocoStepper` / `kSig_LocoStepper_Pre201` (Super Run + Free Flight)
* **Offsets:** `kOff_MoveOwner_Position` (`0x90`), `kOff_MoveOwner_Velocity` (`0xD0`),
  `kOff_MoveOwner_DesiredVel` (`0xC0`), `kIdx_MoveOwner_Up` (`1`), `kSuperJump_RiseThreshold` (`1.0f`)
* **Version gate:** `core::MoveComponentOwnerOffsetForRevision` (`0x2C0`/`0x2B8`/`0x298`),
  `core::LocoStepperContractForRevision`
* **Sites:** `teleport.cpp:1839-1842`, `:1903-1933`, `:1215-1226`, `:1330-1409`

### 6.4 Inventory / Add Item — `game/inventory.cpp`

* **Signatures:** `kSig_InvGetItemQty` → `kSig_InvGetItemQty_Legacy`; `kSig_InvGetHolder`;
  `kSig_InvHolderInsert` → `_Legacy` / `kSig_InvHolderInsert201` / `kSig_InvHolderInsert2944`;
  `kSig_InvCommit` / `kSig_InvCommit_Pre201`; `kSig_InvCoreGlobal` / `kSig_InvCoreGlobal_Pre201`;
  `kSig_TrItemValueCtor` (+ `_Pre201` + 5 inline candidates under
  `MayUseLegacyFuzzySignaturesForRevision`); `kSig_InvCommitPlacement` / `…201`;
  `kSig_InvFreePlacements` / `…201`; `kSig_TrItemValueDtor` (empty ⇒ never resolves, `Add Item`
  works without the dtor)
* **String anchors:** `kStr_ItemInfoTable`, `kStr_ItemGroupInfoTable`, `kStr_StringInfoTable`,
  `kStr_InventoryInfoTable`, `kStr_WantedInfoTable`, fallbacks `"categorygroupinfo"`,
  `"categoryinfo"`, `"tribeinfo"`; `kSig_LeaR8Rip`, `kSig_MovR8Rip`
* **Offsets:** `kOff_Global_Mid`, `kOff_Mid_Container`, `kOff_Container_Sub`, `kOff_Sub_Holder`,
  `kOff_InvHolder_Container`, `kOff_InvHolder_Buckets`, `kOff_InvHolder_Count`,
  `kOff_InvBucket_Slots`, `kOff_InvBucket_Count`, `kOff_InvBucket_Type`,
  `kOff_InvBucket_UsedSlots`, `kOff_InvBucket_MaxSlots`, `kOff_InvBucket_DeltaRaw`,
  `kOff_InvBucket_DeltaClamped`, `kOff_InvBucket_ExpandSlots`,
  `kOff_Sub_IdAllocator`, `kOff_IdAlloc_Counter`,
  `kOff_ItemVal_InstanceId`, `kOff_ItemVal_Subtype`,
  `kOff_ItemDef_BucketType` (runtime `0x428`/`0x418`, legacy `66`),
  `kItemVal_Size`, `kPlacement_Stride`, `kOff_Placement_SlotIdx`,
  `kInvSlot_Stride`, `kOff_InvSlot_TypeId`, `kOff_InvSlot_Quantity`, `kInvSlot_EmptyType`
* **Tables:** `kOff_ItemTable_Count`, `kOff_ItemTable_Defs`, `kOff_ItemDef_Key`,
  `kOff_ItemDef_Name`, `kOff_ItemDef_Tier`, `kOff_ItemDef_Groups`, `kOff_ItemDef_Icons`,
  `kOff_ItemDef_MaxStackCount`, `kOff_ItemDef_ApplyMaxStackCap`,
  `kOff_InvDef_Key`, `kOff_InvDef_Name`, `kOff_InvDef_DefSlots`, `kOff_InvDef_MaxSlots`,
  `kOff_GrpDef_Key`, `kOff_GrpDef_Name`, `kOff_GrpDef_Order`, `kOff_GrpDef_Icon`,
  `kOff_StrDef_Buffer`, `kOff_IconData_Path`, `kOff_Vec_Data`, `kOff_Vec_Count`,
  `kGrpOrder_Internal`, `kGrpOrder_MaxTopTab`, `kIconPath_None`,
  `kGrpKey_SubCatPrefix`, `kIconPrefix_ItemGroup`, `kIcon_Uncategorised` (unused constant)
* **Realm:** `kOff_Teb_TlsPointer`, `kTls_RealmFlag` (documentation; runtime uses
  `core::RealmFlagOffsetForRevision`), `core::InventoryCoreGlobalMovOffsetForRevision`
* **Localization:** `kSig_LocStringGet` (+ 3 fallbacks), `kOff_LocGet_MovGlobal`,
  `kOff_LocProv_Offset`, `kOff_LocMgr_Blob`, `kOff_LocBlob_Data`, `kOff_LocBlob_Size`
* **Uniqueness/bounds:** `kMinPointer`, `kCharList_MaxCount` (player list), literal bucket-count bound
  `4096` (`inventory.cpp:2371`) and slot-count bound `8192` (`inventory.cpp:2386`)
* **Hardcoded addresses (fallbacks):** `+0x6350EE8`, `+0x63307A8`, `+0x634DDB8`, `+0x634DDD0`
* **Sites:** `inventory.cpp:2062-2326`, `:1967-2020`, `:1902-1944`, `:3801-4185`

### 6.5 Money — `game/inventory.cpp`

* **Hardcoded hooks (only `revision < 2625`):** `gameBase + 0x16077B0`, `+0x16078C0`, `+0x16081D0`
* **Item-table path (all revisions):** `kStr_ItemInfoTable`, `kSig_LeaR8Rip`,
  `kOff_ItemTable_Count`, `kOff_ItemTable_Defs`, `kOff_ItemDef_Key`,
  `kOff_ItemDef_MaxStackCount`, `kOff_ItemDef_ApplyMaxStackCap`,
  `kOff_InvBucket_*`, `kOff_InvSlot_TypeId`, `kOff_InvSlot_Quantity`
* **Note:** "Money is the exception: its inventory slot is a passive mirror, so editing it does not
  change spendable currency." (`offsets.h:586-587`). The Money_Copper row is only restamped for
  `revision < 2625` (`inventory.cpp:3160-3170`).
* **Sites:** `inventory.cpp:2093-2105`, `:3160-3170`, `:4512-4544`

### 6.6 Player / health / stamina / spirit — `game/player.cpp`

* **Signatures:** `kCharMgrAnchors[]` (all 6), `kSig_StatCommit` (non-TU201),
  `kSig_DamageApply` → `kSig_DamageApply_Alt`
* **Offsets:** `kOff_Owner_Actor`, `kOff_Actor_StatusMarker`, `kOff_Owner_Possessor`,
  `kOff_Possessor_Pawn`, `kOff_Owner_TypeDesc`, `kOff_Owner_ObjectType` (doc),
  `kOff_Marker_TargetOwner`, `kOff_Root_StatArray`,
  `kOff_CharMgr_ListData`, `kOff_CharMgr_ListCount`,
  `kOff_StatEntry_Type`, `kOff_StatEntry_Current`, `kOff_StatEntry_Base`, `kOff_StatEntry_Norm`,
  `kOff_StatEntry_Floor`, `kOff_StatEntry_Cap`, `kSizeof_StatEntry`, `kStatArray_ScanEntries`
* **Type IDs:** `StatType_Health=0`, `StatType_Stamina=17`, `StatType_Spirit=18`,
  `StatType_MountSprint=19`, `StatType_SprintSt=20`, `StatType_SpiritPool=21`,
  `StatType_StaminaPool117=22`, `StatType_SpiritPool117=23`, `StatType_MountAbility=48`
* **Bounds:** `kMinPointer`, `kCharList_MaxCount`
* **Sites:** `player.cpp:42-101`, `:163`, `:189-268`, `:346-454`, `:502-504`, `:815-859`

### 6.7 God Mode — `game/player.cpp`

* **Signature:** `kSig_StatCommit` (`player.cpp:827`) — installed **only when
  `!core::UsesTu201CompatibleRevision(revision)`**
* **Offsets:** `kOff_StatEntry_Current`, `kOff_StatEntry_Norm`, `kOff_StatEntry_Base`,
  `kOff_StatEntry_Cap`
* **TU201 replacement:** "modern continuous stat-pin guard active (all resolved characters)"
  (`player.cpp:821-824`) — uses `kCharMgrAnchors[]` + `kOff_StatEntry_*` only
* **Doc:** `offsets.h:123-144`

### 6.8 Easy Parry / Easy Evade — `game/player.cpp`

* **Signature:** `kSig_CombatTimingEval` (`player.cpp:852`, line 175 in offsets.h)
* Placeholder alternates `kSig_JustCore` / `kSig_JustCore_Alt` exist in `offsets.h:181-184` but have
  **no consumer** — the shipped path is `kSig_CombatTimingEval`, documented as "Perfect Parry (r9b == 1)
  and Perfect Dodge (r9b == 0)" (`offsets.h:172-176`)

### 6.9 One-Hit-Kill / damage multipliers — `game/player.cpp`

* **Signatures:** `kSig_DamageApply` (primary), `kSig_DamageApply_Alt` (fallback)
* **Offsets used to identify both sides:** `kOff_Owner_Actor` (attacker via `sourceCtx + 0x68`),
  `kOff_Marker_TargetOwner` (victim), plus the standard owner chain
* **State:** `State::dmgOutMult`, `State::dmgInMult` (`core/state.h:55-57`)
* **Doc:** `offsets.h:148-170`

### 6.10 Equipment / refine / repair / sockets — `game/equipment.cpp`

* **Signatures:** `kSig_EquipEffectRefresh` → `kSig_EquipEffectRefresh_Legacy`;
  inline AOB `48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00` (`equipment.cpp:1574`, ResizeSocketVector)
* **Offsets:** `kOff_Sub_EquipComp`, `kOff_EquipComp_Owner`, `kOff_EquipComp_Table` (unused),
  `kOff_EquipTable_Array`, `kOff_EquipTable_Count`, `kEquipEntry_Stride` (unused),
  `kOff_EquipEntry_SlotTag` (unused), `kOff_InvSlot_TypeId`, `kOff_InvSlot_Quantity`,
  `kInvSlot_EmptyType`, `kOff_ItemVal_InstanceId`,
  `kOff_ItemVal_SocketData` (unused), `kOff_ItemVal_SocketData_Legacy` (unused),
  `kOff_ItemVal_SocketSize`/`Cap`/`Unlocked` (unused; literals `0x60`/`0x58`/`0x68`/`0x70` used),
  `kSocketRec_Stride`, `kSocket_Max`, `kOff_SockRec_GearId`, `kOff_SockRec_Marker`,
  `kOff_SockRec_Index`, `kOff_SockRec_State`, `kSock_Empty`,
  `kOff_ItemVal_RefineLevel`, `kRefine_Max`, `kOff_ItemVal_Durability`
* **Equip-table probe lists (hardcoded):** `equipment.cpp:137`, `:181-182`, `:196`
* **Note:** `UnlockedCount` uses `isLegacy ? 0x68 : 0x70` (`equipment.cpp:439-440`) — alias of
  `kOff_ItemVal_SocketSize`/`kOff_ItemVal_SocketUnlocked`
* **Sites:** `equipment.cpp:45-204`, `:376-434`, `:451-520`, `:683-728`, `:1224-1228`, `:1564-1582`

### 6.11 Dye — `game/dye.cpp`

* **Signatures:** `kSig_EquipBatch` → `_Legacy`; `kSig_DyeApplyBatch` → `_Legacy`;
  `kSig_DyeUpsert` → `_Legacy`; `kSig_DyeVisualSet`, `kSig_DyeVisualClear`, `kSig_DyeRecordRemove`,
  `kSig_DyeApplySlot` (last four **nulled for `revision >= 2625`**, `dye.cpp:1630-1636`)
* **Offsets:** `kOff_Sub_EquipComp`, `kOff_EquipComp_Owner`, `kOff_EquipComp_Table` (unused),
  `kOff_EquipTable_Array`, `kOff_EquipTable_Count`, `kOff_ItemVal_DyeData` (unused),
  `kOff_ItemVal_DyeCount`, `kDye_MaxChannels`,
  `kOff_InvSlot_TypeId`, `kOff_InvSlot_Quantity`, `kInvSlot_EmptyType`, `kOff_ItemVal_InstanceId`,
  `kOff_InvHolder_Buckets`, `kOff_InvHolder_Count`, `kOff_InvBucket_Slots`, `kOff_InvBucket_Count`,
  `kOff_Container_Sub`, `kOff_Owner_Possessor`, `kOff_Possessor_Pawn`
* **Batch geometry:** `kDyeBatch_Blocks`, `kDyeBatch_BlockSize`, `kDyeBatch_RecordsOff`, `kDyeBatch_Size`
* **Realm:** `core::RealmFlagOffsetForRevision`, `kOff_Teb_TlsPointer`
* **Sites:** `dye.cpp:44-68`, `:1563-1638`; dye color family data lives in `dye_data.h` /
  `dye_slots_table.h` (generated from game data tables, not binary locators)

### 6.12 World: time / weather / game speed / fog / wind — `game/world.cpp`

* **Game Speed:** `kSig_FrameTimerBody` → `kSig_FrameTimerBody_Pre201`;
  `kOff_TimeStruct_Delta` (`0x64`), `kOff_TimeStruct_ScaledDelta` (`0x68`)
* **Time of Day (numeric clock):** `kSig_FieldTimeRealm` (`kOff_FieldTime_ServerVmovups`,
  `kOff_FieldTime_ClientVmovups`, `kLen_FieldTime_Vmovups`),
  `kOff_FieldTime_Day/Hour/Min/Sec`
* **Time of Day (freeze):** `kSig_FieldTimeTick` → `kSig_FieldTimeTick_Pre201`
* **Time of Day (sun, render clamp):** `kSig_TodEngineGlobal` (`kOff_TodEngineGlobal_Mov`,
  `kLen_TodEngineGlobal_Mov`), `kOff_Tod_Manager` (`0x2F8`), `kOff_Tod_CurrentHour` (`0x3D0`),
  `kOff_Tod_LowerLimit` (`0x3D4`), `kOff_Tod_UpperLimit` (`0x3D8`)
* **Weather:** `kSig_WeatherRain`, `kSig_WeatherSnow`, `kSig_WeatherDust`
* **Wind / clouds / fog:** `kSig_WindPack` → `kSig_WindPack_Pre201`;
  `kSig_EnvManager` → `kSig_EnvManager_Legacy` (`kLen_EnvManager_Mov`);
  `CN::FOG_A`, `CN::FOG_B`, `CN::DUST_BASE`, `CN::DUST_ADD`, `CN::DUST_THRESH`,
  `CN::DUST_WIND_SCALE`, `CN::STORM_THRESH`, `CN::CLOUD_TOP`, `CN::CLOUD_THICK`, `CN::CLOUD_BASE`;
  `WN::DIR_X`, `WN::DIR_Z`, `WN::TURB_DENS`, `WN::TURB_SCALE`, `WN::TURB_LIFT`, `WN::SPEED`,
  `WN::GUST`, `WN::CLOUD_SCROLL_X`, `WN::CLOUD_SCROLL_Z`
* **Bounds:** `kMinPointer`
* **Sites:** `world.cpp:445-605`, `:57-69`, `:146-168`, `:632-761`, `:805-888`

### 6.13 Trust / Friendly (NPC gift + pet/mount taming) — `game/friendly.cpp`

* **Signatures:** `kSig_FriendlySetNpc201`, `kSig_FriendlySetPet201` (primary);
  `kSig_FriendlySetNpc`, `kSig_FriendlySetPet` (TU 2.00 fallback — tried only if **both** 201 setters
  fail, `friendly.cpp:239-245`); `kSig_FriendlyGetNpc201`, `kSig_FriendlyGetPet201` (in-place
  mutation observers). Unused: `kSig_FriendlyTrustSiteA/B`, `kOff_FriendlyTrustSiteA/B_Hook`,
  `kOrig_FriendlyTrustBytes`, `kSig_FriendlyNpcTrustWriter`, `kSig_FriendlyAlertDisp`
* **Offsets:** `kOff_FriendlyRec_Key` (`0x00`), `kOff_FriendlyRec_Group` (`0x04`),
  `kOff_FriendlyRec_Value` (`0x20`), `kFriendly_Max` (`100`)
* **Contract:** `friendly.cpp` installs a custom detour rather than `mem::InstallHook`
  (`friendly.cpp:230-235`)
* **Sites:** `friendly.cpp:237-299`; record layout doc `offsets.h:1750-1833`

### 6.14 Worker (level + abilities) — `game/worker.cpp`

* **Signature:** `kSig_WorkerMaxLevelAndSkills` (`worker.cpp:57`), must match **exactly one** site
* **Patch bytes:** `kWorkerPatchSize` (6), `kWorkerPatchOriginal`
  (`0F 85 95 00 00 00`), `kWorkerPatchEnabled` (`E9 96 00 00 00 90`)
* **Version gate:** `WorkerPatchSupportedForRevision(r) → r == 2850 || r == 2944`
  (`game/worker_logic.h:18-21`); `CanTransitionWorkerPatch` (`worker_logic.h:26-45`)
* **Bounds:** `kMinPointer` via `mem::IsValidUserPtr` (`worker.cpp:23-24`)
* **Sites:** `worker.cpp:42-107`, `:142-193`

### 6.15 Crime / bounty / wanted — `game/inventory.cpp`

* **Signatures:** `kSig_EvaluateCrimeWantedState` (all revisions); `kSig_RegisterCrimeEvent`
  (`revision <= 2850` only)
* **String anchor:** `kStr_WantedInfoTable`
* **Offsets:** `kOff_ItemTable_Count`, `kOff_WantedDef_IncreasePrice`, `kOff_WantedDef_IsBlocked`
  (unused), `kWantedRows_Max`
* **Fallback address:** `gameBase + 0x6350EE8` for `revision >= 2625` (`inventory.cpp:1952`)
* **Contract:** `RegisterCrimeEvent_t` (`crime_hook_contract.h:8-11`)
* **Sites:** `inventory.cpp:2066-2083`, `:5703-5744`

### 6.16 Pet / mount — spread across subsystems

* **Trust/taming:** `kSig_FriendlySetPet201` / `kSig_FriendlyGetPet201` /
  `kSig_FriendlySetPet`; `kOff_FriendlyRec_Value`; "pet/vehicle uses +0x18"
  (`offsets.h:1795-1796`)
* **Dye on mounts/vehicles:** `kSig_DyeApplySlot`,
  "Works universally for ALL equipped components (Player characters AND Mounts/Vehicles)"
  (`offsets.h:1548-1549`); fallbacks `kSig_DyeVisualSet` / `kSig_DyeVisualClear`
* **Equip component for mounts:** `kOff_Sub_EquipComp`, `kOff_EquipComp_Owner`,
  `kOff_EquipTable_Array`, `kOff_EquipTable_Count`; mount-gear slot tags 14..25
  (`equipment.cpp:1551-1558`)
* **Mount stamina / ability:** `StatType_MountSprint = 19`, `StatType_MountAbility = 48`,
  `kOff_Root_StatArray`, `kSizeof_StatEntry`, `kStatArray_ScanEntries`
* **ObjectType:** `Obj_Vehicle = 5`, `Obj_Pet = 6` (documentation only)
* **Sites:** `friendly.cpp:237-299`, `dye.cpp:1547-1611`, `equipment.cpp:1551-1558`,
  `player.cpp` (mount stamina gates)

---

## 7. Counts and open uncertainties

### 7.1 Counts

| Group | Count |
|---|---|
| `kSig_*` signature constants in `offsets.h` | **83** |
| `kCharMgrAnchors[]` signature entries | **6** |
| Signal/string locator rows in §3.1 (+§3.1.1) | **89** |
| `kStr_*` string-anchor constants | **7** |
| Other named string anchors (`kGrpKey_SubCatPrefix`, `kIconPrefix_ItemGroup`, `kIcon_Uncategorised`) | **3** |
| **Total string anchors in `offsets.h`** | **10** |
| Inline string anchors in `.cpp` (`regioninfo`, `fieldinfo`, `categorygroupinfo`, `categoryinfo`, `tribeinfo`) | **5** |
| **Total signature + string locators (named, in `offsets.h`)** | **99** |
| `kOff_*` structure-offset constants | **132** |
| Other numeric constants (strides, counts, sentinels, flags, patch bytes) | **45** |
| **Total `constexpr` constants in `offsets.h`** | **267** |
| Hardcoded module-relative addresses outside `offsets.h` | **7** |
| Inline AOBs outside `offsets.h` | **2** (equipment `ResizeSocketVector`, teleport map-destination) + **5** legacy fuzzy ctor candidates |
| Disabled/inert locators | **4 categories** (§3.7) |

### 7.2 Uncertain / flagged items

1. **`src/mem/scanner.h:29-31` is stale.** It claims a `VirtualQuery`-based page walk; the
   implementation (`scanner.cpp:93-120`) is PE-section based via `ShouldScanSection`.
2. **`src/core/version_detect.cpp:54-56` vs `:58-63` contradict each other** on whether the PE
   resource version moves across Steam updates. The code follows `:58-63`.
3. **PE `1.0.0.2474` (TU 1.18.02)** is named in a comment but has no `case` in
   `ModernTitleUpdateForRevision` — it falls into the `nullptr` / `"Crimson Desert 1.18.02 (Active)"`
   fallback while simultaneously being the value the fallback string claims. Behaviour is
   self-consistent but fragile.
4. **`GameTU`-keyed accessors in `version_detect.cpp` are dead code**: `DetectVersion()` only ever sets
   `TU_1_18_01_Plus`, so `GetPlacementStride` (`216`), `GetPlacementSlotIdxOffset` (`208`),
   `GetSlotStride` (`0xC0`), `GetItemValSocketOffset` (`0x58`), `GetItemValWorkingSize` (`0xC0`) legacy
   branches can never execute. Only `0xE0`/`0xD8`/`0xC8`/`0x60`/`0x108` (and `0x418`/`0x428` for
   BucketType) are reachable.
5. **`kOff_Node_Gimmick` (`0x10`) is explicitly marked UNVERIFIED on PE 2944** by the source
   (`offsets.h:492`), and the code still does `ReadPtr(node + kOff_Node_Gimmick, &g)` at
   `teleport.cpp:1167` even though the same comment says the live value is a `u16`, not a pointer.
6. **`kCharMgrAnchors` comment says "all four"** (`offsets.h:227`) while the array has six entries.
7. **`kExpected_OriginMatches = 9` is unused**; `teleport.cpp:549` uses a literal `6` minimum and
   `teleport.cpp:526` allocates `32` matches. The two disagree with the constant.
8. **`kSig_TrItemValueDtor = ""`** — an empty pattern. `FindPattern` returns 0 for it
   (`scanner.cpp:156-157`); this is a silent no-op, not an error.
9. **`kOff_TableResolver_MovGlobal = 0x14` vs `kOff_LocoStepper_Pre201`** — for 16-bit-key resolvers
   `teleport.cpp:1805` selects `kOff_ItemResolver_MovGlobal = 0x15`, but the comment at
   `offsets.h:1017-1019` says the mov-global sits at `+0x15` "differs from
   `kSig_TableResolverPrologue` only in loading a 16-bit key … the mov-global sits at +0x15", which is
   consistent; `teleport.cpp:1814-1832` also has a forward-scan fallback that makes the exact value
   non-load-bearing.
10. **`kOff_SceneDesc_LevelName` (`0x18`), `kOff_SceneDesc_IsEmpty` (`0x6C`),
    `kOff_Registry_SceneTable` (`0x50`), `kOff_ItemTable_Defs` (`0x58`),
    `kOff_Placement_SlotIdx` (`0xD8`), `kPlacement_Stride` (`0xE0`), `kInvSlot_Stride` (`0xC8`),
    `kEquipEntry_Stride` (`0xD0`), `kOff_EquipEntry_SlotTag` (`0xC8`), `kOff_EquipComp_Table` (`0x80`),
    `kOff_ItemVal_DyeData` (`0x78`), `kOff_ItemVal_Socket*`, `kOff_StatEntry_Floor`,
    `kOff_WantedDef_IsBlocked`, `kOff_Owner_ObjectType`, `kOff_FieldTime_Min/Sec`,
    `kOff_EnvManager_Mov`, `kExpected_OriginMatches`, `kIcon_Uncategorised`,
    `kSig_JustCore`, `kSig_JustCore_Alt`, `kSig_FriendlyTrustSiteA/B`,
    `kOff_FriendlyTrustSiteA/B_Hook`, `kOrig_FriendlyTrustBytes`,
    `kSig_FriendlyNpcTrustWriter`, `kSig_FriendlyAlertDisp`** have **no consumer** in `src/`. They are
    documentation / retired locators. Several are functionally replaced by hardcoded literals in the
    consumer (noted inline in §3.3).
11. **Bounty path on PE 2944**: `No Bounty` still works via `kSig_EvaluateCrimeWantedState` +
    `kStr_WantedInfoTable`, but crime-banner / minimap-circle / guard-dispatch suppression is
    unavailable (`inventory.cpp:2078-2082`).
12. **Marker protection is fully disabled** — `InstallMarkerProtectionHook` unconditionally returns
    `false` (`teleport.cpp:461-468`), so `kSig_MarkerProtection` is resolved and counted but never used
    to patch anything.
