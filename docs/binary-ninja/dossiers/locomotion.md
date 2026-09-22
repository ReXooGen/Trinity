# Locomotion (Super Run / Super Jump / Free Flight) — feature dossier

Trinity scope: the three PLAYER-tab locomotion features — **Super Run**
(`st.superRun` / `st.superRunMult`), **Super Jump** (`st.superJump` /
`st.superJumpMult`) and **Free Flight** (`st.freeFlight` / `st.flightSpeed`) —
plus the movement hook they all depend on.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, image size `0x17FCD000`, FileVersion `1.0.0.2944`.
Binary Ninja database `CrimsonDesert.exe.bndb`, view `view_1` (PE).
See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**, through Binary Ninja's WARP MCP endpoint
(`127.0.0.1:24642/mcp`, view `view_1`). No breakpoints, no debugger, no
injection, no memory writes, no game launch.

---

## 1. Why these two functions, and why nothing else

Grounded movement speed **cannot** be won at the Havok character-proxy
integrator. Three approaches were tried and live-disproven (2026-07-14), and a
hardware-watchpoint trace showed why: the character movement component runs a
**servo**. Per tick it (1) computes a drive velocity, (2) passes it as `arg3`
into a sub-step driver that writes it to `moveOwner+0xC0` and calls the
integrator, then (3) measures the displacement that actually happened and books
it back. Any velocity injected *inside* the integrator makes the body overshoot
what the servo expected, and step 3 pulls it right back — pulsing on the flat and
clamped to exactly 1× uphill (`src/game/offsets.h:348-380`).

The fix is therefore to scale the drive vector **at the sub-step driver**,
before the servo consumes it — which is `VIBE_Locomotion_StepDriveVelocity`.

```text
Trinity menu (PLAYER tab)
  Super Run     menu.cpp:150  ─┐
  Super Jump    menu.cpp:152  ─┤
  Free Flight   menu.cpp:154  ─┘
        |
        v
MinHook detour #1:  kSig_MoveUpdate
  VIBE_Locomotion_MoveUpdateIntegrator @ 0x144282080
    RCX = move owner  (= [movement component + 0x2C0])
    • ApplyJumpScaling(owner)          <-- Super Jump: scale +0xC0 up-component
    • trampoline -> the real integrator
    • publish live position from +0x90 (TRAVEL tab readout)
    • apply queued map-marker teleport (+0x90/+0xC0/+0xD0/+0x1A0) w/ read-back
    • fire queued native fast travel
    • dispatch Player::Tick, World::Tick, Inventory::Tick, Dye::Tick,
      Equipment::Tick, Friendly::Tick, ServiceProtectionExpiry   <-- THE game-thread pump
        |
        v
MinHook detour #2:  kSig_LocoStepper_PE2944
  VIBE_Locomotion_StepDriveVelocity @ 0x14369FF60
    RCX = movement component,  XMM1 = dt,  R8 = float* drive velocity (x,y,z)
    • Super Run   : vel[0] *= mult, vel[2] *= mult (clamp 50.0f)   <-- grounded
    • Free Flight : vel[1] = ±speed (clamp 35.0f), hover, damping  <-- airborne
    • trampoline
        |
        v
  calls VIBE_Locomotion_MoveUpdateIntegrator at 0x1436a01f8
        with RCX = [component + 0x2C0]
```

The `[component + 0x2C0]` relation is the load-bearing fact of this dossier and
is confirmed from three independent directions (§4).

---

## 2. Function dossiers

### VIBE_Locomotion_MoveUpdateIntegrator  *(renamed this pass from `Teleport:: Movement-update`)*

