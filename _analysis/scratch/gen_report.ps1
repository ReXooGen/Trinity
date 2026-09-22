# gen_report.ps1 - build existing-vibe-annotations.md from captured MCP raw output.
$ErrorActionPreference = 'Stop'
$scratch = 'C:\Users\mul0\Documents\GitHub\Trinity\_analysis\scratch'
$outFile = 'C:\Users\mul0\Documents\GitHub\Trinity\_analysis\inventory\existing-vibe-annotations.md'

function Norm([string]$a) { '0x' + $a.ToUpper().Replace('0X','') }

# ---- load function table ----
$funcs = Get-Content (Join-Path $scratch 'funcs.json') -Raw | ConvertFrom-Json

# ---- load prototypes ----
$protoMap = @{}
$ptxt = Get-Content (Join-Path $scratch 'protos_raw.txt') -Raw
foreach ($s in ([regex]::Split($ptxt, '(?m)^########## ') | Where-Object { $_.Trim() })) {
    $ls = $s -split "`r?`n"
    $jl = $ls | Where-Object { $_.TrimStart().StartsWith('{') } | Select-Object -First 1
    if (-not $jl) { continue }
    $o = $jl | ConvertFrom-Json
    $protoMap[$o.prototype.function.name] = $o.prototype.definition
}

# ---- load comments ----
$commentMap = @{}
$ctxt = Get-Content (Join-Path $scratch 'comments_raw.txt') -Raw
foreach ($s in ([regex]::Split($ctxt, '(?m)^########## ') | Where-Object { $_.Trim() })) {
    $ls = $s -split "`r?`n"
    $label = ($ls[0] -replace '\s+\(bn_comment_get\)','').Trim() -replace '^\[\d+\]\s*',''
    $jl = $ls | Where-Object { $_.TrimStart().StartsWith('{') } | Select-Object -First 1
    if (-not $jl) { continue }
    $o = $jl | ConvertFrom-Json
    $commentMap[$label] = $o.text
}

