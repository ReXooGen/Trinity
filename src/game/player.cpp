#include "player.h"
#include "teleport.h"
#include "inventory.h"

#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <cstdint>
#include <iterator>

#include <MinHook.h>

#pragma comment(lib, "xinput9_1_0.lib")

#include "offsets.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../mem/hooks.h"
#include "../core/logger.h"
#include "../core/state.h"

namespace trinity::game
{
    using mem::ReadPtr;
    using mem::Read64;
    using mem::Read32;
    using mem::Read8;
    using mem::Write64;

    namespace
    {
        // --- Fresh player-set resolution (character-manager global) --------
        // Crimson Desert is a three-protagonist game: Kliff plus two companions,
        // all of them controllable, summonable, and able to coexist in the world
        // at once (swapped between, or the other two summoned as companions). The
        // stat features must cover EVERY active protagonist, not just the single
        // locally-possessed body the engine's own accessor (sub_2393AA0) returns.
        //
        // The character you actively control is NOT reliably tagged SelfPlayer
        // (playing a secondary protagonist re-tags it Mercenary), so we identify
        // protagonists by their shared character-class VTABLE + a live vital
        // chain instead of by tag - see TickResolveSelf. We resolve the whole
        // SET fresh every game tick from the gameplay-character manager, never
        // cached: a body transition (mount / transform / character swap)
        // reallocates a character, but the next resolve simply rebuilds the set,
        // so there is no stale-pointer churn to track. See offsets.h
        // (kCharMgrAnchors) and the trinity-engine-architecture notes.
        //
        // Resolved address of the qword_6181090 slot; the manager is
        // *(*g_charMgrGlobal). Zero if no anchor resolved, in which case every
        // stat feature below is inert (RefreshSelf no-ops).
        uintptr_t g_charMgrGlobal = 0;

        // Resolve the char-manager global by consensus across kCharMgrAnchors.
        // Each anchor is an independent call site that RIP-resolves the same
        // global, so they act as each other's check: we take the value the most
        // anchors agree on, and log loudly on any disagreement or on anchors
        // that stopped matching. A single update is very unlikely to break all
        // of them, and the vote means a lone stale survivor pointing at a
        // sibling realm's manager (see offsets.h) cannot silently win.
        uintptr_t ResolveCharMgrGlobal()
        {
            constexpr int kN = static_cast<int>(std::size(kCharMgrAnchors));
            uintptr_t vals[kN] = {};
            int votes[kN] = {};
            int distinct = 0, matched = 0;

            for (const CharMgrAnchor& a : kCharMgrAnchors)
            {
                const uintptr_t m = mem::FindPattern(a.sig);
                if (!m) continue;
                const uintptr_t g = mem::ResolveRipAt(m + a.movOff, 7);
                if (!g) continue;

                ++matched;
                int i = 0;
                for (; i < distinct; ++i)
                    if (vals[i] == g) { ++votes[i]; break; }
                if (i == distinct) { vals[distinct] = g; votes[distinct] = 1; ++distinct; }
            }

            if (!distinct) return 0;

            int best = 0;
            for (int i = 1; i < distinct; ++i)
                if (votes[i] > votes[best]) best = i;

            // Anchors that disagree mean at least one is matching the wrong site
            // (a sibling realm's manager resolves fine and then fails silently),
            // so surface it rather than trusting the winner blindly.
            if (distinct > 1)
                LOG_WARN("player: char-manager anchors DISAGREE (%d distinct values); "
                         "using %p with %d/%d votes - re-derive the anchors.",
                         distinct, reinterpret_cast<void*>(vals[best]), votes[best], matched);
            else if (matched < kN)
                LOG("player: char-manager successfully resolved (%d anchors verified).", matched);

            return vals[best];
        }

        // The player set's stat entries + battle-damage identities, recomputed
        // each tick by RefreshSelf(). The stat hooks match against these live
        // sets instead of a historical cache: because they are always the
        // current bodies', membership (not a ring) is correct and self-healing.
        // Public sets have margin for scanning, but a VALID gameplay party can
        // never exceed the game's three protagonists. A fourth same-vtable body
        // is the character/equipment-menu preview actor and must never receive
        // stat writes.
        constexpr int kMaxPlayers      = 8;
        constexpr int kMaxPartyPlayers = 3;
        constexpr int kMaxGaugePerType = 16;                   // stamina/spirit gauges per body
        constexpr int kMaxStatEntries  = kMaxPlayers * kMaxGaugePerType;

