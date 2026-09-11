# Trinity: game update maintenance playbook

Start here when Crimson Desert updates and Trinity features stop working.
This is an agent-facing navigation and verification guide, not a claim that
every feature works on the next game build.

For fast binary comparison, use the exact signature snapshot in section 2A.
It duplicates the active AOBs that install features today, grouped by behaviour;
`src/game/offsets.h` remains the single source of truth when the two differ.

Source baseline inspected: 2026-09-06, commit `34d1c50` (inventory limit 1999).
The current revision map includes PE revision 2760 -> TU 2.01.00. Every address,
layout, signature and revision-specific branch must be revalidated for a new EXE.
This document was prepared from source inspection and historical repair notes;
no game execution or live feature validation was performed when creating it.

## 1. First ten minutes

1. Read this file, then inspect `git status --short` and the current commit.
   Preserve unrelated edits. Follow any applicable `AGENTS.md` instructions.
2. Establish the actual game executable path, PID, PE version, SHA-256, module
   base and image size. TU marketing version and PE file version are different.
   Do not assume the installation path, process base or an old dump is current.
3. Identify the loaded ASI path and compare its hash with the intended artifact.
   Read the startup version/build line and failures in `Trinity.log` if logging
   is enabled. A file on disk does not prove the process loaded that same build.
4. If Cheat Engine MCP is available, confirm connectivity, attached PID and module
   inventory. Start with read-only memory, scans and disassembly. For an
   investigative request, establish the cause before editing; perform fixes and
   live mutations within the user's authorized scope.
5. Group failures: overlay/bootstrap, widespread missing signatures, a shared
   player/realm dependency, or one feature. Fix shared dependencies first.
6. Record evidence for each candidate before changing it: match count, RVA,
   instruction boundary, callers, argument layout and the observed game action.

Useful read-only commands, from the repository root in PowerShell:

```powershell
git status --short
git log -5 --oneline
rg -n 'kSig_|kCharMgrAnchors|kOff_|kTls_' src/game/offsets.h
rg -n 'FindPattern|FindAllMatches|CountMatches|InstallHook' src/game src/core
rg -n '2760|2692|revision|Pre201|Legacy|0x1FD|0x1F2' src tests
rg -n 'Readiness|WaitForReadiness|ShouldScanSection' src tests
rg -n 'LOC\(|State::Get|MarkerStatus|ConsumeMarkerResult' src/gui/menu.cpp
```

For the confirmed executable path, use `Get-Item` / `.VersionInfo` and
`Get-FileHash -Algorithm SHA256`; do not hash a similarly named stale copy.

## 2. Source navigation and dependency map

| Area | Start here | What to inspect after a patch |
| --- | --- | --- |
| Startup / readiness | `src/dllmain.cpp`, `src/core/mod.cpp`, `src/core/readiness.*` | Initialization order, representative readiness signatures, timeout/profile selection, installer results |
| Version policy | `src/core/version_detect.*`, `src/core/version_mapping.*` | PE/TU mapping, movement-owner offset, TLS realm offset, inventory RIP-load offset, legacy fuzzy policy |
| Scanner / hooks | `src/mem/scanner.*`, `src/mem/section_filter.*`, `src/mem/hooks.h` | PE section flags, readable regions, uniqueness, RIP resolution, valid hook boundaries |
| Layout registry | `src/game/offsets.h` | AOBs, alternate signatures, member offsets, ABI notes and original reverse-engineering anchors |
| Player / combat | `src/game/player.cpp`, `src/game/player_logic.*` | Character-manager consensus, controlled-body identity, stat pools, damage source/target routing |
| Movement / teleport | `src/game/teleport.cpp`, `src/game/map_marker.h`, `src/game/marker_teleport_logic.h` | Movement and locomotion hooks, node tables, waypoint state, write order and result confirmation |
| Inventory / crime | `src/game/inventory.cpp`, `src/game/inventory_logic.*`, `src/game/inventory_hook_contract.h`, `src/game/crime_hook_contract.h` | Holder identity, transaction ABI, dual-realm commit, slots/stacks, wanted-state routing |
| Trust | `src/game/friendly.cpp`, `src/game/friendly_logic.*` | NPC/pet setters and lookups, trust record layout, pre-write baseline and delta scaling |
| World | `src/game/world.cpp` | Simulation timer, field time, visible sun, weather hooks and environment manager |
| Equipment | `src/game/equipment.cpp`, `src/game/equipment_logic.*` | Selected character/item instance, sockets, refine/durability/stat changes, effect refresh |
| Dye / appearance | `src/game/dye.cpp`, `src/game/dye_data.h`, `src/game/dye_slots_table.h` | Equip capture, channels, apply/upsert/remove functions, render update and durable item records |
| Catalog / assets | `src/game/item_names.*`, `src/game/pak.*`, `src/gui/icons.*` | Item tables, localized names, archive layout, icon lookup; distinguish data failure from hook failure |
| UI / persistence / input | `src/gui/menu.cpp`, `src/gui/framework.cpp`, `src/core/state.h`, `src/core/settings.cpp`, `src/core/localization.cpp`, `src/hooks/input.cpp`, `src/hooks/xinput_hook.cpp` | Actual exposed actions, settings, hotkeys, controller bindings and result messages |
| Renderer | `src/hooks/dx12_hook.cpp`, `src/hooks/hdr_composite_shader.h` | Present/render path, resize, HDR and overlay input when gameplay hooks are healthy |