# ---- theme map ----
$themeMap = @{}
$groups = @{
  'Table resolvers' = @(
    'VIBE_SkillTable_GetById','j_VIBE_ItemInfoTable_GetById','j_VIBE_MercenaryInfoTable_GetRecordById',
    'VIBE_CharacterInfoTable_GetRecordById','VIBE_ItemInfoTable_GetById','VIBE_FactionNodeTable_GetById',
    'VIBE_FieldInfoTable_GetById','VIBE_LevelGimmickSceneObjectInfoTable_GetById','VIBE_StatusInfoTable_GetById',
    'VIBE_QuestGaugeInfoTable_GetById','VIBE_StatusGroupInfoTable_GetById','VIBE_BuffInfoTable_GetById',
    'VIBE_GamePlayVariableInfoTable_GetById','VIBE_MercenaryInfoTable_GetRecordById')
  'CharacterStatus' = @(
    'VIBE_CharacterStatus_RebuildSlotValues','VIBE_CharacterStatus_ApplyDeltaPrimary','VIBE_CharacterStatus_GetAccumulatedValue',
    'VIBE_CharacterStatus_GetGroupSlotEntry','VIBE_CharacterStatus_ApplyDelta','VIBE_CharacterStatus_IsStatusGroupActive',
    'VIBE_CharacterStatus_AccumulateSlotValue','VIBE_CharacterStatus_ComputeSlotEntries','j_VIBE_CharacterStatus_CollectRowEntries',
    'VIBE_CharacterStatus_GetRowField18','VIBE_CharacterStatus_GetRowField10','VIBE_CharacterStatus_GetRowField00',
    'VIBE_CharacterStatus_GetRowField08','VIBE_CharacterStatus_GetRowField20','VIBE_CharacterStatus_GetRowFieldByRowTable',
    'VIBE_CharacterStatus_InterpolateTierValue','VIBE_CharacterStatus_GetTierIndexForStatus','VIBE_CharacterStatus_CollectRowEntries',
    'VIBE_GetCharacterStatusValueForSlot','VIBE_ComputeStatusValueForKey')
  'Record / slot-stat model' = @(
    'VIBE_GetRecordCopyForActor','VIBE_ScopedActor_GetActiveSlotIndex','VIBE_SumRecordSlotStatValues',
    'VIBE_CompareRecordsBySlotStatValue','VIBE_GetRecordSlotStatValue','VIBE_GetActiveSlotIndexForRecord',
    'VIBE_Record_ConstructDefault','VIBE_Record_AssignCopy','VIBE_Record_EvalSlotStat','VIBE_Record_GetSlotStatValue',
    'VIBE_Record_Entry_SumTierValues','VIBE_Record_Entry_GetStatusTierIndex','VIBE_Record_Entry_ComputeTierValue',
    'VIBE_ApplyRecordToCharacter')
  'UiList' = @(
    'VIBE_UiList_BuildSlotEntriesByLoadout','VIBE_UiList_BuildSlotEntries','VIBE_UiList_BuildSlotEntriesAndSum',
    'VIBE_UiList_BuildSlotEntriesFromFactionNode','VIBE_UiList_SumRowSkillValues','VIBE_UiList_ContainsRowStatus',
    'VIBE_UiList_QuerySlotEntries','VIBE_UiList_QuerySlotEntriesFiltered')
  'FastTravel' = @(
    'VIBE_FastTravel_SelectDestination','VIBE_FastTravel_TryAcceptRequest','VIBE_FastTravel_Execute','VIBE_FastTravel_FlushPendingRequest')
  'World / Time' = @(
    'VIBE_World_EvalTimeCurve','VIBE_World_GetTimeScale','VIBE_World_FrameUpdate','VIBE_Engine_GetTimeValue','VIBE_Time_ComputeDeltaTicks')
  'Trust / Friendship' = @(
    'VIBE_FindTargetActorByFriendship','VIBE_TargetFilter_GetBitIndex','VIBE_Friendship_GetTierCode',
    'VIBE_Trust_FindRecordById_L18','VIBE_Trust_FindRecordByActor_L38','VIBE_Trust_FindRecordById_L38')
  'Container / Map / Vector helpers' = @(
    'VIBE_ScopedRef_Acquire','VIBE_Map_FindOrCreateValueSlot','VIBE_Int16Vector_Append','VIBE_Vector68_Grow',
    'VIBE_HashMap_LookupValue','VIBE_IdMap_GetGamePlayVariableValue')
  'Actor / Scope plumbing' = @(
    'VIBE_ScopeAttacher_ClientActor_InitGlobal','VIBE_ScopeAttacher_ClientActor_Init','VIBE_Actor_GetFieldAttacher',
    'VIBE_Actor_TryGetGaugeTime')
}
foreach ($k in $groups.Keys) { foreach ($n in $groups[$k]) {
    if ($themeMap.ContainsKey($n)) { throw "duplicate theme assignment: $n" }
    $themeMap[$n] = $k } }

$themeOrder = @('Table resolvers','CharacterStatus','Record / slot-stat model','UiList','FastTravel','World / Time',
                'Trust / Friendship','Container / Map / Vector helpers','Actor / Scope plumbing')

$themeBlurb = @{
  'Table resolvers' = 'Uniform lazily-deserialised global data-table accessors: `XxxInfoTable_GetById(pId) -> record`, each with a shared empty record fallback. These are the game''s static-data lookup layer (skills, items, characters, mercenaries, statuses, buffs, fields, factions, scenes, gameplay variables).'
  'CharacterStatus' = 'GAS-like status/stat accumulator system on the character status context: applying deltas, clamping, accumulated reads, grouped-status gating, per-row slot computation, tier interpolation and row-field decoding.'
  'Record / slot-stat model' = 'Generic "record" object model (construct/copy/evaluate) plus the slot-index and slot-stat accessors shared by the UI-list and status code.'
  'UiList' = 'Menu/list builders and queries that turn records into UI slot entries (loadout view, faction-node view, filtered queries, row aggregation).'
  'FastTravel' = 'Native fast-travel pipeline: destination selection gate, request validation/dispatch, execution and pending-request flush.'
  'World / Time' = 'World clock and frame pump: time-scale read, time-dilation curve evaluation, engine time source, delta computation and the per-frame world update.'
  'Trust / Friendship' = 'NPC/pet affinity system: trust-record lookup in the open-addressed maps, friendship tier coding and target-actor selection with its filter bit index.'
  'Container / Map / Vector helpers' = 'Generic containers called by the trust/status code: open-addressed map find-or-create, hash/id-map lookup, 0x68-byte element vector growth, int16 vector append and scoped-reference acquisition.'
  'Actor / Scope plumbing' = 'Actor/scope glue: global ClientActor scope attacher init, field attacher accessor and the actor gauge-time query.'
}

# ---- helpers ----
function Get-Size($f) { [Convert]::ToInt64($f.highest, 16) - [Convert]::ToInt64($f.lowest, 16) }

