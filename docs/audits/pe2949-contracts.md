# PE 2949 anchor-contract verification

Image: `E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`
Image base: `0x140000000`

| contract check | expected | actual | result | evidence |
| --- | --- | --- | --- | --- |
| native Slot Size setter: exactly one match | 1 match | 1 | **PASS** | mov qword ptr [rsp + 0x10], rbp; mov qword ptr [rsp + 0x18], rsi; mov qword ptr [rsp + 0x20], rdi; push r14; sub rsp, 0x20; mov rax, qword ptr [rcx + 0x18]; movzx ebp, r9w; mov ecx, dword ptr [rcx + 0x20] |
| native Slot Size setter at plan RVA 0x2135850 | 0x2135850 | 0x2135850 | **PASS** | VA 0x142135850 |
| setter ABI writes bucket+0x16, +0x1A, derived +0x14 | all three store sites present | + 0x16=yes, + 0x1a=yes, + 0x14=yes | **PASS** | capstone sweep of 400 instructions from the setter entry |
| pickup capacity signature: exactly one match | 1 match | 1 | **PASS** | RVA 0x2407822 |
| pickup branch at plan RVA 0x2407824 | 0x2407824 | 0x2407824 | **PASS** | signature RVA + 2 (kInvPickupCapacityPatchSize offset used by InstallPickupCapacityPatch) |
| pickup branch bytes are the reversible original | 74 07 | 74 07 | **PASS** | read straight out of the on-disk image |
| pickup branch decodes as test dl,dl / je +7 | test dl,dl ; je 0x...+7 | test dl, dl ; je 0x14240782d ; movzx ecx, word ptr [rsp + 0x48] | **PASS** | capstone at the signature start |
| RealmFlagOffsetForRevision(2949) matches the image's realm selector | 0x1EC | 0x1EC | **PASS** | kSig_FieldTimeRealm @ RVA 0x20E55B4: mov edx, 0x1ec |
| readiness sentinel kSig_DamageApply_Alt | 1 match | 1 | **PASS** | RVA 0x17AE110 |
| readiness sentinel kSig_CombatTimingEval | 1 match | 1 | **PASS** | RVA 0x873850 |
| readiness sentinel kSig_MoveUpdate | 1 match | 1 | **PASS** | RVA 0x4282090 |
| readiness sentinel kSig_InvGetItemQty | 1 match | 1 | **PASS** | RVA 0x17F8E80 |
| readiness sentinel kSig_EvaluateCrimeWantedState | 1 match | 1 | **PASS** | RVA 0x25D7670 |
| readiness sentinel kSig_TodEngineGlobal | 1 match | 1 | **PASS** | RVA 0x2CE536A |
| readiness sentinel kSig_WeatherRain | 1 match | 1 | **PASS** | RVA 0x3DC39C0 |
| kSig_InvGetHolder exactly one match | 1 match | 1 | **PASS** | RVA 0x212A100 |
| kSig_InvHolderInsert2944 exactly one match | 1 match | 1 | **PASS** | RVA 0x2407780 |
| PE 2850/2760 holder-insert AOB must NOT match PE 2949 | 0 matches | 0 | **PASS** | proves PE 2949 does not silently inherit the PE 2850 frame |
| kSig_InvCoreGlobal (durable container walk, optional) | informational | 0 match(es) | **PASS** | used at inventory.cpp:2374 for TU201 revisions; a zero match degrades the inventory list to lazy discovery only |
| UsesTu201CompatibleRevision admits exactly 2760/2850/2944/2949 | 2949 admitted, unknown revisions rejected | gate present | **PASS** | src/core/version_mapping.cpp:23-30 |
| LocoStepperContractForRevision maps 2949 to the PE2944 contract | Pe2944 (move-owner +0x2C0) | mapped | **PASS** | src/core/version_mapping.cpp:57-70 |
| unknown PE revisions fail closed for movement | default -> Unsupported (offset 0) | present | **PASS** | src/core/version_mapping.cpp:68 |
| SlotSizeOverrideSupportedForRevision returns true for every revision | documented as intentional (PE 2949 branch gates internally) | unconditional true | **PASS** | src/core/version_mapping.cpp:32-37 - install path itself gates on revision == 2949 and disables Slot Size when the setter is not unique |
