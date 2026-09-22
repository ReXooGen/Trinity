# PE 2949 live-session runbook

Handoff for the owner. Everything here is copy-pasteable. The static audit is complete
(`pe2949-a9e5ca20.md`); this is the part that needs hands on the keyboard.

**Audited artifact**: `build-clean/Trinity.asi`
size `1940480`, SHA-256 `8B2318970C21577F4621805F2DE921EB6CCEF70DDFE56DBD57C122FD0E05F9A2`,
embedded build stamp `Sep 21 2026 19:20:07`.

**Current install**: `Trinity.asi` is renamed to `Trinity.asi123` (mod deliberately disabled).
`CrimsonDesert.exe` was running when this runbook was written.

---

## Step 0 — before you start

Close `CrimsonDesert.exe` completely (check Task Manager; the ASI loader `winmm.dll` is loaded into
the game process, so the file stays locked until the process is gone).

```powershell
Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Select-Object Id,ProcessName
```

Expect no output. If a process is listed, close it before continuing.

---

## Step 1 — backup (skip if `pe2949-predeploy-backup/` is enough for you)

A recoverable backup already exists at `docs/audits/pe2949-predeploy-backup/` with
`SHA256SUMS-audit-backup.txt`:

| File | SHA-256 |
| --- | --- |
| `Trinity.asi123` (the pre-audit install) | `39FD38CE11AF983591D3011C8405EFDEFC7C273E129A1E561C5580BA56F6FD3B` |
| `Trinity.ini` | `71F2B653ADA73D150DC259E66690A5A1E457748050CEFD71157DDF346B92172E` |
| `winmm.dll` | `5DC7D6695E509948DF5CD39B780B132D9AB0CA001287803350DB282B7214DA28` |

To make a second, in-place backup next to the game:

```powershell
$g  = 'E:\Steam\steamapps\common\Crimson Desert\bin64'
$ts = (Get-Date).ToString('yyyyMMdd-HHmmss')
Copy-Item "$g\Trinity.asi123" "$g\Trinity.asi.backup-audit-$ts" -Force
```

---

## Step 2 — deploy and hash-compare

```powershell
$repo = 'C:\Users\mul0\Documents\GitHub\Trinity'
$g    = 'E:\Steam\steamapps\common\Crimson Desert\bin64'

Copy-Item "$repo\build-clean\Trinity.asi" "$g\Trinity.asi" -Force

$built     = (Get-FileHash "$repo\build-clean\Trinity.asi" -Algorithm SHA256).Hash
$installed = (Get-FileHash "$g\Trinity.asi" -Algorithm SHA256).Hash
"built     : $built"
"installed : $installed"
if ($built -eq $installed -and $built -eq '8B2318970C21577F4621805F2DE921EB6CCEF70DDFE56DBD57C122FD0E05F9A2') {
    'DEPLOY OK'
} else {
    'DEPLOY MISMATCH - do not launch, restore the backup'
}
```

Expected: `DEPLOY OK`.

> The game is now un-disabled: `bin64\Trinity.asi` exists again, so the mod will load on next launch.
> If you want the old behaviour back, copy `Trinity.asi123` over `Trinity.asi` and delete it.

---

## Step 3 — fresh launch and startup log

Launch the game normally, wait for the main menu, then check the log:

```powershell
$g = 'E:\Steam\steamapps\common\Crimson Desert\bin64'
Get-Content "$g\Trinity.log" -Tail 40
```

Confirm these lines (values in `<>` come from your build):

