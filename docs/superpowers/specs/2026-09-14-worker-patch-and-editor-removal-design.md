# Worker Patch and Mount Editor Removal Design

## Goal

Replace the unverified Workers editor with a reversible controller for the
user-supplied PE 2850 AA patch, remove the dedicated Mount/Horse editor, and
remove obsolete Mount/Worker keys from Trinity.ini while preserving unrelated
mount gameplay and persistence features.

## Scope

The new Player feature enables the exact AA behavior supplied by the user:

`0F 85 95 00 00 00` becomes `E9 96 00 00 00 90` at the unique worker
qualification branch in Crimson Desert PE 2850. The patch is applied only when
the detected revision is PE 2850, the exact surrounding signature has one
match, and the target bytes are in an expected state. It is restored on toggle
off and on Trinity shutdown.

The old worker observer, worker enumeration, guessed object layout, direct
memory setters, bulk/template actions, and Worker/Mercenary menu are removed.
The old Mount & Horse menu and mount-specific dye editing are removed. Player
dye, Inventory dye, player Equipment profiles, Inventory mount-item browsing,
Infinite Stamina & Mount, Player mount tracking, movement, and Free Flight stay
in the product.

## Architecture

`Worker` becomes a small byte-patch controller. `Install()` validates the
current revision and resolves one exact signature. `SetEnabled(bool)` performs
an expected-state transition using guarded instruction patching. `Enabled()`
reports the controller's applied state and `Remove()` restores the original
bytes only when the target still contains Trinity's patched bytes. No MinHook,
worker object reads, enumeration, or native worker calls remain.

The persisted state contains one new boolean, `workerMaxLevelAndSkills`,
defaulting to false. Settings Load/Save recognizes and writes this key only;
the legacy worker keys are ignored on load and omitted on save. The new Player
toggle persists through the existing Auto Save mechanism. If a loaded enabled
state cannot be applied after readiness, the state is reset to false and the
failure is logged.

Mount editor cleanup is restricted to `Dye`'s mount target mode, mount
component resolution/capture, mount dye cache records, and mount apply/restore
branches. Shared Player mount tracking and all non-editor consumers remain.

## Error handling and safety

- Unknown revisions, missing signatures, ambiguous matches, invalid memory, or
  unexpected target bytes fail closed and never write instructions.
- Enabling an already-patched target and disabling an already-original target
  are idempotent success cases.
- A failed enable reverts the UI state to false; a failed disable leaves the
  state enabled and reports the failure.
- Shutdown attempts restoration only for the exact bytes written by Trinity;
  it does not overwrite an unexpected third-party or game state.
- Existing Trinity.ini files containing removed worker keys remain readable for
  other settings, but those removed keys are not imported or regenerated.

## Verification

Pure helper tests cover revision support, exact original/patched bytes,
idempotent transitions, and fail-closed unexpected-byte handling. Static checks
confirm removed UI routes, legacy Worker APIs, legacy state fields, and
obsolete worker settings output are absent. `git diff --check`, x64 Release
build, and Release CTest are required. Build/test evidence is kept separate
from in-game semantic evidence; the latter requires a fresh launch and manual
confirmation that all workers show maximum level and unlocked abilities.
