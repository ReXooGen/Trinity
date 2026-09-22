# Travel / Native Fast Travel — feature dossier

Trinity scope: the world-map **native fast travel** path (map marker, saved
locations, destination selection) — the travel that streams properly instead of
desyncing the Havok body, as opposed to direct position writes.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, Binary Ninja database
`E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe.bndb`
(view `view_1`, PE). See `../snapshots/PE-2944.md`.

Analysis method: static only, through Binary Ninja's WARP MCP endpoint
(`binaryninja_ui_mcp`), view `view_1`. No breakpoints, no debugger, no
injection, no memory writes, no game launch.

---

## 1. Confirmed call flow

```text
world map UI  sub_140DBAC20 @ 0x140DBAC20  (candidate: not renamed, 6802 bytes)
  reads the current pin: [rsi+0x5C0] -> +0xC8 (xmm: low dword sceneId, high dword nodeIndex)
                                       -> +0xD0 (kind byte, compared against 0xE)
  0x140DBC374  lea  rcx, [rbp+0x1320]        ; &local sceneId copy
  0x140DBC37B  call VIBE_LevelGimmickSceneObjectInfoTable_GetById
  0x140DBC380  cmp  ebx, dword [rax+0x28]    ; nodeIndex < nodeCount ?
  0x140DBC383  jae  reject
  0x140DBC385  mov  r8d, ebx                 ; nodeIndex
  0x140DBC388  mov  edx, edi                 ; sceneId
  0x140DBC38A  call VIBE_FastTravel_SelectDestination        <-- selection gate
  0x140DBC38F  test al, al ; je reject
  0x140DBC393  mov  dword [rsi+0x960], edi   ; stage pending sceneId
               mov  dword [rsi+0x964], ebx   ; stage pending nodeIndex
        |
        v
VIBE_FastTravel_SelectDestination @ 0x140654ED0   RCX=int32* sceneId, EDX=sceneId, R8D=nodeIndex -> AL
  -> VIBE_ScopeAttacher_ClientActor_InitGlobal, sub_14064F4D0 (field attacher)
  -> VIBE_LevelGimmickSceneObjectInfoTable_GetById(&sceneId)
  -> sub_1415DF310(nodeArray + nodeIndex*0xD8, &nodeEntry)   ; copies one 0xD8-byte node
  -> sub_1417EDAC0(...) -> AL
        |
        v
VIBE_FastTravel_FlushPendingRequest @ 0x140DBC6C0   (this, key, cancel)
  fields: +0x958 key, +0x960 sceneId, +0x964 nodeIndex, +0x968 mercenaryHandle
  0x140DBC7BA  xor  r9d, r9d
  0x140DBC7BD  call VIBE_FastTravel_TryAcceptRequest   ; scene path  (mode 0)
  0x140DBC7E8  call VIBE_FastTravel_TryAcceptRequest   ; mercenary path (sceneId=-1, nodeIndex=-1, R9D=handle)
        |
        v
VIBE_FastTravel_TryAcceptRequest @ 0x1406550B0
  RCX ignored (global ClientActor) | EDX=sceneId | R8D=nodeIndex | R9D=0 ordinary | return AL
  scene path: reject sceneId==-1; record=GetById(&sceneId); reject nodeCount(+0x28) <= nodeIndex;
              attach scopes; resolve travel target id; copy node entry;
  0x140655566 call VIBE_FastTravel_Execute
        |
        v
VIBE_FastTravel_Execute @ 0x140A9A8D0   ; performs the actual travel

third caller of the dispatcher: sub_140D9E6C0 @ 0x140D9E858  (not analysed this pass)
```

The dispatcher has exactly **three** callers; the selection gate has exactly
**one**.

---

## 2. Function dossiers

### VIBE_FastTravel_TryAcceptRequest  *(pre-existing name, kept)*