- Feature: Super Jump; also the movement hook that carries map-marker teleport,
  the live-position readout, native fast-travel dispatch and the game-thread tick
  pump (see [`travel.md`](travel.md) §4 and `player-combat.md`).
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x4282080` / VA `0x144282080` / `.code` / 4393 bytes / 142 basic blocks
- Locator: `kSig_MoveUpdate` = `48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56`
  (14 tokens, **2 wildcards**; `src/game/offsets.h:325-326`)
  → **exactly 1 match in the whole image**, at this VA, section `.code`,
  measured over all 12 PE sections with Trinity's own scanner semantics.
  **The wildcard tail is what makes this unique:** the 6-token prefix
  `48 8B C4 4C 89 48` alone has **400 matches**. Do not shorten it.
  Live hook state: installed (MinHook detour).
- Confidence: **confirmed**
- ABI: `RCX` = move owner (Havok character proxy / integrator object).
  Trinity declares `hkMoveUpdate(uint64_t moveOwner, a2…a7)` and forwards every
  register, returning the trampoline's `RAX`. Only `RCX` is actually relied on;
  `a2…a7` are never read, so ABI drift beyond the prologue is **not detectable**
  from Trinity's side.
- Data model:
  | Field | Meaning | Trinity use |
  |---|---|---|
  | `moveOwner+0x90` | position `x,y,z,w` f32 | read for the TRAVEL-tab readout; written by map-marker teleport |
  | `moveOwner+0xC0` | desired / input velocity `x,y,z,w` f32 | **Super Jump** scales component `[1]` (up); zeroed when pinning a teleport |
  | `moveOwner+0xD0` | the frame velocity the integrator writes back → new position = pos + velocity | zeroed when pinning a teleport |
  | `moveOwner+0x1A0` | secondary destination | written by map-marker teleport |

  All four are present in this function's own body: `0x1442822a0 movups xmm5,[rbx+0xc0]`;
  `0x144282337 mulps xmm3,[rbx+0xd0]` + `0x144282378 movups [rbx+0xd0],xmm3`;
  `0x1442823b3 lea r8,[rbx+0x90]`; `0x1442825c5 lea r13,[rbx+0x90]`;
  `0x14428251f lea rdi,[rbx+0xd0]`.
- Call flow: **exactly 2 callers** —
  `VIBE_Locomotion_StepDriveVelocity` @ `0x1436a01f8` and `sub_1436a37f0`
  @ `0x1436a494e`. At the first call site:
  ```asm
  0x1436a0157  lea  rbx, [r13+0x2C0]     ; r13 = movement component
  0x1436a01f5  mov  rcx, [rbx]           ; RCX = [component+0x2C0] = move owner
  0x1436a01f8  call VIBE_Locomotion_MoveUpdateIntegrator
  ```
  Notable callees: `sub_144132270`, `sub_144134a10`, `sub_144134f30`,
  `sub_144286600`, `sub_144285a40`, `sub_144283f10`.
- Trinity consumer: `src/game/teleport.cpp` → hook `hkMoveUpdate`
  (install `:1839-1842`, body `:1575-1722`); `ApplyJumpScaling` `:1197-1222`
  called at `:1586-1588`. Role: **MinHook inline detour** (not a patch).
- Static proof: unique wildcard AOB; prologue spill of `RDX`/`R9`; both caller
  sites; the `[component+0x2C0]` load immediately before the call; the
  `+0x90` / `+0xC0` / `+0xD0` field accesses inside the body.
- Live proof: Trinity tracks the local player's coordinates correctly and Super
  Jump works in game, which validates that `RCX` is the local player's move
  owner on PE 2944. Recorded in `src/game/offsets.h:318-321`.
- OFF/restore or fail-safe result: MinHook detour removed via `mem::RemoveHook`
  (`MH_DisableHook` + `MH_RemoveHook`) from `Teleport::Remove()` `:1946`
  ← `Mod::Shutdown`. **No bytes are patched, so there is nothing to restore.**
  If the locator is missing, `teleport.cpp:1841` returns `false` and position
  tracking, marker apply, native travel dispatch **and every game-thread tick
  above** are absent. **HAZARD: `src/core/mod.cpp:130` discards that `false`
  return**, so the overlay still starts with all of those silently missing —
  only `mem::InstallHook`'s own `LOG_ERR` records it.
- Update notes: re-find with the full 14-token wildcard AOB (expect exact
  unique). Then re-verify (a) the prologue still spills `RDX`→`[rax+10h]` and
  `R9`→`[rax+20h]`, (b) the caller still loads `RCX` from `[component+0x2C0]`,
  (c) `+0x90` is still position and `+0xC0` still the desired velocity. If the
  caller's `+0x2C0` load is gone, you are looking at the wrong function.
- Open questions: the `a2…a7` parameter meanings are uncharacterised (Trinity
  never reads them). `R9`/`RDX` spilled at the prologue are the only clue that
  the game itself passes two more real arguments.
- Naming note: this function carried the pre-existing user symbol
  `Teleport:: Movement-update` (`BNINTERNALNAMESPACE`, `autoDefined=false`) from
  an earlier analysis session. That is **not** a game symbol — the binary
  contains no `MoveUpdate` / `MovementUpdate` / `Movement-update` string — and
  its colon-namespace collides with Trinity's own `Teleport` module. It was
  renamed to the standard `VIBE_` scheme; the old name is preserved in the
  Binary Ninja comment so earlier notes stay traceable.

### VIBE_Locomotion_StepDriveVelocity  *(renamed this pass from `sub_14369ff60`)*

- Feature: **Super Run** (grounded speed scaling) and **Free Flight**
  (propulsion / hover / forced landing).
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x369FF60` / VA `0x14369FF60` / `.code` / 4225 bytes / 123 basic blocks
- Locator: `kSig_LocoStepper_PE2944` (40 bytes, `src/game/offsets.h:401-403`)
  `48 8B C4 48 89 58 10 44 88 48 20 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 78 F8 FF FF 48 81 EC 50 08 00 00`
  → **exactly 1 match in the whole image**, at this VA, section `.code`.
  Live hook state: installed at this exact VA (MinHook detour);
  `src/game/offsets.h:397-399` records the PE 2944 live install and the
  `component+0x2C0` owner layout.
