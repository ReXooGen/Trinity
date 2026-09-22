# Existing `VIBE_` annotations - Binary Ninja audit (Crimson Desert, PE 2944)

READ-ONLY audit of the currently open Binary Ninja database. No symbol, comment, type or
prototype was modified; every value below was read through the WARP MCP endpoint
(`bn_function_search`, `bn_symbol_list`, `bn_comment_get`, `bn_function_prototype_get`,
`bn_function_callers`).

- Functions matching `VIBE_`: **81** (identical to the `VIBE_` symbol count - every `VIBE_` symbol is a FunctionSymbol).
- Addresses are normalised to uppercase 0x-prefixed form (tool output is lowercase).
- Size = highestAddress - lowestAddress (bytes); block counts from the same query.
- Address span: 0x1403880A0 - 0x14E0AA810
- Annotated (non-empty function comment): **21**; missing comment: **60**.

## Index (all 81 functions, ascending address)

| # | Address | Name | Size (bytes) | Basic blocks | Theme | Comment |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0x1403880A0 | `VIBE_SkillTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 2 | 0x140388AC0 | `j_VIBE_ItemInfoTable_GetById` | 4 | 1 | Table resolvers | MISSING |
| 3 | 0x1403891B0 | `j_VIBE_MercenaryInfoTable_GetRecordById` | 4 | 1 | Table resolvers | MISSING |
| 4 | 0x140389570 | `VIBE_CharacterInfoTable_GetRecordById` | 295 | 13 | Table resolvers | MISSING |
| 5 | 0x14038AB60 | `VIBE_ItemInfoTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 6 | 0x140393DF0 | `VIBE_ScopedRef_Acquire` | 141 | 8 | Container / Map / Vector helpers | MISSING |
| 7 | 0x14042CC40 | `VIBE_UiList_BuildSlotEntriesByLoadout` | 3836 | 207 | UiList | MISSING |
| 8 | 0x140430020 | `VIBE_FactionNodeTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 9 | 0x140436380 | `VIBE_Map_FindOrCreateValueSlot` | 756 | 36 | Container / Map / Vector helpers | yes (868 chars) |
| 10 | 0x1404A2010 | `VIBE_FieldInfoTable_GetById` | 295 | 13 | Table resolvers | yes (281 chars) |
| 11 | 0x1404A2630 | `VIBE_LevelGimmickSceneObjectInfoTable_GetById` | 293 | 13 | Table resolvers | yes (419 chars) |
| 12 | 0x1405835F0 | `VIBE_StatusInfoTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 13 | 0x1405D1E40 | `VIBE_QuestGaugeInfoTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 14 | 0x140636580 | `VIBE_StatusGroupInfoTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 15 | 0x14064F490 | `VIBE_ScopeAttacher_ClientActor_InitGlobal` | 54 | 1 | Actor / Scope plumbing | MISSING |
| 16 | 0x140654ED0 | `VIBE_FastTravel_SelectDestination` | 469 | 22 | FastTravel | yes (2850 chars) |
| 17 | 0x1406550B0 | `VIBE_FastTravel_TryAcceptRequest` | 1242 | 43 | FastTravel | yes (5182 chars) |
| 18 | 0x14065FBD0 | `VIBE_GetRecordCopyForActor` | 767 | 41 | Record / slot-stat model | MISSING |
| 19 | 0x14065FEE0 | `VIBE_ScopedActor_GetActiveSlotIndex` | 298 | 16 | Record / slot-stat model | MISSING |
| 20 | 0x14066B500 | `VIBE_ComputeStatusValueForKey` | 767 | 35 | CharacterStatus | MISSING |
| 21 | 0x14066F620 | `VIBE_BuffInfoTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 22 | 0x1408B4480 | `VIBE_ScopeAttacher_ClientActor_Init` | 138 | 5 | Actor / Scope plumbing | MISSING |
| 23 | 0x140951440 | `VIBE_World_EvalTimeCurve` | 482 | 26 | World / Time | yes (547 chars) |
| 24 | 0x140952080 | `VIBE_World_GetTimeScale` | 108 | 4 | World / Time | yes (301 chars) |
| 25 | 0x140A9A8D0 | `VIBE_FastTravel_Execute` | 583 | 21 | FastTravel | yes (1159 chars) |
| 26 | 0x140AD23F0 | `VIBE_World_FrameUpdate` | 3176 | 167 | World / Time | yes (488 chars) |
| 27 | 0x140D243C0 | `VIBE_SumRecordSlotStatValues` | 336 | 11 | Record / slot-stat model | MISSING |
| 28 | 0x140DBC6C0 | `VIBE_FastTravel_FlushPendingRequest` | 346 | 15 | FastTravel | yes (1756 chars) |
| 29 | 0x140EDF480 | `VIBE_CompareRecordsBySlotStatValue` | 1739 | 96 | Record / slot-stat model | MISSING |
| 30 | 0x140F27200 | `VIBE_UiList_BuildSlotEntries` | 983 | 32 | UiList | MISSING |
| 31 | 0x140F6E050 | `VIBE_UiList_BuildSlotEntriesAndSum` | 5218 | 252 | UiList | MISSING |
| 32 | 0x141416B70 | `VIBE_Engine_GetTimeValue` | 135 | 4 | World / Time | yes (274 chars) |
| 33 | 0x141480810 | `VIBE_GamePlayVariableInfoTable_GetById` | 295 | 13 | Table resolvers | MISSING |
| 34 | 0x1414E07A0 | `VIBE_Int16Vector_Append` | 228 | 12 | Container / Map / Vector helpers | MISSING |
| 35 | 0x14176DEC0 | `VIBE_UiList_BuildSlotEntriesFromFactionNode` | 1343 | 70 | UiList | MISSING |
| 36 | 0x141776000 | `VIBE_GetRecordSlotStatValue` | 288 | 11 | Record / slot-stat model | MISSING |
| 37 | 0x1417862F0 | `VIBE_UiList_SumRowSkillValues` | 580 | 29 | UiList | MISSING |
| 38 | 0x141786540 | `VIBE_UiList_ContainsRowStatus` | 309 | 21 | UiList | MISSING |
| 39 | 0x1417AAD30 | `VIBE_CharacterStatus_RebuildSlotValues` | 3089 | 74 | CharacterStatus | MISSING |
| 40 | 0x1417AC0C0 | `VIBE_CharacterStatus_ApplyDeltaPrimary` | 2433 | 92 | CharacterStatus | yes (1289 chars) |
| 41 | 0x1417AD6F0 | `VIBE_CharacterStatus_GetAccumulatedValue` | 402 | 11 | CharacterStatus | yes (1119 chars) |
| 42 | 0x1417ADD40 | `VIBE_CharacterStatus_GetGroupSlotEntry` | 133 | 3 | CharacterStatus | MISSING |
| 43 | 0x1417AE530 | `VIBE_CharacterStatus_ApplyDelta` | 967 | 41 | CharacterStatus | yes (894 chars) |
| 44 | 0x1417AF690 | `VIBE_Time_ComputeDeltaTicks` | 293 | 14 | World / Time | MISSING |
| 45 | 0x1417B0650 | `VIBE_CharacterStatus_IsStatusGroupActive` | 142 | 4 | CharacterStatus | MISSING |
| 46 | 0x1417B2B30 | `VIBE_CharacterStatus_AccumulateSlotValue` | 215 | 8 | CharacterStatus | MISSING |
| 47 | 0x1417B2EC0 | `VIBE_CharacterStatus_ComputeSlotEntries` | 1003 | 47 | CharacterStatus | MISSING |
| 48 | 0x1417E6CB0 | `VIBE_IdMap_GetGamePlayVariableValue` | 153 | 10 | Container / Map / Vector helpers | MISSING |
| 49 | 0x1417FA740 | `VIBE_Actor_GetFieldAttacher` | 58 | 1 | Actor / Scope plumbing | MISSING |
| 50 | 0x141ABC170 | `VIBE_FindTargetActorByFriendship` | 920 | 53 | Trust / Friendship | yes (2088 chars) |
| 51 | 0x141ABCDF0 | `VIBE_TargetFilter_GetBitIndex` | 130 | 11 | Trust / Friendship | yes (434 chars) |
| 52 | 0x141EC9F70 | `VIBE_Friendship_GetTierCode` | 141 | 14 | Trust / Friendship | yes (524 chars) |
| 53 | 0x141ECA2D0 | `VIBE_Trust_FindRecordById_L18` | 125 | 9 | Trust / Friendship | yes (606 chars) |
| 54 | 0x141ECB640 | `VIBE_Trust_FindRecordByActor_L38` | 261 | 16 | Trust / Friendship | yes (588 chars) |
| 55 | 0x141ECB750 | `VIBE_Trust_FindRecordById_L38` | 125 | 9 | Trust / Friendship | yes (327 chars) |
| 56 | 0x141ECCD40 | `VIBE_Vector68_Grow` | 286 | 15 | Container / Map / Vector helpers | yes (470 chars) |
| 57 | 0x141F3CB00 | `j_VIBE_CharacterStatus_CollectRowEntries` | 4 | 1 | CharacterStatus | MISSING |
| 58 | 0x141F3CF90 | `VIBE_CharacterStatus_GetRowField18` | 259 | 9 | CharacterStatus | MISSING |
| 59 | 0x141F3D0A0 | `VIBE_CharacterStatus_GetRowField10` | 211 | 8 | CharacterStatus | MISSING |
| 60 | 0x141F3D180 | `VIBE_CharacterStatus_GetRowField00` | 257 | 9 | CharacterStatus | MISSING |
| 61 | 0x141F3D290 | `VIBE_CharacterStatus_GetRowField08` | 259 | 9 | CharacterStatus | MISSING |
| 62 | 0x141F3D3A0 | `VIBE_CharacterStatus_GetRowField20` | 211 | 8 | CharacterStatus | MISSING |
| 63 | 0x141F3D480 | `VIBE_CharacterStatus_GetRowFieldByRowTable` | 162 | 7 | CharacterStatus | MISSING |
| 64 | 0x141F3E250 | `VIBE_CharacterStatus_InterpolateTierValue` | 256 | 18 | CharacterStatus | MISSING |
| 65 | 0x142146110 | `VIBE_HashMap_LookupValue` | 108 | 9 | Container / Map / Vector helpers | MISSING |
| 66 | 0x14214BD30 | `VIBE_GetActiveSlotIndexForRecord` | 607 | 22 | Record / slot-stat model | MISSING |
| 67 | 0x142153350 | `VIBE_GetCharacterStatusValueForSlot` | 495 | 22 | CharacterStatus | MISSING |
| 68 | 0x142155520 | `VIBE_Record_ConstructDefault` | 644 | 18 | Record / slot-stat model | MISSING |
| 69 | 0x142156370 | `VIBE_Record_AssignCopy` | 1105 | 24 | Record / slot-stat model | MISSING |
| 70 | 0x142156B40 | `VIBE_Record_EvalSlotStat` | 1014 | 31 | Record / slot-stat model | MISSING |
| 71 | 0x142157010 | `VIBE_Record_GetSlotStatValue` | 230 | 1 | Record / slot-stat model | MISSING |
| 72 | 0x1421A1D40 | `VIBE_Actor_TryGetGaugeTime` | 324 | 11 | Actor / Scope plumbing | MISSING |
| 73 | 0x14240ECC0 | `VIBE_Record_Entry_SumTierValues` | 988 | 57 | Record / slot-stat model | MISSING |
| 74 | 0x14240F6A0 | `VIBE_CharacterStatus_GetTierIndexForStatus` | 133 | 1 | CharacterStatus | MISSING |
| 75 | 0x142414870 | `VIBE_Record_Entry_GetStatusTierIndex` | 144 | 9 | Record / slot-stat model | MISSING |
| 76 | 0x142414C10 | `VIBE_Record_Entry_ComputeTierValue` | 650 | 40 | Record / slot-stat model | MISSING |
| 77 | 0x1427990D0 | `VIBE_ApplyRecordToCharacter` | 2387 | 119 | Record / slot-stat model | MISSING |
| 78 | 0x142845130 | `VIBE_UiList_QuerySlotEntries` | 4999 | 251 | UiList | MISSING |
| 79 | 0x14284FAB0 | `VIBE_UiList_QuerySlotEntriesFiltered` | 1608 | 49 | UiList | MISSING |
| 80 | 0x148946AA0 | `VIBE_MercenaryInfoTable_GetRecordById` | 295 | 13 | Table resolvers | MISSING |
| 81 | 0x14E0AA810 | `VIBE_CharacterStatus_CollectRowEntries` | 204 | 15 | CharacterStatus | MISSING |