- Feature: Native Fast Travel — final post-confirmation dispatcher
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x6550B0` / VA `0x1406550B0` / `.code` / 1243 bytes / 43 basic blocks
- Locator: `kSig_TravelDispatcher_PE2944` (44 bytes, `src/game/offsets.h`)
  `48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 41 56 41 57 48 8D AC 24 50 FE FF FF 48 81 EC B0 02 00 00 41 8B F9 45 8B F8 33 DB`
  → **exactly 1 match in the whole image**, at this VA, section `.code`.
  The 44 bytes were read back from the database and compared byte-for-byte with
  the shipping signature: identical. Live hook state: not hooked (called).
- Confidence: **confirmed**
- ABI: `RCX` = ignored (spilled to `[rsp+8]`, never dereferenced here);
  `RDX` = `int32 sceneId` (`-1` = "no scene"); `R8D` = `int32 nodeIndex`;
  `R9D` = 4th argument, `0` selects the ordinary fast-travel path, non-zero
  selects the mercenary path; `stack`/`XMM` = none; `return` = `AL`
  (1 = accepted).
  Prologue proof:
  `0x1406550B5 mov dword [rsp+0x10], edx` ·
  `0x1406550B9 mov qword [rsp+0x8], rcx` ·
  `0x1406550D4 mov edi, r9d` ·
  `0x1406550D7 mov r15d, r8d` ·
  `0x1406550DA xor ebx, ebx` + `test r9d, r9d` ·
  return value built in `BL` (`0x140655344 mov bl,1` / `0x140655348 xor bl,bl`).
- Data model: scene record from `VIBE_LevelGimmickSceneObjectInfoTable_GetById`;
  `record+0x28` `u32` nodeCount; `record+0x20` `ptr` nodeArray; node stride
  `0xD8`; `sub_1415DF310` copies one node entry.
  *(Inherited from the pre-existing Binary Ninja comment, not re-derived this
  pass: the travel-target id map `data_146D6AA40` with bucketCount `+0x68`,
  buckets `+0x78`, nodes `+0x80`; the id source `data_146CE5B28` /
  `sub_14E877080`; `sub_140C04D80`, `sub_1407782C0`, `sub_140778A50` on the
  mercenary path.)*
- Call flow: `VIBE_FastTravel_FlushPendingRequest` `0x140DBC7BD` (scene path,
  `R9D` explicitly zeroed) and `0x140DBC7E8` (mercenary path);
  `sub_140D9E6C0` `0x140D9E858` (unanalysed) → **this** →
  `VIBE_FastTravel_Execute` `0x140A9A8D0` (at `0x140655566`), plus
  `VIBE_ScopeAttacher_ClientActor_InitGlobal`, `VIBE_Actor_GetFieldAttacher`,
  `VIBE_LevelGimmickSceneObjectInfoTable_GetById`, `VIBE_FieldInfoTable_GetById`,
  `VIBE_CharacterInfoTable_GetRecordById`.
- Trinity consumer: `src/game/teleport.cpp` → `Teleport::Install()` binds
  `g_travelFn = reinterpret_cast<TravelFn>(dispatcher)`;
  `Teleport::Tick()` fires the queued call on the game thread;
  `src/game/travel_logic.h` → `NativeTravelCall` / `PrepareNativeTravelCall()`
  build `context = nullptr, sceneId, nodeIndex, travelMode = 0`.
  Role: **native call** (not a hook, not a patch).
- Static proof: prologue disassembly, pseudo-C decompilation, all three callers,
  and a unique 44-byte AOB over all 12 sections.
- Live proof: **none in this pass.** The intended runtime check is Trinity's own
  `teleport: native fast-travel accepted/refused scene=… node=…` log line.
- OFF/restore or fail-safe result: no bytes are patched, so there is nothing to
  restore. Trinity only calls it, inside `__try/__except`, and logs
  `accepted`/`refused`/`raised an exception`. If the locator fails, `g_travelFn`
  stays null and `Teleport::Install()` logs
  `PE 2944 native Fast Travel contract incomplete … - menu disabled`
  (fail-closed).
- Update notes: the 44-byte prologue, the `EDX`/`R8D`/`R9D` argument order, the
  `test r9d,r9d` path split, and the `record+0x28` bounds check are all
  revision-specific. Re-find with the AOB, then re-read the prologue before
  re-enabling.
- Open questions: see §6.

### VIBE_FastTravel_SelectDestination  *(renamed this pass from `sub_140654ed0`)*

- Feature: Native Fast Travel — world-map destination selection gate
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x654ED0` / VA `0x140654ED0` / `.code` / 470 bytes / 22 basic blocks
- Locator: `kSig_TravelToNode_PE2944` (31 bytes, `src/game/offsets.h`)
  `89 54 24 10 48 89 4C 24 08 53 55 56 57 41 54 41 56 41 57 48 81 EC 90 00 00 00 41 8B D8 33 FF`
  → **exactly 1 match in the whole image**, at this VA, section `.code`.
  Documentation nit: the `offsets.h` prose calls this a "32-byte prologue"; the
  string is 31 byte tokens. The match count is unaffected.