- Confidence: **confirmed**
- ABI: `RCX` = movement component (`this`); `XMM1` = `float dt` (Trinity declares
  it as a real `float` parameter **solely** to keep `XMM1` intact through the
  MinHook trampoline — it never reads it, `teleport.cpp:82-90`); `R8` = `float*`
  drive velocity `x,y,z`; `R9B` = `a4`; `a5…a7` on the stack; return unused.
  The Binary Ninja prototype independently agrees:
  `int64_t*(void* arg1, int32_t arg2[0x4] @ zmm1, int32_t (* arg3)[0x4], char arg4, …)`.
- Data model:
  - `[component + 0x2C0]` = **local-player move owner**. This is a *pointer*
    stored at `+0x2C0`, not a value. Proven at `0x14369ffd6`:
    `lea rbx,[rcx+0x2C0]; mov rax,[rbx]; vmulps xmm2,xmm0,[rax+0x180]; vmovups xmm0,[rax+0x90]`.
  - Six independent `[component+0x2C0]` accesses exist:
    `0x14369ffd6`, `0x1436a0157`, `0x1436a021a`, `0x1436a027d`, `0x1436a02f7`,
    `0x1436a0742`.
  - Per-revision owner offset (`src/core/version_mapping.cpp:30-46`):
    **PE 2944 = `0x2C0`**, modern (2760/2850) = `0x2B8`, legacy = `0x298`,
    unsupported = `0`. `0` would make `ReadPtr(component+0)` read the vtable, so
    `isPlayer` would never match — moot in practice because the hook is not
    installed at all on an unsupported revision.
  - Move-owner fields `+0x90` (destination) and `+0x180` are read here and match
    the fields `VIBE_Locomotion_MoveUpdateIntegrator` consumes.
  - `R8` points at a **caller scratch buffer near `0x013FDD70`** — far *below*
    `kMinPointer`. `mem::Read32`/`Write32` reject anything under that floor and
    would silently no-op. Trinity must (and does) access this vector raw +
    SEH-guarded (`src/game/offsets.h:388-395`).
- Call flow: **14 call sites**, from `sub_1436a37f0` (4),
  `sub_14219e4ac` (2), `sub_14369f000` (1), `sub_1436a4ba0` (2), plus further
  sites. → **this** → `VIBE_Locomotion_MoveUpdateIntegrator` @ `0x1436a01f8`.