function Get-RangeLabel($addrs) {
    $sorted = $addrs | Sort-Object
    $clusters = @(); $start = $sorted[0]; $prev = $sorted[0]
    foreach ($a in $sorted[1..($sorted.Count-1)]) {
        if (($a - $prev) -gt 0x30000) { $clusters += ,@($start, $prev); $start = $a }
        $prev = $a
    }
    $clusters += ,@($start, $prev)
    $parts = foreach ($c in $clusters) {
        if ($c[0] -eq $c[1]) { Norm ('0x{0:x}' -f $c[0]) } else { (Norm ('0x{0:x}' -f $c[0])) + '-' + (Norm ('0x{0:x}' -f $c[1])) }
    }
    return ($parts -join '; ')
}

$sb = New-Object System.Text.StringBuilder
function W([string]$s) { [void]$sb.AppendLine($s) }

# ---- header ----
W '# Existing `VIBE_` annotations - Binary Ninja audit (Crimson Desert, PE 2944)'
W ''
W 'READ-ONLY audit of the currently open Binary Ninja database. No symbol, comment, type or'
W 'prototype was modified; every value below was read through the WARP MCP endpoint'
W '(`bn_function_search`, `bn_symbol_list`, `bn_comment_get`, `bn_function_prototype_get`,'
W '`bn_function_callers`).'
W ''
W ('- Functions matching `VIBE_`: **{0}** (identical to the `VIBE_` symbol count - every `VIBE_` symbol is a FunctionSymbol).' -f $funcs.Count)
W '- Addresses are normalised to uppercase 0x-prefixed form (tool output is lowercase).'
W '- Size = highestAddress - lowestAddress (bytes); block counts from the same query.'
W ('- Address span: {0} - {1}' -f (Norm $funcs[0].addr), (Norm $funcs[$funcs.Count-1].addr))
W ('- Annotated (non-empty function comment): **{0}**; missing comment: **{1}**.' -f (($funcs | Where-Object { -not [string]::IsNullOrWhiteSpace($commentMap[$_.name]) }).Count), (($funcs | Where-Object { [string]::IsNullOrWhiteSpace($commentMap[$_.name]) }).Count))
W ''

# ---- index table ----
W '## Index (all 81 functions, ascending address)'
W ''
W '| # | Address | Name | Size (bytes) | Basic blocks | Theme | Comment |'
W '| --- | --- | --- | --- | --- | --- | --- |'
$i = 0
foreach ($f in $funcs) {
    $i++
    $c = $commentMap[$f.name]
    $has = if ([string]::IsNullOrWhiteSpace($c)) { 'MISSING' } else { 'yes ({0} chars)' -f $c.Length }
    W ('| {0} | {1} | `{2}` | {3} | {4} | {5} | {6} |' -f $i, (Norm $f.addr), $f.name, (Get-Size $f), $f.bb, $themeMap[$f.name], $has)
}
W ''

# ---- detail ----
W '## Per-function detail'
W ''
W 'Every entry carries the verbatim existing function comment (function-start comment only;'
W 'instruction-level comments were not part of this audit). Comments are reproduced exactly,'
W 'inside 4-backtick fences, with no rewrapping.'
W ''
$i = 0
foreach ($f in $funcs) {
    $i++
    $c = $commentMap[$f.name]
    $proto = $protoMap[$f.name]
    if (-not $proto) { $proto = $f.def }
    W ('### {0}. `{1}`' -f $i, $f.name)
    W ''
    W ('- **Address:** {0}' -f (Norm $f.addr))
    W ('- **Name:** `{0}`' -f $f.name)
    W ('- **Size:** {0} bytes ({1}-{2})' -f (Get-Size $f), (Norm $f.addr), (Norm $f.highest))
    W ('- **Basic blocks:** {0}' -f $f.bb)
    W ('- **Prototype:** `{0}`' -f $proto)
    W ('- **Theme:** {0}' -f $themeMap[$f.name])
    W ('- **Auto-discovered / thunk:** {0}' -f $f.auto)
    if ([string]::IsNullOrWhiteSpace($c)) {
        W '- **Comment:** (no comment)'
    } else {
        W ('- **Comment** ({0} chars):' -f $c.Length)
        W ''
        W '````text'
        W $c.TrimEnd()
        W '````'
    }
    W ''
}