## Per-function detail

Every entry carries the verbatim existing function comment (function-start comment only;
instruction-level comments were not part of this audit). Comments are reproduced exactly,
inside 4-backtick fences, with no rewrapping.

### 1. `VIBE_SkillTable_GetById`

- **Address:** 0x1403880A0
- **Name:** `VIBE_SkillTable_GetById`
- **Size:** 295 bytes (0x1403880A0-0x1403881C7)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 2. `j_VIBE_ItemInfoTable_GetById`

- **Address:** 0x140388AC0
- **Name:** `j_VIBE_ItemInfoTable_GetById`
- **Size:** 4 bytes (0x140388AC0-0x140388AC4)
- **Basic blocks:** 1
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** true
- **Comment:** (no comment)

### 3. `j_VIBE_MercenaryInfoTable_GetRecordById`

- **Address:** 0x1403891B0
- **Name:** `j_VIBE_MercenaryInfoTable_GetRecordById`
- **Size:** 4 bytes (0x1403891B0-0x1403891B4)
- **Basic blocks:** 1
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** true
- **Comment:** (no comment)

### 4. `VIBE_CharacterInfoTable_GetRecordById`

- **Address:** 0x140389570
- **Name:** `VIBE_CharacterInfoTable_GetRecordById`
- **Size:** 295 bytes (0x140389570-0x140389697)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 5. `VIBE_ItemInfoTable_GetById`

- **Address:** 0x14038AB60
- **Name:** `VIBE_ItemInfoTable_GetById`
- **Size:** 295 bytes (0x14038AB60-0x14038AC87)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 6. `VIBE_ScopedRef_Acquire`

- **Address:** 0x140393DF0
- **Name:** `VIBE_ScopedRef_Acquire`
- **Size:** 141 bytes (0x140393DF0-0x140393E7D)
- **Basic blocks:** 8
- **Prototype:** `int64_t*(int64_t* arg1, uint64_t arg2)`
- **Theme:** Container / Map / Vector helpers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 7. `VIBE_UiList_BuildSlotEntriesByLoadout`

