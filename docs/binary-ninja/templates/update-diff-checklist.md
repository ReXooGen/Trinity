# Update diff checklist

Run this for every new Crimson Desert executable, before any Trinity source edit
or deployment. Work per feature dossier; do not sweep the whole executable.

## 0. Freeze the new snapshot first

- [ ] Copy the executable somewhere stable and hash it (`Get-FileHash -Algorithm SHA256`).
- [ ] Record filename, full path, SHA-256, FileVersion (revision), image base,
      image size, checksum, TimeDateStamp, section table in
      `docs/binary-ninja/snapshots/PE-<revision>.md`.
- [ ] Open it in Binary Ninja, save a new versioned `.bndb`, and record that
      database path in the same snapshot file.
- [ ] Note the active view handle (the PE view, not the `Raw` view).
- [ ] Re-check the section table against Trinity's section filter: does any
      section named exactly `.debug` now exist, and is it executable? That
      changes which bytes a locator can match.

## 1. Re-run every recorded locator

For each function row in each dossier:

- [ ] Run the recorded AOB / predicate over the whole image and record the match
      count. Classify: **exact unique**, **moved but structurally equivalent**,
      **ambiguous**, or **absent**.
- [ ] Use `_analysis/aob_scan2.ps1` (wildcard-capable) — **not** `aob_scan.ps1`,
      which cannot express `?` and will tempt you into truncating a wildcard
      signature into an ambiguous prefix.
- [ ] Re-run `_analysis/extract_sigs.ps1` against the new `offsets.h` and
      `_analysis/extract_inline_aobs.ps1` against the equipment / dye / teleport
      / inventory sources, so **every** locator gets a fresh measured count in
      one consistent pass rather than a hand-picked subset.
- [ ] Exact unique: compare prologue bytes, ABI setup, important offsets, branch
      semantics, and xrefs before re-enabling the feature.
- [ ] Moved equivalent: update the locator **only** if the dossier contract still
      agrees; otherwise treat as absent.
- [ ] Ambiguous: never bind on a first match. Resolve through the documented
      neighbour/UI/table anchor.
- [ ] Absent: mark the feature unavailable and keep it fail-closed. Never inherit
      a legacy or pre-<revision> fallback into an unverified build.
- [ ] **Check whether the locator is even consulted.** Several locators resolve
      and are then gated off, force-nulled, or hard-disabled. A green match is
      not a working feature. The PE 2944 examples are
      `kSig_MarkerProtection` (resolves, hook hard-disabled),
      `kSig_DyeApplySlot` + the three dye visual leaves (force-nulled for
      `revision >= 2625`), and `kSig_FriendlyNpcTrustWriter` (resolves, zero
      consumers).

## 2. Re-verify the ABI, not just the address

A signature can survive while the contract changes. For every confirmed function:

- [ ] Argument registers in the same order (which register actually carries
      sceneId/nodeIndex/handle) — read the prologue, not the old comment.
- [ ] Return register and its truthiness.
- [ ] Stack argument offsets and any XMM inputs.
- [ ] Null-argument safety: does any argument get dereferenced now that did not
      before? Trinity passes `nullptr` for the fast-travel context.
- [ ] Caller call sites: which mode/flags does the game itself pass, and does
      Trinity still match the ordinary path?

## 3. Re-verify data offsets

- [ ] Structure offset and stride constants (for example scene `nodeCount`,
      `nodeArray`, node stride) are re-read in the new build.
- [ ] Offsets only promoted to a named constant when seen in more than one
      relevant function.
- [ ] A moved field is recorded as a PE-specific mapping, not silently rewritten.

## 4. Close the feature

- [ ] Trinity source comments, tests, the Binary Ninja comment, and the dossier
      tell the same story.
- [ ] Add a RED test for each changed contract, then the smallest
      revision-specific change; run the full CTest suite.
- [ ] Confirm the OFF/restore or fail-closed path for the feature.
- [ ] Visible in-game test performed and recorded in the dossier's `Live proof`.
- [ ] Deploy only with the game closed: hash the old ASI, create a timestamped
      backup, copy the verified build, compare the installed hash, then
      fresh-launch test.

## 5. Record what changed

- [ ] Append a change summary to the snapshot file.
- [ ] Update each affected dossier's `Update notes` and `Open questions`.
- [ ] If a contract differed from the previous snapshot, say so explicitly and
      leave the feature fail-closed until it is re-proven.
