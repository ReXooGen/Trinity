#pragma once
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Trinity - Crimson Desert game interface registry.
//
// This is the ONLY place the mod encodes knowledge about the game binary.
// Everything here is a *byte signature* or a *struct offset* rather than an
// absolute address, so a game patch that shifts code around does not silently
// break us: at load time we re-scan for the signatures below and log if any
// fail to resolve. IDB addresses in comments are RVAs from the analysis dump
// (imagebase 0x0) and are documentation only - never used at runtime.
//
// Engine: Pearl Abyss custom engine ("pa::" namespace). RTTI is intact, so
// class/reflection names are durable anchors across updates.
//
// The stat model below was originally derived against the pre-1.17 game build and
// re-validated where noted against Crimson Desert 1.17.00 (EXE SHA-256
// A1DFC0329E177240A978EE4CC3D331E5DDD1903D1055787816199C559E16857C) in
// IDA (every signature here is a confirmed unique match).
// ---------------------------------------------------------------------------

namespace trinity::game
{
    // Any pointer below this is treated as bogus (unmapped / small-int garbage).
    inline constexpr uintptr_t kMinPointer = 0x10000000;

    // --- Actor / character layout ------------------------------------------
    // A gameplay character ("owner" object, vtable 0x50B9A10) classifies itself
    // and reaches its stat/vital chain through these offsets:
    //   owner  [0x48]  -> object-type enum (int; see ObjectType)
    //   owner  [0x68]  -> actor
    //   actor  [0x20]  -> status marker
    //   marker [0x18]  -> root / vital owner (see kOff_Marker_TargetOwner)
    // The object-type enum is the engine's own classification (registered in
    // IDB sub_15F530). NOTE: the +0x48 word is NOT how the local player is
    // found - on the live player it flickers (0/3/8) and several characters
    // read 1 at once during combat. The controlled body is resolved by the
    // engine's own predicate instead: player-class type-descriptor tag +
    // possessor round-trip (see kOff_Owner_Possessor / kOff_Owner_TypeDesc).
    // The enum below is kept only as documentation of the classification.
    inline constexpr uintptr_t kOff_Owner_Actor       = 0x68;
    inline constexpr uintptr_t kOff_Actor_StatusMarker = 0x20; // actor -> status marker
    inline constexpr uintptr_t kOff_Owner_ObjectType  = 0x48;  // int32 ObjectType (documentation only)

    enum ObjectType : int32_t
    {
        Obj_SelfUser            = 0,
        Obj_SelfPlayer          = 1,  // the locally-controlled character
        Obj_GamePlayData        = 2,
        Obj_NonPlayerCharacter  = 3,
        Obj_Mercenary           = 4,
        Obj_Vehicle             = 5,
        Obj_Pet                 = 6,
        Obj_OtherPlayer         = 9,
    };

    // --- Stat / attribute entries ------------------------------------------
    // The engine resolves an attribute to a 0x90-byte "stat entry":
    //   sub_145A5D0(component, attrId) -> entry = [component+0x58] + 0x90*index
    // Layout, reverse-engineered from the commit function sub_C19E1A0 which
    // recomputes and writes these fields:
    //   +0x00 : int32  type id  (see StatType below)
    //   +0x08 : int64  current value  (absolute)   == norm(+0x20) + base(+0x18)
    //   +0x18 : int64  base value
    //   +0x20 : int64  normalized current (current - base)
    //   +0x28 : int64  floor / lower clamp
    //   +0x30 : int64  cap (max) - the value the "is full" flag compares against
    // The commit clamps current DOWNWARD only (a target above current is
    // ignored), so raising a stat requires writing the fields directly.
    inline constexpr uintptr_t kOff_StatEntry_Type    = 0x00; // int32
    inline constexpr uintptr_t kOff_StatEntry_Current = 0x08; // int64 absolute current
    inline constexpr uintptr_t kOff_StatEntry_Base    = 0x18; // int64 base
    inline constexpr uintptr_t kOff_StatEntry_Norm    = 0x20; // int64 current - base
    inline constexpr uintptr_t kOff_StatEntry_Floor   = 0x28; // int64 lower clamp
    inline constexpr uintptr_t kOff_StatEntry_Cap     = 0x30; // int64 max / cap
    inline constexpr uintptr_t kSizeof_StatEntry      = 0x90; // stride between entries

    // A character's stat entries form ONE contiguous 0x90-stride array with
    // health first (the pointer at root+0x58, i.e. [component+0x58]; entry i =
    // base + 0x90*i). The fresh player resolve reaches this health entry from
    // the SelfPlayer character (kOff_Root_StatArray), then derives the stamina
    // and spirit entries by scanning the array and type-checking each slot.
    // NOTE: a body carries more than one stamina-typed entry - the sprint gauge
    // (type 20) sits several slots past the type-17 meter - so we track ALL
    // stamina-typed slots, not one offset.
    inline constexpr int kStatArray_ScanEntries = 16; // slots scanned from health

    // Stat entry type ids (these are the *type* tags stored at entry+0x00, not
    // the attribute enum index). Confirmed against this build.
    enum StatType : int32_t
    {
        StatType_Health      = 0,
        StatType_Stamina     = 17, // a stamina-typed gauge, but NOT the sprint one
        StatType_Spirit      = 18, // internal spirit / mount energy gauge
        StatType_MountSprint = 19, // authoritative mount/horse gallop & sprint gauge
        // The gauge that actually depletes while sprinting. Found empirically:
        // it was the only array slot decreasing during a sprint (base 120000,
        // draining ~7000/s), and unlike the others it carries a real cap.
        // Type 17 pins full without stopping sprint; type 20 is the real gate.
        StatType_SprintSt   = 20,
        // The HUD Spirit bar. Type 18 is a partially-filled internal gauge that
        // never moves on screen; the displayed 0-30 pool (confirmed against the
        // in-game stat screen) is this type instead. Same trap as Stamina above.
        StatType_SpiritPool = 21,
        // Live 1.17 capture (0.13.26): these are the authoritative gameplay
        // pools. Type 22 at slot 12 fell from 320000 while sprinting; type 23
        // at slot 13 fell from 110000 while spending spirit. The older 17/18
        // entries remain full and are internal/secondary gauges on this build.
        StatType_StaminaPool117 = 22,
        StatType_SpiritPool117  = 23,
    };
    // NOTE (movement speed): the player stat array also carries two "rate"
    // entries (type 30 and type 74) that rest at 100000 == 1.0x, but writing
    // them has NO effect on locomotion - on-foot movement speed is driven by
    // the character physics move controller, not the stat array. Left for a
    // future milestone; see the project notes.

    // --- Signatures --------------------------------------------------------

    // --- God Mode: guard the single stat-commit choke point ----------------
    // Every HP change - combat, fall damage, drains, heals - is applied by one
    // of several "apply" functions (IDB sub_1459D30 / sub_145B9E0 / sub_145C0F0
    // / sub_145FE10), and ALL of them funnel their final value through ONE
    // commit, pa_StatCommit (IDB sub_BED7820):
    //     int64 pa_StatCommit(void* entry, int64 time, int64 target, uint16 f)
    // It reconstructs current = base(+0x18) + norm(+0x20), clamps it to `target`
    // / floor, then writes current(+0x08) and norm(+0x20). It is the sole writer
    // of the authoritative current-HP field.
    //
    // God Mode hooks it: after the original runs, if the entry is the player's
    // tracked health entry, we force current back to full. Because that happens
    // synchronously inside the commit call - the instant HP is written, before
    // any death check up the stack reads it - a single huge hit (fall damage,
    // one-shots) can never be observed at a lethal value. This is the old
    // per-frame pin moved to the exact write site, which removes the between-
    // frame race that let fall damage kill.
    //
    // pa_StatCommit lives in the .link section (not .text); the scanner walks
    // the whole committed image, so that is fine. Prologue: mov [rsp+10],rbx;
    // push rbp/rsi/rdi; sub rsp,20; mov rbx,[rcx+18]; movzx ebp,r9w;
    // add rbx,[rcx+20] (base+norm); ... Unique match.
    inline constexpr const char* kSig_StatCommit =
        "48 89 5C 24 10 55 56 57 48 83 EC 20 48 8B 59 18 41 0F B7 E9 48 03 59 20 48 89 D6 48 89 CF 4C 39 C3";

    // --- Damage multipliers: hook the damage-apply dispatcher ---------------
    // One level above pa_StatCommit sits a per-status "apply signed delta"
    // dispatcher (IDB sub_145B2A0):
    //   int64 pa_StatApplyDelta(void* targetOwner, uint16 statusId, int64 time,
    //                           int64 delta, void* sourceCtx,
    //                           char a6..a10, void* out)
    // It early-outs on delta == 0, routes the primary vital (HP, statusId 0)
    // into a dedicated HP helper (IDB sub_1459400) and every other status into
    // a generic sibling. All battle damage - incoming AND outgoing - passes
    // through it while BOTH sides are still identifiable:
    //   targetOwner          : the victim's vital-owner object; for a tracked
    //                          character it is marker+0x18 (see below)
    //   sourceCtx +0x68      : the attacker's actor. sourceCtx is the same
    //                          object our player chain calls "owner"
    //                          (marker+0x08), so the existing chain offsets
    //                          identify the attacker.
    // Argument/caller layout cross-checked against an earlier game build
    // (sub_1412D8340 there - byte-identical dispatch shape) and re-validated
    // in our dump: unique match at 0x145B2A0.
    inline constexpr const char* kSig_DamageApply =
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9";

    // marker+0x18 -> the character's vital/target owner: the object battle
    // damage is addressed to (the `targetOwner` argument above). Validation:
    // its first qword points back at the marker.
    inline constexpr uintptr_t kOff_Marker_TargetOwner = 0x18;

    // --- Fresh player resolution (character manager) -----------------------
    // The churn-proof alternative to the accessor-discovered stat-entry ring:
    // the gameplay-character manager owns a vector of every live character,
    // and the local player is the single one whose ObjectType is SelfPlayer.
    // Resolving it fresh each tick yields an always-current player with no
    // stale cache - a body transition (mount/transform) reallocates the
    // character, but the next resolve simply returns the new one. See the
    // trinity-engine-architecture notes for the full derivation.
    //
    // The manager is read as `manager = *(*G)` from ~30 sites, each of which
    // then passes it as ARG1 to one of the char-manager API functions.
    //
    // Why the original signature broke, and what replaced it. It keyed on the
    // caller's prologue plus the register the manager was cached into:
    //   mov r14,rdx; mov r12,rcx; mov rax,cs:G; mov r15,[rax]; lea rdi,off_...
    // Every one of those registers is the register allocator's free choice. A
    // game update recompiled that function and duly renamed `mov r15,[rax]` ->
    // `mov rsi,[rax]` and `lea rdi` -> `lea r13`, killing the match while the
    // code's MEANING was untouched.
    //
    // The anchors below instead key only on the bytes between the load and the
    // call - `mov rcx,[rax]` (manager is arg1), the arg2/arg3 setup, and
    // literal struct offsets. Those registers are fixed by the Win64 ABI and
    // the constants are fixed by the struct layout, so none of it is the
    // allocator's to rename. Each anchor is independently unique image-wide and
    // resolves the same global; Install() cross-checks that they agree, so a
    // future update has to break all of them at once to disable the feature,
    // and a single stale survivor cannot quietly win the vote.
    //
    // CAUTION: the same API family is also called with two SIBLING globals
    // (0x61830D0 / 0x61830D8 in the current dump) - the client/server realm
    // split, see the trinity-engine-architecture notes. Do NOT loosen these
    // into "any global passed to the char-manager API": that also matches the
    // wrong realm's manager, which resolves fine and then fails silently. Keep
    // every anchor tied to a specific call site.
    //
    // In the current dump all four resolve to qword_61830F8 (was qword_6181090
    // before the update).
    struct CharMgrAnchor
    {
        const char* sig;
        uintptr_t   movOff; // offset of `mov rax,cs:<global>` (7-byte instr) within the match
    };

    inline constexpr CharMgrAnchor kCharMgrAnchors[] = {
        // sub_22E6330: mov rax,cs:G / mov rcx,[rax] / mov r8,[r8] / shr r8,20h.
        // Best of the set - pure ABI arg setup plus a literal shift count.
        {"48 8B 05 ?? ?? ?? ?? 48 8B 08 4D 8B 00 49 C1 E8 20", 0},
        // sub_251E3B0: mov r8d,[rdx+90h] / lea rdx,[rsp+..] / mov rcx,[rax] / call.
        // rdx is the incoming arg2 at entry; 0x90 is a struct offset.
        {"48 8B 05 ?? ?? ?? ?? 44 8B 82 90 00 00 00 48 8D 54 24 ?? 48 8B 08 E8", 0},
        // 1.17: this caller now reads +0x158 (was +0x160) before the same manager call.
        // rcx is the incoming arg1 at entry; the literal field offset is part of the anchor.
        {"48 8B 05 ?? ?? ?? ?? 44 8B 81 58 01 00 00 48 8D 55 ?? 48 8B 08 E8", 0},
        // sub_2514EB0 / sub_22EBC00: mov r8d,[rdi] / lea rdx,[rsp+..] / mov rcx,[rax] / call.
        // Weakest of the set (rdi is allocator-chosen) and it matches BOTH of
        // those sites - but both resolve to the same global, so it still votes
        // correctly. Kept as a fallback.
        {"48 8B 05 ?? ?? ?? ?? 44 8B 07 48 8D 54 24 ?? 48 8B 08 E8", 0},
    };