- **Address:** 0x14042CC40
- **Name:** `VIBE_UiList_BuildSlotEntriesByLoadout`
- **Size:** 3836 bytes (0x14042CC40-0x14042DB3C)
- **Basic blocks:** 207
- **Prototype:** `int32_t*(void* arg1, int32_t* arg2, int16_t arg3, int32_t arg4, int32_t arg5, int32_t arg6)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 8. `VIBE_FactionNodeTable_GetById`

- **Address:** 0x140430020
- **Name:** `VIBE_FactionNodeTable_GetById`
- **Size:** 295 bytes (0x140430020-0x140430147)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 9. `VIBE_Map_FindOrCreateValueSlot`

- **Address:** 0x140436380
- **Name:** `VIBE_Map_FindOrCreateValueSlot`
- **Size:** 756 bytes (0x140436380-0x140436674)
- **Basic blocks:** 36
- **Prototype:** `void**(int32_t* arg1, int64_t* arg2, int32_t arg3, int64_t* arg4, char* arg5 @ 0x30, void** arg6, void** arg7)`
- **Theme:** Container / Map / Vector helpers
- **Auto-discovered / thunk:** false
- **Comment** (868 chars):

````text
VIBE_Map_FindOrCreateValueSlot(map, keySrc, key, outHandle, outKey, outScratch, outValue) -> outValue

Find-or-create for the open-addressed map used by the trust tables.

map layout: +0x00 int32 bucketCount, +0x04 int32 capacity, +0x08 int32 count,
            +0x0C int32 probe counter, +0x10 ptr buckets (0x100 B each), +0x18 ptr nodes.

If bucketCount is 0 the table is initialised with sub_140391680. The bucket is
key % bucketCount; if the bucket is already full (entry count == 0x1F) it rehashes. A new
node is allocated (0x18 bytes) with the bucket/hash at +0x00 and the key at +0x04, the
bucket's {hash, nodeIndex} pair is appended, and the node's value pointer is returned through
the last out-parameter. When the entry already exists the existing value is returned instead.
The outHandle parameter receives a temporary owner handle that the caller releases.
````

### 10. `VIBE_FieldInfoTable_GetById`

- **Address:** 0x1404A2010
- **Name:** `VIBE_FieldInfoTable_GetById`
- **Size:** 295 bytes (0x1404A2010-0x1404A2137)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment** (281 chars):

````text
VIBE_FieldInfoTable_GetById(uint16_t* pId) -> record

Table accessor for the global "fieldinfo" table (data_146D6E410), with data_146CF9830 as the
shared empty record. Used by the fast-travel paths to obtain the id of the field the actor
belongs to (read from the record at +0x28).
````

### 11. `VIBE_LevelGimmickSceneObjectInfoTable_GetById`

- **Address:** 0x1404A2630
- **Name:** `VIBE_LevelGimmickSceneObjectInfoTable_GetById`
- **Size:** 293 bytes (0x1404A2630-0x1404A2755)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int32_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment** (419 chars):

````text
VIBE_LevelGimmickSceneObjectInfoTable_GetById(int32_t* pId) -> record

Table accessor for the global "LevelGimmickSceneObjectInfo" table (data_146D6E428), with
data_146CDE128 as the shared empty record; null slots are lazily deserialised and cached.

The record holds the scene's travel nodes: +0x20 pointer to an array of 0xD8-byte node
entries and +0x28 their count. Both are used by VIBE_FastTravel_TryAcceptRequest.
````

### 12. `VIBE_StatusInfoTable_GetById`

- **Address:** 0x1405835F0
- **Name:** `VIBE_StatusInfoTable_GetById`
- **Size:** 295 bytes (0x1405835F0-0x140583717)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 13. `VIBE_QuestGaugeInfoTable_GetById`

- **Address:** 0x1405D1E40
- **Name:** `VIBE_QuestGaugeInfoTable_GetById`
- **Size:** 295 bytes (0x1405D1E40-0x1405D1F67)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 14. `VIBE_StatusGroupInfoTable_GetById`

- **Address:** 0x140636580
- **Name:** `VIBE_StatusGroupInfoTable_GetById`
- **Size:** 295 bytes (0x140636580-0x1406366A7)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 15. `VIBE_ScopeAttacher_ClientActor_InitGlobal`

- **Address:** 0x14064F490
- **Name:** `VIBE_ScopeAttacher_ClientActor_InitGlobal`
- **Size:** 54 bytes (0x14064F490-0x14064F4C6)
- **Basic blocks:** 1
- **Prototype:** `struct pa::UserOrCharacterValidLoginedScopeAttacher::pa::ScopeAttacher<class pa::ClientActor>::VTable**(int64_t arg1, struct pa::UserOrCharacterValidLoginedScopeAttacher::pa::ScopeAttacher<class pa::ClientActor>::VTable** arg2)`
- **Theme:** Actor / Scope plumbing
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 16. `VIBE_FastTravel_SelectDestination`

- **Address:** 0x140654ED0
- **Name:** `VIBE_FastTravel_SelectDestination`
- **Size:** 469 bytes (0x140654ED0-0x1406550A5)
- **Basic blocks:** 22
- **Prototype:** `uint64_t(int64_t arg1, int32_t arg2, int32_t arg3)`
- **Theme:** FastTravel
- **Auto-discovered / thunk:** false
- **Comment** (2850 chars):

````text
Trinity record (PE 2944 travel dossier, Phase 0) - role and ABI above are unchanged.
Trinity feature: Travel / Native Fast Travel - world-map destination selection gate.
PE evidence: PE 2944 (CrimsonDesert.exe 1.0.0.2944). VA 0x140654ED0 / RVA 0x654ED0, .code, 470 bytes, 22 basic blocks.
Locator: kSig_TravelToNode_PE2944 (31 bytes, see src/game/offsets.h) -> exactly 1 match in the whole image at this VA (all 12 PE sections scanned). NOTE: the offsets.h prose calls it a "32-byte prologue"; the string is actually 31 byte tokens.
ABI (confirmed from the caller's setup at 0x140DBC362-0x140DBC38A):
  RCX = int32* sceneId - pointer to the CALLER'S LOCAL copy (lea rcx,[rbp+0x1320] at 0x140DBC374). It is dereferenced by the table resolver, so it must stay valid for the synchronous call and must never be null.
  EDX = int32 sceneId by value (mov edx,edi at 0x140DBC388)
  R8D = int32 nodeIndex (mov r8d,ebx at 0x140DBC385)
  R9  = not part of this entry point's contract
  return AL: 1 = selection accepted (test al,al / je at 0x140DBC38F).
Data: record = VIBE_LevelGimmickSceneObjectInfoTable_GetById(RCX); record+0x28 u32 nodeCount (caller bounds-checks nodeIndex < nodeCount with cmp ebx,[rax+0x28] / jae at 0x140DBC380); record+0x20 ptr nodeArray; node stride 0xD8; sub_1415DF310 copies one 0xD8-byte node entry. Map-UI selection source: [mapUI+0x5C0] -> +0xC8 xmm (low dword = sceneId, high dword = nodeIndex), +0xD0 kind byte compared against 0xE.
Flow: single caller sub_140DBAC20 (world-map UI dispatch) at 0x140DBC38A. On AL=1 that caller stages the pending request as [rsi+0x960] = sceneId and [rsi+0x964] = nodeIndex, which VIBE_FastTravel_FlushPendingRequest later feeds to VIBE_FastTravel_TryAcceptRequest with R9D = 0. Callees: VIBE_ScopeAttacher_ClientActor_InitGlobal (0x14064F490), sub_14064F4D0 (field attacher), VIBE_LevelGimmickSceneObjectInfoTable_GetById (0x1404A2630), sub_1415DF310 (node-entry copy), sub_1417EDAC0 (888-byte validation/commit step - NOT identified; candidate, do not assume it is a modal).
Trinity: src/game/teleport.cpp Teleport::Install() PE 2944 branch resolves this address with mem::FindPattern(kSig_TravelToNode_PE2944) purely as a presence/contract check. Trinity does NOT call it - g_travelFn is bound to the dispatcher VIBE_FastTravel_TryAcceptRequest at 0x1406550B0. Do not pass nullptr in RCX if a future change ever calls it directly.
Proof: static = decompilation + caller register setup disassembly + shared registry/node contract with the confirmed dispatcher + shared pending-request staging. live = none in this pass.
Confidence: strong candidate. Role (map-UI destination selection/validation, AL=accepted) is supported by two independent sources; the older note that it "opens CommonModalMessage" is NOT proven and is recorded as an open question in docs/binary-ninja/dossiers/travel.md.
````

### 17. `VIBE_FastTravel_TryAcceptRequest`

- **Address:** 0x1406550B0
- **Name:** `VIBE_FastTravel_TryAcceptRequest`
- **Size:** 1242 bytes (0x1406550B0-0x14065558A)
- **Basic blocks:** 43
- **Prototype:** `bool(void* unusedActor, int32_t sceneId, int32_t nodeIndex, int32_t mercenaryHandle)`
- **Theme:** FastTravel
- **Auto-discovered / thunk:** false
- **Comment** (5182 chars):

````text
VIBE_FastTravel_TryAcceptRequest(unusedActor, sceneId, nodeIndex, mercenaryHandle) -> bool

Validates a pending fast-travel request and performs it when it is acceptable. Returns AL =
whether the request was accepted.

Confirmed calling contract (matches every call site):
  rcx / unusedActor      IGNORED. The function always works on the global ClientActor via
                         VIBE_ScopeAttacher_ClientActor_InitGlobal, so callers pass a stale or
                         null register value here.
  edx / sceneId          LevelGimmickSceneObjectInfo id, or -1 for "no scene".
  r8d / nodeIndex        index into that record's node array.
  r9d / mercenaryHandle  0 selects the ordinary fast-travel path; any other value selects the
                         mercenary/character path and is the actor handle to resolve.
Return: AL.

Path A - mercenaryHandle != 0 (mercenary / character branch):
  1. Attach the global ClientActor scope and its field attacher.
  2. sub_1408AE8E0 resolves the actor for mercenaryHandle; its characterinfo
     (*(*(actor+0x68)+0x20)+0x30) supplies the mercenary id at +0xBE.
  3. Reject (return false) when the id is 0xFFFF, or when the MercenaryInfo record has +0x5B
     clear.
  4. Otherwise resolve the character's field info (VIBE_FieldInfoTable_GetById with the id at
     *(*(*(clientActor+0x68)+0x1A0)+0x28)) and the travel scope (sub_1408B4510), build a
     request object (sub_1407782C0 / sub_140778A50) and hand it to
     sub_140C04D80(*(actorManager+0x18), requestObj, &out, fieldInfo, ..., &handleCopy,
     &nodeEntry, &idScratch). Accepted is set to true on BOTH branches of the travel-scope
     check, i.e. merely getting past the mercenary gate counts as accepted.

Path B - mercenaryHandle == 0 (ordinary fast travel):
  1. Reject immediately when sceneId == -1.
  2. record = VIBE_LevelGimmickSceneObjectInfoTable_GetById(&sceneId); reject when
     record->count (+0x28) <= nodeIndex.
  3. Attach the global ClientActor scope and its field attacher.
  4. Resolve the travel target id: the value parsed from the global object at data_146CE5B28
     (sub_14E877080 -> idScratch) is looked up in the id map at data_146D6AA40 (bucketCount
     +0x68, buckets +0x78, nodes +0x80). Any failure, or a node value of 0xFFFFFFFF at
     node+0x08, rejects the request.
  5. sub_1415DF310 copies the 0xD8-byte node entry
     (record->nodes(+0x20) + nodeIndex*0xD8) into nodeEntry, then
     VIBE_FastTravel_Execute(actorManager, targetId, *fieldInfo, 0, &nodeEntry) performs it.
  6. Accepted is true.

Note: the "field attacher" and ClientActor scopes are released on every exit path through the
pa::ScopeAttacherBase vtable.

--- PE 2944 Trinity travel record (Phase 0 dossier; supersedes nothing above) ---
Trinity feature: Travel / Native Fast Travel - the final post-confirmation dispatcher.
PE evidence: PE 2944 = CrimsonDesert.exe 1.0.0.2944, SHA-256 6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7. VA 0x1406550B0 / RVA 0x6550B0, .code, 1243 bytes, 43 basic blocks.
Locator: kSig_TravelDispatcher_PE2944 (44 bytes) -> exactly 1 match in the whole image at this VA (all 12 PE sections scanned under Trinity's section filter).
ABI proved from this function's own prologue:
  0x1406550B5  mov dword [rsp+0x10 {sceneId_1}], edx   -> EDX = int32 sceneId
  0x1406550B9  mov qword [rsp+0x8 {unusedActor_1}], rcx -> RCX is only spilled, never dereferenced here
  0x1406550D4  mov edi, r9d                            -> R9D = 4th argument (0 = ordinary fast travel)
  0x1406550D7  mov r15d, r8d                           -> R8D = int32 nodeIndex
  0x1406550DA/0x1406550?? test r9d,r9d                 -> R9D selects scene path (0) vs mercenary path (!=0)
  return AL, built from BL (0x140655344 mov bl,1 / 0x140655348 xor bl,bl)
RCX safety for Trinity (which passes nullptr): the only consumer is VIBE_ScopeAttacher_ClientActor_InitGlobal (0x14064F490), whose whole body is VIBE_ScopeAttacher_ClientActor_Init(*(data_146D691B0+0x30), out) - it never touches its first parameter. Two independent sources: that body, and two callers passing different RCX values with identical behaviour. Passing nullptr is therefore safe on PE 2944.
Callers (all three): VIBE_FastTravel_FlushPendingRequest (0x140DBC6C0) scene path 0x140DBC7BD with R9D explicitly zeroed by "xor r9d,r9d" at 0x140DBC7BA, and mercenary path 0x140DBC7E8 (sceneId=-1, nodeIndex=-1, R9D=handle); plus sub_140D9E6C0 at 0x140D9E858.
Key callee: VIBE_FastTravel_Execute (0x140A9A8D0) called at 0x140655566 - it performs the actual travel.
Trinity: src/game/teleport.cpp Teleport::Install() PE 2944 branch binds g_travelFn to this address via kSig_TravelDispatcher_PE2944 and only uses it when the selection locator also matches; src/game/travel_logic.h PrepareNativeTravelCall() builds the call as context=nullptr, sceneId, nodeIndex, travelMode=0; the call is fired on the game thread and its AL result is logged as accepted/refused.
Proof: static = prologue disassembly + decompilation + all three callers + unique 44-byte AOB. live = this session performed none; the accepted/refused logging path is the intended runtime check.
Confidence: confirmed.
````

### 18. `VIBE_GetRecordCopyForActor`

- **Address:** 0x14065FBD0
- **Name:** `VIBE_GetRecordCopyForActor`
- **Size:** 767 bytes (0x14065FBD0-0x14065FECF)
- **Basic blocks:** 41
- **Prototype:** `int64_t*(int64_t* arg1, int64_t* arg2, int64_t arg3)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 19. `VIBE_ScopedActor_GetActiveSlotIndex`

- **Address:** 0x14065FEE0
- **Name:** `VIBE_ScopedActor_GetActiveSlotIndex`
- **Size:** 298 bytes (0x14065FEE0-0x14066000A)
- **Basic blocks:** 16
- **Prototype:** `uint64_t(int64_t* arg1, int64_t arg2)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 20. `VIBE_ComputeStatusValueForKey`

- **Address:** 0x14066B500
- **Name:** `VIBE_ComputeStatusValueForKey`
- **Size:** 767 bytes (0x14066B500-0x14066B7FF)
- **Basic blocks:** 35
- **Prototype:** `int64_t(int64_t arg1, int32_t arg2, char arg3, char arg4, int16_t arg5)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 21. `VIBE_BuffInfoTable_GetById`

- **Address:** 0x14066F620
- **Name:** `VIBE_BuffInfoTable_GetById`
- **Size:** 295 bytes (0x14066F620-0x14066F747)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 22. `VIBE_ScopeAttacher_ClientActor_Init`

- **Address:** 0x1408B4480
- **Name:** `VIBE_ScopeAttacher_ClientActor_Init`
- **Size:** 138 bytes (0x1408B4480-0x1408B450A)
- **Basic blocks:** 5
- **Prototype:** `struct pa::UserOrCharacterValidLoginedScopeAttacher::pa::ScopeAttacher<class pa::ClientActor>::VTable**(void* arg1, struct pa::UserOrCharacterValidLoginedScopeAttacher::pa::ScopeAttacher<class pa::ClientActor>::VTable** arg2)`
- **Theme:** Actor / Scope plumbing
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 23. `VIBE_World_EvalTimeCurve`