# ---- themes ----
W '## Themes'
W ''
W '| Theme | Functions | Address range(s) | What the theme appears to cover |'
W '| --- | --- | --- | --- |'
foreach ($t in $themeOrder) {
    $members = $funcs | Where-Object { $themeMap[$_.name] -eq $t }
    $addrs = $members | ForEach-Object { [Convert]::ToInt64($_.addr, 16) }
    W ('| {0} | {1} | {2} | {3} |' -f $t, $members.Count, (Get-RangeLabel $addrs), $themeBlurb[$t])
}
W ('| **Total** | **{0}** | | |' -f $funcs.Count)
W ''
W 'Theme membership is the auditor''s grouping by name + comment; it is not stored in the database.'
W ''

# ---- missing comments ----
$missing = $funcs | Where-Object { [string]::IsNullOrWhiteSpace($commentMap[$_.name]) }
W '## Functions without comments'
W ''
W ('**{0} of {1}** `VIBE_` functions have an empty function-start comment - these are the annotation gaps.' -f $missing.Count, $funcs.Count)
W ''
W '| # | Address | Name | Size (bytes) | Basic blocks | Theme |'
W '| --- | --- | --- | --- | --- | --- |'
$j = 0
foreach ($f in $missing) { $j++; W ('| {0} | {1} | `{2}` | {3} | {4} | {5} |' -f $j, (Norm $f.addr), $f.name, (Get-Size $f), $f.bb, $themeMap[$f.name]) }
W ''
W 'Gap distribution by theme:'
W ''
W '| Theme | Missing | Total | Coverage |'
W '| --- | --- | --- | --- |'
foreach ($t in $themeOrder) {
    $tot = ($funcs | Where-Object { $themeMap[$_.name] -eq $t }).Count
    $mis = ($missing | Where-Object { $themeMap[$_.name] -eq $t }).Count
    W ('| {0} | {1} | {2} | {3}% |' -f $t, $mis, $tot, [math]::Round(100.0 * ($tot - $mis) / $tot, 0))
}
W ''

# ---- naming conventions ----
$naming = @'
## Naming-convention observations

### Exact style in use

`VIBE_<FeatureToken>_<RoleToken...>` - a fixed three-part shape:

1. **Prefix** - literal `VIBE_`, always uppercase, always the first four characters. It is the
   annotation author's namespace marker (the `VIBE_` string is what the whole audit keys on).
2. **Feature token** - one CamelCase noun naming the subsystem, in PascalCase:
   `SkillTable`, `ItemInfoTable`, `CharacterInfoTable`, `MercenaryInfoTable`, `FactionNodeTable`,
   `FieldInfoTable`, `LevelGimmickSceneObjectInfoTable`, `StatusInfoTable`, `StatusGroupInfoTable`,
   `BuffInfoTable`, `QuestGaugeInfoTable`, `GamePlayVariableInfoTable`, `CharacterStatus`,
   `Record`, `UiList`, `FastTravel`, `World`, `Trust`, `Friendship`, `Map`, `HashMap`, `IdMap`,
   `Time`, `Engine`, `Actor`, `ScopedActor`, `ScopedRef`, `ScopeAttacher`, `Vector68`,
   `Int16Vector`, `TargetFilter`.
3. **Role token(s)** - a verb-led PascalCase phrase describing what the function does:
   `GetById`, `GetRecordById`, `FindOrCreateValueSlot`, `ApplyDelta`, `ApplyDeltaPrimary`,
   `GetAccumulatedValue`, `BuildSlotEntries`, `QuerySlotEntriesFiltered`, `FindRecordByActor_L38`, ...
   Verbs seen: `Get`, `Find`, `Build`, `Query`, `Apply`, `Compute`, `Eval`, `Sum`, `Compare`,
   `Contains`, `Interpolate`, `Rebuild`, `Acquire`, `Append`, `Grow`, `Lookup`, `Select`, `Execute`,
   `TryAccept`, `Flush`, `Init`, `Collect`, `Assign`, `Construct`, `Is` (predicate).

Examples that fit the pattern exactly: `VIBE_FastTravel_TryAcceptRequest`,
`VIBE_CharacterStatus_ApplyDeltaPrimary`, `VIBE_UiList_QuerySlotEntriesFiltered`,
`VIBE_Record_Entry_ComputeTierValue`, `VIBE_Trust_FindRecordByActor_L38`.

No other naming style exists in the set - no lowercase, no snake_case, no Hungarian, no `sub_`
leftovers, no trailing `_v2`. The only non-`VIBE_` leading characters are Binary Ninja's own
thunk marker (below).