        std::atomic<uintptr_t> g_hpEntries[kMaxPlayers]{};
        std::atomic<uintptr_t> g_stamEntries[kMaxStatEntries]{};
        std::atomic<uintptr_t> g_mountStamEntries[kMaxStatEntries]{};
        std::atomic<uintptr_t> g_spiritEntries[kMaxStatEntries]{};
        // Battle-damage identities, one per tracked player. A player's actor is
        // the attacker side of an outgoing hit; its vital/target owner (the
        // "root" object) is the victim side of an incoming one. A hit against any
        // protagonist is scaled by the incoming multiplier; a hit dealt by any of
        // them is scaled by the outgoing one. See the damage-apply hook below.
        std::atomic<uintptr_t> g_actors[kMaxPlayers]{};
        std::atomic<uintptr_t> g_targetOwners[kMaxPlayers]{};

        constexpr int kMaxMounts = 4;
        std::atomic<uintptr_t> g_mountActors[kMaxMounts]{};
        std::atomic<int>       g_mountCount{0};

        // Stat commit (pa_StatCommit / IDB sub_BED7820) - the single funnel every
        // HP/Stamina/Spirit write passes through. God Mode, Infinite Stamina
        // and Infinite Spirit all guard it; see the hook below.
        using StatCommit_t = int64_t(__fastcall*)(void* entry, int64_t time, int64_t target, uint16_t flag);
        StatCommit_t oStatCommit = nullptr;
        void* g_commitTarget = nullptr;

        // Damage-apply dispatcher (pa_StatApplyDelta / IDB sub_145B2A0) - one
        // level above the commit funnel, the only site where a battle hit
        // still carries BOTH its victim (targetOwner) and its attacker
        // (sourceCtx). The damage multipliers scale the signed delta here.
        using DamageApply_t = int64_t(__fastcall*)(void* targetOwner, uint16_t statusId,
                                                   int64_t time, int64_t delta, uintptr_t sourceCtx,
                                                   char a6, char a7, char a8, char a9, char a10,
                                                   void* out);
        DamageApply_t oDamageApply = nullptr;
        void* g_damageHookTarget = nullptr;

        // --- Stat-entry typing --------------------------------------------
        bool StatEntryType(uintptr_t entry, int32_t* type)
        {
            uint32_t t = 0;
            if (!Read32(entry + kOff_StatEntry_Type, &t)) return false;
            *type = static_cast<int32_t>(t);
            return true;
        }

        bool IsHealthType(int32_t t)  { return t == StatType_Health; }

        // Match authoritative stamina gauges in Crimson Desert (Sprint 20, Pool 22, Mount Gallop/Flight 19).
        // Strictly purged 17 & 18 (internal Heat/Combustion gauges) and 48 (Fire Breath) to completely prevent character auto-ignition.
        bool IsStaminaType(int32_t t) {
            return t == StatType_SprintSt || t == StatType_StaminaPool117 ||
                   t == StatType_MountSprint || t == 19 || t == 20 || t == 22;
        }

        // Both spirit-typed HUD gauges: 21 (SpiritPool) and 23 (SpiritPool117)
        bool IsSpiritType(int32_t t)  {
            return t == StatType_SpiritPool || t == StatType_SpiritPool117;
        }

        // True if `e` is a member of one of the resolved player sets (a tiny
        // linear scan; a fresh resolve keeps each set to just the live bodies').
        bool InSet(const std::atomic<uintptr_t>* set, int n, uintptr_t e)
        {
            if (e < kMinPointer) return false;
            for (int i = 0; i < n; ++i)
                if (set[i].load(std::memory_order_relaxed) == e) return true;
            return false;
        }

        // Force an entry's current value back to full (whichever of base/cap
        // holds the max), writing both representations the game reads.
        void PinEntry(uintptr_t e)
        {
            if (e < kMinPointer) return;
            uint64_t base = 0, cap = 0;
            if (!Read64(e + kOff_StatEntry_Base, &base)) return;
            if (!Read64(e + kOff_StatEntry_Cap, &cap)) return;
            if (base > 1000000000ULL || cap > 1000000000ULL) return;
            const uint64_t full = (cap > base) ? cap : base;
            if (!full) return;
            Write64(e + kOff_StatEntry_Current, full);
            Write64(e + kOff_StatEntry_Norm,    full - base);
        }