- Confidence: **strong candidate** — the role and ABI are supported by two
  independent sources, but one inherited claim is unproven (see Open questions).
- ABI: `RCX` = `int32* sceneId` — a pointer to the **caller's local copy**
  (`0x140DBC374 lea rcx,[rbp+0x1320]`), dereferenced by the table resolver, so it
  must stay valid for the synchronous call and must **never** be null;
  `RDX` = `int32 sceneId` by value; `R8D` = `int32 nodeIndex`; `R9` unused;
  `return` = `AL` (1 = accepted).
  Caller proof: `0x140DBC385 mov r8d, ebx` · `0x140DBC388 mov edx, edi` ·
  `0x140DBC38A call` · `0x140DBC38F test al, al`.
- Data model: same registry contract as the dispatcher —
  `record+0x28` nodeCount (the caller bounds-checks `nodeIndex < nodeCount`),
  `record+0x20` nodeArray, node stride `0xD8`.
  Map-UI selection source: `[mapUI+0x5C0]` → `+0xC8` (xmm; low dword sceneId,
  high dword nodeIndex) and `+0xD0` (kind byte compared against `0xE`).
- Call flow: single caller `sub_140DBAC20` `0x140DBC38A` (world-map UI dispatch)
  → **this** → `VIBE_ScopeAttacher_ClientActor_InitGlobal` `0x14064F490`,
  `sub_14064F4D0` (field attacher), `VIBE_LevelGimmickSceneObjectInfoTable_GetById`
  `0x1404A2630`, `sub_1415DF310` (node-entry copy), `sub_1417EDAC0`
  (888 bytes, unidentified).
- Trinity consumer: `src/game/teleport.cpp` → `Teleport::Install()`, PE 2944
  branch: `mem::FindPattern(kSig_TravelToNode_PE2944)`. It is used **only as a
  presence/contract check** — the feature is enabled only when this locator
  *and* the dispatcher locator both match. `g_travelFn` is bound to the
  dispatcher, **not** to this function. Role: **locator only, never called.**
- Static proof: decompilation; the caller's register setup at
  `0x140DBC385`–`0x140DBC38A`; the shared registry/node contract with the
  confirmed dispatcher; the shared pending-request staging into `+0x960`/`+0x964`
  that `VIBE_FastTravel_FlushPendingRequest` later consumes.
- Live proof: none in this pass.
- OFF/restore or fail-safe result: not called, nothing patched. If this locator
  returns 0 on a future PE, `Teleport::Install()` disables the whole PE 2944
  travel menu (fail-closed) even when the dispatcher still matches.
  **Hazard:** because `RCX` is dereferenced, any future attempt to call this
  function directly with `nullptr` (the way the dispatcher is called) would
  fault. Do not substitute it for `g_travelFn`.
- Update notes: re-find with the 31-byte AOB; re-verify the `lea rcx` /
  `mov edx` / `mov r8d` caller setup before trusting the ABI.
- Open questions: see §6.

### VIBE_FastTravel_FlushPendingRequest  *(pre-existing name, kept)*