`offsets.h` is the main registry, but it is NOT the complete dependency list.
For example, `map_marker.h` contains literal offsets, `teleport.cpp` contains
the current waypoint signature, and installers select revision-specific ABIs.
Search all consumers before changing a shared definition.

## 2A. Current AOB snapshot for comparison

This snapshot is intentionally in the update guide so a future repair can compare
the old and new executable without first reconstructing every symbol name. It is
the exact baseline from `src/game/offsets.h` at commit `34d1c50`. `?` and `??`
are existing wildcards, not bytes to fill in. The full comments beside each entry
in `offsets.h` explain the ABI and discovery evidence; read them before changing
the pattern.

### Player, damage and controlled body

```text
kSig_StatCommit        = 48 89 5C 24 10 55 56 57 48 83 EC 20 48 8B 59 18 41 0F B7 E9 48 03 59 20 48 89 D6 48 89 CF 4C 39 C3
kSig_DamageApply       = 48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9
kSig_DamageApply_Alt   = 48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9
kSig_CombatTimingEval  = 48 8B C4 41 55 41 56 41 57 48 83 EC 70 C5 78 29 40 A8
kSig_JustCore          = 48 8B C4 55 41 56 48 81 EC ?? ?? ?? ?? C5 FC 10 89
kSig_JustCore_Alt      = 48 8B C4 55 41 56 48 81 EC ?? ?? ?? ?? 44 0F 29
```

`kCharMgrAnchors` must resolve by consensus, not by taking the first match:

```text
anchor[0], mov +0x0C = 4D 8B 00 49 C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8
anchor[1], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 48 8B 08 4D 8B 00 49 C1 E8 20
anchor[2], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 82 90 00 00 00 48 8D 54 24 ?? 48 8B 08 E8
anchor[3], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 81 58 01 00 00 48 8D 55 ?? 48 8B 08 E8
anchor[4], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 81 60 01 00 00 48 8D 55 ?? 48 8B 08 E8
anchor[5], mov +0x00 = 48 8B 05 ?? ?? ?? ?? 44 8B 07 48 8D 54 24 ?? 48 8B 08 E8
```

Relevant current layout: manager list `+0xB8/+0xC0`; player possessor round-trip
`owner+0xA0 -> possessor+0xD0`; type descriptor `owner+0x88`; stat array `root+0x58`.

### Movement, travel and map marker capture

```text
kSig_MoveUpdate              = 48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56
kSig_LocoStepper             = 48 8B C4 48 89 58 10 44 88 48 20 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 78 F8 FF FF 48 81 EC 50 08 00 00
kSig_LocoStepper_Pre201      = 48 8B C4 48 89 58 10 44 88 48 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 68 F8 FF FF 48 81 EC 60 08 00 00
kSig_TravelToNode            = 48 89 5C 24 18 48 89 74 24 20 89 54 24 10 48 89 4C 24 08 55 57 41 56 48 8D 6C 24 B9 48 81 EC B0 00 00 00 41 8B F0 33 DB 83 FA FF
kSig_TravelToNode_Pre201     = 48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 48 8D 6C 24 B9 48 81 EC B0 00 00 00 41 8B F8 33 DB 83 FA FF
kSig_TravelToNode_Legacy     = 48 8B C4 48 89 58 18 89 50 10 48 89 48 08 57 48 81 EC 80 00 00 00
kSig_MarkerPattern           = C5 FB 10 07 C5 FB 11 02 8B 47 08 89 42 08
kSig_MarkerOriginPrefix      = C5 F8 5C 05
kSig_MarkerPlayer            = 48 8B 06 C5 F8 11 88 B0 01 00 00
kSig_MarkerProtection        = 48 8B 46 08 48 89 F1
```

Current movement layout is position `+0x90`, desired velocity `+0xC0`, velocity
`+0xD0`, with the second teleport destination `+0x1A0`. Map waypoint reading is
implemented separately in `map_marker.h`: `global -> +0xA8 -> destination +0x20/+0x24/+0x28`.

### Inventory, items, localization and crime

