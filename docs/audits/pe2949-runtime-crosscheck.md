# PE 2949 runtime cross-check (read-only)

Purpose: prove that the RVA/VA column recorded in `pe2949-a9e5ca20.md` still
describes the *running* image, not just the file on disk.

Method (no process state is modified):

1. `python tools/audit/pe2949_audit.py scan ...` records, for every unique
   signature match, the on-disk RVA and the exact fixed bytes of the pattern.
2. `python tools/audit/pe2949_audit.py runtime-cases ...` emits
   `pe2949-runtime-check.lua`.
3. Cheat Engine (attached to a running, **unmodded** `CrimsonDesert.exe`)
   executes that Lua script. The script reads `getAddress("CrimsonDesert.exe")`,
   then for each case reads `len(pattern)` bytes at `base + RVA` and compares
   every non-wildcard position against the audited file bytes.

Environment at execution time:

| Item | Value |
| --- | --- |
| Game PID | 2476 |
| Mod state | `Trinity.asi` renamed to `Trinity.asi123`; not loaded |
| CE bridge | v12.0.0 |
| Image base (live) | `0x140000000` (equals the on-disk `ImageBase`) |
| Cases checked | 44 |
| Result | **44 ok / 0 bad / 0 read failures** |

Raw bridge output:

```text
RUNTIME CROSSCHECK: cases=44 ok=44 bad=0
```

Scope of the 44 cases: every `kSig_*` entry whose on-disk match count is
exactly 1, every `kCharMgrAnchors[i]` whose match count is exactly 1, and the
`kStr_*` table-name anchors. Multi-match and zero-match entries are excluded by
construction, because there is no single address to compare.

Interpretation and limits:

* This proves the on-disk RVA -> live VA mapping (`VA = ImageBase + RVA`), and
  that no code section is relocated or decrypted differently in memory.
* It does **not** prove any semantic behaviour (ON effect, OFF restoration,
  persistence). Those require the live in-game cases listed in
  `pe2949-a9e5ca20.md`.
* The game was running **without** the mod during the check, so the bytes are
  the engine's own; nothing in this evidence can be an artefact of our hooks.