        // The engine accessor's class gate: the type-descriptor tag byte at
        // *(owner+0x88)+1 is 1 (SelfPlayer) or 9 (OtherPlayer) for player-class
        // characters - ((tag - 1) & 0xF7) == 0 is the exact test sub_2393AA0
        // (and sub_30DF50) compiles to. This is the ONLY reliable type read:
        // the +0x48 objType word on the live player flickers (seen 0/3/8), so it
        // is not used. This gate identifies the protagonist character CLASS - we
        // use it only to derive that class's vtable (any pool slot will do); the
        // actual active-body selection is vtable-based (see TickResolveSelf),
        // because the body you control is re-tagged (Mercenary=4) when playing a
        // secondary protagonist and would slip past a tag-only test.
        bool IsPlayerClass(uintptr_t owner)
        {
            uint64_t td = 0;
            uint8_t tag = 0;
            return Read64(owner + kOff_Owner_TypeDesc, &td) && td >= kMinPointer &&
                   Read8(static_cast<uintptr_t>(td) + 1, &tag) && ((tag - 1) & 0xF7) == 0;
        }

        // The player's resolved identity/stat chain for one tick.
        struct SelfChain { uintptr_t actor, targetOwner, statArray; };

        // Walk owner -> actor(+0x68) -> marker(+0x20) -> root(+0x18) ->
        // statArray(+0x58). The array base is the Health entry (index 0);
        // return false unless it type-checks as Health, which validates the
        // whole chain before we trust it. `root` doubles as the vital/target
        // owner battle damage is addressed to.
        bool WalkSelfChain(uintptr_t owner, SelfChain* out)
        {
            uint64_t actor = 0, marker = 0, root = 0, arr = 0;
            if (!Read64(owner + kOff_Owner_Actor, &actor) || actor < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(actor) + kOff_Actor_StatusMarker, &marker) ||
                marker < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(marker) + kOff_Marker_TargetOwner, &root) ||
                root < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(root) + kOff_Root_StatArray, &arr) ||
                arr < kMinPointer) return false;
            int32_t t = 0;
            if (!StatEntryType(static_cast<uintptr_t>(arr), &t) || !IsHealthType(t)) return false;

            out->actor       = static_cast<uintptr_t>(actor);
            out->targetOwner = static_cast<uintptr_t>(root);
            out->statArray   = static_cast<uintptr_t>(arr);
            return true;
        }

        // Dedicated mount vital chain walker: does not require humanoid Health entry at index 0
        bool WalkMountVitalChain(uintptr_t owner, uintptr_t* outStatArray)
        {
            uint64_t actor = 0, marker = 0, root = 0, arr = 0;
            if (!Read64(owner + kOff_Owner_Actor, &actor) || actor < kMinPointer)
                actor = owner;
            if (!Read64(static_cast<uintptr_t>(actor) + kOff_Actor_StatusMarker, &marker) ||
                marker < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(marker) + kOff_Marker_TargetOwner, &root) ||
                root < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(root) + kOff_Root_StatArray, &arr) ||
                arr < kMinPointer) return false;
            if (outStatArray) *outStatArray = static_cast<uintptr_t>(arr);
            return true;
        }

        // Recompute the player set's identities and stat entries from a fresh
        // resolve. Nothing is cached: the manager and its vector are re-read
        // fresh, so a body transition / character swap is picked up next tick.
        //
        // The set we want is every ACTIVE protagonist - Kliff, the character you
        // are currently playing, and any summoned companion. The class TAG alone
        // does not identify them: the game keeps a large pool of player-class
        // (SelfPlayer/OtherPlayer) character slots, but the body you actively
        // control is re-tagged when you play a secondary protagonist (observed
        // live: the played character carries tag 4 / Mercenary, not SelfPlayer,
        // while Kliff-as-companion keeps SelfPlayer). What they DO share is the
        // protagonist character-class vtable. So we (A) derive that vtable from
        // any player-class character, then (B) track every character of that
        // exact class whose vital chain resolves - which is precisely the active
        // protagonists (the pool's inactive slots have no stat array and every
        // NPC/enemy is a different class), tag-agnostic.

        // Zero every resolved set. Used both when no protagonist is resolvable
        // this tick and when the resolve is skipped entirely (see RefreshSelf) -
        // so a stale entry can never match after a transition, swap, or a
        // feature being toggled back on.
        void ClearPlayerSets()
        {
            for (int i = 0; i < kMaxPlayers; ++i)
            {
                g_hpEntries[i].store(0, std::memory_order_release);
                g_actors[i].store(0, std::memory_order_release);
                g_targetOwners[i].store(0, std::memory_order_release);
            }
            for (int i = 0; i < kMaxStatEntries; ++i)
            {
                g_stamEntries[i].store(0, std::memory_order_release);
                g_mountStamEntries[i].store(0, std::memory_order_release);
                g_spiritEntries[i].store(0, std::memory_order_release);
            }
        }

        // The resolve exists solely to feed the stat pins (God Mode / Infinite
        // Stamina / Infinite Spirit) and the damage multipliers. When none of
        // those consume the sets, the whole-character-list walk it does every
        bool AnyStatFeatureActive(const State& st)
        {
            return st.godMode || st.infStamina || st.infMountStamina || st.infSpirit ||
                   st.dmgInMult != 1.0f || st.dmgOutMult != 1.0f ||
                   Teleport::IsProtected() || Teleport::GetFlightEngaged();
        }

        static bool IsPlayerHoldingGuard()
        {
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                (GetAsyncKeyState('Q') & 0x8000) != 0 ||
                (GetAsyncKeyState('F') & 0x8000) != 0)
                return true;

            XINPUT_STATE xs{};
            for (DWORD i = 0; i < 4; ++i)
            {
                if (XInputGetState(i, &xs) == ERROR_SUCCESS)
                {
                    if ((xs.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 ||
                        xs.Gamepad.bLeftTrigger > 30)
                        return true;
                }
            }
            return false;
        }

        void TickResolveSelf()
        {
            if (!g_charMgrGlobal) return;
            uint64_t p = 0, mgr = 0, data = 0;
            if (!Read64(g_charMgrGlobal, &p) || p < kMinPointer) return;                 // P = *slot
            if (!Read64(static_cast<uintptr_t>(p), &mgr) || mgr < kMinPointer) return;   // mgr = *P
            if (!Read64(static_cast<uintptr_t>(mgr) + kOff_CharMgr_ListData, &data) ||
                data < kMinPointer)
                return;
            uint32_t count = 0;
            if (!Read32(static_cast<uintptr_t>(mgr) + kOff_CharMgr_ListCount, &count) ||
                count == 0 || count > kCharList_MaxCount)
                return;

            // (A) The protagonist class vtable = the vtable of any player-class
            // character (the SelfPlayer/OtherPlayer pool all share it). Nothing
            // is hardcoded, so it survives game updates and rebasing.
            uint64_t anchorVt = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint64_t ch = 0;
                if (!Read64(static_cast<uintptr_t>(data) + 8ull * i, &ch) || ch < kMinPointer) continue;
                if (!IsPlayerClass(static_cast<uintptr_t>(ch))) continue;
                if (Read64(static_cast<uintptr_t>(ch), &anchorVt) && anchorVt >= kMinPointer) break;
                anchorVt = 0;
            }
            if (!anchorVt)  // not in world / no protagonist resolvable
            {
                ClearPlayerSets();
                return;
            }

            // (B) Track every active instance of that class: same vtable AND a
            // resolvable vital chain (skips the pool's empty slots).
            uintptr_t nextHp[kMaxPlayers]{};
            uintptr_t nextActors[kMaxPlayers]{};
            uintptr_t nextTargets[kMaxPlayers]{};
            uintptr_t nextStam[kMaxStatEntries]{};
            uintptr_t nextMountStam[kMaxStatEntries]{};
            uintptr_t nextSpir[kMaxStatEntries]{};
            uintptr_t nextMounts[kMaxMounts]{};
            int nPlayers = 0, nStam = 0, nMountStam = 0, nSpir = 0, nMounts = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint64_t ch = 0;
                if (!Read64(static_cast<uintptr_t>(data) + 8ull * i, &ch) || ch < kMinPointer) continue;
                const uintptr_t owner = static_cast<uintptr_t>(ch);

                // Scan for mount / vehicle entity (Obj_Vehicle = 5, Obj_Pet = 6, or tag == 5)
                uint32_t objType = 0;
                Read32(owner + kOff_Owner_ObjectType, &objType);
                uint64_t td = 0;
                uint8_t tag = 0;
                if (Read64(owner + kOff_Owner_TypeDesc, &td) && td >= kMinPointer)
                    Read8(static_cast<uintptr_t>(td) + 1, &tag);

                if (objType == Obj_Vehicle || tag == 5 || objType == Obj_Pet)
                {
                    uint64_t mountActor = 0;
                    Read64(owner + kOff_Owner_Actor, &mountActor);
                    const uintptr_t act = (mountActor >= kMinPointer) ? static_cast<uintptr_t>(mountActor) : owner;
                    if (act >= kMinPointer && nMounts < kMaxMounts)
                    {
                        bool alreadyAdded = false;
                        for (int m = 0; m < nMounts; ++m)
                        {
                            if (nextMounts[m] == act) { alreadyAdded = true; break; }
                        }
                        if (!alreadyAdded)
                            nextMounts[nMounts++] = act;
                    }

                    // Scan mount vital chain for dedicated horse sprint/gallop gauges
                    uintptr_t mountStatArray = 0;
                    if (WalkMountVitalChain(owner, &mountStatArray))
                    {
                        for (int k = 0; k < kStatArray_ScanEntries; ++k)
                        {
                            const uintptr_t e = mountStatArray + k * kSizeof_StatEntry;
                            int32_t stt = 0;
                            if (!StatEntryType(e, &stt)) continue;
                            if (stt == StatType_MountSprint || stt == 19)
                            {
                                if (nMountStam < kMaxStatEntries)
                                    nextMountStam[nMountStam++] = e;
                            }
                        }
                    }
                }

                if (nPlayers >= kMaxPlayers) continue;

                uint64_t vt = 0;
                if (!Read64(owner, &vt) || vt != anchorVt) continue;
                SelfChain c;
                if (!WalkSelfChain(owner, &c)) continue;

                nextHp[nPlayers] = c.statArray;
                nextActors[nPlayers] = c.actor;
                nextTargets[nPlayers] = c.targetOwner;
                ++nPlayers;

                // Scan protagonist stat array for stamina (20/22) and spirit (21/23) gauges ONLY
                for (int k = 1; k < kStatArray_ScanEntries; ++k)
                {
                    const uintptr_t e = c.statArray + k * kSizeof_StatEntry;
                    int32_t stt = 0;
                    if (!StatEntryType(e, &stt)) continue;
                    if (stt == StatType_SprintSt || stt == StatType_StaminaPool117 || stt == 20 || stt == 22)
                    {
                        if (nStam < kMaxStatEntries) nextStam[nStam++] = e;
                    }
                    else if (IsSpiritType(stt))
                    {
                        if (nSpir < kMaxStatEntries) nextSpir[nSpir++] = e;
                    }
                }
            }

            // Opening the game's character/equipment screen creates a fourth
            // same-class preview body. The old resolver published it and then
            // PinEntry wrote into its temporary stat array, causing the CTD.
            // Clear first and stay inert for this tick; never publish preview
            // pointers, even briefly.
            if (nPlayers > kMaxPartyPlayers)
            {
                // Menus and some populated scenes can expose extra instances
                // of the protagonist class. Do not disable all stat features:
                // keep only the first three live party bodies, which are the
                // same stable set published during normal gameplay, and drop
                // every gauge collected from later preview/duplicate bodies.
                nPlayers = kMaxPartyPlayers;
                nStam = 0;
                nSpir = 0;
                for (int i = 0; i < nPlayers; ++i)
                {
                    const uintptr_t statArray = nextHp[i];
                    for (int k = 1; k < kStatArray_ScanEntries; ++k)
                    {
                        const uintptr_t e = statArray + k * kSizeof_StatEntry;
                        int32_t stt = 0;
                        if (!StatEntryType(e, &stt)) continue;
                        if (stt == StatType_SprintSt || stt == StatType_StaminaPool117 || stt == 20 || stt == 22)
                        {
                            if (nStam < kMaxStatEntries) nextStam[nStam++] = e;
                        }
                        else if (IsSpiritType(stt))
                        {
                            if (nSpir < kMaxStatEntries) nextSpir[nSpir++] = e;
                        }
                    }
                }

                static bool s_previewLogged = false;
                if (!s_previewLogged)
                {
                    LOG_WARN("player: extra player-class bodies detected - using the first %d "
                             "stable party bodies for stat writes.", kMaxPartyPlayers);
                    s_previewLogged = true;
                }
            }

            // Require the exact candidate identity set to survive three game
            // ticks. Transient menu/swap bodies disappear before they can ever
            // become writable; normal gameplay resumes a few frames later.
            static uintptr_t s_candidateHp[kMaxPartyPlayers]{};
            static int s_candidateCount = 0;
            static int s_stableTicks = 0;
            bool same = nPlayers == s_candidateCount;
            for (int i = 0; same && i < nPlayers; ++i)
                same = nextHp[i] == s_candidateHp[i];
            if (!same)
            {
                s_candidateCount = nPlayers;
                for (int i = 0; i < kMaxPartyPlayers; ++i)
                    s_candidateHp[i] = (i < nPlayers) ? nextHp[i] : 0;
                s_stableTicks = 1;
                ClearPlayerSets();
                return;
            }
            if (s_stableTicks < 3)
            {
                ++s_stableTicks;
                ClearPlayerSets();
                return;
            }

            for (int i = 0; i < nPlayers; ++i)
            {
                g_hpEntries[i].store(nextHp[i], std::memory_order_release);
                g_actors[i].store(nextActors[i], std::memory_order_release);
                g_targetOwners[i].store(nextTargets[i], std::memory_order_release);
            }
            for (int i = 0; i < nStam; ++i)
                g_stamEntries[i].store(nextStam[i], std::memory_order_release);
            for (int i = 0; i < nMountStam; ++i)
                g_mountStamEntries[i].store(nextMountStam[i], std::memory_order_release);
            for (int i = 0; i < nSpir; ++i)
                g_spiritEntries[i].store(nextSpir[i], std::memory_order_release);

            for (int i = 0; i < kMaxMounts; ++i)
                g_mountActors[i].store((i < nMounts) ? nextMounts[i] : 0, std::memory_order_release);
            g_mountCount.store(nMounts, std::memory_order_release);

            // Clear any trailing slots from a previous tick so a stale entry
            // pointer can never accidentally match after a transition/swap.
            for (int i = nPlayers; i < kMaxPlayers; ++i)
            {
                g_hpEntries[i].store(0, std::memory_order_release);
                g_actors[i].store(0, std::memory_order_release);
                g_targetOwners[i].store(0, std::memory_order_release);
            }
            for (int i = nStam; i < kMaxStatEntries; ++i) g_stamEntries[i].store(0, std::memory_order_release);
            for (int i = nMountStam; i < kMaxStatEntries; ++i) g_mountStamEntries[i].store(0, std::memory_order_release);
            for (int i = nSpir; i < kMaxStatEntries; ++i) g_spiritEntries[i].store(0, std::memory_order_release);

            const State& st = State::Get();

            // Log only when discovery changes, so the console shows whether
            // the player chain and gauge typing are healthy without frame spam.
            static int s_lastPlayers = -1, s_lastStam = -1, s_lastMountStam = -1, s_lastSpir = -1;
            if (nPlayers != s_lastPlayers || nStam != s_lastStam || nMountStam != s_lastMountStam || nSpir != s_lastSpir)
            {
                LOG("player: stat discovery - players=%d stamina=%d mountStamina=%d spirit=%d flags(stamina=%d mount=%d spirit=%d).",
                    nPlayers, nStam, nMountStam, nSpir, st.infStamina ? 1 : 0, st.infMountStamina ? 1 : 0, st.infSpirit ? 1 : 0);
                s_lastPlayers = nPlayers;
                s_lastStam = nStam;
                s_lastMountStam = nMountStam;
                s_lastSpir = nSpir;
            }

            // Easy Parry pulsing lives ONLY in Player::Tick() now - see there.
            // (Previously this function also pulsed on its own 120ms timer,
            // racing Tick()'s 80ms timer over the same s_lastParryPulse
            // timestamp; since Tick() calls this function immediately after
            // its own pulse, the 120ms branch here almost never actually won,
            // it just made the pulse cadence unpredictable and the code
            // confusing to reason about.)
        }

        // --- God Mode / Infinite Stamina / Infinite Mount Stamina / Infinite Spirit: guard the stat-
        // commit write ------------------------------------------------------
        int64_t __fastcall hkStatCommit(void* entry, int64_t time, int64_t target, uint16_t flag)
        {
            const uintptr_t e = reinterpret_cast<uintptr_t>(entry);
            const State& st = State::Get();

            bool isPlayerHp = InSet(g_hpEntries, kMaxPlayers, e);

            int32_t entryType = -1;
            StatEntryType(e, &entryType);

            // God Mode strictly locks player HP only when godMode toggle is ON
            const bool isGodModeHp = st.godMode && isPlayerHp;

            // Strictly lock only player/companion stamina - never touch enemy/shielded soldier stamina
            const bool isPlayerStam = st.infStamina && InSet(g_stamEntries, kMaxStatEntries, e);

            // Horse and mount stamina
            const bool isMountStam = st.infMountStamina && InSet(g_mountStamEntries, kMaxStatEntries, e);

            const bool isPlayerSpir = st.infSpirit && InSet(g_spiritEntries, kMaxStatEntries, e);

            const bool shouldLock = isGodModeHp ||
                                    isPlayerStam ||
                                    isMountStam ||
                                    isPlayerSpir;

            // Zero-Drop Pre-Commit Interception:
            // Overwrite target to 100% full capacity BEFORE the engine executes the write
            // so the engine never writes a reduced number or broadcasts a drop.
            if (shouldLock)
            {
                uint64_t base = 0, cap = 0;
                if (Read64(e + kOff_StatEntry_Base, &base))
                {
                    Read64(e + kOff_StatEntry_Cap, &cap);
                    const uint64_t full = (cap > base) ? cap : base;
                    if (full > 0)
                    {
                        target = static_cast<int64_t>(full);
                    }
                }
            }

            const int64_t result = oStatCommit(entry, time, target, flag);

            if (isGodModeHp) PinEntry(e);
            if (isPlayerStam) PinEntry(e);
            if (isMountStam) PinEntry(e);
            if (isPlayerSpir) PinEntry(e);

            return result;
        }

        bool IsPlayerEntity(uintptr_t target)
        {
            if (target < kMinPointer) return false;
            if (InSet(g_targetOwners, kMaxPlayers, target)) return true;
            if (InSet(g_actors, kMaxPlayers, target)) return true;
            if (InSet(g_hpEntries, kMaxPlayers, target)) return true;
            for (int i = 0; i < kMaxPartyPlayers; ++i)
            {
                const uintptr_t act = Player::GetActor(i);
                if (act && act == target) return true;
                const uintptr_t cAct = Inventory::CharacterAddr(i);
                if (cAct && cAct == target) return true;
            }
            return false;
        }

        // --- Damage multipliers: scale the hit at the apply dispatcher -----
        int64_t ScaleDamage(uintptr_t targetOwner, uintptr_t sourceCtx, int64_t delta)
        {
            const State& st = State::Get();

            // Victim is Player (Incoming Hit)
            if (IsPlayerEntity(targetOwner))
            {
                if (st.godMode) return 0;
                if (st.dmgInMult != 1.0f)
                {
                    const double scaled = static_cast<double>(delta) * static_cast<double>(st.dmgInMult);
                    if (scaled <= static_cast<double>(INT64_MIN)) return INT64_MIN;
                    if (scaled >= 0.0) return 0;
                    return static_cast<int64_t>(scaled);
                }
                return delta;
            }

            // Victim is Non-Player / Enemy (Outgoing Hit dealt by Player)
            bool isPlayerAttacker = false;
            if (sourceCtx >= kMinPointer)
            {
                if (IsPlayerEntity(sourceCtx))
                {
                    isPlayerAttacker = true;
                }
                else
                {
                    uint64_t actor = 0;
                    if (Read64(sourceCtx + kOff_Owner_Actor, &actor) && actor >= kMinPointer)
                    {
                        if (IsPlayerEntity(static_cast<uintptr_t>(actor)))
                            isPlayerAttacker = true;
                    }
                }
            }

            if (isPlayerAttacker)
            {
                float mult = st.oneHitKill ? 1000.0f : st.dmgOutMult;
                if (mult != 1.0f)
                {
                    const double scaled = static_cast<double>(delta) * static_cast<double>(mult);
                    if (scaled <= static_cast<double>(INT64_MIN)) return INT64_MIN;
                    if (scaled >= 0.0) return 0;
                    return static_cast<int64_t>(scaled);
                }
            }

            return delta;
        }

        int64_t __fastcall hkDamageApply(void* targetOwner, uint16_t statusId,
                                         int64_t time, int64_t delta, uintptr_t sourceCtx,
                                         char a6, char a7, char a8, char a9, char a10,
                                         void* out)
        {
            const State& st = State::Get();
            const uintptr_t owner = reinterpret_cast<uintptr_t>(targetOwner);
            const bool isPlayerTarget = IsPlayerEntity(owner);

            // Determine if damage source is an active hostile enemy vs environmental/fall impact
            bool isEnemyAttacker = false;
            if (sourceCtx >= kMinPointer && !IsPlayerEntity(sourceCtx))
            {
                uint64_t sourceActor = 0;
                if (Read64(sourceCtx + kOff_Owner_Actor, &sourceActor) && sourceActor >= kMinPointer)
                {
                    if (!IsPlayerEntity(static_cast<uintptr_t>(sourceActor)))
                        isEnemyAttacker = true;
                }
                else
                {
                    uintptr_t sub = 0;
                    if (ReadPtr(sourceCtx + kOff_Container_Sub, &sub) && sub >= kMinPointer)
                        isEnemyAttacker = true;
                }
            }

            if (st.easyParry && isPlayerTarget && isEnemyAttacker && IsPlayerHoldingGuard())
            {
                // Force Perfect Deflect / Parry: 0 damage, parry reaction flag (a6 = 2), attacker stagger (a7 = 1)
                delta = 0;
                a6 = 2;
                a7 = 1;
            }
            else if (delta < 0)
            {
                if (statusId == StatType_Health)
                {
                    if (st.godMode && isPlayerTarget)
                    {
                        delta = 0; // complete damage immunity
                    }
                    else if ((st.noFallDamage || Teleport::IsProtected() || Teleport::GetFlightEngaged()) &&
                             isPlayerTarget && !isEnemyAttacker)
                    {
                        // 100% Nullify ALL fall damage, cliff drops, gravity impacts & landing shock
                        delta = 0;
                    }
                    else
                    {
                        delta = ScaleDamage(owner, sourceCtx, delta);
                    }
                }
                else if (isPlayerTarget && IsStaminaType(statusId) && st.infStamina)
                {
                    delta = 0; // zero-out player stamina drain instantly
                }
                else if (st.infMountStamina && (statusId == StatType_MountSprint || statusId == 19) &&
                         (InSet(g_mountActors, kMaxMounts, owner) || InSet(g_mountStamEntries, kMaxStatEntries, owner)))
                {
                    delta = 0; // zero-out mount/wyvern/dragon stamina drain instantly
                }
                else if (isPlayerTarget && IsSpiritType(statusId) && st.infSpirit)
                {
                    delta = 0; // zero-out spirit drain instantly
                }
            }

            return oDamageApply(targetOwner, statusId, time, delta, sourceCtx,
                                a6, a7, a8, a9, a10, out);
        }

        // --- Just Core: Just Guard (Perfect Parry) & Just Evade (Perfect Dodge) ---
        using JustCore_t = bool(__fastcall*)(__int64 a1, float* a2, float a3, char a4, bool* a5);
        JustCore_t oJustCore = nullptr;
        void*      g_justCoreTarget = nullptr;

        bool __fastcall hkJustCore(__int64 a1, float* a2, float a3, char a4, bool* a5)
        {
            const bool orig = oJustCore ? oJustCore(a1, a2, a3, a4, a5) : false;
            const State& st = State::Get();

            // a4 != 0: Just Guard (Perfect Parry)
            // a4 == 0: Just Evade (Perfect Dodge)
            const bool isGuard = (a4 != 0);
            const bool isEvade = (a4 == 0);

            if ((isGuard && st.easyParry) || (isEvade && st.easyEvade))
            {
                if (a5) *a5 = true;
                return true;
            }

            return orig;
        }
    }

    bool Player::Install()
    {
        g_charMgrGlobal = ResolveCharMgrGlobal();
        if (!g_charMgrGlobal)
        {
            LOG_ERR("player: char-manager global NOT FOUND (no anchor matched) - God Mode / "
                    "Infinite Stamina / Infinite Spirit / damage multipliers disabled.");
        }

        mem::InstallHook("player: stat-commit", kSig_StatCommit,
                         "God Mode / Infinite Stamina / Infinite Spirit disabled",
                         &hkStatCommit, &oStatCommit, &g_commitTarget);

        mem::InstallHook("player: damage-apply", kSig_DamageApply, "damage multipliers disabled",
                         &hkDamageApply, &oDamageApply, &g_damageHookTarget);



        return true;
    }

    void Player::Tick()
    {
        TickResolveSelf();
    }

    void Player::RefreshSelf()
    {
        TickResolveSelf();
    }

    void Player::Remove()
    {
        mem::RemoveHook(&g_commitTarget);
        mem::RemoveHook(&g_damageHookTarget);
        mem::RemoveHook(&g_justCoreTarget);
        for (int i = 0; i < kMaxPlayers; ++i)
        {
            g_hpEntries[i].store(0);
            g_actors[i].store(0);
            g_targetOwners[i].store(0);
        }
        for (int i = 0; i < kMaxStatEntries; ++i)
        {
            g_stamEntries[i].store(0);
            g_spiritEntries[i].store(0);
        }
    }

    bool Player::Ready()
    {
        return g_hpEntries[0].load(std::memory_order_relaxed) >= kMinPointer &&
               g_actors[0].load(std::memory_order_relaxed) >= kMinPointer;
    }

    uintptr_t Player::GetActor(int index)
    {
        if (index < 0 || index >= kMaxPlayers) return 0;
        return g_actors[index].load(std::memory_order_acquire);
    }

    int Player::GetTrackedPlayerCount()
    {
        int count = 0;
        for (int i = 0; i < kMaxPlayers; ++i)
        {
            if (g_actors[i].load(std::memory_order_acquire) >= kMinPointer)
                ++count;
        }
        return count;
    }

    uintptr_t Player::GetMountActor(int index)
    {
        if (index < 0 || index >= kMaxMounts) return 0;
        return g_mountActors[index].load(std::memory_order_acquire);
    }

    int Player::GetTrackedMountCount()
    {
        return g_mountCount.load(std::memory_order_acquire);
    }
}