    // Character manager -> the vector of all gameplay characters. It is the
    // engine's custom pa vector (data ptr, then u32 size, u32 capacity);
    // element i is a character* at data + 8*i. Confirmed against the character
    // factory sub_24AA890 (registers each new character via
    // sub_589D00(mgr+0xB8, &char)) and the append helper sub_589D00.
    inline constexpr uintptr_t kOff_CharMgr_ListData  = 0xB8; // character*[] data ptr
    inline constexpr uintptr_t kOff_CharMgr_ListCount = 0xC0; // u32 count
    inline constexpr uint32_t  kCharList_MaxCount     = 8192; // sanity bound (live ~388)

    // Selecting the ONE controlled body among SelfPlayer-typed characters.
    // objType==1 is unique only AT REST (live-confirmed: 388 chars, exactly one
    // type-1). During combat / body transitions the engine spawns transient
    // SelfPlayer-typed characters, so several can carry objType==1 at once and
    // "first type-1 in the vector" flickers between the real body and transients
    // (observed live: the resolved player oscillated every frame).
    //
    // The engine's OWN local-player accessor (IDB sub_2393AA0, the only reader
    // that walks this list to return "the player") disambiguates with a
    // POSSESSOR ROUND-TRIP: the controlled character's possessor/controller at
    // owner+0xA0 points BACK at the character via possessor+0xD0. Only the one
    // body its controller actually possesses satisfies
    //   *(*(owner+0xA0)+0xD0) == owner
    // so this is a deterministic single source of truth, not a heuristic - and
    // it is self-validating: only a real pointer round-trip can match, so a
    // wrong offset resolves to nothing rather than to a wrong character.
    // LIVE-CONFIRMED: exactly one character matches the round-trip per tick,
    // stable across combat/transitions, while the +0x48 objType count swings
    // wildly - the round-trip is the true single source of truth.
    inline constexpr uintptr_t kOff_Owner_Possessor  = 0xA0; // -> possessor/controller
    inline constexpr uintptr_t kOff_Possessor_Pawn   = 0xD0; // -> back-ref to owner

    // The engine's class gate in that accessor is NOT the +0x48 objType word
    // (which on the LIVE player flickers 0/3/8 and is unusable): it reads the
    // type-descriptor at owner+0x88 and tests its tag byte (*(owner+0x88)+1)
    // with ((tag - 1) & 0xF7) == 0, i.e. tag 1 (SelfPlayer) or 9 (OtherPlayer)
    // = a player-class character (see also sub_30DF50, the same tag switch).
    // This tag reads a stable 1 on the player. It is the ONLY type read used.
    inline constexpr uintptr_t kOff_Owner_TypeDesc = 0x88; // -> type descriptor (tag byte at +1)

    // A resolved character IS the god-mode "owner" object (vtable 0x50B9A10):
    // its ObjectType is at +0x48 (kOff_Owner_ObjectType) and its vital chain is
    //   owner -> actor(+0x68) -> marker(+0x20) -> root(+0x18) -> statArray(+0x58)
    // root is the same "component" the stat accessor takes as its argument, so
    // the stat-entry array is the pointer at root+0x58 whose first entry
    // (index 0) is the Health entry. Type-checking that entry as Health
    // validates the whole walk before we trust it.
    inline constexpr uintptr_t kOff_Root_StatArray = 0x58; // ptr -> stat entry[0] (Health)

    // --- World: live position (read-only milestone) ------------------------
    // The per-actor "physics move controller" object carries a 16-byte
    // position vector (x,y,z,w floats) at +0x90. A dedicated SIMD movement-
    // integration function (IDB sub_3A3E140) is called once per tick with
    // that controller as its first (rcx) argument and writes the updated
    // position back to [rcx+0x90] before returning; hooking its ENTRY and
    // reading rcx+0x90 *after* calling through gets the just-updated value
    // with a plain function hook (no mid-instruction/codecave hook needed).
    //
    // NOTE (2026-07-09): statically, this function is reached through a
    // polymorphic per-actor movement-tick dispatch (sub_2F4A720, itself only
    // reachable via a vtable slot), not an obviously input-only/player-
    // exclusive path, so it looked like it might fire for NPCs/mounts too.
    // LIVE-VERIFIED CLEAN, though: the user confirmed the tracked coordinates
    // track the local player correctly in-game (no static player-identity
    // chain to this controller was ever found - the movement-tick dispatch
    // apparently only reaches this path for the player in practice).
    //
    // Prologue: mov rax,rsp; mov [rax+20h],r9; mov [rax+10h],rdx; push rbp;
    // push r14. Unique match in this build.
    inline constexpr const char* kSig_MoveUpdate =
        "48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56";
    inline constexpr uintptr_t kOff_MoveOwner_Position = 0x90; // x,y,z,w f32
    // The integrator writes the frame's velocity back to +0xD0 (it computes
    // new position = pos + velocity and stores the velocity at [a1+208]).
    // Zeroing this while pinning a teleport keeps the integrator from flinging
    // the proxy on the next tick.
    inline constexpr uintptr_t kOff_MoveOwner_Velocity = 0xD0; // x,y,z,w f32

    // --- Super Jump: scale the integrator's INPUT velocity -------------------
    // Scales the up component of the desired-velocity vector at +0xC0
    // (world-space: [0]=x, [1]=y up, [2]=z) before sub_3A3E140 (the Havok
    // character-proxy integrator) consumes it. Works because a jump is a
    // ballistic impulse: once airborne there is no ground/locomotion authority
    // fighting the scaled velocity. Rising-only (positive y above the
    // threshold) so falling and stair-stepping are never amplified.
    inline constexpr uintptr_t kOff_MoveOwner_DesiredVel = 0xC0; // x,y,z,w f32 (input velocity)
    inline constexpr int       kIdx_MoveOwner_Up         = 1;    // vertical component (y)
    // Only amplify an upward velocity that is clearly a jump/launch, not the
    // small +y jitter of walking over steps/slopes, so ground movement is left
    // alone. Units are the engine's own velocity scale (position is ~cm).
    inline constexpr float     kSuperJump_RiseThreshold  = 1.0f;

    // --- Super Run: scale the locomotion stepper's drive velocity ------------
    // GROUNDED movement speed CANNOT be won at the integrator - three
    // attempts inside sub_3A3E140, all live-disproven (2026-07-14):
    //   (a) flat-multiply the input velocity (+0xC0): pulses on uneven
    //       ground, dead uphill;
    //   (b) scale the resolved position delta (+0x90 diff across the call):
    //       still stuttery;
    //   (c) (a) plus corrections for the integrator's prev-velocity fast-path
    //       heuristic (+0xE0) and a pre-applied ground-constraint clip (its
    //       arg2 plane): downhill perfect, uphill still EXACTLY 1x, rough
    //       roads stutter.
    // Scaling the "CharacterMoveSpeedInfo" data table also had zero effect:
    // each character's actionchart binary (actionchart/bin__/<name>.paac,
    // loader IDB sub_1B47F60) embeds its OWN copy of the move-speed data
    // (AnimationInfo moveSpeedInfoArray) - the global table is design-source
    // only, never read back at runtime.
    //
    // WHY the integrator can't be beaten (HW-watchpoint trace of +0xC0,
    // 2026-07-14): the character movement component runs a SERVO. Per tick it
    //   1. computes a drive velocity,
    //   2. passes it as ARG3 into a sub-step driver (IDB sub_2F49550) that
    //      writes it to moveOwner+0xC0 and calls the integrator itself, then
    //   3. measures the displacement that actually happened and books it back
    //      (IDB sub_2F4DE00 writes +0xC0 = (pos_after - pos_before) / dt).
    // Any velocity injected inside the integrator makes the body overshoot
    // what the servo expected and step 3 pulls it right back - pulsing on the
    // flat, hard 1x clamp uphill.
    //
    // THE FIX (live-verified via Frida arg3 scaling: "worked very well, very
    // smooth", including uphill, up to 10x): hook the sub-step driver and
    // scale the HORIZONTAL components of arg3 before the servo consumes it.
    // Vertical stays untouched (gravity rides in arg3 y at ~-55 while
    // grounded; scaling it would slam the character into the ground).
    //
    // MS x64: a1=rcx component, dt=xmm1 f32, arg3=r8 -> f32 x,y,z drive
    // velocity, a4=r9b, a5..a7 stack. The component holds the move-owner
    // (integrator arg1 / hknp proxy) at +0x298 - not needed for gating (this
    // dispatch only runs for the local player in practice, same as
    // kSig_MoveUpdate above; Super Jump ships ungated on the same evidence).
    //
    // GOTCHA (cost a live debug round-trip): arg3 points at a scratch buffer
    // at a very LOW address (~0x013FDD70) - far below kMinPointer. The
    // mem::Read32/Write32 helpers reject anything under that floor, so reading
    // the drive vector through them silently returns false and the scale
    // no-ops (symptom: hook installs and fires, but every component reads 0.00
    // and speed never changes). Access this vector raw + SEH-guarded instead;
    // the floor is for validating pointer CHAINS, not arguments the callee is
    // about to dereference anyway.
    //
    // Signature = prologue + home-store/push sequence + the exact frame setup
    // (lea rbp,[rax-798h]; sub rsp,860h). The frame displacements are what
    // make it unique - 6 same-shaped functions match if they are wildcarded.
    inline constexpr const char* kSig_LocoStepper =
        "48 8B C4 48 89 58 10 44 88 48 20 55 56 57 41 54 41 55 41 56 41 57 "
        "48 8D A8 68 F8 FF FF 48 81 EC 60 08 00 00";

    // --- Fast travel / map-gimmick teleport --------------------------------
    // The world map fast-travels through sub_505140(ignored, sceneId, nodeIndex)
    // (IDB 0x505140): a normal, server-blessed travel that streams properly (the
    // only reliable long-range teleport - every memory-write approach desyncs the
    // Havok body; see trinity-open-questions). The first arg is unused (it feeds
    // sub_5019D0, which pulls the travel manager from a global and ignores it),
    // so we pass nullptr. It validates nodeIndex < nodeCount then triggers travel.
    //   char sub_505140(void* /*ignored*/, int sceneId, unsigned nodeIndex)
    // Prologue: mov rax,rsp; mov [rax+18],rbx; mov [rax+10],edx; mov [rax+8],rcx;
    // push rdi; sub rsp,80h. Unique in this build (IDB 0x505140).
    inline constexpr const char* kSig_TravelToNode =
        "48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 48 8D 6C 24 B9 "
        "48 81 EC B0 00 00 00 41 8B F8 33 DB 83 FA FF";

    // The destinations live in the LevelGimmickSceneObjectInfo registry, a global
    // (IDB qword_6185008), read through its resolver sub_396CC0(u32* sceneId)
    // which returns the scene descriptor (and lazy-loads the data table row on
    // first touch - so it must only be called on the game thread).
    //
    // The resolver body is the shared table-resolver template clone: a byte
    // pattern on it matches ~25 sibling table resolvers (first match in this
    // build is a DIFFERENT table near 0x3318C2 - hooking that produced the
    // v1/v2 garbage menu of four ~25k-node "scenes"). Like the area-name table
    // below, it is found by the string-anchored scan on its unique table-name
    // string instead (see kStr_LevelNameTable for the algorithm).
    //
    // Registry / scene-descriptor / node layout (all live - these tables read
    // zero in the static dump):
    //   registry  +0x08 u32  sceneCount   (resolver keys are 0..count-1)
    //   registry  +0x50 ptr  sceneTable   (desc = *(sceneTable + 8*sceneId),
    //                        null until the resolver lazy-loads the row - use
    //                        the resolver, not the raw slot)
    //   sceneDesc +0x28 u32  nodeCount
    //   sceneDesc +0x20 ptr  nodeArray   (node  = nodeArray + 0xC0*index)
    //   node      +0x10 ptr  gimmick object (a run of std::string members:
    //                        sector keys, an item code, a type template)
    //   node      +0x6c f32  world position x,y,z  (+0x50 is scale, +0x5c a quat)
    // Each "scene" is one gimmick TYPE (scene 0 = bells, 111 = ores, 165 = boards,
    // ...); the node index enumerates every instance of that type on the map.
    inline constexpr const char* kStr_GimmickSceneTable = "LevelGimmickSceneObjectInfo";

    inline constexpr uintptr_t kOff_Registry_SceneCount = 0x08; // u32
    inline constexpr uintptr_t kOff_Registry_SceneTable = 0x50; // ptr[]
    inline constexpr uintptr_t kOff_SceneDesc_NodeCount = 0x28; // u32
    inline constexpr uintptr_t kOff_SceneDesc_NodeArray = 0x20; // ptr
    inline constexpr uintptr_t kNode_Stride             = 0xC0;
    inline constexpr uintptr_t kOff_Node_Gimmick        = 0x10; // ptr -> gimmick object
    inline constexpr uintptr_t kOff_Node_Position       = 0x6C; // f32 x,y,z

