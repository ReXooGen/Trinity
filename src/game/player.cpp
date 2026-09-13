#include "player.h"

#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <cmath>
#include <algorithm>

#include "offsets.h"
#include "teleport.h"
#include "inventory.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../mem/hooks.h"
#include "../hooks/xinput_hook.h"
#include "../core/logger.h"
#include "../core/state.h"
#include "../core/version_detect.h"

namespace trinity::game
{
    using mem::ReadPtr;
    using mem::Read64;
    using mem::Read32;
    using mem::Read16;
    using mem::Read8;
    using mem::ReadVec3;
    using mem::Write64;
    using mem::Write32;
    using mem::Write16;
    using mem::Write8;

    namespace
    {
        // --- Fresh player-set resolution (character-manager global) --------
        // Crimson Desert features multiple playable protagonists (Kliff and companions).
        // The active character set is resolved dynamically from the engine's gameplay
        // character manager (kCharMgrAnchors) without caching stale pointers.
        uintptr_t g_charMgrGlobal = 0;

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
                {
                    if (vals[i] == g) { ++votes[i]; break; }
                }
                if (i == distinct)
                {
                    vals[distinct] = g;
                    votes[distinct] = 1;
                    ++distinct;
                }
            }

            if (distinct == 0) return 0;
            int best = 0;
            for (int i = 1; i < distinct; ++i)
            {
                if (votes[i] > votes[best]) best = i;
            }

            if (distinct > 1)
            {
                LOG_WARN("player: char-manager anchors disagree (%d distinct values); using %p with %d/%d votes.",
                         distinct, reinterpret_cast<void*>(vals[best]), votes[best], matched);
            }
            else
            {
                LOG_OK("player: char-manager successfully resolved (consensus: %d/%d anchors).", matched, kN);
            }

