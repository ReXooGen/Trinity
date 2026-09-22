# Function dossier template

Copy this block once per important game function. Keep every field, even when
the answer is "unknown" — an explicit unknown is what stops a future update from
inventing a contract.

```md
### VIBE_<Feature>_<Role>

- Feature: <visible Trinity menu feature>
- Snapshot: PE revision / EXE SHA-256 / BNDB
- Location: RVA / VA / section / function length / basic-block count
- Locator: exact AOB or predicate; clean match count; live hook state
- Confidence: confirmed | strong candidate | candidate
- ABI: RCX=; RDX=; R8=; R9=; stack=; XMM=; return=
- Data model: object offsets, arrays/strides, enums, ownership
- Call flow: relevant callers -> this function -> relevant callees
- Trinity consumer: source file, source symbol, and hook/native-call/patch role
- Static proof:
- Live proof:
- OFF/restore or fail-safe result:
- Update notes: which bytes/offsets are revision-specific and what to re-find
- Open questions:
```

## Rules for filling it in

1. **Two independent evidence sources before a `VIBE_` name.** Acceptable
   sources: shipping source AOB/RVA, xrefs/call flow, decompilation, structure
   access, UI path, or a safe live result. One source is a hypothesis.
2. **If a field is not proven, keep the original `sub_*` name** and say so in
   `Confidence` and `Open questions`. Never invent semantics.
3. **Record both RVA and VA.** RVA survives a rebase; VA is what the current
   database shows.
4. **Separate static from live evidence.** A decompilation is not a live test.
5. **Never use breakpoints, injected code, byte patches, or a direct game call
   to obtain evidence.** Crashes are not evidence.
6. **Number bases are never converted by hand.** Use the Binary Ninja MCP
   (`convert_number` where available, otherwise a tool-side formatter) and
   record the tool result.
7. **A locator is only durable if it was measured.** State the match count over
   the whole image, not just at the expected address.

## Locator quality ladder

| Quality | Meaning | Use |
|---|---|---|
| Exact unique AOB | 1 match in the whole image | Preferred; verify prologue + ABI after a PE change |
| Structural predicate | string/xref/table anchor with a documented walk | Use when the AOB matches many siblings |
| Ambiguous AOB | >1 match | Reject for binding; keep as a hint only |
| Absent | 0 matches | Feature must stay fail-closed |

## Measuring a locator (do this, do not estimate)

Three helpers under `_analysis/` reproduce Trinity's own scanner semantics
against the on-disk image, so a recorded match count is what the deployed ASI
will actually see:

| Script | Purpose |
|---|---|
| `_analysis/aob_scan.ps1` | exact byte patterns; PE-section walk, `[VA, VA+VirtualSize)` clamped to the raw bytes present, skipping only a section named exactly `.debug` without `IMAGE_SCN_MEM_EXECUTE` |
| `_analysis/aob_scan2.ps1` | **wildcard-capable** (`?` / `??`); use this for any signature containing wildcards |
| `_analysis/extract_sigs.ps1` | pulls every `kSig_*` constant out of `src/game/offsets.h`, handling adjacent string-literal concatenation |
| `_analysis/extract_inline_aobs.ps1` | pulls byte-pattern literals written directly at a call site (no `kSig_` constant) out of a `.cpp` file |
| `_analysis/bn.ps1`, `_analysis/bnbatch.ps1` | Binary Ninja MCP access (see "Access" below) |

Rules that follow from this:

1. **A wildcard signature must be measured with `aob_scan2.ps1`.** `aob_scan.ps1`
   throws on wildcards, and truncating a wildcard signature to an exact prefix
   can silently turn a unique locator into a 400-match one
   (`kSig_MoveUpdate` is exactly this trap).
2. **Report the match count for the whole signature, and for any prefix you
   were tempted to shorten it to.** Both numbers belong in the dossier.
3. **A signature that begins part-way into a function is normal.** Record the
   function offset (for example "function `+0x5`") so a future pass does not
   mistake the match address for a function start.
4. **A resolved locator is not a working feature.** Record the match count, the
   resolved address, *and* whether the consuming code actually installs,
   patches, or calls it. Several locators in this database resolve and are then
   deliberately nulled or gated off.

## Access

```text
url           = http://127.0.0.1:24642/mcp      (Binary Ninja WARP MCP)
authorization = disabled
helper        = pwsh -File _analysis/bn.ps1 -Tool <tool> -ArgsJson '<json>'
batch         = pwsh -File _analysis/bnbatch.ps1 -SpecFile <spec.json>
```

Files opened in the Binary Ninja UI are not automatically active for MCP: call
`bn_binary_view_list`, then `bn_binary_view_set_active` with the PE view handle
(never the `Raw` view).

If the harness-provided `mcp__binaryninja__*` tools fail with WinError 10061
while port 24642 is listening, the WARP endpoint is still healthy — use the
`_analysis/bn*.ps1` helpers above, which talk to it directly.
