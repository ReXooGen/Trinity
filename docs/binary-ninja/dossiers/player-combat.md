# Player and combat — feature dossier

Trinity scope: the **PLAYER → Combat & Gameplay Options** page
(`src/gui/menu.cpp:79-117`, routed at `:3230`) and the stat-pinning subsystem
behind it: God Mode, One-Hit Kill, Infinite Stamina & Mount, Infinite Spirit,
Infinite Item Durability, No Fall Damage, No Bounty, and the Outgoing / Incoming
Damage sliders — plus the character-manager resolution all of them depend on.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, FileVersion `1.0.0.2944`.
Binary Ninja database `CrimsonDesert.exe.bndb`, view `view_1` (PE).
See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**. No breakpoints, no debugger, no injection,
no memory writes, no game launch.

---

## 1. The two hooks that matter, and one that is dead

```text
PLAYER tab -> RenderCombatOptions (menu.cpp:79-117)
   God Mode              menu.cpp:87-90    (gated by a readiness check)
   One-Hit Kill          menu.cpp:85-86    ScaleDamage = 10000.0f  (player.cpp:713)
   Infinite Stamina & Mount  menu.cpp:95-100
   Infinite Spirit       menu.cpp:101-102
   Infinite Item Durability  menu.cpp:91-92   -> equipment.cpp:2033-2042
   No Fall Damage        menu.cpp:93-94    (default true, state.h:48)
   No Bounty             menu.cpp:103-108  -> see crime-money.md
   Outgoing / Incoming Damage sliders  menu.cpp:109-112

        v
HOOK A:  kSig_DamageApply / kSig_DamageApply_Alt
         Player::damage-apply  @ 0x1417AE100
         carries: God Mode, No Fall Damage, mount immunity, One-Hit Kill,
                  both damage multipliers, infinite-stamina drain block

HOOK B (DEAD): kSig_CombatTimingEval
         Player::Combat-timing @ 0x140873850
         the callback is a pure pass-through -> Easy Parry does NOT exist

HOOK C (NOT INSTALLED on this revision): kSig_StatCommit
         -> 0 matches on PE 2944, and skipped for TU 2.01+ anyway
```

---

## 2. Function dossiers

### `Player::damage-apply` — VA `0x1417AE100` *(pre-existing game-side symbol)*