```text
kSig_InvGetItemQty           = 66 89 54 24 10 53 57 48 83 EC 28 0F B7 DA
kSig_InvGetItemQty_Legacy    = 48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B E8 0F B7 DA
kSig_InvGetHolder            = 40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41 30
kSig_InvSetExpandSlots       = 48 89 5C 24 ? 56 48 83 EC 20 48 8B 41 ? 48 8B F2 8B 49
kSig_InvHolderInsert201      = 48 89 5C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 00 FE FF FF 48 81 EC 00 03 00 00
kSig_InvHolderInsert         = 48 89 5C 24 ? 4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 10 03 00 00
kSig_InvHolderInsert_Legacy  = 48 89 5C 24 ? 4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC F0 02 00 00
kSig_InvCommit               = 48 89 5C 24 18 66 44 89 4C 24 20 48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 00 FF FF FF 48 81 EC 00 02 00 00
kSig_InvCommit_Pre201        = 4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 48 01 00 00 4D 8B D0 48 8B D1
kSig_InvCoreGlobal           = 48 8B 05 ? ? ? ? 48 8B 48 30 48 8B 49 50 48 89 8D E0 02 00 00 48 85 C9
kSig_InvCoreGlobal_Pre201    = 48 89 54 24 ? 53 48 83 EC 30 48 8B DA C7 44 24 20 00 00 00 00 48 8B 05 ? ? ? ? 48 8B 50 30 48 8B 52 50 48 8B CB E8
kSig_TrItemValueCtor         = 48 89 5C 24 18 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 4C 8B F2 4C 8B E1
kSig_TrItemValueCtor_Pre201  = 48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B EA 48 8B F1 48 C7 01 FF FF FF FF 0F B7 02 66 89 41 08
kSig_InvCommitPlacement201   = 48 89 5C 24 10 48 89 6C 24 20 56 57 41 56 48 83 EC 30 41 0F B7 58 08 48 8B F1 48 8D 4C 24 50 66 89 5C 24 50 45 0F B7 F1
kSig_InvCommitPlacement      = 48 89 5C 24 ? 4C 89 44 24 ? 55 56 57 48 83 EC 30 41 0F B7 59
kSig_InvFreePlacements201    = 48 89 5C 24 10 57 48 83 EC 20 48 89 CB 48 83 39 00 74 ? 31 FF 39 79 08 76 ? 66 0F 1F 44 00 00 89 F8 48 69 C8 E0 00 00 00
kSig_InvFreePlacements       = 48 89 4C 24 08 53 48 83 EC 20 48 8B D9 48 8B 09 8B 43 08 48 69 D0 D8 00 00 00 48 03 D1 E8 ? ? ? ? 90 48 8B 0B 48 8D 43 10 48 3B C8
kSig_LocStringGet            = 8B 41 18 48 8B 0D ? ? ? ? 3B 41 60 72 08 48 8D 05 ? ? ? ? C3 48 03 41 58 C3
kSig_LocStringGet_Alt1       = 8B 51 10 48 8B 05 ?? ?? ?? ?? 48 8B 48 08
kSig_LocStringGet_Alt2       = 48 8B 05 ?? ?? ?? ?? 48 8B 48 08 3B 51 08
kSig_LocStringGet_Legacy     = 8B 41 10 48 8B 05 ?? ?? ?? ?? 48 8B 48 08
kSig_EvaluateCrimeWantedState= 48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 48 8B DA 4C 6B D9 38 41 B0 07
kSig_RegisterCrimeEvent      = 48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC C0 00 00 00 4D 8B F0
```

The shared generic RIP references are `kSig_LeaR8Rip = 4C 8D 05 ?? ?? ?? ??` and
`kSig_MovR8Rip = 4C 8B 05 ?? ?? ?? ??`; inspect their predicates, never hook them.

### World, time and weather

```text
kSig_FrameTimerBody          = 48 8B F9 48 8B 51 60 8B 42 64 89 42 60
kSig_FrameTimerBody_Pre201   = 48 8B F9 48 8B 41 60 C5 FA 10 40 64 C5 FA 11 40 60
kSig_FieldTimeRealm          = BA ?? 01 00 00 48 8B 08 0F B6 04 0A 84 C0 74 0A C5 FC 10 05 ?? ?? ?? ?? EB 08 C5 FC 10 05 ?? ?? ?? ??
kSig_FieldTimeTick           = 48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 64 24 20 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C
kSig_FieldTimeTick_Pre201    = 48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F9 C5 F2 58 41 2C
kSig_TodEngineGlobal         = 83 3D ?? ?? ?? ?? FF 75 ?? 48 89 1D ?? ?? ?? ?? 48 89 3D ?? ?? ?? ?? 44 89
kSig_WeatherRain             = 48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 6C 01 00 00
kSig_WeatherSnow             = 48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 68 01 00 00
kSig_WeatherDust             = 48 8B 41 ?? 41 B8 40 00 00 00 48 85 C0 41 B9 60 01 00 00 48 8D 50 18 B8 CC 01 00 00 49 0F 44 D0
kSig_WindPack                = 48 89 5C 24 08 57 48 83 EC 20 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1
kSig_WindPack_Pre201         = 48 89 5C 24 08 57 48 83 EC 30 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1
kSig_EnvManager              = 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 40 48 8B D7 48 8B 88 E0 0E 00 00
```

### Equipment, dye and trust