| Expect | Why it matters |
| --- | --- |
| `Trinity v<...> initializing (built Sep 21 2026 19:20:07)` | the stamp matches the deployed artifact, so the log is evidence about *this* binary |
| `Game version detected: Crimson Desert 2.03.01 (Active) [PE: 1.0.0.2949]` | revision gate selected the audited path |
| `Gameplay code ready - installing feature hooks.` | all 7 readiness sentinels resolved |
| `player: char-manager successfully resolved (N anchors verified)` | **report N** — the audit measured 1 of 6 (blocker B1) |
| `player: damage-apply hook installed @ 00000001417AE110` | matches audited RVA `0x17AE110` |
| `teleport: movement-update hook installed @ 0000000144282090` | RVA `0x4282090` |
| `inventory: PE 2949 native slot-expansion setter resolved @ 0000000142135850` | RVA `0x2135850` |
| `inventory: PE 2949 Slot Size pickup branch resolved @ 0000000142407824` | RVA `0x2407824` |
| `equipment: EquipEffectRefresh resolved @ ...` | **check which RVA** — blocker B2, the AOB matches twice |
| `teleport: map marker ... hooks=5/5` | expected; `5` is `kExpected_MarkerMatches` and equals the `kSig_MarkerPattern` match count. A `0/5` here is a **MinHook trampoline-allocation failure**, not a missing signature — re-test in a second session before treating it as a contract problem |
| `Ready - INSERT (or LB + DOWN on controller) toggles the menu in-game.` | overlay + input live |

Also note whether a **new** entry appears in `Trinity_Crash.txt`. The 2026-09-21 crashes were
attributed to an **NVIDIA driver fault** (2-3 FPS stall in rare dark scenes, then a crash) and are
**not** a mod defect — see R5. Only a crash with a *repeatable* fault address needs investigating.

---

## Step 4 — live checklist

Do these in order. The first three target the audit's blockers.

### Priority 1 — Player / Combat (blocker B1)

| # | Action | Record |
| --- | --- | --- |
| 1 | Enable **God Mode**, take damage | effect on? yes/no |
| 2 | Disable God Mode, take damage | damage returns? yes/no |
| 3 | Enable **Infinite Stamina** and **Infinite Spirit**, sprint / use spirit | effect? yes/no |
| 4 | Disable each | resource drains again? yes/no |
| 5 | **Switch character**, repeat 1-2 | still correct character? yes/no |
| 6 | **Load a save**, repeat 1-2 | still correct character? yes/no — *does it edit any NPC/companion?* |
| 7 | Keep a companion/NPC in view during 1-6 | any NPC affected? yes/no |

### Priority 2 — Equipment (blocker B2)

| # | Action | Record |
| --- | --- | --- |
| 8 | Open **Edit Equipment**, refine one gear piece | stat changes on screen? |
| 9 | Socket an abyss gear | socket fills? |
| 10 | **Swap** equipment | swap applies? |
| 11 | Equip / unequip, then save + reload | survived reload? |
| 12 | Log line for `EquipEffectRefresh` | which RVA? same on two runs? |

### Priority 3 — Inventory transaction domain

| # | Action | Record |
| --- | --- | --- |
| 13 | **Add Item** — one item | appears? quantity correct? |
| 14 | **Add Item** — bulk / Add All in a category | appears? |
| 15 | Add **money** | spendable balance changes? |
| 16 | Edit a stack quantity, then **use / equip / sell / discard / buy** | every transaction behaves? |
| 17 | Save + reload | edit persists? |
| 18 | Try an item you cannot hold (bad id) | refused with an error, **no ghost item**? |

### Priority 4 — Slot Size (the plan's explicit OFF-restoration contract)

| # | Action | Record |
| --- | --- | --- |
| 19 | Enable **Slot Size** = `1` | capacity shown = 1? |
| 20 | Set = vanilla value | capacity = vanilla? |
| 21 | Set = `700` | real capacity = 700 (not just the grid)? |
| 22 | Pick an item up **while full** | pickup succeeds? |
| 23 | Sell / discard / vendor transaction | all normal? |
| 24 | Save + reload | value persists? |
| 25 | **Disable Slot Size** | capacity returns to vanilla everywhere? |
| 26 | Pick up again after disable | normal full-inventory behaviour? |

### Priority 5 — Travel

| # | Action | Record |
| --- | --- | --- |
| 27 | Open the map, create a marker, teleport to it | did you arrive (direct-reader path)? |
| 28 | Move the marker, teleport again | new position? |
| 29 | Remove the marker, teleport | fails safely? |
| 30 | Marker in unreachable terrain | protection / height fallback? |
| 31 | After an OFF / cancel, walk around | no residual movement? |
| 32 | **Fast Travel** — each category, one node | travels exactly once? |
| 33 | Fast Travel cancel / back | returns cleanly? |
| 34 | **Super Run** / **Free Flight** on and off | velocity + locomotion restored on OFF? |
| 35 | Saved locations: save / load / delete a slot | round-trips? |