    // The scene descriptor is the reflected class LevelGimmickSceneObjectInfo
    // (112 bytes; field names recovered from its deserializer's error strings,
    // IDB sub_1175F40). The two fields that fixed the fast-travel menu:
    //   _stringKey  : the scene's authored name ("MineIron_01", "TreasureBox",
    //                 "AbyssRuins_Field", ...) - an engine refcounted string:
    //                 desc+0x08 -> string object -> first qword = char* buffer.
    //   _useTeleport: TRUE only for real fast-travel scenes. This is exactly
    //                 the world map's own filter: sub_B58680 marks a map pin
    //                 travelable only when its scene has this flag (7 scenes in
    //                 the live build: the overworld artifact network, Abyss
    //                 Island/Bridge/Core, housing and the two standstone sets).
    //                 Everything else (ores, chests, bells, shops...) is not a
    //                 travel destination, which is why the unfiltered v1 menu
    //                 "barely worked".
    inline constexpr uintptr_t kOff_SceneDesc_StringKey   = 0x08; // engine string
    inline constexpr uintptr_t kOff_SceneDesc_IsBlocked   = 0x10; // bool
    inline constexpr uintptr_t kOff_SceneDesc_LevelName   = 0x18; // engine string
    inline constexpr uintptr_t kOff_SceneDesc_UseTeleport = 0x49; // bool
    inline constexpr uintptr_t kOff_SceneDesc_IsEmpty     = 0x6C; // bool

    // --- Named area boxes (waypoint display names) --------------------------
    // The game has NO per-node names; its map labels AREAS. The data table
    // "FieldLevelNameTableInfo" (registry global IDB qword_619E708, resolver
    // IDB sub_B7B6850) maps a field id (u32) to a hash map of LevelNameInfo
    // entries, each = a named world-space AABB. Point-in-box over these gives
    // the game's own area name for any position - live-verified: the overworld
    // fast-travel artifacts resolve to "AbyssRuins_Her_0021" (Her = Hernand)
    // style names, region-coded (CD/Del/Dem/Her/Kwe).
    //
    // The resolver's code is a template clone shared by ~24 data-table
    // resolvers, so no byte pattern on it is unique. Resolution is a 3-step,
    // string-anchored scan instead (see Teleport::Install):
    //   1. find the ASCII bytes "FieldLevelNameTableInfo" in the module,
    //   2. find the `lea r8, [rip+..]` (4C 8D 05 ..) that references them
    //      (the lazy-load path passes the table name to the open function),
    //   3. scan BACK from that lea for the resolver's prologue below; the
    //      registry global is the RIP-relative `mov rbx` inside it.
    inline constexpr const char* kStr_LevelNameTable = "FieldLevelNameTableInfo";
    inline constexpr const char* kSig_LeaR8Rip       = "4C 8D 05 ?? ?? ?? ??";
    inline constexpr const char* kSig_TableResolverPrologue =
        "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 50 8B 39 48 8B 1D";
    // Within the prologue: +0x12 is `mov edi,[rcx]`, +0x14 the 7-byte
    // `mov rbx, cs:<registry global>` whose RIP operand we resolve.
    inline constexpr uintptr_t kOff_TableResolver_MovGlobal = 0x14;
    inline constexpr size_t    kLen_MovGlobalInstr          = 7;
    // How far back from the `lea r8` the prologue may sit (it is at +0x5D in
    // this build; give it slack for patch drift).
    inline constexpr size_t    kMax_LeaToPrologue           = 0x180;

    // FieldLevelNameTableInfo row (64 bytes): the LevelNameInfo container is
    // an engine hash map at +0x20..0x3F.
    inline constexpr uintptr_t kOff_LvlRow_BucketCount = 0x20; // u32
    inline constexpr uintptr_t kOff_LvlRow_Size        = 0x24; // u32 (entry count)
    inline constexpr uintptr_t kOff_LvlRow_Buckets     = 0x30; // ptr (0x100-byte buckets)
    inline constexpr uintptr_t kOff_LvlRow_Entries     = 0x38; // ptr (entry* array)
    // Bucket: [count u32 @0, pad, then (hash u32, entryIdx u32) pairs @ +8].
    inline constexpr uintptr_t kLvlBucket_Stride       = 0x100;
    inline constexpr uintptr_t kOff_LvlBucket_Pairs    = 0x08;
    // LevelNameInfo entry (offsets live-verified via hexdump):
    inline constexpr uintptr_t kOff_LvlEntry_Name      = 0x10; // engine string
    inline constexpr uintptr_t kOff_LvlEntry_IsSector  = 0x18; // bool (_isSectorLevel)
    inline constexpr uintptr_t kOff_LvlEntry_Box       = 0x1C; // f32 min xyz, max xyz

    // --- Map Marker Teleport (clean-room marker capture from crimsondesert-main) ---
    inline constexpr const char* kSig_MarkerPattern =
        "C5 FB 10 07 C5 FB 11 02 8B 47 08 89 42 08";
    inline constexpr size_t      kExpected_MarkerMatches = 5;

    inline constexpr const char* kSig_MarkerOriginPrefix =
        "C5 F8 5C 05";
    inline constexpr size_t      kExpected_OriginMatches = 9;

    inline constexpr const char* kSig_MarkerPlayer =
        "48 8B 06 C5 F8 11 88 B0 01 00 00";

    inline constexpr const char* kSig_MarkerProtection =
        "48 8B 46 08 48 89 F1";

    inline constexpr uintptr_t kOff_Player_Dest0 = 0x90;  // Vec3 x, y, z
    inline constexpr uintptr_t kOff_Player_Dest1 = 0x1A0; // Vec3 x, y, z
    inline constexpr float     kMarker_CoordLimit = 1.0e9f;
    inline constexpr float     kMarker_DestLift   = 10.0f;

    // --- Inventory: item enumeration + quantity editing ---------------------
    // The player's inventory needs no pointer chain from the player object:
    // GetItemQuantity (IDB sub_14A1330) is called by the HUD every time it shows
    // a count, so hooking it captures the live inventory CONTAINER (its 1st arg)
    // within a frame of loading - no transaction, no player walk. From the
    // container, GetInventoryHolder (IDB sub_1CDD520) returns the item holder.
    // Both have unique byte signatures. (Live-confirmed: a walk from here lists
    // every item, and writing a slot's quantity sticks - the game even re-stacks
    // it. Money is the exception: its inventory slot is a passive mirror, so
    // editing it does not change spendable currency.)
    inline constexpr const char* kSig_InvGetItemQty =
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B E8 0F B7 DA";
    inline constexpr const char* kSig_InvGetHolder =
        "40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41 30";

    // The engine's OWN slot-expansion setter (IDB sub_1CE8190) - what the game
    // itself runs when your expansion count changes:
    //     void* f(holder, int* outErr, void* unused, u16 bucketType, u16 count)
    // It finds the bucket the same way we do (bucket+0x10 == bucketType), then:
    //     bucket[0x16] = count            ; buff accumulator
    //     bucket[0x1A] = count            ; _varyExpandSlotCount (the real one)
    //     bucket[0x14] = row._defaultSlotCount + bucket[0x1A]
    // Note `count` is the EXPANSION, not the cap: the resulting cap is
    // default + count, so a target cap needs count = cap - _defaultSlotCount.
    // Its final write is NOT clamped to _maxSlotCount (the only _maxSlotCount
    // read gates a dead branch), so counts past the table max do take effect.
    // Preferred over writing kOff_InvBucket_MaxSlots directly, which only
    // pokes a cache the engine recomputes - see kOff_InvBucket_ExpandSlots.
    // 3rd arg is dead (forwarded to a resolver that ignores it): pass nullptr.
    //
    // HOOKED, not just called, because the engine re-stamps VANILLA values
    // through it behind our back (found 2026-07-15 chasing "inventory full"
    // beside a screen of empty slots). The server-side expansion sync (IDB
    // sub_256DD40, fired from event dispatchers e.g. sub_FCC82B0 on event
    // type 83) recomputes the character-inventory expansion from the unlock
    // items the player actually OWNS: it probes six item-type ids from
    // config, picks the highest owned tier, maps it to a count from config
    // globals, drives this setter on the server holder, then replicates the
    // value to the client realm as network message 2137 (sub_243DDF0 packs
    // it; client handler sub_9B7330 -> sub_80ABC0 -> this setter again, after
    // mapping the wire id to a local InventoryType via qword_6181418). So a
    // poked expansion survives only until the next inventory event, in BOTH
    // realms - and a pickup planned inside that window fails the insert
    // planner's cap check while the on-screen grid still shows the raised
    // cap. Substituting the count inside the hook makes the engine's own
    // re-stamps apply the override, which closes the window for good.
    inline constexpr const char* kSig_InvSetExpandSlots =
        "48 89 5C 24 ? 56 48 83 EC 20 48 8B 41 ? 48 8B F2 8B 49";

    // The FREE-SPACE GATE (IDB sub_1CE8F40) - the check that actually throws
    // "inventory full" on a world pickup, BEFORE the insert planner runs:
    // the server-side give-items transaction (sub_2566C90) calls
    //     free = f(holder, keyPtr, bucketType, &itemTypeId, subType)
    // and refuses with eErrNoInventorySlotNotExist when free <= 0. What it
    // computes (decompiled 2026-07-15):
    //   - bucket = holder bucket with +0x10 == bucketType (0 if none -> full!)
    //   - non-stackable item: free = (i16)cap(+0x14) - (i16)used(+0x12)
    //   - stackable item:     free = (stackMax - owned%stackMax) % stackMax
    //                              + stackMax * max(0, cap - used)
    // Consequences worth remembering: for a stackable item you own a PARTIAL
    // stack of, the first term alone passes the gate even when cap<=used -
    // while an item you own none of fails it. That asymmetry is what
    // "full inventory on SOME pickups" looks like from the outside. Both
    // reads are SIGNED 16-bit, so a cap past 0x7FFF goes negative and fails
    // everything (the menu's 9999 limit keeps us clear of that). This is
    // what confirmed kOff_InvBucket_UsedSlots (below) as the field the mod's
    // quantity editor was leaving stale - see its comment for the fix.

    // Per-holder insert planner (IDB sub_1F850C0). Its 3rd arg (r8) is the
    // inventory CONTAINER; it fires for BOTH the client mirror container AND the
    // server-authority container on every add/reconcile. We hook it purely to
    // CAPTURE the server container (the one whose holder != the client walk
    // holder) - the durable global walk only reaches the client, and there is
    // no client->server pointer link (live-confirmed: midscan + linkscan both
    // empty). Editing a quantity in the client holder alone reverts because a
    // per-frame server reconcile overwrites it; writing the SAME slot in BOTH
    // holders makes the edit real, usable, and non-reverting (live-proven).
    // 1.17: frame grew from 0x2F0 to 0x310; the surrounding ABI saves stayed stable.
    // Unique byte signature in the 1.17 scan.
    inline constexpr const char* kSig_InvHolderInsert =
        "48 89 5C 24 ? 4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 "
        "41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 10 03 00 00";

    // Inventory transaction COMMIT (IDB sub_1CE1E70), called by the transaction
    // orchestrator sub_1CC15C0 as `commit(holder, &err, CONTAINER, ...)` - its
    // 3rd arg (r8) is the container. This is the capture point for the
    // SERVER-AUTHORITY container, and the reason is worth spelling out:
    //
    // Loading a save is itself a big inventory transaction, and it drives commit
    // for EVERY realm's container - the server ones FIRST, before the client
    // container object even exists. Live-proven (attach at the title screen,
    // load a save, touch nothing):
    //     0x..0f0200  (server arena)   <- 1st
    //     0x..0f0500  (server arena)   <- 2nd  } both getHolder() to the SAME holder
    //     0x..bf48ac0 (client)         <- 3rd, LAST
    // So the server holder is available seconds after load with no player action.
    // holderInsert (above) does NOT fire at load - it only fires on a real
    // add/drop/buy, which is why edits used to need a "calibration" pickup.
    //
    // Do NOT gate the capture on resolving the client container first: at the
    // moment the server containers go by, that resolve is guaranteed to fail
    // (its global/mid/container chain is not built yet) and the capture is lost.
    // Record every distinct container here, decide which is the server one later.
    //
    // There is NO durable pointer chain to the server container - exhaustively
    // ruled out (2026-07-15): not <=2 hops from either world root
    // (qword_6180C28 / qword_6180C78, which both lead only to the CLIENT actor),
    // no module-global points at it, and the handle-keyed actor registry
    // (sub_2D889E0, world+0x110) does not contain the player at all. It always
    // lands at arena+0xF0200 in a 16MB-aligned server arena, but nothing
    // reachable points at that arena. Capture-at-load is the route; this is it.
    // Unique byte signature.
    inline constexpr const char* kSig_InvCommit =
        "4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 55 41 56 "
        "41 57 48 8D 6C 24 ? 48 81 EC 48 01 00 00 4D 8B D0 48 8B D1";