```text
kSig_EquipBatch              = 48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4D 8B E0 4C 8B EA 4C 8B F1 4C 8B 79 08
kSig_EquipBatch_Legacy       = 48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4D 8B F8 4C 8B E2 4C 8B F1 4C 8B 69 08
kSig_DyeApplySlot            = 48 83 EC 30 41 0F B7 D9 48 8B FA 48 8B E9 48 8B 41 08 48 8D 50 08 45 33 FF
kSig_DyeApplyBatch           = 48 89 5C 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 4D 8B E0 48 8B F2 4C 8B F1
kSig_DyeApplyBatch_Legacy    = 48 89 5C 24 ? 48 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 20 01 00 00 4D 8B E0 48 8B F2
kSig_DyeUpsert               = 48 8B 41 78 4C 8B D1 44 8B 81 80 00 00 00 49 C1 E0 04
kSig_DyeUpsert_Legacy        = 48 8B 41 ? 4C 8B D1 44 8B 41 ? 49 C1 E0 04
kSig_DyeVisualSet            = 48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 10 FF FF FF 48 81 EC F0 01 00 00 45 0F B7 F1 49 8B F0 48 8B FA
kSig_DyeVisualClear          = 48 89 5C 24 18 44 88 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 45 0F B7 F0 48 8B FA 48 8B F1
kSig_DyeRecordRemove         = 44 8B 91 80 00 00 00 33 C0 4C 8B D9 45 85 D2 0F 84
kSig_EquipEffectRefresh      = 48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B F2 48 8B F1 80 49 22 20 4C 8D 81 00 01 00 00
kSig_EquipEffectRefresh_Legacy = 48 89 5C 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B F2
kSig_FriendlySetNpc201       = 4C 8B DC 53 55 56 57 41 56 41 57 48 83 EC 68 48 8B FA 48 8B F1 0F B7 42 04
kSig_FriendlySetPet201       = 49 89 E3 53 55 56 57 41 56 41 57 48 83 EC 68 48 89 D7 48 89 CE 0F B7 42 04 66 41 89 43 08 49 8D 4B 08 E8 ? ? ? ? 31 ED 39 6E 1C
kSig_FriendlyGetNpc201       = 48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 1C 00
kSig_FriendlyGetPet201       = 48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 42 68 48 8B F9 48 8D 4C 24 30 48 8B F2 4C 8B 40 20 41 0F B7 40 30 66 89 44 24 30 E8 ? ? ? ? 83 7F 3C 00
kSig_FriendlySetNpc          = 4C 8B DC 53 55 56 57 41 56 48 83 EC 60 48 8B FA 48 8D 69 38
kSig_FriendlySetPet          = 49 89 E3 53 55 56 57 41 56 48 83 EC 60 48 89 D7 48 8D 69 18
```

For trust sites, retain the existing hook offset `+0x12`: `kSig_FriendlyTrustSiteA`
is `41 8B C0 EB 4E C5 FC 10 07 C5 FC 11 01 C5 FC 10 4F 20 C5 FC 11 49 20`, and
`kSig_FriendlyTrustSiteB` is `44 89 C0 EB 4E C5 FC 10 07 C5 FC 11 01 C5 FC 10 4F 20 C5 FC 11 49 20`.

### Update comparison matrix

Use the matrix while comparing a new executable. Fill in actual result rather
than relying on assumptions. A function needs its row **and** its dependency rows.

| Feature group | First patterns to compare | Also validate |
| --- | --- | --- |
| Overlay only fails | DX12 path and loader log | Rendering, input, active ASI path; gameplay AOBs may be irrelevant |
| All gameplay fails | readiness sentinels in `mod.cpp` | Scanner PE sections and delayed materialization |
| Combat/player | stat commit, damage apply, manager anchors | Character identity and stat-entry types |
| Movement/teleport | move update, loco, waypoint state | Post-original write order and live readback |
| Add Item/inventory | holder, commit, insert, constructor | Server/client holders, TLS realm and placement ABI |
| Time/weather | frame timer, field-time, TOD, weather | Client/server effect and disabled-state restoration |
| Equipment/dye | equip batch, apply/upsert, refresh | Instance ID, render update and durable inventory copy |
| Trust | current NPC/pet setter and getter pair | Record layout, pre-write baseline and actual relationship change |

## 2B. Runtime contract cards and log triage

The AOB identifies a candidate instruction sequence; these cards identify the
*correct* target and the observable result needed before considering a feature
repaired. Read the startup portion of `Trinity.log` before opening the menu.
Record the exact messages and addresses for the new EXE in the repair entry.