- **Address:** 0x140951440
- **Name:** `VIBE_World_EvalTimeCurve`
- **Size:** 482 bytes (0x140951440-0x140951622)
- **Basic blocks:** 26
- **Prototype:** `void* const*(int64_t arg1, char arg2, float arg3, int32_t arg4[0x4] @ zmm5)`
- **Theme:** World / Time
- **Auto-discovered / thunk:** false
- **Comment** (547 chars):

````text
VIBE_World_EvalTimeCurve(worldSub, uint8 curveIndex, float x) -> float

Piecewise curve evaluation over the curve descriptor array at
worldSub + ((curveIndex + 0xCC) << 4), i.e. worldSub+0xCC0 for index 0 and worldSub+0xCD0 for
index 1. Each descriptor holds a count followed by {key, value, span} float triplets; the
function interpolates between them, clamping the blend to [0, 1], and returns the smallest
value seen (3.40282347e+38 = FLT_MAX when nothing applies).

Called by World::FrameTimerUpdate to derive the frame's time-dilation factor.
````

### 24. `VIBE_World_GetTimeScale`

- **Address:** 0x140952080
- **Name:** `VIBE_World_GetTimeScale`
- **Size:** 108 bytes (0x140952080-0x1409520EC)
- **Basic blocks:** 4
- **Prototype:** `int64_t(void* arg1) __location("zmm0")`
- **Theme:** World / Time
- **Auto-discovered / thunk:** false
- **Comment** (301 chars):

````text
VIBE_World_GetTimeScale(worldSub) -> float

Returns the world time scale passed to the frame dispatch. When worldSub+0x107E == 1 and
sub_1409520F0 agrees, the constant data_146D614D8 is returned; otherwise the scale is read
from sub-object worldSub+0x1258 -> virtual +0xC0 -> +0x68 -> +0x78 -> +0x6D0.
````

### 25. `VIBE_FastTravel_Execute`

- **Address:** 0x140A9A8D0
- **Name:** `VIBE_FastTravel_Execute`
- **Size:** 583 bytes (0x140A9A8D0-0x140A9AB17)
- **Basic blocks:** 21
- **Prototype:** `int64_t(void* arg1, int32_t arg2, int32_t arg3, int32_t arg4, int64_t arg5)`
- **Theme:** FastTravel
- **Auto-discovered / thunk:** false
- **Comment** (1159 chars):

````text
VIBE_FastTravel_Execute(actorManager, int32 targetId, int32 fieldId, int32 flags, int64 nodeEntry)

Performs the actual fast travel. It attaches the global travel scope
(sub_1408B4510), optionally fills a travel descriptor from the actor at +0x60
(j_sub_14EADDF90), builds a request object (sub_1407782C0) and issues it through the manager.

`nodeEntry` is the 0xD8-byte scene node copied by the caller, and `targetId` is the resolved
travel target.

--- PE 2944 travel record ---
Locator: 48-byte prologue AOB "44 89 4C 24 20 44 89 44 24 18 89 54 24 10 55 53 56 57 41 56 48 8D AC 24 10 FF FF FF 48 81 EC F0 01 00 00 48 8B F1 48 8D 54 24 48 48 8B 0D FE E8" -> exactly 1 match in the whole image at 0x140A9A8D0 (RVA 0xA9A8D0, .code, 584 bytes, 21 basic blocks).
Role in the Trinity travel chain: sole travel-performing callee of VIBE_FastTravel_TryAcceptRequest (called at 0x140655566 with the 0xD8-byte node entry copied by sub_1415DF310).
Trinity: not called or hooked directly; reachable only through the dispatcher. Recorded as the end of the flow so a future PE can be re-anchored from either end.
Confidence: confirmed (locator + existing decompilation).
````

### 26. `VIBE_World_FrameUpdate`

- **Address:** 0x140AD23F0
- **Name:** `VIBE_World_FrameUpdate`
- **Size:** 3176 bytes (0x140AD23F0-0x140AD3058)
- **Basic blocks:** 167
- **Prototype:** `int64_t(int64_t* arg1)`
- **Theme:** World / Time
- **Auto-discovered / thunk:** false
- **Comment** (488 chars):

````text
VIBE_World_FrameUpdate(world) -> ...

Per-frame world update entry point. It calls World::FrameTimerUpdate(world) first and then
runs the frame on every subsystem with the delta the timer produced: the vibration/impulse
accumulators at world->[0xA]+0x38 (clamped to 300), the clock sub-object, the per-entity
update loop over world->[0x18]->[0x88]->[0x78], and the audio/physics bridges
(sub_1408B77B0 / sub_1408B7AF0 / sub_1434DA010 / sub_1409412E0) with
timer->[0x64] and timer->[0x68].
````

### 27. `VIBE_SumRecordSlotStatValues`

- **Address:** 0x140D243C0
- **Name:** `VIBE_SumRecordSlotStatValues`
- **Size:** 336 bytes (0x140D243C0-0x140D24510)
- **Basic blocks:** 11
- **Prototype:** `uint64_t(int64_t* arg1, int16_t arg2)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 28. `VIBE_FastTravel_FlushPendingRequest`

- **Address:** 0x140DBC6C0
- **Name:** `VIBE_FastTravel_FlushPendingRequest`
- **Size:** 346 bytes (0x140DBC6C0-0x140DBC81A)
- **Basic blocks:** 15
- **Prototype:** `void(int64_t* arg1, int64_t arg2, char arg3)`
- **Theme:** FastTravel
- **Auto-discovered / thunk:** false
- **Comment** (1756 chars):

````text
VIBE_FastTravel_FlushPendingRequest(this, key, cancel)

Consumes the pending fast-travel request stored on `this` and clears it:
  +0x958 pending key (must equal the supplied key, otherwise the function returns immediately)
  +0x960 pending sceneId
  +0x964 pending nodeIndex
  +0x968 pending mercenaryHandle

When `cancel` is set the request is dropped without being performed. Otherwise the scene path
is taken when +0x960 (the sceneId) is not -1:
     VIBE_FastTravel_TryAcceptRequest(<rcx ignored>, +0x960, +0x964, 0)
     then +0x960 and +0x964 are set to -1.
Otherwise, when +0x968 (the mercenary handle) is non-zero, the mercenary path is taken:
     VIBE_FastTravel_TryAcceptRequest(<rcx ignored>, -1, -1, +0x968)
     then +0x968 is cleared.

This is the caller that fixes the calling convention of VIBE_FastTravel_TryAcceptRequest:
sceneId in edx, nodeIndex in r8d, mercenaryHandle in r9d, rcx unused.

--- PE 2944 travel record ---
Locator: 48-byte prologue AOB "40 53 48 83 EC 20 48 8B D9 48 39 91 58 09 00 00 0F 85 3F 01 00 00 48 89 7C 24 38 48 C7 81 58 09 00 00 00 00 00 00 83 B9 60 09 00 00 FF 48 89 74" -> exactly 1 match in the whole image at 0x140DBC6C0 (RVA 0xDBC6C0, .code, 347 bytes, 15 basic blocks).
ABI: RCX = this (the map-UI object carrying the pending request), RDX = key that must equal [this+0x958], R8B = cancel flag; returns void.
Scene path proof: 0x140DBC7BA "xor r9d,r9d" then 0x140DBC7BD call -> the ordinary fast travel really is R9D = 0, matching Trinity's travelMode = 0.
Trinity: this is the game's own equivalent of Trinity's queued dispatch (src/game/teleport.cpp fires g_travelFn on the game thread). Recorded as ABI corroboration; Trinity neither hooks nor calls it.
Confidence: confirmed (locator + disassembly).
````

### 29. `VIBE_CompareRecordsBySlotStatValue`

- **Address:** 0x140EDF480
- **Name:** `VIBE_CompareRecordsBySlotStatValue`
- **Size:** 1739 bytes (0x140EDF480-0x140EDFB4B)
- **Basic blocks:** 96
- **Prototype:** `uint64_t(int64_t* arg1, int64_t* arg2, int64_t* arg3, int128_t arg4 @ zmm6, int128_t arg5 @ zmm7)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 30. `VIBE_UiList_BuildSlotEntries`

- **Address:** 0x140F27200
- **Name:** `VIBE_UiList_BuildSlotEntries`
- **Size:** 983 bytes (0x140F27200-0x140F275D7)
- **Basic blocks:** 32
- **Prototype:** `int64_t(void* arg1, int32_t arg2[0x4] @ zmm6)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 31. `VIBE_UiList_BuildSlotEntriesAndSum`

- **Address:** 0x140F6E050
- **Name:** `VIBE_UiList_BuildSlotEntriesAndSum`
- **Size:** 5218 bytes (0x140F6E050-0x140F6F4B2)
- **Basic blocks:** 252
- **Prototype:** `uint64_t(int64_t* arg1, int32_t arg2[0x4] @ zmm6)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 32. `VIBE_Engine_GetTimeValue`

- **Address:** 0x141416B70
- **Name:** `VIBE_Engine_GetTimeValue`
- **Size:** 135 bytes (0x141416B70-0x141416BF7)
- **Basic blocks:** 4
- **Prototype:** `int64_t()`
- **Theme:** World / Time
- **Auto-discovered / thunk:** false
- **Comment** (274 chars):

````text
VIBE_Engine_GetTimeValue() -> int64

Time source used by the frame timer. Lazily initialises the epoch (data_146EFBC30) behind the
once-flag data_146EFBC28 and returns (sub_141416920() - epoch) / 1000000, the division done
with the reciprocal magic 0x431BDE82D7B634DB >> 18.
````

### 33. `VIBE_GamePlayVariableInfoTable_GetById`

- **Address:** 0x141480810
- **Name:** `VIBE_GamePlayVariableInfoTable_GetById`
- **Size:** 295 bytes (0x141480810-0x141480937)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 34. `VIBE_Int16Vector_Append`

- **Address:** 0x1414E07A0
- **Name:** `VIBE_Int16Vector_Append`
- **Size:** 228 bytes (0x1414E07A0-0x1414E0884)
- **Basic blocks:** 12
- **Prototype:** `void(int64_t* arg1, int16_t* arg2, int32_t arg3)`
- **Theme:** Container / Map / Vector helpers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 35. `VIBE_UiList_BuildSlotEntriesFromFactionNode`