### Inconsistencies future annotations must decide about

1. **Thunk names are Binary Ninja-generated, not authored.** Three names start with `j_` and are
   marked `autoDiscovered = true`; they are 4-byte jump thunks that BN auto-named after their
   target, so the `VIBE_` prefix is inherited rather than written by the annotator:
   - `j_VIBE_ItemInfoTable_GetById` (0x140388AC0, 4 bytes, 1 block) -> `VIBE_ItemInfoTable_GetById` (0x14038AB60)
   - `j_VIBE_MercenaryInfoTable_GetRecordById` (0x1403891B0, 4 bytes, 1 block) -> `VIBE_MercenaryInfoTable_GetRecordById` (0x148946AA0)
   - `j_VIBE_CharacterStatus_CollectRowEntries` (0x141F3CB00, 4 bytes, 1 block) -> `VIBE_CharacterStatus_CollectRowEntries` (0x14E0AA810)
   These three are effectively **duplicate names** for one logical function. They have no comments
   and, being thunks, arguably should not be annotated at all - annotate the real target instead.
   A `VIBE_`-prefixed audit will always over-count by 3 for this reason.
2. **Two spellings for the same role**: the table accessors use both `GetById` (13 functions) and
   `GetRecordById` (`VIBE_CharacterInfoTable_GetRecordById`, `VIBE_MercenaryInfoTable_GetRecordById`).
   `GetById` is the majority form.
3. **Feature token present vs. absent**: many functions drop the feature token and lead with the
   verb (`VIBE_GetRecordCopyForActor`, `VIBE_GetRecordSlotStatValue`, `VIBE_SumRecordSlotStatValues`,
   `VIBE_CompareRecordsBySlotStatValue`, `VIBE_GetActiveSlotIndexForRecord`,
   `VIBE_GetCharacterStatusValueForSlot`, `VIBE_ApplyRecordToCharacter`,
   `VIBE_FindTargetActorByFriendship`, `VIBE_ComputeStatusValueForKey`), while their siblings keep it
   (`VIBE_Record_GetSlotStatValue`, `VIBE_CharacterStatus_...`). There is no rule; both forms coexist
   inside the same subsystem.
4. **Layout suffixes are unique to Trust**: `_L18` / `_L38` encode the container's map offset
   (+0x18 / +0x38). This suffix convention appears nowhere else; it is the one place where the name
   encodes a raw struct offset.
5. **Sub-object token chaining**: `VIBE_Record_Entry_...` nests two feature tokens (`Record` then
   `Entry`), i.e. the role token itself may be a sub-namespace. `VIBE_CharacterStatus_GetRowField18`
   goes the other way and encodes a struct field offset (`0x18`, `0x10`, `0x00`, `0x08`, `0x20`) in
   the role token, mixing hex offsets into names that are otherwise semantic.
6. **`VIBE_World_*` is thinner than the rest**: only 5 functions, and `World`/`Engine`/`Time` are
   used interchangeably for the clock concepts (`VIBE_World_GetTimeScale`,
   `VIBE_Engine_GetTimeValue`, `VIBE_Time_ComputeDeltaTicks`).

### Comment style in use (must be matched by future annotations)

Twenty-one functions carry comments; the established shape is:

```
<ExactFunctionName>(<arg names>) -> <return>
<blank line>
Prose: what it does, the object layout it walks (offsets, e.g. "+0x68", "record->count (+0x28)"),
which globals it uses (data_146D6E410), which sub_ functions it calls, and what the return means.
```

Observed refinements:

- The **first line is a pseudo-signature** naming the real argument roles, which frequently
  disagrees with (and improves on) the Binary Ninja prototype - e.g. `VIBE_FastTravel_TryAcceptRequest`
  has BN prototype `bool(void* unusedActor, int32_t sceneId, int32_t nodeIndex, int32_t mercenaryHandle)`
  and the comment repeats it, while `VIBE_CharacterStatus_ApplyDelta(statusCtx, statusId, timeNow, delta, sourceInfo)`
  renames all five of BN's `argN`s.
- Bullet lists with `*` enumerate branches; indented `+0xNN` fragments document struct offsets.
- Four comments carry a **PE-dossier tail** delimited by `--- PE 2944 ... ---` (or prefixed
  `Trinity record (PE 2944 ...)`), containing labelled lines such as `Trinity feature:`,
  `PE evidence:` (build string + SHA-256 + VA/RVA + section + size + block count),
  `Locator:` (AOB signature constant name + uniqueness), `ABI proved from this function's own prologue:`
  with raw disassembly, `Callers (all three):`, `Trinity:` (which repo source file binds the address),
  `Proof:` (static = ... / live = ...) and `Confidence: confirmed.`