| System | Entry point and required companion | A correct runtime result | High-value failure evidence |
| --- | --- | --- | --- |
| Startup/readiness | `Mod::Initialize`, `GameplayCodeReady`, `WaitForReadiness` | Version line, then hook installation after code has materialized | `Gameplay-code readiness timed out after 180 seconds`; distinguish missing sentinel from scanner section exclusion |
| Local player/combat | Character-manager consensus, stat commit, damage dispatcher | `player: char-manager successfully resolved (N anchors verified)` and the relevant live damage/stat action affects only the controlled actor | `anchors DISAGREE`; never replace consensus with a first-match pointer |
| Waypoint teleport | Current waypoint reader, move update, marker application helper | `map marker queued` followed by a verified post-update write; player visibly arrives and stays there | `marker signatures count mismatch`, `no marker hooks`, or `write failed verification after 3 attempts`; `Queued` is not success |
| Add Item | Authoritative holder capture, item constructor, planner, insertion, commit and TLS switch | `inventory: Added ... [server=1 client=1]`, then item is usable and persists through the relevant reload | `add-item path incomplete`, `authoritative server holder unavailable`, or a result with `server=0`; a visible mirror item is insufficient |
| Slots/stacks/catalog | Expansion setter, item/InventoryInfo table resolver, localization getter | Catalog announces a sane row count; changed capacity allows normal pickups and purchases | `slot-expansion setter hook failed`, catalog global/table errors, or engine validation error `298648703` after a stack override |
| Time/weather | Frame timer, field-time tick/realm, TOD global, individual weather functions | Requested clock, visible sun and weather response; disabling returns normal simulation | `field-clock signature NOT FOUND`, `TOD engine-global signature NOT FOUND`, or `ambiguous`; one working weather control proves only its own hook |
| Equipment/dye | Equip batch, active component, apply/upsert, render leaves/effect refresh | Correct character/slot/instance updates immediately and is still present after re-equip or reload | `effect refresh faulted`, `upsert signature not found`, `visual test only`, or `realm flag unresolved`; render-only success is not persistence |
| Trust | TU-specific NPC/pet setters and getters, baseline cache | A real positive NPC/pet action changes the authoritative relationship by the expected scaled delta | No actual delta, or a source/destination alias that makes the incoming value look unchanged; test NPC and pet separately |

### Recognize the update before selecting a branch

`version_detect.cpp` combines PE version and in-memory dye patterns. A file
version can remain unchanged across Steam updates, and the code currently gives
unknown revisions a compatible-looking fallback display. Therefore record both
the four-part PE version and EXE SHA-256, then compare the current AOB snapshot.
Never select `201`, `Pre201` or `Legacy` merely because a version label looks
familiar. Verify the signature count, function ABI and the runtime contract.

For the current known branch, PE revision `2760` maps to TU `2.01.00`; the
revision policy selects movement owner `+0x2B8`, realm flag `+0x1FD`, current
inventory commit/placement patterns and the TU 2.01 readiness profile. Older
branches use different values such as movement owner `+0x298` and TLS `+0x1F2`.
Those are comparison clues only for a future update, never defaults to copy.

### Fast diagnosis order

1. Confirm loader, correct ASI hash, DX12 overlay and the initialization log.
2. Capture PE version/hash and scan the readiness sentinels before attempting
   individual functions. If several disappear together, inspect scanner sections
   and late code materialization first.
3. Compare the first AOBs for the failing contract card, then locate the new
   routine from its callers/data flow. Verify it is the required writer or
   dispatcher, not a lookup helper, thunk or a visually similar sibling.
4. Update the AOB plus every changed ABI/offset/version branch in one pass.
   Run targeted unit coverage, Release build and CTest.
5. Install the exact verified ASI only while the game is closed. Confirm the
   loaded build/log, then run the action described in the contract card.
6. Write the new evidence into the record at the end of this file. Keep unknown,
   untested and live-failed features explicit; they are the next repair queue.

## 2C. Update-critical patterns outside `offsets.h`

The AOB snapshot alone is deliberately insufficient: two live maintenance
patterns are stored at call sites, not in the central registry. Compare and
move these into `offsets.h` if a future repair changes them, so the next snapshot
remains complete.

```text
// src/game/teleport.cpp — direct reader for a right-click map waypoint.
// Expected: exactly one result before its RIP target is resolved at +7.
kSig_RightClickWaypointRef = 48 8B 05 ?? ?? ?? ?? 48 8B 98 A8 00 00 00 C4 C1 78 10 04 24

// src/game/equipment.cpp — called native helper, not a detour.
kSig_ResizeSocketVector    = 48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00
```

`inventory.cpp` also carries a list of deliberately fuzzy legacy constructor
fallbacks. Use those only when `MayUseLegacyFuzzySignaturesForRevision()` permits
them and only after checking their candidate count and ABI. They are not proof
that a new title update is compatible. Search for `kLegacyCtorSigs` before
changing Add Item behavior.

The map-marker subsystem includes manual inline hooks as well as MinHook detours.
For every manual hook, preserve the expected original bytes, overwritten length,
trampoline replay, jump-back address and `RemoveMarkerHooks()` restoration path.
An AOB that finds the right place is still unsafe if its copied instruction
sequence or overwrite length no longer matches the new function.

## 2D. Signature acceptance record

Do not replace an AOB in source until its record contains all applicable facts:

