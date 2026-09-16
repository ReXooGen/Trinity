#include "player.h"
#include "stat_view.h"
#include "actor_registry.h"

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
#include "dye.h"
#include "equipment_table.h"
#include "mount_equipment.h"
#include "damage_policy.h"
#include <intrin.h>
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
        static_assert(kMaxPlayers >= kMaxPartyPlayers + 1); // Unknown active body plus identified party roots.
        constexpr int kMaxGaugePerType     = 4;
        constexpr int kMaxStatEntries      = kMaxPlayers * kMaxGaugePerType;
        constexpr int kMaxMounts           = 4;
        constexpr int kMaxMountStamEntries = 32;
        // kOff_Root_StatArray = 0x58 is defined in offsets.h

        std::atomic<uintptr_t> g_hpEntries[kMaxPlayers]{};
        std::atomic<uintptr_t> g_stamEntries[kMaxStatEntries]{};
        std::atomic<uintptr_t> g_spiritEntries[kMaxStatEntries]{};
        std::atomic<uintptr_t> g_mountStamEntries[kMaxMountStamEntries]{};

        // Combat/stat slots are packed, with the controlled body always in slot 0.
        std::atomic<uintptr_t> g_actors[kMaxPlayers]{};
        std::atomic<uintptr_t> g_owners[kMaxPlayers]{};
        std::atomic<uintptr_t> g_targetOwners[kMaxPlayers]{};

        // Public handles are indexed by identity, never by controlled-body slot.
        std::atomic<uintptr_t> g_characterActors[kMaxPartyPlayers]{};
        std::atomic<uintptr_t> g_characterOwners[kMaxPartyPlayers]{};

        std::atomic<uintptr_t> g_mountActors[kMaxMounts]{};
        std::atomic<uintptr_t> g_mountOwners[kMaxMounts]{};
        std::atomic<uintptr_t> g_mountTargetOwners[kMaxMounts]{};
        std::atomic<int>       g_mountCount{0};

        Player::MountDescriptor g_mountDescriptors[kMaxMounts]{};
        CRITICAL_SECTION       g_mountDescLock{};
        bool                   g_mountDescLockInit = false;

        std::atomic<uintptr_t> g_playerPossessor{0};
        std::atomic<int>       g_activeCharacterIdx{-1};

        // --- Stat-entry typing helpers -------------------------------------
        bool StatEntryType(uintptr_t entry, int32_t* type)
        {
            uint16_t t = 0;
            if (!Read16(entry + kOff_StatEntry_Type, &t)) return false;
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

        struct ClientStatSource
        {
            RegistryActor binding{};
            uintptr_t comp = 0;
            bool mount = false;
        };
        SRWLOCK g_clientStatLock = SRWLOCK_INIT;
        ClientStatSource g_clientStatSources[8]{};
        ActorRegistryView g_clientStatRegistry{};

        bool ClientStatSourceCurrent(const ActorRegistryView& registry, const ClientStatSource& source)
        {
            if (!RegistryActorCurrent(registry, source.binding)) return false;
            uintptr_t sub = 0, comp = 0;
            if (source.comp && (!ReadPtr(source.binding.owner + 0x68, &sub) ||
                !ReadPtr(sub + 0x38, &comp) || comp != source.comp || !IsNativeClientEquip(comp))) return false;
            if (source.mount && source.comp)
            {
                bool owned = false;
                if (!ReadNativeMountGear(source.comp, &owned) || !owned) return false;
            }
            const uintptr_t owner = source.binding.owner;
            uintptr_t td = 0, possessor = 0, pawn = 0, pawnTd = 0;
            uint8_t tag = 0, pawnTag = 0;
            // Companions share the client possessor with its current pawn.
            // This is independent of which realm populated g_owners first.
            return ReadPtr(owner + 0x88, &td) && Read8(td + 1, &tag) &&
                (source.mount ? tag == 5 : (tag == 1 || tag == 4 || tag == 9 || tag == 59)) &&
                ReadPtr(owner + 0xA0, &possessor) && IsEquipmentPointer(possessor) &&
                ReadPtr(possessor + 0xD0, &pawn) && IsEquipmentPointer(pawn) &&
                ReadPtr(pawn + 0x88, &pawnTd) && Read8(pawnTd + 1, &pawnTag) &&
                (pawnTag == 1 || pawnTag == 4 || pawnTag == 9 || pawnTag == 59 || pawnTag == 5) &&
                [&] { uintptr_t back = 0; return ReadPtr(pawn + 0xA0, &back) && back == possessor; }();
        }

        bool ClientStatEntry(uintptr_t entry, uintptr_t targetRoot = 0, bool* isMount = nullptr)
        {
            if (!TryAcquireSRWLockShared(&g_clientStatLock)) return false;
            const auto registry = ReadActorRegistry(Dye::ClientRegistryGlobal());
            bool found = false;
            if (registry.Same(g_clientStatRegistry))
                for (const auto& source : g_clientStatSources)
                {
                    if (!source.binding.owner || !ClientStatSourceCurrent(registry, source)) continue;
                    const auto view = ReadStatView(source.binding.owner);
                    if ((entry && view.Contains(entry)) || (targetRoot && view.count && view.root == targetRoot))
                    { found = true; if (isMount) *isMount = source.mount; break; }
                }
            ReleaseSRWLockShared(&g_clientStatLock);
            return found;
        }

        bool TrackedStatEntry(uintptr_t entry, bool* isMount = nullptr)
        {
            if (isMount) *isMount = false;
            for (const auto& source : g_owners)
            {
                const uintptr_t owner = source.load(std::memory_order_acquire);
                if (owner && ReadStatView(owner).Contains(entry)) return true;
            }
            for (const auto& source : g_mountOwners)
            {
                const uintptr_t owner = source.load(std::memory_order_acquire);
                if (owner && ReadStatView(owner).Contains(entry))
                {
                    if (isMount) *isMount = true;
                    return true;
                }
            }
            return ClientStatEntry(entry, 0, isMount);
        }

        void PinValidatedEntry(uintptr_t e)
        {
            // CRITICAL: NEVER pin elemental accumulation gauges (48 = Heat/Burn, 49 = Cold/Frost).
            // Pinning 48 fills the heat meter to 100% (400,000) and makes the character catch fire!
            int32_t t = 0;
            if (!StatEntryType(e, &t) || (!IsHealthType(t) && !IsStaminaType(t) && !IsSpiritType(t)))
                return;

            uint64_t base = 0, cap = 0, cur = 0;
            if (!Read64(e + kOff_StatEntry_Base, &base) ||
                !Read64(e + kOff_StatEntry_Cap, &cap) ||
                !Read64(e + kOff_StatEntry_Current, &cur)) return;
            if (base > 1000000000ULL || cap > 1000000000ULL) return;
            uint64_t full = (cap > base) ? cap : base;
            if (!full && cur > 0 && cur < 1000000000ULL) full = cur;
            if (!full) return;
            if (cur == full) return;
            Write64(e + kOff_StatEntry_Current, full);
            Write64(e + kOff_StatEntry_Norm,    full - base);
        }

        void PinEntry(uintptr_t e)
        {
            if (e < kMinPointer || !TrackedStatEntry(e)) return;
            PinValidatedEntry(e);
        }

        bool PossessorRoundTrip(uintptr_t owner);

        void RefreshClientStats()
        {
            // Only Player::Tick owns the cursor. Hooks take a nonblocking copy
            // of the published bindings, never wait behind discovery.
            static ULONGLONG nextScan = 0;
            static ULONGLONG nextVisit = 0;
            const ULONGLONG now = GetTickCount64();
            if (now < nextVisit) return;
            nextVisit = now + 16;
            static uint32_t bucket = 0, row = 0;
            const auto registry = ReadActorRegistry(Dye::ClientRegistryGlobal());
            ClientStatSource next[8]{};
            if (!TryAcquireSRWLockExclusive(&g_clientStatLock)) return;
            const bool changed = !registry.Same(g_clientStatRegistry);
            if (!changed) memcpy(next, g_clientStatSources, sizeof(next));
            else bucket = row = 0;
            ReleaseSRWLockExclusive(&g_clientStatLock);
            for (auto& source : next)
                if (!ClientStatSourceCurrent(registry, source) || !ReadStatView(source.binding.owner).count) source = {};
            // Native commit/delta hooks handle drains between visits.
            if (registry.bucketCount && now >= nextScan)
            {
                nextScan = now + 100;
                unsigned buckets = 0, nodes = 0;
                while (bucket < registry.bucketCount && buckets++ < 16 && nodes < 64)
                {
                    uint32_t count = 0;
                    if (!Read32(registry.buckets + uintptr_t{bucket} * 0x100, &count) || count > 31)
                    { bucket = row = 0; break; }
                    while (row < count && nodes++ < 64)
                    {
                        ClientStatSource source{ReadRegistryActor(registry, bucket, row++)};
                        source.mount = IsNativeMountOwner(source.binding.owner);
                        if (!ClientStatSourceCurrent(registry, source) || !ReadStatView(source.binding.owner).count) continue;
                        const uintptr_t comp = NativeEquipmentFromRoot(source.binding.owner);
                        if (!IsNativeClientEquip(comp)) continue;
                        source.comp = comp;
                        bool ownedMountGear = false;
                        const int id = source.mount ? -1 : Inventory::IdentifyCharacterFromComp(comp);
                        if (source.mount)
                        {
                            if (!ReadNativeMountGear(comp, &ownedMountGear) || !ownedMountGear) continue;
                        }
                        else if (id < 0 && !PossessorRoundTrip(source.binding.owner)) continue;
                        bool duplicate = false;
                        for (const auto& old : next) duplicate |= old.binding.owner == source.binding.owner;
                        if (!duplicate) for (auto& old : next) if (!old.binding.owner)
                        {
                            old = source;
                            const auto view = ReadStatView(source.binding.owner);
                            LOG("player: client stat source char=%d mount=%d owner=%p root=%p array=%p count=%u",
                                id, source.mount ? 1 : 0, reinterpret_cast<void*>(view.owner), reinterpret_cast<void*>(view.root),
                                reinterpret_cast<void*>(view.array), view.count);
                            break;
                        }
                    }
                    if (row >= count) { ++bucket; row = 0; }
                }
                if (bucket >= registry.bucketCount) bucket = row = 0;
            }
            if (!registry.Same(ReadActorRegistry(Dye::ClientRegistryGlobal()))) return;
            if (!TryAcquireSRWLockExclusive(&g_clientStatLock)) return;
            memcpy(g_clientStatSources, next, sizeof(next));
            g_clientStatRegistry = registry;
            ReleaseSRWLockExclusive(&g_clientStatLock);
            const auto& st = State::Get();
            if (!st.godMode && !st.infStamina && !st.infSpirit && !st.infMountStamina) return;
            for (const auto& source : next)
            {
                if (!ClientStatSourceCurrent(registry, source)) continue;
                const auto view = ReadStatView(source.binding.owner);
                for (uint32_t i = 0; i < view.count; ++i)
                {
                    const uintptr_t entry = view.array + uintptr_t{i} * kSizeof_StatEntry;
                    int32_t type = 0;
                    if (!StatEntryType(entry, &type)) break;
                    const bool stamina = source.mount
                        ? ((st.infMountStamina || st.infStamina) && IsMountStaminaType(type))
                        : (st.infStamina && IsPlayerStaminaType(type));
                    if ((st.godMode && IsHealthType(type)) || stamina || (st.infSpirit && IsSpiritType(type)))
                    {
                        if (!registry.Same(ReadActorRegistry(Dye::ClientRegistryGlobal())) ||
                            !ClientStatSourceCurrent(registry, source) || !view.Same(ReadStatView(source.binding.owner))) break;
                        PinValidatedEntry(entry);
                    }
                }
            }
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

        int IdentifyPlayerCharacter(uintptr_t owner)
        {
            if (owner < kMinPointer) return -1;
            uint32_t party = 0;
            if (core::GetGameVersion().revision < 2800 &&
                Read32(owner + kOff_Owner_PartyIndex, &party) && party >= 1 && party <= 3)
                return static_cast<int>(party - 1);

            // Inventory identity must use native evidence, not Player's published handles.
            int id = Inventory::IdentifyCharacterFromEquip(owner);
            if (id < 0 || id >= kMaxPartyPlayers)
            {
                uintptr_t actor = 0;
                if (ReadPtr(owner + kOff_Owner_Actor, &actor) && actor >= kMinPointer && actor != owner)
                    id = Inventory::IdentifyCharacterFromEquip(actor);
            }
            return (id >= 0 && id < kMaxPartyPlayers) ? id : -1;
        }

        struct SelfChain
        {
            uintptr_t actor        = 0;
            uintptr_t targetOwner  = 0;
            uintptr_t statArray    = 0;
            uint32_t count         = 0;
        };

        bool WalkSelfChain(uintptr_t owner, SelfChain* out)
        {
            const StatView view = ReadStatView(owner);
            if (!view.count) return false;
            int32_t t = 0;
            if (!StatEntryType(view.array, &t) || !IsHealthType(t)) return false;
            out->actor = view.actor;
            out->targetOwner = view.root;
            out->statArray = view.array;
            out->count = view.count;
            return true;
        }

        bool WalkMountVitalChain(uintptr_t owner, uintptr_t* outStatArray, uintptr_t* outTargetOwner = nullptr, uintptr_t* outActor = nullptr)
        {
            const auto view = ReadStatView(owner);
            if (!view.count) return false;
            if (outStatArray) *outStatArray = view.array;
            if (outTargetOwner) *outTargetOwner = view.root;
            if (outActor) *outActor = view.actor;
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

        uintptr_t FindMountEquipComp(uintptr_t actor, char* outGearSummary = nullptr, size_t cap = 0,
                                    bool* outHasGear = nullptr, bool* outOwnedItems = nullptr)
        {
            if (outHasGear) *outHasGear = false;
            if (outGearSummary && cap > 0) outGearSummary[0] = '\0';
            if (outOwnedItems) *outOwnedItems = false;
            const uintptr_t comp = NativeMountEquipmentFromRoot(actor);
            const EquipTableDesc table = ReadNativeEquipmentTable(comp);
            if (!table.valid) return 0;
            const bool hasGear = ReadNativeMountGear(comp, outOwnedItems);
            if (outHasGear) *outHasGear = hasGear;
            for (uint32_t i = 0; i < table.count; ++i)
            {
                const uintptr_t entry = table.array + i * table.stride;
                uint16_t tid = 0, tag = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || !tid || tid == kInvSlot_EmptyType ||
                    !Read16(entry + table.tagOffset, &tag)) continue;
                if (!NativeMountSlotName(tag)) continue;
                if (outGearSummary && cap && !outGearSummary[0])
                    Inventory::NameForTypeId(tid, outGearSummary, cap);
            }
            return comp;
        }

        void ClearPlayerSets()
        {
            g_activeCharacterIdx.store(-1, std::memory_order_release);
            g_playerPossessor.store(0, std::memory_order_release);
            for (int i = 0; i < kMaxPartyPlayers; ++i)
            {
                g_characterActors[i].store(0, std::memory_order_release);
                g_characterOwners[i].store(0, std::memory_order_release);
            }
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

            // Inspect a rolling window, plus still-current source cells of the
            // previously tracked actors. Offscreen companions/mounts remain
            // published only while their native list membership revalidates.
            struct Cell { uint32_t index; uintptr_t owner; };
            static Cell retained[kMaxPlayers + kMaxMounts + 1]{};
            static uintptr_t lastManager = 0, lastData = 0;
            static uint32_t cursor = 0;
            if (lastManager != mgr || lastData != data)
            {
                lastManager = static_cast<uintptr_t>(mgr);
                lastData = static_cast<uintptr_t>(data);
                cursor = 0;
                for (auto& cell : retained) cell = {};
            }
            Cell cells[96]{};
            uint32_t cellCount = 0;
            auto addCell = [&](uint32_t index, uintptr_t expected = 0) {
                uintptr_t owner = 0;
                if (index >= count || cellCount >= std::size(cells) || !ReadPtr(static_cast<uintptr_t>(data) + uintptr_t{index} * 8, &owner) ||
                    !IsEquipmentPointer(owner) || (expected && owner != expected)) return;
                for (uint32_t i = 0; i < cellCount; ++i) if (cells[i].owner == owner) return;
                cells[cellCount++] = {index, owner};
            };
            for (const auto& cell : retained) if (cell.owner) addCell(cell.index, cell.owner);
            // The movement hook supplies a live controlled-body hint while the
            // manager reallocates/reorders. PASS 1 still requires tag, possessor
            // round-trip and a valid vital chain; it never assigns companion IDs.
            const uintptr_t moving = Teleport::GetMoveOwner();
            if (IsEquipmentPointer(moving) && PossessorRoundTrip(moving) && cellCount < std::size(cells))
                cells[cellCount++] = {UINT32_MAX, moving};
            const uint32_t budget = count < 64 ? count : 64;
            for (uint32_t n = 0; n < budget; ++n)
            {
                if (cursor >= count) cursor = 0;
                addCell(cursor++);
            }
            uint64_t anchorVt = 0;
            uintptr_t mainPlayerOwner = 0;
            uint64_t playerPoss = 0;
            SelfChain mainC;

            // PASS 1: Find the REAL controlled player using the POSSESSOR ROUND-TRIP.
            // This is what the engine itself does (sub_2393AA0) and is immune to
            // uninitialized or accidental tag matches during world load.
            // *(*(owner+0xA0)+0xD0) == owner  → true ONLY for the real controlled body.
            for (uint32_t i = 0; i < cellCount; ++i)
            {
                const uintptr_t ch = cells[i].owner;
                const uintptr_t cand = ResolveEntityFromListSlot(static_cast<uintptr_t>(ch));
                if (cand < kMinPointer) continue;

                // Must pass BOTH the tag check AND the possessor round-trip.
                if (!IsPlayerClass(cand) && !IsPlayerOrCompanion(cand)) continue;
                if (!PossessorRoundTrip(cand)) continue;

                // A round-trip can outlive its vitals during reload. Keep looking.
                uint64_t candVt = 0, candPoss = 0;
                SelfChain candC;
                if (!Read64(cand, &candVt) || candVt < kMinPointer ||
                    !Read64(cand + kOff_Owner_Possessor, &candPoss) || candPoss < kMinPointer ||
                    !WalkSelfChain(cand, &candC))
                    continue;
                anchorVt = candVt;
                playerPoss = candPoss;
                mainC = candC;
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
            int nPlayers = 1, nStam = 0, nMountStam = 0, nSpir = 0, nMounts = 0;
            bool slotAssigned[kMaxPartyPlayers] = { false, false, false };
            uintptr_t characterActors[kMaxPartyPlayers] = {};
            uintptr_t characterOwners[kMaxPartyPlayers] = {};

            // Resolve identity before publishing any handles. Unknown is not Kliff.
            const int activeIdx = IdentifyPlayerCharacter(mainPlayerOwner);

            // PASS 2: Slot 0 is ALWAYS the active/controlled player, even if unidentified.
            g_playerPossessor.store(static_cast<uintptr_t>(playerPoss), std::memory_order_release);
            g_hpEntries[0].store(mainC.statArray, std::memory_order_release);
            g_actors[0].store(mainC.actor, std::memory_order_release);
            g_targetOwners[0].store(mainC.targetOwner, std::memory_order_release);
            g_owners[0].store(mainPlayerOwner, std::memory_order_release);

            // Character indices never select stat slots: offscreen Kliff must not replace slot 0.
            if (activeIdx >= 0 && activeIdx < kMaxPartyPlayers)
            {
                characterActors[activeIdx] = mainC.actor;
                characterOwners[activeIdx] = mainPlayerOwner;
                slotAssigned[activeIdx] = true;
            }
            else
            {
                LOG_THROTTLE(10000, "player: controlled body has no native character identity; indexed handle withheld.");
            }

            for (uint32_t k = 1; k < mainC.count; ++k)
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
            // Stats do not depend on naming the companion's current gear.
            // Shared native possessor + humanoid tag establishes a party body.
            {
                for (uint32_t i = 0; i < cellCount; ++i)
                {
                    if (nPlayers >= kMaxPlayers) break;

                    const uintptr_t ch = cells[i].owner;
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

                    const int compIdx = IdentifyPlayerCharacter(owner);

                    // Must be a valid companion slot (0=Kliff, 1=Damiane, 2=Oongka)
                    const bool knownCharacter = compIdx >= 0 && compIdx < kMaxPartyPlayers;
                    const bool nativePartyBody = sharesPlayerPoss && isPlayerOrCompTag;
                    if (!nativePartyBody && (!knownCharacter || slotAssigned[compIdx])) continue;

                    uint64_t actor = 0;
                    Read64(owner + kOff_Owner_Actor, &actor);
                    const uintptr_t compActor = (actor >= kMinPointer) ? static_cast<uintptr_t>(actor) : owner;

                    if (knownCharacter && !slotAssigned[compIdx])
                    {
                        characterActors[compIdx] = compActor;
                        characterOwners[compIdx] = owner;
                        slotAssigned[compIdx] = true;
                    }

                    // WalkSelfChain finds vitals/HP for genuine humanoid characters
                    SelfChain compC;
                    uintptr_t statArr = 0, targetOwn = 0;
                    if (WalkSelfChain(owner, &compC))
                    {
                        statArr = compC.statArray;
                        targetOwn = compC.targetOwner;
                    }

                    const int statSlot = nPlayers++;
                    g_actors[statSlot].store(compActor, std::memory_order_release);
                    g_owners[statSlot].store(owner, std::memory_order_release);
                    g_hpEntries[statSlot].store(statArr, std::memory_order_release);
                    g_targetOwners[statSlot].store(targetOwn, std::memory_order_release);

                    if (statArr >= kMinPointer)
                    {
                        for (uint32_t k = 1; k < compC.count; ++k)
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
                    }
                }
            }

            // Publish only roots seen in this resolve, including zero for absent characters.
            for (int i = 0; i < kMaxPartyPlayers; ++i)
            {
                g_characterActors[i].store(characterActors[i], std::memory_order_release);
                g_characterOwners[i].store(characterOwners[i], std::memory_order_release);
            }
            g_activeCharacterIdx.store(activeIdx, std::memory_order_release);
            for (int i = nPlayers; i < kMaxPlayers; ++i)
            {
                g_hpEntries[i].store(0, std::memory_order_release);
                g_actors[i].store(0, std::memory_order_release);
                g_owners[i].store(0, std::memory_order_release);
                g_targetOwners[i].store(0, std::memory_order_release);
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

            for (uint32_t i = 0; i < cellCount && candMountCount < 32; ++i)
            {
                const uintptr_t ch = cells[i].owner;
                const uintptr_t owner = static_cast<uintptr_t>(ch);
                if (owner < kMinPointer || owner == mainPlayerOwner) continue;
                bool isPartyOwner = false;
                for (int p = 0; p < kMaxPartyPlayers; ++p)
                {
                    if (owner == characterOwners[p])
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
                        const uintptr_t pOwn = characterOwners[p];
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
                    const auto mountView = ReadStatView(owner);
                    for (uint32_t k = 0; k < mountView.count; ++k)
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
                bool hasHorseGear = false, hasOwnedItems = false;
                char gearSummary[64] = {};
                uintptr_t eqComp = FindMountEquipComp(effActor, gearSummary, sizeof(gearSummary), &hasHorseGear, &hasOwnedItems);
                if (!eqComp && effActor != owner)
                    eqComp = FindMountEquipComp(owner, gearSummary, sizeof(gearSummary), &hasHorseGear, &hasOwnedItems);

                // Default gear on ambient horses has instance=-1. Prefer the
                // player's positive-ID gear; +50 values are entity IDs, not ownership.
                const bool isPlayerOwnedMount = sharesPartyPoss || isRidden || hasOwnedItems;

                // STRICT FILTER: eliminate ambient birds/pigeons, dogs, cats, squirrels, etc.
                // NEVER eliminate player-owned mount or mount with horse gear!
                if (!isRidden && !hasMountSprint && !hasHorseGear && !isPlayerOwnedMount)
                    continue;

                // Calculate distance to player (protected against NaN / Inf to prevent CRT sort failure)
                float dist = 9999.0f;
                if (isPlayerOwnedMount || isRidden)
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
            // 1. Possessor-linked mount or positive-instance equipment comes first
            // 2. Ridden mount comes next
            // 3. Mount with horse gear comes before mount without gear
            // 4. Closest mount comes next (with strict weak ordering check against NaN)
            std::sort(candMounts, candMounts + candMountCount, [](const CandidateMount& a, const CandidateMount& b) {
                const bool aOwned = a.isPlayerOwned;
                const bool bOwned = b.isPlayerOwned;
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
                    const auto mountView = ReadStatView(cand.owner);
                    for (uint32_t k = 0; k < mountView.count; ++k)
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

            unsigned saved = 0;
            for (auto& cell : retained) cell = {};
            auto retain = [&](uintptr_t owner) {
                if (!owner || saved >= std::size(retained)) return;
                for (unsigned i = 0; i < saved; ++i) if (retained[i].owner == owner) return;
                for (uint32_t i = 0; i < cellCount; ++i)
                    if (cells[i].owner == owner) { retained[saved++] = cells[i]; break; }
            };
            retain(mainPlayerOwner);
            for (int i = 1; i < nPlayers; ++i) retain(g_owners[i].load(std::memory_order_relaxed));
            for (int i = 0; i < nMounts; ++i) retain(g_mountOwners[i].load(std::memory_order_relaxed));

            for (int i = nStam; i < kMaxStatEntries; ++i)
                g_stamEntries[i].store(0, std::memory_order_release);
            for (int i = nSpir; i < kMaxStatEntries; ++i)
                g_spiritEntries[i].store(0, std::memory_order_release);
            for (int i = nMountStam; i < kMaxMountStamEntries; ++i)
                g_mountStamEntries[i].store(0, std::memory_order_release);

            // Immediately apply stat pins when enabled
            if (st.godMode)
            {
                for (int i = 0; i < nPlayers; ++i)
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
            for (int i = 0; i < kMaxPlayers; ++i)
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
            if (!st.godMode && !st.infStamina && !st.infMountStamina && !st.infSpirit)
                return oStatCommit ? oStatCommit(entry, time, target, flag) : 0;
            const uintptr_t e = reinterpret_cast<uintptr_t>(entry);

            // Lazy validation: if main player owner is invalid, do not nuke the whole player set
            const uintptr_t mainOwner = g_owners[0].load(std::memory_order_relaxed);

            bool isPlayerHp = InSet(g_hpEntries, kMaxPlayers, e);
            if (!isPlayerHp && st.godMode && g_hpEntries[0].load(std::memory_order_relaxed) < kMinPointer)
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
            if (StatEntryType(e, &accType) &&
                (accType == StatType_HeatBurn || accType == StatType_ColdFrost || accType == 48 || accType == 49) &&
                TrackedStatEntry(e))
            {
                if (st.godMode)
                {
                    Write64(e + kOff_StatEntry_Current, 0);
                    Write64(e + kOff_StatEntry_Norm, 0);
                    return 0;
                }
            }

            bool mountEntry = false;
            const bool currentEntry = TrackedStatEntry(e, &mountEntry);
            int32_t currentType = -1;
            StatEntryType(e, &currentType);
            isPlayerHp = currentEntry && IsHealthType(currentType);
            const bool isStam = currentEntry && !mountEntry && IsPlayerStaminaType(currentType);
            const bool isMountStam = currentEntry && mountEntry && IsMountStaminaType(currentType);
            const bool isSpirit = currentEntry && IsSpiritType(currentType);

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
                if (!Read64(e + kOff_StatEntry_Base, &base) || !Read64(e + kOff_StatEntry_Cap, &cap) ||
                    !Read64(e + kOff_StatEntry_Current, &cur) || base > 1000000000ULL || cap > 1000000000ULL)
                    return oStatCommit ? oStatCommit(entry, time, target, flag) : 0;
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

        // --- Native damage contract (disk verified at 141719850 -> 141717840) ---
        using DamageApply_t = int64_t(__fastcall*)(void* targetOwner, uint16_t statusId,
                                                   int64_t time, int64_t delta, uintptr_t sourceCtx,
                                                   char a6, char a7, char a8, char a9, char a10,
                                                   void* out);
        DamageApply_t oDamageApply = nullptr;
        void*         g_damageHookTarget = nullptr;

        // 141FE5B3D..5B6B supplies exactly eight qword args. At 141FE7470,
        // RBP=entry RSP-948: +950/+958/+960/+968 are the register homes;
        // +970=victim, +978=time, +980=auxiliary, +988=damage result.
        // Return RAX is the arg2 error/result pointer (141FE8164), not a bool.
        using DamageEvent_t = void* (__fastcall*)(void* processor, void* result, void* instance,
                                                  uintptr_t source, uintptr_t victim, int64_t time,
                                                  void* auxiliary, void* damageResult);
        using DamageDefinition_t = uintptr_t (__fastcall*)(void* instance);
        DamageEvent_t oDamageEvent = nullptr;
        DamageDefinition_t g_damageDefinition = nullptr;
        void* g_damageEventTarget = nullptr;
        uintptr_t g_damageBuffVtable = 0;
        uintptr_t g_damageEventHpReturn = 0;
        thread_local const damage_policy::ScopedHitContext* g_damageContext = nullptr;

        constexpr const char* kDamageEventSignature =
            "48 8B C4 4C 89 48 20 4C 89 40 18 48 89 50 10 48 89 48 08 "
            "55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 F6 FF FF "
            "48 81 EC 08 0A 00 00 C5 F8 29 70 A8 C5 F8 29 78 98";
        constexpr const char* kDamageDefinitionSignature =
            "40 53 48 83 EC 20 48 8B D9 B8 FF FF 00 00 48 81 C1 EA 00 00 00 "
            "66 39 01 74 ?? E8 ?? ?? ?? ?? 8B 8B 34 01 00 00 48 03 C9 "
            "48 8B 40 18 48 8B 44 C8 08";
        constexpr const char* kDamageBuffVtableSignature =
            "48 8D 05 ?? ?? ?? ?? 48 89 07 C6 87 90 00 00 00 02 "
            "48 89 B7 98 00 00 00 48 89 B7 A0 00 00 00 48 89 B7 A8 00 00 00";

        bool DamageImageContains(uintptr_t address, size_t size)
        {
            const auto& module = mem::GameModule();
            return address >= module.base && size <= module.size &&
                   address - module.base <= module.size - size;
        }

        bool IsDamageBuffVtable(uintptr_t vtable)
        {
            // MSVC x64 COL: signature/offset/cdOffset/typeRVA/hierarchyRVA/selfRVA.
            uintptr_t col = 0;
            uint32_t fields[6]{};
            constexpr char expected[] = ".?AVDamageBuffData@pa@@";
            char name[sizeof(expected)]{};
            const uintptr_t base = mem::GameModule().base;
            return vtable >= sizeof(uintptr_t) && DamageImageContains(vtable - 8, 0x60) &&
                   ReadPtr(vtable - 8, &col) && DamageImageContains(col, sizeof(fields)) &&
                   mem::ReadBytes(col, fields, sizeof(fields)) &&
                   fields[0] == 1 && fields[1] == 0 && fields[2] == 0 && base + fields[5] == col &&
                   DamageImageContains(base + fields[3], 16 + sizeof(expected)) &&
                   mem::ReadBytes(base + fields[3] + 16, name, sizeof(name)) &&
                   memcmp(name, expected, sizeof(expected)) == 0;
        }

        uintptr_t DamageVictimRoot(uintptr_t victim)
        {
            uintptr_t sub = 0, engine = 0, root = 0, back = 0;
            // Same chain as 141FE7542..754A, checked in both directions.
            if (victim < kMinPointer || !ReadPtr(victim + 0x68, &sub) || sub < kMinPointer ||
                !ReadPtr(sub + 0x20, &engine) || engine < kMinPointer ||
                !ReadPtr(engine + 0x18, &root) || root < kMinPointer ||
                !ReadPtr(root, &back) || back != engine ||
                !ReadPtr(engine + 8, &back) || back != victim)
                return 0;
            return root;
        }

        uintptr_t ReadDamageDefinition(void* instance)
        {
            if (!g_damageDefinition || reinterpret_cast<uintptr_t>(instance) < kMinPointer) return 0;
            // Verified 1421325F0 ABI: instance in RCX, definition pointer in RAX;
            // +EA selects BuffInfo, otherwise +E8/+144/+134 selects SkillInfo data.
            // Keep SEH isolated from the scoped context's C++ destructors.
            __try { return g_damageDefinition(instance); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        damage_policy::HitContext ReadDamageContext(void* instance, uintptr_t source,
                                                    uintptr_t victim, int64_t time)
        {
            damage_policy::HitContext context{};
            context.source = source;
            context.victimRoot = DamageVictimRoot(victim);
            context.time = time;
            if (source < kMinPointer || !context.victimRoot) return context;
            const uintptr_t definition = ReadDamageDefinition(instance);
            uintptr_t vtable = 0;
            uint8_t kind = 0xFF, noDead = 1;
            if (definition < kMinPointer || !g_damageBuffVtable ||
                !ReadPtr(definition, &vtable) || vtable != g_damageBuffVtable ||
                !Read8(definition + 8, &kind) || kind != 0 ||
                !Read8(definition + 0x118, &noDead))
                return context;
            // 141FE7E81 tests this exact byte; 7E97..7EC5 caps damage at HP-1000.
            // skill.pabgb Damage_Fixed/NoDead (1011/1012) and UnArmed/NoDead
            // (1002/1003) differ here, while both share cause=8/damageType=10.
            context.definitionVerified = true;
            context.noDead = noDead != 0;
            return context;
        }

        void* InvokeDamageEvent(void* processor, void* result, void* instance,
                                uintptr_t source, uintptr_t victim, int64_t time,
                                void* auxiliary, void* damageResult,
                                const damage_policy::ScopedHitContext* previous)
        {
            // Also restore TLS on native SEH unwind; /EHsc alone does not guarantee
            // C++ destructor execution for an exception raised inside the engine.
            __try { return oDamageEvent(processor, result, instance, source, victim, time, auxiliary, damageResult); }
            __finally { g_damageContext = previous; }
        }

        void* __fastcall hkDamageEvent(void* processor, void* result, void* instance,
                                       uintptr_t source, uintptr_t victim, int64_t time,
                                       void* auxiliary, void* damageResult)
        {
            if (!oDamageEvent) return result;
            const auto* previous = g_damageContext;
            // Unknown/nested events shadow their parent even during accessor calls.
            damage_policy::ScopedHitContext unknown(g_damageContext, {});
            const State& st = State::Get();
            const auto context = (st.oneHitKill || st.dmgOutMult != 1.0f)
                ? ReadDamageContext(instance, source, victim, time) : damage_policy::HitContext{};
            damage_policy::ScopedHitContext hit(g_damageContext, context);
            return InvokeDamageEvent(processor, result, instance, source, victim, time,
                                     auxiliary, damageResult, previous);
        }

        bool InstallDamageEventHook()
        {
            if (!g_damageHookTarget) return false;
            const auto unique = [](const char* signature) -> uintptr_t {
                const auto matches = mem::FindAllMatches(signature, 2);
                return matches.size() == 1 ? matches[0] : 0;
            };
            const uintptr_t entry = unique(kDamageEventSignature);
            const uintptr_t accessor = unique(kDamageDefinitionSignature);
            const uintptr_t factory = unique(kDamageBuffVtableSignature);
            const uintptr_t caller = unique(
                "48 8B 8D A0 00 00 00 48 89 4C 24 38 48 8D 4C 24 70 "
                "48 89 4C 24 30 48 89 44 24 28 48 89 74 24 20 4D 8B CF "
                "4D 8B C6 48 8D 54 24 78 49 8B CC E8 ?? ?? ?? ??");
            if (!entry || !accessor || !factory || !caller || !DamageImageContains(entry, 0xD42) ||
                mem::ResolveCall(caller + 46) != entry)
                return false;
            const mem::ModuleRegion event{entry, 0xD42};
            const uintptr_t definitionCall = mem::FindPattern(
                "4D 8B F1 49 8B C8 45 33 FF 44 89 7D 98 E8 ?? ?? ?? ?? "
                "4C 8B C8 44 38 78 08 4D 0F 45 CF 4C 89 4D A0", event);
            const uintptr_t hpCall = mem::FindPattern(
                "48 8D 85 60 09 00 00 48 89 44 24 50 C6 44 24 48 00 "
                "0F B6 85 50 09 00 00 88 44 24 40 44 88 64 24 38 44 88 54 24 30 "
                "C6 44 24 28 00 4C 89 74 24 20 4C 8B 85 78 09 00 00 "
                "0F B7 92 A0 00 00 00 48 8B 49 18 E8 ?? ?? ?? ??", event);
            const uintptr_t noDead = mem::FindPattern(
                "48 8B 5D A0 80 BB 18 01 00 00 00 74 3F 48 8B 4D 20 "
                "48 3B CF 7F 36 48 89 7E 08 48 B8 CF F7 53 E3 A5 9B C4 20", event);
            if (!definitionCall || mem::ResolveCall(definitionCall + 13) != accessor ||
                !hpCall || mem::ResolveCall(hpCall + 66) != reinterpret_cast<uintptr_t>(g_damageHookTarget) || !noDead)
                return false;
            const uintptr_t vtable = mem::ResolveRipAt(factory, 7);
            uintptr_t reader = 0;
            if (!IsDamageBuffVtable(vtable) || !ReadPtr(vtable + 0x50, &reader) ||
                !DamageImageContains(reader, 0x1E1) || !mem::FindPattern(
                    "48 8D 97 18 01 00 00 41 B8 01 00 00 00 48 8B CB FF 50 08",
                    mem::ModuleRegion{reader, 0x1E1}))
                return false;

            // Publish immutable metadata before enabling the detour.
            g_damageDefinition = reinterpret_cast<DamageDefinition_t>(accessor);
            g_damageBuffVtable = vtable;
            g_damageEventHpReturn = hpCall + 71;
            if (mem::InstallHook("player: per-hit damage context", kDamageEventSignature,
                                 "outgoing damage left native", &hkDamageEvent,
                                 &oDamageEvent, &g_damageEventTarget))
                return true;
            g_damageEventHpReturn = 0;
            g_damageBuffVtable = 0;
            g_damageDefinition = nullptr;
            return false;
        }

        struct NativeDamageContract
        {
            uintptr_t clientGlobal = 0;
            uintptr_t serverGlobal = 0;
            uint32_t realmOffset = 0;
            bool targetLayoutVerified = false;
        };

        const NativeDamageContract& DamageContract()
        {
            // Bounded to the installed dispatcher/its HP callee, not a whole-module scan.
            // Cache code metadata only; realm, status ID, entities and HP are read per hit.
            // InstallHook enables the detour before publishing its target. Do not cache
            // that brief unavailable state as a permanent contract-resolution failure.
            static const NativeDamageContract unavailable{};
            if (!g_damageHookTarget) return unavailable;
            static const NativeDamageContract contract = [] {
                NativeDamageContract result{};
                const mem::ModuleRegion dispatcher{reinterpret_cast<uintptr_t>(g_damageHookTarget), 0x160};
                if (!dispatcher.base) return result;
                const uintptr_t route = mem::FindPattern(
                    "65 48 8B 0C 25 58 00 00 00 48 8B 11 41 B8 ?? ?? ?? ?? "
                    "48 8B 0D ?? ?? ?? ?? 41 80 3C 10 00 48 0F 45 0D ?? ?? ?? ?? "
                    "48 8B 09 48 8B 51 08 48 8B 4A 10 66 3B 99 A0 00 00 00", dispatcher);
                if (!route || !Read32(route + 14, &result.realmOffset) || result.realmOffset > 0x1000)
                    return NativeDamageContract{};
                result.clientGlobal = mem::ResolveRipAt(route + 18, 7);
                result.serverGlobal = mem::ResolveRipAt(route + 30, 8);

                // Positional mapping: source -> HP r9; a6/a7 -> HP arg5/6;
                // HP arg7=0; a8/a9/a10 -> HP arg8/9/10. Never invent a tackle enum.
                const uintptr_t callSite = mem::FindPattern(
                    "C6 44 24 30 00 0F B6 8C 24 B0 00 00 00 88 4C 24 28 "
                    "0F B6 8C 24 A8 00 00 00 88 4C 24 20 4C 8B 8C 24 A0 00 00 00 "
                    "4C 8B C0 48 8B D5 48 8B CE E8 ?? ?? ?? ??", dispatcher);
                if (!callSite) return NativeDamageContract{};
                const uintptr_t hp = mem::ResolveCall(callSite + 46);
                const auto& module = mem::GameModule();
                if (hp < module.base || hp - module.base >= module.size ||
                    module.size - (hp - module.base) < 0xA00)
                    return NativeDamageContract{};
                const mem::ModuleRegion healthFunction{hp, 0xA00};
                result.targetLayoutVerified = mem::FindPattern(
                    "49 8B 0F 48 8B 81 F8 03 00 00 80 78 18 00 75 ?? "
                    "80 B9 98 03 00 00 00 75 ?? 49 8B 07 0F B6 88 73 02 00 00 "
                    "88 4C 24 45 84 C9", healthFunction) != 0 && mem::FindPattern(
                    "0F B6 80 72 02 00 00 88 44 24 44 0F B7 D3 49 8B CF E8", healthFunction) != 0;
                return result;
            }();
            return contract;
        }

        bool ResolveDamageHealthStatus(uint16_t* status)
        {
            const auto& contract = DamageContract();
            if (!contract.clientGlobal || !contract.serverGlobal) return false;
            // Native uses *GS:[58] + the dispatcher immediate (disk: 0x1EC),
            // NOT the unrelated allocator TLS flag 0x1FD. TLS may live below kMinPointer.
            uintptr_t global = 0;
            __try
            {
                const uintptr_t tlsArray = __readgsqword(0x58);
                if (!tlsArray) return false;
                const uintptr_t tls = *reinterpret_cast<const uintptr_t*>(tlsArray);
                if (!tls) return false;
                global = *reinterpret_cast<const uint8_t*>(tls + contract.realmOffset)
                    ? contract.serverGlobal : contract.clientGlobal;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            uintptr_t realm = 0, data = 0, tables = 0, statuses = 0;
            return ReadPtr(global, &realm) && realm >= kMinPointer &&
                   ReadPtr(realm, &data) && data >= kMinPointer &&
                   ReadPtr(data + 8, &tables) && tables >= kMinPointer &&
                   ReadPtr(tables + 0x10, &statuses) && statuses >= kMinPointer &&
                   Read16(statuses + 0xA0, status) && *status != 0xFFFF;
        }

        uintptr_t ResolveEnemyHealthEntry(uintptr_t targetOwner, uint16_t healthStatus)
        {
            // Dispatcher arg1 is strictly the stat root. Native 141719490 maps the
            // status ID to [root+58]+index*90; 14171E696/14171E6C4 verifies count+60 and
            // the entry's WORD ID. Do not confuse it with an int32 legacy type tag.
            uintptr_t array = 0, found = 0;
            uint32_t count = 0;
            if (targetOwner < kMinPointer || !ReadPtr(targetOwner + 0x58, &array) ||
                !mem::IsValidUserPtr(array) || !Read32(targetOwner + 0x60, &count) ||
                count == 0 || count > 256 ||
                array > mem::kMaxPointer - static_cast<uintptr_t>(count) * 0x90)
                return 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uintptr_t entry = array + static_cast<uintptr_t>(i) * 0x90;
                uint16_t id = 0;
                if (!Read16(entry, &id)) return 0;
                if (id != healthStatus) continue;
                if (found) return 0; // Ambiguous identity is not permission to amplify.
                found = entry;
            }
            return found;
        }

        damage_policy::HealthState ReadDamageHealthState(uintptr_t root, uint16_t healthStatus)
        {
            damage_policy::HealthState state{};
            if (!DamageContract().targetLayoutVerified) return state;
            uintptr_t engine = 0, entity = 0, actor = 0, marker = 0, back = 0, protection = 0;
            // Native: engine=*root; entity=engine[8] (141717941..794D).
            // Callers 141FE7542..754A / 141FE7FC2..7FD9 walk the return chain below.
            if (!ReadPtr(root, &engine) || engine < kMinPointer ||
                !ReadPtr(engine + 8, &entity) || entity < kMinPointer ||
                !ReadPtr(entity + kOff_Owner_Actor, &actor) || actor < kMinPointer ||
                !ReadPtr(actor + kOff_Actor_StatusMarker, &marker) || marker != engine ||
                !ReadPtr(marker + kOff_Marker_TargetOwner, &back) || back != root)
                return state;
            if (IsPlayerEntity(entity) || IsMountEntity(entity)) return state;
            uint8_t state272 = 0, state273 = 0, blocked398 = 0, blocked18 = 0;
            // 141717A0D..A3F: negative HP is rejected by engine[3F8]->18 or
            // engine[398], and engine[273] gates application unless a6 permits it.
            // 141717BC9/C64 write WORD engine[272]=1/100: these are transition
            // states, NOT proven no-death flags. Conservatively preserve either state.
            if (!Read8(engine + 0x272, &state272) || !Read8(engine + 0x273, &state273) ||
                !Read8(engine + 0x398, &blocked398) ||
                !ReadPtr(engine + 0x3F8, &protection) || protection < kMinPointer ||
                !Read8(protection + 0x18, &blocked18))
                return state;
            state.nativeProtected = state272 || state273 || blocked398 || blocked18;
            const uintptr_t entry = ResolveEnemyHealthEntry(root, healthStatus);
            state.verified = entry >= kMinPointer &&
                Read64(entry + kOff_StatEntry_Current, &state.current) &&
                Read64(entry + kOff_StatEntry_Floor, &state.floor);
            return state;
        }

        // Incoming only. Outgoing must pass the policy's status/source/protection gates.
        int64_t ScaleDamage(int64_t delta)
        {
            const State& st = State::Get();
            if (st.godMode) return 0;
            if (st.dmgInMult != 1.0f)
            {
                const double scaled = static_cast<double>(delta) * static_cast<double>(st.dmgInMult);
                return static_cast<int64_t>(scaled);
            }
            return delta;
        }

        // --- Hook 2: pa_StatApplyDelta (Damage dispatcher - 11 args) --------
        int64_t ApplyClassifiedDamage(void* targetOwner, uint16_t statusId,
                                         int64_t time, int64_t delta, uintptr_t sourceCtx,
                                         char a6, char a7, char a8, char a9, char a10,
                                         void* out, const damage_policy::HitContext& context)
        {
            if (!oDamageApply) return 0;
            const State& st = State::Get();
            const uintptr_t owner = reinterpret_cast<uintptr_t>(targetOwner);
            if (owner < kMinPointer)
            {
                return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
            }

            // Resolve only in the bounded Tick path, never recursively per delta.
            // CRITICAL DEFENSE FOR LOADING SCREEN / WORLD LOAD:
            // If player is not yet loaded in the world, NEVER modify any deltas or treat
            // entities as enemies! All deltas during loading are internal engine setups.
            const uintptr_t mainPlayer = g_owners[0].load(std::memory_order_relaxed);
            bool clientMount = false;
            const bool clientTarget = ClientStatEntry(0, owner, &clientMount);
            if (mainPlayer < kMinPointer && !clientTarget)
            {
                return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
            }

            const bool isPlayerTarget  = IsPlayerEntity(owner) || (clientTarget && !clientMount);
            const bool isMountTarget   = IsMountEntity(owner) || (clientTarget && clientMount);
            const bool isEnemyTarget   = !isPlayerTarget && !isMountTarget;
            const bool isEnemyAttacker = (sourceCtx >= kMinPointer && !IsPlayerEntity(sourceCtx));
            uint16_t healthStatus = 0;
            const bool healthStatusKnown = ResolveDamageHealthStatus(&healthStatus);
            // Legacy enum is only a fallback for existing incoming features. Outgoing
            // amplification requires the native dispatcher ID to resolve successfully.
            const bool isHealth = statusId == (healthStatusKnown ? healthStatus : StatType_Health);

            // ---------------------------------------------------------------
            // A. OUTGOING DAMAGE: Player hitting Enemy
            // ---------------------------------------------------------------
            if (isEnemyTarget)
            {
                if ((st.oneHitKill || st.dmgOutMult != 1.0f) && healthStatusKnown && isHealth &&
                    delta < 0 && sourceCtx == mainPlayer && PossessorRoundTrip(mainPlayer))
                {
                    // 141719901 -> HP r9 -> [rbp+118] -> 141717CCE: source is
                    // dereferenced at +60/+88/+68, i.e. entity/owner, not an action
                    // descriptor. Do not credit nulls or guessed weapon wrappers.
                    damage_policy::OutgoingHit hit{};
                    hit.statusId = statusId;
                    hit.healthStatusId = healthStatus;
                    hit.healthStatusKnown = healthStatusKnown;
                    hit.source = sourceCtx;
                    hit.verifiedPlayerOwner = mainPlayer;
                    hit.victimRoot = owner;
                    hit.time = time;
                    hit.context = context;
                    hit.enemyTarget = true;
                    hit.flags = {static_cast<uint8_t>(a6), static_cast<uint8_t>(a7),
                                 static_cast<uint8_t>(a8), static_cast<uint8_t>(a9), static_cast<uint8_t>(a10)};
                    hit.health = ReadDamageHealthState(owner, healthStatus);
                    delta = damage_policy::ApplyOutgoing(hit, delta, st.oneHitKill, st.dmgOutMult);
                }
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
                if (isHealth && isPlayerTarget && st.easyParry && delta < 0 && isEnemyAttacker && IsPlayerHoldingGuard())
                {
                    delta = 0;
                    a6 = 2; // Perfect parry
                    a7 = 1; // Counter stagger (engine deflects sourceCtx natively)
                    return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
                }

                // 3. EASY EVADE (Perfect Dodge):
                if (isHealth && isPlayerTarget && st.easyEvade && delta < 0 && IsPlayerHoldingEvade())
                {
                    delta = 0;
                    a6 = 3; // Perfect evade
                    a7 = 0;
                    return oDamageApply(targetOwner, statusId, time, delta, sourceCtx, a6, a7, a8, a9, a10, out);
                }

                if (isHealth)
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
                        delta = ScaleDamage(delta);
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

        int64_t __fastcall hkDamageApply(void* targetOwner, uint16_t statusId,
                                         int64_t time, int64_t delta, uintptr_t sourceCtx,
                                         char a6, char a7, char a8, char a9, char a10, void* out)
        {
            const auto* previous = g_damageContext;
            // Trivially destructible snapshot; no C++ unwinding objects in SEH wrapper.
            const damage_policy::HitContext context = damage_policy::ContextForDispatch(
                previous, reinterpret_cast<uintptr_t>(_ReturnAddress()), g_damageEventHpReturn);
            // Only 141FE7FD9's direct HP call can consume this context. Suspend it
            // through the original dispatcher so callbacks cannot borrow permission.
            __try
            {
                g_damageContext = nullptr;
                return ApplyClassifiedDamage(targetOwner, statusId, time, delta, sourceCtx,
                                              a6, a7, a8, a9, a10, out, context);
            }
            __finally { g_damageContext = previous; }
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

        if (!InstallDamageEventHook())
            LOG_WARN("player: per-hit damage contract unavailable - outgoing damage left native.");

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
        static std::atomic_flag busy = ATOMIC_FLAG_INIT;
        if (busy.test_and_set(std::memory_order_acquire)) return;
        __try { TickImpl(); }
        __finally { busy.clear(std::memory_order_release); }
    }

    void Player::TickImpl()
    {
        const State& st = State::Get();
        // Just-window assist input tracking must run EVERY frame (a dodge tap
        // is shorter than the resolve interval); cheap key/gamepad polls.
        if (st.easyParry || st.easyEvade)
            PollCombatInputTaps();

        static ULONGLONG s_lastResolve = 0;
        const ULONGLONG now = GetTickCount64();
        // Finite 64-cell discovery slices; a longer interval alone only moves
        // an exhaustive scan's hitch to a different frame.
        const ULONGLONG interval = 100;
        if (now - s_lastResolve >= interval)
        {
            s_lastResolve = now;
            TickResolveSelf();
        }

        RefreshClientStats();

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
        AcquireSRWLockExclusive(&g_clientStatLock);
        for (auto& source : g_clientStatSources) source = {};
        g_clientStatRegistry = {};
        ReleaseSRWLockExclusive(&g_clientStatLock);
        mem::RemoveHook(&g_commitTarget);
        mem::RemoveHook(&g_damageHookTarget);
        mem::RemoveHook(&g_damageEventTarget);
        g_damageEventHpReturn = 0;
        g_damageBuffVtable = 0;
        g_damageDefinition = nullptr;
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
        if (index < 0 || index >= kMaxPartyPlayers) return 0;
        return g_characterActors[index].load(std::memory_order_acquire);
    }

    uintptr_t Player::GetOwner(int index)
    {
        if (index < 0 || index >= kMaxPartyPlayers) return 0;
        return g_characterOwners[index].load(std::memory_order_acquire);
    }

    uintptr_t Player::GetControlledOwner()
    {
        return g_owners[0].load(std::memory_order_acquire);
    }

    int Player::GetTrackedPlayerCount()
    {
        int count = 0;
        for (int i = 0; i < kMaxPartyPlayers; ++i)
        {
            if (g_characterActors[i].load(std::memory_order_acquire) >= kMinPointer)
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
        // Called only by the nonblocking profile worker below. A missing
        // signature must not turn a negative profile lookup into an image scan.
        static uintptr_t s_coreGlobal = 0;
        static bool s_triedPattern = false;
        if (s_coreGlobal) return s_coreGlobal;
        if (g_charMgrGlobal >= kMinPointer + 0x528)
        {
            s_coreGlobal = g_charMgrGlobal - 0x528;
            return s_coreGlobal;
        }
        if (!s_triedPattern)
        {
            s_triedPattern = true;
            const uintptr_t m = mem::FindPattern(kSig_GameCoreGlobal);
            if (m) s_coreGlobal = mem::ResolveRipAt(m, 7);
        }
        return s_coreGlobal;
    }

    static int NativeProfileParty(uintptr_t owner)
    {
        // TU 2.02 +50 is an entity ID, including on actor containers.
        // Values 1..3 on unrelated nested storage cannot establish identity.
        if (core::GetGameVersion().revision >= 2800) return -1;
        uint32_t party = 0;
        return IsEquipmentPointer(owner) && Read32(owner + kOff_Owner_PartyIndex, &party) &&
            party >= 1 && party <= kMaxPartyPlayers ? static_cast<int>(party - 1) : -1;
    }

    static int NativeProfileRootParty(uintptr_t owner)
    {
        const int party = NativeProfileParty(owner);
        if (party >= 0) return party;
        uintptr_t inner = 0;
        return IsEquipmentPointer(owner) && ReadPtr(owner + kOff_Owner_Actor, &inner) && inner != owner ?
            NativeProfileParty(inner) : -1;
    }

    static uintptr_t FindProfileEquipComp(uintptr_t root, int index)
    {
        if (!IsEquipmentPointer(root) || index < 0 || index >= kMaxPartyPlayers) return 0;

        auto checkComp = [index](uintptr_t comp) -> bool {
            if (!ReadNativeEquipmentTable(comp).valid) return false;
            const int identity = Inventory::IdentifyCharacterFromComp(comp);
            // The profile/tracked root already establishes the character.
            return identity < 0 || identity == index;
        };

        // Only the native fixed routes. ReadNativeEquipmentTable revalidates
        // RTTI, the owning-actor backlink and the current table on every use.
        if (checkComp(root)) return root;
        uintptr_t comp = 0;
        if (ReadPtr(root + kOff_Sub_EquipComp, &comp) && checkComp(comp))
            return comp;

        // root + 0x68 -> sub -> +0x38
        uintptr_t sub = 0;
        if (ReadPtr(root + kOff_Container_Sub, &sub) && IsEquipmentPointer(sub))
        {
            if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && checkComp(comp))
                return comp;
        }

        // root + 0x68 -> inner actor -> +0x68 -> sub -> +0x38
        uintptr_t inner = 0;
        if (ReadPtr(root + kOff_Owner_Actor, &inner) && IsEquipmentPointer(inner) && inner != root)
        {
            if (ReadPtr(inner + kOff_Container_Sub, &sub) && IsEquipmentPointer(sub))
            {
                if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && checkComp(comp))
                    return comp;
            }
        }
        return 0;
    }

    enum class ProfileSource { None, CoreCell, CoreRoot, Manager };

    struct ProfileWorld
    {
        uintptr_t coreGlobal = 0, core = 0, controlledOwner = 0;
        uintptr_t managerLink = 0, manager = 0, data = 0;
        uint32_t count = 0;
        uintptr_t trackedOwner[kMaxPartyPlayers]{};
        uintptr_t trackedActor[kMaxPartyPlayers]{};
    };

    struct ProfileDiscovery
    {
        uint64_t generation = 0;
        uintptr_t owner = 0, actor = 0, comp = 0, nativeOwner = 0;
        uintptr_t trackedOwner = 0, trackedActor = 0;
        ProfileSource source = ProfileSource::None;
        uintptr_t rootCell = 0, root = 0, ownerCell = 0;
        uintptr_t manager = 0, managerLink = 0;
        uint32_t listIndex = 0;
    };

    constexpr unsigned kProfileScanBudget = 64;
    constexpr ULONGLONG kProfileRetryMs = 100;
    constexpr unsigned kProfileRootCount = 0x50 / 8 + 1;
    constexpr unsigned kProfileRootEntries = 0x300 / 8;
    constexpr unsigned kProfileCoreCells = kProfileRootCount * (kProfileRootEntries + 1);

    struct ProfileScanCursor
    {
        unsigned coreIndex = 0;
        uint32_t managerIndex = 0;
        uintptr_t managerData = 0;
        bool managerTurn = false;
    };

    struct ProfileCache
    {
        ProfileWorld world{};
        uint64_t generation = 0;
        ProfileDiscovery results[kMaxPartyPlayers]{};
        ULONGLONG nextTrackedProbe[kMaxPartyPlayers]{};
        ULONGLONG nextScan = 0;
        ProfileScanCursor cursor{};
    };

    static SRWLOCK s_profileCacheMutex = SRWLOCK_INIT;
    static ProfileCache s_profileCache{};
    static std::atomic_flag s_profileWorker = ATOMIC_FLAG_INIT;

    struct ProfileWorkerRelease
    {
        ~ProfileWorkerRelease() { s_profileWorker.clear(std::memory_order_release); }
    };

    static ProfileWorld ReadProfileWorld()
    {
        ProfileWorld world{};
        world.coreGlobal = ResolveCoreGlobal();
        if (!ReadPtr(world.coreGlobal, &world.core) || !IsEquipmentPointer(world.core)) world.core = 0;
        world.controlledOwner = Player::GetControlledOwner();
        for (int i = 0; i < kMaxPartyPlayers; ++i)
        {
            world.trackedOwner[i] = Player::GetOwner(i);
            world.trackedActor[i] = Player::GetActor(i);
        }
        if (ReadPtr(g_charMgrGlobal, &world.managerLink) && IsEquipmentPointer(world.managerLink) &&
            ReadPtr(world.managerLink, &world.manager) && IsEquipmentPointer(world.manager) &&
            ReadPtr(world.manager + kOff_CharMgr_ListData, &world.data) && IsEquipmentPointer(world.data) &&
            Read32(world.manager + kOff_CharMgr_ListCount, &world.count) && world.count <= kCharList_MaxCount)
            return world;
        world.managerLink = world.manager = world.data = 0;
        world.count = 0;
        return world;
    }

    static bool SameProfileGeneration(const ProfileWorld& a, const ProfileWorld& b)
    {
        return a.coreGlobal == b.coreGlobal && a.core == b.core && a.controlledOwner == b.controlledOwner &&
            a.managerLink == b.managerLink && a.manager == b.manager;
    }

    static bool ProfileSourceCurrent(const ProfileDiscovery& result, const ProfileCache& cache, int index)
    {
        const auto& world = cache.world;
        if (!result.owner || result.generation != cache.generation ||
            result.trackedOwner != world.trackedOwner[index] || result.trackedActor != world.trackedActor[index])
            return false;
        uintptr_t current = 0;
        if (result.source == ProfileSource::CoreCell || result.source == ProfileSource::CoreRoot)
        {
            if (!world.core || !ReadPtr(world.coreGlobal, &current) || current != world.core) return false;
            if (result.source == ProfileSource::CoreRoot &&
                (!ReadPtr(result.rootCell, &current) || current != result.root)) return false;
        }
        else if (result.source == ProfileSource::Manager)
        {
            uint32_t count = 0;
            if (world.managerLink != result.managerLink || world.manager != result.manager || world.data != result.root ||
                !ReadPtr(g_charMgrGlobal, &current) || current != result.managerLink ||
                !ReadPtr(result.managerLink, &current) || current != result.manager ||
                !ReadPtr(result.rootCell, &current) || current != result.root ||
                !Read32(result.manager + kOff_CharMgr_ListCount, &count) || count > kCharList_MaxCount ||
                result.listIndex >= count) return false;
        }
        else return false;
        // Readable old owners are not evidence of membership in the current world.
        return ReadPtr(result.ownerCell, &current) && current == result.owner;
    }

    static bool ValidateProfileDiscovery(ProfileDiscovery& result, const ProfileCache& cache, int index)
    {
        if (!ProfileSourceCurrent(result, cache, index)) return false;
        const int party = NativeProfileRootParty(result.owner);
        if (party >= 0 && party != index) return false;
        uintptr_t actor = 0;
        if (!ReadPtr(result.owner + kOff_Owner_Actor, &actor) || !IsEquipmentPointer(actor)) actor = 0;
        if (result.comp)
        {
            // Also re-walk the fixed route: a valid component owned by some
            // other live actor must not survive a changed profile/owner chain.
            uintptr_t nativeOwner = 0;
            if (FindProfileEquipComp(result.owner, index) != result.comp ||
                !ReadPtr(result.comp + kOff_EquipComp_Owner, &nativeOwner) || nativeOwner != result.nativeOwner)
                return false;
            if (party < 0 && Inventory::IdentifyCharacterFromComp(result.comp) != index) return false;
        }
        else if (party != index) return false;
        result.actor = actor;
        return ProfileSourceCurrent(result, cache, index);
    }

    static void InspectProfileCell(ProfileDiscovery candidate, ProfileCache& cache, int wanted)
    {
        if (!ReadPtr(candidate.ownerCell, &candidate.owner) || !IsEquipmentPointer(candidate.owner)) return;
        int index = NativeProfileRootParty(candidate.owner);
        if (index < 0)
        {
            // Classify every party-less manager candidate independently of the
            // requester. All callers share a scan budget: discarding B while A
            // scans could otherwise starve B indefinitely.
            if (candidate.source != ProfileSource::Manager) return;
            for (int attempt = 0; attempt < kMaxPartyPlayers; ++attempt)
            {
                const int probe = (wanted + attempt) % kMaxPartyPlayers;
                const uintptr_t comp = FindProfileEquipComp(candidate.owner, probe);
                const int identity = comp ? Inventory::IdentifyCharacterFromComp(comp) : -1;
                if (identity >= 0 && identity < kMaxPartyPlayers)
                {
                    index = identity;
                    candidate.comp = comp;
                    break;
                }
            }
            if (index < 0) return;
        }
        if (cache.results[index].comp) return;
        candidate.generation = cache.generation;
        candidate.trackedOwner = cache.world.trackedOwner[index];
        candidate.trackedActor = cache.world.trackedActor[index];
        if (!candidate.comp) candidate.comp = FindProfileEquipComp(candidate.owner, index);
        if (candidate.comp) ReadPtr(candidate.comp + kOff_EquipComp_Owner, &candidate.nativeOwner);
        if (!ValidateProfileDiscovery(candidate, cache, index)) return;
        if (candidate.comp || !cache.results[index].owner) cache.results[index] = candidate;
    }

    static ProfileDiscovery CoreProfileCell(const ProfileWorld& world, uintptr_t rootOffset, uintptr_t entryOffset)
    {
        ProfileDiscovery candidate{};
        if (!world.core) return candidate;
        candidate.source = ProfileSource::CoreRoot;
        candidate.rootCell = world.core + rootOffset;
        if (ReadPtr(candidate.rootCell, &candidate.root) && IsEquipmentPointer(candidate.root))
            candidate.ownerCell = candidate.root + entryOffset;
        return candidate;
    }

    static ProfileDiscovery DiscoverProfileSlice(ProfileCache& cache, int index, ULONGLONG now)
    {
        auto& result = cache.results[index];
        const auto& world = cache.world;
        // Probe tracked native chains first, but only publish them with a
        // still-current core/manager owner cell. Published atomics alone can
        // lag a load and are not sufficient cache provenance.
        if (now >= cache.nextTrackedProbe[index])
        {
            cache.nextTrackedProbe[index] = now + kProfileRetryMs;
            const uintptr_t owner = world.trackedOwner[index];
            const uintptr_t actor = world.trackedActor[index];
            uintptr_t trackedRoot = owner, trackedComp = 0;
            const int party = NativeProfileRootParty(owner);
            if (party < 0 || party == index)
            {
                trackedComp = FindProfileEquipComp(owner, index);
                const int actorParty = NativeProfileRootParty(actor);
                if (!trackedComp && IsEquipmentPointer(actor) &&
                    (actorParty < 0 || actorParty == index))
                {
                    trackedComp = FindProfileEquipComp(actor, index);
                    trackedRoot = actor;
                }
            }
            if (trackedComp && (result.owner == trackedRoot || result.owner == owner) &&
                ProfileSourceCurrent(result, cache, index))
            {
                ProfileDiscovery tracked = result;
                tracked.comp = trackedComp;
                ReadPtr(tracked.comp + kOff_EquipComp_Owner, &tracked.nativeOwner);
                if (ValidateProfileDiscovery(tracked, cache, index)) result = tracked;
            }
            // An owner-only hit is also a bounded negative equipment cache.
            // Recheck its fixed chain on the next retry, not a whole scan lap.
            if (!result.comp && ProfileSourceCurrent(result, cache, index))
            {
                ProfileDiscovery retry = result;
                retry.comp = FindProfileEquipComp(retry.owner, index);
                if (retry.comp && ReadPtr(retry.comp + kOff_EquipComp_Owner, &retry.nativeOwner) &&
                    ValidateProfileDiscovery(retry, cache, index)) result = retry;
            }
        }

        if (!ValidateProfileDiscovery(result, cache, index))
        {
            // Preserve the owner-only API when a native table is temporarily
            // absent, but never keep equipment from a failed validation.
            result.comp = result.nativeOwner = 0;
            if (!ValidateProfileDiscovery(result, cache, index)) result = {};
        }
        if (result.comp || now < cache.nextScan) return result;
        cache.nextScan = now + kProfileRetryMs;

        // Both native hints count against the SAME budget as the rolling scan.
        unsigned scanned = 0;
        InspectProfileCell(CoreProfileCell(world, 0x30, 0x50), cache, index);
        ++scanned;
        InspectProfileCell(CoreProfileCell(world, 0x00, 0xF0), cache, index);
        ++scanned;

        auto& cursor = cache.cursor;
        if (cursor.managerData != world.data)
        {
            cursor.managerData = world.data;
            cursor.managerIndex = 0;
        }
        // Alternate domains so a large manager never starves core profiles,
        // nor does a missing core profile postpone the manager until next lap.
        while (!result.comp && scanned < kProfileScanBudget)
        {
            ProfileDiscovery candidate{};
            cursor.managerTurn = !cursor.managerTurn;
            if (cursor.managerTurn && world.count)
            {
                if (cursor.managerIndex >= world.count) cursor.managerIndex = 0;
                candidate.source = ProfileSource::Manager;
                candidate.manager = world.manager;
                candidate.managerLink = world.managerLink;
                candidate.rootCell = world.manager + kOff_CharMgr_ListData;
                candidate.root = world.data;
                candidate.listIndex = cursor.managerIndex++;
                candidate.ownerCell = world.data + uintptr_t{8} * candidate.listIndex;
            }
            else
            {
                if (cursor.coreIndex >= kProfileCoreCells) cursor.coreIndex = 0;
                const unsigned cell = cursor.coreIndex++;
                const uintptr_t rootOffset = (cell / (kProfileRootEntries + 1)) * uintptr_t{8};
                const unsigned entry = cell % (kProfileRootEntries + 1);
                if (entry) candidate = CoreProfileCell(world, rootOffset, (entry - 1) * uintptr_t{8});
                else if (world.core)
                {
                    candidate.source = ProfileSource::CoreCell;
                    candidate.rootCell = world.coreGlobal;
                    candidate.root = world.core;
                    candidate.ownerCell = world.core + rootOffset;
                }
            }
            ++scanned;
            InspectProfileCell(candidate, cache, index);
        }
        if (!ValidateProfileDiscovery(result, cache, index)) result = {};
        return result;
    }

    static ProfileDiscovery ResolveProfileDiscovery(int index)
    {
        if (index < 0 || index >= kMaxPartyPlayers || s_profileWorker.test_and_set(std::memory_order_acquire)) return {};
        ProfileWorkerRelease release;
        // SRWLOCK is a try-only mutex for POD snapshots/publication. In
        // particular, Inventory identity and all engine reads run UNLOCKED.
        // The worker lease prevents concurrent or reentrant duplicate scans.
        ProfileCache cache{};
        if (!TryAcquireSRWLockExclusive(&s_profileCacheMutex)) return {};
        cache = s_profileCache;
        ReleaseSRWLockExclusive(&s_profileCacheMutex);

        const ProfileWorld world = ReadProfileWorld();
        if (!SameProfileGeneration(cache.world, world))
        {
            const uint64_t generation = cache.generation + 1;
            const ULONGLONG nextScan = cache.nextScan;
            cache = {};
            cache.generation = generation;
            cache.nextScan = nextScan; // Transitions must not multiply the scan budget.
        }
        for (int i = 0; i < kMaxPartyPlayers; ++i)
        {
            if (cache.world.trackedOwner[i] != world.trackedOwner[i] || cache.world.trackedActor[i] != world.trackedActor[i])
            {
                cache.results[i] = {};
                cache.nextTrackedProbe[i] = 0;
            }
        }
        cache.world = world;
        ProfileDiscovery result = DiscoverProfileSlice(cache, index, GetTickCount64());
        const ProfileWorld current = ReadProfileWorld();
        bool changed = !SameProfileGeneration(world, current) || world.data != current.data || world.count != current.count;
        for (int i = 0; i < kMaxPartyPlayers; ++i)
            changed |= world.trackedOwner[i] != current.trackedOwner[i] || world.trackedActor[i] != current.trackedActor[i];
        if (changed)
        {
            // Publish the consumed budget even when the engine changes during
            // discovery. The next call sees the old stamp and clears as needed.
            result = {};
            for (auto& entry : cache.results) entry = {};
        }
        else if (result.owner && !ProfileSourceCurrent(result, cache, index))
            result = cache.results[index] = {};

        if (!TryAcquireSRWLockExclusive(&s_profileCacheMutex)) return {};
        s_profileCache = cache;
        ReleaseSRWLockExclusive(&s_profileCacheMutex);
        return result;
    }

    uintptr_t Player::GetProfileOwner(int index)
    {
        return ResolveProfileDiscovery(index).owner;
    }

    uintptr_t Player::GetProfileActor(int index)
    {
        return ResolveProfileDiscovery(index).actor;
    }

    uintptr_t Player::GetProfileEquipComp(int index)
    {
        return ResolveProfileDiscovery(index).comp;
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