- **Address:** 0x14176DEC0
- **Name:** `VIBE_UiList_BuildSlotEntriesFromFactionNode`
- **Size:** 1343 bytes (0x14176DEC0-0x14176E3FF)
- **Basic blocks:** 70
- **Prototype:** `int32_t*(void* arg1, int32_t* arg2, int16_t arg3, int32_t* arg4)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 36. `VIBE_GetRecordSlotStatValue`

- **Address:** 0x141776000
- **Name:** `VIBE_GetRecordSlotStatValue`
- **Size:** 288 bytes (0x141776000-0x141776120)
- **Basic blocks:** 11
- **Prototype:** `int32_t(void* arg1, int16_t arg2, int32_t arg3[0x4] @ zmm0, int32_t arg4[0x4] @ zmm6) __location("zmm0")[0x4]`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 37. `VIBE_UiList_SumRowSkillValues`

- **Address:** 0x1417862F0
- **Name:** `VIBE_UiList_SumRowSkillValues`
- **Size:** 580 bytes (0x1417862F0-0x141786534)
- **Basic blocks:** 29
- **Prototype:** `void*(void* arg1, int64_t* arg2, int64_t* arg3)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 38. `VIBE_UiList_ContainsRowStatus`

- **Address:** 0x141786540
- **Name:** `VIBE_UiList_ContainsRowStatus`
- **Size:** 309 bytes (0x141786540-0x141786675)
- **Basic blocks:** 21
- **Prototype:** `uint64_t(void* arg1, int64_t* arg2)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 39. `VIBE_CharacterStatus_RebuildSlotValues`

- **Address:** 0x1417AAD30
- **Name:** `VIBE_CharacterStatus_RebuildSlotValues`
- **Size:** 3089 bytes (0x1417AAD30-0x1417AB941)
- **Basic blocks:** 74
- **Prototype:** `int32_t*(int64_t* arg1, int32_t* arg2, int128_t arg3 @ zmm6, int128_t arg4 @ zmm7, int128_t arg5 @ zmm8, int128_t arg6 @ zmm9, int128_t arg7 @ zmm10, int128_t arg8 @ zmm11, int128_t arg9 @ zmm12)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 40. `VIBE_CharacterStatus_ApplyDeltaPrimary`