```text
Symbol / feature / exact source consumer:
Old AOB / new AOB / wildcard reason:
Game EXE SHA-256 / PE version / module base / section name and characteristics:
Match count in the initialized process / each candidate RVA:
Instruction bytes before and after the candidate / function boundary:
RIP displacement location, instruction length and resolved target (when used):
Win64 ABI: RCX/RDX/R8/R9, stack arguments, XMM arguments, return value:
Original-call timing and which thread may call it:
Relevant object layout, pointer chain and realm:
Hook form: MinHook or manual inline; overwrite/replay/jump-back proof:
Live action that proves this is the intended function:
```

The repository's generic `InstallHook` can warn when a signature is ambiguous
and hook the first result. Treat that warning as a failed diagnosis for a newly
updated signature: find why there is more than one candidate or add a semantic
predicate/consensus check. Never accept a first match merely because the game
does not crash immediately.

## 2E. Failure containment, rollback and artifacts

Trinity starts heavy work on `MainThread` after `DllMain`, installs a vectored
crash logger, and each subsystem exposes `Remove()`. This matters during an
update: a crash can mean a wrong target, an ABI mismatch, a stale member offset,
or an initialization/thread-order issue. It is not automatically proof that a
single AOB is bad.

If the game crashes after installing a candidate build, preserve these files
before retrying: `Trinity.log`, `Trinity_Crash.txt`, and, when written,
`Trinity_Crash.dmp`, plus the EXE and installed ASI hashes. The crash report
contains the fault module/RVA, registers, stack frames and active feature flags;
compare them against the hook and object chain just changed. Do not publish these
personal diagnostic files in a release package.

For recovery, close the game, retain the known-good `Trinity.asi` with its
SHA-256, and restore that exact artifact only after recording the failed build's
hash. Verify the restored installed file hash before relaunch. Never use the
historical `tools/deploy_master.ps1` as a generic deploy command: its hard-coded
paths and old version markers make it unsuitable for a future repair without a
fresh review.

`Trinity.ini` can re-enable persisted feature states on launch. When isolating a
crash or behavior regression, preserve the user's original configuration, then
test from a known-safe copy with feature toggles disabled and `fileLogging=1`.
The ASI loader/proxy and `Trinity.asi` must be identified separately; do not add
a second proxy loader to a game installation that already has one.

## 2F. Authority and document precedence

When sources disagree, use this order:

1. The exact initialized game process and a reproduced action.
2. The current source code and its final diff.
3. The verified build artifact and the ASI actually loaded by that process.
4. This playbook's dated snapshot and repair record.
5. Historical README/RE notes and absent-tool references.

For example, `README_TU200_OFFSETS.md` refers to old analysis scripts that are
not present in this checkout and may state scanner rules for an earlier build.
Treat it as background only. The current `section_filter`, scanner behavior and
the exact live PE section characteristics decide what may be scanned. Never copy
an old absolute VA, `.debug` rule, TU label or offset into a new build without
fresh evidence.

## 2G. UI-to-runtime surface map

The menu is the authoritative list of what a user can actually invoke. Before
declaring a title update repaired, use these renderer groups in `src/gui/menu.cpp`
to enumerate the current visible actions; new menu pages must be added to the
feature ledger and relevant contract card.

| Menu renderer group | Runtime systems to trace |
| --- | --- |
| `RenderCombatOptions`, `RenderPlayer`, `RenderMountOptions` | `Player`, `Teleport` movement hooks and shared `State` toggles |
| `RenderTravel`, `RenderFastTravelCats`, `RenderFastTravelNodes`, `RenderSavedLocations` | Travel function/table resolvers, move update and marker status/result flow |
| `RenderInventoryHome`, `RenderInventoryEditor`, `RenderInventoryStorage`, `RenderInventoryAdd`, `RenderInventoryAbyss` | Catalog resolver, quantities, Add Item transaction, slots/stacks, currency and equipment identity |
| `RenderDye*` and `RenderEquip*` | Active equip component, item instance, dye apply/upsert/render leaves, sockets, gear/refine/repair/effect refresh |
| `RenderTimePresets`, `RenderWeatherAtmosphere`, `RenderWorld` | Frame timer, field clock, TOD renderer and weather/environment hooks |
| `RenderRestore*` | Inventory/catalog restore transactions; test each exposed bulk action separately because one successful item add does not validate all of them |
| `RenderKeybinds`, `RenderFontSettings`, `RenderMenuUISettings`, `RenderSystem` | Input hooks, controller masks, localization, persistence, `Trinity.ini`, console/logging and overlay rendering |

The current menu includes one-shot actions and conditional pages that do not map
one-for-one to a persisted `State` boolean. A source update can leave an old
hook healthy while a menu action now calls a changed helper, so compare each menu
action's call chain when a report says one button alone stopped working.

## 3. Re-find functions without guessing

### Widespread NOT FOUND

Check `ShouldScanSection` before replacing signatures. A historical TU 2.00.02
failure involved executable gameplay code in `.debug`; filtering by section name
alone hid valid code. Preserve permission/region checks and exclusion of non-code
debug data. The on-disk image and the initialized live image can differ.