- Inline backticks are used inside comments (5 of 21), so the report fences comments with four
  backticks.
- Only `VIBE_FastTravel_*` (4 functions, all four annotated) has reached the full dossier standard.
  `VIBE_World_*`, `VIBE_CharacterStatus_*`, `VIBE_UiList_*` and `VIBE_Record_*` have prose comments
  only, with no PE evidence / locator / proof block.
'@
W $naming
W ''

# ---- feature relevance ----
$relevance = @'
## Functions relevant to these Trinity menu features

Relevance is judged from the existing name + comment + (where noted) `bn_function_callers`
evidence. Confidence: **direct** = the function's documented job is that feature; **adjacent** =
the function is a shared accessor/helper the feature's code paths necessarily use; **weak** = only
indirectly connected. `none` means no `VIBE_` function in the database names or documents the topic.

### 1. Locomotion / super-run / super-jump / free-flight
**No direct `VIBE_` candidates.** Not one of the 81 names mentions movement, velocity, jump, fly,
sprint or locomotion, and no comment does either (a keyword scan for `fly`, `jump`, `sprint`,
`super`, `locomot` hit only the word "supersedes" in `VIBE_FastTravel_TryAcceptRequest`).
Adjacent time/frame plumbing that a speed hack would have to interact with:
- `VIBE_World_FrameUpdate` - **0x140AD23F0** (per-frame world pump; the movement tick lives under it)
- `VIBE_World_GetTimeScale` - **0x140952080** (frame time-dilation factor - the "game speed" knob)
- `VIBE_World_EvalTimeCurve` - **0x140951440** (the curve that produces the dilation)
- `VIBE_Engine_GetTimeValue` - **0x141416B70** (time source)
- `VIBE_Time_ComputeDeltaTicks` - **0x1417AF690**
Landscape check: `bn_function_search` for `Fly`, `SuperJump`, `Sprint`, `GameSpeed` returns 0, 0, 1
(a stdio function) and 0 matches in the whole database, so there is no other annotated anchor to
borrow for locomotion either.

### 2. Worker level / skills
- `VIBE_SkillTable_GetById` - **0x1403880A0** - direct: the `skillinfo` table resolver (574 callers)
- `VIBE_UiList_SumRowSkillValues` - **0x1417862F0** - direct: aggregates skill values per UI row
- `VIBE_UiList_BuildSlotEntriesByLoadout` - **0x14042CC40** - direct: builds the slot/loadout rows a
  worker/character skill screen renders
- `VIBE_GetRecordSlotStatValue` - **0x141776000**, `VIBE_SumRecordSlotStatValues` - **0x140D243C0**,
  `VIBE_CompareRecordsBySlotStatValue` - **0x140EDF480** - adjacent: per-slot stat reads/sums/sorting
- `VIBE_Record_EvalSlotStat` - **0x142156B40**, `VIBE_Record_GetSlotStatValue` - **0x142157010** -
  adjacent: stat evaluation for a slot
- `VIBE_ApplyRecordToCharacter` - **0x1427990D0** - adjacent: commits a record (stats/skills) onto a character
- `VIBE_GetRecordCopyForActor` - **0x14065FBD0** - adjacent: per-actor record copy (40 call sites)
- `VIBE_MercenaryInfoTable_GetRecordById` - **0x148946AA0** (real) / `j_VIBE_MercenaryInfoTable_GetRecordById`
  - **0x1403891B0** (thunk) - adjacent: mercenary/worker static data
- `VIBE_CharacterInfoTable_GetRecordById` - **0x140389570** - adjacent: character/worker static data
Landscape check: `Worker` matches only an AK audio function; `Skill` matches only the two functions above.

### 3. Crime / wanted / bounty
**No `VIBE_` candidates at all.** `bn_function_search` for `Wanted`, `Crime` and `Bounty` returns
**0 matches each** across the entire database, and no `VIBE_` comment mentions them. The nearest
VIBE_ code is faction/NPC-target logic, which is a plausible reuse point but is not a crime system:
- `VIBE_FactionNodeTable_GetById` - **0x140430020** (faction records)
- `VIBE_UiList_BuildSlotEntriesFromFactionNode` - **0x14176DEC0** (renders faction-node rows)
- `VIBE_FindTargetActorByFriendship` - **0x141ABC170** / `VIBE_TargetFilter_GetBitIndex` - **0x141ABCDF0**
  (NPC target selection - the same machinery a guard/bounty-target picker would use)