            return vals[best];
        }

        // Maximum tracked player slots and stat entries
        constexpr int kMaxPlayers          = 8;
        constexpr int kMaxPartyPlayers     = 3;
        constexpr int kMaxGaugePerType     = 4;
        constexpr int kMaxStatEntries      = kMaxPlayers * kMaxGaugePerType;
        constexpr int kMaxMounts           = 4;
        constexpr int kMaxMountStamEntries = 32;
        // kOff_Root_StatArray = 0x58 is defined in offsets.h

        std::atomic<uintptr_t> g_hpEntries[kMaxPlayers]{};
        std::atomic<uintptr_t> g_stamEntries[kMaxStatEntries]{};
        std::atomic<uintptr_t> g_spiritEntries[kMaxStatEntries]{};
        std::atomic<uintptr_t> g_mountStamEntries[kMaxMountStamEntries]{};

        std::atomic<uintptr_t> g_actors[kMaxPlayers]{};
        std::atomic<uintptr_t> g_owners[kMaxPlayers]{};
        std::atomic<uintptr_t> g_targetOwners[kMaxPlayers]{};

        std::atomic<uintptr_t> g_mountActors[kMaxMounts]{};
        std::atomic<uintptr_t> g_mountOwners[kMaxMounts]{};
        std::atomic<uintptr_t> g_mountTargetOwners[kMaxMounts]{};
        std::atomic<int>       g_mountCount{0};

        Player::MountDescriptor g_mountDescriptors[kMaxMounts]{};
        CRITICAL_SECTION       g_mountDescLock{};
        bool                   g_mountDescLockInit = false;

        std::atomic<uintptr_t> g_playerPossessor{0};
        std::atomic<int>       g_activeCharacterIdx{0};

        // --- Stat-entry typing helpers -------------------------------------
        bool StatEntryType(uintptr_t entry, int32_t* type)
        {
            uint32_t t = 0;
            if (!Read32(entry + kOff_StatEntry_Type, &t)) return false;
            *type = static_cast<int32_t>(t);
            return true;
        }

        inline bool PlausibleStatType(int32_t t) { return t >= 0 && t < 256; }
        inline bool IsHealthType(int32_t t)     { return t == StatType_Health; }

        inline bool IsPlayerStaminaType(int32_t t)
        {
            return t == StatType_SprintSt || t == StatType_StaminaPool117 || t == StatType_Stamina ||
                   t == 20 || t == 22 || t == 17;
        }

        inline bool IsMountStaminaType(int32_t t)
        {
            return t == StatType_MountSprint || t == StatType_SprintSt ||
                   t == StatType_StaminaPool117 || t == StatType_Stamina ||
                   t == 19 || t == 20 || t == 22 || t == 17;
        }

        inline bool IsSpiritType(int32_t t)
        {
            return t == StatType_SpiritPool || t == StatType_SpiritPool117 ||
                   t == StatType_Spirit || t == 18 || t == 21 || t == 23;
        }

        inline bool IsStaminaType(int32_t t)
        {
            return IsPlayerStaminaType(t) || IsMountStaminaType(t);
        }

        bool InSet(const std::atomic<uintptr_t>* set, int n, uintptr_t e)
        {
            if (e < kMinPointer) return false;
            for (int i = 0; i < n; ++i)
            {
                if (set[i].load(std::memory_order_relaxed) == e) return true;
            }
            return false;
        }

        void PinEntry(uintptr_t e)
        {
            if (e < kMinPointer) return;

            // CRITICAL: NEVER pin elemental accumulation gauges (48 = Heat/Burn, 49 = Cold/Frost).
            // Pinning 48 fills the heat meter to 100% (400,000) and makes the character catch fire!
            int32_t t = 0;
            if (StatEntryType(e, &t) && (t == StatType_HeatBurn || t == StatType_ColdFrost || t == 48 || t == 49))
                return;

            uint64_t base = 0, cap = 0, cur = 0;
            Read64(e + kOff_StatEntry_Base, &base);
            Read64(e + kOff_StatEntry_Cap, &cap);
            Read64(e + kOff_StatEntry_Current, &cur);
            if (base > 1000000000ULL || cap > 1000000000ULL) return;
            uint64_t full = (cap > base) ? cap : base;
            if (!full && cur > 0 && cur < 1000000000ULL) full = cur;
            if (!full) return;
            Write64(e + kOff_StatEntry_Current, full);
            Write64(e + kOff_StatEntry_Norm,    full - base);
        }

        // IsPlayerClass: checks type-descriptor tag (same gate the engine uses internally).
        // Tag 1 = SelfPlayer, Tag 9 = OtherPlayer. ((tag-1)&0xF7)==0 is the engine formula.
        bool IsPlayerClass(uintptr_t owner)
        {
            uint64_t td = 0;
            uint8_t tag = 0;
            return Read64(owner + kOff_Owner_TypeDesc, &td) && td >= kMinPointer &&
                   Read8(static_cast<uintptr_t>(td) + 1, &tag) && ((tag - 1) & 0xF7) == 0;
        }

        // PossessorRoundTrip: the ONLY reliable way to identify the truly controlled
        // player body at world load. The engine uses this exact check (sub_2393AA0):
        //   controlled body satisfies *(*(owner+0xA0)+0xD0) == owner
        // A random NPC with accidental tag data cannot satisfy a pointer round-trip.
        bool PossessorRoundTrip(uintptr_t owner)
        {
            if (owner < kMinPointer) return false;
            uint64_t poss = 0, back = 0;
            if (!Read64(owner + kOff_Owner_Possessor, &poss) || poss < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(poss) + kOff_Possessor_Pawn, &back)) return false;
            return static_cast<uintptr_t>(back) == owner;
        }

        // IsPlayerOrCompanion: tag-based check used for secondary validation only.
        // Tag 1 = SelfPlayer, Tag 9 = OtherPlayer, Tag 4 = Mercenary/Companion, Tag 59/0x3B = Party.
        bool IsPlayerOrCompanion(uintptr_t owner)
        {
            if (owner < kMinPointer) return false;
            uint64_t td = 0;
            uint8_t tag = 0;
            if (!Read64(owner + kOff_Owner_TypeDesc, &td) || td < kMinPointer) return false;
            if (!Read8(static_cast<uintptr_t>(td) + 1, &tag)) return false;
            return tag == 1 || tag == 9 || tag == 4 || tag == 59 || tag == 0x3B;
        }

        struct SelfChain
        {
            uintptr_t actor        = 0;
            uintptr_t targetOwner  = 0;
            uintptr_t statArray    = 0;
        };

        bool WalkSelfChain(uintptr_t owner, SelfChain* out)
        {
            uint64_t actor = 0, marker = 0, root = 0, arr = 0;
            if (!Read64(owner + kOff_Owner_Actor, &actor) || actor < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(actor) + kOff_Actor_StatusMarker, &marker) || marker < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(marker) + kOff_Marker_TargetOwner, &root) || root < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(root) + kOff_Root_StatArray, &arr) || arr < kMinPointer) return false;

            int32_t t = 0;
            if (!StatEntryType(static_cast<uintptr_t>(arr), &t) || !IsHealthType(t)) return false;

            out->actor       = static_cast<uintptr_t>(actor);
            out->targetOwner = static_cast<uintptr_t>(root);
            out->statArray   = static_cast<uintptr_t>(arr);
            return true;
        }

        bool WalkMountVitalChain(uintptr_t owner, uintptr_t* outStatArray, uintptr_t* outTargetOwner = nullptr, uintptr_t* outActor = nullptr)
        {
            uint64_t actor = 0, marker = 0, root = 0, arr = 0;
            if (!Read64(owner + kOff_Owner_Actor, &actor) || actor < kMinPointer)
                actor = owner;
            if (!Read64(static_cast<uintptr_t>(actor) + kOff_Actor_StatusMarker, &marker) || marker < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(marker) + kOff_Marker_TargetOwner, &root) || root < kMinPointer) return false;
            if (!Read64(static_cast<uintptr_t>(root) + kOff_Root_StatArray, &arr) || arr < kMinPointer) return false;

            if (outStatArray)   *outStatArray   = static_cast<uintptr_t>(arr);
            if (outTargetOwner) *outTargetOwner = static_cast<uintptr_t>(root);
            if (outActor)       *outActor       = static_cast<uintptr_t>(actor);
            return true;
        }

        // Resolves the actual character/actor entity pointer from a CharacterManager list slot.
        // In Crimson Desert (both TU 2.00 and TU 2.01 per Cheat Engine Table v5.0 and reference player.cpp),
        // data[i] is directly the 64-bit Actor/Owner entity pointer.
        uintptr_t ResolveEntityFromListSlot(uintptr_t slotVal)
        {
            if (slotVal < kMinPointer) return 0;
            return slotVal;
        }

        uintptr_t FindMountEquipComp(uintptr_t actor, char* outGearSummary = nullptr, size_t cap = 0, bool* outHasGear = nullptr)
        {
            if (actor < kMinPointer) return 0;
            if (outHasGear) *outHasGear = false;
            if (outGearSummary && cap > 0) outGearSummary[0] = '\0';

            uintptr_t fallbackComp = 0;

            auto inspectComp = [&](uintptr_t comp) -> bool {
                if (comp < kMinPointer) return false;

                static const char* const kExcludeWords[] = {
                    "Feed", "feed", "Food", "food", "Potion", "potion", "Meat", "Fruit",
                    "Skill", "skill", "Recipe", "Book", "Horn", "Material", "Sugar", "sugar",
                    "Hay", "hay", "Berry", "berry", "Juice", "juice", "Beet", "beet", "trade", "Trade",
                    "AbyssGear", "Item_Skill", "Riding_Deer_Horn",
                    "Bottle", "bottle", "Water", "water", "Arrow", "arrow", "Quiver", "quiver"
                };

                struct LayoutCandidate {
                    uintptr_t stride;
                    uintptr_t tagOffset;
                };
                const LayoutCandidate candidates[] = {
                    { 0xD0, 0xC8 }, // Primary: TU 2.01 / TU 2.02 / legacy (208-byte stride)
                    { 0xC8, 0xC0 }, // Secondary: 200-byte stride
                };
                const uintptr_t tableOffsets[] = { 0x90, 0x88, 0x80, 0x50, 0x78, 0x38, 0x40, 0x60, 0x70 };

                for (uintptr_t tOff : tableOffsets)
                {
                    uintptr_t tblDesc = 0;
                    if (!ReadPtr(comp + tOff, &tblDesc) || tblDesc < kMinPointer) continue;
                    uintptr_t array = 0;
                    uint32_t count = 0;
                    if (!ReadPtr(tblDesc + kOff_EquipTable_Array, &array) || array < kMinPointer) continue;
                    if (!Read32(tblDesc + kOff_EquipTable_Count, &count) || count == 0 || count > 64) continue;

                    if (!fallbackComp) fallbackComp = comp;

                    for (const auto& cand : candidates)
                    {
                        bool foundGear = false;
                        char gearList[256] = {};

                        for (uint32_t i = 0; i < count; ++i)
                        {
                            const uintptr_t entry = array + static_cast<uintptr_t>(i) * cand.stride;
                            uint16_t tid = 0;
                            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) continue;
                            uint16_t tag = 0;
                            Read16(entry + cand.tagOffset, &tag);

                            char itemName[64] = {};
                            Inventory::NameForTypeId(tid, itemName, sizeof(itemName));

                            bool isExcluded = false;
                            for (const char* exc : kExcludeWords)
                            {
                                if (strstr(itemName, exc)) { isExcluded = true; break; }
                            }
                            if (isExcluded) continue;

                            bool isGear = (tag <= 4 || tag == 14 || (tag >= 22 && tag <= 25));
                            if (!isGear && itemName[0])
                            {
                                if (strstr(itemName, "Barding") || strstr(itemName, "Saddle") ||
                                    strstr(itemName, "Chamfron") || strstr(itemName, "Champron") ||
                                    strstr(itemName, "Stirrup") || strstr(itemName, "Horseshoe") ||
                                    strstr(itemName, "Horse Armor") || strstr(itemName, "HorseArmor") ||
                                    strstr(itemName, "Zirah Kuda") || strstr(itemName, "Pelana") ||
                                    strstr(itemName, "Exclaire"))
                                    isGear = true;
                            }

                            if (isGear)
                            {
                                foundGear = true;
                                if (!itemName[0])
                                    snprintf(itemName, sizeof(itemName), "Gear #%u", tid);
                                if (gearList[0])
                                    strncat_s(gearList, sizeof(gearList), ", ", _TRUNCATE);
                                strncat_s(gearList, sizeof(gearList), itemName, _TRUNCATE);
                            }
                        }

                        if (foundGear)
                        {
                            if (outHasGear) *outHasGear = true;
                            if (outGearSummary && cap > 0 && gearList[0])
                                strncpy_s(outGearSummary, cap, gearList, cap - 1);
                            return true;
                        }
                    }
                }

                // Check direct array offsets (0x58, 0x60)
                const uintptr_t directOffsArr[] = { 0x58, 0x60 };
                for (uintptr_t dOff : directOffsArr)
                {
                    uintptr_t array = 0;
                    uint32_t count = 0;
                    if (ReadPtr(comp + dOff, &array) && Read32(comp + dOff + 8, &count) &&
                        array >= kMinPointer && count > 0 && count <= 64)
                    {
                        if (!fallbackComp) fallbackComp = comp;
                        for (const auto& cand : candidates)
                        {
                            bool foundGear = false;
                            char gearList[256] = {};
                            for (uint32_t i = 0; i < count; ++i)
                            {
                                const uintptr_t entry = array + static_cast<uintptr_t>(i) * cand.stride;
                                uint16_t tid = 0;
                                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) continue;
                                uint16_t tag = 0;
                                Read16(entry + cand.tagOffset, &tag);

                                char itemName[64] = {};
                                Inventory::NameForTypeId(tid, itemName, sizeof(itemName));

                                bool isExcluded = false;
                                for (const char* exc : kExcludeWords)
                                {
                                    if (strstr(itemName, exc)) { isExcluded = true; break; }
                                }
                                if (isExcluded) continue;

                                bool isGear = (tag <= 4 || tag == 14 || (tag >= 22 && tag <= 25));
                                if (!isGear && itemName[0])
                                {
                                    if (strstr(itemName, "Barding") || strstr(itemName, "Saddle") ||
                                        strstr(itemName, "Chamfron") || strstr(itemName, "Champron") ||
                                        strstr(itemName, "Stirrup") || strstr(itemName, "Horseshoe") ||
                                        strstr(itemName, "Horse Armor") || strstr(itemName, "HorseArmor") ||
                                        strstr(itemName, "Zirah Kuda") || strstr(itemName, "Pelana") ||
                                        strstr(itemName, "Exclaire"))
                                        isGear = true;
                                }

                                if (isGear)
                                {
                                    foundGear = true;
                                    if (!itemName[0])
                                        snprintf(itemName, sizeof(itemName), "Gear #%u", tid);
                                    if (gearList[0])
                                        strncat_s(gearList, sizeof(gearList), ", ", _TRUNCATE);
                                    strncat_s(gearList, sizeof(gearList), itemName, _TRUNCATE);
                                }
                            }
                            if (foundGear)
                            {
                                if (outHasGear) *outHasGear = true;
                                if (outGearSummary && cap > 0 && gearList[0])
                                    strncpy_s(outGearSummary, cap, gearList, cap - 1);
                                return true;
                            }
                        }
                    }
                }

                return false;
            };

            // 1. Direct component on actor + 0x38
            uintptr_t comp = 0;
            if (ReadPtr(actor + 0x38, &comp) && comp >= kMinPointer)
            {
                if (inspectComp(comp)) return comp;
            }

            // 2. Container walk (*(*(actor + 0x68) + 0x38))
            uintptr_t sub = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && sub >= kMinPointer)
            {
                if (ReadPtr(sub + 0x38, &comp) && comp >= kMinPointer && inspectComp(comp))
                    return comp;
                if (ReadPtr(sub + 0x80, &comp) && comp >= kMinPointer && inspectComp(comp))
                    return comp;
                if (ReadPtr(sub + 0x90, &comp) && comp >= kMinPointer && inspectComp(comp))
                    return comp;
                if (ReadPtr(sub + 0x168, &comp) && comp >= kMinPointer && inspectComp(comp))
                    return comp;
                for (uintptr_t o = 0; o < 0x200; o += 8)
                {
                    uintptr_t p = 0;
                    if (ReadPtr(sub + o, &p) && p >= kMinPointer)
                    {
                        uintptr_t back = 0;
                        if (ReadPtr(p + 0x08, &back))
                        {
                            if (back == actor)
                            {
                                if (inspectComp(p)) return p;
                            }
                            uintptr_t backAct = 0;
                            if (ReadPtr(back + kOff_Owner_Actor, &backAct) && backAct == actor)
                            {
                                if (inspectComp(p)) return p;
                            }
                        }
                    }
                }
            }

            // 3. Alternate sub-container offsets
            const uintptr_t subOffsets[] = { 0x60, 0x68, 0x70, 0x58, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0 };
            const uintptr_t compOffsets[] = { 0x38, 0x30, 0x40, 0x28, 0x48, 0x50, 0x58, 0x60, 0x68, 0x80, 0x88, 0x90, 0x160, 0x168 };
            for (uintptr_t sOff : subOffsets)
            {
                if (ReadPtr(actor + sOff, &sub) && sub >= kMinPointer)
                {
                    for (uintptr_t cOff : compOffsets)
                    {
                        if (ReadPtr(sub + cOff, &comp) && comp >= kMinPointer && inspectComp(comp))
                            return comp;
                    }
                }
            }

            // 4. Alternate direct component offsets
            const uintptr_t directOffs[] = { 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x168 };
            for (uintptr_t d : directOffs)
            {
                if (ReadPtr(actor + d, &comp) && comp >= kMinPointer)
                {
                    if (inspectComp(comp)) return comp;
                }
            }

            if (fallbackComp)
                return fallbackComp;

            return 0;
        }

        void ClearPlayerSets()
        {
            for (int i = 0; i < kMaxPlayers; ++i)
            {
                g_hpEntries[i].store(0, std::memory_order_release);
                g_actors[i].store(0, std::memory_order_release);
                g_owners[i].store(0, std::memory_order_release);
                g_targetOwners[i].store(0, std::memory_order_release);
            }
            for (int i = 0; i < kMaxMounts; ++i)
            {
                g_mountActors[i].store(0, std::memory_order_release);
                g_mountTargetOwners[i].store(0, std::memory_order_release);
                g_mountOwners[i].store(0, std::memory_order_release);
            }
            if (g_mountDescLockInit)
            {
                EnterCriticalSection(&g_mountDescLock);
                for (int i = 0; i < kMaxMounts; ++i) g_mountDescriptors[i] = Player::MountDescriptor{};
                LeaveCriticalSection(&g_mountDescLock);
            }
            g_mountCount.store(0, std::memory_order_release);
            for (int i = 0; i < kMaxStatEntries; ++i)
            {
                g_stamEntries[i].store(0, std::memory_order_release);
                g_spiritEntries[i].store(0, std::memory_order_release);
            }
            for (int i = 0; i < kMaxMountStamEntries; ++i)
            {
                g_mountStamEntries[i].store(0, std::memory_order_release);
            }
            g_activeCharacterIdx.store(0, std::memory_order_release);
        }

        bool AnyStatFeatureActive(const State& st)
        {
            if (Teleport::IsProtected() || Teleport::GetFlightEngaged()) return true;
            return st.godMode || st.infStamina || st.infMountStamina || st.infSpirit ||
                   st.oneHitKill || st.noFallDamage || st.easyParry || st.easyEvade ||
                   st.dmgInMult != 1.0f || st.dmgOutMult != 1.0f;
        }

        // --- Input Helper predicates for combat triggers -------------------
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
                if (hooks::XInputReadReal(i, &xs) == ERROR_SUCCESS)
                {
                    if ((xs.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 ||
                        xs.Gamepad.bLeftTrigger > 30)
                        return true;
                }
            }
            return false;
        }

        static bool IsPlayerHoldingEvade()
        {
            if ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
                (GetAsyncKeyState('C') & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_MENU) & 0x8000) != 0)
                return true;

            XINPUT_STATE xs{};
            for (DWORD i = 0; i < 4; ++i)
            {
                if (hooks::XInputReadReal(i, &xs) == ERROR_SUCCESS)
                {
                    if ((xs.Gamepad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B)) != 0 ||
                        xs.Gamepad.bRightTrigger > 30)
                        return true;
                }
            }
            return false;
        }

        // --- Just-window assist (Easy Parry / Easy Evade) --------------------
        // The engine decides "was that guard/dodge timed perfectly?" in one
        // evaluator (kSig_JustWindowEval, TU 2.02 @ 0x1407FCC80). Its success
        // return is what spawns the REAL perfect-parry/evade: enemy stagger,
        // the counter window, the deflect animation. Nullifying damage in
        // hkDamageApply (the old approach) fires AFTER that decision, so the
        // counter never opened. Hooking the evaluator and forcing the result
        // gives the genuine promotion. NPCs can never trigger it: the gates
        // below require OUR keyboard/gamepad timestamps.
        using JustWindowEval_t = uint8_t (__fastcall*)(void* mgr, void* buf, float aux,
                                                       uint8_t isGuard, bool* outWindows);
        JustWindowEval_t oJustWindowEval   = nullptr;
        void*            g_justEvalTarget  = nullptr;
        std::atomic<ULONGLONG> g_lastGuardInputMs{ 0 };
        std::atomic<ULONGLONG> g_lastEvadeInputMs{ 0 };
        static constexpr ULONGLONG kJustInputGraceMs = 700;

        void PollCombatInputTaps()
        {
            const ULONGLONG now = GetTickCount64();
            if (IsPlayerHoldingGuard())
                g_lastGuardInputMs.store(now, std::memory_order_release);
            if (IsPlayerHoldingEvade())
                g_lastEvadeInputMs.store(now, std::memory_order_release);
        }

        uint8_t __fastcall hkJustWindowEval(void* mgr, void* buf, float aux,
                                            uint8_t isGuard, bool* outWindows)
        {
            const uint8_t result = oJustWindowEval(mgr, buf, aux, isGuard, outWindows);
            if (result) return result;

            const State& st = State::Get();
            const ULONGLONG now = GetTickCount64();
            if (isGuard)
            {
                if (st.easyParry &&
                    now - g_lastGuardInputMs.load(std::memory_order_acquire) <= kJustInputGraceMs)
                {
                    if (outWindows) *outWindows = true;
                    return 1;
                }
            }
            else if (st.easyEvade &&
                     now - g_lastEvadeInputMs.load(std::memory_order_acquire) <= kJustInputGraceMs)
            {
                if (outWindows) *outWindows = true;
                return 1;
            }
            return result;
        }

        void TickResolveSelf()
        {
            const State& st = State::Get();
            if (!g_charMgrGlobal)
            {
                ClearPlayerSets();
                return;
            }

            uint64_t p = 0, mgr = 0, data = 0;
            if (!Read64(g_charMgrGlobal, &p) || p < kMinPointer) { ClearPlayerSets(); return; }
            if (!Read64(static_cast<uintptr_t>(p), &mgr) || mgr < kMinPointer) { ClearPlayerSets(); return; }

            uint32_t count = 0;
            // Layout: data at +0xB8, count at +0xC0 (authoritative on TU 2.00 & TU 2.01 per Cheat Engine Table v5.0)
            if (Read64(static_cast<uintptr_t>(mgr) + kOff_CharMgr_ListData, &data) && data >= kMinPointer &&
                Read32(static_cast<uintptr_t>(mgr) + kOff_CharMgr_ListCount, &count) && count > 0 && count <= kCharList_MaxCount)
            {
                // List layout verified
            }
            else
            {
                ClearPlayerSets();
                return;
            }

            uint64_t anchorVt = 0;
            uintptr_t mainPlayerOwner = 0;
            uint64_t playerPoss = 0;

            // PASS 1: Find the REAL controlled player using the POSSESSOR ROUND-TRIP.
            // This is what the engine itself does (sub_2393AA0) and is immune to
            // uninitialized or accidental tag matches during world load.
            // *(*(owner+0xA0)+0xD0) == owner  → true ONLY for the real controlled body.
            for (uint32_t i = 0; i < count; ++i)
            {
                uint64_t ch = 0;
                if (!Read64(static_cast<uintptr_t>(data) + 8ull * i, &ch) || ch < kMinPointer) continue;
                const uintptr_t cand = ResolveEntityFromListSlot(static_cast<uintptr_t>(ch));
                if (cand < kMinPointer) continue;

                // Must pass BOTH the tag check AND the possessor round-trip.
                if (!IsPlayerClass(cand) && !IsPlayerOrCompanion(cand)) continue;
                if (!PossessorRoundTrip(cand)) continue;

                // Found the real player body. Extract its VTable as the anchor.
                if (!Read64(cand, &anchorVt) || anchorVt < kMinPointer) continue;
                Read64(cand + kOff_Owner_Possessor, &playerPoss);
                mainPlayerOwner = cand;
                break;
            }

            if (!anchorVt || !mainPlayerOwner || playerPoss < kMinPointer)
            {
                // Player not yet confirmed. Do NOT track any entities.
                // This prevents NPCs with stale data from being mistaken for players.
                ClearPlayerSets();
                return;
            }
            g_playerPossessor.store(static_cast<uintptr_t>(playerPoss), std::memory_order_release);
            int nPlayers = 1, nStam = 0, nMountStam = 0, nSpir = 0, nMounts = 0;
            bool slotAssigned[kMaxPartyPlayers] = { false, false, false };

            // PASS 2: Track confirmed active player in slot 0.
            SelfChain mainC;
            if (!WalkSelfChain(mainPlayerOwner, &mainC))
            {
                ClearPlayerSets();
                return;
            }

            // Slot 0 is ALWAYS the active/controlled player
            g_hpEntries[0].store(mainC.statArray, std::memory_order_release);
            g_actors[0].store(mainC.actor, std::memory_order_release);
            g_targetOwners[0].store(mainC.targetOwner, std::memory_order_release);
            g_owners[0].store(mainPlayerOwner, std::memory_order_release);

            // Determine active character identity: PartyIndex leads (1=Kliff, 2=Damiane, 3=Oongka)
            uint32_t activeParty = 0;
            int activeIdx = 0;
            if (Read32(mainPlayerOwner + kOff_Owner_PartyIndex, &activeParty) && activeParty >= 1 && activeParty <= 3)
            {
                activeIdx = static_cast<int>(activeParty - 1);
            }
            else
            {
                const int id = Inventory::IdentifyCharacterFromEquip(mainPlayerOwner);
                if (id >= 0 && id < kMaxPartyPlayers)
                    activeIdx = id;
            }
            g_activeCharacterIdx.store(activeIdx, std::memory_order_release);

            // Map controlled body into its specific character slot (0=Kliff, 1=Damiane, 2=Oongka)
            if (activeIdx >= 0 && activeIdx < kMaxPartyPlayers)
            {
                g_hpEntries[activeIdx].store(mainC.statArray, std::memory_order_release);
                g_actors[activeIdx].store(mainC.actor, std::memory_order_release);
                g_targetOwners[activeIdx].store(mainC.targetOwner, std::memory_order_release);
                g_owners[activeIdx].store(mainPlayerOwner, std::memory_order_release);
                slotAssigned[activeIdx] = true;
            }

            for (int k = 1; k < kStatArray_ScanEntries; ++k)
            {
                const uintptr_t e = mainC.statArray + k * kSizeof_StatEntry;
                int32_t stt = 0;
                if (!StatEntryType(e, &stt)) break;
                if (!PlausibleStatType(stt)) break;
                if (IsPlayerStaminaType(stt))
                {
                    if (nStam < kMaxStatEntries)
                        g_stamEntries[nStam++].store(e, std::memory_order_release);
                }
                else if (IsSpiritType(stt))
                {
                    if (nSpir < kMaxStatEntries)
                        g_spiritEntries[nSpir++].store(e, std::memory_order_release);
                }
                else if (stt == StatType_HeatBurn || stt == StatType_ColdFrost || stt == 48 || stt == 49)
                {
                    if (st.godMode)
                    {
                        // Cleanse heat (fire/burn) and cold (frost) accumulation on player when God Mode is ON
                        uint64_t cur = 0;
                        if (Read64(e + kOff_StatEntry_Current, &cur) && cur > 0)
                        {
                            Write64(e + kOff_StatEntry_Current, 0);
                            Write64(e + kOff_StatEntry_Norm, 0);
                        }
                    }
                }
            }

            // PASS 3: Discover party companions (Kliff = 0, Damiane = 1, Oongka = 2) from remaining entities in character manager
            if (!slotAssigned[0] || !slotAssigned[1] || !slotAssigned[2])
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (slotAssigned[0] && slotAssigned[1] && slotAssigned[2]) break;

                    uint64_t ch = 0;
                    if (!Read64(static_cast<uintptr_t>(data) + 8ull * i, &ch) || ch < kMinPointer) continue;
                    const uintptr_t owner = ResolveEntityFromListSlot(static_cast<uintptr_t>(ch));
                    if (owner < kMinPointer || owner == mainPlayerOwner) continue;

                    // FAST REJECTION: Only check entities that could plausibly be player or companion characters.
                    uint64_t candTd = 0;
                    uint8_t tagByte = 0;
                    if (!Read64(owner + kOff_Owner_TypeDesc, &candTd) || candTd < kMinPointer) continue;
                    if (!Read8(static_cast<uintptr_t>(candTd) + 1, &tagByte)) continue;
                    const bool isPlayerOrCompTag = (tagByte == 1 || tagByte == 9 || tagByte == 4 || tagByte == 59 || tagByte == 0x3B);

                    uint64_t vt = 0;
                    Read64(owner, &vt);
                    const bool isHumanoid = (anchorVt != 0 && vt == anchorVt);

                    uint64_t candPoss = 0;
                    Read64(owner + kOff_Owner_Possessor, &candPoss);
                    const bool sharesPlayerPoss = (playerPoss >= kMinPointer && candPoss == playerPoss);

                    if (!isPlayerOrCompTag && !isHumanoid && !sharesPlayerPoss)
                        continue;

                    // Direct party index mapping: 1 = Kliff (0), 2 = Damiane (1), 3 = Oongka (2)
                    uint32_t partyIdx = 0;
                    Read32(owner + kOff_Owner_PartyIndex, &partyIdx);

                    int compIdx = -1;
                    if (partyIdx >= 1 && partyIdx <= 3)
                    {
                        compIdx = static_cast<int>(partyIdx - 1);
                    }
                    else
                    {
                        compIdx = Inventory::IdentifyCharacterFromEquip(owner);
                        if (compIdx < 0)
                        {
                            uint64_t act = 0;
                            if (Read64(owner + kOff_Owner_Actor, &act) && act >= kMinPointer)
                                compIdx = Inventory::IdentifyCharacterFromEquip(static_cast<uintptr_t>(act));
                        }
                    }

                    // Must be a valid companion slot (0=Kliff, 1=Damiane, 2=Oongka)
                    if (compIdx < 0 || compIdx >= kMaxPartyPlayers || slotAssigned[compIdx]) continue;

                    uint64_t actor = 0;
                    Read64(owner + kOff_Owner_Actor, &actor);
                    const uintptr_t compActor = (actor >= kMinPointer) ? static_cast<uintptr_t>(actor) : owner;

                    g_actors[compIdx].store(compActor, std::memory_order_release);
                    g_owners[compIdx].store(owner, std::memory_order_release);
                    slotAssigned[compIdx] = true;

                    // WalkSelfChain finds vitals/HP for genuine humanoid characters
                    SelfChain compC;
                    uintptr_t statArr = 0, targetOwn = 0;
                    if (WalkSelfChain(owner, &compC))
                    {
                        statArr = compC.statArray;
                        targetOwn = compC.targetOwner;
                    }

                    if (statArr >= kMinPointer)
                    {
                        g_hpEntries[compIdx].store(statArr, std::memory_order_release);
                        g_targetOwners[compIdx].store(targetOwn, std::memory_order_release);

                        for (int k = 1; k < kStatArray_ScanEntries; ++k)
                        {
                            const uintptr_t e = statArr + k * kSizeof_StatEntry;
                            int32_t stt = 0;
                            if (!StatEntryType(e, &stt)) break;
                            if (!PlausibleStatType(stt)) break;
                            if (IsPlayerStaminaType(stt))
                            {
                                if (nStam < kMaxStatEntries)
                                    g_stamEntries[nStam++].store(e, std::memory_order_release);
                            }
                            else if (IsSpiritType(stt))
                            {
                                if (nSpir < kMaxStatEntries)
                                    g_spiritEntries[nSpir++].store(e, std::memory_order_release);
                            }
                            else if (stt == StatType_HeatBurn || stt == StatType_ColdFrost || stt == 48 || stt == 49)
                            {
                                if (st.godMode)
                                {
                                    uint64_t cur = 0;
                                    if (Read64(e + kOff_StatEntry_Current, &cur) && cur > 0)
                                    {
                                        {
                                            Write64(e + kOff_StatEntry_Current, 0);
                                            Write64(e + kOff_StatEntry_Norm, 0);
                                        }
                                    }
                                }
                            }
                        }

                        if (slotAssigned[1] && slotAssigned[2]) break;
                    }
                }
            }

            // Mount discovery (vehicles and genuine mounts)
            struct CandidateMount
            {
                uintptr_t actor = 0;
                uintptr_t owner = 0;
                uintptr_t targetOwner = 0;
                uintptr_t statArray = 0;
                uintptr_t equipComp = 0;
                float     distance = 9999.0f;
                bool      isRidden = false;
                bool      isPlayerOwned = false;
                bool      hasMountSprint = false;
                bool      hasHorseGear = false;
                char      gearSummary[64] = {};
            };
            CandidateMount candMounts[32] = {};
            int candMountCount = 0;

            uint64_t activePawn = 0;
            if (playerPoss >= kMinPointer)
            {
                Read64(static_cast<uintptr_t>(playerPoss) + kOff_Possessor_Pawn, &activePawn);
            }

            float px = 0.0f, py = 0.0f, pz = 0.0f;
            Teleport::GetLastPosition(&px, &py, &pz);

            for (uint32_t i = 0; i < count && candMountCount < 32; ++i)
            {
                uint64_t ch = 0;
                if (!Read64(static_cast<uintptr_t>(data) + 8ull * i, &ch) || ch < kMinPointer) continue;
                const uintptr_t owner = static_cast<uintptr_t>(ch);
                if (owner < kMinPointer || owner == mainPlayerOwner) continue;
                bool isPartyOwner = false;
                for (int p = 0; p < kMaxPartyPlayers; ++p)
                {
                    if (owner == g_owners[p].load(std::memory_order_relaxed))
                    {
                        isPartyOwner = true;
                        break;
                    }
                }
                if (isPartyOwner) continue;

                uint32_t objType = 0;
                Read32(owner + kOff_Owner_ObjectType, &objType);
                const uint32_t maskedObj = objType & 0xFF;

                uint32_t obj50 = 0;
                Read32(owner + kOff_Owner_PartyIndex, &obj50);

                uint32_t obj60 = 0;
                Read32(owner + 0x60, &obj60);

                uint64_t td = 0;
                uint8_t tagByte = 0;
                if (Read64(owner + kOff_Owner_TypeDesc, &td) && td >= kMinPointer)
                    Read8(td + 1, &tagByte);

                // Strictly exclude human players & party companions (Kliff, Damiane, Oongka, etc.)
                if (tagByte == 1 || tagByte == 4 || tagByte == 9 || tagByte == 59 || tagByte == 0x3B) continue;
                if (IsPlayerClass(owner) || IsPlayerOrCompanion(owner)) continue;

                uint64_t actor = 0;
                Read64(owner + kOff_Owner_Actor, &actor);
                const uintptr_t directMountAct = (actor >= kMinPointer) ? static_cast<uintptr_t>(actor) : owner;

                uint64_t mountPoss = 0;
                Read64(owner + kOff_Owner_Possessor, &mountPoss);
                bool sharesPartyPoss = (playerPoss >= kMinPointer && mountPoss == playerPoss);
                if (!sharesPartyPoss && mountPoss >= kMinPointer)
                {
                    for (int p = 0; p < kMaxPartyPlayers; ++p)
                    {
                        const uintptr_t pOwn = g_owners[p].load(std::memory_order_relaxed);
                        if (!pOwn) continue;
                        uint64_t pPoss = 0;
                        if (Read64(pOwn + kOff_Owner_Possessor, &pPoss) && pPoss == mountPoss)
                        {
                            sharesPartyPoss = true;
                            break;
                        }
                    }
                }
                const bool isRidden = (activePawn != 0 && (activePawn == static_cast<uint64_t>(owner) ||
                                                          activePawn == static_cast<uint64_t>(directMountAct) ||
                                                          activePawn == actor));

                // Mount Candidate Filter: vehicles, mounts, pets, ridden pawns, and vehicle types (TU 2.00/2.01 consensus)
                const bool isMountCandidate = (maskedObj == Obj_Vehicle || maskedObj == Obj_Pet ||
                                               tagByte == 5 || tagByte == 6 || obj50 == 5 ||
                                               sharesPartyPoss || isRidden ||
                                               (maskedObj > 0 && maskedObj < 16 && maskedObj != Obj_SelfPlayer && maskedObj != Obj_OtherPlayer));
                if (!isMountCandidate) continue;

                uintptr_t mountStatArray = 0, mountTarget = 0, mountAct = 0;
                bool hasVitals = WalkMountVitalChain(owner, &mountStatArray, &mountTarget, &mountAct) && mountStatArray >= kMinPointer;
                if (!hasVitals)
                {
                    SelfChain sc{};
                    if (WalkSelfChain(owner, &sc) && sc.statArray >= kMinPointer)
                    {
                        mountStatArray = sc.statArray;
                        mountTarget    = sc.targetOwner;
                        mountAct       = sc.actor;
                        hasVitals      = true;
                    }
                }
                const uintptr_t effActor = mountAct ? mountAct : directMountAct;

                // Check Mount Sprint Stamina (StatType_MountSprint = 19)
                bool hasMountSprint = false;
                if (hasVitals)
                {
                    for (int k = 0; k < kStatArray_ScanEntries; ++k)
                    {
                        const uintptr_t e = mountStatArray + k * kSizeof_StatEntry;
                        int32_t stt = 0;
                        if (!StatEntryType(e, &stt) || !PlausibleStatType(stt)) break;
                        if (stt == StatType_MountSprint || stt == 19)
                        {
                            hasMountSprint = true;
                            break;
                        }
                    }
                }

                // Check Horse Gear
                bool hasHorseGear = false;
                char gearSummary[64] = {};
                uintptr_t eqComp = FindMountEquipComp(effActor, gearSummary, sizeof(gearSummary), &hasHorseGear);
                if (!eqComp && effActor != owner)
                    eqComp = FindMountEquipComp(owner, gearSummary, sizeof(gearSummary), &hasHorseGear);

                // Direct actor / owner + 0x38 equip component fallback
                if (!eqComp)
                {
                    uintptr_t c = 0;
                    if (ReadPtr(effActor + 0x38, &c) && c >= kMinPointer)
                        eqComp = c;
                    else if (ReadPtr(owner + 0x38, &c) && c >= kMinPointer)
                        eqComp = c;
                }

                const bool isPlayerOwnedMount = (obj50 == 5 || (tagByte == 6 && obj50 > 0) || sharesPartyPoss || isRidden || hasHorseGear);

                // STRICT FILTER: eliminate ambient birds/pigeons, dogs, cats, squirrels, etc.
                // NEVER eliminate player-owned mount or mount with horse gear!
                if (!isRidden && !hasMountSprint && !hasHorseGear && !isPlayerOwnedMount)
                    continue;

                // Calculate distance to player (protected against NaN / Inf to prevent CRT sort failure)
                float dist = 9999.0f;
                if (isPlayerOwnedMount || isRidden || hasHorseGear)
                {
                    dist = 0.0f;
                }
                else
                {
                    float mPos[3] = {};
                    if (ReadVec3(owner + kOff_MoveOwner_Position, mPos) ||
                        (effActor && ReadVec3(effActor + kOff_MoveOwner_Position, mPos)))
                    {
                        float dx = mPos[0] - px, dy = mPos[1] - py, dz = mPos[2] - pz;
                        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (!std::isnan(d) && !std::isinf(d))
                            dist = d;
                    }
                }

                CandidateMount& cm = candMounts[candMountCount++];
                cm.actor = effActor;
                cm.owner = owner;
                cm.targetOwner = mountTarget ? mountTarget : owner;
                cm.statArray = mountStatArray;
                cm.equipComp = eqComp;
                cm.distance = dist;
                cm.isRidden = isRidden;
                cm.isPlayerOwned = isPlayerOwnedMount;
                cm.hasMountSprint = hasMountSprint;
                cm.hasHorseGear = hasHorseGear;
                strncpy_s(cm.gearSummary, sizeof(cm.gearSummary), gearSummary, sizeof(cm.gearSummary) - 1);
            }

            // Sort candidate mounts:
            // 1. Player owned mount or mount with gear comes first
            // 2. Ridden mount comes next
            // 3. Mount with horse gear comes before mount without gear
            // 4. Closest mount comes next (with strict weak ordering check against NaN)
            std::sort(candMounts, candMounts + candMountCount, [](const CandidateMount& a, const CandidateMount& b) {
                const bool aOwned = a.isPlayerOwned || a.hasHorseGear;
                const bool bOwned = b.isPlayerOwned || b.hasHorseGear;
                if (aOwned != bOwned) return aOwned > bOwned;
                if (a.isRidden != b.isRidden) return a.isRidden > b.isRidden;
                if (a.hasHorseGear != b.hasHorseGear) return a.hasHorseGear > b.hasHorseGear;
                float da = (std::isnan(a.distance) || std::isinf(a.distance)) ? 9999.0f : a.distance;
                float db = (std::isnan(b.distance) || std::isinf(b.distance)) ? 9999.0f : b.distance;
                return da < db;
            });

            if (g_mountDescLockInit)
                EnterCriticalSection(&g_mountDescLock);

            for (int m = 0; m < candMountCount && nMounts < kMaxMounts; ++m)
            {
                const CandidateMount& cand = candMounts[m];
                g_mountActors[nMounts].store(cand.actor, std::memory_order_release);
                g_mountTargetOwners[nMounts].store(cand.targetOwner, std::memory_order_release);
                g_mountOwners[nMounts].store(cand.owner, std::memory_order_release);

                Player::MountDescriptor& desc = g_mountDescriptors[nMounts];
                desc.actor = cand.actor;
                desc.owner = cand.owner;
                desc.equipComp = cand.equipComp;
                desc.distance = cand.distance;
                desc.isRidden = cand.isRidden;
                desc.hasHorseGear = cand.hasHorseGear;
                strncpy_s(desc.gearSummary, sizeof(desc.gearSummary), cand.gearSummary, sizeof(desc.gearSummary) - 1);

                if (cand.isRidden)
                {
                    if (cand.gearSummary[0])
                        snprintf(desc.label, sizeof(desc.label), "[Riding] Horse (%s)", cand.gearSummary);
                    else
                        snprintf(desc.label, sizeof(desc.label), "[Riding] Horse");
                }
                else if (cand.isPlayerOwned)
                {
                    if (cand.gearSummary[0])
                        snprintf(desc.label, sizeof(desc.label), "[Active] Horse (%s)", cand.gearSummary);
                    else
                        snprintf(desc.label, sizeof(desc.label), "[Active] Horse");
                }
                else
                {
                    if (cand.gearSummary[0])
                        snprintf(desc.label, sizeof(desc.label), "Horse %d (%.1fm) - %s", nMounts + 1, cand.distance, cand.gearSummary);
                    else if (cand.distance < 500.0f)
                        snprintf(desc.label, sizeof(desc.label), "Horse %d (%.1fm)", nMounts + 1, cand.distance);
                    else
                        snprintf(desc.label, sizeof(desc.label), "Horse %d", nMounts + 1);
                }

                if (cand.statArray >= kMinPointer)
                {
                    for (int k = 0; k < kStatArray_ScanEntries; ++k)
                    {
                        const uintptr_t e = cand.statArray + k * kSizeof_StatEntry;
                        int32_t stt = 0;
                        if (!StatEntryType(e, &stt)) break;
                        if (!PlausibleStatType(stt)) break;
                        if ((IsMountStaminaType(stt) || IsSpiritType(stt)) && nMountStam < kMaxMountStamEntries)
                        {
                            g_mountStamEntries[nMountStam++].store(e, std::memory_order_release);
                        }
                        else if (stt == StatType_HeatBurn || stt == StatType_ColdFrost || stt == 48 || stt == 49)
                        {
                            uint64_t cur = 0;
                            if (Read64(e + kOff_StatEntry_Current, &cur) && cur > 0)
                            {
                                Write64(e + kOff_StatEntry_Current, 0);
                                Write64(e + kOff_StatEntry_Norm, 0);
                            }
                        }
                    }
                }

                ++nMounts;
            }

            for (int i = nMounts; i < kMaxMounts; ++i)
            {
                g_mountActors[i].store(0, std::memory_order_release);
                g_mountTargetOwners[i].store(0, std::memory_order_release);
                g_mountOwners[i].store(0, std::memory_order_release);
                g_mountDescriptors[i] = Player::MountDescriptor{};
            }

            if (g_mountDescLockInit)
                LeaveCriticalSection(&g_mountDescLock);

            g_mountCount.store(nMounts, std::memory_order_release);

            // Clean unassigned slots (safeguarded against single-frame transient misses)
            for (int i = 1; i < kMaxPartyPlayers; ++i)
            {
                if (!slotAssigned[i])
                {
                    const uintptr_t existingOwner = g_owners[i].load(std::memory_order_relaxed);
                    bool stillAlive = false;
                    if (existingOwner >= kMinPointer)
                    {
                        uint32_t pIdx = 0;
                        if (Read32(existingOwner + kOff_Owner_PartyIndex, &pIdx) && pIdx == static_cast<uint32_t>(i + 1))
                            stillAlive = true;
                    }
                    if (!stillAlive)
                    {
                        g_hpEntries[i].store(0, std::memory_order_release);
                        g_actors[i].store(0, std::memory_order_release);
                        g_owners[i].store(0, std::memory_order_release);
                        g_targetOwners[i].store(0, std::memory_order_release);
                    }
                }
            }
            for (int i = kMaxPartyPlayers; i < kMaxPlayers; ++i)
            {
                g_hpEntries[i].store(0, std::memory_order_release);
                g_actors[i].store(0, std::memory_order_release);
                g_owners[i].store(0, std::memory_order_release);
                g_targetOwners[i].store(0, std::memory_order_release);
            }
            for (int i = nStam; i < kMaxStatEntries; ++i)
                g_stamEntries[i].store(0, std::memory_order_release);
            for (int i = nSpir; i < kMaxStatEntries; ++i)
                g_spiritEntries[i].store(0, std::memory_order_release);
            for (int i = nMountStam; i < kMaxMountStamEntries; ++i)
                g_mountStamEntries[i].store(0, std::memory_order_release);

            // Immediately apply stat pins when enabled
            if (st.godMode)
            {
                for (int i = 0; i < kMaxPartyPlayers; ++i)
                {
                    const uintptr_t e = g_hpEntries[i].load(std::memory_order_relaxed);
                    if (e >= kMinPointer) PinEntry(e);
                }
            }
            if (st.infStamina)
            {
                for (int i = 0; i < nStam; ++i)
                {
                    const uintptr_t e = g_stamEntries[i].load(std::memory_order_relaxed);
                    if (e >= kMinPointer) PinEntry(e);
                }
            }
            if (st.infMountStamina || st.infStamina)
            {
                for (int i = 0; i < nMountStam; ++i)
                {
                    const uintptr_t e = g_mountStamEntries[i].load(std::memory_order_relaxed);
                    if (e >= kMinPointer) PinEntry(e);
                }
            }
            if (st.infSpirit)
            {
                for (int i = 0; i < nSpir; ++i)
                {
                    const uintptr_t e = g_spiritEntries[i].load(std::memory_order_relaxed);
                    if (e >= kMinPointer) PinEntry(e);
                }
            }
        }

        bool IsPlayerEntity(uintptr_t target)
        {
            if (target < kMinPointer) return false;
            
            // 1. Check verified cached player slots
            for (int i = 0; i < kMaxPartyPlayers; ++i)
            {
                const uintptr_t own = g_owners[i].load(std::memory_order_relaxed);
                if (own >= kMinPointer)
                {
                    if (target == own) return true;
                    const uintptr_t act = g_actors[i].load(std::memory_order_relaxed);
                    if (act && target == act) return true;
                    const uintptr_t tgt = g_targetOwners[i].load(std::memory_order_relaxed);
                    if (tgt && target == tgt) return true;
                    const uintptr_t hp = g_hpEntries[i].load(std::memory_order_relaxed);
                    if (hp && target == hp) return true;
                }
            }

            // 2. Direct live fallback: ONLY accept move-owner if strictly verified via PossessorRoundTrip!
            // NPCs must NEVER be recognized as player entities or they steal God Mode and become immortal.
            const uintptr_t moveOwn = Teleport::GetMoveOwner();
            if (moveOwn >= kMinPointer && PossessorRoundTrip(moveOwn))
            {
                if (target == moveOwn) return true;
                SelfChain c;
                if (WalkSelfChain(moveOwn, &c))
                {
                    if (target == c.actor || target == c.targetOwner || target == c.statArray)
                        return true;
                }
            }

            // 3. Direct Type Descriptor Tag check:
            // If target is an owner with Tag 1 (SelfPlayer), it is definitely the player!
            if (IsPlayerClass(target))
                return true;

            // 4. If target is a vital root / targetOwner, check its actor back-pointer
            uint64_t targetActor = 0;
            if (Read64(target, &targetActor) && targetActor >= kMinPointer)
            {
                const uintptr_t pAct = g_actors[0].load(std::memory_order_relaxed);
                if (pAct >= kMinPointer && targetActor == pAct)
                    return true;
            }

            return false;
        }

        // --- Hook 1: pa_StatCommit (Stat write funnel) ---------------------
        using StatCommit_t = int64_t(__fastcall*)(void* entry, int64_t time, int64_t target, uint16_t flag);
        StatCommit_t oStatCommit = nullptr;
        void*        g_commitTarget = nullptr;

        int64_t __fastcall hkStatCommit(void* entry, int64_t time, int64_t target, uint16_t flag)
        {
            if (!entry) return 0;
            const State& st = State::Get();
            const uintptr_t e = reinterpret_cast<uintptr_t>(entry);

            // Lazy validation: if main player owner is invalid, do not nuke the whole player set
            const uintptr_t mainOwner = g_owners[0].load(std::memory_order_relaxed);

            bool isPlayerHp = InSet(g_hpEntries, kMaxPartyPlayers, e);
            if (!isPlayerHp)
            {
                const uintptr_t moveOwn = Teleport::GetMoveOwner();
                if (moveOwn >= kMinPointer && PossessorRoundTrip(moveOwn))
                {
                    SelfChain c;
                    if (WalkSelfChain(moveOwn, &c) && c.statArray == e)
                    {
                        isPlayerHp = true;
                        g_hpEntries[0].store(e, std::memory_order_release);
                        g_actors[0].store(c.actor, std::memory_order_release);
                        g_targetOwners[0].store(c.targetOwner, std::memory_order_release);
                        g_owners[0].store(moveOwn, std::memory_order_release);
                    }
                }
            }

            // Extinguish and prevent fire/heat accumulation (type 48) and frost (type 49) on player/mount
            int32_t accType = 0;
            if (StatEntryType(e, &accType) && (accType == StatType_HeatBurn || accType == StatType_ColdFrost || accType == 48 || accType == 49))
            {
                if (st.godMode)
                {
                    Write64(e + kOff_StatEntry_Current, 0);
                    Write64(e + kOff_StatEntry_Norm, 0);
                    return 0;
                }
            }

            const bool isStam       = InSet(g_stamEntries, kMaxStatEntries, e);
            const bool isMountStam  = (g_mountCount.load(std::memory_order_relaxed) > 0) && InSet(g_mountStamEntries, kMaxMountStamEntries, e);
            const bool isSpirit     = InSet(g_spiritEntries, kMaxStatEntries, e);

            // 1. PLAYER & MOUNT STAT LOCKING (God Mode / Inf Stamina / Inf Spirit)
            bool shouldLock = false;
            if (isPlayerHp)
            {
                shouldLock = st.godMode;
            }
            else
            {
                shouldLock = ((st.infStamina && isStam) || ((st.infMountStamina || st.infStamina) && isMountStam)) ||
                             (st.infSpirit && isSpirit);
            }

            int64_t fullTarget = target;
            if (shouldLock)
            {
                uint64_t base = 0, cap = 0, cur = 0;
                Read64(e + kOff_StatEntry_Base, &base);
                Read64(e + kOff_StatEntry_Cap, &cap);
                Read64(e + kOff_StatEntry_Current, &cur);
                uint64_t full = (cap > base) ? cap : base;
                if (!full && cur > 0 && cur < 1000000000ULL) full = cur;
                if (full > 0)
                {
                    target = static_cast<int64_t>(full);
                    fullTarget = target;
                }
            }

            const int64_t result = oStatCommit ? oStatCommit(entry, time, target, flag) : 0;

            if (shouldLock)
                PinEntry(e);

            return shouldLock ? fullTarget : result;
        }

        bool IsMountEntity(uintptr_t target)
        {
            if (target < kMinPointer) return false;
            const int count = g_mountCount.load(std::memory_order_relaxed);
            for (int i = 0; i < count && i < kMaxMounts; ++i)
            {
                const uintptr_t own = g_mountOwners[i].load(std::memory_order_relaxed);
                if (own >= kMinPointer)
                {
                    if (target == own) return true;
                    const uintptr_t act = g_mountActors[i].load(std::memory_order_relaxed);
                    if (act && target == act) return true;
                    const uintptr_t tgt = g_mountTargetOwners[i].load(std::memory_order_relaxed);
                    if (tgt && target == tgt) return true;
                }
            }

            // Direct live fallback: check if target belongs to the player's possessor
            const uintptr_t playerPoss = g_playerPossessor.load(std::memory_order_relaxed);
            if (playerPoss >= kMinPointer)
            {
                uint64_t poss = 0;
                if (Read64(target + kOff_Owner_Possessor, &poss) && poss == playerPoss)
                {
                    const uintptr_t mainPlayer = g_owners[0].load(std::memory_order_relaxed);
                    if (target != mainPlayer) return true;
                }
            }

            return false;
        }

        // --- Floor clamp bypass for enemy entities -------------------------
        // CRITICAL: sub_141558B60 (the HP apply path) does NOT call pa_StatCommit.
        // It instead invokes a virtual method directly on the stat entry to commit HP.
        // This means hkStatCommit is NEVER called for enemy HP hits, so our floor
        // bypass there has zero effect. We must clear the floor HERE in hkDamageApply,
        // BEFORE forwarding to oDamageApply, so sub_141558B60 sees Floor=0 and the
        // engine's own clamping logic allows the hit to reduce HP to 0.
        //
        // targetOwner (pa_StatApplyDelta arg1) is the vital-owner / root object.
        // The stat array is at [root + 0x58], and entry[0] is always Health.
        // --- Floor clamp bypass for enemy entities -------------------------
        // CRITICAL: sub_141558B60 (the HP apply path) does NOT call pa_StatCommit.
        // It instead invokes a virtual method directly on the stat entry to commit HP.
        // When One-Hit Kill is active, we clear the floor here so the hit reduces HP to 0.
        // Resolves the authoritative Health StatEntry pointer from any targetOwner form
        // (Root vital-owner, Actor component, Owner container, or direct StatArray pointer).
        uintptr_t ResolveEnemyHealthEntry(uintptr_t targetOwner)
        {
            if (targetOwner < kMinPointer) return 0;

            // 1. Direct path: targetOwner is Root object ([targetOwner + 0x58] is StatArray)
            uint64_t arr = 0;
            if (Read64(targetOwner + kOff_Root_StatArray, &arr) && arr >= kMinPointer)
            {
                const uintptr_t e = static_cast<uintptr_t>(arr);
                int32_t type = 0;
                if (StatEntryType(e, &type) && IsHealthType(type))
                    return e;
            }

            // 2. targetOwner is an Actor (status marker at +0x3A0)
            uint64_t marker = 0, root = 0;
            if (Read64(targetOwner + kOff_Actor_StatusMarker, &marker) && marker >= kMinPointer &&
                Read64(static_cast<uintptr_t>(marker) + kOff_Marker_TargetOwner, &root) && root >= kMinPointer &&
                Read64(static_cast<uintptr_t>(root) + kOff_Root_StatArray, &arr) && arr >= kMinPointer)
            {
                const uintptr_t e = static_cast<uintptr_t>(arr);
                int32_t type = 0;
                if (StatEntryType(e, &type) && IsHealthType(type))
                    return e;
            }

            // 3. targetOwner is an Owner container ([targetOwner + 0x08] is Actor)
            uint64_t act = 0;
            if (Read64(targetOwner + kOff_Owner_Actor, &act) && act >= kMinPointer &&
                Read64(static_cast<uintptr_t>(act) + kOff_Actor_StatusMarker, &marker) && marker >= kMinPointer &&
                Read64(static_cast<uintptr_t>(marker) + kOff_Marker_TargetOwner, &root) && root >= kMinPointer &&
                Read64(static_cast<uintptr_t>(root) + kOff_Root_StatArray, &arr) && arr >= kMinPointer)
            {
                const uintptr_t e = static_cast<uintptr_t>(arr);
                int32_t type = 0;
                if (StatEntryType(e, &type) && IsHealthType(type))
                    return e;
            }

            // 4. Direct stat entry
            int32_t directType = 0;
            if (StatEntryType(targetOwner, &directType) && IsHealthType(directType))
                return targetOwner;

            return 0;
        }


        // --- Damage multipliers: scale the hit at the apply dispatcher -----
        int64_t ScaleDamage(uintptr_t targetOwner, uintptr_t sourceCtx, int64_t delta)
        {
            const State& st = State::Get();

            // 1. Victim is Player or Player's Mount (Incoming Hit against Player)
            if (IsPlayerEntity(targetOwner) || IsMountEntity(targetOwner))
            {
                if (st.godMode) return 0;
                if (st.dmgInMult != 1.0f)
                {
                    const double scaled = static_cast<double>(delta) * static_cast<double>(st.dmgInMult);
                    return static_cast<int64_t>(scaled);
                }
                return delta;
            }

            // 2. Victim is ENEMY / TARGET (Outgoing Hit dealt by Player / Weapon / Skill)
            if (st.oneHitKill)
            {
                return (delta >= 0) ? 2000000000LL : -2000000000LL;
            }

            if (st.dmgOutMult != 1.0f)
            {
                const double scaled = static_cast<double>(delta) * static_cast<double>(st.dmgOutMult);
                return static_cast<int64_t>(scaled);
            }

            // When One-Hit Kill is OFF and multiplier is 1.0, preserve native damage completely!
            return delta;
        }

        // --- Hook 2: pa_StatApplyDelta (Damage dispatcher - 11 args) --------
        using DamageApply_t = int64_t(__fastcall*)(void* targetOwner, uint16_t statusId,
                                                   int64_t time, int64_t delta, uintptr_t sourceCtx,
                                                   char a6, char a7, char a8, char a9, char a10,
                                                   void* out);
        DamageApply_t oDamageApply = nullptr;
        void*         g_damageHookTarget = nullptr;

        int64_t __fastcall hkDamageApply(void* targetOwner, uint16_t statusId,
                                         int64_t time, int64_t delta, uintptr_t sourceCtx,
                                         char a6, char a7, char a8, char a9, char a10,
                                         void* out)
        {
            if (!oDamageApply) return 0;
            const State& st = State::Get();
            const uintptr_t owner = reinterpret_cast<uintptr_t>(targetOwner);
            if (owner < kMinPointer)
            {
                return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
            }

            // Immediately ensure player is resolved
            if (g_owners[0].load(std::memory_order_relaxed) < kMinPointer)
            {
                TickResolveSelf();
            }            // CRITICAL DEFENSE FOR LOADING SCREEN / WORLD LOAD:
            // If player is not yet loaded in the world, NEVER modify any deltas or treat
            // entities as enemies! All deltas during loading are internal engine setups.
            const uintptr_t mainPlayer = g_owners[0].load(std::memory_order_relaxed);
            if (mainPlayer < kMinPointer)
            {
                return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
            }

            const bool isPlayerTarget  = IsPlayerEntity(owner);
            const bool isMountTarget   = IsMountEntity(owner);
            const bool isEnemyTarget   = !isPlayerTarget && !isMountTarget;
            const bool isEnemyAttacker = (sourceCtx >= kMinPointer && !IsPlayerEntity(sourceCtx));

            // ---------------------------------------------------------------
            // A. OUTGOING DAMAGE: Player hitting Enemy
            // ---------------------------------------------------------------
            // Filter:
            // 1. Target is enemy (not player, not mount)
            // 2. Attacker is NOT an enemy (player or player skill/weapon)
            // 3. Delta is negative (damage, not positive timer ticks)
            // 4. Delta is not grass bending constant (-250,000,000)
            // 5. Delta is a real combat hit (delta > -500,000,000; larger are engine level scripts)
            if (isEnemyTarget && !isEnemyAttacker && delta < 0 && delta != -250000000LL && delta > -500000000LL)
            {
                if (st.oneHitKill)
                {
                    const uintptr_t healthEntry = ResolveEnemyHealthEntry(owner);
                    int64_t floorVal = 0;
                    uint64_t beforeHp = 0;
                    if (healthEntry >= kMinPointer)
                    {
                        Read64(healthEntry + kOff_StatEntry_Floor, reinterpret_cast<uint64_t*>(&floorVal));
                        Read64(healthEntry + kOff_StatEntry_Current, &beforeHp);
                    }

                    // CRITICAL QUEST / NON-LETHAL PROTECTION:
                    // If the enemy has a non-zero Floor clamp (floorVal > 0), it is a non-lethal quest target,
                    // sparring opponent, or story character meant to be subdued / injured rather than killed
                    // (e.g. Shakatu's private soldiers in "叛乱或革命", Broken Horn in "悬崖尽头回荡的欢呼").
                    // We must NEVER kill them or clear their Floor clamp! Instead, bring their HP down to
                    // floorVal in 1 hit so they transition to the wounded/downed/surrender state immediately.
                    if (floorVal > 0)
                    {
                        if (static_cast<int64_t>(beforeHp) > floorVal)
                        {
                            delta = -static_cast<int64_t>(beforeHp - floorVal);
                        }
                        else
                        {
                            delta = 0;
                        }
                    }
                    else
                    {
                        // Regular mortal enemy (floorVal <= 0): fatal blow so engine triggers native death sequence
                        delta = (beforeHp > 0) ? -static_cast<int64_t>(beforeHp + 1000000000ULL) : -2000000000LL;
                    }

                    return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
                }
                else if (st.dmgOutMult != 1.0f)
                {
                    const double scaled = static_cast<double>(delta) * static_cast<double>(st.dmgOutMult);
                    delta = static_cast<int64_t>(scaled);
                    return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
                }

                // If One-Hit Kill is OFF and Outgoing Multiplier is 1.0 (Normal Combat / Mod features OFF),
                // pass through directly to native engine without modifying delta, floor clamp, or entity immunity!
                return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
            }

            // ---------------------------------------------------------------
            // B. PLAYER / MOUNT TARGET: Incoming hit / heal / drain
            // ---------------------------------------------------------------
            if ((isPlayerTarget || isMountTarget) && delta != 0)
            {
                // 1. HEALING (Food, Potions, Medicine, Health Regen):
                // Positive delta is health/stat restoration.
                // NEVER clamp, nullify, or zero out positive delta!
                // Eating food must ALWAYS restore blood/HP.
                if (delta > 0)
                {
                    return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
                }

                // 2. EASY PARRY (Perfect Parry / Just Guard):
                // ONLY trigger if player is holding the Guard button!
                // Do NOT auto-parry without guarding or player becomes invincible when God Mode is OFF.
                if (isPlayerTarget && st.easyParry && delta < 0 && isEnemyAttacker && IsPlayerHoldingGuard())
                {
                    delta = 0;
                    a6 = 2; // Perfect parry
                    a7 = 1; // Counter stagger (engine deflects sourceCtx natively)
                    return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
                }

                // 3. EASY EVADE (Perfect Dodge):
                if (isPlayerTarget && st.easyEvade && delta < 0 && IsPlayerHoldingEvade())
                {
                    delta = 0;
                    a6 = 3; // Perfect evade
                    a7 = 0;
                    return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
                }

                if (statusId == StatType_Health || statusId == 0)
                {
                    if (st.godMode)
                    {
                        delta = 0;
                    }
                    else if ((Teleport::IsProtected() || Teleport::GetFlightEngaged()) && isPlayerTarget)
                    {
                        // Temporary landing protection right after Sky Arrival or active free flight
                        delta = 0;
                    }
                    else if (st.noFallDamage && isPlayerTarget && !isEnemyAttacker && sourceCtx == 0 &&
                             (static_cast<uint8_t>(a8) == 0x13 || static_cast<uint8_t>(a8) == 19 || a6 == 1))
                    {
                        // Nullify ONLY fall damage and high landing impact (a8=0x13 / 19), NOT enemy magic or hazards!
                        delta = 0;
                    }
                    else if (st.dmgInMult != 1.0f)
                    {
                        delta = ScaleDamage(owner, sourceCtx, delta);
                    }
                }
                else if (statusId == StatType_HeatBurn || statusId == StatType_ColdFrost || statusId == 48 || statusId == 49)
                {
                    // Fire/Heat or Cold accumulation attack incoming
                    if (st.godMode)
                    {
                        delta = 0;
                    }
                }
                else if (isPlayerTarget && IsPlayerStaminaType(statusId))
                {
                    if (st.infStamina || Teleport::GetFlightEngaged())
                        delta = 0;
                }
                else if (isMountTarget && (IsMountStaminaType(statusId) || IsSpiritType(statusId)))
                {
                    if (st.infMountStamina || st.infStamina || Teleport::GetFlightEngaged())
                        delta = 0;
                }
                else if (isPlayerTarget && IsSpiritType(statusId))
                {
                    if (st.infSpirit)
                        delta = 0;
                }
            }

            return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
        }
    }

    // --- Public Player Subsystem Implementation ----------------------------
    bool Player::Install()
    {
        if (!g_mountDescLockInit)
        {
            InitializeCriticalSection(&g_mountDescLock);
            g_mountDescLockInit = true;
        }

        g_charMgrGlobal = ResolveCharMgrGlobal();
        if (!g_charMgrGlobal)
        {
            LOG_ERR("player: char-manager global NOT FOUND (no anchor matched).");
        }

        if (!mem::InstallHook("player: stat-commit", kSig_StatCommit, nullptr,
                              &hkStatCommit, &oStatCommit, &g_commitTarget))
        {
            if (!mem::InstallHook("player: stat-commit (legacy)", kSig_StatCommit_Legacy, nullptr,
                                  &hkStatCommit, &oStatCommit, &g_commitTarget))
            {
                LOG("player: stat-commit bypassed (God Mode, Stamina, Spirit & Thermal actively managed by damage-apply hook and per-tick stat pins).");
            }
            else
            {
                LOG_OK("player: stat-commit legacy hook installed @ %p", g_commitTarget);
            }
        }
        else
        {
            LOG_OK("player: stat-commit hook installed @ %p", g_commitTarget);
        }

        if (!mem::InstallHook("player: damage-apply", kSig_DamageApply, "",
                              &hkDamageApply, &oDamageApply, &g_damageHookTarget))
        {
            if (mem::InstallHook("player: damage-apply (alt)", kSig_DamageApply_Alt, "damage multipliers disabled",
                                  &hkDamageApply, &oDamageApply, &g_damageHookTarget))
            {
                LOG_OK("player: damage-apply hook installed @ %p", g_damageHookTarget);
            }
            else
            {
                LOG_ERR("player: damage-apply signature NOT FOUND.");
            }
        }
        else
        {
            LOG_OK("player: damage-apply hook installed @ %p", g_damageHookTarget);
        }

        // Easy Parry / Easy Evade: force the Just-window evaluator's result.
        if (!mem::InstallHook("player: just-window-eval", kSig_JustWindowEval, nullptr,
                              &hkJustWindowEval, &oJustWindowEval, &g_justEvalTarget))
            LOG_WARN("player: just-window-eval NOT FOUND - easy parry/evade falls back to damage nullification only.");
        else
            LOG_OK("player: just-window-eval hook installed @ %p (easy parry/evade live)", g_justEvalTarget);

        return true;
    }

    void Player::Tick()
    {
        const State& st = State::Get();
        // Just-window assist input tracking must run EVERY frame (a dodge tap
        // is shorter than the resolve interval); cheap key/gamepad polls.
        if (st.easyParry || st.easyEvade)
            PollCombatInputTaps();

        static ULONGLONG s_lastResolve = 0;
        const ULONGLONG now = GetTickCount64();
        // Resolve interval: 1000ms when ready, 200ms when waiting for player to load.
        // This keeps game-thread execution time sub-microsecond and eliminates frametime spikes.
        const ULONGLONG interval = Ready() ? 1000 : 200;
        if (now - s_lastResolve >= interval)
        {
            TickResolveSelf();
            s_lastResolve = now;
        }

        if (AnyStatFeatureActive(st))
        {
            RefreshSelf();
        }
    }

    void Player::RefreshSelf()
    {
        const State& st = State::Get();
        if (st.infStamina)
        {
            for (int i = 0; i < kMaxStatEntries; ++i)
            {
                PinEntry(g_stamEntries[i].load(std::memory_order_relaxed));
            }
        }
        if (st.infMountStamina || st.infStamina)
        {
            for (int i = 0; i < kMaxMountStamEntries; ++i)
            {
                PinEntry(g_mountStamEntries[i].load(std::memory_order_relaxed));
            }
        }
        if (st.infSpirit)
        {
            for (int i = 0; i < kMaxStatEntries; ++i)
            {
                PinEntry(g_spiritEntries[i].load(std::memory_order_relaxed));
            }
        }
    }

    void Player::Remove()
    {
        mem::RemoveHook(&g_commitTarget);
        mem::RemoveHook(&g_damageHookTarget);
        mem::RemoveHook(&g_justEvalTarget);

        ClearPlayerSets();

        if (g_mountDescLockInit)
        {
            DeleteCriticalSection(&g_mountDescLock);
            g_mountDescLockInit = false;
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

    uintptr_t Player::GetOwner(int index)
    {
        if (index < 0 || index >= kMaxPlayers) return 0;
        return g_owners[index].load(std::memory_order_acquire);
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

    int Player::GetActiveCharacterIdx()
    {
        return g_activeCharacterIdx.load(std::memory_order_acquire);
    }

    uintptr_t Player::GetCharMgrGlobal()
    {
        return g_charMgrGlobal;
    }

    static uintptr_t ResolveCoreGlobal()
    {
        static uintptr_t s_coreGlobal = 0;
        if (s_coreGlobal) return s_coreGlobal;
        if (g_charMgrGlobal >= kMinPointer + 0x528)
        {
            s_coreGlobal = g_charMgrGlobal - 0x528;
            return s_coreGlobal;
        }
        const uintptr_t m = mem::FindPattern(kSig_GameCoreGlobal);
        if (m)
        {
            s_coreGlobal = mem::ResolveRipAt(m, 7);
            if (s_coreGlobal) return s_coreGlobal;
        }
        return s_coreGlobal;
    }

    uintptr_t Player::GetProfileOwner(int index)
    {
        if (index < 0 || index > 2) return 0;
        __try
        {
            const uintptr_t coreGlobal = ResolveCoreGlobal();
            if (!coreGlobal) return 0;
            uintptr_t core = 0;
            if (!ReadPtr(coreGlobal, &core) || core < kMinPointer) return 0;

            const uint32_t wantedParty = static_cast<uint32_t>(index + 1);

            // A fully usable owner has the full live viewport render chain.
            // If present, it takes highest priority.
            auto ownerUsable = [](uintptr_t owner) -> bool {
                if (owner < kMinPointer) return false;
                uintptr_t poss = 0, pawn = 0, sub = 0, render = 0;
                if (!ReadPtr(owner + kOff_Owner_Possessor, &poss) || poss < kMinPointer) return false;
                if (!ReadPtr(poss + kOff_Possessor_Pawn, &pawn) || pawn < kMinPointer) return false;
                if (!ReadPtr(pawn + 0x68, &sub) || sub < kMinPointer) return false;
                if (!ReadPtr(sub + 0x110, &render) || render < kMinPointer) return false;
                return true;
            };

            uintptr_t fallbackCand = 0;

            // Fast path: verified engine companion profile offsets on TU 2.00 / TU 2.01 / TU 2.02
            if (index == 0) // Kliff (Party 1)
            {
                uintptr_t p30 = 0;
                if (ReadPtr(core + 0x30, &p30) && p30 >= kMinPointer)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(p30 + 0x50, &owner) && owner >= kMinPointer)
                    {
                        uint32_t pIdx = 0;
                        if (Read32(owner + kOff_Owner_PartyIndex, &pIdx) && pIdx == wantedParty)
                        {
                            if (ownerUsable(owner)) return owner;
                            if (!fallbackCand) fallbackCand = owner;
                        }
                    }
                }
            }
            else if (index == 1) // Damiane (Party 2)
            {
                uintptr_t p0 = 0;
                if (ReadPtr(core + 0x00, &p0) && p0 >= kMinPointer)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(p0 + 0xF0, &owner) && owner >= kMinPointer)
                    {
                        uint32_t pIdx = 0;
                        if (Read32(owner + kOff_Owner_PartyIndex, &pIdx) && pIdx == wantedParty)
                        {
                            if (ownerUsable(owner)) return owner;
                            if (!fallbackCand) fallbackCand = owner;
                        }
                    }
                }
            }

            // Dynamic search for candidate companion owners across core root tables
            const uintptr_t roots[] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50 };
            for (uintptr_t rOff : roots)
            {
                uintptr_t root = 0;
                if (!ReadPtr(core + rOff, &root) || root < kMinPointer) continue;
                for (uintptr_t o = 0; o < 0x300; o += 8)
                {
                    uintptr_t cand = 0;
                    if (ReadPtr(root + o, &cand) && cand >= kMinPointer)
                    {
                        uint32_t pIdx = 0;
                        if (Read32(cand + kOff_Owner_PartyIndex, &pIdx) && pIdx == wantedParty)
                        {
                            if (ownerUsable(cand)) return cand;
                            if (!fallbackCand) fallbackCand = cand;
                        }
                    }
                }
            }

            if (fallbackCand) return fallbackCand;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    uintptr_t Player::GetProfileActor(int index)
    {
        __try
        {
            const uintptr_t owner = GetProfileOwner(index);
            if (!owner) return 0;
            uintptr_t actor = 0;
            if (ReadPtr(owner + kOff_Owner_Actor, &actor) && actor >= kMinPointer)
                return actor;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    static uintptr_t FindProfileEquipComp(uintptr_t root)
    {
        if (root < 0x100000000ULL || root > 0x7FFFFFFFFFFFULL) return 0;

        auto checkComp = [](uintptr_t comp) -> bool {
            if (comp < 0x100000000ULL || comp > 0x7FFFFFFFFFFFULL) return false;
            for (uintptr_t off : { (uintptr_t)0x90, (uintptr_t)0x88, (uintptr_t)0x80, (uintptr_t)0x50, (uintptr_t)0x78, (uintptr_t)0x38, (uintptr_t)0x40, (uintptr_t)0x48, (uintptr_t)0x60, (uintptr_t)0x70 })
            {
                uintptr_t desc = 0;
                if (ReadPtr(comp + off, &desc) && desc >= 0x100000000ULL && desc <= 0x7FFFFFFFFFFFULL)
                {
                    uintptr_t arr = 0;
                    uint32_t cnt = 0;
                    if (ReadPtr(desc + kOff_EquipTable_Array, &arr) && arr >= 0x100000000ULL && arr <= 0x7FFFFFFFFFFFULL &&
                        Read32(desc + kOff_EquipTable_Count, &cnt) && cnt > 0 && cnt <= 64)
                    {
                        for (uintptr_t stride : { (uintptr_t)0xD0, (uintptr_t)0xC8 })
                        {
                            const uintptr_t tagOff = (stride == 0xD0) ? 0xC8 : 0xC0;
                            int validItems = 0;
                            bool hasInvalidTag = false;
                            uint32_t tagMask = 0;
                            for (uint32_t i = 0; i < cnt; ++i)
                            {
                                const uintptr_t entry = arr + static_cast<uintptr_t>(i) * stride;
                                uint16_t tid = 0, tag = 0;
                                if (Read16(entry + kOff_InvSlot_TypeId, &tid) && tid != 0 && tid != kInvSlot_EmptyType)
                                {
                                    if (Read16(entry + tagOff, &tag) && tag < 32)
                                    {
                                        validItems++;
                                        tagMask |= (1u << tag);
                                    }
                                    else
                                    {
                                        hasInvalidTag = true;
                                        break;
                                    }
                                }
                            }
                            int distinct = 0;
                            for (uint32_t m = tagMask; m != 0; m &= (m - 1)) ++distinct;
                            if (validItems > 0 && !hasInvalidTag && distinct >= 2)
                                return true;
                        }
                    }
                }
            }
            return false;
        };

        // 1. Direct sub check (if root is already SubContainer, e.g. from Player::GetActor)
        uintptr_t comp = 0;
        if (ReadPtr(root + kOff_Sub_EquipComp, &comp) && comp >= 0x100000000ULL && comp <= 0x7FFFFFFFFFFFULL && checkComp(comp))
            return comp;

        // 2. Standard walk: root + 0x68 -> sub -> sub + 0x38 -> comp
        uintptr_t sub = 0;
        if (ReadPtr(root + kOff_Container_Sub, &sub) && sub >= 0x100000000ULL && sub <= 0x7FFFFFFFFFFFULL)
        {
            if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && comp >= 0x100000000ULL && comp <= 0x7FFFFFFFFFFFULL && checkComp(comp))
                return comp;
        }

        // 2. If root is owner, inspect inner actor (+0x68)
        uintptr_t inner = 0;
        if (ReadPtr(root + kOff_Owner_Actor, &inner) && inner >= kMinPointer && inner != root)
        {
            if (ReadPtr(inner + kOff_Container_Sub, &sub) && sub >= kMinPointer)
            {
                if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && comp >= kMinPointer && checkComp(comp))
                    return comp;
            }
            const uintptr_t directOffsets[] = { 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0x168 };
            for (uintptr_t dOff : directOffsets)
            {
                if (ReadPtr(inner + dOff, &comp) && comp >= kMinPointer && checkComp(comp))
                    return comp;
            }
        }

        // 3. Alternate sub / comp offsets on sub
        const uintptr_t subOffsets[] = { 0x60, 0x68, 0x70, 0x58, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0 };
        const uintptr_t compOffsets[] = { 0x38, 0x30, 0x40, 0x28, 0x48, 0x50, 0x58, 0x60, 0x68, 0x80, 0x88, 0x90, 0x168 };
        for (uintptr_t sOff : subOffsets)
        {
            if (ReadPtr(root + sOff, &sub) && sub >= kMinPointer)
            {
                for (uintptr_t cOff : compOffsets)
                {
                    if (ReadPtr(sub + cOff, &comp) && comp >= kMinPointer && checkComp(comp))
                        return comp;
                }
            }
        }

        // 4. Direct actor offsets
        const uintptr_t directOffsets[] = { 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0x168 };
        for (uintptr_t dOff : directOffsets)
        {
            if (ReadPtr(root + dOff, &comp) && comp >= kMinPointer && checkComp(comp))
                return comp;
        }

        // 5. Root itself ONLY if checkComp strictly passes
        if (checkComp(root)) return root;

        return 0;
    }

    uintptr_t Player::GetProfileEquipComp(int index)
    {
        __try
        {
            const uintptr_t actor = GetProfileActor(index);
            if (actor)
            {
                const uintptr_t comp = FindProfileEquipComp(actor);
                if (comp) return comp;
            }
            const uintptr_t owner = GetProfileOwner(index);
            if (owner)
            {
                const uintptr_t comp = FindProfileEquipComp(owner);
                if (comp) return comp;
            }
            // Direct tracked actor/owner fallback from CharacterManager
            const uintptr_t trackedActor = GetActor(index);
            if (trackedActor)
            {
                const uintptr_t comp = FindProfileEquipComp(trackedActor);
                if (comp) return comp;
            }
            const uintptr_t trackedOwner = GetOwner(index);
            if (trackedOwner)
            {
                const uintptr_t comp = FindProfileEquipComp(trackedOwner);
                if (comp) return comp;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    uintptr_t Player::GetMountActor(int index)
    {
        if (index < 0 || index >= kMaxMounts) return 0;
        return g_mountActors[index].load(std::memory_order_acquire);
    }

    uintptr_t Player::GetMountOwner(int index)
    {
        if (index < 0 || index >= kMaxMounts) return 0;
        return g_mountOwners[index].load(std::memory_order_acquire);
    }

    int Player::GetTrackedMountCount()
    {
        return g_mountCount.load(std::memory_order_acquire);
    }

    bool Player::GetMountDescriptor(int index, MountDescriptor* out)
    {
        if (index < 0 || index >= kMaxMounts || !out) return false;
        if (g_mountDescLockInit)
            EnterCriticalSection(&g_mountDescLock);
        *out = g_mountDescriptors[index];
        if (g_mountDescLockInit)
            LeaveCriticalSection(&g_mountDescLock);
        return out->actor >= kMinPointer;
    }
}