- Trinity consumer: `src/game/teleport.cpp` → hook `hkLocoStep`, body
  `:1326-1573`, install `:1903-1933`; signature chosen by
  `core::LocoStepperContractForRevision`; offset selector
  `src/core/version_mapping.cpp:30-46`. Role: **MinHook inline detour**.
  Writes (all gated on `vel != nullptr` and `isPlayer`):
  | Feature | Write | Cap |
  |---|---|---|
  | Super Run | `vel[0] *= st.superRunMult`, `vel[2] *= st.superRunMult` | `kMaxSafeGroundSpeed = 50.0f` |
  | Free Flight vertical | `vel[1] = ±safeFlightSpeed` | `kMaxSafeVerticalSpeed = 35.0f` |
  | Free Flight horizontal | scale `vel[0]`, `vel[2]` | `kMaxSafeFlightSpeed = 35.0f` |
  | Flight hover / damping / forced landing | `vel[1] = ±0.004`, damping, `vel = {0,-4,0}` | — |
- Static proof: unique 40-byte AOB; prologue byte-compare; six move-owner
  accesses; the call-site `RCX=[component+0x2C0]` derivation; ABI agreement with
  the Binary Ninja prototype.
- Live proof: `src/game/offsets.h:397-399` records the hook installing **and
  firing** at `0x14369FF60` on PE 2944 with move-owner offset `0x2C0`; the user
  confirmed Super Run and Free Flight working in game on 2026-09-20.
- OFF/restore or fail-safe result: MinHook detour removed via `mem::RemoveHook`
  (`g_locoStepTarget`) `src/game/teleport.cpp:1945` ← `Teleport::Remove()`
  ← `Mod::Shutdown`. No bytes patched. Fail-closed paths:
  - contract `Unsupported` → `LOG_ERR("teleport: locomotion-stepper contract unavailable for PE %u - Super Run/Free Flight disabled.")` `:1923`, `locoSignature` stays null;
  - install failure → `LOG_ERR("teleport: locomotion-stepper hook NOT installed - Super Run/Free Flight disabled.")` `:1933`;
  - Super Run scale skipped when `st.superRunMult == 1.0f` `:1539`; read failure skips the write `:1542`.
- **No menu gating.** Unlike God Mode (`menu.cpp:88-90`), `menu.cpp:150-155`
  performs no readiness check, and `teleport.h` exposes no `Teleport::`
  locomotion readiness accessor. On an unsupported revision the PLAYER-tab
  toggles stay **visible and settable but inert** — a silent-failure hazard for a
  future PE.
- **Locomotion emits essentially no runtime log lines** — only the one-shot
  `teleport: loco diag …` (`:1371`) and the HUD `FLY` suffix. This is the main
  static↔runtime comparison gap for this dossier; see §6.
- Update notes: re-find with the 40-byte AOB (expect exact unique). Then
  re-verify (a) `+0x2C0` is still the move-owner pointer offset, (b) `R8` is
  still the drive-velocity scratch pointer, (c) `[moveOwner+0x90]` is still the
  destination vector shared with the move integrator. **If the drive-vector
  argument register moves, Super Run and Free Flight must stay fail-closed.**
- Open questions: see §6.

---

## 3. Locator evidence (measured over the whole image)

Method: `_analysis/aob_scan.ps1` / `_analysis/aob_scan2.ps1` reproduce Trinity's
scanner (`src/mem/scanner.cpp` + `src/mem/section_filter.cpp`) against the
on-disk file image — PE section walk, `[VA, VA+VirtualSize)` clamped to the raw
bytes present, skipping only a section named exactly `.debug` without
`IMAGE_SCN_MEM_EXECUTE`. **All 12 sections of this build are scanned.**
`aob_scan2.ps1` adds the `?` single-byte wildcard that Trinity's parser supports.