    // Fallback container resolution (the hook above only captures the
    // container when the game happens to query an item count, which is NOT
    // guaranteed at load - live sessions showed it hit-or-miss). The durable
    // path starts at the core global singleton (IDB qword_6180C28):
    //   global -> +0x30 -> +0x50 = inventory container
    //   container -> +0x68 -> +0xB8 = item holder
    // (That holder walk is GetInventoryHolder's own main path - its decompile
    // reads [[container+0x68]+0xB8] after a container-type check; instead of
    // replicating the type check we validate the RESULT structurally, i.e.
    // the holder must expose a sane bucket array. Live-confirmed: this walk
    // resolves the same container the hook captures, from load, with no
    // transaction.)
    // The global is anchored by the travel-manager wrapper (IDB sub_5019D0),
    // whose body is `mov rax, cs:<global>; mov rdx,[rax+30h]; mov rdx,[rdx+50h]`
    // - the exact chain we walk. Unique match; the mov's RIP operand is at
    // match+0x15 (7-byte instruction).
    inline constexpr const char* kSig_InvCoreGlobal =
        "48 89 54 24 ? 53 48 83 EC 30 48 8B DA C7 44 24 20 00 00 00 00 "
        "48 8B 05 ? ? ? ? 48 8B 50 30 48 8B 52 50 48 8B CB E8";
    inline constexpr uintptr_t kOff_InvCoreGlobal_Mov = 0x15; // mov rax, cs:<global>
    inline constexpr uintptr_t kOff_Global_Mid        = 0x30; // global+0x30 -> mid
    inline constexpr uintptr_t kOff_Mid_Container     = 0x50; // mid+0x50 -> container
    inline constexpr uintptr_t kOff_Container_Sub     = 0x68; // container+0x68 -> sub-object
    inline constexpr uintptr_t kOff_Sub_Holder        = 0xB8; // sub+0xB8 = item holder

    // holder -> buckets -> 192-byte item slots.
    //
    // A bucket is not a "type group" - it IS one of the game's storages, and
    // bucket+0x10 says which (see kStr_InventoryInfoTable below). That is why
    // one walk of this holder yields your pack, your Private Storage, your
    // Wardrobe and the Bank all at once, and why a stack in the Bank and one in
    // your pack used to show up as two identical, unexplained rows.
    //
    // Proven by the engine's own bucket lookup (IDB sub_1CE1020), which routes
    // an item to a bucket by matching that field against the item's default
    // storage:
    //     v9 = *(u16*)(itemDef + 66);          // ItemInfo._defaultPushInventoryInfo
    //     for (bucket : holder+0x18 .. +0x20)  // same array we walk
    //         if (*(u16*)(bucket + 0x10) == v9) break;
    // (It can be overridden per item via sub_1CEB790 - which is how the same
    // item can live in the Bank as well as in your pack.)
    inline constexpr uintptr_t kOff_InvHolder_Buckets = 0x18; // ptr[]
    inline constexpr uintptr_t kOff_InvHolder_Count   = 0x20; // u32 bucket count
    inline constexpr uintptr_t kOff_InvBucket_Slots   = 0x00; // ptr[] (slot array)
    // The slot array is a vector: data at +0x00, SIZE at +0x08, capacity at
    // +0x0C. Proven by the push path (IDB sub_ED65670), which grows it on
    // demand - `if (size <= idx) { if (cap < idx) realloc; resize(idx); }`.
    // Size is large headroom (~1460), NOT the storage's slot cap: the cap is
    // kOff_InvBucket_MaxSlots below. It still matters, because the insert
    // planner scans min(cap, size) - see kOff_InvBucket_ExpandSlots.
    inline constexpr uintptr_t kOff_InvBucket_Count   = 0x08; // u16 slot-array SIZE
    inline constexpr uintptr_t kOff_InvBucket_Type    = 0x10; // u16 InventoryType (which storage)
    // The LIVE effective slot cap for this bucket/storage - found 2026-07-15
    // via the RTTI-vtable route (type descriptor ".?AVCommonVaryMaxExpand-
    // InventorySlotBuffProcessor@pa@@" -> COL -> vtable -> the buff-apply
    // slot), not a memory hunt: decompiling that class's apply handler
    // (sub_1C6AB50) shows it resolves the holder (sub_1CDD520, our own
    // kSig_InvGetHolder) then calls a chain that bottoms out in
    // sub_1F85E70(bucket, out, deltaI16) - which finds the bucket the SAME
    // way we do (matches bucket+0x10 == bucketType), clamps against the
    // InventoryInfo row's own _defaultSlotCount/_maxSlotCount (its own
    // +0x48/+0x4A reads - same offsets as kOff_InvDef_DefSlots/MaxSlots
    // above), and writes the result here. This is the number an "is this
    // storage full" check would read - NOT the array capacity at bucket+0x08
    // (that is fixed headroom, ~1460, unrelated to what the UI shows/enforces).
    inline constexpr uintptr_t kOff_InvBucket_MaxSlots = 0x14; // u16, LIVE cap - write target
    // Two accompanying accumulators the same function also updates (raw and
    // clamped running totals of every delta ever applied to this bucket) -
    // not needed for an absolute set, kept here for completeness/future use.
    inline constexpr uintptr_t kOff_InvBucket_DeltaRaw    = 0x16; // u16
    inline constexpr uintptr_t kOff_InvBucket_DeltaClamped = 0x18; // u16

    // How many slots of this storage are IN USE. Both bucket constructors
    // (IDB sub_1CE84A0 / sub_DEB14F0) zero it, and the free-space getter
    // (sub_1CE8F40) is literally `bucket+0x14 - bucket+0x12`; the insert
    // planner's full-check (sub_1F850C0) is
    //     needed + bucket[0x12] > min(bucket[0x14], slot-array size)
    // which is why the array size above still matters.
    //
    // It is an INCREMENTAL accumulator, never recomputed from a scan: the
    // push path does `used += ceil(newQty/stackMax) - ceil(oldQty/stackMax)`.
    // Two consequences worth knowing: picking up an item that stacks onto an
    // existing stack without crossing a slot boundary legitimately moves this
    // by ZERO (not a bug), and writing a slot's quantity directly - as the
    // quantity editor does - bypasses the only code that maintains this, so
    // it goes stale.
    //
    // And the stale case is not cosmetic (LIVE-CAUGHT 2026-07-15): loading a
    // save rebuilds every bucket by pushing each saved stack through that
    // ceil math, so one editor-made stack of 999999 against a vanilla
    // stackMax of 50 books 20000 "used" slots on the spot - a real bucket
    // read used=27445 with cap=2000, at which point the insert planner AND
    // the pickup free-space gate (kSig_InvFreeSpace) refuse everything:
    // "inventory full" beside a screen of empty slots, locked slots in the
    // grouped UI. Inventory::Tick's RepairUsedSlots heals it by clamping
    // this DOWN to physical occupancy (1 per occupied slot - identical to
    // the engine's own accounting in any state the engine produced itself,
    // since it splits stacks at stackMax). Clamp down only, never up, and
    // let the engine keep applying its own deltas on top.
    inline constexpr uintptr_t kOff_InvBucket_UsedSlots = 0x12; // u16, used count
    // The storage's EXPANSION count - the "extra slots you own" beyond the
    // InventoryInfo row's _defaultSlotCount, and the value that actually
    // drives the cap. kOff_InvBucket_MaxSlots is a DERIVED cache of it:
    //     bucket[0x14] = row._defaultSlotCount + bucket[0x1A]
    // recomputed by the engine's own setter (IDB sub_1CE8190, our
    // kSig_InvSetExpandSlots) - and separately as
    //     bucket[0x14] = clamp(row._defaultSlotCount + bucket[0x16],
    //                          row._maxSlotCount)
    // by the slot-expansion buff path (sub_1F85E70). So writing 0x14 alone
    // is poking a cache: any expansion sync or buff apply/expire recomputes
    // it from 0x1A/0x16 and reverts us. Set this instead (or better, call
    // the setter, which maintains 0x16 + 0x1A + 0x14 together).
    //
    // The game's own save/replication schema names it, which is how it was
    // identified - the deserialiser strings spell the record out:
    //     InventoryElementSaveData { _inventoryKey    : InventoryType
    //                                _varyExpandSlotCount : TInventorySlotNo
    //                                _itemList }
    // (Same technique as the ItemInfo/ItemGroupInfo field maps: the table
    // deserialisers name each C++ member.) Nothing else in the binary reads
    // or writes 0x1A - sub_1CE8190 is its sole accessor.
    inline constexpr uintptr_t kOff_InvBucket_ExpandSlots = 0x1A; // u16, _varyExpandSlotCount

    inline constexpr uintptr_t kInvSlot_Stride        = 0xC8; // 200-byte slots in TU 1.16+
    inline constexpr uintptr_t kOff_InvSlot_TypeId    = 0x08; // u16 item type id
    inline constexpr uintptr_t kOff_InvSlot_Quantity  = 0x10; // i64 quantity (edit here)
    inline constexpr uint16_t  kInvSlot_EmptyType     = 0xFFFF;

    // --- Creating an item from nothing (the add-item path) --------------------
    // A slot IS a TrItemValue (same 0xC0 stride), and the game's own recipe for
    // making one lives in the server reconcile (IDB sub_25568A0, 0x2556FA0..
    // 0x255717B) - it creates a stack from nothing using these primitives. We
    // replay it verbatim; every step below was live-validated 2026-07-15 (a real,
    // usable, persistent item that survives save/reload):
    //
    //   container = *(holder+8)                       // kOff_InvHolder_Container
    //   tag       = *(*(container+0x88)+1)            // object-type tag
    //   owner     = (tag & 0xF7) ? *(container+0xA0) : container
    //   alloc     = *(*(owner+0x68)+0x10)             // instance-id allocator
    //   ctor(itemVal, &typeId, qty)                   // kSig_TrItemValueCtor
    //   itemVal+0x0A = 0
    //   itemVal+0x00 = InterlockedIncrement64(alloc+0x20)   // THE unique id
    //   bucket = the holder bucket whose +0x10 == itemDef+66
    //   plan(bucket, &err, container, {itemVal,1,1}, 0, &out, 0, 0, 1)
    //   for each 216-byte placement p in out:
    //       commit(holder, &err, 0, p, *(u16*)(p+208))
    //   freePlacements(&out); dtor(itemVal)
    //
    // WHY EACH PIECE MATTERS (each was a separate failed attempt):
    //  * The PLAN alone mutates nothing - it deep-copies the bucket and emits
    //    placement records. Calling it and seeing err=0 means nothing. The
    //    COMMIT is what writes, via sub_ED65670 (the only function in the whole
    //    chain that touches the live slot array).
    //  * The unique instance id is not optional. An earlier design memcpy'd a
    //    template slot and stamped typeId+qty; the item had no id and it BRICKED
    //    A SAVE. Never fabricate a slot - always go through ctor + allocator.
    //  * The REALM must match the target holder (see kTls_RealmFlag): the ctor
    //    reaches sub_ED69660, which reads the client-only storage base
    //    qword_6180EB0 and forces it to 0 in the server realm, so a client-realm
    //    item is subtly different from a server-realm one.
    //  * BOTH holders must be written, each in its own realm, sharing ONE
    //    allocator id - the same rule the quantity editor already follows. The
    //    server->client reconcile syncs quantities but does NOT create slots, so
    //    a server-only add is invisible until a save/reload.
    //
    // Buffer note: the ctor does NOT write every byte of the 192 (+0x0C, +0x3A,
    // +0x54, +0x8A.. are left untouched). The game gets away with an
    // uninitialised stack buffer because it copy-constructs before the value
    // goes anywhere; we hand ours straight to the planner, so ZERO IT FIRST or
    // the holes reach the live slot (live-seen: garbage at +0x0C).

    // TrItemValue ctor (IDB sub_1F86FD0): void f(itemVal, u16* typeId, i64 qty).
    // Self-contained - fills subtype/durability/flags/sub-lists from the item
    // def alone. Leaves the instance id as -1 for the caller to stamp.
    inline constexpr const char* kSig_TrItemValueCtor =
        "48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC "
        "48 83 EC 60 4C 8B EA 48 8B F1 48 C7 01 FF FF FF FF 0F B7 02 66 89 41 08";
    // Per-placement COMMIT (IDB sub_1CE1020):
    //     void* f(holder, int* outErr, void* unused, void* placement, u16 slotIdx)
    // Re-finds the bucket from the item's own def (+66) and calls sub_ED65670,
    // which validates the item may live in that storage, copies it into an empty
    // slot (or merges onto an existing stack) and maintains the used-slot count.
    // 3rd arg is a genuine don't-care: it only supplies the high bits of a
    // scratch whose low word is immediately overwritten with the typeId.
    inline constexpr const char* kSig_InvCommitPlacement =
        "48 89 5C 24 ? 4C 89 44 24 ? 55 56 57 48 83 EC 30 41 0F B7 59";
    // Free the planner's placement vector. The pre-1.17 build reached the
    // cleanup target through a 5-byte jmp thunk; 1.17 recompiles the target but
    // keeps the same vector ABI: [vec+0] data, [vec+8] count, [vec+10h] inline
    // storage sentinel, with 0xD8-byte placement records. The 1.17 target first
    // destroys [data, data + count*0xD8), then releases the backing allocation.
    // Re-derived against CrimsonDesert.exe SHA256
    // A1DFC0329E177240A978EE4CC3D331E5DDD1903D1055787816199C559E16857C.
    // The signature was unique in that build; Inventory::Install also re-checks
    // uniqueness before enabling Add Item.
    inline constexpr const char* kSig_InvFreePlacements =
        "48 89 4C 24 08 53 48 83 EC 20 48 8B D9 48 8B 09 8B 43 08 "
        "48 69 D0 D8 00 00 00 48 03 D1 E8 ? ? ? ? 90 48 8B 0B "
        "48 8D 43 10 48 3B C8";
    // TrItemValue dtor (IDB sub_ED6DF40, via thunk sub_1F88270). Destroys the
    // sub-objects the ctor allocated; does NOT free the buffer itself.
    inline constexpr const char* kSig_TrItemValueDtor = "";