### Priority 6 — World

| # | Action | Record |
| --- | --- | --- |
| 36 | **Game Speed** 1x .. 5x | applies? |
| 37 | **Time freeze** on/off, day transition | clock freezes / resumes? |
| 38 | Time preset | clock jumps to preset? |
| 39 | Save + reload | time state sane? |
| 40 | Weather: rain / snow / dust / wind / clear / fog | each applies? |
| 41 | Zone transition with weather forced ON | survives the transition? |
| 42 | Turn weather OFF | engine regains control (no frozen values)? |

### Priority 7 — Worker and Trust

| # | Action | Record |
| --- | --- | --- |
| 43 | **Max Worker Level & Skills** on | one worker maxed? |
| 44 | Check other workers | all maxed? |
| 45 | Save + reload, check UI + quest progression | consistent? |
| 46 | Disable | level/abilities revert? |
| 47 | **Trust Multiplier** on, gift/feed/tame an **NPC** | multiplier applies? |
| 48 | Same for a **pet/mount** (separately!) | applies? |
| 49 | Check relationship UI, reload | value sticks? |
| 50 | Disable | ordinary behaviour returns, **no cross-target change**? |

### Priority 8 — Bootstrap, overlay, input, persistence

| # | Action | Record |
| --- | --- | --- |
| 51 | **INSERT** opens/closes the menu | yes/no |
| 52 | **LB + DOWN** on controller opens/closes | yes/no |
| 53 | Navigate every tab and every sub-page with keyboard | any dead route? |
| 54 | Repeat with controller | any dead route? |
| 55 | Navigate with the game's **inventory/map open** | input routing OK? (record separately from gameplay-hook failures) |
| 56 | Change a value with the arrow keys / d-pad | applies? |
| 57 | Type in a search box; back out | typing + focus restored? |
| 58 | Change **menu scale / font / language** | applies and survives restart? |
| 59 | **Keybind** rebinding, then use the new key | works? |
| 60 | **Reset defaults** and **autosave** | both behave? |
| 61 | **FPS counter / console** toggles | persist across restart? |

---

## Step 5 — report back

Send me, and I will finalise the matrix from `STATIC ONLY` → `PASS`/`FAILED`:

1. The `Trinity.log` from the session (or at least the lines in Step 3).
2. The checklist above with any `no` / unexpected result.
3. Whether `Trinity_Crash.txt` gained an entry, and if so its timestamp + fault address.
4. Your save state (new game / which chapter) and the date, for the record.

I can then re-run `python tools/audit/pe2949_audit.py scan` to prove the binary under test is
unchanged, and update `pe2949-a9e5ca20.md` §7 plus `pe2949-final-verification.md` §5-§6 with live
evidence instead of `STATIC ONLY`.

---

## Rollback

```powershell
$repo = 'C:\Users\mul0\Documents\GitHub\Trinity'
$g    = 'E:\Steam\steamapps\common\Crimson Desert\bin64'
$ts   = (Get-ChildItem "$g\Trinity.asi.backup-audit-*" | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
if ($ts) { Copy-Item $ts "$g\Trinity.asi" -Force; "restored from $ts" }
# or fully disable the mod again:
# Rename-Item "$g\Trinity.asi" "$g\Trinity.asi123" -Force
```

---

## Known issues to expect (from the static audit — not your fault if you hit them)

| ID | Symptom to watch for |
| --- | --- |
| B1 | character manager resolved from a single anchor — stat edits may target the wrong object |
| B2 | equipment refresh resolved by first-hit from 2 candidates — refine/socket may silently no-op |
| B3 | dye routes are unreachable; the dye UI cannot be opened at all (expected, not a bug you can work around) |
| R1 | inventory list may appear empty until the game itself queries item counts |
| R2 | some weather paths use fallbacks because `EnvManager` is missing |
| R4 | the combat-timing hook is installed but its detour is inert |
| R5 | **resolved — not the mod.** The 2026-09-21 crashes were attributed by the owner to an NVIDIA driver fault (2-3 FPS stall in rare dark scenes, then a crash). Ignore them; only a crash with a *repeatable* fault address would need investigating |
