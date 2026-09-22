# Trust, friendship, pets and the worker system — feature dossier

Trinity scope: **Trust Multiplier** (NPC / pet / mount taming, PLAYER tab) and
**Max Worker Level & Skills** (a single reversible code patch). Also the
support functions both features lean on, and the dead locators that surround
them.

Snapshot: **PE 2944** — `CrimsonDesert.exe` SHA-256
`6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7`,
image base `0x140000000`, FileVersion `1.0.0.2944`.
Binary Ninja database `CrimsonDesert.exe.bndb`, view `view_1` (PE).
See [`../snapshots/PE-2944.md`](../snapshots/PE-2944.md).

Analysis method: **static only**. No breakpoints, no debugger, no injection,
no memory writes, no game launch.

---

## 1. Trust / friendship / pet

### 1.1 Mechanism

`"Trust Multiplier"` (`src/gui/menu.cpp:156-159`, state `src/core/state.h:102-103`)
hooks **four** engine functions and rewrites the trust value **on gains only**
— it never lowers a value. Two of the four are the 2.01+ locators and two are
TU 2.00 fallbacks that do not resolve on this revision.

```text
menu "Trust Multiplier"  (menu.cpp:156-159)
        |
        v
Friendly::Install  (src/game/friendly.cpp:350 area)
  NPC setter            kSig_FriendlySetNpc201  -> 0x141ECB7D0  NPC::TrustMultiplier
  Pet/mount setter      kSig_FriendlySetPet201  -> 0x14DF6DA50  Pet_and_mount::TrustMultiplier
  NPC getter            kSig_FriendlyGetNpc201  -> 0x141ECA1C0
  Pet/mount getter      kSig_FriendlyGetPet201  -> 0x141ECB640  VIBE_Trust_FindRecordByActor_L38
  TU 2.00 fallbacks     kSig_FriendlySetNpc / kSig_FriendlySetPet  -> 0 matches on PE 2944
        |
        v
record +0x20 (i64 trust) rewritten, clamped to 0..100
```

### 1.2 Record layout (`src/game/offsets.h:1827-1833`)

| Offset | Type | Meaning | Note |
|---|---|---|---|
| `+0x00` | `u32` | record key | `kOff_FriendlyRec_Key` |
| `+0x04` | `u16` | group / bucket key | `kOff_FriendlyRec_Group` |
| `+0x20` | `i64` | **trust value** | `kOff_FriendlyRec_Value` |
| — | — | cap `kFriendly_Max = 100` | taming / NPC cap |

**Do not read `+0x28`.** The source comment records that TU 2.01 still stores
`_varyFriendly` in the second 32-byte copy block, and that reading `+0x28`
silently rejected every real trust update (`offsets.h:1829-1831`). This is the
single most likely field to be "fixed" incorrectly during a PE update.

### 1.3 Function dossiers

#### `NPC::TrustMultiplier` — VA `0x141ECB7D0` *(pre-existing game-side symbol)*

- Feature: Trust Multiplier (NPC half)
- Location: RVA `0x1ECB7D0` / VA `0x141ECB7D0` / `.code`
- Locator: `kSig_FriendlySetNpc201` (25 tokens, `src/game/offsets.h`)
  → **exactly 1 match in the whole image**, at this VA, all 12 sections scanned.