- Feature: God Mode, No Fall Damage, Infinite Stamina drain block, mount
  immunity, One-Hit Kill, Outgoing / Incoming Damage multipliers.
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x17AE100` / VA `0x1417AE100` / `.code` / 346 bytes / 5 basic blocks
- Locator: **two** shipping signatures, both exact-unique, both landing on this
  same VA:
  - `kSig_DamageApply` (35 tokens) → **1** @ `0x1417AE100`
  - `kSig_DamageApply_Alt` (35 tokens) → **1** @ `0x1417AE100`

  Having two independent signatures agree on one address is the strongest
  locator evidence in this dossier. Live hook state: installed (MinHook detour).
- Confidence: **confirmed** for the locator→address binding and role (two
  unique signatures + a pre-existing symbol name). The full ABI below is the
  Binary Ninja prototype and was **not** re-derived by decompilation this pass.
- ABI (Binary Ninja prototype):
  `uint64_t(int64_t* statusCtx, int16_t statusId, int64_t timeNow, int64_t delta, int64_t* sourceInfo, char flagA, char flagB, char flagC, char flagD, char flagE, int64_t* outValue)`
  — i.e. `RCX` status context, `EDX`/`DX` status id, `R8` time, `R9` delta,
  `sourceInfo` on the stack, five `char` flags, and an out-parameter.
  **Trinity never validates this past `kMinPointer`** — see §5.
- Data model: operates on the character status context; the pinned values are
  written through the *stat-pin* path (`PinEntry`, `player.cpp:208-209`), not
  by this function's own fields.
- Call flow: not walked exhaustively this pass (it is a leaf-ish damage-apply;
  Trinity only needs the entry address and the register/stack layout).
- Trinity consumer: `src/game/player.cpp` — hook `hkDamageApply`
  (the `No Fall Damage` / flight coupling at `:762-782`). Role: **MinHook
  inline detour**.
- Static proof: two independent 35-token signatures each matching exactly once
  in the whole image, at the same VA, over all 12 PE sections.
- Live proof: God Mode and the damage sliders are live-working; `progress.md`
  still classifies this row as "STATIC READY, not semantic PASS".
- OFF/restore or fail-safe result: MinHook detour; **no bytes are patched** in
  this file. Removing the hook restores the game's own damage handling exactly.
- Update notes: re-run **both** signatures. If they disagree, that is a red
  flag — record it and keep the feature fail-closed rather than picking one.
  Then re-read the argument layout: the five `char` flags and the stack
  `sourceInfo` are the parts most likely to change.
- Open questions: see §5.

### `Player::Combat-timing` — VA `0x140873850` — **hook is dead**

- Feature: **Easy Parry / Easy Evade — NOT IMPLEMENTED on this revision.**
- Location: RVA `0x873850` / VA `0x140873850` / `.code` / 832 bytes / 46 basic blocks
- Locator: `kSig_CombatTimingEval` (18 tokens) → **exactly 1 match**, at this VA.
- ABI (Binary Ninja prototype):
  `uint64_t(void* arg1, int128_t* arg2, int128_t arg3 @ zmm2, char arg4, bool* arg5)`
- Confidence: **confirmed** that this is the function Trinity's
  `kSig_CombatTimingEval` resolves, and **confirmed** that Trinity's callback
  does nothing with it.
- Trinity consumer: `src/game/player.cpp` — `hkCombatTimingEval` (`:799-805`) is
  a **pure pass-through**: it calls the trampoline and returns `orig` unchanged;
  `outResult` is never written; the `const State& st` at `:802` is fetched and
  never used.

  There is **no** `easyParry` field in `src/core/state.h:44-57`, **no** settings
  key (`src/core/settings.cpp:78-99`), and **no** menu row for it. The only
  leftovers are stale language-ini keys.

  Worse, the install log still claims the hook is active and the fail-safe
  string still reads `Easy Parry & Easy Evade helper timing disabled`
  (`player.cpp:852-856`) — so the log actively misleads.

  `kSig_JustCore` and `kSig_JustCore_Alt` (`offsets.h:181-184`) have **zero
  consumers** in `src/` and **0 matches** on PE 2944.

- **Status classification: legacy / dormant / not safe to enable.** Do not
  "restore" Easy Parry by re-deriving this function from the current callback
  signature: `REVERSE_ENGINEERING_GUIDE.md:170-181` documents an intended
  mechanism (a reaction flag `a6 = 2`) that **does not match** the current
  callback signature. Whether the body was deleted deliberately or reverted is
  stated nowhere in `src/`.
- Update notes: leave the hook dead. If Easy Parry is ever reinstated, it needs
  its own dossier entry, a menu row, a state field and a settings key first.

### `kSig_StatCommit` — **absent on PE 2944**

| Item | Value |
|---|---|
| Measured matches | **0** over all 12 sections |
| Trinity install gate | skipped for TU 2.01+ (`src/game/player.cpp:818-824`, gate `src/core/version_mapping.cpp:22-28`) — so it is not even probed on this revision |
| Consequence | God Mode's documented guarantee "never observed at lethal HP" (`offsets.h:133-139`) **does not hold on modern builds**. The guard is the per-frame `PinEntry` write only. |

This is the most important *semantic* limitation in this dossier: God Mode on
PE 2944 is a per-frame value pin, not a damage-time interception.

---

## 3. Character-status and record functions in scope

These carry the stat model Trinity pins. All are pre-existing `VIBE_` names and
were **kept**; the table records the ones a PE update must re-check.

| Function | VA | Role |
|---|---|---|
| `VIBE_ComputeStatusValueForKey` | `0x14066B500` | resolves a status value by key — the value God Mode pins |
| `VIBE_CharacterStatus_ApplyDelta` | `0x1417AE530` | applies a status delta |
| `VIBE_CharacterStatus_ApplyDeltaPrimary` | `0x1417AC0C0` | primary delta path |
| `VIBE_CharacterStatus_RebuildSlotValues` | `0x1417AAD30` | rebuilds slot values |
| `VIBE_CharacterStatus_GetAccumulatedValue` | `0x1417AD6F0` | accumulated value read |
| `VIBE_CharacterStatus_AccumulateSlotValue` | `0x1417B2B30` | accumulation |
| `VIBE_CharacterStatus_ComputeSlotEntries` | `0x1417B2EC0` | slot-entry computation |
| `VIBE_CharacterStatus_IsStatusGroupActive` | `0x1417B0650` | grouped-status gate |
| `VIBE_CharacterStatus_GetGroupSlotEntry` | `0x1417ADD40` | group slot entry |
| `VIBE_ApplyRecordToCharacter` | `0x1427990D0` | applies a record to a character |
| `VIBE_GetCharacterStatusValueForSlot` | `0x142153350` | per-slot status value |
| `VIBE_GetActiveSlotIndexForRecord` | `0x14214BD30` | active slot index (also the worker patch host — see `trust-worker.md`) |
| `VIBE_StatusInfoTable_GetById` | `0x1405835F0` | status definition table |
| `VIBE_StatusGroupInfoTable_GetById` | `0x140636580` | status-group table |
| `VIBE_CharacterInfoTable_GetRecordById` | `0x140389570` | character record |
| `VIBE_SkillTable_GetById` | `0x1403880A0` | skill definitions |
| `VIBE_BuffInfoTable_GetById` | `0x14066F620` | buff definitions |

The character-manager anchors are `kCharMgrAnchors[]` in
`src/game/offsets.h` — **six** entries. The comment above them says "all four
resolve to `qword_61830F8`", which is stale; the array has six. Fix the comment
before trusting it during an update.

### Dead player API (record, do not resurrect)

| Symbol | Status |
|---|---|
| `Player::DumpCharacters()` | **declared** `player.h:69`, **no definition anywhere** |
| `Player::GetMountActor` / `GetMountOwner` / `GetTrackedMountCount` | defined `player.cpp:968-983`, **no callers** |
| `Player::RefreshSelf()` | defined `player.cpp:888-905`, **no callers** |
| `g_isRidingMount` (`player.cpp:136`), `s_lastResolveMs` (`:331`) | assigned, never read |
| `kOff_StatEntry_Floor`, `WN::DIR_X/DIR_Z/TURB_DENS/TURB_SCALE`, `CN::DUST_ADD` | never written |

---

## 4. Locator evidence (measured over the whole image)

Method: `_analysis/aob_scan2.ps1` reproduces Trinity's scanner against the
on-disk image, all 12 sections.

| Locator | Length | Matches | VA | Result |
|---|---|---|---|---|
| `kSig_DamageApply` | 35 tok | **1** | `0x1417AE100` | exact unique |
| `kSig_DamageApply_Alt` | 35 tok | **1** | `0x1417AE100` | exact unique — **same VA** |
| `kSig_CombatTimingEval` | 18 tok | **1** | `0x140873850` | exact unique (hook is dead) |
| `kSig_StatCommit` | 33 tok | **0** | — | **absent — not probed on 2944** |
| `kSig_JustCore` | 17 tok | **0** | — | absent, no consumer |
| `kSig_JustCore_Alt` | 16 tok | **0** | — | absent, no consumer |

---

## 5. Cross-feature hazards

1. **The stat/damage subsystem rides the movement hook.** `Player::Tick()` and
   `World::Tick()` are called **only** from `VIBE_Locomotion_MoveUpdateIntegrator`
   (`src/game/teleport.cpp:1637-1645`). If `kSig_MoveUpdate` fails, every
   stat-pin, the sun-clamp half of Freeze and **all** live weather/wind
   injection stop — while the menu still shows those controls as available.
   `src/core/mod.cpp:130` discards `Teleport::Install()`'s `false` return, so
   nothing else reports it. See [`locomotion.md`](locomotion.md) §1.
2. **Mount features are silently disabled on the TU 2.01 fallback path.**
   `player.cpp:333-338` is entered when `g_charMgrGlobal == 0`, and that path
   deliberately turns **off** party-wide *and* mount discovery. Infinite Mount
   Stamina and mount God Mode then do nothing while the menu still shows them
   available.
3. **`st.infMountStamina` has no independent control** — it is only ever
   assigned `= st.infStamina` (`menu.cpp:98`, `settings.cpp:232`). Do not treat
   it as a separate feature during an update.
4. **The damage-apply failure log under-reports badly.** `player.cpp:843` names
   only `infinite stamina drain block disabled`, but that single hook carries
   God Mode, No Fall Damage, mount immunity, One-Hit Kill and both multipliers.
   A failed install therefore looks far less serious in the log than it is.
5. **Stale character pointers can outlive their check.** `Player::Tick`
   early-returns **without clearing the atomics** when no stat feature is active
   and no consumer has called `GetTrackedPlayerCount()` within 2000 ms
   (`player.cpp:866-870`, `player_logic.cpp:21-27`), leaving stale pointers live
   for the damage hook.
6. **Deliberate exclusions that look like bugs.** Status types **17 / 18 / 48**
   are purged from `IsStaminaType` (`player.cpp:173-178`) so Wyvern fire-breath
   is never pinned. Snow intensity is unreachable from any weather preset.
   Neither is an oversight.
7. **Damage-apply stack-argument ABI is never validated** beyond the
   `kMinPointer` floor — the five `char` flags and the `sourceInfo` stack
   argument are the fragile part of this contract.

---

## 6. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. Re-run **both** damage-apply signatures. Require exactly 1 match each and
   require them to land on the **same** VA. Disagreement ⇒ fail closed.
3. Re-read `Player::damage-apply`'s argument layout: `RCX` status context,
   status id, time, delta, stack `sourceInfo`, the five `char` flags, and the
   out-parameter. Confirm no argument is now dereferenced that Trinity passes as
   a sentinel.
4. Re-run `kSig_CombatTimingEval`; expect 1 match. **Do not wire a behaviour to
   it** — record instead whether Easy Parry is still absent from the menu, the
   state struct and the settings keys.
5. Re-run `kSig_StatCommit`; expect **0**. If it becomes non-zero, that is new
   information: it would restore the damage-time interception that God Mode
   currently lacks. Document it before changing any gate.
6. Re-verify `kSig_MoveUpdate` first (see `locomotion.md`) — this whole
   subsystem is dead without it, and the failure is silent.
7. Re-check the character-manager anchors array (`kCharMgrAnchors`) and fix the
   stale "all four" comment.
8. Runtime test, one feature at a time with an OFF leg, recording observed
   results (the log under-reports — do not infer success from silence):
   - God Mode: take a hit with it OFF (record damage), then ON (no damage);
     confirm the OFF leg restores damage.
   - One-Hit Kill: confirm a single hit kills; OFF restores normal damage.
     Note the label says 1,000× while the code uses `10000.0f` — fix whichever
     is wrong.
   - Infinite Stamina & Spirit: sprint/cast to drain with OFF, then ON.
   - No Fall Damage: jump from height with it OFF, then ON.
   - Mount legs: only meaningful if the character-manager global resolved —
     check `g_charMgrGlobal != 0` first, otherwise the mount features are
     expected to be inert.