    inline constexpr uintptr_t kOff_InvHolder_Container = 0x08; // holder+8 -> container
    // ItemInfo._defaultPushInventoryInfo - which storage this item goes to by
    // default. The commit re-reads it, but we need it to pick the bucket too.
    // Working 0.13.2 ASI FUN_180012840 reads the holder bucket type from
    // itemDef+0x418 before selecting the matching client/server bucket.
    inline constexpr uintptr_t kOff_ItemDef_BucketType  = 0x418; // u16, 1.17
    //
    // ★ The inventory CONTAINER *is* the player CHARACTER object - the very same
    // "owner" the player/god-mode code resolves. Live-confirmed 2026-07-15: the
    // container's type tag (kOff_Owner_TypeDesc -> +1) reads 1 = SelfPlayer, and
    // its possessor round-trips exactly as kOff_Owner_Possessor/
    // kOff_Possessor_Pawn describe. So the inventory and player systems are the
    // same object graph, and those constants are reused here rather than
    // redefined - see their comment above for why the round-trip is the true
    // single source of truth.
    //
    // What is new: each REALM has its OWN player character (client and server
    // hold different characters, with different possessors), and BOTH round-trip
    // - each is the live character of its own realm. That is precisely what the
    // add path needs, since it must find the live store on each side.
    //
    // It also means the round-trip is the only way to tell the live store from
    // the insert planner's short-lived DEEP COPIES of it: a copy carries the
    // original's possessor pointer, and a possessor can only point back at one
    // character. Copies mirror the player's contents ~99% and match on bucket
    // count and type tag, so nothing else rejects them - and they are freed,
    // so mistaking one for the real store means faulting on a dead holder.
    inline constexpr uintptr_t kOff_Sub_IdAllocator = 0x10; // (owner+0x68)+0x10 -> id allocator
    inline constexpr uintptr_t kOff_IdAlloc_Counter = 0x20; // i64, InterlockedIncrement64 target

    // Working 0.13.2 ASI FUN_180012540 clears 0x108 bytes before invoking the
    // 1.17 TrItemValue constructor. This working object is larger than the
    // live inventory-slot stride and must not be truncated to 0xC0.
    inline constexpr uintptr_t kItemVal_Size        = 0x108;
    inline constexpr uintptr_t kOff_ItemVal_InstanceId = 0x00; // i64 (-1 out of the ctor)
    inline constexpr uintptr_t kOff_ItemVal_Subtype    = 0x0A; // u16 (reconcile zeroes it)
    // Crimson Desert 1.17 working transaction ABI, recovered from the
    // known-good Trinity 0.13.2 ASI: records are 0xE0 bytes and the slot
    // index consumed by CommitPlacement is the u16 at +0xD8.
    inline constexpr uintptr_t kPlacement_Stride       = 0xE0;
    inline constexpr uintptr_t kOff_Placement_SlotIdx  = 0xD8; // u16

    // --- The client/server realm flag ----------------------------------------
    // The engine runs two realms in one process and selects between them with a
    // per-thread flag, inlined everywhere as:
    //     root = qword_6180F60; if (*(u8*)(TLS+498)) root = qword_6180F68;
    // where TLS = *(NtCurrentTeb()->ThreadLocalStoragePointer).
    // Item construction is realm-sensitive through it (see the add-item note
    // above), so building an item for the server holder means flipping this for
    // the duration and restoring it afterwards - leaving a game thread in the
    // wrong realm would corrupt whatever it touches next.
    //
    // Get the TEB via NtQueryInformationThread(ThreadBasicInformation): a plain
    // exported ntdll call. Do NOT hand-roll a `mov rax, gs:[30h]` stub - that
    // was tried and fails (bogus TEB, then an access violation on the second
    // call, almost certainly CFG rejecting an indirect call into our own page).
    inline constexpr uintptr_t kOff_Teb_TlsPointer = 0x58; // TEB.ThreadLocalStoragePointer
    inline constexpr uintptr_t kTls_RealmFlag      = 498;  // u8: 0 = client, 1 = server

    // Item-info table (typeId -> item definition -> item key string, for names).
    // Its resolver is one of ~121 identical 16-bit-key table-resolver clones, so
    // - like the fast-travel/area-name tables above - it is located by string-
    // anchoring on its unique table name "iteminfo": find the string, find the
    // `lea r8,[rip+str]` that passes it, scan back to the clone prologue, and
    // read its RIP-relative `mov rbx, cs:<table global>`. The clone prologue
    // here differs from kSig_TableResolverPrologue only in loading a 16-bit key
    // (0F B7 39) instead of 32-bit (8B 39); the mov-global sits at +0x15.
    //   table +0x08 u32  count (typeId bound)
    //   table +0x58 ptr  def[]      (def = *(table+0x58 + 8*typeId))
    //   def   +0x08 ptr -> string object whose first qword is the key char*
    inline constexpr const char* kStr_ItemInfoTable = "iteminfo";
    inline constexpr uintptr_t kOff_ItemResolver_MovGlobal = 0x15;
    inline constexpr uintptr_t kOff_ItemTable_Count = 0x08; // u32
    // Working 0.13.2 ASI FUN_180014390 reads this pointer at +0x58 in 1.17.
    inline constexpr uintptr_t kOff_ItemTable_Defs  = 0x58; // ptr[]
    inline constexpr uintptr_t kOff_ItemDef_Key     = 0x08; // ptr -> string obj

    // --- Storages: what each bucket IS ---------------------------------------
    // "InventoryInfo" is the table describing every storage in the game - one
    // row per InventoryType, and bucket+0x10 (above) is that row's _key. Its
    // rows are what let the menu name a storage without hardcoding anything.
    // The 20 storages, by engine key (the game registers exactly these):
    //     Money, Character (your pack), PearlUser, PearlCharacter, Quest,
    //     PetAndVehicle, Wagon, CampWareHouse, WareHouse, Bank, CampStraw,
    //     BirdFeed, Recovery, Housing_Dresser, Housing_Refrigerator,
    //     Housing_Symbol, Housing_Collecting, Housing_GatheredMaterials, Kuku,
    //     InvisibleInventory
    // Which key maps to which NUMBER is deliberately not recorded here: the
    // order those statics are declared in is NOT the enum value (live rows come
    // back in an order that contradicts it), and nothing needs to know - we read
    // the number off the bucket and let the table say what it is. Do not
    // reintroduce a hardcoded ordinal list; it would be guesswork.
    //
    // It is another 16-bit-key resolver clone (IDB sub_516050, 176-byte rows,
    // row loader sub_517460, deserializer sub_1171F30) and shares the table
    // layout above (+0x08 count, +0x50 def[]), so DefForRow walks it unchanged.
    //
    // The ONE difference that matters: this clone loads its table-name string
    // INDIRECTLY - `mov r8, cs:<slot>` where the slot holds a char* to
    // "Inventory" - rather than `lea r8, "iteminfo"`, because that name is a
    // shared/interned string. Same clone shape otherwise (the name load sits at
    // fn+0x4E either way), so the only change needed was to let the table hunt
    // follow the extra indirection. Verified unique: exactly one site in the
    // image both loads this name pointer AND has the clone prologue above it;
    // the only other site is the table loader, which the prologue check already
    // rejects - the same discriminator "iteminfo" relies on.
    inline constexpr const char* kStr_InventoryInfoTable = "Inventory";
    inline constexpr const char* kSig_MovR8Rip = "4C 8B 05 ?? ?? ?? ??";
    //
    // InventoryInfo fields we consume (field->offset recovered from the
    // deserializer's own per-field error strings, exactly as for ItemInfo):
    inline constexpr uintptr_t kOff_InvDef_Key      = 0x08; // _stringKey -> "WareHouse"
    // _InventoryNameUIText is the game's own label, but it is NOT unique: several
    // rows share "Inventory", because in the game's own screens the surrounding
    // UI says which one you are looking at (the warehouse has both a "focus
    // Inventory" and a "focus WareHouse" pane). A flat list has to qualify the
    // repeats itself - see the storage naming in inventory.cpp.
    inline constexpr uintptr_t kOff_InvDef_Name     = 0x70; // _InventoryNameUIText (loc-string)
    inline constexpr uintptr_t kOff_InvDef_DefSlots = 0x48; // u16 _defaultSlotCount
    inline constexpr uintptr_t kOff_InvDef_MaxSlots = 0x4A; // u16 _maxSlotCount
    // _InventoryNameUIText is built by the SAME builder as ItemInfo._itemName
    // (IDB sub_FF6460), so the localised-name walk below reads it verbatim.

    // --- Item taxonomy: the inventory's REAL category tree -------------------
    // Every "*info" table row is built by a GENERATED deserializer whose
    // per-field failure message names the C++ member it was reading, e.g.
    //   "ItemGroupInfo의 _groupName를 읽어들이는데 실패했다."
    //     ( = "failed to read ItemGroupInfo's _groupName" )
    // Walking that deserializer's disassembly and pairing each field read with
    // the error string in its failure block recovers the field->offset map
    // EXACTLY - no guessing. (IDB: ItemInfo = sub_1177130, 1016-byte row;
    // ItemGroupInfo = sub_11782F0, 120-byte row.) This is a general technique
    // for this binary; reuse it for any table.
    //
    // ItemInfo fields we consume (offsets into the def resolved above):
    inline constexpr uintptr_t kOff_ItemDef_Name   = 0x20;  // _itemName (loc-string struct)
    inline constexpr uintptr_t kOff_ItemDef_Groups = 0x350; // _itemGroupInfoList (vector)
    inline constexpr uintptr_t kOff_ItemDef_Tier   = 0x210; // u8 _itemTier (rarity 0..5)
    // _maxStackCount / _applyMaxStackCap: the per-item stack cap. Most rows
    // already carry 999999 (an effectively-unlimited default) - the cap that
    // actually bites for outliers (equipment, key items, some consumables) is
    // whatever _applyMaxStackCap gates. Inventory::SetAllMaxStackSizes forces
    // both to one uniform value/enabled across every row in one pass.
    inline constexpr uintptr_t kOff_ItemDef_MaxStackCount    = 0x18;  // i64
    inline constexpr uintptr_t kOff_ItemDef_ApplyMaxStackCap = 0x111; // u8 bool
    //
    // "ItemGroupInfo" (note the capitals - it is NOT lowercase like "iteminfo")
    // is the inventory's category tree, and it is what the UI actually shows.
    // Its resolver is another 16-bit-key clone, byte-identical in prologue to
    // the iteminfo one, so the same string-anchor finds it and the table layout
    // is shared (+0x08 count, +0x50 def[]). ItemInfo._itemGroupInfoList holds
    // ROW INDICES into it - that is the item -> category link.
    //   grpDef +0x08 ptr _stringKey ("ItemGroup_SubCategory_Equip_Weapon_Range")
    //   grpDef +0x18     _groupName  <- LOCALISED display text ("Ranged Weapon")
    //   grpDef +0x38 vec _itemGroupInfoList (child groups)
    //   grpDef +0x48 vec _itemInfoList
    //   grpDef +0x68 u16 _orderIndex
    //   grpDef +0x6C u16 _iconPath
    //   grpDef +0x6E u8  _isShowCategoryString
    //
    // _orderIndex alone encodes the whole hierarchy (live-verified across a
    // real inventory - every item resolves to exactly this shape):
    //     1..5    the top tabs      (Equipment, Food, Materials, Documents, Others)
    //     1300..8900 on a decade base = the sub-category the game displays
    //                (1500 "Two-Handed Weapon", 1600 "Ranged Weapon",
    //                 5100 "Elixir", 6600 "Crafting and Refinement Material",
    //                 7000 "Currency", 7800 "Projectile", 8300 "Enhanced Kuku Pot")
    //     base+n  leaf groups, which always sort INSIDE their own sub-category
    //             (1509 "Two-Handed Sword" < 1600 "Ranged Weapon")
    //     65535   internal semantic groups ("Material_Alchemy_HP_Main_Tier2")
    //             that must never be displayed - the sentinel is what tells
    //             display rows apart from bookkeeping rows.
    // So: drop 65535, take <=5 as the tab (OPTIONAL - e.g. Lubricant/Cogwheel
    // have no tab, only Currency), and the smallest remaining order is the
    // category. Nothing about this is hardcoded, and it tracks game updates.
    inline constexpr const char* kStr_ItemGroupInfoTable = "ItemGroupInfo";
    inline constexpr uintptr_t kOff_GrpDef_Name    = 0x18; // loc-string struct
    inline constexpr uintptr_t kOff_GrpDef_Order   = 0x68; // u16 _orderIndex
    inline constexpr uint16_t  kGrpOrder_Internal  = 0xFFFF; // never displayed
    inline constexpr uint16_t  kGrpOrder_MaxTopTab = 5;      // 1..5 = top tabs
    // Engine vector shape shared by the *List fields above.
    inline constexpr uintptr_t kOff_Vec_Data  = 0x00; // T*
    inline constexpr uintptr_t kOff_Vec_Count = 0x08; // u32