- `VIBE_Actor_TryGetGaugeTime` - **0x1421A1D40**, `VIBE_QuestGaugeInfoTable_GetById` - **0x1405D1E40**
  (a "gauge" concept, but documented as quest gauge, not a wanted meter)

### 4. Inventory / items / money
- `VIBE_ItemInfoTable_GetById` - **0x14038AB60** (real; 3126 call sites) /
  `j_VIBE_ItemInfoTable_GetById` - **0x140388AC0** (thunk) - direct: the item static-data resolver
- `VIBE_UiList_QuerySlotEntries` - **0x142845130**, `VIBE_UiList_QuerySlotEntriesFiltered` - **0x14284FAB0**
  - direct: inventory-style filtered slot queries
- `VIBE_UiList_BuildSlotEntries` - **0x140F27200**, `VIBE_UiList_BuildSlotEntriesAndSum` - **0x140F6E050**,
  `VIBE_UiList_BuildSlotEntriesByLoadout` - **0x14042CC40** - direct: slot-entry construction
- `VIBE_UiList_ContainsRowStatus` - **0x141786540** - adjacent: row predicate for list filtering
- `VIBE_GetActiveSlotIndexForRecord` - **0x14214BD30**, `VIBE_ScopedActor_GetActiveSlotIndex` - **0x14065FEE0**
  - adjacent: which slot/row a record currently occupies
- `VIBE_GetRecordSlotStatValue` - **0x141776000**, `VIBE_Record_GetSlotStatValue` - **0x142157010** -
  adjacent: per-slot stat (item stat) reads
- `VIBE_GetCharacterStatusValueForSlot` - **0x142153350** - weak
- `VIBE_HashMap_LookupValue` - **0x142146110**, `VIBE_Map_FindOrCreateValueSlot` - **0x140436380** -
  weak: generic container lookups
- **Money/currency: no candidate.** No `VIBE_` name or comment mentions money, gold, silver or
  currency, and a `Money` search returns only C++ `std::money_put` library functions.

### 5. Equipment / refine / sockets
- `VIBE_UiList_BuildSlotEntriesByLoadout` - **0x14042CC40** - direct: loadout/equipment slot view
- `VIBE_GetRecordSlotStatValue` - **0x141776000**, `VIBE_SumRecordSlotStatValues` - **0x140D243C0**,
  `VIBE_CompareRecordsBySlotStatValue` - **0x140EDF480** - direct-ish: per-slot equipment stat math
  (the compare function is a 96-block sort/compare used to rank records by a slot stat)
- `VIBE_Record_GetSlotStatValue` - **0x142157010**, `VIBE_Record_EvalSlotStat` - **0x142156B40**,
  `VIBE_Record_Entry_ComputeTierValue` - **0x142414C10**, `VIBE_Record_Entry_SumTierValues` - **0x14240ECC0**,
  `VIBE_Record_Entry_GetStatusTierIndex` - **0x142414870** - adjacent: record/entry stat + tier model
- `VIBE_Record_ConstructDefault` - **0x142155520**, `VIBE_Record_AssignCopy` - **0x142156370** - adjacent:
  record lifecycle (build a modified copy of an equipment record)
- `VIBE_ApplyRecordToCharacter` - **0x1427990D0**, `VIBE_GetRecordCopyForActor` - **0x14065FBD0** - adjacent
- `VIBE_ScopedActor_GetActiveSlotIndex` - **0x14065FEE0**, `VIBE_GetActiveSlotIndexForRecord` - **0x14214BD30** - adjacent
- `VIBE_Vector68_Grow` - **0x141ECCD40** - weak: grows the 0x68-byte entry vectors used by the
  record/trust tables
- **Refine/sockets: no candidate by name.** `Refine` and `Socket` each return 0 matches in the whole
  database; the socket/refine UI is probably served by the generic `VIBE_UiList_*` +
  `VIBE_Record_*` slot-stat machinery above.

### 6. Time / weather / game-speed
- `VIBE_World_GetTimeScale` - **0x140952080** - **direct game-speed hook**: returns the world time
  scale handed to the frame dispatch; only 3 call sites, all inside `World::FrameTimerUpdate`