Check delayed code availability next. `GameplayCodeReady` uses different sentinel
sets for known TU 2.01 and older builds. Readiness presence is not uniqueness or
full feature coverage. A stale sentinel can force the entire timeout. Confirm
that a proposed sentinel actually exists in the new build before updating the
profile; do not simply shorten the wait or remove every failing requirement.

### Individual missing or ambiguous signatures

1. Start at the named `kSig_*` declaration and find every consumer with `rg`.
2. Use old disassembly/comments as search clues: RTTI/reflection names, table
   references, field access patterns, call relationships and the operation's data
   flow. Raw historical VAs are not new hook targets.
3. Find the equivalent routine in the new initialized image. Determine whether
   it is a writer, accessor, dispatcher, thunk or inlined replacement.
4. Verify function boundaries, Win64 register/stack arguments, return type,
   overwrite length and original-call behavior before installing a detour.
5. Build an AOB around stable semantics. Wildcard relocation/immediate bytes only
   when justified. Count matches across the same regions the runtime scans.
   Do not assume one match implies the correct semantic target.
6. For globals, resolve RIP-relative loads using the actual instruction length
   and displacement location. Validate indirection depth and object contents.
7. Revalidate structure members and calling contracts even if the AOB survives.
   Change declaration, installer, version policy and consumers together as needed.

Character-manager anchors intentionally include a multi-match fallback whose
sites should agree on one global. Preserve this consensus contract instead of
blindly demanding one raw match or accepting a different realm's manager.

## 4. Critical contracts from previous repairs

### Controlled player and stats

Follow `kCharMgrAnchors` through the character list and the controlled-body
predicate. The old `owner + 0x48` object-type word is not a reliable local-player
selector. Inspect type-descriptor and possessor round-trip checks in the current
implementation, especially after mounting, changing character or loading.

Primary anchors include `kSig_StatCommit`, `kSig_DamageApply`,
`kSig_CombatTimingEval` and `kSig_JustCore`, plus their alternatives. Different
stat entries can look like stamina/spirit while only one governs actual use.
Observe the meter during the relevant action; a full secondary field is not proof.

### Revision switches are executable behavior

At this source baseline, revision 2760 selects movement-owner offset `0x2B8`
instead of `0x298`, and TLS realm offset `0x1FD` instead of `0x1F2` through
`version_mapping.cpp`. Inventory also chooses transaction/placement ABIs by
revision. Unknown future revisions can fall through older defaults. Adding only
a display-name mapping is insufficient; audit every revision branch and caller.

### Add Item, slots and stacks

Re-find `kSig_InvGetHolder`, `kSig_InvCoreGlobal`, `kSig_TrItemValueCtor`,
`kSig_InvCommit`, `kSig_InvHolderInsert201` and placement helpers as a connected
transaction path. Preserve the captured authoritative holder argument and its
container; plausible client inventory memory is not an authoritative substitute.

Require distinct valid server and client holders and the intended realm during
each engine call. Preserve server-first commit and rejection of client-only or
same-holder operations. A useful diagnostic is `server=1 client=1`; additionally
check that the item can be used/equipped and survives the appropriate reload.

Slot size uses `kSig_InvSetExpandSlots` and expansion semantics, not an arbitrary
table-cap write. The baseline UI limit is `kMaxInventorySlots = 1999`; read the
current shared constant and validate the engine's actual capacity behavior.
Preserve non-stackable/unique item handling. Historical forced stack overrides
caused server validation error `298648703`.

### Trust

Use `kSig_FriendlySetNpc201`, `kSig_FriendlySetPet201` and their Get counterparts
to locate the current path. Historical TU 2.01 evidence described `0x68` records
with trust at `+0x28`; verify against the new writer and lookup layout.
Preserve `SelectTrustBaseline` and `ScaleTrustValue` behavior when source and
destination alias. Test a real positive trust change for NPC and pet separately.
Historical notes did not establish complete live trust correctness.

### Right-click map waypoint and teleport

Inspect the signature in `teleport.cpp`, then `ReadCurrentMapMarker` in
`map_marker.h`: current source reads global -> UI state -> destination at `+0xA8`,
then XYZ at destination `+0x20/+0x24/+0x28`. Re-find this using an actual right-click
waypoint that changes position; custom pins and captured hooks may follow other
paths. Historical `hooks=5/5` did not prove capture of this action.

Preserve finite/range/pointer validation. All-zero coordinates are rejected;
zero altitude by itself is valid. `MarkerStatus::Queued` means only queued.
The movement hook applies after `oMoveUpdate`; the current helper writes position
at `+0x90` and `+0x1A0`, clears velocity at `+0xC0/+0xD0`, and checks readback within
`0.5f`. The pending flow allows three attempts. Confirm the new movement layout
before reusing these values. Only confirmed application should produce success
and the corresponding marker/protection side effects. Readback still needs a
visible in-game check for later correction or snap-back.

### Equipment, dye and world

Equipment and dye share equip capture and inventory identity. Check the selected
character/mount, slot tag and instance ID before following refresh/apply functions.
For dye, inspect both visual leaves and persistent inventory records: a color
visible once is not a persistence test. Use equip-batch / dye-upsert callers as
anchors if prologues change.