    // --- Icons: the game's own sprite names, for items AND categories --------
    // ItemInfo._itemIconList (+0x90) is the same vector shape, over 32-byte
    // ItemIconData rows (deserializer sub_1196CB0, fields named by its error
    // strings exactly as above):
    //     +0x00 u16 _iconPath      +0x02 u16 _highlightIconPath
    //     +0x04 u8  _checkExistSealedData
    //     +0x08 vec _gimmickStateList      +0x18 u8 _checkUsable
    // _iconPath is NOT text - it is a u16 ROW INDEX into the `stringinfo`
    // table. (Its field reader, sub_1187F20, maps the on-disk u32 key to a row
    // index through the table's key map at +0x60; the u16 it leaves behind is
    // that row.) ItemGroupInfo._iconPath (+0x6C) goes through the SAME reader,
    // so a category's icon is the identical walk - which is why one code path
    // serves both.
    //
    // stringinfo is another 16-bit-key resolver clone (IDB sub_304590, row
    // loader sub_3069F0), byte-identical in prologue, so the shared table
    // layout (+0x08 count, +0x50 def[]) and the same string-anchor find it:
    //     +0x00 u32 _key    +0x08 _stringKey (EMPTY on the icon rows)
    //     +0x10 u8  _isBlocked
    //     +0x18     _buffer   <- the payload: the icon sprite name
    // The sprite name lowercased + ".dds" is the icon's file in pak chunk 0012
    // under ui/texture/icon. Live-verified: item 6476 "Righteous Verdict" ->
    // "ItemIcon_Prefab_cd_phm_02_sword_0039" -> itemicon_prefab_cd_phm_02_sword_0039.dds,
    // and category "Two-Handed Weapon" -> "ItemIcon_ItemGroup_twohand_weapon"
    // -> itemicon_itemgroup_twohand_weapon.dds (71 such category icons ship).
    // Note the strings' casing is inconsistent in the data ("itemIcon_...",
    // "ItemIcon_...") - lowercase before using one as a file name.
    //
    // The resolver has a lazy row-load path, but every row an item points at is
    // already populated at load (live-checked: 38 of 38 items, zero nulls), so
    // this stays a pure pointer walk and never calls into the game.
    inline constexpr const char* kStr_StringInfoTable = "stringinfo";
    inline constexpr uintptr_t kOff_StrDef_Buffer  = 0x18; // ptr -> string obj
    inline constexpr uintptr_t kOff_ItemDef_Icons  = 0x90; // _itemIconList (vector)
    inline constexpr uintptr_t kOff_IconData_Path  = 0x00; // u16 -> stringinfo row
    inline constexpr uintptr_t kOff_GrpDef_Icon    = 0x6C; // u16 -> stringinfo row
    inline constexpr uint16_t  kIconPath_None      = 0xFFFF;

    // --- Category icons the game ships but never names ------------------------
    // A handful of displayed categories have NO usable _iconPath: the sprite
    // name they would need is simply absent from `stringinfo`, so no row can
    // point at it. "Packaged Trade Goods" is one - stringinfo carries
    // "ItemIcon_ItemGroup_trade" but nothing for trade_packed - yet
    // itemicon_itemgroup_trade_packed.dds IS in pak chunk 0012 all the same.
    // The art exists; only the data link is missing.
    //
    // We load icons from the pak BY FILE NAME and never touch stringinfo, so we
    // can name the file ourselves. The group's _stringKey gives it away, because
    // the two follow one convention:
    //     ItemGroup_SubCategory_trade_Packed -> ItemIcon_ItemGroup_trade_Packed
    //                                        -> itemicon_itemgroup_trade_packed.dds
    // Checked against the shipped data: of the 50 SubCategory rows, 43 derive to
    // a .dds that exists. That is a rule, not a coincidence - and a derived name
    // that happens to miss just draws blank, exactly as the row does today.
    inline constexpr uintptr_t   kOff_GrpDef_Key      = 0x08; // ptr -> string obj
    inline constexpr const char* kGrpKey_SubCatPrefix = "ItemGroup_SubCategory_";
    inline constexpr const char* kIconPrefix_ItemGroup = "ItemIcon_ItemGroup_";
    // Last resort, for a category we can name no icon for at all - chiefly our
    // synthetic "Uncategorised" bucket, which is not a game row and so has no
    // key to derive from. This is the game's own "unknown category" art, and it
    // ships. It pairs with the "Uncategorised" label fallback: a row that falls
    // back on one falls back on the other.
    inline constexpr const char* kIcon_Uncategorised  = "ItemIcon_ItemGroup_special_unknown";

    // --- Localised display names (real in-game text) -------------------------
    // ItemInfo._itemName is a 32-byte localised-string struct; its first qword
    // is a "provider" object whose vtable slot 3 is the text getter. That getter
    // (IDB sub_FF6430) is only a bounds-checked pointer walk, so we replicate it
    // as guarded reads instead of calling into the game from the render thread:
    //     off  = *(u32*)(provider + 0x10);          // 0xFFFFFFFF until interned
    //     blob = *(void**)(locMgr + 0x08);          // +0x00 data, +0x08 u32 size
    //     name = off < size ? *(char**)blob + off : "";
    // The bounds check is what makes this safe: an unresolved (-1) offset can
    // only ever yield "", never a wild pointer. The blob is a ~9.8MB interned
    // char pool holding the CURRENT language's text, which is why this beats a
    // baked-in name table (the community one this replaced had gone stale - it
    // mapped Money_Copper to "Silver"; the game says "Copper").
    // Anchored on the getter itself: 0x22 bytes, unique, and its
    // `mov rax, cs:<locMgr>` sits at +0x03 (7-byte instruction).
    // 1.17.00 note: this getter was recompiled and the old AOB does not resolve.
    // Localisation is optional; inventory falls back to prettified engine keys.
    inline constexpr const char* kSig_LocStringGet =
        "8B 51 10 48 8B 05 ? ? ? ? 48 8B 48 08 3B 51 08 72 08 "
        "48 8D 05 ? ? ? ? C3 48 8B C2 48 03 01 C3";
    inline constexpr uintptr_t kOff_LocGet_MovGlobal = 0x03; // mov rax, cs:<locMgr>
    inline constexpr uintptr_t kOff_LocProv_Offset   = 0x10; // u32 offset into blob
    inline constexpr uintptr_t kOff_LocMgr_Blob      = 0x08; // ptr -> blob
    inline constexpr uintptr_t kOff_LocBlob_Data     = 0x00; // char*
    inline constexpr uintptr_t kOff_LocBlob_Size     = 0x08; // u32 used bytes

    // --- World: Game Speed (fixed-timestep override) ------------------------
    // The engine's per-frame timing update (IDB sub_8FBD80) measures the real
    // frame delta, applies UI/pause/native-timescale factors, and stores the
    // master delta the whole simulation (animation, physics, AI, ability
    // timers) advances by. Its tail carries a FIXED-TIMESTEP override, used by
    // the game's own video/demo capture so recorded frames are smooth and
    // deterministic regardless of render rate:
    //     if (byte_606B9CE == 1)                 // enable flag
    //         frameDelta = dword_615A4F0;        // forced seconds-per-frame
    // Capture start (IDB sub_34B35D0) sets dword_615A4F0 = 1.0f/targetFps
    // (default 60 -> 0.0166667) and flips the flag on; capture stop
    // (sub_34B3950 / sub_3635600) clears it. Nothing in the frame loop clears
    // the flag, so writing it ourselves sticks (live-confirmed by the user).
    //
    // Repurposed as Game Speed: forcing a larger delta advances more sim-time
    // per frame (faster); a smaller one is slow-motion. We drive the multiplier
    // relative to the engine's own 60 FPS reference (dword_615A4F0 = mult/60).
    //
    // Both globals are BSS (zero in the static dump), so they are located by a
    // signature over the override block and resolved from its RIP operands:
    //   match+2 : disp32 of `cmp cs:byte_606B9CE, 1`  (flag; next instr +7)
    //   match+37: `vmovss xmm0, cs:dword_615A4F0`     (value; 8-byte instr)
    // IDB match at 0x8FC348. Unique block.
    inline constexpr const char* kSig_GameSpeed =
        "80 3D ?? ?? ?? ?? 01 75 30 48 8B 4F 58 41 8B C7 C5 78 2F 61 64 0F 97 C0 "
        "85 C0 74 09 80 3D ?? ?? ?? ?? 01 75 14 C5 FA 10 05 ?? ?? ?? ?? C5 FA 11 "
        "41 64 C6 05 ?? ?? ?? ?? 00";
    inline constexpr uintptr_t kOff_GameSpeed_FlagDisp    = 2;  // disp32 of cmp cs:byte_606B9CE,1
    inline constexpr uintptr_t kOff_GameSpeed_FlagEnd     = 7;  // next-instr addr for that cmp
    inline constexpr uintptr_t kOff_GameSpeed_ValueVmovss = 37; // vmovss xmm0,cs:dword_615A4F0
    inline constexpr int       kLen_GameSpeed_Vmovss      = 8;  // that vmovss is 8 bytes (disp at end)
    // Engine fixed-timestep reference: cs:Y (1.0f) / target fps (dword_5E379E0,
    // default 60.0f) => baseline delta 1/60. A 1.00x multiplier over this is the
    // 60-FPS-equivalent step; see World::Tick.
    inline constexpr float     kGameSpeed_BaselineFps     = 60.0f;

    // --- Time of Day: the master field clock (World feature, world.cpp) -------
    // The REAL day/night clock is two BSS globals (client / server realm), each
    // a 32-byte struct of int32s. The per-frame sun/sky update reads them (IDB
    // sub_1CA3890 -> sub_871360) and writes them (IDB sub_8719B0 / sub_1D44970).
    // Everything else about time-of-day is downstream: the "TimeOfDayManager"
    // and its +0x3D0 "currentTimeOfDay" float are a RENDER MIRROR that nothing
    // reads back (writing it is a no-op, live-confirmed), and the engine
    // timeScale is a GLOBAL sim scale that freezes the whole world - both were
    // dead ends. These two globals are the source of truth; writing them moves
    // the clock and the change sticks and keeps flowing (live-verified).
    //
    // Struct layout (from the h/m/s reconstruction math in sub_871360, and
    // live-confirmed 2026-07-20: read day=42 hour=13 min=46 sec=39 matched the
    // in-game clock exactly):
    //   +0x00 i32 day    +0x04 i32 hour    +0x08 i32 minute    +0x0C i32 second
    //
    // Located by a unique signature over sub_1CA3890's realm-select read: the
    // TLS realm probe `mov edx, 1F2h` (498 = the client/server selector byte)
    // followed by the two `vmovups ymm0, cs:<global>` (server if TLS[498], else
    // client). The two RIP operands resolve to the server and client globals.
    inline constexpr const char* kSig_FieldTimeRealm =
        "BA F6 01 00 00 48 8B 08 0F B6 04 0A 84 C0 74 0A "
        "C5 FC 10 05 ?? ?? ?? ?? EB 08 C5 FC 10 05 ?? ?? ?? ??";
    // Within the match: server `vmovups` at +0x10, client `vmovups` at +0x1A;
    // each is 8 bytes (4-byte opcode C5 FC 10 05 + 4-byte disp at its tail).
    inline constexpr uintptr_t kOff_FieldTime_ServerVmovups = 0x10;
    inline constexpr uintptr_t kOff_FieldTime_ClientVmovups = 0x1A;
    inline constexpr int       kLen_FieldTime_Vmovups       = 8;
    // Time struct fields (int32 each).
    inline constexpr uintptr_t kOff_FieldTime_Day  = 0x00;
    inline constexpr uintptr_t kOff_FieldTime_Hour = 0x04;
    inline constexpr uintptr_t kOff_FieldTime_Min  = 0x08;
    inline constexpr uintptr_t kOff_FieldTime_Sec  = 0x0C;

    // --- Time of Day: FREEZE via the field-time tick -------------------------
    // sub_871360 is the per-frame FieldTime tick. Its very first act is
    //   [mgr+0x2C] += frameDeltaSeconds   (a float accumulator)
    // and EVERYTHING downstream advances from it: the reconstruction that
    // rewrites the two realm clock globals above (every game tick, via
    // sub_8719B0) and the sun/sky sync. So freezing the clock is simply forcing
    // that delta to 0 while frozen - the accumulator holds, the globals stop
    // being rewritten, the sun stops, and NOTHING else is touched (physics, AI
    // and combat advance on their own deltas).
    //
    // This supersedes the old pin-the-globals-every-frame freeze, which lost a
    // race against this very function: it rewrites the globals every game tick
    // from its accumulator, so a per-frame pin from another thread never held.
    //
    // The tick is __fastcall(rcx=mgr, xmm1=delta, xmm2=unused); the delta is a
    // single float in xmm1, so the detour prototype declares `float delta` to
    // land on xmm1 and zeroes it. Signature = the ABI-fixed prologue plus the
    // distinctive accumulator add `vaddss xmm0, xmm1, [rcx+2Ch]`
    // (make_signature_for_function, unique in this build).
    inline constexpr const char* kSig_FieldTimeTick =
        "48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 56 41 57 48 8B EC "
        "48 83 EC 70 48 8B F9 C5 F2 58 41 2C";