| Function | Locator | Length | Matches | Result |
|---|---|---|---|---|
| `VIBE_Locomotion_MoveUpdateIntegrator` | `kSig_MoveUpdate` (2 wildcards) | 14 tok | **1** @ `0x144282080` | exact unique |
| `VIBE_Locomotion_StepDriveVelocity` | `kSig_LocoStepper_PE2944` | 40 B | **1** @ `0x14369FF60` | exact unique |
| — 6-token prefix of `kSig_MoveUpdate` | `48 8B C4 4C 89 48` | 6 B | **400** | **ambiguous — never use alone** |
| `kSig_LocoStepper` (PE 2760/2850) | 40 B | **1** @ `0x14369FF60` | **byte-identical to the PE 2944 signature** |
| `kSig_LocoStepper_Pre201` | 36 B | **0** | absent (expected) |
| `kSig_MarkerPlayer` | 11 B | **0** | absent — see §5 |

**Correction to the source comment.** `kSig_LocoStepper` (`offsets.h:407-409`)
and `kSig_LocoStepper_PE2944` (`:401-403`) are **byte-identical strings**, and
both match the same single address. The "do not use it as a PE 2944 fallback"
comment at `:405-406` therefore describes a *naming / version-selector*
distinction, **not** a byte-level difference: on this build the two locators are
interchangeable, so the fail-closed guarantee the comment claims does not
actually come from the bytes. This should be fixed in `offsets.h` before the
next PE; the dossier records it so a future update is not misled.

---

## 4. The `[component+0x2C0]` contract — three independent confirmations

1. **Trinity source + live proof.** `src/core/version_mapping.cpp:30-46` selects
   `0x2C0` for PE 2944, and `src/game/offsets.h:397-399` records the live
   diagnostic `isPlayer=1 moveOwnerOffsetFound=0x2C0`.
2. **Binary Ninja disassembly of the stepper.** Six `lea …, [component+0x2C0]`
   sites, and at `0x14369ffd6` the value is immediately dereferenced
   (`mov rax,[rbx]`) and used as a float4 vector base at `+0x90`/`+0x180` —
   i.e. `+0x2C0` holds the move-owner *pointer*.
3. **The call site into the integrator.** `0x1436a0157 lea rbx,[r13+0x2C0]` …
   `0x1436a01f5 mov rcx,[rbx]` … `0x1436a01f8 call` — the object handed to
   `VIBE_Locomotion_MoveUpdateIntegrator` in `RCX` is exactly
   `[component+0x2C0]`, which is the same object Trinity's Super Jump
   `ApplyJumpScaling` receives as `moveOwner`.

This is why the PE 2944 branch must **not** fall back to the historical `+0x2B8`.

---

## 5. Cross-feature hazards discovered in this pass

These are not locomotion features, but they are locomotion-adjacent and matter
for a PE update:

1. **`kSig_MarkerPlayer` has 0 matches on PE 2944.** `teleport.cpp:524` calls
   `FindAllMatches(kSig_MarkerPlayer, 2)` and `:586` only installs the
   marker-player proxy hook `if (players.size() == 1)`. With zero matches,
   `g_markerPlayer` stays `0`, so the *secondary* marker-player write path
   (`markerPlayer+0x90` / `+0x1A0`) never runs. Map-marker teleport still works
   because PE 2944 resolves the active destination directly from UI state
   (`g_markerDestinationGlobal`, `:528-532`, `:538`). Treat the proxy hook as
   unavailable on this revision rather than "broken".
2. **`kSig_MarkerPattern` = 5 matches, and `kExpected_MarkerMatches = 5`**
   (`offsets.h:561`) — they agree, so the count warning at `:544-548` does not
   fire. By contrast `kExpected_OriginMatches = 9` (`:565`) is **dead**;
   `:549` actually requires `origins.size() >= 6`, and `kSig_MarkerOriginPrefix`
   yields 26 matches. The constant disagrees with the code and should be
   deleted or corrected.