For time, distinguish simulation speed, field clock and the visible sun. Check
`kSig_FrameTimerBody`, `kSig_FieldTimeTick`, `kSig_TodEngineGlobal` and their
fallbacks. Weather uses rain/snow/dust/wind hooks plus the environment manager;
validate each independently and confirm normal behavior returns when disabled.

## 5. Feature validation ledger

Create one row per actual exposed action from `src/gui/menu.cpp`, including
one-shot buttons that are absent from `State`. The groups below are a checklist,
not a frozen count of features. Include any new or conditional features present
in the checkout/build being repaired.

| Group | Required runtime scenarios |
| --- | --- |
| Player / combat | God Mode, one-hit kill, incoming/outgoing multipliers, durability, fall damage, stamina on foot/mount, spirit, parry and evade; check target discrimination and toggle-off behavior |
| Movement | Super Run, Super Jump, Free Flight up/down bindings; grounded/airborne and mount/character transitions |
| Teleport | Right-click marker moved several times, missing marker, saved locations and exposed node/travel actions; verify actual destination and no snap-back |
| Inventory | Catalog/search, quantity changes, Add Item, bulk/collection actions exposed in UI, slots and stacks; normal pickups/vendor purchases, unique items and persistence |
| Crime / trust | No Bounty and normal crime behavior after disabling; actual NPC/pet trust deltas with multiplier on/off |
| Equipment | Exposed socket/gear, refinement, durability and stat operations; correct instance/character, effect refresh and persistence |
| Appearance | Exposed dye/material/condition/reset actions; supported characters/mounts, immediate render, re-equip and reload |
| World | Game speed, time advance/freeze, sun, presets, clouds/fog, rain/snow/dust/wind; restore normal behavior |
| System | Menu keyboard/controller opening, hotkeys, text capture, localization, settings save/load, resize/HDR where relevant |

Track each action as `not tested`, `static checked`, `built`, `live passed`,
`live failed` or `blocked`, with the exact test and EXE/ASI hashes. An installed
hook, a passing test suite or a success toast cannot mark an entire group passed.

## 6. Build, tests and deployment provenance

Read `CMakeLists.txt` and the build script before running them. At this baseline,
`Build_Trinity.ps1` also rewrites the build timestamp and creates/copies release
packages; it is not a compile-only command. `tools/deploy_master.ps1` should also
be reviewed before use. For a narrow repair, prefer direct CMake in a Visual
Studio x64 developer environment with CMake and Ninja available:

```powershell
cmake -S . -B build-update -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_EXTENDED_HOOKS=OFF
cmake --build build-update --target Trinity TrinityReadinessTests TrinityMapMarkerTests
ctest --test-dir build-update --output-on-failure
git diff --check
```

Stop if any command fails. Select the extended-hooks option to match the
authorized target variant; verify that its sources exist before enabling it.
Do not reuse a build directory configured for a different generator/toolchain.
If CMake is absent from PATH, locate the installed VS CMake/`vcvars64.bat` as
`Build_Trinity.ps1` does; an existing `CMakeCache.txt` provides a local hint,
not a portable path guarantee.

Current CTest targets are `TrinityReadinessTests` (readiness, version, scanner
and extracted gameplay logic contracts) and `TrinityMapMarkerTests` (marker
reading/application). Inspect their cases when selecting regression coverage.
They do not execute the game's engine or validate live signatures.

Before deployment within the user's authorized scope: confirm the game is
closed, preserve a backup of the installed ASI, copy the exact verified artifact,
and compare SHA-256. Relaunch and confirm the actual loaded module/build, then
run the feature ledger. Keep build, installed and release-package hashes aligned.
Do not overwrite an in-use ASI. Do not include personal settings, logs, profiles,
backups or saves in a public package.

Inspect both staged and unstaged diffs before a scoped commit. Include only the
repair's files; verify that the intended substantive logic is actually present.
State separately what passed static checks, build/CTest and live testing.

## 7. Record the next verified repair

Append a compact entry here after an authorized maintenance task:

```text
Date / source commit / build variant:
Game path / TU / full PE version / EXE SHA-256 / live module base:
Built ASI path and hash / installed ASI path and hash:
Observed failing action and reproduction:
Root cause:
Function / signature symbol / old and new RVA / match count:
Evidence for ABI, member offsets, global indirection and realm:
Files changed and regression checks:
Build and CTest result:
Live actions passed / failed / not tested:
Remaining work and next precise diagnostic step:
```

Older background: [REVERSE_ENGINEERING_GUIDE.md](REVERSE_ENGINEERING_GUIDE.md),
[README_TU200_OFFSETS.md](README_TU200_OFFSETS.md),
[TU200_RE_NOTES.md](TU200_RE_NOTES.md). Treat their addresses, TLS constants,
support claims and version assumptions as historical until corroborated by the
current source and exact running game. Prefer symbol searches to stale line numbers.