- Feature: Native Fast Travel — the game's own queued-request consumer
  (the in-game equivalent of Trinity's queued dispatch)
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0xDBC6C0` / VA `0x140DBC6C0` / `.code` / 347 bytes / 15 basic blocks
- Locator: 48-byte prologue AOB
  `40 53 48 83 EC 20 48 8B D9 48 39 91 58 09 00 00 0F 85 3F 01 00 00 48 89 7C 24 38 48 C7 81 58 09 00 00 00 00 00 00 83 B9 60 09 00 00 FF 48 89 74`
  → **exactly 1 match in the whole image**, at this VA, section `.code`.
- Confidence: **confirmed** (locator + disassembly)
- ABI: `RCX` = `this` (the object carrying the pending request); `RDX` = key that
  must equal `[this+0x958]` or the function returns immediately; `R8B` = cancel
  flag; `return` = void.
- Data model: `this+0x958` pending key, `this+0x960` pending sceneId,
  `this+0x964` pending nodeIndex, `this+0x968` pending mercenary handle.
- Call flow: → **this** → `VIBE_FastTravel_TryAcceptRequest` at two sites:
  `0x140DBC7BD` scene path (`0x140DBC7BA xor r9d,r9d`, then sceneId in `EDX`,
  nodeIndex in `R8D`, mode `0`) and `0x140DBC7E8` mercenary path
  (`sceneId = -1`, `nodeIndex = -1`, `R9D` = handle).
- Trinity consumer: none directly — **not hooked and not called**. Recorded
  because it is the strongest corroboration of the dispatcher ABI and because it
  proves the game's own ordinary path passes mode `0`, exactly as
  `PrepareNativeTravelCall()` does.
- Static proof: decompilation + the scene/mercenary call-site disassembly above.
- Live proof: none in this pass.
- OFF/restore or fail-safe result: n/a (never touched by Trinity).
- Update notes: the four field offsets and the two call sites are
  revision-specific; re-read them if the dispatcher ABI is re-derived.
- Open questions: none material.

### VIBE_FastTravel_Execute  *(pre-existing name, kept)*

- Feature: Native Fast Travel — the travel performer (end of the chain)
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0xA9A8D0` / VA `0x140A9A8D0` / `.code` / 584 bytes / 21 basic blocks
- Locator: 48-byte prologue AOB
  `44 89 4C 24 20 44 89 44 24 18 89 54 24 10 55 53 56 57 41 56 48 8D AC 24 10 FF FF FF 48 81 EC F0 01 00 00 48 8B F1 48 8D 54 24 48 48 8B 0D FE E8`
  → **exactly 1 match in the whole image**, at this VA, section `.code`.
- Confidence: **confirmed** (locator + existing decompilation)
- ABI: `RCX` = actor manager; `EDX` = `int32` target id; `R8D` = `int32` field id;
  `R9D` = `int32` flags; 5th argument on the stack = `int64` node entry pointer.
  *(From the pre-existing Binary Ninja comment; not re-derived this pass.)*
- Data model: the `0xD8`-byte node entry copied by the caller; a travel
  descriptor optionally filled from the actor at `+0x60`.
- Call flow: `VIBE_FastTravel_TryAcceptRequest` `0x140655566` → **this**.
- Trinity consumer: none directly — reachable only through the dispatcher.
  Recorded so a future PE can be re-anchored from either end of the chain.
- Static proof: unique 48-byte locator + existing decompilation.
- Live proof: none in this pass.
- OFF/restore or fail-safe result: never touched by Trinity.
- Update notes: if the dispatcher is re-found but this is not, the chain is
  broken — treat as feature-unavailable rather than assuming the old entry.
- Open questions: stack argument layout not re-verified this pass.

### VIBE_LevelGimmickSceneObjectInfoTable_GetById  *(pre-existing name, kept)*

- Feature: Native Fast Travel — the scene/destination registry resolver
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x4A2630` / VA `0x1404A2630` / `.code` / 294 bytes / 13 basic blocks
- Locator: 48-byte prologue AOB
  `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 50 8B 39 48 8B 1D DD BD 8C 06 3B 7B 08 0F 83 E2 00 00 00 48 8D 34 FD 00 00 00 00 48 8B 43 58`
  → **exactly 1 match in the whole image**, at this VA, section `.code`.
  **Weak locator:** it contains the RIP-relative displacement
  `48 8B 1D DD BD 8C 06`, which moves on any PE relayout. Prefer the predicate.
- Locator (predicate, preferred): Trinity's `ResolveTableResolver()` start table
  name string `LevelGimmickSceneObjectInfo` (`kStr_GimmickSceneTable`), then walk
  to the enclosing resolver. **The bare string is NOT unique: 69 matches in this
  image** (`.srdata` and `.sbss`). Uniqueness therefore comes from the
  string + the resolver-prologue walk (`kSig_LeaR8Rip` + `FindResolverPrologueAbove`)
  in `src/game/teleport.cpp`, not from the string alone. Do not "simplify" the
  locator to a string search.
- Confidence: **confirmed** (existing name kept; locator evidence measured this pass)
- ABI: `RCX` = `int32* sceneId` (dereferenced); `return` = scene record pointer.
- Data model: `record+0x08` stringKey, `record+0x10` isBlocked,
  `record+0x18` levelName, `record+0x20` nodeArray, `record+0x28` nodeCount,
  `record+0x49` useTeleport, `record+0x6C` isEmpty
  (`src/game/offsets.h`).
- Call flow: called by both `VIBE_FastTravel_SelectDestination` and
  `VIBE_FastTravel_TryAcceptRequest`, and by the world-map UI itself
  (`0x140DBC37B`).
- Trinity consumer: `src/game/teleport.cpp` → `ResolveTableResolver(kStr_GimmickSceneTable, …)`
  resolves this function into `g_sceneResolver` and the registry global into
  `g_registryGlobal`; the fast-travel menu uses it to enumerate destinations and
  fill the map UI. Role: **native call** on the game thread (the resolver
  lazy-loads its data row, so it must not be called off the game thread).
- Static proof: unique 48-byte AOB; measured non-uniqueness (69) of the bare
  string anchor.
- Live proof: inherited note in `src/game/offsets.h` that the registry tables
  read zero in a static dump and must be populated live.
- OFF/restore or fail-safe result: if the resolver is not found, Trinity logs
  `teleport: scene-registry resolver NOT FOUND - fast-travel menu disabled.`
- Update notes: re-measure the string match count on every PE; if the resolver
  prologue walk changes, the feature must go fail-closed.
- Open questions: none material.

### sub_140DBAC20  *(candidate — deliberately NOT renamed)*

- Feature: Native Fast Travel — world-map UI dispatch (travel callsite only)
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0xDBAC20` / VA `0x140DBAC20` / `.code` / 6802 bytes / 342 basic blocks
- Locator: 48-byte prologue AOB
  `48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 20 ED FF FF B8 E0 13 00 00 E8 1E B6 AD 03 48 2B E0`
  → **exactly 1 match in the whole image**, at this VA, section `.code`.
- Confidence: **candidate** — only the travel callsite is understood.
- ABI: not characterised (wide signature with XMM arguments; unrelated to the
  travel contract).
- Data model: travel-relevant source is `[rsi+0x5C0]` → `+0xC8` / `+0xD0`;
  staged pending request at `[rsi+0x960]` / `[rsi+0x964]`.
- Call flow: has **no direct callers** (invoked indirectly); → **this** →
  `VIBE_FastTravel_SelectDestination` `0x140DBC38A`.
- Trinity consumer: none — not called and not hooked. Recorded because it is the
  UI anchor that fixes the selection gate's ABI.
- Static proof: the travel callsite disassembly (`0x140DBC348`–`0x140DBC393`).
- Why it stays `sub_*`: the function also drives unrelated UI (it references the
  string `SkillTreePanel` at `0x140DBC437`), so any single `VIBE_` feature name
  would be wrong.
- Open questions: see §6.

---

## 3. Locator evidence (measured over the whole image)

Method: `_analysis/aob_scan.ps1` reproduces Trinity's scanner
(`src/mem/scanner.cpp` + `src/mem/section_filter.cpp`) against the on-disk file
image — PE section walk, `[VA, VA+VirtualSize)`, skipping only a section named
exactly `.debug` without `IMAGE_SCN_MEM_EXECUTE`. All **12** sections of this
build are scanned.

| Function | Locator | Length | Matches | Result |
|---|---|---|---|---|
| `VIBE_FastTravel_TryAcceptRequest` | `kSig_TravelDispatcher_PE2944` | 44 B | **1** @ `0x1406550B0` | exact unique |
| `VIBE_FastTravel_SelectDestination` | `kSig_TravelToNode_PE2944` | 31 B | **1** @ `0x140654ED0` | exact unique |
| `VIBE_FastTravel_FlushPendingRequest` | documented prologue AOB | 48 B | **1** @ `0x140DBC6C0` | exact unique |
| `VIBE_FastTravel_Execute` | documented prologue AOB | 48 B | **1** @ `0x140A9A8D0` | exact unique |
| `sub_140DBAC20` | documented prologue AOB | 48 B | **1** @ `0x140DBAC20` | exact unique |
| `VIBE_LevelGimmickSceneObjectInfoTable_GetById` | documented prologue AOB | 48 B | **1** @ `0x1404A2630` | exact unique but RIP-relative → fragile |
| registry table-name anchor | `LevelGimmickSceneObjectInfo` | 27 B | **69** | **ambiguous** — needs the resolver walk |
| area-name table anchor | `FieldLevelNameTableInfo` | 23 B | **25** | **ambiguous** — needs the resolver walk |

Notes:
- The two shipping `offsets.h` locators were verified byte-for-byte against the
  bytes read from the database at their matched addresses, and both matched.
- Two of the prologue AOBs above are **new records created in this pass**; they
  are unique on PE 2944 but, unlike the two shipping locators, they are not yet
  wired into any Trinity source or test.
- The registry anchor being ambiguous (69 matches) is a correction to the
  `offsets.h` prose, which describes it as "its unique table-name string". The
  string is unique as a *table name*, not as a byte pattern; the resolver walk is
  what makes the locator usable.

---

## 4. Trinity source consumers

| Source | Symbol | Role |
|---|---|---|
| `src/game/offsets.h` | `kSig_TravelToNode_PE2944` | selection-gate locator (presence check) |
| `src/game/offsets.h` | `kSig_TravelDispatcher_PE2944` | dispatcher locator (call target) |
| `src/game/offsets.h` | `kStr_GimmickSceneTable`, `kOff_SceneDesc_*`, `kNode_Stride` | registry/destination data model |
| `src/game/offsets.h` | `kSig_TravelToNode`, `…_Pre201`, `…_Legacy` | older-build locators — **must not** be used as a PE 2944 fallback |
| `src/game/teleport.cpp` | `Teleport::Install()` (PE 2944 branch) | resolves both locators, requires both, binds `g_travelFn` = dispatcher |
| `src/game/teleport.cpp` | `Teleport::Tick()` | fires the queued travel on the game thread, SEH-guarded, logs accept/refuse |
| `src/game/travel_logic.h` | `NativeTravelCall`, `PrepareNativeTravelCall()` | builds `context=nullptr, sceneId, nodeIndex, travelMode=0` |
| `src/game/teleport.cpp` | `ResolveTableResolver()` | string + resolver-prologue walk that finds the scene registry |
| `src/mem/scanner.cpp`, `src/mem/section_filter.cpp` | `FindPattern`, `ShouldScanSection` | locator semantics used for every uniqueness result above |

Related but **separate** feature (not covered here): the map-marker teleport
path — `src/game/marker_teleport_logic.h`, `src/game/map_marker.h` — which writes
a destination into the move owner rather than using native fast travel.

---

## 5. Fail-safe behaviour

The native fast travel feature has **no patch bytes and no OFF restoration**: it
is a call, not a modification. Fail-safe is therefore entirely "do not call":

- Both locators must be non-zero on PE 2944, otherwise `travel` stays `0` and the
  menu is disabled with an explicit `LOG_ERR` (`contract incomplete … menu
  disabled`).
- The call is wrapped in `__try/__except`; an exception is logged and the queued
  request is dropped rather than retried.
- `NodeIndex` is validated by the game itself (`nodeIndex < record+0x28`); an
  out-of-range request returns `AL = 0` and is logged as `refused`.
- Non-2944 revisions take their own recorded contract; PE 2944 deliberately does
  **not** fall back to `kSig_TravelToNode`.

---

## 6. Open questions (require later runtime validation)

1. **Does the selection gate really open `CommonModalMessage`?** The inherited
   Trinity comment says the selection gate "only opens CommonModalMessage", but
   its decisive callee `sub_1417EDAC0` (888 bytes, 31 basic blocks) was not
   identified in this pass. The static evidence only shows that it returns a
   boolean that becomes `AL`. Treat the modal claim as **unproven** until a
   visible UI observation confirms it.
2. **Third dispatcher caller `sub_140D9E6C0` @ `0x140D9E858`** was not analysed.
   Its semantics (likely a mercenary/companion travel UI) are unknown.
3. **`R9D` naming.** Binary Ninja's prototype calls it `mercenaryHandle`;
   `src/game/offsets.h` and `travel_logic.h` call it `travelMode`. Both agree
   that `0` is the ordinary path, and `xor r9d,r9d` at `0x140DBC7BA` proves the
   game passes `0` there. The non-zero semantics (a handle to a mercenary actor)
   come from the pre-existing analysis and were **not** re-derived in this pass.
4. **`sub_1417EDAC0`'s arguments** at the selection-gate call site are only
   partially typed in the decompiler; its first parameter shows as an
   uninitialised-looking local.
5. **Live acceptance not re-verified.** No in-game travel was performed in this
   session; the `accepted`/`refused` log path is the intended check.
6. **Inherited-only details** (not re-derived here): the travel-target id map
   `data_146D6AA40`, the id source `data_146CE5B28` / `sub_14E877080`,
   `sub_140C04D80`, `sub_1407782C0` / `sub_140778A50`, and the
   `VIBE_FastTravel_Execute` stack argument layout.
7. **`sub_140DBAC20`'s ABI** is uncharacterised; only its travel callsite is
   documented.

---

## 7. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. Re-run `kSig_TravelDispatcher_PE2944` (expect exact unique). Verify the first
   44 bytes match, then re-read the prologue: `EDX`=sceneId spill, `RCX` spill
   only, `mov edi, r9d`, `mov r15d, r8d`, `test r9d, r9d`.
3. Re-run `kSig_TravelToNode_PE2944` (expect exact unique), then re-read the
   caller setup `lea rcx, [rbp+…]` / `mov edx, …` / `mov r8d, …` / `test al, al`.
4. Re-check the registry contract in both functions: `record+0x28` nodeCount,
   `record+0x20` nodeArray, node stride `0xD8`.
5. Re-measure the `LevelGimmickSceneObjectInfo` string count; if it is no longer
   resolvable through the `ResolveTableResolver()` walk, disable the menu.
6. If either shipping locator is absent or ambiguous: leave `travel = 0`
   (menu disabled), record the mismatch here, and **do not** enable the legacy
   `kSig_TravelToNode` path on the new revision.
7. Re-verify `nullptr` safety of `RCX`: confirm the only `RCX` consumer still
   ignores its first parameter before allowing the call with `context = nullptr`.
8. Only after that, run the in-game test: open the map, pick a destination,
   confirm, and check Trinity's `accepted` / `refused` log line and the
   resulting travel.

---

## 8. Addendum — independent re-verification pass (2026-09-20, session 2)

This section records a **second, independent** static pass over the same PE 2944
database. It re-measured every locator in §3 and added the marker-capture
signature census that §1 did not cover. Nothing in §1–§7 was contradicted.

### 8.1 Every travel locator re-measured

All five documented travel locators still resolve **exactly once** over all 12
PE sections, at the same addresses:

| Locator | Length | Matches | VA |
|---|---|---|---|
| `kSig_TravelDispatcher_PE2944` | 44 B | **1** | `0x1406550B0` |
| `kSig_TravelToNode_PE2944` | 31 B | **1** | `0x140654ED0` |
| `VIBE_FastTravel_FlushPendingRequest` prologue | 48 B | **1** | `0x140DBC6C0` |
| `VIBE_FastTravel_Execute` prologue | 48 B | **1** | `0x140A9A8D0` |
| `sub_140DBAC20` prologue | 48 B | **1** | `0x140DBAC20` |

`VIBE_LevelGimmickSceneObjectInfoTable_GetById` (`0x1404A2630`) also still
resolves uniquely on its 48-byte prologue, and its **preferred predicate
locator remains ambiguous** — this pass measured the `LevelGimmickSceneObjectInfo`
table-name anchor again and it is **not** unique. §3's warning stands: the
string alone is not a locator; the `ResolveTableResolver()` walk is what makes
it usable.

### 8.2 Marker-capture signature census — **three findings that change behaviour**

The map-marker path (§1 "related but separate feature") had never had its
signature counts measured. Doing so explains why map-marker teleport works on
PE 2944 despite a missing hook:

| Locator | Length | Matches | Trinity's expectation | Verdict |
|---|---|---|---|---|
| `kSig_MarkerPattern` | 14 tok | **5** | `kExpected_MarkerMatches = 5` (`offsets.h:561`) | **agrees** — no warning fires |
| `kSig_MarkerOriginPrefix` | 4 tok | **26** | code requires `>= 6` (`teleport.cpp:549`) | **passes** |
| `kExpected_OriginMatches = 9` (`offsets.h:565`) | — | — | **unused constant** | **constant is dead and wrong** — delete or correct it |
| `kSig_MarkerPlayer` | 11 tok | **0** | code installs only `if (players.size() == 1)` (`teleport.cpp:586`) | **hook never installs** |
| `kSig_MarkerProtection` | 7 tok | **1** @ `0x14CAF4D62` | `InstallMarkerProtectionHook` unconditionally `return false` (`teleport.cpp:461-468`) | resolves but **hard-disabled** |

**Consequence of `kSig_MarkerPlayer == 0`:** `g_markerPlayer` stays `0`, so the
*secondary* marker-player write path (`markerPlayer+0x90` / `+0x1A0`) never
runs. Map-marker teleport still works because PE 2944 resolves the active
destination directly from UI state through the inline AOB at
`src/game/teleport.cpp:529`:

```text
48 8B 05 ?? ?? ?? ?? 48 8B 98 A8 00 00 00 C4 C1 78 10 04 24
   -> exactly 1 match, VA 0x140DEFCFE, inside sub_140DEF890 (+0x46E)
   -> mem::ResolveRipAt(hit, 7)  =>  g_markerDestinationGlobal
```

This is the anchor that keeps the feature alive on this revision. Treat the
proxy hook as **unavailable on PE 2944**, not broken — and treat the direct
reader as the primary contract to protect during an update.

**`kSig_MarkerProtection` must stay disabled.** Its historical target
`0x14C4542E2` is an engine streaming / task-queue ring buffer, not player
collision; patching it corrupts the queue. The resolved match at `0x14CAF4D62`
on this build is irrelevant while the hook is hard-disabled — do not "enable it
now that it resolves".

### 8.3 Binary Ninja annotation state for travel

The four `VIBE_FastTravel_*` functions
(`VIBE_FastTravel_SelectDestination` `0x140654ED0`,
`VIBE_FastTravel_TryAcceptRequest` `0x1406550B0`,
`VIBE_FastTravel_FlushPendingRequest` `0x140DBC6C0`,
`VIBE_FastTravel_Execute` `0x140A9A8D0`) and
`VIBE_LevelGimmickSceneObjectInfoTable_GetById` `0x1404A2630` were **already
annotated** by an earlier analysis session, and they are the only functions in
the database whose comments carry the full PE-dossier tail. No rename and no
comment change was needed in this pass — per the governing plan, an existing
`VIBE_*` name is only changed when the evidence proves it wrong, and it does not
here.

### 8.4 Added to the update recipe for the next PE

In addition to steps 1–8 above, the next update must also:

- Re-run `kSig_MarkerPattern` (expect **5** = `kExpected_MarkerMatches`) and
  `kSig_MarkerOriginPrefix` (expect `>= 6`; 26 on this build).
- Re-run `kSig_MarkerPlayer`. Expect **0**; if it becomes 1, the marker-player
  proxy write path becomes available again and the feature gets *stronger* —
  record that as a capability change, not a fix.
- Re-run the `teleport.cpp:529` inline AOB (expect exactly 1 match) and
  re-resolve its RIP displacement, because **this** is the anchor map-marker
  teleport actually depends on.
- Fix the dead `kExpected_OriginMatches = 9` constant, and confirm
  `kSig_MarkerProtection`'s hook is still hard-disabled.
- Continue to treat **Native Fast Travel as fail-closed and never
  live-verified** (§6 item 1, and the prior-knowledge digest). The recorded
  `accepted` log line proves the *selection gate* ran, not that the player
  travelled. Do not ship a success toast on that evidence alone.