- `VIBE_World_EvalTimeCurve` - **0x140951440** - direct: piecewise curve that derives the frame's
  time-dilation factor
- `VIBE_World_FrameUpdate` - **0x140AD23F0** - direct: per-frame world entry point (calls
  `World::FrameTimerUpdate` then ticks every subsystem) - the natural place to scale or freeze time
- `VIBE_Engine_GetTimeValue` - **0x141416B70** - direct: the engine time source used by the frame timer
- `VIBE_Time_ComputeDeltaTicks` - **0x1417AF690** - adjacent: delta-tick computation
- `VIBE_QuestGaugeInfoTable_GetById` - **0x1405D1E40**, `VIBE_Actor_TryGetGaugeTime` - **0x1421A1D40** - weak:
  in-game gauge timers rather than world time
- **Weather: no candidate.** `Weather` returns 0 matches database-wide.

### 7. Trust / friendship / pets
- `VIBE_Trust_FindRecordByActor_L38` - **0x141ECB640** - direct: actor-keyed trust record
- `VIBE_Trust_FindRecordById_L18` - **0x141ECA2D0** - direct: id-keyed trust record (map at +0x18)
- `VIBE_Trust_FindRecordById_L38` - **0x141ECB750** - direct: id-keyed trust record (map at +0x38)
- `VIBE_Friendship_GetTierCode` - **0x141EC9F70** - direct: maps a friendship value to a tier code;
  its comment names the CE "Fast friendship" entry and the only caller
- `VIBE_FindTargetActorByFriendship` - **0x141ABC170** - direct: picks the target actor by friendship
- `VIBE_TargetFilter_GetBitIndex` - **0x141ABCDF0** - direct: the row-flag bit a candidate must have
- `VIBE_Map_FindOrCreateValueSlot` - **0x140436380** - direct support: the open-addressed map used by
  the trust tables; called from `NPC::TrustMultiplier` (0x141ECB7D0) among 59 call sites
- `VIBE_Vector68_Grow` - **0x141ECCD40** - **direct support, caller-proven**: its only three callers are
  `NPC::TrustMultiplier` (0x141ECB7D0), `Pet_and_mount::TrustMultiplier` (**0x14DF6DA50**) and
  `sub_14295DCD0`, so this is the growth path of the per-character trust entry vectors
- `VIBE_ScopedRef_Acquire` - **0x140393DF0** - weak: scope lifetime guard used around such tables
- **Pets**: no `VIBE_`-prefixed pet function exists, but the database does contain
  `Pet_and_mount::TrustMultiplier` (**0x14DF6DA50**) and its thunk `j_Pet_and_mount::TrustMultiplier`,
  which call `VIBE_Vector68_Grow` and (via `NPC::TrustMultiplier`) `VIBE_Map_FindOrCreateValueSlot`.
  So the pet/mount affinity path is reachable through the VIBE_ trust helpers even though it is not
  itself VIBE_-named.
- Landscape check: `Friendship` matches only the two VIBE_ functions above; `Pet_`/`Mount` match only
  the two `Pet_and_mount::TrustMultiplier` symbols.

### Coverage summary

| Trinity menu feature | Direct VIBE_ candidates | Adjacent / weak VIBE_ candidates | Verdict |
| --- | --- | --- | --- |
| Locomotion / super-run / super-jump / free-flight | 0 | 5 (World/Time + Engine/Time) | **gap** - no movement annotations exist |
| Worker level / skills | 3 | 8 | covered by table + record/slot helpers |
| Crime / wanted / bounty | 0 | 4 (faction / target-filter / gauge) | **gap** - topic absent from the database |
| Inventory / items / money | 6 | 6 (+ money: 0) | items/slots covered; money is a **gap** |
| Equipment / refine / sockets | 1 | 12 (refine/sockets by name: 0) | slot-stat machinery covers it, names do not |
| Time / weather / game-speed | 4 | 2 (+ weather: 0) | best-covered feature after FastTravel |
| Trust / friendship / pets | 6 | 2 (+ pet path via `Pet_and_mount::TrustMultiplier` 0x14DF6DA50) | covered |
'@
W $relevance
W ''
W '---'
W ''
W 'End of audit. Source of truth: the open Binary Ninja database (read-only access, no mutations).'
W ''

[System.IO.File]::WriteAllText($outFile, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output "wrote $outFile"
Write-Output ("bytes: " + (Get-Item $outFile).Length)
Write-Output ("funcs: " + $funcs.Count + "  missing: " + $missing.Count)