    // --- Time of Day: FREEZE the visible SUN via the RENDER manager ----------
    // The numeric field clock above is only half the story. The visible
    // day/night (sun/moon/sky) is driven by the RENDER "TimeOfDay" manager's
    // currentTimeOfDay float, which advances on its OWN per-frame accumulator -
    // so freezing the field-time tick stops the numeric clock but the SUN
    // keeps moving (the long-standing "Freeze only freezes the clock" bug).
    //
    // The engine's own debug commands PROVE this is the right layer:
    // PearlAbyssEngine.Debug.TimeOfDayForward / Backward / *x2 (handlers
    // sub_2F616F0 / sub_2F61690 / sub_2F617B0 / sub_2F61750) all call the
    // manager's AdvanceTime (vtable +0x130) to move the sun, and the engine
    // console's /settimeofdaylowerlimit + /settimeofdayupperlimit (handlers
    // sub_31FB810 / sub_31FB860) clamp currentTimeOfDay into [lower,upper].
    // So the reliable sun freeze = force lower == upper == the captured hour
    // every tick; the engine pins the sun to it while real time keeps flowing.
    // Restored to the originals on disable/unload.
    //
    // Runtime chain (current build, re-RE'd 2026-07-21):
    //   engine  = *qword_648F688
    //   manager = *(engine + 0x2F8)   (engine vtable slot 8 getter sub_36341F0
    //                                  = `return *(engine + 0x2F8)`)
    //   manager+0x3D0 f32 currentTimeOfDay (hours 0..24)
    //   manager+0x3D4 f32 lowerLimit
    //   manager+0x3D8 f32 upperLimit
    //
    // qword_648F688 is BSS (runtime-populated by the engine-console registrar
    // sub_31FAB10). Anchored on its one-time init store, guarded by the
    // dword_648F680 == -1 check that precedes it:
    //   cmp cs:dword_648F680, -1 ; jnz ; mov cs:qword_648F688, rbx ; mov cs:.., rdi
    // The engine global = RIP target of that first `mov cs:<g>, rbx` store.
    inline constexpr const char* kSig_TodEngineGlobal =
        "83 3D ?? ?? ?? ?? FF 75 1B 48 89 1D ?? ?? ?? ?? 48 89 3D ?? ?? ?? ?? "
        "48 8D 0D ?? ?? ?? ?? E8";
    inline constexpr uintptr_t kOff_TodEngineGlobal_Mov = 9; // the `48 89 1D <disp32>`
    inline constexpr int       kLen_TodEngineGlobal_Mov = 7; // 3-byte opcode + disp32
    inline constexpr uintptr_t kOff_Tod_Manager     = 0x2F8; // engine -> render manager
    inline constexpr uintptr_t kOff_Tod_CurrentHour = 0x3D0; // f32 hours 0..24
    inline constexpr uintptr_t kOff_Tod_LowerLimit  = 0x3D4; // f32 clamp lower
    inline constexpr uintptr_t kOff_Tod_UpperLimit  = 0x3D8; // f32 clamp upper

    // --- Weather & Atmosphere (world.cpp) ------------------------------------
    // Dynamic AOB patterns for weather intensity hooks in TU 1.18.00+:
    inline constexpr const char* kSig_WeatherRain =
        "48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 6C 01 00 00";

    inline constexpr const char* kSig_WeatherSnow =
        "48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 68 01 00 00";

    inline constexpr const char* kSig_WeatherDust =
        "48 8B 41 ?? 41 B8 40 00 00 00 48 85 C0 41 B9 60 01 00 00 48 8D 50 18 B8 CC 01 00 00 49 0F 44 D0";

    inline constexpr const char* kSig_WindPack =
        "48 89 5C 24 08 57 48 83 EC 30 48 8B 01 48 8B D9 48 85 C0 48 8B FA B9 40 00 00 00 4C 8D 40 18 4C 0F 44 C1";

    // Safe EnvManager resolution and cloud/atmosphere nodes in TU 1.18.00+:
    inline constexpr const char* kSig_EnvManager =
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 40 48 8B 88 F0 0E 00 00";
    inline constexpr uintptr_t kOff_EnvManager_Mov = 3;
    inline constexpr int       kLen_EnvManager_Mov = 7;

    namespace CN {
        inline constexpr ptrdiff_t FOG_A           = 0x134;
        inline constexpr ptrdiff_t DUST_BASE       = 0x138;
        inline constexpr ptrdiff_t CLOUD_TOP       = 0x13C;
        inline constexpr ptrdiff_t CLOUD_THICK     = 0x140;
        inline constexpr ptrdiff_t CLOUD_BASE      = 0x144;
        inline constexpr ptrdiff_t DUST_WIND_SCALE = 0x158;
        inline constexpr ptrdiff_t DUST_THRESH     = 0x198;
        inline constexpr ptrdiff_t STORM_THRESH    = 0x19C;
        inline constexpr ptrdiff_t FOG_B           = 0x1A0;
        inline constexpr ptrdiff_t DUST_ADD        = 0x1A4;
    }

    namespace WN {
        inline constexpr ptrdiff_t DIR_X          = 0x18;
        inline constexpr ptrdiff_t DIR_Z          = 0x1C;
        inline constexpr ptrdiff_t TURB_DENS      = 0x60;
        inline constexpr ptrdiff_t TURB_SCALE     = 0x64;
        inline constexpr ptrdiff_t TURB_LIFT      = 0x68;
        inline constexpr ptrdiff_t SPEED          = 0x88;
        inline constexpr ptrdiff_t GUST           = 0x9C;
        inline constexpr ptrdiff_t CLOUD_SCROLL_X = 0xD0;
        inline constexpr ptrdiff_t CLOUD_SCROLL_Z = 0xD4;
    }

    // --- Armor dye / material / repair-condition (dye.cpp) -------------------
    // The dyehouse system, fully RE'd 2026-07-17 from the server's own dye
    // transaction (IDB sub_257C330 - it logs "sql->dyeItem"):
    //
    // An item's dye state is a vector of 16-byte "dye records" ON THE ITEM
    // VALUE ITSELF - the same 192-byte TrItemValue the inventory code already
    // edits (which is why dye survives unequip/re-equip and saves: it is item
    // state, persisted to the save DB by that transaction). Record layout
    // (field roles derived from this build's own record copier, IDB
    // sub_D20110):
    //     +0  u32  color-group key (dyecolorgroupinfo._key; the dyehouse UI's
    //              color family). The renderer reads the RGB verbatim - this
    //              key just records WHICH palette the color came from - but
    //              write a real one so the game's own dye UI stays coherent.
    //     +4  u16  material template 1..10 into partprefabdyetexturepalleteinfo
    //              (cloth/leather/metal texture variants); 0xFFFF = the item's
    //              natural material.
    //     +6  u8   channel ("mod" 0..11) - WHICH colorable zone of the mesh.
    //              An item defines up to 12 (partprefabdyeslotinfo); records
    //              are keyed by this byte, one per channel.
    //     +7  u8 r, +8 u8 g, +9 u8 b, +10 0xFF
    //     +11 u8   repair condition: 0 = pristine .. 0x7F = fully weathered
    //              (0xFF = legacy "no override", renders pristine)
    //     +13 u8   0x04 on channels 0 and 3 in natural records (mirrored for
    //              shape fidelity; the engine accepts records without it)
    //
    // The vector lives at itemVal+0x70 (data) / +0x78 (u32 count), capped at
    // 12 records; the engine upserts by channel byte. The same TrItemValue
    // shape (with a u16 slot tag appended at +0xC0, stride 0xC8) is what the
    // equip component keeps per equipped slot in its table at comp+0x88 -
    // walking that table IS "what am I wearing right now".
    //
    // HOW WE APPLY: the client's own dye-ack handler (IDB sub_7D9C50) - what
    // runs when the dyehouse server transaction acks (message 2440) - takes
    // (equipComponent, int* err, batchBlob) and does everything: upserts the
    // records into the equipped entry, live-updates the rendered materials
    // per channel (sub_7DA640 set / sub_7DB870 clear - no mesh teardown, no
    // re-equip), propagates to linked parts, and kicks the HUD refresh. We
    // build the blob and call it - the game's own code path end to end.
    // Server-side validation of the record is NONE (the wire handler
    // sub_24588D0 passes arbitrary RGB through), so free-color dyeing works.
    //
    // The batch blob is 10 blocks x 196 bytes: u16 slotTag (0xFFFF = block
    // unused), u16 pad, then 12 x 16-byte records. Records whose channel byte
    // has the high bit set (we use 0xFF) are skipped; an all-zero record with
    // material 0xFFFF and repair 0xFF means CLEAR that channel.
    //
    // PERSISTENCE: worn equipment is NOT in any inventory holder, so there is
    // no inventory item to mirror dye onto - the equip table IS the item's
    // home while it is worn. Proven by the equip path (IDB sub_7D7470): when a
    // slot is vacated it PUTS THE OLD ITEM BACK into a bucket
    // (sub_1CE2600 -> sub_F4B8F50, "copy item value into bucket slot, or add
    // to the stack there"), which would duplicate the item if equipping had
    // left it in the bag. The entry carries the item whole - the item-value
    // copy (sub_F4DAD00) copies the dye vector (+0x70/+0x78) with it, which is
    // also why dye survives an unequip.
    //
    // So the durable target is the SERVER realm's equip component, not a
    // holder. sub_7D9C50 renders, and we only ever call it on the client's
    // component; the server's entry is written as plain data with the engine's
    // own upsert primitive (sub_1F8CB40: find record by channel, overwrite,
    // else append via sub_D20110 if count < 12) - no render calls on a server
    // actor. The append can ALLOCATE, so that write runs with the realm flag
    // flipped (kTls_RealmFlag), exactly like the add-item path.
    //
    // (An earlier design searched both holders by instance id and always came
    // up empty - live-confirmed 2026-07-17, every slot: "no client-side item
    // with instance id 1001675". The visual apply was fine; the item was never
    // there to find.)
    //
    // The dyehouse's own server transaction (sub_257C330) addresses items by
    // (storageType, slotIndex) via sub_1CE3CB0 - i.e. it dyes an item sitting
    // in a bucket, then broadcasts the result, which is the ack we call
    // directly. That path is no use to us for worn gear, for the reason above.

    // The equip component of a character: *(*(actor + 0x68) + 0x38). The
    // engine's own route - the dye-ack dispatcher (IDB sub_9BDF30) resolves
    // the broadcast's actor and calls the applier with
    // `*(*(actor + 104) + 56)`, and BatchEquip reaches the same object from
    // the other side (`*(*(comp+8) + 104) + 56`). comp+0x08 is the owning
    // actor, which makes the walk self-validating: resolve the component from
    // an actor, then require it to point back.
    //
    // This is what lets us reach BOTH realms without waiting on a hook: the
    // character actor IS what inventory.cpp calls a container (its holder walk
    // *(*(actor+0x68)+0xB8) is sub_1CDE460 verbatim), and that file already
    // resolves the client's by global walk and the server's from the commit
    // hook's capture list. A hooked component is whichever realm happened to
    // fire, and only re-fires when the player changes gear.
    inline constexpr uintptr_t kOff_Sub_EquipComp = 0x38; // (actor+0x68)+0x38
    inline constexpr uintptr_t kOff_EquipComp_Owner = 0x08; // -> the actor

    // The equip-batch function (IDB sub_7C98D0, "BatchEquip"): its rcx is the
    // component whose +0x88 table drives everything above.
    // Hooked as a FALLBACK capture path for the walk above (and as the signal
    // that a gear change happened). Fires on every player equip change
    // including the initial load-in dress-up. Signature = full
    // prologue through the arg shuffle (mov r15,r8; mov r12,rdx; mov r14,rcx;
    // mov r13,[rcx+8]); stack/frame immediates wildcarded. Unique.
    inline constexpr const char* kSig_EquipBatch =
        "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 "
        "48 8D AC 24 ? ? ? ? B8 ? ? ? ? "
        "E8 ? ? ? ? 48 2B E0 4D 8B E0 4C 8B EA 4C 8B F1 4C 8B 79 08";

    // The client dye-ack applier (IDB sub_7D9C50):
    //     int* f(void* equipComponent, int* outErr, void* batch1960)
    // Called directly with our crafted batch. Signature = prologue + the
    // literal 0x120 frame + arg shuffle (mov r12,r8; mov rsi,rdx). If a patch
    // resizes the frame, re-find via xrefs to the dye upsert (kSig_DyeUpsert)
    // from a ~0x550-byte function in the equip-component code region.
    // 1.17.00 note: this render-side applier still needs a fresh signature.
    // Dye::Install fails closed rather than calling an unverified function.
    inline constexpr const char* kSig_DyeApplyBatch =
        "48 89 5C 24 ? 48 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 "
        "48 8D 6C 24 ? 48 81 EC 20 01 00 00 4D 8B E0 48 8B F2";