- **Address:** 0x1417AC0C0
- **Name:** `VIBE_CharacterStatus_ApplyDeltaPrimary`
- **Size:** 2433 bytes (0x1417AC0C0-0x1417ACA41)
- **Basic blocks:** 92
- **Prototype:** `uint64_t(int64_t* arg1, void* arg2, int64_t arg3, int64_t* arg4, char arg5, char arg6, char arg7, char arg8, char arg9, char arg10)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment** (1289 chars):

````text
VIBE_CharacterStatus_ApplyDeltaPrimary(statusCtx, timeNow, delta, sourceInfo, arg5, arg6, 0, arg7, arg8, arg9)

Heavyweight variant of VIBE_CharacterStatus_ApplyDelta, entered from Player::damage-apply
when statusId is the primary status - element 0 of the global status id array at
g_engine->[0x10]+0xA0. It also reads element 1 (+0xA2) as the secondary status.

Beyond the clamped accumulator update it:
  * negates the delta when the sign bit is set and the pawn is blocking
    (pawn+0x3F8->[0x18] / pawn+0x398), and for the arg8 == 0xD case;
  * applies the pawn state transition: pawn+0x272 is set to 0x100 / 1, pawn+0x273 and
    pawn+0x274 are updated, the timestamp pawn+0x4D is refreshed and the virtual
    pawn->vtbl+0x150 / +0x268 handlers run;
  * dispatches game events through pa::GameEventHandlerParameter with event codes 0x43 and
    0x44, sub_1418043E0, and the virtual handler statusCtx->[0x0D]->[0x58]->vtbl+0x188;
  * records the last-damage info on the damage tracker returned by
    statusCtx->[0x00]->vtbl+0x1A8: the tracker+0x4C gate, tracker[1] = damage source,
    tracker+0x4D = damage type from the virtual +0x190 handler, tracker+0x4E = arg8,
    tracker[3] = arg9, and the sticky flags tracker+0x52 / +0x53.

Returns the boolean produced by the inner call.
````

### 41. `VIBE_CharacterStatus_GetAccumulatedValue`

- **Address:** 0x1417AD6F0
- **Name:** `VIBE_CharacterStatus_GetAccumulatedValue`
- **Size:** 402 bytes (0x1417AD6F0-0x1417AD882)
- **Basic blocks:** 11
- **Prototype:** `int64_t*(int64_t* arg1, int64_t* arg2, int16_t arg3, int64_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment** (1119 chars):

````text
VIBE_CharacterStatus_GetAccumulatedValue(statusCtx, out, statusId, timeNow) -> out

Reads the current accumulated value of statusId.

  * grouped status (statusinfo+0x11 != 0): requires the group to be active
    (VIBE_CharacterStatus_IsStatusGroupActive) and then reads
    VIBE_CharacterStatus_GetGroupSlotEntry(statusCtx, statusId) through sub_1417B40C0.
    The primary status also needs pawn+0x273 to be set.
  * ungrouped status: index = statusgroupinfo(characterinfo+0x5B8)+0x48[statusinfo+0x14];
    the value is the first qword of the slot record, i.e. *( statusCtx+0x18 + index*0x20 ).

*out is set to the shared "no slot" marker data_146d016D8 when the status has no active
slot, when the group index is -1, or when statusgroupinfo+0x30 is clear.
Returns out.
TRAINER HOOK (this build): the Cheat Engine "Set Attack / Set Defense" table injects at
0x1417AD7DC - the value load of the UNGROUPED branch - and overrides the returned value when
BL (the status id, see `movzx ebx, r8w` at 0x1417AD716) is 3 (Attack) or 4 (Defense).
The grouped branch at 0x1417AD84C is not covered. See the comment at 0x1417AD7DC.
````

### 42. `VIBE_CharacterStatus_GetGroupSlotEntry`

- **Address:** 0x1417ADD40
- **Name:** `VIBE_CharacterStatus_GetGroupSlotEntry`
- **Size:** 133 bytes (0x1417ADD40-0x1417ADDC5)
- **Basic blocks:** 3
- **Prototype:** `int64_t(int64_t* arg1, int16_t arg2)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 43. `VIBE_CharacterStatus_ApplyDelta`

- **Address:** 0x1417AE530
- **Name:** `VIBE_CharacterStatus_ApplyDelta`
- **Size:** 967 bytes (0x1417AE530-0x1417AE8F7)
- **Basic blocks:** 41
- **Prototype:** `uint64_t(int64_t* arg1, int16_t arg2, int64_t arg3, int64_t arg4, void* arg5)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment** (894 chars):

````text
VIBE_CharacterStatus_ApplyDelta(statusCtx, statusId, timeNow, delta, sourceInfo) -> bool

Clamped per-status accumulator update; returns 1 when the clamped value changed.

  * grouped status (statusinfo+0x11 != 0): gated by
    VIBE_CharacterStatus_IsStatusGroupActive, then j_sub_14CAE4220 collects the change and
    the virtual handlers statusCtx->[0x00]->vtbl+0x338 / +0x320 plus sub_1417987C0 are
    notified.

  * ungrouped status: index = statusgroupinfo(characterinfo+0x5B8)+0x48[statusinfo+0x14];
    the slot record is statusCtx+0x18 + index*0x20 = {value, min, max, counter}.
    delta is added to statusCtx+0x38 + index*8, the value is clamped between the slot min
    (+0x08) and max (+0x10), the counter (+0x18) is incremented and the virtual handlers
    vtbl+0x338 / vtbl+0x328 are notified.

Returns 0 when the status is absent (group index == -1) or the value did not change.
````

### 44. `VIBE_Time_ComputeDeltaTicks`

- **Address:** 0x1417AF690
- **Name:** `VIBE_Time_ComputeDeltaTicks`
- **Size:** 293 bytes (0x1417AF690-0x1417AF7B5)
- **Basic blocks:** 14
- **Prototype:** `int64_t*(int64_t arg1, int64_t* arg2, int64_t* arg3, int64_t* arg4)`
- **Theme:** World / Time
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 45. `VIBE_CharacterStatus_IsStatusGroupActive`

- **Address:** 0x1417B0650
- **Name:** `VIBE_CharacterStatus_IsStatusGroupActive`
- **Size:** 142 bytes (0x1417B0650-0x1417B06DE)
- **Basic blocks:** 4
- **Prototype:** `void*(int64_t* arg1, int16_t arg2)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 46. `VIBE_CharacterStatus_AccumulateSlotValue`

- **Address:** 0x1417B2B30
- **Name:** `VIBE_CharacterStatus_AccumulateSlotValue`
- **Size:** 215 bytes (0x1417B2B30-0x1417B2C07)
- **Basic blocks:** 8
- **Prototype:** `int64_t(int64_t* arg1, int16_t arg2, int64_t arg3, int64_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 47. `VIBE_CharacterStatus_ComputeSlotEntries`

- **Address:** 0x1417B2EC0
- **Name:** `VIBE_CharacterStatus_ComputeSlotEntries`
- **Size:** 1003 bytes (0x1417B2EC0-0x1417B32AB)
- **Basic blocks:** 47
- **Prototype:** `void*(int64_t arg1, int64_t* arg2, int128_t arg3 @ zmm6)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 48. `VIBE_IdMap_GetGamePlayVariableValue`

- **Address:** 0x1417E6CB0
- **Name:** `VIBE_IdMap_GetGamePlayVariableValue`
- **Size:** 153 bytes (0x1417E6CB0-0x1417E6D49)
- **Basic blocks:** 10
- **Prototype:** `uint64_t(void* arg1, int16_t arg2)`
- **Theme:** Container / Map / Vector helpers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 49. `VIBE_Actor_GetFieldAttacher`

- **Address:** 0x1417FA740
- **Name:** `VIBE_Actor_GetFieldAttacher`
- **Size:** 58 bytes (0x1417FA740-0x1417FA77A)
- **Basic blocks:** 1
- **Prototype:** `int64_t(void* arg1, int64_t arg2)`
- **Theme:** Actor / Scope plumbing
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 50. `VIBE_FindTargetActorByFriendship`

- **Address:** 0x141ABC170
- **Name:** `VIBE_FindTargetActorByFriendship`
- **Size:** 920 bytes (0x141ABC170-0x141ABC508)
- **Basic blocks:** 53
- **Prototype:** `int64_t*(void* registry, int64_t* outId, void* actor, char requireRowFlag)`
- **Theme:** Trust / Friendship
- **Auto-discovered / thunk:** false
- **Comment** (2088 chars):

````text
VIBE_FindTargetActorByFriendship(registry, outId, actor, requireRowFlag) -> outId

Selects a target actor for `actor` out of the registry's candidate table and writes the
chosen id to *outId (-1 when nothing qualifies).

registry : +0x08 owner object; +0x68 int32 bucketCount, +0x78 buckets, +0x80 nodes (the same
           open-addressed map that NPC::In-place trust uses); +0x6C capacity;
           +0x0A0 a second scope object.
actor    : +0x60 is the key (type / faction id) used both for the map lookup and for the
           trust record.
requireRowFlag : when set, a candidate row must also have row+0x8B non-zero.

Flow:
 1. data_146CE5318 non-zero -> *outId = -1 immediately.
 2. Attach a scoped reference on registry->[8]'s owner.
 3. key = actor->[0x60]; filterBit = VIBE_TargetFilter_GetBitIndex(registry, key) - the bit
    index a candidate row must have set.
 4. Reject when filterBit is 0, when the map is empty, or when `key` is not in it.
 5. Walk the map value's entry array; for each entry resolve
      owner = sub_1405C15A0(&entry); row = owner->[0x120] + entry[4] * 0x90
    and accept it when (!requireRowFlag || row[0x8B]) and bit `filterBit` of row[4] is set.
    For an accepted entry resolve
      trustRec = NPC::In-place trust(registry->[8]->[0x68]->[0x140], actor)
      tierCode = trustRec ? VIBE_Friendship_GetTierCode(trustRec->[0x20]) : 0
    and append the entry to the local candidate list when trustRec is null or the tier code is
    above 0xFE (the top friendship tier).
 6. Release the scope, attach a scope on registry->[8]->[0xA0] and walk the candidates: for
    each one iterate the owner's 0x40-byte sub-entries calling
    sub_14234C080(subEntry, scratch, actor, ...); the first candidate that yields a match is
    written to *outId and the walk stops.
 7. *outId = -1 when nothing matched or when the second scope is unavailable.

This is the function the Cheat Engine "Fast friendship" table hooks - see the comment at
0x141ABC320 inside it. Called from 12 sites (e.g. sub_142B3A040, which passes
*(*(actor+0x68)+0xF8) as the registry).
````

### 51. `VIBE_TargetFilter_GetBitIndex`

- **Address:** 0x141ABCDF0
- **Name:** `VIBE_TargetFilter_GetBitIndex`
- **Size:** 130 bytes (0x141ABCDF0-0x141ABCE72)
- **Basic blocks:** 11
- **Prototype:** `uint64_t(void* arg1, int32_t arg2)`
- **Theme:** Trust / Friendship
- **Auto-discovered / thunk:** false
- **Comment** (434 chars):

````text
VIBE_TargetFilter_GetBitIndex(registry, int32 key) -> uint64

Looks `key` up in `registry`'s map (+0x68 bucketCount, +0x78 buckets, +0x80 nodes; node key at
+0x04, value at +0x08) and returns the bit index a candidate row must have set: the byte at
value+0x11, or the dword at value+0x10 when that byte is >= 6. Returns 0 when the map is empty
or the key is absent, which the caller treats as "no filter" and rejects the whole search.
````

### 52. `VIBE_Friendship_GetTierCode`

- **Address:** 0x141EC9F70
- **Name:** `VIBE_Friendship_GetTierCode`
- **Size:** 141 bytes (0x141EC9F70-0x141EC9FFD)
- **Basic blocks:** 14
- **Prototype:** `int64_t(int64_t arg1)`
- **Theme:** Trust / Friendship
- **Auto-discovered / thunk:** true
- **Comment** (524 chars):

````text
VIBE_Friendship_GetTierCode(int64 friendshipValue) -> int64

Maps a friendship / affinity value to a small tier code by walking a ladder of thresholds
held in the data_146CE77xx / data_146CE78xx globals. Values in the top ranges return 0xFE or
0xFF.

The only caller, VIBE_FindTargetActorByFriendship, accepts a candidate when the returned code
is above 0xFE - i.e. only the top friendship tier qualifies unless the value is forced
upwards, which is exactly what the "Fast friendship" Cheat Engine entry does at
0x141ABC320.
````

### 53. `VIBE_Trust_FindRecordById_L18`

- **Address:** 0x141ECA2D0
- **Name:** `VIBE_Trust_FindRecordById_L18`
- **Size:** 125 bytes (0x141ECA2D0-0x141ECA34D)
- **Basic blocks:** 9
- **Prototype:** `int64_t(void* arg1, int16_t arg2)`
- **Theme:** Trust / Friendship
- **Auto-discovered / thunk:** false
- **Comment** (606 chars):

````text
VIBE_Trust_FindRecordById_L18(container, int16 characterId) -> record*

Id-based sibling of NPC::In-place trust, using the same container layout (map at +0x18,
buckets +0x28, nodes +0x30).

Looks characterId up in the characterinfo table, probes the map with the record's first
int32, and returns the entry-array base stored at node+0x08 - that is, record 0 of the
embedded per-character table. Returns 0 when the map is empty, the bucket has no match, or
the node has no value.

Unlike the actor-based variants it does not apply the characterinfo+0x14B / actor+0x60 keyed
scan; it always returns record 0.
````

### 54. `VIBE_Trust_FindRecordByActor_L38`

- **Address:** 0x141ECB640
- **Name:** `VIBE_Trust_FindRecordByActor_L38`
- **Size:** 261 bytes (0x141ECB640-0x141ECB745)
- **Basic blocks:** 16
- **Prototype:** `int32_t*(void* arg1, void* arg2)`
- **Theme:** Trust / Friendship
- **Auto-discovered / thunk:** false
- **Comment** (588 chars):

````text
VIBE_Trust_FindRecordByActor_L38(container, actor) -> record*

Actor-based twin of NPC::In-place trust for the OTHER container layout: map at container+0x38
(buckets +0x48, nodes +0x50, capacity +0x3C), the layout used by NPC_Trust_Multiplier
(0x141ECB7D0).

Same algorithm: character id from *(*(actor+0x68)+0x20)+0x30, map key from characterinfo's
first int32, node key compared at node+0x04, then
  * characterinfo+0x14B != 0 -> return the entry-array base (record 0), else
  * scan the 0x68-byte entries for the one whose first dword equals actor+0x60.
Returns 0 when nothing matches.
````

### 55. `VIBE_Trust_FindRecordById_L38`

- **Address:** 0x141ECB750
- **Name:** `VIBE_Trust_FindRecordById_L38`
- **Size:** 125 bytes (0x141ECB750-0x141ECB7CD)
- **Basic blocks:** 9
- **Prototype:** `int64_t(void* arg1, int16_t arg2)`
- **Theme:** Trust / Friendship
- **Auto-discovered / thunk:** false
- **Comment** (327 chars):

````text
VIBE_Trust_FindRecordById_L38(container, int16 characterId) -> record*

Id-based twin for the container layout with the map at +0x38 (buckets +0x48, nodes +0x50).
Probes the map with characterinfo(characterId)'s first int32 and returns record 0 of the
embedded per-character table (node+0x08). Returns 0 when there is no match.
````

### 56. `VIBE_Vector68_Grow`

- **Address:** 0x141ECCD40
- **Name:** `VIBE_Vector68_Grow`
- **Size:** 286 bytes (0x141ECCD40-0x141ECCE5E)
- **Basic blocks:** 15
- **Prototype:** `uint64_t(uint64_t* arg1, uint32_t arg2)`
- **Theme:** Container / Map / Vector helpers
- **Auto-discovered / thunk:** false
- **Comment** (470 chars):

````text
VIBE_Vector68_Grow(vec, neededCount)

Grows a vector whose elements are 0x68 bytes: capacity at vec+0x0C, count at vec+0x08,
data pointer at vec+0x00. The new capacity is max((capacity * 3 + 1) / 2, neededCount); the
buffer is reallocated, the existing elements are copied (32 + 32 + 32 + 8 bytes), and the old
buffer is freed through sub_1447F025C / sub_1447F035C depending on the TLS allocator flag.
Raises 0xA0000002 through j_sub_14B985780 when the allocation fails.
````

### 57. `j_VIBE_CharacterStatus_CollectRowEntries`

- **Address:** 0x141F3CB00
- **Name:** `j_VIBE_CharacterStatus_CollectRowEntries`
- **Size:** 4 bytes (0x141F3CB00-0x141F3CB04)
- **Basic blocks:** 1
- **Prototype:** `void(void* arg1, int32_t arg2, int32_t arg3, char arg4, int64_t* arg5)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** true
- **Comment:** (no comment)

### 58. `VIBE_CharacterStatus_GetRowField18`

- **Address:** 0x141F3CF90
- **Name:** `VIBE_CharacterStatus_GetRowField18`
- **Size:** 259 bytes (0x141F3CF90-0x141F3D093)
- **Basic blocks:** 9
- **Prototype:** `int64_t*(void* arg1, int64_t* arg2, int16_t arg3, int32_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 59. `VIBE_CharacterStatus_GetRowField10`

- **Address:** 0x141F3D0A0
- **Name:** `VIBE_CharacterStatus_GetRowField10`
- **Size:** 211 bytes (0x141F3D0A0-0x141F3D173)
- **Basic blocks:** 8
- **Prototype:** `int64_t*(void* arg1, int64_t* arg2, int16_t arg3, int32_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 60. `VIBE_CharacterStatus_GetRowField00`

- **Address:** 0x141F3D180
- **Name:** `VIBE_CharacterStatus_GetRowField00`
- **Size:** 257 bytes (0x141F3D180-0x141F3D281)
- **Basic blocks:** 9
- **Prototype:** `int64_t*(void* arg1, int64_t* arg2, int16_t arg3, int32_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 61. `VIBE_CharacterStatus_GetRowField08`

- **Address:** 0x141F3D290
- **Name:** `VIBE_CharacterStatus_GetRowField08`
- **Size:** 259 bytes (0x141F3D290-0x141F3D393)
- **Basic blocks:** 9
- **Prototype:** `int64_t*(void* arg1, int64_t* arg2, int16_t arg3, int32_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 62. `VIBE_CharacterStatus_GetRowField20`

- **Address:** 0x141F3D3A0
- **Name:** `VIBE_CharacterStatus_GetRowField20`
- **Size:** 211 bytes (0x141F3D3A0-0x141F3D473)
- **Basic blocks:** 8
- **Prototype:** `int64_t*(void* arg1, int64_t* arg2, int16_t arg3, int32_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 63. `VIBE_CharacterStatus_GetRowFieldByRowTable`

- **Address:** 0x141F3D480
- **Name:** `VIBE_CharacterStatus_GetRowFieldByRowTable`
- **Size:** 162 bytes (0x141F3D480-0x141F3D522)
- **Basic blocks:** 7
- **Prototype:** `uint64_t(void* arg1, int16_t arg2, int32_t arg3)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 64. `VIBE_CharacterStatus_InterpolateTierValue`

- **Address:** 0x141F3E250
- **Name:** `VIBE_CharacterStatus_InterpolateTierValue`
- **Size:** 256 bytes (0x141F3E250-0x141F3E350)
- **Basic blocks:** 18
- **Prototype:** `int64_t(void* arg1, int16_t arg2, int64_t arg3, char arg4, int32_t arg5)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** true
- **Comment:** (no comment)

### 65. `VIBE_HashMap_LookupValue`

- **Address:** 0x142146110
- **Name:** `VIBE_HashMap_LookupValue`
- **Size:** 108 bytes (0x142146110-0x14214617C)
- **Basic blocks:** 9
- **Prototype:** `int64_t(void* arg1, int64_t arg2)`
- **Theme:** Container / Map / Vector helpers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 66. `VIBE_GetActiveSlotIndexForRecord`

- **Address:** 0x14214BD30
- **Name:** `VIBE_GetActiveSlotIndexForRecord`
- **Size:** 607 bytes (0x14214BD30-0x14214BF8F)
- **Basic blocks:** 22
- **Prototype:** `int32_t(void* ctx, int64_t recordKey)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 67. `VIBE_GetCharacterStatusValueForSlot`

- **Address:** 0x142153350
- **Name:** `VIBE_GetCharacterStatusValueForSlot`
- **Size:** 495 bytes (0x142153350-0x14215353F)
- **Basic blocks:** 22
- **Prototype:** `int64_t*(void* arg1, int64_t* arg2, int64_t arg3, int16_t arg4)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 68. `VIBE_Record_ConstructDefault`

- **Address:** 0x142155520
- **Name:** `VIBE_Record_ConstructDefault`
- **Size:** 644 bytes (0x142155520-0x1421557A4)
- **Basic blocks:** 18
- **Prototype:** `int32_t*(int64_t* arg1)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 69. `VIBE_Record_AssignCopy`

- **Address:** 0x142156370
- **Name:** `VIBE_Record_AssignCopy`
- **Size:** 1105 bytes (0x142156370-0x1421567C1)
- **Basic blocks:** 24
- **Prototype:** `int32_t*(int32_t* arg1, int32_t* arg2)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 70. `VIBE_Record_EvalSlotStat`

- **Address:** 0x142156B40
- **Name:** `VIBE_Record_EvalSlotStat`
- **Size:** 1014 bytes (0x142156B40-0x142156F36)
- **Basic blocks:** 31
- **Prototype:** `void(void* arg1, char arg2, char arg3, int16_t arg4, int32_t arg5, int64_t arg6, int64_t* arg7)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 71. `VIBE_Record_GetSlotStatValue`

- **Address:** 0x142157010
- **Name:** `VIBE_Record_GetSlotStatValue`
- **Size:** 230 bytes (0x142157010-0x1421570F6)
- **Basic blocks:** 1
- **Prototype:** `int32_t(void* arg1, int32_t arg2) __location("zmm0")[0x4]`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 72. `VIBE_Actor_TryGetGaugeTime`

- **Address:** 0x1421A1D40
- **Name:** `VIBE_Actor_TryGetGaugeTime`
- **Size:** 324 bytes (0x1421A1D40-0x1421A1E84)
- **Basic blocks:** 11
- **Prototype:** `uint64_t(int64_t* arg1, int256_t* arg2)`
- **Theme:** Actor / Scope plumbing
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 73. `VIBE_Record_Entry_SumTierValues`

- **Address:** 0x14240ECC0
- **Name:** `VIBE_Record_Entry_SumTierValues`
- **Size:** 988 bytes (0x14240ECC0-0x14240F09C)
- **Basic blocks:** 57
- **Prototype:** `int64_t(void* arg1, int16_t arg2, char arg3, char arg4)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 74. `VIBE_CharacterStatus_GetTierIndexForStatus`

- **Address:** 0x14240F6A0
- **Name:** `VIBE_CharacterStatus_GetTierIndexForStatus`
- **Size:** 133 bytes (0x14240F6A0-0x14240F725)
- **Basic blocks:** 1
- **Prototype:** `uint64_t(void* arg1, int16_t arg2)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 75. `VIBE_Record_Entry_GetStatusTierIndex`

- **Address:** 0x142414870
- **Name:** `VIBE_Record_Entry_GetStatusTierIndex`
- **Size:** 144 bytes (0x142414870-0x142414900)
- **Basic blocks:** 9
- **Prototype:** `uint64_t(void* arg1, int16_t arg2, int16_t arg3)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 76. `VIBE_Record_Entry_ComputeTierValue`

- **Address:** 0x142414C10
- **Name:** `VIBE_Record_Entry_ComputeTierValue`
- **Size:** 650 bytes (0x142414C10-0x142414E9A)
- **Basic blocks:** 40
- **Prototype:** `int64_t(void* arg1, int16_t arg2, int64_t* arg3)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 77. `VIBE_ApplyRecordToCharacter`

- **Address:** 0x1427990D0
- **Name:** `VIBE_ApplyRecordToCharacter`
- **Size:** 2387 bytes (0x1427990D0-0x142799A23)
- **Basic blocks:** 119
- **Prototype:** `int32_t*(int64_t* arg1, int32_t* arg2, int64_t arg3, void* arg4)`
- **Theme:** Record / slot-stat model
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 78. `VIBE_UiList_QuerySlotEntries`

- **Address:** 0x142845130
- **Name:** `VIBE_UiList_QuerySlotEntries`
- **Size:** 4999 bytes (0x142845130-0x1428464B7)
- **Basic blocks:** 251
- **Prototype:** `int32_t*(uint64_t arg1, int32_t* arg2, int16_t arg3, int32_t arg4)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 79. `VIBE_UiList_QuerySlotEntriesFiltered`

- **Address:** 0x14284FAB0
- **Name:** `VIBE_UiList_QuerySlotEntriesFiltered`
- **Size:** 1608 bytes (0x14284FAB0-0x1428500F8)
- **Basic blocks:** 49
- **Prototype:** `int32_t*(uint64_t arg1, int32_t* arg2, int16_t arg3, int32_t* arg4, int32_t arg5[0x4] @ zmm6, char arg6, int32_t* arg7, int64_t* arg8)`
- **Theme:** UiList
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 80. `VIBE_MercenaryInfoTable_GetRecordById`

- **Address:** 0x148946AA0
- **Name:** `VIBE_MercenaryInfoTable_GetRecordById`
- **Size:** 295 bytes (0x148946AA0-0x148946BC7)
- **Basic blocks:** 13
- **Prototype:** `int64_t(int16_t* arg1)`
- **Theme:** Table resolvers
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

### 81. `VIBE_CharacterStatus_CollectRowEntries`

- **Address:** 0x14E0AA810
- **Name:** `VIBE_CharacterStatus_CollectRowEntries`
- **Size:** 204 bytes (0x14E0AA810-0x14E0AA8DC)
- **Basic blocks:** 15
- **Prototype:** `void(void* arg1, int32_t arg2, int32_t arg3, char arg4, int64_t* arg5)`
- **Theme:** CharacterStatus
- **Auto-discovered / thunk:** false
- **Comment:** (no comment)

## Themes

| Theme | Functions | Address range(s) | What the theme appears to cover |
| --- | --- | --- | --- |
| Table resolvers | 14 | 0x1403880A0-0x14038AB60; 0x140430020; 0x1404A2010-0x1404A2630; 0x1405835F0; 0x1405D1E40; 0x140636580; 0x14066F620; 0x141480810; 0x148946AA0 | Uniform lazily-deserialised global data-table accessors: `XxxInfoTable_GetById(pId) -> record`, each with a shared empty record fallback. These are the game's static-data lookup layer (skills, items, characters, mercenaries, statuses, buffs, fields, factions, scenes, gameplay variables). |
| CharacterStatus | 20 | 0x14066B500; 0x1417AAD30-0x1417B2EC0; 0x141F3CB00-0x141F3E250; 0x142153350; 0x14240F6A0; 0x14E0AA810 | GAS-like status/stat accumulator system on the character status context: applying deltas, clamping, accumulated reads, grouped-status gating, per-row slot computation, tier interpolation and row-field decoding. |
| Record / slot-stat model | 14 | 0x14065FBD0-0x14065FEE0; 0x140D243C0; 0x140EDF480; 0x141776000; 0x14214BD30-0x142157010; 0x14240ECC0-0x142414C10; 0x1427990D0 | Generic "record" object model (construct/copy/evaluate) plus the slot-index and slot-stat accessors shared by the UI-list and status code. |
| UiList | 8 | 0x14042CC40; 0x140F27200; 0x140F6E050; 0x14176DEC0-0x141786540; 0x142845130-0x14284FAB0 | Menu/list builders and queries that turn records into UI slot entries (loadout view, faction-node view, filtered queries, row aggregation). |
| FastTravel | 4 | 0x140654ED0-0x1406550B0; 0x140A9A8D0; 0x140DBC6C0 | Native fast-travel pipeline: destination selection gate, request validation/dispatch, execution and pending-request flush. |
| World / Time | 5 | 0x140951440-0x140952080; 0x140AD23F0; 0x141416B70; 0x1417AF690 | World clock and frame pump: time-scale read, time-dilation curve evaluation, engine time source, delta computation and the per-frame world update. |
| Trust / Friendship | 6 | 0x141ABC170-0x141ABCDF0; 0x141EC9F70-0x141ECB750 | NPC/pet affinity system: trust-record lookup in the open-addressed maps, friendship tier coding and target-actor selection with its filter bit index. |
| Container / Map / Vector helpers | 6 | 0x140393DF0; 0x140436380; 0x1414E07A0; 0x1417E6CB0; 0x141ECCD40; 0x142146110 | Generic containers called by the trust/status code: open-addressed map find-or-create, hash/id-map lookup, 0x68-byte element vector growth, int16 vector append and scoped-reference acquisition. |
| Actor / Scope plumbing | 4 | 0x14064F490; 0x1408B4480; 0x1417FA740; 0x1421A1D40 | Actor/scope glue: global ClientActor scope attacher init, field attacher accessor and the actor gauge-time query. |
| **Total** | **81** | | |

Theme membership is the auditor's grouping by name + comment; it is not stored in the database.

## Functions without comments

**60 of 81** `VIBE_` functions have an empty function-start comment - these are the annotation gaps.

| # | Address | Name | Size (bytes) | Basic blocks | Theme |
| --- | --- | --- | --- | --- | --- |
| 1 | 0x1403880A0 | `VIBE_SkillTable_GetById` | 295 | 13 | Table resolvers |
| 2 | 0x140388AC0 | `j_VIBE_ItemInfoTable_GetById` | 4 | 1 | Table resolvers |
| 3 | 0x1403891B0 | `j_VIBE_MercenaryInfoTable_GetRecordById` | 4 | 1 | Table resolvers |
| 4 | 0x140389570 | `VIBE_CharacterInfoTable_GetRecordById` | 295 | 13 | Table resolvers |
| 5 | 0x14038AB60 | `VIBE_ItemInfoTable_GetById` | 295 | 13 | Table resolvers |
| 6 | 0x140393DF0 | `VIBE_ScopedRef_Acquire` | 141 | 8 | Container / Map / Vector helpers |
| 7 | 0x14042CC40 | `VIBE_UiList_BuildSlotEntriesByLoadout` | 3836 | 207 | UiList |
| 8 | 0x140430020 | `VIBE_FactionNodeTable_GetById` | 295 | 13 | Table resolvers |
| 9 | 0x1405835F0 | `VIBE_StatusInfoTable_GetById` | 295 | 13 | Table resolvers |
| 10 | 0x1405D1E40 | `VIBE_QuestGaugeInfoTable_GetById` | 295 | 13 | Table resolvers |
| 11 | 0x140636580 | `VIBE_StatusGroupInfoTable_GetById` | 295 | 13 | Table resolvers |
| 12 | 0x14064F490 | `VIBE_ScopeAttacher_ClientActor_InitGlobal` | 54 | 1 | Actor / Scope plumbing |
| 13 | 0x14065FBD0 | `VIBE_GetRecordCopyForActor` | 767 | 41 | Record / slot-stat model |
| 14 | 0x14065FEE0 | `VIBE_ScopedActor_GetActiveSlotIndex` | 298 | 16 | Record / slot-stat model |
| 15 | 0x14066B500 | `VIBE_ComputeStatusValueForKey` | 767 | 35 | CharacterStatus |
| 16 | 0x14066F620 | `VIBE_BuffInfoTable_GetById` | 295 | 13 | Table resolvers |
| 17 | 0x1408B4480 | `VIBE_ScopeAttacher_ClientActor_Init` | 138 | 5 | Actor / Scope plumbing |
| 18 | 0x140D243C0 | `VIBE_SumRecordSlotStatValues` | 336 | 11 | Record / slot-stat model |
| 19 | 0x140EDF480 | `VIBE_CompareRecordsBySlotStatValue` | 1739 | 96 | Record / slot-stat model |
| 20 | 0x140F27200 | `VIBE_UiList_BuildSlotEntries` | 983 | 32 | UiList |
| 21 | 0x140F6E050 | `VIBE_UiList_BuildSlotEntriesAndSum` | 5218 | 252 | UiList |
| 22 | 0x141480810 | `VIBE_GamePlayVariableInfoTable_GetById` | 295 | 13 | Table resolvers |
| 23 | 0x1414E07A0 | `VIBE_Int16Vector_Append` | 228 | 12 | Container / Map / Vector helpers |
| 24 | 0x14176DEC0 | `VIBE_UiList_BuildSlotEntriesFromFactionNode` | 1343 | 70 | UiList |
| 25 | 0x141776000 | `VIBE_GetRecordSlotStatValue` | 288 | 11 | Record / slot-stat model |
| 26 | 0x1417862F0 | `VIBE_UiList_SumRowSkillValues` | 580 | 29 | UiList |
| 27 | 0x141786540 | `VIBE_UiList_ContainsRowStatus` | 309 | 21 | UiList |
| 28 | 0x1417AAD30 | `VIBE_CharacterStatus_RebuildSlotValues` | 3089 | 74 | CharacterStatus |
| 29 | 0x1417ADD40 | `VIBE_CharacterStatus_GetGroupSlotEntry` | 133 | 3 | CharacterStatus |
| 30 | 0x1417AF690 | `VIBE_Time_ComputeDeltaTicks` | 293 | 14 | World / Time |
| 31 | 0x1417B0650 | `VIBE_CharacterStatus_IsStatusGroupActive` | 142 | 4 | CharacterStatus |
| 32 | 0x1417B2B30 | `VIBE_CharacterStatus_AccumulateSlotValue` | 215 | 8 | CharacterStatus |
| 33 | 0x1417B2EC0 | `VIBE_CharacterStatus_ComputeSlotEntries` | 1003 | 47 | CharacterStatus |
| 34 | 0x1417E6CB0 | `VIBE_IdMap_GetGamePlayVariableValue` | 153 | 10 | Container / Map / Vector helpers |
| 35 | 0x1417FA740 | `VIBE_Actor_GetFieldAttacher` | 58 | 1 | Actor / Scope plumbing |
| 36 | 0x141F3CB00 | `j_VIBE_CharacterStatus_CollectRowEntries` | 4 | 1 | CharacterStatus |
| 37 | 0x141F3CF90 | `VIBE_CharacterStatus_GetRowField18` | 259 | 9 | CharacterStatus |
| 38 | 0x141F3D0A0 | `VIBE_CharacterStatus_GetRowField10` | 211 | 8 | CharacterStatus |
| 39 | 0x141F3D180 | `VIBE_CharacterStatus_GetRowField00` | 257 | 9 | CharacterStatus |
| 40 | 0x141F3D290 | `VIBE_CharacterStatus_GetRowField08` | 259 | 9 | CharacterStatus |
| 41 | 0x141F3D3A0 | `VIBE_CharacterStatus_GetRowField20` | 211 | 8 | CharacterStatus |
| 42 | 0x141F3D480 | `VIBE_CharacterStatus_GetRowFieldByRowTable` | 162 | 7 | CharacterStatus |
| 43 | 0x141F3E250 | `VIBE_CharacterStatus_InterpolateTierValue` | 256 | 18 | CharacterStatus |
| 44 | 0x142146110 | `VIBE_HashMap_LookupValue` | 108 | 9 | Container / Map / Vector helpers |
| 45 | 0x14214BD30 | `VIBE_GetActiveSlotIndexForRecord` | 607 | 22 | Record / slot-stat model |
| 46 | 0x142153350 | `VIBE_GetCharacterStatusValueForSlot` | 495 | 22 | CharacterStatus |
| 47 | 0x142155520 | `VIBE_Record_ConstructDefault` | 644 | 18 | Record / slot-stat model |
| 48 | 0x142156370 | `VIBE_Record_AssignCopy` | 1105 | 24 | Record / slot-stat model |
| 49 | 0x142156B40 | `VIBE_Record_EvalSlotStat` | 1014 | 31 | Record / slot-stat model |
| 50 | 0x142157010 | `VIBE_Record_GetSlotStatValue` | 230 | 1 | Record / slot-stat model |
| 51 | 0x1421A1D40 | `VIBE_Actor_TryGetGaugeTime` | 324 | 11 | Actor / Scope plumbing |
| 52 | 0x14240ECC0 | `VIBE_Record_Entry_SumTierValues` | 988 | 57 | Record / slot-stat model |
| 53 | 0x14240F6A0 | `VIBE_CharacterStatus_GetTierIndexForStatus` | 133 | 1 | CharacterStatus |
| 54 | 0x142414870 | `VIBE_Record_Entry_GetStatusTierIndex` | 144 | 9 | Record / slot-stat model |
| 55 | 0x142414C10 | `VIBE_Record_Entry_ComputeTierValue` | 650 | 40 | Record / slot-stat model |
| 56 | 0x1427990D0 | `VIBE_ApplyRecordToCharacter` | 2387 | 119 | Record / slot-stat model |
| 57 | 0x142845130 | `VIBE_UiList_QuerySlotEntries` | 4999 | 251 | UiList |
| 58 | 0x14284FAB0 | `VIBE_UiList_QuerySlotEntriesFiltered` | 1608 | 49 | UiList |
| 59 | 0x148946AA0 | `VIBE_MercenaryInfoTable_GetRecordById` | 295 | 13 | Table resolvers |
| 60 | 0x14E0AA810 | `VIBE_CharacterStatus_CollectRowEntries` | 204 | 15 | CharacterStatus |

Gap distribution by theme:

| Theme | Missing | Total | Coverage |
| --- | --- | --- | --- |
| Table resolvers | 12 | 14 | 14% |
| CharacterStatus | 17 | 20 | 15% |
| Record / slot-stat model | 14 | 14 | 0% |
| UiList | 8 | 8 | 0% |
| FastTravel | 0 | 4 | 100% |
| World / Time | 1 | 5 | 80% |
| Trust / Friendship | 0 | 6 | 100% |
| Container / Map / Vector helpers | 4 | 6 | 33% |
| Actor / Scope plumbing | 4 | 4 | 0% |

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
2. **Two spellings for the same role**: the table accessors use both `GetById` (11 names, 10 real
   functions + the `j_` thunk) and `GetRecordById` (3 names: `VIBE_CharacterInfoTable_GetRecordById`,
   `VIBE_MercenaryInfoTable_GetRecordById` and its `j_` thunk). Both take the same
   `(int16_t* pId) -> record` shape, so the difference is purely lexical. `GetById` is the majority form.
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
  `VIBE_CompareRecordsBySlotStatValue` - **0x140EDF480** - direct: per-slot equipment stat math
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
- `VIBE_ScopedRef_Acquire` - **0x140393DF0** - weak: scoped-reference acquire (no comment in the
  database), one of the shared lifetime helpers of this subsystem
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
| Worker level / skills | 3 | 9 | covered by table + record/slot helpers |
| Crime / wanted / bounty | 0 | 6 (faction / target-filter / gauge) | **gap** - topic absent from the database |
| Inventory / items / money | 6 | 8 (+ money: 0) | items/slots covered; money is a **gap** |
| Equipment / refine / sockets | 4 | 12 (refine/sockets by name: 0) | slot-stat machinery covers it, names do not |
| Time / weather / game-speed | 4 | 3 (+ weather: 0) | best-covered feature after FastTravel |
| Trust / friendship / pets | 6 | 3 (+ pet path via `Pet_and_mount::TrustMultiplier` 0x14DF6DA50) | covered |

---

End of audit. Source of truth: the open Binary Ninja database (read-only access, no mutations).