- Confidence: **confirmed** for the locator→address binding and the role
  (the symbol name is pre-existing in the database and the address is the
  unique match of Trinity's own setter signature). The full ABI was **not**
  re-derived in this pass.
- Trinity consumer: `src/game/friendly.cpp` — MinHook detour that rewrites
  `record+0x20` on gains. Role: **hook**.
- Supporting evidence: `VIBE_Map_FindOrCreateValueSlot` (`0x140436380`) is
  called from this function, and `VIBE_Vector68_Grow` (`0x141ECCD40`) has only
  three callers — `NPC::TrustMultiplier`, `Pet_and_mount::TrustMultiplier`
  (`0x14DF6DA50`) and `sub_14295DCD0`. That shared callee set is independent
  proof that this really is the NPC trust-setter and that the pet path is real.
- Update notes: re-run `kSig_FriendlySetNpc201`; confirm `0x141ECB7D0` still
  calls `VIBE_Map_FindOrCreateValueSlot` and still touches `+0x20` on the
  record. If the `+0x20` access is gone the field moved — re-derive before
  re-enabling.

#### `Pet_and_mount::TrustMultiplier` — VA `0x14DF6DA50` *(pre-existing game-side symbol)*

- Feature: Trust Multiplier (pet / mount half)
- Location: RVA `0xDF6DA50` / VA `0x14DF6DA50` / `.code`
- Locator: `kSig_FriendlySetPet201` (44 tokens) → **exactly 1 match**, at this VA.
- Confidence: **confirmed** (locator unique + pre-existing symbol + the
  `VIBE_Vector68_Grow` caller-set argument above).
- ABI: not re-derived this pass.
- Trinity consumer: `src/game/friendly.cpp` — **hook**.
- Update notes: as above; this is the address most likely to move, because it
  sits far from the other trust code and has the least corroboration.

#### `VIBE_Trust_FindRecordByActor_L38` — VA `0x141ECB640` *(pre-existing VIBE_ name, kept)*

- Feature: Trust Multiplier — pet/mount trust **record lookup**
- Location: RVA `0x1ECB640` / VA `0x141ECB640` / `.code` / `int32_t*(void*, void*)`
- Locator (two independent ones, both exact unique on this build):
  - `kSig_FriendlyGetPet201` (53 tokens) → **1** @ `0x141ECB640`
  - documented 48-byte prologue AOB
    `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 FF DE 4B`
    → **1** @ `0x141ECB640`
- Confidence: **confirmed** (two unique locators land on the same VA; the
  `_L38` suffix records the `0x38` record stride seen in the body).
- Data model: the body loads `[rdx+0x68]`, then `[.. +0x20]`, then a `u16` at
  `+0x30` copied to a stack key before calling the lookup — consistent with the
  `0x38`-stride trust record used by `VIBE_WantedState_Evaluate`'s sibling
  tables.
- Trinity consumer: `src/game/friendly.cpp` — **hook** (the pet/mount getter).
- Update notes: the prologue contains a `call rel32` tail (`E8 FF DE 4B …`) and
  a stack-frame displacement, so the **48-byte form is fragile**. Prefer
  `kSig_FriendlyGetPet201`, and note that both must be re-measured.

#### Other trust/friendship functions in scope

| Function | VA | Role | Confidence |
|---|---|---|---|
| `VIBE_Trust_FindRecordById_L18` | `0x141ECA2D0` | trust record lookup by id | confirmed (pre-existing name) |
| `VIBE_Trust_FindRecordById_L38` | `0x141ECB750` | trust record lookup by id, `0x38` stride | confirmed (pre-existing name) |
| `VIBE_Friendship_GetTierCode` | `0x141EC9F70` | maps a trust value to a friendship tier | confirmed (pre-existing name) |
| `VIBE_FindTargetActorByFriendship` | `0x141ABC170` | finds an actor by friendship, `(registry, outId, actor, requireRowFlag)` | confirmed (pre-existing name) |
| `VIBE_TargetFilter_GetBitIndex` | `0x141ABCDF0` | target-filter bit index | confirmed (pre-existing name) |
| `VIBE_Map_FindOrCreateValueSlot` | `0x140436380` | open-addressed map insert used by the NPC trust setter | confirmed (pre-existing name) |
| `VIBE_Vector68_Grow` | `0x141ECCD40` | grows the `0x68`-byte record vector | confirmed — its **only** callers are the two trust multipliers + `sub_14295DCD0` |
| `VIBE_ScopedRef_Acquire` | `0x140393DF0` | scoped reference acquire | confirmed (pre-existing name) |

### 1.4 Dead / unused trust locators — record, do not "fix"

| Locator | Matches on PE 2944 | Status |
|---|---|---|
| `kSig_FriendlyTrustSiteA` | **0** | unused, no consumer in `src/` |
| `kSig_FriendlyTrustSiteB` | **0** | unused, no consumer in `src/` |
| `kOrig_FriendlyTrustBytes` | — | unused original-byte array |
| `kSig_FriendlyNpcTrustWriter` | **1** @ `0x142B7C6E0` | **resolves but has zero consumers** |
| `kSig_FriendlyAlertDisp` | **0** | unused; the `FactionRelationAlertDispatcher` anchor at `0x142759760` no longer matches |

These five are documentation debt. `kSig_FriendlyNpcTrustWriter` is the
interesting one: it still resolves on PE 2944, so a future update could promote
it to a real locator — but **only** after a dossier records what it does.

### 1.5 Fail-closed behaviour

- The install path hooks each locator independently; a locator that does not
  resolve simply leaves that hook uninstalled.
- `kSig_FriendlySetNpc` / `kSig_FriendlySetPet` (the TU 2.00 fallbacks) have
  **0 matches** on PE 2944, so there is no silent downgrade to an older
  contract — the 2.01+ locators are required.
- Because trust is only ever **raised**, a failed hook degrades to "the
  multiplier does nothing", never to a corrupted value. This is the right
  failure direction and should be preserved.

---

## 2. Worker — Max Worker Level & Skills

### 2.1 The patch

`"Max Worker Level & Skills"` (`src/gui/menu.cpp:134-148`,
`st.workerMaxLevelAndSkills`) is **not** a hook. It is a single reversible
6-byte code patch on the **first grade-selector branch** of a slot-index
resolver.

```text
kSig_WorkerMaxLevelAndSkills
  0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5      (15 tokens)
        |
        v  exactly 1 match in the whole image
  VA 0x14214BE8C   (inside VIBE_GetActiveSlotIndexForRecord @ 0x14214BD30, offset +0x15C)

  ORIGINAL  0F 85 95 00 00 00   =  JNZ +0x95   -> 0x14214BF27
  ENABLED   E9 96 00 00 00 90   =  JMP +0x96 ; NOP  -> 0x14214BF27
```

Both forms jump to the **same destination** `0x14214BF27`; the patch converts a
*conditional* grade test into an *unconditional* jump, so the caller always
resolves the **top grade tier (5)** — which is what unlocks max worker level and
all worker abilities. The sixth byte is padded with `NOP` to keep the 6-byte
footprint ahead of `mov rdi,[rsp+20h]`.

Binary Ninja independently confirms the site:

```asm
0x14214be85  call    VIBE_IdMap_GetGamePlayVariableValue
0x14214be8a  test    al, al
0x14214be8c  jne     0x14214bf27          <-- PATCH SITE (JNZ +0x95)
0x14214be92  mov     rdi, qword [rsp+0x20]     ; 48 8B 7C 24 20
0x14214be97  movzx   edx, r13w                 ; 41 0F B7 D5
0x14214be9b  mov     rcx, qword [rdi+0x68]
0x14214be9f  mov     rcx, qword [rcx+0x168]
0x14214bea6  call    VIBE_IdMap_GetGamePlayVariableValue
0x14214beab  test    al, al
0x14214bead  jne     0x14214bf38
```

Only the **first** branch is patched; the second identical lookup at
`0x14214bea6` keeps its own conditional jump. That matches the source comment
"the worker grade-selector's **first** `jne` branch".

> Small correction worth recording: the analysis note for this site described
> the trailing instruction as `movzx edx, r15w`. Binary Ninja decodes it as
> `movzx edx, r13w`, and the byte `41 0F B7 D5` (ModRM `D5` ⇒ `rm = r13`) agrees
> with Binary Ninja. The signature bytes are unaffected; only the register name
> in the prose was wrong.

### 2.2 Function dossier

#### VIBE_GetActiveSlotIndexForRecord — worker patch host  *(pre-existing name, kept)*

- Feature: Max Worker Level & Skills (the patch site lives inside it)
- Snapshot: PE 2944 / SHA-256 `6D348BE9…81FA7` / `CrimsonDesert.exe.bndb`
- Location: RVA `0x214BD30` / VA `0x14214BD30` / `.code` / 608 bytes / 22 basic blocks
  — patch site at function offset **`+0x15C`** (`0x14214BE8C`)
- Locator: `kSig_WorkerMaxLevelAndSkills` (15 tokens,
  `src/game/offsets.h:1849-1850`), which **is the patch site itself** plus 8
  continuation bytes:
  `0F 85 95 00 00 00 48 8B 7C 24 20 41 0F B7 D5`
  → **exactly 1 match in the whole image**, at `0x14214BE8C`, all 12 sections
  scanned. The continuation bytes exist so an unrelated conditional branch
  elsewhere can never be selected. Live patch state: runtime-controlled.
- Confidence: **confirmed**
- ABI: not exercised by Trinity — no hook, no call. The containing function's
  prototype is `int32_t(void* ctx, int64_t recordKey)`; Trinity only reads and
  rewrites 6 bytes inside its body.
- Data model: the patched branch is a grade/slot selector. The taken path
  continues at `0x14214BF27`. Two `VIBE_IdMap_GetGamePlayVariableValue`
  (`0x1417E6CB0`) lookups gate it.
- Call flow: `VIBE_IdMap_GetGamePlayVariableValue` on both sides of the patched
  branch; the not-taken path re-runs the same lookup with its own `jne`.
- Trinity consumer: `src/game/worker.cpp` — `Worker::Install` (`:42-100`),
  `Worker::SetEnabled` (`:144-194`), `Worker::Shutdown` restore (`:111-127`);
  revision gate `src/game/worker_logic.h:18-21`; byte arrays
  `src/game/offsets.h:1841-1845`. Role: **reversible byte patch** (the only
  `mem::PatchMemory` user in the whole mod).
- Static proof: unique 15-byte AOB landing exactly on the patch site; database
  disassembly showing `jne 0x14214bf27` followed by exactly the recorded
  continuation bytes; the two forms (`JNZ +0x95` and `JMP +0x96`) provably reach
  the same target from the same address.
- Live proof: the patch site was verified unique on 2026-09-19 and the user
  validated max worker level and skills in game. The stricter safety gate from
  the PE 2944 plan (re-proving the branch on this build) was **not** formally
  ticked — see §4.
- OFF/restore or fail-safe result: `Worker::Shutdown` writes
  `kWorkerPatchOriginal` back **only if** the current bytes equal
  `kWorkerPatchEnabled`. If the bytes are neither original nor enabled it logs
  `worker: target at %p contains unexpected bytes; feature disabled.` and leaves
  memory untouched. Nothing is patched unless the AOB matches exactly once
  (`FindAllMatches(sig, 2)`, `worker.cpp:57-63`) **and** the 6 current bytes are
  one of the two known states. Every transition is re-validated by
  `CanTransitionWorkerPatch` (`worker_logic.h:26-45`), which requires
  `size == 6` and exact byte equality.
- Update notes: re-run `kSig_WorkerMaxLevelAndSkills` (expect exactly 1 match).
  Then re-read the 6 bytes at the match — they must be `0F 85 95 00 00 00` or
  `E9 96 00 00 00 90`. Confirm the JNZ target is still `+0x95` and that the
  following instructions are still `48 8B 7C 24 20` / `41 0F B7 D5`.
  **If the branch displacement changes, the patch bytes must change with it —
  never reuse `E9 96 00 00 00 90` blindly.** Also confirm the revision gate
  still covers only PE 2850 and 2944.
- Open questions: the meaning of the two grade tiers on either side of this
  branch was **not** re-derived statically. It is inherited from the
  user-supplied Auto Assembler script documented at `offsets.h:1836-1840`.
- Naming note: the name `VIBE_GetActiveSlotIndexForRecord` is **correct** and
  consistent with the worker patch: a "grade selector" *is* a slot-index
  selector, and forcing the branch makes it always resolve the top tier. The
  name was therefore kept; only the comment was added.

### 2.3 Worker support functions (in scope, not patched)

| Function | VA | Relevance |
|---|---|---|
| `VIBE_SkillTable_GetById` | `0x1403880A0` | skill definition resolver (574 callers) |
| `VIBE_UiList_SumRowSkillValues` | `0x1417862F0` | sums row skill values |
| `VIBE_UiList_BuildSlotEntriesByLoadout` | `0x14042CC40` | builds the worker/gear slot entries the patched selector feeds |
| `VIBE_IdMap_GetGamePlayVariableValue` | `0x1417E6CB0` | the two lookups that gate the patched branch |

### 2.4 Legacy worker paths are forbidden by a test

`tests/verify_worker_feature_contract.ps1:23-36` forbids the legacy worker and
mount keys and routes. Any future change that reintroduces a worker editor or a
second worker hook must fail that test on purpose, not by accident —
`src/game/worker.h:5-8` documents that there is deliberately **no** worker
enumeration or editor API.

---

## 3. Locator evidence (measured over the whole image)

Method: `_analysis/aob_scan2.ps1` reproduces Trinity's scanner against the
on-disk image, all 12 sections.

| Locator | Length | Matches | First VA | Result |
|---|---|---|---|---|
| `kSig_WorkerMaxLevelAndSkills` | 15 tok | **1** | `0x14214BE8C` | exact unique — **is the patch site** |
| `kSig_FriendlySetNpc201` | 25 tok | **1** | `0x141ECB7D0` | exact unique |
| `kSig_FriendlySetPet201` | 44 tok | **1** | `0x14DF6DA50` | exact unique |
| `kSig_FriendlyGetNpc201` | 53 tok | **1** | `0x141ECA1C0` | exact unique |
| `kSig_FriendlyGetPet201` | 53 tok | **1** | `0x141ECB640` | exact unique |
| `kSig_FriendlyNpcTrustWriter` | 32 tok | **1** | `0x142B7C6E0` | resolves, **no consumer** |
| `kSig_FriendlySetNpc` (TU 2.00 fallback) | 20 tok | **0** | — | absent (expected) |
| `kSig_FriendlySetPet` (TU 2.00 fallback) | 20 tok | **0** | — | absent (expected) |
| `kSig_FriendlyTrustSiteA` | 23 tok | **0** | — | dead locator |
| `kSig_FriendlyTrustSiteB` | 23 tok | **0** | — | dead locator |
| `kSig_FriendlyAlertDisp` | 32 tok | **0** | — | dead locator |

---

## 4. Outstanding validation debt

`progress.md` classifies every hook and locator row in this dossier as
**"STATIC READY, not semantic PASS"**. Specifically:

1. **Worker safety gate never formally ticked.** The PE 2944 plan requires that
   "the old `0F 85 95 00 00 00` priority branch remains unpatched unless it
   independently satisfies that proof". The bytes are still the patch site and
   the patch works live, but the proof was never recorded as closed. Closing it
   means: static re-read of the branch and its target (§2.2), plus an in-game
   OFF → ON → OFF cycle with a save/reload.
2. **Trust has no ON/OFF/persistence evidence.** The trust multiplier is
   live-working, but no test records that it survives a reload, that turning it
   OFF restores the game's own rate, or that the 100 cap still holds on this
   revision.
3. **`record+0x20` is the only trust field proven by a live result.** The
   `+0x28` mistake is recorded in the source; a PE update must not repeat it.

---

## 5. Update recipe for the next PE

1. Compute the new EXE SHA-256 and write `../snapshots/PE-<revision>.md` first.
2. **Worker first** (it is the only byte patch and the most destructive if
   wrong): re-run `kSig_WorkerMaxLevelAndSkills`; require exactly 1 match; read
   the 6 bytes; recompute the jump displacement from the disassembly and update
   *both* byte arrays if it changed. Confirm the revision gate covers the new
   revision before enabling.
3. **Trust next:** re-run all four friendly locators. Require exactly 1 match
   each. Confirm the pet/mount setter still shares `VIBE_Vector68_Grow` with the
   NPC setter — that shared callee is the cheapest structural check that the two
   functions are still the trust multipliers.
4. Re-read `record+0x20` in the setter bodies. **Do not** accept `+0x28`.
5. Confirm the TU 2.00 fallbacks (`kSig_FriendlySetNpc` / `kSig_FriendlySetPet`)
   are still absent, so there is no silent downgrade path.
6. Delete or wire up the five dead locators in §1.4 — but only after this
   dossier records what each one does.
7. If the worker AOB is absent or ambiguous, leave the toggle `OFF` and record
   the mismatch; **do not** fall back to the PE 2850 address
   `CrimsonDesert.exe+20967CC`.
8. Runtime test, one feature at a time with an OFF leg:
   - Worker: note a worker's level and skills → ON → confirm max level and all
     abilities → save/reload → confirm persistence → OFF → confirm the values
     return to the game's own.
   - Trust: record the trust gain rate with the multiplier OFF, then ON, then
     OFF; confirm it returns to the original rate and never exceeds 100.