    // 1.17 read-only candidate: same saved-register set and ABI shuffle as
    // the prior applier, but the compiler now uses a 0x50 frameless stack.
    // Kept separate until runtime-address logging confirms one unique hit.
    inline constexpr const char* kSig_DyeApplyBatch117Candidate =
        "48 89 5C 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 "
        "48 83 EC 50 4D 8B E0 48 8B F2 4C 8B F1";

    // The dye-record upsert primitive (IDB sub_1F8CB40):
    //     void f(void* itemVal, const uint8_t record[16])
    // Finds the record whose +6 channel matches, overwrites it; else appends
    // (growing the vector in the CALLING THREAD'S REALM) while count < 12.
    // Used for the inventory-instance mirror. The `49 C1 E0 04` is the
    // 16-byte record stride (shl r8,4) - semantic, keep literal.
    // 1.17.00 note: record upsert still needs a fresh signature.
    inline constexpr const char* kSig_DyeUpsert =
        "48 8B 41 78 4C 8B D1 44 8B 81 80 00 00 00 49 C1 E0 04";

    // Equip component layout (verified in THIS build from BatchEquip's own
    // table walk: `a1[17]` -> desc, `*(desc+8) + 200*i`, tag at +192).
    inline constexpr uintptr_t kOff_EquipComp_Table  = 0x80; // 1.17 -> table descriptor (live client/server chain capture)
    inline constexpr uintptr_t kOff_EquipTable_Array = 0x08; // entry[] base
    inline constexpr uintptr_t kOff_EquipTable_Count = 0x10; // u32
    inline constexpr uintptr_t kEquipEntry_Stride    = 0xD0; // 1.17: TrItemValue grew by 8 bytes
    inline constexpr uintptr_t kOff_EquipEntry_SlotTag = 0xC8; // 1.17 u16 slot tag (helm 3, chest 4,
                                                               // gloves 5, boots 6, cloak 16)
    // Within an entry, the TrItemValue fields reuse kOff_ItemVal_InstanceId /
    // kOff_InvSlot_TypeId / kOff_InvSlot_Quantity above, plus:
    inline constexpr uintptr_t kOff_ItemVal_DyeData  = 0x78; // 1.17 -> 16-byte record[]
    inline constexpr uintptr_t kOff_ItemVal_DyeCount = 0x80; // 1.17 u32 count (capacity at +0x84)
    inline constexpr uint32_t  kDye_MaxChannels      = 12;

    // --- Abyss Gear sockets (live-cracked 2026-07-18; see the abyss-gear note) -
    // Every worn item's TrItemValue carries a socket list right next to its dye
    // vector: a PRE-ALLOCATED 5-slot vector of 6-byte records, plus a separate
    // inline count of how many sockets are actually unlocked. The record array
    // is index-addressed (record[i] = socket i); the unlocked count is the real
    // "how many sockets" driver, not the record's own index byte. Both fields
    // ride with the item value on save (sub_F4DAD00 deep-copies the +0x58 vector
    // and the +0x68 count), so a server-realm write persists exactly like dye.
    //
    // Add/remove a gear touches ONLY a record's bytes (that is all the game's own
    // Witch-socket does); unlocking a NEW socket also grows a save-data sublist
    // inside the +0x58 target at +0xC0, which we do NOT reproduce - so unlocking
    // renders live but is not durable yet (see Equipment::UnlockAll).
    inline constexpr uintptr_t kOff_ItemVal_SocketData        = 0x60; // 1.17+: -> record[] vector data
    inline constexpr uintptr_t kOff_ItemVal_SocketData_Legacy = 0x58; // Legacy: -> record[]
    inline constexpr uintptr_t kOff_ItemVal_SocketSize        = 0x68; // u32 vector size (always 5)
    inline constexpr uintptr_t kOff_ItemVal_SocketCap         = 0x6C; // u32 vector capacity (5)
    inline constexpr uintptr_t kOff_ItemVal_SocketUnlocked    = 0x70; // u32 unlocked-socket count
    inline constexpr uintptr_t kSocketRec_Stride              = 6;
    inline constexpr int       kSocket_Max                    = 5;    // absolute max (matches the vector capacity)
    // Record layout (6 bytes):
    inline constexpr uintptr_t kOff_SockRec_GearId = 0; // u16 abyss-gear typeId (0xFFFF = empty)
    inline constexpr uintptr_t kOff_SockRec_Marker = 2; // u16 0xFFFF when filled, 0x0000 when empty
    inline constexpr uintptr_t kOff_SockRec_Index  = 4; // u8  socket index (0xFF = the game's "locked" record)
    inline constexpr uintptr_t kOff_SockRec_State  = 5; // u8  0x05 filled, 0x00 empty
    inline constexpr uint16_t  kSock_Empty         = 0xFFFF;

    // --- Refinement level (the equipment "refinement"/enhancement upgrade) -----
    // Every piece refines up to level 10 (wiki: "Refinement"; internally the
    // enhancement/"강화" concept). The level lives inline on the TrItemValue at
    // +0x0A - the SAME u16 offsets.h otherwise calls the item subtype: a freshly
    // made stack reads 0 there, a refined piece carries its level (seen 7->8 live
    // in the value copier sub_F4DAD00). Because sub_F4DAD00 copies +0x0A, the
    // level rides with the item value on save, so - exactly like dye/sockets - a
    // write mirrored into BOTH realms persists (a client-only write is undone by
    // the server->client reconcile, which zeroes a client +0x0A). Two things stay
    // live-verify-only, so treat them as unconfirmed until tested in-game:
    //   * whether bumping +0x0A alone re-derives the piece's stats (real
    //     refinement also rebuilds derived data and spins a new item instance);
    //     we trigger the same effect refresh a socket edit does as the best lever.
    //   * whether the server accepts an out-of-band level on reconcile/save.
    inline constexpr uintptr_t kOff_ItemVal_RefineLevel = 0x0A; // u16, == kOff_ItemVal_Subtype
    inline constexpr int       kRefine_Max              = 10;
    inline constexpr uintptr_t kOff_ItemVal_Durability  = 0x40; // u16 durability (max ~10000 or 1000)

    // The equipped-item EFFECT refresh (IDB sub_7C88A0): re-applies every
    // equipped item's effects - re-reading each item's abyss-gear sockets and
    // REBUILDING its derived effect data (sub_7C55B0 per item), then the final
    // recompute pass (sub_7E1160 -> sub_7CB670/sub_7CCBD0). This is exactly what
    // the Witch's own socket action runs. A raw socket write updates the record
    // but leaves the derived effect structure stale, so the gear stays dormant
    // until a reload; calling this on the client equip component (game thread)
    // makes it take hold live. (An earlier attempt called only the last-stage
    // re-aggregators sub_7CB670/sub_7CCBD0 - they READ the derived structure and
    // do not rebuild it, so they did nothing on a raw write. Live trace of the
    // Witch's socketing found sub_7C88A0 as the real entry.)
    // Signature: void* f(equipComponent, int* out).
    // 1.17.00 note: the old effect-refresh AOB no longer resolves. Socket/refine
    // edits remain guarded and persistent; live effects may wait for a reload.
    inline constexpr const char* kSig_EquipEffectRefresh =
        "48 89 5C 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 "
        "48 8B EC 48 83 EC 60 4C 8B F2";

    // --- Why live REMOVAL of an abyss gear does not strip its effect -----------
    // (RE 2026-07-19, static trace of the whole effect cluster around sub_7C88A0)
    //
    // The applied stat/skill effects do NOT live in any list we can just clear.
    // The engine applies them through a DIFF ENGINE: sub_7CA0E0(comp, err,
    // &newList, flag, &oldList) -> sub_7CFBD0(comp, &newList, &oldList) computes
    // new-minus-old and pushes the delta into the character stat aggregator. An
    // effect is only REMOVED when the gone gear is present in the caller's OLD
    // list. Every apply path (sub_7C98D0 batch equip, sub_7D7470 single equip,
    // sub_7DE320 per-slot) funnels through sub_7CA0E0.
    //
    // Two dead ends already tried and disproven live:
    //   * sub_7CB670 / sub_7CCBD0 - READ the derived data, do not rebuild it.
    //   * the append-only accumulator acc = *(*(actor+0x68)+0x178) (list
    //     {data@+0x118, count@+0x120, cap@+0x124}, sub_7C55B0 appends to it) -
    //     clearing its count before sub_7C88A0 did NOT strip the effect, because
    //     the effect was already pushed downstream by the diff apply; the list is
    //     just a working ledger, not the applied state.
    //
    // The caller always re-derives its "old" list from the item's CURRENT socket
    // records, so once we have already emptied the record a raw refresh (or even
    // the game's own unequip/re-equip) can never see the removed gear in "old" ->
    // nothing to subtract. Only a full reload fixes it, because the item's whole
    // effect state is rebuilt from scratch on load. A correct live fix must drive
    // sub_7CA0E0 with an OLD list that still contains the removed gear (or call
    // the game's own Witch unsocket handler) - needs a live trace to pin, do NOT
    // ship another blind clear.

    // Batch blob geometry for kSig_DyeApplyBatch.
    inline constexpr size_t    kDyeBatch_Blocks     = 10;
    inline constexpr size_t    kDyeBatch_BlockSize  = 196;  // u16 tag + pad + 12*16
    inline constexpr size_t    kDyeBatch_RecordsOff = 4;
    inline constexpr size_t    kDyeBatch_Size       = kDyeBatch_Blocks * kDyeBatch_BlockSize;

    // The color families (dyecolorgroupinfo keys + preset shades) and the
    // dyeable-prefab registry (partprefabdyeslotinfo) live in dye_data.h,
    // generated straight from this install's own data tables by
    // scripts/gen_dye_data.py - rerun it after a game patch.

    // --- Trust Multiplier: scale the friendly ("Friendly"/친밀도) gain --------
    // "Friendly" is the engine's TRUST value for both NPCs (gifting) and
    // animals/mounts (feeding-to-tame): strings UI_Gift_Friendly_IncreaseAmount
    // and UI_Vehicle_FriendlyLevelUp, runtime field _varyFriendly. It is a
    // 0..100 value; reaching 100 is what completes taming.
    //
    // The value is stored in a per-relationship 0x58-byte RECORD held in the
    // friendly component, and EVERY write to it - gift, feed, AI, save-load,
    // and the network sync - funnels through ONE of two leaf setters (both
    // live-confirmed: a HW watchpoint on an NPC's trust value broke at the
    // update store 0xDBE114F inside the NPC setter). The batch/RPC path
    // (sub_613220) does NOT fire on a direct gift - it is downstream of these,
    // so hooking the setters is strictly more complete:
    //   _DWORD* setNpc(void* npcMap /*=comp+0x18 owner*/, void* record)  // IDB sub_DBE1000
    //   _DWORD* setPet(void* petMap /*=comp+0x38 owner*/, void* record)  // IDB sub_1AD4710
    // Both take the destination map owner in rcx and the SOURCE record in rdx;
    // they locate/insert the matching slot and copy the 0x58-byte record in.
    // Record layout (the copy is 4 SIMD stores at +0x00/+0x20/+0x40/+0x50):
    //   record +0x00 : u32  record key   (matched within the bucket)
    //   record +0x04 : u16  group key    (selects the bucket)
    //   record +0x20 : i64  TRUST value  (the field written at 0xDBE114F; the
    //                       same field sub_613220 compares >= 100 to tame)
    // The two setters differ only by which map they target (NPC comp+0x18 vs
    // pet/vehicle comp+0x38), which is the byte the prologue's `lea rbp,[rcx+..]`
    // encodes - that displacement is the signature discriminator.
    //
    // The Trust Multiplier scales the GAIN: for each write, compare the record's
    // new value to the last value we let through for that (map,group,key) and,
    // if it went up, rewrite value = clamp(old + (new-old)*mult, 0, 100). The
    // cache is seeded (unscaled) the first time a key is seen - and because the
    // save-loader drives these SAME setters at login, every relationship is
    // pre-seeded there, so the first in-game gift/feed is already scaled and a
    // loaded save is never re-scaled. See the trinity-friendly-system notes.
    inline constexpr const char* kSig_FriendlySetNpc =
        "49 89 E3 53 55 56 57 41 56 48 83 EC 60 48 89 D7 48 8D 69 18 0F B7 42 04 66 41 89 43 08";
    inline constexpr const char* kSig_FriendlySetPet =
        "4C 8B DC 53 55 56 57 41 56 48 83 EC 60 48 8B FA 48 8D 69 38 "
        "0F B7 42 04 66 41 89 43 08";

    inline constexpr uintptr_t kOff_FriendlyRec_Key   = 0x00; // u32 record key
    inline constexpr uintptr_t kOff_FriendlyRec_Group = 0x04; // u16 group/bucket key
    inline constexpr uintptr_t kOff_FriendlyRec_Value = 0x20; // i64 trust value
    inline constexpr int64_t   kFriendly_Max          = 100;  // the taming cap
}