3. **Safe-landing / fall protection is permanently inert.**
   `Teleport::ActivateProtection` is a hard no-op (`teleport.cpp:2092-2098`) and
   every writer of `g_markerProtectDeadline` writes `0`, so `IsProtected()`
   (`:2086-2090`) is always `false`. That makes the "Automatic Safe Landing
   cushion" branch inside `hkLocoStep` (`:1377-1389`) dead code and the
   `ActivateProtection(120000)` after a marker teleport (`:1609`) a no-op. What
   actually protects the player is `st.noFallDamage` (default `true`,
   `state.h:48`) through `hkDamageApply` (`player.cpp:762-767`).
4. **`kSig_MarkerProtection` resolves (1 match @ `0x14CAF4D62`) but is dead.**
   `InstallMarkerProtectionHook` unconditionally `return false;`
   (`teleport.cpp:461-468`) because the historical target `0x14C4542E2` is an
   engine streaming / task-queue ring buffer, not player collision. Patching it
   corrupted the queue. **Leave it disabled.**

---

## 6. Open questions (require later runtime validation)

1. **`R8` drive-vector identity.** Trinity assumes `R8` is the caller's
   scratch drive vector. The Binary Ninja prototype agrees (`arg3` is a pointer
   type), and the live Super Run result corroborates it, but the vector's
   *lifetime* and whether it is the same buffer the servo books back from were
   not re-derived statically.
2. **`a2…a7` of the integrator** are uncharacterised; `RDX`/`R9` are spilled at
   the prologue but never read by Trinity.
3. **No locomotion log line.** Super Run / Super Jump / Free Flight activity
   cannot be confirmed from `Trinity.log` alone. §7 step 6 proposes the minimum
   safe runtime check.
4. **`isPlayer` gating is unproven.** The comment at `offsets.h:382-386` states
   the dispatch only reaches this path for the local player in practice. The
   player-identity match is derived from a one-shot diagnostic log
   (`teleport.cpp:1356-1375`), not a validated pointer chain, and a match failure
   is silent apart from that one log line.
5. **`oLocoStep`'s `dt` parameter** exists only to preserve `XMM1`; whether the
   trampoline needs any other XMM register preserved was not verified.

---

## 7. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. Re-run `kSig_MoveUpdate` (expect exact unique). **Do not shorten the
   wildcard**, then re-read the prologue: `mov rax,rsp` / `mov [rax+20h],r9` /
   `mov [rax+10h],rdx` / `push rbp` / `push r14`.
3. At the matched address, walk to its callers and confirm one of them is the
   locomotion stepper, and that the call site still does
   `mov rcx,[component+0x2C0]`. That single instruction is the cheapest proof
   that the `+0x2C0` contract survived.
4. Re-run `kSig_LocoStepper_PE2944` (expect exact unique) and re-read the 40-byte
   prologue. Confirm the ABI is still `RCX=component, XMM1=dt, R8=drive vector`.
5. Re-check the move-owner offset in `src/core/version_mapping.cpp`. If PE 2944's
   `+0x2C0` moved, update the mapping **and** record the old value as
   revision-specific — never let a new revision inherit it silently.
6. If either locator is absent or ambiguous: leave both hooks uninstalled (Super
   Run / Super Jump / Free Flight disabled), record the mismatch here, and do
   **not** enable `kSig_LocoStepper` or `kSig_LocoStepper_Pre201` as fallbacks.
7. Only then run the in-game test, one feature at a time, each with an OFF leg:
   - Super Run: enable at 2×, walk forward, disable — confirm speed returns to
     normal and the character does not rubber-band.
   - Super Jump: jump, confirm a higher arc, disable, confirm the normal arc.
   - Free Flight: toggle on, confirm lift and hover, toggle off mid-air, confirm
     the forced landing, then take fall damage once with `noFallDamage` off to
     prove the coupling is the only protection.
   - Because there is no locomotion log line, record the **observed** result
     here; do not infer success from the absence of an error.
8. Re-verify the §5 hazards: `kSig_MarkerPlayer` count (expect 0 → proxy hook
   stays off), `kSig_MarkerPattern` count (expect 5 = `kExpected_MarkerMatches`),
   `kSig_MarkerProtection` (expect 1 match but hook hard-disabled).
