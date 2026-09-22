# Trinity Crash Diagnostics Design

**Date:** 2026-09-22
**Status:** Approved design, implementation pending

## Goal

Produce enough evidence after a fatal Crimson Desert crash to distinguish a direct Trinity fault, likely prior corruption by Trinity, a game or driver fault, and an inconclusive result. Keep normal-game overhead low and keep each dump in the approximate 50–300 MB range.

## Scope

The diagnostic system will:

- capture only terminal unhandled exceptions, never handled first-chance exceptions;
- retain a fixed-size pre-crash breadcrumb history;
- write one timestamped text report and one diagnostic minidump per fatal crash;
- preserve the three newest crash bundles and remove older bundles only during a later clean startup;
- assign an evidence-based attribution category without claiming certainty from the faulting module alone.

It will not attempt to recover from a fatal exception, suppress Windows Error Reporting, create full-memory dumps, upload files, or claim that logging fixes the underlying crash.

## Architecture

### Crash diagnostics module

Move crash-reporting responsibilities out of `dllmain.cpp` into a focused `core/crash_diagnostics` module. `dllmain.cpp` will only register the top-level unhandled-exception filter and initialize the diagnostic session.

The module owns:

- a preallocated, fixed-capacity ring buffer of 512 breadcrumb records;
- immutable session identity captured during normal startup;
- the guarded terminal crash writer;
- startup-time retention cleanup;
- attribution classification.

No allocation, locking, hashing, directory enumeration, or cleanup will be initiated from the fatal exception path. Data needed at crash time must already be held in fixed buffers or supplied by the exception context.

### Breadcrumb producers

Instrumentation will record only state transitions and bounded diagnostic events:

- feature ON/OFF and value changes;
- hook installation/removal, target address, trampoline address, and result;
- reversible patch transitions and verified original/current bytes;
- identity changes for important runtime objects such as player, movement owner, inventory holder, and physics body;
- mutating operation summaries: feature, target address range, byte count, result, and thread ID;
- queued and completed high-risk actions such as inventory, equipment, teleport, trust, worker, and world mutations;
- warnings and failed safety gates.

High-frequency operations such as stat pinning will be rate-limited or deduplicated. Raw item data, save data, credentials, chat text, and arbitrary memory contents will not be written to the text log.

## Crash bundle

Each fatal crash creates files sharing this base name:

`Trinity_Crash_YYYYMMDD-HHMMSS_PID`

### Text report

The `.txt` report contains:

- timestamp, process/thread ID, game uptime, Trinity version/build time;
- executable and Trinity module base, size, version, and startup SHA-256;
- exception record, access type/address, complete x64 register context;
- fault module and RVA;
- faulting-thread stack frames available in-process;
- complete feature-state snapshot;
- resolved hook/patch contract snapshot;
- the chronological breadcrumb ring with sequence numbers and relative timestamps;
- dump-write success/failure and selected dump flags;
- attribution category, evidence supporting it, and explicit limitations.

### Diagnostic minidump

Use `MiniDumpWriteDump` with a bounded diagnostic flag set:

- `MiniDumpWithThreadInfo`;
- `MiniDumpWithUnloadedModules`;
- `MiniDumpWithHandleData`;
- `MiniDumpWithIndirectlyReferencedMemory`;
- `MiniDumpWithDataSegs`;
- `MiniDumpWithProcessThreadData`;
- `MiniDumpWithFullMemoryInfo`;
- `MiniDumpIgnoreInaccessibleMemory`.

Do not use `MiniDumpWithFullMemory` or `MiniDumpWithPrivateReadWriteMemory`. If the diagnostic dump exceeds the intended range in live use, reduce optional memory flags rather than silently switching to a full dump.

## Attribution model

The report emits one of four categories:

- `TRINITY_DIRECT`: fault RIP is inside Trinity or a verified Trinity-owned trampoline/stub.
- `TRINITY_SUSPECTED`: the fault is outside Trinity, but recent breadcrumbs show a Trinity mutation overlapping the faulting object/range or a failed contract immediately precedes it.
- `GAME_OR_DRIVER`: the fault and usable stack remain in game/driver code and no recent Trinity mutation overlaps the implicated range.
- `INCONCLUSIVE`: evidence is missing, contradictory, or insufficient.

`GAME_OR_DRIVER` is not proof that Trinity is innocent; it means the captured evidence contains no direct Trinity link. Symbol names from export-only resolution are informational and must not be treated as authoritative function identities.

## Retention and failure handling

- Keep the three newest complete bundles.
- Perform pruning only on the next clean startup, never inside the crash handler.
- Never overwrite an existing timestamped bundle.
- Use an interlocked recursion guard so a failure inside reporting does not recurse.
- If text generation fails, still attempt the dump; if dump generation fails, retain the text report with the Windows error code.
- Preserve and chain the previously installed top-level exception filter.

## Performance and safety

- Ring-buffer writes are fixed-size and lock-free or use a non-blocking atomic sequence protocol.
- No per-frame disk I/O is added.
- Repeated identical breadcrumbs are coalesced with a count and latest timestamp.
- Addresses are recorded as module-relative RVAs when possible and as raw addresses when needed for same-process dump analysis.
- Crash-time feature state must come from a previously captured snapshot, not unsynchronized traversal of live game objects.

## Testing

Automated tests will verify:

- first-chance exceptions cannot enter the terminal writer;
- ring ordering, wraparound, deduplication, and concurrent publication;
- attribution decisions for direct, suspected, external, and inconclusive evidence;
- dump flags exclude full/private memory and include the required diagnostic data;
- filenames are unique and deterministic from timestamp/PID;
- retention keeps exactly the newest three bundles;
- recursion guard and report/dump fallback behavior;
- existing crash-reporting contract tests remain green.

Verification requires a Release build and the complete CTest suite. Live proof remains separate: a controlled synthetic crash must produce a readable bundle, and a real game crash is required before claiming that attribution is operational in Crimson Desert.

## Acceptance criteria

- A handled first-chance exception produces no crash bundle.
- A terminal synthetic exception produces one timestamped `.txt` and `.dmp` bundle.
- WinDbg can enumerate threads/modules and inspect indirectly referenced memory from the dump.
- The report includes the last relevant Trinity mutations and an evidence-backed attribution category.
- Normal gameplay produces no crash-log spam and no measurable per-frame disk activity.
- Existing user-owned changes and the installed ASI remain untouched until a separately authorized deployment.
