#include "equipment.h"
#include "equipment_logic.h"
#include "equipment_table.h"
#include "socket_layout.h"
#include "inventory_scan.h"
#include "equipment_edit_scan.h"

#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <future>
#include <chrono>
#include <mutex>
#include <imgui.h>

#include "offsets.h"
#include "player.h"
#include "inventory.h"
#include "dye.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../core/logger.h"
#include "../core/state.h"
#include "../core/version_detect.h"

// The equipment editor. socket_layout.h owns the verified native socket layout;
// historical socket marker/state comments in offsets.h are not used here.
//
//   Component walk  -> each realm's equip component, straight off that realm's
//                      player character (*(*(actor+0x68)+0x38)) - the same walk
//                      the dye editor uses, and self-validating via comp+0x08.
//   Socket record   -> a 6-byte entry in a validated, constructed socket vector.
//                      Never manufacture a size/capacity or guess another layout.
//   Refinement      -> bounded requests consumed on the game thread, with the
//                      original character/item/component stamp revalidated.
//
// See equipment.h for what is durable (add/clear) and what is live-only (unlock).

namespace trinity::game
{
    namespace
    {
        using namespace trinity::mem;

        // Retained for older builds only. The TU 2.02 signature's native
        // contract is unverified; that build must use normal engine recompute.
        using EquipRefresh_t = void* (__fastcall*)(void*, int*);
        EquipRefresh_t    g_refresh = nullptr; // sub_7C88A0
        std::atomic<bool> g_dirty{ false };
        std::atomic<uint64_t> g_snapshotEpoch{ 1 };

        void InvalidateSnapshot()
        {
            g_snapshotEpoch.fetch_add(1, std::memory_order_release);
        }

        inline bool IsValidCanonicalPtr(uintptr_t p)
        {
            return IsEquipmentPointer(p);
        }

        EquipTableDesc ReadEquipTableDesc(uintptr_t comp)
        {
            return ReadNativeEquipmentTable(comp);
        }

        // --- Each realm's equip component, by walk (mirrors dye.cpp) ----------
        bool CompValid(uintptr_t comp)
        {
            if (!IsValidCanonicalPtr(comp)) return false;
            const EquipTableDesc tbl = ReadEquipTableDesc(comp);
            return tbl.valid && tbl.count > 0 && tbl.count <= 64;
        }

        // rootIdx permits unidentified gear only on a trusted per-character root.
        // Reject mismatched candidates here so the rest of the walk still runs.
        uintptr_t FindEquipCompFromActor(uintptr_t actor, int targetIdx = -1, int rootIdx = -1)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            uintptr_t seen[2] = {};
            int nSeen = 0;
            auto accept = [&](uintptr_t candidate) {
                if (!IsValidCanonicalPtr(candidate)) return false;
                for (int i = 0; i < nSeen; ++i)
                    if (seen[i] == candidate) return false;
                if (nSeen < 2) seen[nSeen++] = candidate;
                return CompValid(candidate) && (targetIdx < 0 ||
                    AcceptCharacterComponent(targetIdx,
                        Inventory::IdentifyCharacterFromComp(candidate), rootIdx));
            };

            // 1. Direct component at actor + 0x38 (actor is SubContainer)
            uintptr_t comp = 0;
            if (ReadPtr(actor + kOff_Sub_EquipComp, &comp) && accept(comp))
                return comp;

            // 2. Standard character / mount container walk (*(*(actor+0x68)+0x38))
            uintptr_t sub = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && IsValidCanonicalPtr(sub))
            {
                if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && accept(comp))
                    return comp;
            }

            return 0;
        }

        uintptr_t CompForCharacter(uintptr_t actor, int targetIdx = -1, int rootIdx = -1)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            // 1. Direct actor check
            uintptr_t comp = FindEquipCompFromActor(actor, targetIdx, rootIdx);
            if (comp) return comp;

            // 2. If actor is an owner object, inspect inner actor (+0x68)
            uintptr_t innerAct = 0;
            if (ReadPtr(actor + kOff_Owner_Actor, &innerAct) && IsValidCanonicalPtr(innerAct) && innerAct != actor)
            {
                comp = FindEquipCompFromActor(innerAct, targetIdx, rootIdx);
                if (comp) return comp;
            }

            return 0;
        }

        uintptr_t FindTrackedCharacterComp(int targetIdx)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            for (int pass = 0; pass < 3; ++pass)
            {
                const int p = (targetIdx + pass) % 3;
                const uintptr_t roots[] = { Player::GetOwner(p), Player::GetActor(p) };
                for (int r = 0; r < 2; ++r)
                {
                    if (!roots[r] || (r == 1 && roots[r] == roots[0])) continue;
                    if (const uintptr_t comp = CompForCharacter(roots[r], targetIdx, p))
                        return comp;
                }
            }
            return 0;
        }

        static std::atomic<int> s_activeCharIdx{ -1 }; // -1 = auto-detect active player character

        uintptr_t ProfileComp(int targetIdx, uintptr_t* candidate = nullptr, int* identity = nullptr)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            const uintptr_t comp = Player::GetProfileEquipComp(targetIdx);
            if (candidate) *candidate = comp;
            if (!CompValid(comp)) return 0;
            const int id = Inventory::IdentifyCharacterFromComp(comp);
            if (identity) *identity = id;
            return AcceptCharacterComponent(targetIdx, id, targetIdx) ? comp : 0;
        }

        uintptr_t FindCharacterFallback(int targetIdx, bool logFailure = false, int liveIdx = -1)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                return comp;
            // Player owns the cached profile/manager search. Do not repeat the
            // exhaustive CharacterAddrs walk on the menu's failed-read path.
            uintptr_t profileCandidate = 0;
            int profileId = -1;
            const uintptr_t profile = ProfileComp(targetIdx, &profileCandidate, &profileId);
            if (!profile && logFailure)
            {
                static ULONGLONG lastLog[3] = {};
                const ULONGLONG now = GetTickCount64();
                if (!lastLog[targetIdx] || now - lastLog[targetIdx] >= 10000)
                {
                    lastLog[targetIdx] = now;
                    LOG_WARN("equipment: Ready failed selected=%d live=%d active=%d profile=%p profileId=%d (no accepted table)",
                        targetIdx, liveIdx, Player::GetActiveCharacterIdx(),
                        reinterpret_cast<void*>(profileCandidate), profileId);
                }
            }
            return profile;
        }

        // Prefer the live realm walk; off-screen selections never use its capture.
        uintptr_t ClientComp(bool logFailure = false, int selectedIndex = -1)
        {
            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int selected = selectedIndex >= 0 ? selectedIndex : s_activeCharIdx.load(std::memory_order_acquire);
            const int targetIdx = selected < 0 ? liveIdx : selected;
            if (targetIdx < 0 || targetIdx > 2) return 0;

            if (targetIdx == liveIdx)
            {
                const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                if (const uintptr_t comp = CompForCharacter(liveChar, targetIdx, liveIdx))
                    return comp;

                const int liveActiveIdx = Player::GetActiveCharacterIdx();
                if (liveActiveIdx >= 0 && liveActiveIdx < 3)
                {
                    const uintptr_t owner = Player::GetOwner(liveActiveIdx);
                    const uintptr_t actor = Player::GetActor(liveActiveIdx);
                    if (owner != liveChar)
                        if (const uintptr_t comp = CompForCharacter(owner, targetIdx, liveActiveIdx))
                            return comp;
                    if (actor != owner && actor != liveChar)
                        if (const uintptr_t comp = CompForCharacter(actor, targetIdx, liveActiveIdx))
                            return comp;
                }

                const uintptr_t h = Inventory::ClientHolderAddr();
                if (h)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(h + 8, &owner) && owner >= kMinPointer)
                    {
                        if (const uintptr_t comp = CompForCharacter(owner, targetIdx, liveIdx))
                            return comp;
                    }
                }

                const uintptr_t hooked = Dye::HookedClientComp();
                if (CompValid(hooked))
                {
                    uintptr_t hookedOwner = 0;
                    const bool ownerKnown =
                        ReadPtr(hooked + kOff_EquipComp_Owner, &hookedOwner);
                    if (!liveChar || (ownerKnown && hookedOwner == liveChar))
                    {
                        if (AcceptCharacterComponent(targetIdx,
                            Inventory::IdentifyCharacterFromComp(hooked),
                            liveChar && ownerKnown && hookedOwner == liveChar ? liveIdx : -1))
                            return hooked;
                    }
                }
            }
            return FindCharacterFallback(targetIdx, logFailure, liveIdx);
        }

        // Server-authority mirror with the same strict routing as dye.cpp.
        uintptr_t ServerComp(int selectedIndex = -1)
        {
            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int selected = selectedIndex >= 0 ? selectedIndex : s_activeCharIdx.load(std::memory_order_acquire);
            const int targetIdx = selected < 0 ? liveIdx : selected;
            if (targetIdx < 0 || targetIdx > 2) return 0;

            if (targetIdx == liveIdx)
            {
                const uintptr_t serverChar = Inventory::ServerCharacterAddr();
                if (const uintptr_t comp = CompForCharacter(serverChar, targetIdx, liveIdx))
                    return comp;

                const int liveActiveIdx = Player::GetActiveCharacterIdx();
                if (liveActiveIdx >= 0 && liveActiveIdx < 3)
                {
                    const uintptr_t owner = Player::GetOwner(liveActiveIdx);
                    const uintptr_t actor = Player::GetActor(liveActiveIdx);
                    if (owner != serverChar)
                        if (const uintptr_t comp = CompForCharacter(owner, targetIdx, liveActiveIdx))
                            return comp;
                    if (actor != owner && actor != serverChar)
                        if (const uintptr_t comp = CompForCharacter(actor, targetIdx, liveActiveIdx))
                            return comp;
                }
            }
            return FindCharacterFallback(targetIdx);
        }

        bool IsDummyOrUnarmed(uint16_t typeId, const char* name)
        {
            if (typeId == 0 || typeId == kInvSlot_EmptyType) return true;
            if (!name || !*name) return false;
            if (strstr(name, "Ordinary Gloves") || strstr(name, "Ordinary_Gloves") ||
                strstr(name, "OrdinaryGloves") || strstr(name, "Unarmed") ||
                strstr(name, "Bare Hands") || strstr(name, "BareHands") ||
                strstr(name, "Default Weapon") || strstr(name, "Dummy"))
            {
                return true;
            }
            return false;
        }

        // The TrItemValue copy the component keeps for the equipped slot `tag`.
        uintptr_t FindEntryByTag(uintptr_t comp, uint16_t tag, bool validateName = true)
        {
            const EquipTableDesc tbl = ReadEquipTableDesc(comp);
            if (!tbl.valid) return 0;

            for (uint32_t i = 0; i < tbl.count; ++i)
            {
                const uintptr_t entry = tbl.array + static_cast<uintptr_t>(i) * tbl.stride;
                uint16_t t = 0;
                if (!Read16(entry + tbl.tagOffset, &t) || t != tag) continue;
                uint16_t tid = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;

                int64_t qty = 1;
                Read64(entry + kOff_InvSlot_Quantity, &qty);
                int64_t inst = 1;
                Read64(entry + kOff_ItemVal_InstanceId, &inst);

                if (validateName)
                {
                    char itemName[96] = "";
                    Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
                    if (IsDummyOrUnarmed(tid, itemName)) continue;
                }

                return entry;
            }
            return 0;
        }

        // Batch edit enumeration is a fresh native read, never the UI snapshot.
        int ReadEditTags(uint16_t (&tags)[64])
        {
            const EquipTableDesc table = ReadEquipTableDesc(ClientComp());
            if (!table.valid) return 0;
            int count = 0;
            for (uint32_t i = 0; i < table.count; ++i)
            {
                const uintptr_t entry = table.array + static_cast<uintptr_t>(i) * table.stride;
                uint16_t type = 0, tag = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &type)) return 0;
                if (!type || type == kInvSlot_EmptyType) continue;
                if (!Read16(entry + table.tagOffset, &tag)) return 0;
                tags[count++] = tag;
            }
            return count;
        }

        // A copied UI row is a comparison token, never a source of live write
        // pointers. This helper takes no snapshot lock and performs no resolve;
        // callers already obtained the entry through native RTTI/table checks.
        bool DisplayedTargetMatches(uintptr_t entry, const Equipment::SlotInfo* expected)
        {
            if (!expected) return true; // fresh native/batch operation
            uint16_t typeId = 0;
            int64_t instanceId = 0;
            const bool matches = expected->characterIndex >= 0 && expected->characterIndex < 3 &&
                expected->characterIndex == Equipment::GetActiveCharacter() &&
                IsValidCanonicalPtr(expected->controlledOwner) &&
                expected->controlledOwner == Player::GetControlledOwner() &&
                expected->instanceId > 0 && expected->typeId != 0 && expected->typeId != kInvSlot_EmptyType &&
                Read16(entry + kOff_InvSlot_TypeId, &typeId) && typeId == expected->typeId &&
                Read64(entry + kOff_ItemVal_InstanceId, &instanceId) && instanceId == expected->instanceId;
            if (!matches) InvalidateSnapshot();
            return matches;
        }

        // --- Socket record access -------------------------------------------
        // Page protection is a bounds check, not an allocation/lifetime proof.
        // Component provenance and vector size/capacity must pass separately.
        bool WritableDataRange(uintptr_t address, size_t bytes)
        {
            return SocketDataRange(address, bytes, true);
        }

        using SocketDesc = SocketLayout;

        uintptr_t SocketDataOffset()
        {
            // Explicit layout selection; an empty modern vector is not legacy.
            return core::GetGameVersion().revision >= 2625
                ? 0x60 : core::GetItemValSocketOffset(); // TU <= 1.16: +0x58; later: +0x60
        }

        SocketDesc ReadSocketDesc(uintptr_t entry)
        {
            return ReadSocketLayout(entry, SocketDataOffset());
        }

        SocketDesc ReadWritableSocketDesc(uintptr_t entry)
        {
            return ReadSocketLayout(entry, SocketDataOffset(), true);
        }

        uint16_t GearAt(const SocketDesc& desc, int i)
        {
            if (!desc.valid || i < 0 || static_cast<uint32_t>(i) >= desc.size) return kSock_Empty;
            const uint16_t gear = desc.records[i].gear;
            return gear == 0 ? kSock_Empty : gear;
        }

        // Raw (floorless) byte access for the TLS realm flag
        bool RawWrite8(uintptr_t addr, uint8_t val)
        {
            if (!addr) return false;
            __try { *reinterpret_cast<volatile uint8_t*>(addr) = val; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Revalidate the constructed native record immediately before writing.
        bool WriteRecord(const SocketDesc& desc, int i, uint16_t gear)
        {
            if (!desc.valid || i < 0 || static_cast<uint32_t>(i) >= desc.size) return false;
            return WriteSocketLayoutRecord(desc.entry, desc.dataOffset, i, gear);
        }

        // Compatibility bridge for inventory callers: validation only, no allocation.
        uintptr_t EnsureSocketVector(uintptr_t entry)
        {
            const SocketDesc desc = ReadWritableSocketDesc(entry);
            return desc.valid && desc.size ? desc.data : 0;
        }

        bool WriteSocketToEntry(uintptr_t entry, int idx, uint16_t gear, const Equipment::SlotInfo* expected = nullptr)
        {
            if (Inventory::IsTransactionActive()) return false;
            const SocketDesc desc = ReadWritableSocketDesc(entry);
            if (!desc.valid || idx < 0 || static_cast<uint32_t>(idx) >= desc.unlocked) return false;
            return DisplayedTargetMatches(entry, expected) && WriteRecord(desc, idx, gear);
        }

        int GetMaxSocketsForTag(uint16_t /*tag*/)
        {
            // UI upper bound only; actual constructed size comes from ReadSocketDesc.
            return kSocket_Max;
        }

        // Unlock only constructed records; never manufacture vector storage.
        bool OpenAllSockets(uintptr_t entry, int maxSock, const Equipment::SlotInfo* expected = nullptr)
        {
            if (Inventory::IsTransactionActive()) return false;
            return DisplayedTargetMatches(entry, expected) && UnlockSocketLayout(entry, SocketDataOffset(), maxSock);
        }

        // Remove every gear from an unlocked socket on one realm's copy, leaving
        // the sockets open (index kept, just emptied).
        bool EmptyAllSockets(uintptr_t entry, const Equipment::SlotInfo* expected = nullptr)
        {
            if (Inventory::IsTransactionActive()) return false;
            const SocketDesc desc = ReadWritableSocketDesc(entry);
            if (!desc.valid || !desc.unlocked) return false;
            bool ok = true;
            for (uint32_t k = 0; k < desc.unlocked; ++k)
                ok &= DisplayedTargetMatches(entry, expected) && WriteRecord(desc, static_cast<int>(k), kSock_Empty);
            return ok;
        }

        bool SyncSocketAllRealms(uint16_t tag, int64_t instId, int idx, uint16_t gear, uint16_t typeId = 0, const Equipment::SlotInfo* expected = nullptr)
        {
            bool ok = false;

            // 1. Client Equip Comp for Active Character & all player actors
            const uintptr_t clientC = ClientComp();
            if (clientC)
            {
                const uintptr_t ce = FindEntryByTag(clientC, tag);
                if (ce) ok |= WriteSocketToEntry(ce, idx, gear, expected);
            }

            // Direct profile component update (authoritative for in-game pause/party menu)
            const uintptr_t profC = ProfileComp(Equipment::GetActiveCharacter());
            if (profC && profC != clientC)
            {
                const uintptr_t pe = FindEntryByTag(profC, tag);
                if (pe) ok |= WriteSocketToEntry(pe, idx, gear, expected);
            }

            // Every realm copy of the SELECTED character only - never the
            // other protagonists' same-tag items.
            uintptr_t sockCopies[16] = {};
            const int sockNCopies = Inventory::CharacterAddrs(Equipment::GetActiveCharacter(), sockCopies, 16);
            for (int i = 0; i < sockNCopies; ++i)
            {
                const int targetIdx = Equipment::GetActiveCharacter();
                const uintptr_t comp = CompForCharacter(sockCopies[i], targetIdx, targetIdx);
                if (comp && comp != clientC && comp != profC)
                {
                    const uintptr_t ce = FindEntryByTag(comp, tag);
                    if (ce) ok |= WriteSocketToEntry(ce, idx, gear, expected);
                }
            }

            // 1b. Companion containers via PartyIndex - same copy the native
            // inspect UI reads; mirror of the Refine fix. Bounded: PartyIndex
            // match first, and stop touching candidates after a write hit.
            {
                const int targetIdx = Equipment::GetActiveCharacter();
                if (targetIdx >= 0 && targetIdx < 3)
                {
                    const uintptr_t charMgrGlobal = Player::GetCharMgrGlobal();
                    uintptr_t p = 0, mgr = 0, data = 0;
                    if (charMgrGlobal >= kMinPointer &&
                        ReadPtr(charMgrGlobal, &p) && p >= kMinPointer &&
                        ReadPtr(p, &mgr) && mgr >= kMinPointer)
                    {
                        uint32_t cCount = 0;
                        if (ReadPtr(mgr + kOff_CharMgr_ListData, &data) && data >= kMinPointer &&
                            Read32(mgr + kOff_CharMgr_ListCount, &cCount) && cCount > 0 && cCount <= kCharList_MaxCount)
                        {
                            for (uint32_t i = 0; i < cCount; ++i)
                            {
                                uintptr_t cand = 0;
                                if (!ReadPtr(data + static_cast<uintptr_t>(i) * 8, &cand) || cand < kMinPointer) continue;
                                uint32_t pIdx = 0;
                                if (!Read32(cand + kOff_Owner_PartyIndex, &pIdx) || pIdx != static_cast<uint32_t>(targetIdx + 1)) continue;
                                const uintptr_t comp = CompForCharacter(cand, targetIdx, targetIdx);
                                if (comp && comp != clientC && comp != profC)
                                {
                                    const uintptr_t ce = FindEntryByTag(comp, tag);
                                    if (ce) ok |= WriteSocketToEntry(ce, idx, gear, expected);
                                }
                            }
                        }
                    }
                }
            }

            // 2. Direct bag slot sync across ALL containers and holders
            struct SockCtx { int idx; uint16_t gear; bool* ok; const Equipment::SlotInfo* expected; };
            SockCtx ctx{ idx, gear, &ok, expected };
            Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                auto* c = reinterpret_cast<SockCtx*>(u);
                if (WriteSocketToEntry(slot, c->idx, c->gear, c->expected))
                    *(c->ok) = true;
            }, &ctx, typeId);

            // 3. Server Realm (Equip Comp + Mirrors) with RealmFlag = 1
            uint8_t oldFlag = 0;
            const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
            if (flagAddr && RawWrite8(flagAddr, 1))
            {
                const uintptr_t serverC = ServerComp();
                if (serverC)
                {
                    const uintptr_t se = FindEntryByTag(serverC, tag);
                    if (se) ok |= WriteSocketToEntry(se, idx, gear, expected);
                }

                if (profC && profC != serverC)
                {
                    const uintptr_t pe = FindEntryByTag(profC, tag);
                    if (pe) ok |= WriteSocketToEntry(pe, idx, gear, expected);
                }

                // Server-realm companion containers via PartyIndex (RealmFlag
                // = 1 active) - same rationale as the Refine fix above.
                {
                    const int targetIdx = Equipment::GetActiveCharacter();
                    if (targetIdx >= 0 && targetIdx < 3)
                    {
                        const uintptr_t charMgrGlobal = Player::GetCharMgrGlobal();
                        uintptr_t p = 0, mgr = 0, data = 0;
                        if (charMgrGlobal >= kMinPointer &&
                            ReadPtr(charMgrGlobal, &p) && p >= kMinPointer &&
                            ReadPtr(p, &mgr) && mgr >= kMinPointer)
                        {
                            uint32_t cCount = 0;
                            if (ReadPtr(mgr + kOff_CharMgr_ListData, &data) && data >= kMinPointer &&
                                Read32(mgr + kOff_CharMgr_ListCount, &cCount) && cCount > 0 && cCount <= kCharList_MaxCount)
                            {
                                for (uint32_t i = 0; i < cCount; ++i)
                                {
                                    uintptr_t cand = 0;
                                    if (!ReadPtr(data + static_cast<uintptr_t>(i) * 8, &cand) || cand < kMinPointer) continue;
                                    uint32_t pIdx = 0;
                                    if (!Read32(cand + kOff_Owner_PartyIndex, &pIdx) || pIdx != static_cast<uint32_t>(targetIdx + 1)) continue;
                                    const uintptr_t comp = CompForCharacter(cand, targetIdx, targetIdx);
                                    if (comp && comp != serverC && comp != profC)
                                    {
                                        const uintptr_t se = FindEntryByTag(comp, tag);
                                        if (se) ok |= WriteSocketToEntry(se, idx, gear, expected);
                                    }
                                }
                            }
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                    auto* c = reinterpret_cast<SockCtx*>(u);
                    *(c->ok) |= WriteSocketToEntry(slot, c->idx, c->gear, c->expected);
                }, &ctx, typeId);

                RawWrite8(flagAddr, oldFlag);
            }

            return ok;
        }

        enum class EquipmentEdit { Refine, Socket, Unlock, ClearSockets };
        struct RefineRequest
        {
            EquipmentEdit edit = EquipmentEdit::Refine;
            int socket = 0;
            uint16_t gear = kSock_Empty;
            int charIndex = -1;
            uint16_t tag = 0;
            uint16_t typeId = 0;
            uint16_t level = 0;
            int64_t instanceId = 0;
            uintptr_t component = 0;
            uintptr_t owner = 0;
            uintptr_t sub = 0;
            uintptr_t controlledOwner = 0;
            uintptr_t entry = 0;
            EquipTableDesc table{};
            ULONGLONG queuedAt = 0;
            ULONGLONG nextAttempt = 0;
            uint64_t serial = 0;
            EquipmentEditScan holders[2]{};
            unsigned nextHolderRealm = 0;
            unsigned attempts = 0;
            unsigned retryDelay = 500;
            const char* reason = "pending";
            bool retryable = true;
            bool clientVerified = false, serverVerified = false;
            bool changedThisVisit = false;
            bool hadWrites = false;
            bool boundSelection = false; // UI request also expires on selection change
        };

        constexpr size_t kMaxRefineRequests = 64; // includes a complete RefineAll batch
        constexpr ULONGLONG kRefineRequestLifetimeMs = 15000;
        std::mutex g_refineMutex;
        std::atomic_flag g_refineApplying = ATOMIC_FLAG_INIT;
        RefineRequest g_refineRequests[kMaxRefineRequests]{};
        size_t g_refineRequestCount = 0;
        std::atomic<uint64_t> g_editSerial{0};
        std::atomic<uint64_t> g_latestEdit[3][32][7]{}; // refine, unlock, five sockets
        std::atomic<Equipment::EditState> g_editState{Equipment::EditState::Idle};

        void ClearRefineRequests()
        {
            std::lock_guard<std::mutex> lock(g_refineMutex);
            g_refineRequestCount = 0;
            if (g_editState.load(std::memory_order_relaxed) == Equipment::EditState::Pending)
                g_editState.store(Equipment::EditState::Failed, std::memory_order_release);
        }

        bool QueueRefine(const RefineRequest& req)
        {
            std::lock_guard<std::mutex> lock(g_refineMutex);
            if (req.charIndex < 0 || req.charIndex > 2 || req.tag >= 32) return false;
            const int first = req.edit == EquipmentEdit::Refine ? 0 : req.edit == EquipmentEdit::Unlock ? 1 :
                req.edit == EquipmentEdit::ClearSockets ? 2 : req.socket + 2;
            const int last = req.edit == EquipmentEdit::ClearSockets ? 6 : first;
            if (first < 0 || last > 6) return false;
            for (int i = first; i <= last; ++i)
                if (g_latestEdit[req.charIndex][req.tag][i].load(std::memory_order_acquire) > req.serial) return true;
            for (size_t i = 0; i < g_refineRequestCount; ++i)
            {
                const RefineRequest& old = g_refineRequests[i];
                if (old.charIndex == req.charIndex && old.tag == req.tag &&
                    old.edit == req.edit && old.socket == req.socket &&
                    old.typeId == req.typeId && old.instanceId == req.instanceId &&
                    old.component == req.component && old.owner == req.owner &&
                    old.sub == req.sub && old.controlledOwner == req.controlledOwner &&
                    old.table.desc == req.table.desc && old.table.array == req.table.array &&
                    old.entry == req.entry)
                {
                    if (req.serial >= old.serial)
                    {
                        g_refineRequests[i] = req;
                        for (int k = first; k <= last; ++k)
                            g_latestEdit[req.charIndex][req.tag][k].store(req.serial, std::memory_order_release);
                    }
                    return true;
                }
            }
            if (g_refineRequestCount == kMaxRefineRequests) return false;
            for (int i = first; i <= last; ++i)
                g_latestEdit[req.charIndex][req.tag][i].store(req.serial, std::memory_order_release);
            g_refineRequests[g_refineRequestCount++] = req;
            return true;
        }

        int GatherRefineComponents(int charIndex, uintptr_t* comps, int capacity)
        {
            if (charIndex < 0 || charIndex > 2) return 0;
            int count = 0;
            auto add = [&](uintptr_t comp) {
                for (int i = 0; i < count; ++i)
                    if (comps[i] == comp) return;
                if (!comp || count >= capacity || !CompValid(comp) ||
                    !AcceptCharacterComponent(charIndex, Inventory::IdentifyCharacterFromComp(comp), charIndex))
                    return;
                comps[count++] = comp;
            };
            add(ClientComp(false, charIndex));
            add(CompForCharacter(Player::GetOwner(charIndex), charIndex, charIndex));
            add(CompForCharacter(Player::GetActor(charIndex), charIndex, charIndex));
            if (charIndex == Inventory::ActivePlayerCharacterIdx())
            {
                add(CompForCharacter(Inventory::ClientCharacterAddr(), charIndex, charIndex));
                add(CompForCharacter(Inventory::ServerCharacterAddr(), charIndex, charIndex));
            }
            add(ProfileComp(charIndex));
            return count;
        }

        uintptr_t MatchingRefineEntry(uintptr_t comp, const RefineRequest& req)
        {
            // This visit's GatherRefineComponents already checked character
            // routing; the added replica has native client RTTI + exact item
            // provenance. Re-read native table/backlinks and item identity here,
            // not the source's localized character classifier at every checkpoint.
            // Other copies still get their own current character check.
            if (comp != req.component && !AcceptCharacterComponent(req.charIndex,
                Inventory::IdentifyCharacterFromComp(comp), req.charIndex)) return 0;
            // Enqueue already excluded dummy/unarmed items. Revalidation uses
            // exact type/instance below, so no repeated localized-name lookup.
            const uintptr_t entry = FindEntryByTag(comp, req.tag, false);
            uint16_t typeId = 0;
            int64_t instanceId = 0;
            if (!entry || !Read16(entry + kOff_InvSlot_TypeId, &typeId) || typeId != req.typeId ||
                !Read64(entry + kOff_ItemVal_InstanceId, &instanceId) ||
                instanceId != req.instanceId || (comp != req.component && req.instanceId <= 0))
                return 0;
            return entry;
        }

        bool RefineRequestCurrent(RefineRequest& req)
        {
            const char* previousReason = req.reason;
            req.retryable = false;
            req.reason = "invalid-request";
            if (req.charIndex < 0 || req.charIndex > 2 || req.tag >= 32) return false;
            const int first = req.edit == EquipmentEdit::Refine ? 0 : req.edit == EquipmentEdit::Unlock ? 1 :
                req.edit == EquipmentEdit::ClearSockets ? 2 : req.socket + 2;
            const int last = req.edit == EquipmentEdit::ClearSockets ? 6 : first;
            if (first < 0 || last > 6) return false;
            req.reason = "superseded";
            for (int i = first; i <= last; ++i)
                if (g_latestEdit[req.charIndex][req.tag][i].load(std::memory_order_acquire) != req.serial) return false;
            req.reason = "deadline";
            if (GetTickCount64() - req.queuedAt >= kRefineRequestLifetimeMs) return false;
            req.reason = "world-changed";
            if (!Player::Ready() || Player::GetControlledOwner() != req.controlledOwner) return false;
            req.reason = "selection-changed";
            if (req.boundSelection && Equipment::GetActiveCharacter() != req.charIndex) return false;
            req.retryable = true;
            req.reason = previousReason;
            return true;
        }

        bool RefineStampCurrent(RefineRequest& req, const uintptr_t* comps, int count)
        {
            if (!RefineRequestCurrent(req)) return false;
            const char* previousReason = req.reason;
            req.reason = "source-unavailable";
            bool found = false;
            for (int i = 0; i < count; ++i) found |= comps[i] == req.component;
            if (!found) return false; // original object must still be rediscoverable
            req.reason = "source-changed";
            req.retryable = false;
            uintptr_t owner = 0, sub = 0;
            if (!ReadPtr(req.component + kOff_EquipComp_Owner, &owner) || owner != req.owner ||
                !ReadPtr(owner + kOff_Container_Sub, &sub) || sub != req.sub)
                return false;
            const EquipTableDesc table = ReadEquipTableDesc(req.component);
            if (!table.valid || table.desc != req.table.desc || table.array != req.table.array ||
                table.count != req.table.count || table.stride != req.table.stride || table.tagOffset != req.table.tagOffset)
                return false;
            const uintptr_t entry = MatchingRefineEntry(req.component, req);
            int64_t instanceId = 0;
            const bool current = entry == req.entry && entry != 0 &&
                Read64(entry + kOff_ItemVal_InstanceId, &instanceId) && instanceId == req.instanceId;
            req.retryable = current;
            if (current) req.reason = previousReason;
            return current;
        }

        bool SyncRefineAllRealms(RefineRequest& req, int* componentWrites, int* holderWrites)
        {
            *componentWrites = 0;
            *holderWrites = 0;
            req.changedThisVisit = false;
            req.retryDelay = 500;
            ++req.attempts;
            if (!RefineRequestCurrent(req)) return false;
            if (req.edit == EquipmentEdit::Refine && (!Equipment::IsRefinableTag(req.tag) || req.level > kRefine_Max))
            { req.reason = "invalid-refine"; req.retryable = false; return false; }
            if (Inventory::IsTransactionActive()) { req.reason = "transaction"; return false; }
            uintptr_t comps[72] = {};
            int count = GatherRefineComponents(req.charIndex, comps, 72);
            if (!RefineStampCurrent(req, comps, count)) return false;
            if (Inventory::IsTransactionActive()) { req.reason = "transaction"; return false; }
            const uintptr_t replica = Dye::FindClientEquipmentReplica(req.component, req.charIndex, req.tag, req.typeId, req.instanceId);
            bool seenReplica = false;
            for (int i = 0; i < count; ++i) seenReplica |= comps[i] == replica;
            if (replica && !seenReplica && count < 72) comps[count++] = replica;
            if (!req.nextAttempt)
                LOG("equipment: edit targets op=%d char=%d tag=%u source=%p kind=%d clientReplica=%p candidates=%d",
                    static_cast<int>(req.edit), req.charIndex, req.tag, reinterpret_cast<void*>(req.component),
                    static_cast<int>(GetNativeEquipKind(req.component)), reinterpret_cast<void*>(replica), count);
            auto writeEntry = [&](uintptr_t entry) {
                if (!entry || Inventory::IsTransactionActive()) return false;
                switch (req.edit)
                {
                case EquipmentEdit::Refine:
                {
                    uint16_t level = 0;
                    if (!Read16(entry + kOff_ItemVal_RefineLevel, &level)) return false;
                    if (level == req.level) return true;
                    if (!WritableDataRange(entry, kOff_ItemVal_RefineLevel + sizeof(uint16_t)) ||
                        !Write16(entry + kOff_ItemVal_RefineLevel, req.level)) return false;
                    req.changedThisVisit = true;
                    return Read16(entry + kOff_ItemVal_RefineLevel, &level) && level == req.level;
                }
                case EquipmentEdit::Socket:
                case EquipmentEdit::Unlock:
                case EquipmentEdit::ClearSockets:
                {
                    const SocketDesc before = ReadSocketDesc(entry);
                    if (!before.valid) return false;
                    bool same = false;
                    if (req.edit == EquipmentEdit::Socket)
                        same = req.socket >= 0 && static_cast<uint32_t>(req.socket) < before.unlocked &&
                            before.records[req.socket].gear == req.gear && before.records[req.socket].index == req.socket;
                    else if (req.edit == EquipmentEdit::Unlock) same = before.unlocked >= req.level;
                    else
                    {
                        same = before.unlocked > 0;
                        for (unsigned i = 0; i < before.unlocked; ++i)
                            same &= before.records[i].gear == kSock_Empty && before.records[i].index == i;
                    }
                    if (same) return true;
                    // Even a partial failed write needs a display refresh, but
                    // repeated readback of unchanged data must not invalidate UI.
                    req.changedThisVisit = true;
                    if (req.edit == EquipmentEdit::Socket) return WriteSocketToEntry(entry, req.socket, req.gear);
                    if (req.edit == EquipmentEdit::Unlock) return OpenAllSockets(entry, req.level);
                    return EmptyAllSockets(entry);
                }
                }
                return false;
            };
            // Revalidate after bounded discovery; no cached replica authorizes writes.
            if (!RefineStampCurrent(req, comps, count)) return false;
            req.reason = "source-write-failed";
            if (!writeEntry(req.entry)) return false;
            req.hadWrites = true;
            bool clientWritten = false, serverWritten = false;
            for (int i = 0; i < count; ++i)
            {
                if (!RefineStampCurrent(req, comps, count)) return false;
                const uintptr_t entry = MatchingRefineEntry(comps[i], req);
                if (!entry || (entry != req.entry && !writeEntry(entry))) continue;
                ++*componentWrites;
                const NativeEquipKind kind = GetNativeEquipKind(comps[i]);
                clientWritten |= kind == NativeEquipKind::Client;
                serverWritten |= kind == NativeEquipKind::Server;
            }
            req.clientVerified = clientWritten;
            req.serverVerified = serverWritten;
            req.hadWrites |= *componentWrites > 0;
            // Validate BOTH completed cursors before deciding success. Large
            // inventories need throughput, not 256 slots followed by 50ms idle:
            // one fair 2048-slot / ~1ms slice per pump, with fresh write checks.
            const uint64_t epoch = Inventory::MutationEpoch();
            const uintptr_t holders[] = { Inventory::ClientHolderAddr(), Inventory::ServerHolderAddr() };
            req.reason = "holder-unavailable";
            for (unsigned realm = 0; realm < 2; ++realm)
            {
                auto& state = req.holders[realm];
                if (!state.Bind(holders[realm], Inventory::ItemStride(), epoch))
                {
                    state.nextAttempt = GetTickCount64() + 500;
                }
            }
            const unsigned firstRealm = req.nextHolderRealm;
            for (unsigned visit = 0; visit < 2; ++visit)
            {
                const unsigned realm = (firstRealm + visit) % 2;
                auto& state = req.holders[realm];
                auto& scan = state.cursor;
                if (state.complete || !scan.started || GetTickCount64() < state.nextAttempt) continue;
                req.nextHolderRealm = 1 - realm;
                static const LONGLONG frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
                LARGE_INTEGER started{}; QueryPerformanceCounter(&started);
                unsigned checks = 0;
                auto budget = [&] {
                    if ((checks++ & 31u) != 0) return true;
                    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
                    return frequency > 0 && now.QuadPart - started.QuadPart < frequency / 1000;
                };
                req.reason = "holder-read-failed";
                const auto result = scan.SliceWhile([&](uintptr_t slot) {
                    uint16_t type = 0; int64_t instance = 0;
                    if (!Read16(slot + kOff_InvSlot_TypeId, &type)) return false;
                    if (type != req.typeId) return true;
                    if (!Read64(slot + kOff_ItemVal_InstanceId, &instance)) return false;
                    if (req.instanceId <= 0 || instance != req.instanceId) return true;
                    req.reason = "holder-changed";
                    if (Inventory::IsTransactionActive() || Inventory::MutationEpoch() != epoch ||
                        !scan.Bind(realm ? Inventory::ServerHolderAddr() : Inventory::ClientHolderAddr(), Inventory::ItemStride(), epoch) ||
                        !scan.BucketCurrent() || !RefineStampCurrent(req, comps, count) ||
                        !WritableDataRange(slot, kOff_ItemVal_RefineLevel + sizeof(uint16_t))) return false;
                    req.reason = "holder-write-failed";
                    if (!writeEntry(slot)) return false;
                    ++state.matches;
                    req.reason = "holder-read-failed";
                    return true;
                }, [](const InventoryScan&) { return true; }, budget, 2048, 8);
                // Epoch changes on a nonmatching/empty slice must also discard
                // completion; otherwise a stale finished scan can report Synced.
                const bool current = !Inventory::IsTransactionActive() && Inventory::MutationEpoch() == epoch &&
                    scan.Bind(realm ? Inventory::ServerHolderAddr() : Inventory::ClientHolderAddr(), Inventory::ItemStride(), epoch);
                state.Finish(current ? result : InventoryScan::Result::Invalid);
                if (!current) req.reason = "holder-changed";
                if (current && result != InventoryScan::Result::Invalid)
                {
                    state.nextAttempt = 0;
                    req.retryDelay = 16;
                    req.reason = "holder-pending";
                }
                else state.nextAttempt = GetTickCount64() + 500;
                break;
            }
            *holderWrites = static_cast<int>(req.holders[0].matches + req.holders[1].matches);
            req.hadWrites |= *componentWrites > 0 || *holderWrites > 0;
            if (!req.retryable) return false;
            if (!RefineStampCurrent(req, comps, count)) return false;
            if (Inventory::IsTransactionActive() || Inventory::MutationEpoch() != epoch)
            {
                for (auto& state : req.holders) state.Reset();
                req.reason = "holder-changed";
                return false;
            }
            const bool holdersDone = req.holders[0].complete && req.holders[1].complete;
            if (holdersDone) { req.reason = "replica-missing"; req.retryDelay = 500; }
            // Distinguish a data-only update from a verified client/server pair.
            // A TLS flag cannot turn the same pointer into a different realm.
            return clientWritten && serverWritten && holdersDone;
        }

        bool QueueEquipmentEdit(EquipmentEdit edit, uint16_t tag, int value, uint16_t gear,
                                const Equipment::SlotInfo* expected)
        {
            RefineRequest req{};
            req.edit = edit;
            req.charIndex = Equipment::GetActiveCharacter();
            req.tag = tag;
            req.socket = edit == EquipmentEdit::Socket ? value : 0;
            req.level = edit == EquipmentEdit::Socket ? 0 : static_cast<uint16_t>(value);
            req.gear = gear;
            req.component = ClientComp(false, req.charIndex);
            req.table = ReadEquipTableDesc(req.component);
            req.entry = FindEntryByTag(req.component, tag);
            req.controlledOwner = Player::GetControlledOwner();
            if (!Player::Ready() || !req.table.valid || !req.entry || !IsValidCanonicalPtr(req.controlledOwner) ||
                !ReadPtr(req.component + kOff_EquipComp_Owner, &req.owner) ||
                !ReadPtr(req.owner + kOff_Container_Sub, &req.sub) ||
                !Read16(req.entry + kOff_InvSlot_TypeId, &req.typeId) ||
                !Read64(req.entry + kOff_ItemVal_InstanceId, &req.instanceId) || req.instanceId <= 0 ||
                (expected && (expected->tag != tag || !DisplayedTargetMatches(req.entry, expected)))) return false;
            if (edit != EquipmentEdit::Refine)
            {
                const SocketDesc sockets = ReadWritableSocketDesc(req.entry);
                if (!sockets.valid || (expected && !expected->socketsAvailable) ||
                    (edit == EquipmentEdit::Socket && (value < 0 || static_cast<uint32_t>(value) >= sockets.unlocked))) return false;
                if (edit == EquipmentEdit::Unlock) req.level = static_cast<uint16_t>(sockets.size);
            }
            req.boundSelection = expected != nullptr;
            req.queuedAt = GetTickCount64();
            req.serial = g_editSerial.fetch_add(1, std::memory_order_relaxed) + 1;
            if (!QueueRefine(req)) return false;
            g_editState.store(Equipment::EditState::Pending, std::memory_order_release);
            return true;
        }

        bool SyncUnlockAllRealms(uint16_t tag, int64_t instId, int maxSock, uint16_t typeId = 0, const Equipment::SlotInfo* expected = nullptr)
        {
            bool ok = false;
            auto openOnEntry = [&](uintptr_t entry) {
                if (entry < kMinPointer) return;
                ok |= OpenAllSockets(entry, maxSock, expected);
            };

            // 1. Client Equip Comp
            const uintptr_t clientC = ClientComp();
            if (clientC) openOnEntry(FindEntryByTag(clientC, tag));

            // Direct profile component update (authoritative for in-game pause/party menu)
            const uintptr_t profC = ProfileComp(Equipment::GetActiveCharacter());
            if (profC && profC != clientC)
                openOnEntry(FindEntryByTag(profC, tag));

            // 1b. Companion containers via PartyIndex (the copy the native
            // inspect UI reads) - mirror of the Refine fix, bounded to the
            // PartyIndex match only.
            {
                const int targetIdx = Equipment::GetActiveCharacter();
                if (targetIdx >= 0 && targetIdx < 3)
                {
                    const uintptr_t charMgrGlobal = Player::GetCharMgrGlobal();
                    uintptr_t p = 0, mgr = 0, data = 0;
                    if (charMgrGlobal >= kMinPointer &&
                        ReadPtr(charMgrGlobal, &p) && p >= kMinPointer &&
                        ReadPtr(p, &mgr) && mgr >= kMinPointer)
                    {
                        uint32_t cCount = 0;
                        if (ReadPtr(mgr + kOff_CharMgr_ListData, &data) && data >= kMinPointer &&
                            Read32(mgr + kOff_CharMgr_ListCount, &cCount) && cCount > 0 && cCount <= kCharList_MaxCount)
                        {
                            for (uint32_t i = 0; i < cCount; ++i)
                            {
                                uintptr_t cand = 0;
                                if (!ReadPtr(data + static_cast<uintptr_t>(i) * 8, &cand) || cand < kMinPointer) continue;
                                uint32_t pIdx = 0;
                                if (!Read32(cand + kOff_Owner_PartyIndex, &pIdx) || pIdx != static_cast<uint32_t>(targetIdx + 1)) continue;
                                const uintptr_t comp = CompForCharacter(cand, targetIdx, targetIdx);
                                if (comp && comp != clientC && comp != profC)
                                    openOnEntry(FindEntryByTag(comp, tag));
                            }
                        }
                    }
                }
            }

            // 2. Direct bag slot sync across ALL containers and holders
            struct UnlCtx { int maxSock; bool* ok; const Equipment::SlotInfo* expected; };
            UnlCtx ctx{ maxSock, &ok, expected };
            Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                auto* c = reinterpret_cast<UnlCtx*>(u);
                *(c->ok) |= OpenAllSockets(slot, c->maxSock, c->expected);
            }, &ctx, typeId);

            // 3. Server Realm
            uint8_t oldFlag = 0;
            const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
            if (flagAddr && RawWrite8(flagAddr, 1))
            {
                const uintptr_t serverC = ServerComp();
                if (serverC) openOnEntry(FindEntryByTag(serverC, tag));

                if (profC && profC != serverC)
                    openOnEntry(FindEntryByTag(profC, tag));

                // Server-realm companion containers via PartyIndex (RealmFlag
                // = 1 active) - same rationale as the Refine fix above.
                {
                    const int targetIdx = Equipment::GetActiveCharacter();
                    if (targetIdx >= 0 && targetIdx < 3)
                    {
                        const uintptr_t charMgrGlobal = Player::GetCharMgrGlobal();
                        uintptr_t p = 0, mgr = 0, data = 0;
                        if (charMgrGlobal >= kMinPointer &&
                            ReadPtr(charMgrGlobal, &p) && p >= kMinPointer &&
                            ReadPtr(p, &mgr) && mgr >= kMinPointer)
                        {
                            uint32_t cCount = 0;
                            if (ReadPtr(mgr + kOff_CharMgr_ListData, &data) && data >= kMinPointer &&
                                Read32(mgr + kOff_CharMgr_ListCount, &cCount) && cCount > 0 && cCount <= kCharList_MaxCount)
                            {
                                for (uint32_t i = 0; i < cCount; ++i)
                                {
                                    uintptr_t cand = 0;
                                    if (!ReadPtr(data + static_cast<uintptr_t>(i) * 8, &cand) || cand < kMinPointer) continue;
                                    uint32_t pIdx = 0;
                                    if (!Read32(cand + kOff_Owner_PartyIndex, &pIdx) || pIdx != static_cast<uint32_t>(targetIdx + 1)) continue;
                                    const uintptr_t comp = CompForCharacter(cand, targetIdx, targetIdx);
                                    if (comp && comp != serverC && comp != profC)
                                        openOnEntry(FindEntryByTag(comp, tag));
                                }
                            }
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                    auto* c = reinterpret_cast<UnlCtx*>(u);
                    *(c->ok) |= OpenAllSockets(slot, c->maxSock, c->expected);
                }, &ctx, typeId);

                RawWrite8(flagAddr, oldFlag);
            }

            return ok;
        }

        bool SyncEmptyAllRealms(uint16_t tag, int64_t instId, uint16_t typeId = 0, const Equipment::SlotInfo* expected = nullptr)
        {
            bool ok = false;
            auto emptyOnEntry = [&](uintptr_t entry) {
                if (entry < kMinPointer) return;
                ok |= EmptyAllSockets(entry, expected);
            };

            // 1. Client Equip Comp
            const uintptr_t clientC = ClientComp();
            if (clientC) emptyOnEntry(FindEntryByTag(clientC, tag));

            // Direct profile component update (authoritative for in-game pause/party menu)
            const uintptr_t profC = ProfileComp(Equipment::GetActiveCharacter());
            if (profC && profC != clientC)
                emptyOnEntry(FindEntryByTag(profC, tag));

            // 1b. Companion containers via PartyIndex (the copy the native
            // inspect UI reads) - mirror of the Refine fix, bounded.
            {
                const int targetIdx = Equipment::GetActiveCharacter();
                if (targetIdx >= 0 && targetIdx < 3)
                {
                    const uintptr_t charMgrGlobal = Player::GetCharMgrGlobal();
                    uintptr_t p = 0, mgr = 0, data = 0;
                    if (charMgrGlobal >= kMinPointer &&
                        ReadPtr(charMgrGlobal, &p) && p >= kMinPointer &&
                        ReadPtr(p, &mgr) && mgr >= kMinPointer)
                    {
                        uint32_t cCount = 0;
                        if (ReadPtr(mgr + kOff_CharMgr_ListData, &data) && data >= kMinPointer &&
                            Read32(mgr + kOff_CharMgr_ListCount, &cCount) && cCount > 0 && cCount <= kCharList_MaxCount)
                        {
                            for (uint32_t i = 0; i < cCount; ++i)
                            {
                                uintptr_t cand = 0;
                                if (!ReadPtr(data + static_cast<uintptr_t>(i) * 8, &cand) || cand < kMinPointer) continue;
                                uint32_t pIdx = 0;
                                if (!Read32(cand + kOff_Owner_PartyIndex, &pIdx) || pIdx != static_cast<uint32_t>(targetIdx + 1)) continue;
                                const uintptr_t comp = CompForCharacter(cand, targetIdx, targetIdx);
                                if (comp && comp != clientC && comp != profC)
                                    emptyOnEntry(FindEntryByTag(comp, tag));
                            }
                        }
                    }
                }
            }

            // 2. Direct bag slot sync across ALL containers and holders
            struct EmptyCtx { bool* ok; const Equipment::SlotInfo* expected; } ctx{ &ok, expected };
            Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                auto* c = static_cast<EmptyCtx*>(u);
                *(c->ok) |= EmptyAllSockets(slot, c->expected);
            }, &ctx, typeId);

            // 3. Server Realm
            uint8_t oldFlag = 0;
            const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
            if (flagAddr && RawWrite8(flagAddr, 1))
            {
                const uintptr_t serverC = ServerComp();
                if (serverC) emptyOnEntry(FindEntryByTag(serverC, tag));

                if (profC && profC != serverC)
                    emptyOnEntry(FindEntryByTag(profC, tag));

                // Server-realm companion containers via PartyIndex (RealmFlag
                // = 1 active) - same rationale as the Refine fix above.
                {
                    const int targetIdx = Equipment::GetActiveCharacter();
                    if (targetIdx >= 0 && targetIdx < 3)
                    {
                        const uintptr_t charMgrGlobal = Player::GetCharMgrGlobal();
                        uintptr_t p = 0, mgr = 0, data = 0;
                        if (charMgrGlobal >= kMinPointer &&
                            ReadPtr(charMgrGlobal, &p) && p >= kMinPointer &&
                            ReadPtr(p, &mgr) && mgr >= kMinPointer)
                        {
                            uint32_t cCount = 0;
                            if (ReadPtr(mgr + kOff_CharMgr_ListData, &data) && data >= kMinPointer &&
                                Read32(mgr + kOff_CharMgr_ListCount, &cCount) && cCount > 0 && cCount <= kCharList_MaxCount)
                            {
                                for (uint32_t i = 0; i < cCount; ++i)
                                {
                                    uintptr_t cand = 0;
                                    if (!ReadPtr(data + static_cast<uintptr_t>(i) * 8, &cand) || cand < kMinPointer) continue;
                                    uint32_t pIdx = 0;
                                    if (!Read32(cand + kOff_Owner_PartyIndex, &pIdx) || pIdx != static_cast<uint32_t>(targetIdx + 1)) continue;
                                    const uintptr_t comp = CompForCharacter(cand, targetIdx, targetIdx);
                                    if (comp && comp != serverC && comp != profC)
                                        emptyOnEntry(FindEntryByTag(comp, tag));
                                }
                            }
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                    auto* c = static_cast<EmptyCtx*>(u);
                    *(c->ok) |= EmptyAllSockets(slot, c->expected);
                }, &ctx, typeId);

                RawWrite8(flagAddr, oldFlag);
            }

            return ok;
        }


        // Catalog construction can legitimately fail while loading. Negative
        // lookups expire, and a rebuilt catalog may move the category index.
        std::mutex g_gearCatMutex;
        int g_gearCat = -1;
        ULONGLONG g_nextGearCatLookup = 0;
        bool IsGearCategoryName(const char* name)
        {
            return name && (_stricmp(name, "Abyss Gear") == 0 || _stricmp(name, "Abyss Gears") == 0);
        }
        int GearCategory()
        {
            std::lock_guard<std::mutex> lock(g_gearCatMutex);
            const ULONGLONG now = GetTickCount64();
            if (g_gearCat >= 0 && !IsGearCategoryName(Inventory::CatalogCategoryName(g_gearCat)))
            {
                g_gearCat = -1;
                g_nextGearCatLookup = 0;
            }
            if (now < g_nextGearCatLookup) return g_gearCat;
            g_nextGearCatLookup = now + 1000;
            const int n = Inventory::CatalogCategoryCount(); // builds the catalog
            if (g_gearCat >= 0 && g_gearCat < n &&
                IsGearCategoryName(Inventory::CatalogCategoryName(g_gearCat))) return g_gearCat;
            g_gearCat = -1;
            for (int c = 0; c < n; ++c)
            {
                if (IsGearCategoryName(Inventory::CatalogCategoryName(c)))
                {
                    g_gearCat = c;
                    return c;
                }
            }
            return g_gearCat;
        }

        // Historical profile files are retained for explicit saves only.
        // Automatic replay is disabled: character/tag is not a durable item identity.
        struct SavedEquipmentSlot
        {
            bool     active = false;
            uint16_t tag = 0;
            uint16_t typeId = 0;
            uint16_t refineLevel = 0;
            uint32_t unlockedSockets = 0;
            uint16_t socketGems[kSocket_Max] = { kSock_Empty, kSock_Empty, kSock_Empty, kSock_Empty, kSock_Empty };
        };
        static SavedEquipmentSlot s_savedEquipSlots[3][32]; // [charIdx 0..2][tag 0..31]
        std::mutex g_profileMutex;
        std::mutex g_profileIoMutex;
        std::mutex g_profileWorkerMutex;
        std::future<bool> g_profileSaveWorker;
        bool g_profileStopping = false; // guarded by g_profileWorkerMutex
        static std::atomic<ULONGLONG> s_profileSavePendingMs{ 0 };
        struct EquipmentProfileCopy
        {
            SavedEquipmentSlot slots[3][32]{};
            char path[MAX_PATH]{};
            char version[128]{};
        };

        static bool WriteEquipProfiles(const EquipmentProfileCopy& copy)
        {
            // Worker owns a value copy; no game pointers or menu-state mutex.
            std::lock_guard<std::mutex> io(g_profileIoMutex);
            FILE* f = nullptr;
            fopen_s(&f, copy.path, "w");
            if (!f) return false;

            fprintf(f, "# Trinity Persistent Equipment Profile (Auto-Saved)\n");
            // Informational version stamp; profiles are not automatically replayed.
            fprintf(f, "# GameVersion=%s\n", copy.version);
            fprintf(f, "# Saves Refinement, Unlocked Sockets, and Abyss Gems for Kliff, Damiane, and Oongka\n\n");

            for (int c = 0; c < 3; ++c)
            {
                for (int t = 0; t < 32; ++t)
                {
                    const auto& s = copy.slots[c][t];
                    if (!s.active) continue;

                    fprintf(f, "[Player_%d_Tag_%d]\n", c, t);
                    fprintf(f, "Refine=%u\n", s.refineLevel);
                    fprintf(f, "Unlocked=%u\n", s.unlockedSockets);
                    for (int k = 0; k < kSocket_Max; ++k)
                    {
                        if (s.socketGems[k] != kSock_Empty && s.socketGems[k] != 0)
                            fprintf(f, "Socket_%d=0x%04X\n", k, s.socketGems[k]);
                        else
                            fprintf(f, "Socket_%d=0xFFFF\n", k);
                    }
                    fprintf(f, "\n");
                }
            }
            const bool ok = !ferror(f);
            return fclose(f) == 0 && ok;
        }

        static void SaveEquipProfilesToDisk()
        {
            s_profileSavePendingMs.store(GetTickCount64() + 600, std::memory_order_release);
        }

        static void PumpEquipProfileSave()
        {
            std::unique_lock<std::mutex> worker(g_profileWorkerMutex, std::try_to_lock);
            if (!worker.owns_lock() || g_profileStopping) return;
            if (g_profileSaveWorker.valid())
            {
                if (g_profileSaveWorker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
                try
                {
                    if (!g_profileSaveWorker.get()) LOG_WARN("equipment: profile file save failed.");
                }
                catch (...) { LOG_WARN("equipment: profile file worker failed."); }
            }
            ULONGLONG due = s_profileSavePendingMs.load(std::memory_order_acquire);
            if (!due || GetTickCount64() < due) return;
            EquipmentProfileCopy copy{};
            {
                std::unique_lock<std::mutex> lock(g_profileMutex, std::try_to_lock);
                if (!lock.owns_lock()) return;
                if (!s_profileSavePendingMs.compare_exchange_strong(due, 0, std::memory_order_acq_rel)) return;
                memcpy(copy.slots, s_savedEquipSlots, sizeof(copy.slots));
            }
            if (!GetModuleFileNameA(nullptr, copy.path, MAX_PATH)) return;
            char* slash = strrchr(copy.path, '\\');
            if (!slash) return;
            *(slash + 1) = '\0';
            strcat_s(copy.path, "Trinity_EquipmentProfile.ini");
            snprintf(copy.version, sizeof(copy.version), "%s", core::GetGameVersionDisplay());
            try { g_profileSaveWorker = std::async(std::launch::async, [copy] { return WriteEquipProfiles(copy); }); }
            catch (...) { SaveEquipProfilesToDisk(); }
        }

        static void LoadEquipProfilesFromDisk()
        {
            std::lock_guard<std::mutex> io(g_profileIoMutex);
            std::lock_guard<std::mutex> lock(g_profileMutex);
            char iniPath[MAX_PATH];
            GetModuleFileNameA(NULL, iniPath, MAX_PATH);
            char* lastSlash = strrchr(iniPath, '\\');
            if (lastSlash) *(lastSlash + 1) = '\0';
            strcat_s(iniPath, "Trinity_EquipmentProfile.ini");

            FILE* f = nullptr;
            fopen_s(&f, iniPath, "r");
            if (!f) return;

            char line[256];
            int curChar = -1;
            int curTag = -1;
            bool sawVersion = false;
            bool versionOk  = false;

            while (fgets(line, sizeof(line), f))
            {
                char* p = line + strlen(line);
                while (p > line && (*(p - 1) == '\r' || *(p - 1) == '\n' || *(p - 1) == ' ')) { *(--p) = '\0'; }

                if (line[0] == '#' && !strncmp(line, "# GameVersion=", 14))
                {
                    sawVersion = true;
                    versionOk = (strcmp(line + 14, core::GetGameVersionDisplay()) == 0) ||
                                (strstr(line + 14, "TU 2.") != nullptr) ||
                                (strstr(line + 14, "Crimson Desert") != nullptr);
                    continue;
                }

                if (line[0] == '[' && line[strlen(line) - 1] == ']')
                {
                    curChar = -1;
                    curTag = -1;
                    if (sscanf_s(line, "[Player_%d_Tag_%d]", &curChar, &curTag) == 2)
                    {
                        if (curChar >= 0 && curChar < 3 && curTag >= 0 && curTag < 32)
                        {
                            s_savedEquipSlots[curChar][curTag].active = true;
                            s_savedEquipSlots[curChar][curTag].tag = static_cast<uint16_t>(curTag);
                        }
                    }
                }
                else if (curChar >= 0 && curChar < 3 && curTag >= 0 && curTag < 32)
                {
                    unsigned int val = 0;
                    int sockIdx = 0;
                    if (sscanf_s(line, "Refine=%u", &val) == 1)
                    {
                        s_savedEquipSlots[curChar][curTag].refineLevel = static_cast<uint16_t>(val > 10 ? 10 : val);
                    }
                    else if (sscanf_s(line, "Unlocked=%u", &val) == 1)
                    {
                        s_savedEquipSlots[curChar][curTag].unlockedSockets = static_cast<uint32_t>(val > 5 ? 5 : val);
                    }
                    else if (sscanf_s(line, "Socket_%d=0x%x", &sockIdx, &val) == 2 ||
                             sscanf_s(line, "Socket_%d=0X%x", &sockIdx, &val) == 2 ||
                             sscanf_s(line, "Socket_%d=%u", &sockIdx, &val) == 2)
                    {
                        if (sockIdx >= 0 && sockIdx < kSocket_Max)
                        {
                            s_savedEquipSlots[curChar][curTag].socketGems[sockIdx] = static_cast<uint16_t>(val);
                        }
                    }
                }
            }
            fclose(f);

            if (!sawVersion || !versionOk)
            {
                LOG("equipment: loaded historical profiles; automatic replay disabled (current %s).",
                    core::GetGameVersionDisplay());
            }
            else
            {
                LOG("equipment: loaded profiles from '%s' for explicit saves only; automatic replay disabled.", iniPath);
            }
        }

        static void SyncSlotToProfile(int charIdx, uint16_t tag, uintptr_t sourceComp = 0)
        {
            if (charIdx < 0 || charIdx >= 3 || tag >= 32) return;
            const uintptr_t comp = sourceComp ? sourceComp : ClientComp(false, charIdx);
            uintptr_t entry = comp ? FindEntryByTag(comp, tag) : 0;
            if (!entry && !sourceComp && charIdx > 0)
            {
                const uintptr_t profC = ProfileComp(charIdx);
                if (profC) entry = FindEntryByTag(profC, tag);
            }
            if (!entry) return;

            SavedEquipmentSlot prof{};
            prof.active = true;
            prof.tag = tag;
            Read16(entry + kOff_InvSlot_TypeId, &prof.typeId);

            uint16_t refine = 0;
            Read16(entry + kOff_ItemVal_RefineLevel, &refine);
            prof.refineLevel = (refine > 10) ? 10 : refine;

            const SocketDesc sockets = ReadSocketDesc(entry);
            prof.unlockedSockets = sockets.valid ? sockets.unlocked : 0;
            for (int k = 0; k < kSocket_Max; ++k)
            {
                prof.socketGems[k] = GearAt(sockets, k);
            }
            {
                std::lock_guard<std::mutex> lock(g_profileMutex);
                s_savedEquipSlots[charIdx][tag] = prof;
            }

            // Debounce disk write: avoid blocking the render thread on every slider step
            SaveEquipProfilesToDisk();
        }

        bool ApplyRefineGuarded(RefineRequest& req, int* componentWrites, int* holderWrites)
        {
            __try { return SyncRefineAllRealms(req, componentWrites, holderWrites); }
            __except (EXCEPTION_EXECUTE_HANDLER) { req.reason = "guarded-fault"; req.retryable = false; return false; }
        }

        void ProcessPendingRefines()
        {
            // The movement driver can be reached again before a prior drain
            // returns. Keep writes ordered without blocking another game tick.
            if (g_refineApplying.test_and_set(std::memory_order_acquire)) return;
            struct DrainGuard
            {
                ~DrainGuard() { g_refineApplying.clear(std::memory_order_release); }
            } drainGuard;
            static ULONGLONG nextPump = 0;
            if (GetTickCount64() < nextPump) return;
            nextPump = GetTickCount64() + 16;
            if (Inventory::IsTransactionActive()) return;
            RefineRequest req{};
            {
                std::unique_lock<std::mutex> lock(g_refineMutex, std::try_to_lock);
                if (!lock.owns_lock()) return;
                const ULONGLONG now = GetTickCount64();
                size_t at = 0;
                while (at < g_refineRequestCount && now < g_refineRequests[at].nextAttempt) ++at;
                if (at == g_refineRequestCount) return;
                req = g_refineRequests[at];
                for (size_t i = at + 1; i < g_refineRequestCount; ++i) g_refineRequests[i - 1] = g_refineRequests[i];
                --g_refineRequestCount;
            }
            // One edit per pump. Progressing scans resume next pump; missing or
            // invalid holders back off instead of rebuilding UI every retry.
            {
                int componentWrites = 0, holderWrites = 0;
                if (ApplyRefineGuarded(req, &componentWrites, &holderWrites))
                {
                    if (req.changedThisVisit) InvalidateSnapshot();
                    SyncSlotToProfile(req.charIndex, req.tag, req.component);
                    g_editState.store(Equipment::EditState::Synced, std::memory_order_release);
                    LOG("equipment: edit synced op=%d char=%d tag=%u type=%u instance=0x%llX value=%u components=%d holders=%d (client/server verified; native effects deferred).",
                        static_cast<int>(req.edit), req.charIndex, req.tag, req.typeId, static_cast<unsigned long long>(req.instanceId),
                        req.edit == EquipmentEdit::Socket ? req.gear : req.level, componentWrites, holderWrites);
                }
                else
                {
                    if (req.changedThisVisit) InvalidateSnapshot();
                    req.hadWrites |= req.changedThisVisit;
                    if (req.retryable && GetTickCount64() - req.queuedAt < kRefineRequestLifetimeMs)
                    {
                        req.nextAttempt = GetTickCount64() + req.retryDelay;
                        if (QueueRefine(req)) return;
                        req.reason = "queue-full";
                    }
                    // A newer value owns the UI result; an old request retiring
                    // must not overwrite Pending/Synced with a false failure.
                    if (std::strcmp(req.reason, "superseded") == 0) return;
                    g_editState.store(req.hadWrites || componentWrites ? Equipment::EditState::DataOnly : Equipment::EditState::Failed,
                                      std::memory_order_release);
                    LOG_WARN("equipment: edit incomplete op=%d char=%d tag=%u type=%u instance=0x%llX value=%u reason=%s ageMs=%llu attempts=%u lastClient=%d lastServer=%d components=%d holders=%d scans=%d/%d cursor=%u:%u/%u:%u restarts=%u/%u (synchronization not confirmed).",
                        static_cast<int>(req.edit), req.charIndex, req.tag, req.typeId, static_cast<unsigned long long>(req.instanceId),
                        req.edit == EquipmentEdit::Socket ? req.gear : req.level, req.reason,
                        static_cast<unsigned long long>(GetTickCount64() - req.queuedAt), req.attempts,
                        req.clientVerified, req.serverVerified, componentWrites, holderWrites,
                        req.holders[0].complete, req.holders[1].complete,
                        req.holders[0].cursor.bucketIndex, req.holders[0].cursor.row,
                        req.holders[1].cursor.bucketIndex, req.holders[1].cursor.row,
                        req.holders[0].restarts, req.holders[1].restarts);
                }
            }
        }

        // --- Menu-side snapshot ---------------------------------------------
        constexpr int          kMaxSlots = 64;
        Equipment::SlotInfo    g_slots[kMaxSlots];
        int                    g_slotCount = 0;
        std::mutex             g_snapshotMutex;
        constexpr ULONGLONG    kSnapshotIntervalMs = 200;
        constexpr ULONGLONG    kSnapshotGraceMs = 750;
        uintptr_t              g_snapshotWorld = 0;
        int                    g_snapshotSelection = -2;
        int                    g_snapshotAutoCharacter = -2;
        ULONGLONG              g_lastSnapshotAttempt = 0;
        ULONGLONG              g_lastSnapshotSuccess = 0;
        bool                   g_snapshotAttempted = false;
        bool                   g_snapshotReady = false;
        bool                   g_snapshotPersist = false;
        bool                   g_snapshotReadFailed = false;
        int                    g_lastSnapshotFrame = -1;

        const char* SlotNameForTag(uint16_t tag)
        {
            return Equipment::SlotNameForTag(tag);
        }

        // Map an abyss-gear display name to its stat effect description.
        static const char* ResolveGearBuff(const char* name)
        {
            if (!name || !name[0]) return "";

            // Material specific Abyss Gears
            if (strstr(name, "Insight Gear") || strstr(name, "Critical Rate By Material") || strstr(name, "Critical Rate"))
            {
                if (strstr(name, "Fabric") || strstr(name, "Cloth")) return "Crit Rate (Fabric)";
                if (strstr(name, "Feather")) return "Crit Rate (Feather)";
                if (strstr(name, "Leather") || strstr(name, "Hide")) return "Crit Rate (Leather)";
                if (strstr(name, "Ore") || strstr(name, "Metal")) return "Crit Rate (Ore)";
                if (strstr(name, "Plant") || strstr(name, "Flora")) return "Crit Rate (Plant)";
                if (strstr(name, "Gem") || strstr(name, "Precious") || strstr(name, "Stone")) return "Crit Rate (Gem)";
                if (strstr(name, "Wood") || strstr(name, "Timber")) return "Crit Rate (Wood)";
            }
            if (strstr(name, "Destruction Gear") || strstr(name, "Attack By Material") || strstr(name, "Attack By"))
            {
                if (strstr(name, "Fabric") || strstr(name, "Cloth")) return "Attack + (Fabric)";
                if (strstr(name, "Feather")) return "Attack + (Feather)";
                if (strstr(name, "Leather") || strstr(name, "Hide")) return "Attack + (Leather)";
                if (strstr(name, "Ore") || strstr(name, "Metal")) return "Attack + (Ore)";
                if (strstr(name, "Plant") || strstr(name, "Flora")) return "Attack + (Plant)";
                if (strstr(name, "Gem") || strstr(name, "Precious") || strstr(name, "Stone")) return "Attack + (Gem)";
                if (strstr(name, "Wood") || strstr(name, "Timber")) return "Attack + (Wood)";
            }
            if (strstr(name, "Aegis Gear") || strstr(name, "Defense By Material") || strstr(name, "Defense By"))
            {
                if (strstr(name, "Fabric") || strstr(name, "Cloth")) return "Defense + (Fabric)";
                if (strstr(name, "Feather")) return "Defense + (Feather)";
                if (strstr(name, "Leather") || strstr(name, "Hide")) return "Defense + (Leather)";
                if (strstr(name, "Ore") || strstr(name, "Metal")) return "Defense + (Ore)";
                if (strstr(name, "Plant") || strstr(name, "Flora")) return "Defense + (Plant)";
                if (strstr(name, "Gem") || strstr(name, "Precious") || strstr(name, "Stone")) return "Defense + (Gem)";
                if (strstr(name, "Wood") || strstr(name, "Timber")) return "Defense + (Wood)";
            }

            if (strstr(name, "Destruction I") && !strstr(name, "II") && !strstr(name, "III")) return "Attack 1";
            if (strstr(name, "Destruction III")) return "Attack 3";
            if (strstr(name, "Destruction II")) return "Attack 2";
            if (strstr(name, "Greater Destruction")) return "Attack 5";
            if (strstr(name, "Colossal Might")) return "Attack +10%";
            if (strstr(name, "Aegis I") && !strstr(name, "II") && !strstr(name, "III")) return "Damage Reduction 1";
            if (strstr(name, "Aegis III")) return "Damage Reduction 3";
            if (strstr(name, "Aegis II")) return "Damage Reduction 2";
            if (strstr(name, "Fortification I") && !strstr(name, "II") && !strstr(name, "III")) return "Defense +5";
            if (strstr(name, "Fortification III")) return "Defense +15";
            if (strstr(name, "Fortification II")) return "Defense +10";
            if (strstr(name, "Insight I") && !strstr(name, "II") && !strstr(name, "III")) return "Critical Rate +2%";
            if (strstr(name, "Insight III")) return "Critical Rate +6%";
            if (strstr(name, "Insight II")) return "Critical Rate +4%";
            if (strstr(name, "Greater Insight")) return "Critical Rate +10%";
            if (strstr(name, "Swift I") && !strstr(name, "II") && !strstr(name, "III")) return "Attack Speed +4%";
            if (strstr(name, "Swift III")) return "Attack Speed +12%";
            if (strstr(name, "Swift II")) return "Attack Speed +8%";
            if (strstr(name, "Greater Swift")) return "Attack Speed +20%";
            if (strstr(name, "Vitality I") && !strstr(name, "II") && !strstr(name, "III")) return "HP Recovery +5";
            if (strstr(name, "Vitality III")) return "HP Recovery +15";
            if (strstr(name, "Vitality II")) return "HP Recovery +10";
            if (strstr(name, "Vigor I") && !strstr(name, "II") && !strstr(name, "III")) return "Stamina Recovery +5%";
            if (strstr(name, "Vigor III")) return "Stamina Recovery +15%";
            if (strstr(name, "Vigor II")) return "Stamina Recovery +10%";
            if (strstr(name, "Composure I") && !strstr(name, "II") && !strstr(name, "III")) return "Spirit Recovery +5%";
            if (strstr(name, "Composure III")) return "Spirit Recovery +15%";
            if (strstr(name, "Composure II")) return "Spirit Recovery +10%";
            if (strstr(name, "Haste I") && !strstr(name, "II") && !strstr(name, "III")) return "Move Speed +3%";
            if (strstr(name, "Haste III")) return "Move Speed +10%";
            if (strstr(name, "Haste II")) return "Move Speed +6%";
            if (strstr(name, "Abyssbane I") && !strstr(name, "II") && !strstr(name, "III")) return "Abyss Damage +5%";
            if (strstr(name, "Abyssbane III")) return "Abyss Damage +15%";
            if (strstr(name, "Abyssbane II")) return "Abyss Damage +10%";
            if (strstr(name, "Beastbane I") && !strstr(name, "II") && !strstr(name, "III")) return "Beast Damage +5%";
            if (strstr(name, "Beastbane III")) return "Beast Damage +15%";
            if (strstr(name, "Beastbane II")) return "Beast Damage +10%";
            if (strstr(name, "Bloodbane I") && !strstr(name, "II") && !strstr(name, "III")) return "Humanoid Damage +5%";
            if (strstr(name, "Bloodbane III")) return "Humanoid Damage +15%";
            if (strstr(name, "Bloodbane II")) return "Humanoid Damage +10%";
            if (strstr(name, "Malicebane")) return "Boss Damage +10%";
            if (strstr(name, "Life Transference")) return "HP Steal on Hit";
            if (strstr(name, "Spirit Transference")) return "Spirit Steal on Hit";
            if (strstr(name, "Stamina Transference")) return "Stamina Steal on Hit";
            if (strstr(name, "Celestial Transference")) return "Spirit & Stamina Drain";
            if (strstr(name, "Crescent Moon Slash")) return "Crescent Wave Skill";
            if (strstr(name, "Fullmoon Slash")) return "Full Moon Wave Skill";
            if (strstr(name, "Crow Storm")) return "Crow Whirlwind";
            if (strstr(name, "Crow's Pursuit")) return "Crow Dive Strike";
            if (strstr(name, "Arrow Rain")) return "Volley Arrow Skill";
            if (strstr(name, "Ator's Orb")) return "Ator Drone Support";
            if (strstr(name, "Ancient Wrath")) return "Ancient Wrath Aura";
            if (strstr(name, "Ancient Reckoning")) return "Reckoning Burst";
            if (strstr(name, "Ancient Retribution")) return "Retribution Counter";
            if (strstr(name, "Tempest of Destruction")) return "Tempest Whirlwind";
            if (strstr(name, "Earthrending Strike")) return "Shockwave Impact";
            if (strstr(name, "Lightning God's Affliction")) return "Thunder Strike";
            if (strstr(name, "Warden of Darkness")) return "Dark Spear Aura";
            if (strstr(name, "Rising Torrent")) return "Water Slam Impact";
            if (strstr(name, "Flames of Judgment")) return "Fiery Strike";
            if (strstr(name, "Putrid Touch")) return "Poison Infliction";
            if (strstr(name, "Shadow Claw")) return "Shadow Slash";
            if (strstr(name, "Howling of Chaos")) return "Chaos Roar";
            if (strstr(name, "Pillar of Wind")) return "Wind Tornado";
            if (strstr(name, "Frost Spike")) return "Ice Shard Pierce";
            if (strstr(name, "Aptitude I") && !strstr(name, "II") && !strstr(name, "III")) return "Skill EXP +10%";
            if (strstr(name, "Aptitude III")) return "Skill EXP +30%";
            if (strstr(name, "Aptitude II")) return "Skill EXP +20%";
            if (strstr(name, "Fortune")) return "Money Drop +10%";
            if (strstr(name, "Efficiency")) return "Crafting Cost -10%";
            if (strstr(name, "Gourmet")) return "Food Duration +20%";
            if (strstr(name, "Equestrian")) return "Horse EXP +15%";
            if (strstr(name, "Companionship")) return "Companion Bond +10%";
            if (strstr(name, "Service")) return "Contribution EXP +10%";
            if (strstr(name, "Disarm")) return "Equipment Drop +5%";
            if (strstr(name, "Infinite Arrows")) return "Ammo Free Chance 20%";
            return "Abyss Power";
        }

        bool RebuildSnapshot(int selected, Equipment::SlotInfo* rows, int& count, uintptr_t& sourceComp)
        {
            count = 0;
            const uintptr_t comp = ClientComp(true, selected);
            if (!comp) return false;
            sourceComp = comp;

            const EquipTableDesc tbl = ReadEquipTableDesc(comp);
            if (!tbl.valid) return false;

            for (uint32_t i = 0; i < tbl.count && count < kMaxSlots; ++i)
            {
                const uintptr_t entry = tbl.array + static_cast<uintptr_t>(i) * tbl.stride;
                uint16_t tid = 0, tag = 0;
                int64_t inst = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid)) return false;
                if (tid == kInvSlot_EmptyType || tid == 0) continue;
                if (!Read64(entry + kOff_ItemVal_InstanceId, &inst) ||
                    !Read16(entry + tbl.tagOffset, &tag)) return false;

                char itemName[96] = "";
                if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                    snprintf(itemName, sizeof(itemName), "Item #%u", tid);

                // Exclude dummy unarmed placeholder gear (Ordinary Gloves) from Edit Equipment
                if (IsDummyOrUnarmed(tid, itemName)) continue;

                char fallbackName[64] = "";
                const char* nm = SlotNameForTag(tag);
                if (!nm)
                {
                    snprintf(fallbackName, sizeof(fallbackName), "Slot #%u", tag);
                    nm = fallbackName;
                }

                Equipment::SlotInfo& s = rows[count++];
                s = Equipment::SlotInfo{};
                s.tag        = tag;
                s.typeId     = tid;
                s.instanceId = inst;
                s.characterIndex = selected;
                s.controlledOwner = g_snapshotWorld;

                snprintf(s.slotName, sizeof(s.slotName), "%s", nm);
                snprintf(s.itemName, sizeof(s.itemName), "%s", itemName);
                Inventory::IconForTypeId(tid, s.icon, sizeof(s.icon));

                uint16_t refine = 0;
                if (!Read16(entry + kOff_ItemVal_RefineLevel, &refine)) return false;
                s.refineLevel = (refine > kRefine_Max) ? kRefine_Max : static_cast<int>(refine);

                uint16_t dura = 0;
                if (!Read16(entry + kOff_ItemVal_Durability, &dura)) return false;
                s.durability = static_cast<int>(dura);

                // Base stats & reinforcement calculation
                const bool isWeapon = (tag == 0 || tag == 12 || tag == 13 || tag == 2);
                const bool isShield = (tag == 1);
                const bool isArmor = (tag >= 3 && tag <= 6);

                int baseAtk = 0;
                int baseDef = 0;
                if (isWeapon) baseAtk = 14 + s.refineLevel;
                else if (isShield) { baseAtk = 0; baseDef = 20 + s.refineLevel * 2; }
                else if (isArmor) baseDef = (tag == 4 ? 35 : 20) + s.refineLevel * 2;

                s.reinforceExp = 72; // in-game default progress
                s.reinforceBonus = (s.refineLevel >= 4) ? 2 : 1;
                if (isWeapon) s.attack = baseAtk + s.reinforceBonus;
                if (isArmor || isShield) s.defense = baseDef + s.reinforceBonus;

                const SocketDesc sockets = ReadSocketDesc(entry);
                // Socket failure is local to this row. Publish the item and its
                // refine controls; distinguish unreadable from valid size zero.
                s.socketsAvailable = sockets.valid;
                const int maxSock = sockets.valid ? static_cast<int>(sockets.size) : 0;
                s.maxSockets = maxSock;
                s.unlockedCount = sockets.valid ? static_cast<int>(sockets.unlocked) : 0;

                for (auto& socket : s.sockets) socket.gearTypeId = kSock_Empty;

                for (int k = 0; k < maxSock; ++k)
                {
                    Equipment::Socket& so = s.sockets[k];
                    so.unlocked = (k < s.unlockedCount);
                    so.gearTypeId = GearAt(sockets, k);
                    so.filled = (so.gearTypeId != kSock_Empty);
                    if (so.filled)
                    {
                        if (!Inventory::NameForTypeId(so.gearTypeId, so.gearName, sizeof(so.gearName)))
                            snprintf(so.gearName, sizeof(so.gearName), "Gear #%u", so.gearTypeId);
                        Inventory::IconForTypeId(so.gearTypeId, so.gearIcon, sizeof(so.gearIcon));
                        const char* buff = ResolveGearBuff(so.gearName);
                        snprintf(so.gearBuff, sizeof(so.gearBuff), "%s", buff);

                        if (isWeapon)
                        {
                            if (strstr(so.gearBuff, "Attack 1")) s.attack += 1;
                            else if (strstr(so.gearBuff, "Attack 2")) s.attack += 2;
                            else if (strstr(so.gearBuff, "Attack 3")) s.attack += 3;
                            else if (strstr(so.gearBuff, "Attack 5")) s.attack += 5;
                        }
                        ++s.filledCount;
                    }
                }
                uint16_t currentType = 0, currentTag = 0;
                int64_t currentInstance = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &currentType) || currentType != tid ||
                    !Read16(entry + tbl.tagOffset, &currentTag) || currentTag != tag ||
                    !Read64(entry + kOff_ItemVal_InstanceId, &currentInstance) || currentInstance != inst)
                    return false;
            }
            const EquipTableDesc current = ReadEquipTableDesc(comp);
            return current.valid && current.desc == tbl.desc && current.array == tbl.array &&
                current.count == tbl.count && current.stride == tbl.stride && current.tagOffset == tbl.tagOffset;
        }

        // Call only with g_snapshotMutex held. Rows own their display data; the
        // world stamp is comparison-only. Game-thread writers increment the epoch.
        bool SnapshotContextCurrent()
        {
            const uintptr_t world = Player::GetControlledOwner();
            const int selection = s_activeCharIdx.load(std::memory_order_acquire);
            const int autoCharacter = selection < 0 ? Player::GetActiveCharacterIdx() : -1;
            if (world == g_snapshotWorld && selection == g_snapshotSelection &&
                autoCharacter == g_snapshotAutoCharacter) return true;
            g_snapshotWorld = world;
            g_snapshotSelection = selection;
            g_snapshotAutoCharacter = autoCharacter;
            g_slotCount = 0;
            g_snapshotReady = false;
            g_snapshotPersist = false;
            g_snapshotAttempted = false;
            g_snapshotReadFailed = false;
            return false;
        }

        void ExpireSnapshot(ULONGLONG now)
        {
            if (g_snapshotReadFailed && now - g_lastSnapshotSuccess >= kSnapshotGraceMs)
            {
                g_slotCount = 0;
                g_snapshotReady = false;
                g_snapshotPersist = false;
            }
        }

        void EnsureSnapshot()
        {
            SnapshotContextCurrent();
            const ULONGLONG now = GetTickCount64();
            ExpireSnapshot(now);
            const int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
            // Ready, SlotCount and EditsPersist may all run in one render frame.
            // Even an explicit invalidation is consumed at the next publication.
            if (frame >= 0 && frame == g_lastSnapshotFrame) return;
            const uint64_t epoch = g_snapshotEpoch.load(std::memory_order_acquire);
            // Invalidation marks content dirty, not permission to bypass the
            // publication cap on every slider step / retry. World and selection
            // changes reset g_snapshotAttempted via SnapshotContextCurrent.
            if (g_snapshotAttempted && now - g_lastSnapshotAttempt < kSnapshotIntervalMs) return;
            g_snapshotAttempted = true;
            g_lastSnapshotFrame = frame;
            const int selected = g_snapshotSelection < 0 ? Inventory::ActivePlayerCharacterIdx() : g_snapshotSelection;
            Equipment::SlotInfo rows[kMaxSlots]{};
            int count = 0;
            uintptr_t sourceComp = 0;
            const bool ready = IsValidCanonicalPtr(g_snapshotWorld) && selected >= 0 && selected < 3 &&
                RebuildSnapshot(selected, rows, count, sourceComp);
            // A fallback pointer does not by itself prove server authority.
            const bool persist = ready && GetNativeEquipKind(sourceComp) == NativeEquipKind::Server;
            // Selection/world or an edit during a copy invalidate publication.
            if (!SnapshotContextCurrent() || g_snapshotEpoch.load(std::memory_order_acquire) != epoch) return;
            g_lastSnapshotAttempt = GetTickCount64();
            g_snapshotReadFailed = !ready;
            if (ready)
            {
                for (int i = 0; i < count; ++i) g_slots[i] = rows[i];
                g_slotCount = count;
                g_snapshotReady = true;
                g_snapshotPersist = persist;
                g_lastSnapshotSuccess = g_lastSnapshotAttempt;
            }
            else ExpireSnapshot(g_lastSnapshotAttempt);
        }
    }

    const char* Equipment::SlotNameForTag(uint16_t tag)
    {
        switch (tag)
        {
        case 0:  return "Main Hand";
        case 1:  return "Off-Hand";
        case 2:  return "Ranged Weapon";
        case 3:  return "Helmet";
        case 4:  return "Chest";
        case 5:  return "Gloves";
        case 6:  return "Boots";
        case 7:  return "Earring 1";
        case 8:  return "Earring 2";
        case 9:  return "Necklace";
        case 10: return "Ring 1";
        case 11: return "Ring 2";
        case 12: return "Dagger";
        case 13: return "Two-Handed Weapon";
        case 14: return "Saddle";
        case 15: return "Lantern";
        case 16: return "Cloak";
        case 17: return "Glasses";
        case 18: return "Mask";
        case 19: return "Backpack";
        case 20: return "Bracelet";
        case 21: return "Rocket";
        case 22: return "Chamfron";
        case 23: return "Horse Armor";
        case 24: return "Stirrups";
        case 25: return "Horseshoes";
        default: return nullptr;
        }
    }

    bool Equipment::IsRefinableTag(uint16_t tag)
    {
        switch (tag)
        {
        case 0:  // Main Hand Weapon
        case 1:  // Off-Hand Weapon / Shield
        case 2:  // Ranged Weapon (Bow / Arbalest)
        case 3:  // Helmet / Headgear
        case 4:  // Chest Armor
        case 5:  // Gloves
        case 6:  // Boots
        case 7:  // Earring 1
        case 8:  // Earring 2
        case 9:  // Necklace
        case 10: // Ring 1
        case 11: // Ring 2
        case 12: // Dagger
        case 13: // Two-Handed Weapon
        case 16: // Cloak
            return true;
        default:
            // Exclude Lantern (15), Axiom Bracelet (20), Backpack (19), Glasses (17), Mask (18), Mount gear (14, 21-25)
            // These items have no enhancement curve in engine ItemInfo; refining them causes 0xC0000005 crash.
            return false;
        }
    }

    const char* Equipment::CharacterName(int index)
    {
        switch (index)
        {
        case 0: return "Kliff";
        case 1: return "Damiane";
        case 2: return "Oongka";
        default: return "Unknown";
        }
    }

    bool Equipment::IsItemForCharacter(int charIdx, uint16_t typeId, const char* name, const char* key)
    {
        if (charIdx < 0 || charIdx > 2) return true;

        auto ContainsCi = [](const char* haystack, const char* needle) -> bool {
            if (!haystack || !needle || !*needle) return false;
            const size_t nlen = strlen(needle);
            for (; *haystack; ++haystack)
            {
                if (_strnicmp(haystack, needle, nlen) == 0)
                    return true;
            }
            return false;
        };

        // Damiane (1): Royal Oath, Demenissian Hero's Musket, Caliburn, shotguns, rapiers, fencing blades, Spencer, Dewhaven, Rivenheim Cloth/Fabric Armor
        if (charIdx == 1)
        {
            if (typeId == 53935 || typeId == 6324 || typeId == 6041 || typeId == 5306 || typeId == 5300 ||
                typeId == 5297 || typeId == 5277 || typeId == 3463 || (typeId >= 5450 && typeId <= 5468) ||
                (typeId >= 5270 && typeId <= 5310) || (typeId >= 6320 && typeId <= 6330))
                return true;
            if (name && (ContainsCi(name, "Rapier") || ContainsCi(name, "Damian") || ContainsCi(name, "Demian") ||
                         ContainsCi(name, "Spencer") || ContainsCi(name, "Dewhaven") || ContainsCi(name, "White Wind") ||
                         ContainsCi(name, "WhiteWind") || ContainsCi(name, "Hwando") || ContainsCi(name, "Demeniss") ||
                         ContainsCi(name, "Fencing") || ContainsCi(name, "Dual Blade") || ContainsCi(name, "DualBlade") ||
                         ContainsCi(name, "Musket") || ContainsCi(name, "Shotgun") || ContainsCi(name, "Caliburn") ||
                         ContainsCi(name, "Royal Oath") || ContainsCi(name, "Rivenheim") ||
                         ContainsCi(name, "Cloth Armor") || ContainsCi(name, "Cloth")))
                return true;
            if (key && (ContainsCi(key, "Rapier") || ContainsCi(key, "Damian") || ContainsCi(key, "Demian") ||
                        ContainsCi(key, "Spencer") || ContainsCi(key, "Dewhaven") || ContainsCi(key, "WhiteWind") ||
                        ContainsCi(key, "White_Wind") || ContainsCi(key, "Hwando") || ContainsCi(key, "Demeniss") ||
                        ContainsCi(key, "Fencing") || ContainsCi(key, "DualBlade") || ContainsCi(key, "Dual_Blade") ||
                        ContainsCi(key, "DualRapier") || ContainsCi(key, "Dual_Rapier") ||
                        ContainsCi(key, "Musket") || ContainsCi(key, "Shotgun") || ContainsCi(key, "Caliburn") ||
                        ContainsCi(key, "RoyalOath") || ContainsCi(key, "Royal_Oath") || ContainsCi(key, "Rivenheim") ||
                        ContainsCi(key, "ClothArmor") || ContainsCi(key, "Cloth_Armor") ||
                        ContainsCi(key, "Fabric") || ContainsCi(key, "Fabric_")))
                return true;
            return false;
        }

        // Oongka (2)
        if (charIdx == 2)
        {
            if (typeId == 6560 || typeId == 6042 || typeId == 6305 || (typeId >= 6550 && typeId <= 6570) ||
                typeId == 2299 || typeId == 3740 || (typeId >= 3762 && typeId <= 3777) ||
                (typeId >= 1090 && typeId <= 1094) || typeId == 1390)
                return true;
            if (name && (ContainsCi(name, "Oongka") || ContainsCi(name, "Giant") || ContainsCi(name, "Tynion") ||
                         ContainsCi(name, "Rocket") || ContainsCi(name, "Cannon") || ContainsCi(name, "Club") ||
                         ContainsCi(name, "Hammer") || ContainsCi(name, "Heavy Mace") || ContainsCi(name, "Greatshield") ||
                         ContainsCi(name, "Gauntlet") || ContainsCi(name, "Valortread") || ContainsCi(name, "Belkandor") ||
                         ContainsCi(name, "Ashen Wolf") || ContainsCi(name, "Brass Warden") || ContainsCi(name, "Kuku") ||
                         ContainsCi(name, "Daeil") || ContainsCi(name, "Troll") || ContainsCi(name, "Ordinary Gloves") ||
                         ContainsCi(name, "Wells") || ContainsCi(name, "Well") || ContainsCi(name, "Betrayer") ||
                         ContainsCi(name, "Two-Handed") || ContainsCi(name, "War Hammer") || ContainsCi(name, "Halberd") ||
                         ContainsCi(name, "Silverwolf") || ContainsCi(name, "Silver Wolf") || ContainsCi(name, "Axe") ||
                         ContainsCi(name, "Plate Armor") || ContainsCi(name, "Horned Helmet") || ContainsCi(name, "Horned") ||
                         ContainsCi(name, "Heavy Plate") || ContainsCi(name, "Heavy Armor")))
                return true;
            if (key && (ContainsCi(key, "Oongka") || ContainsCi(key, "Giant") || ContainsCi(key, "Tynion") ||
                        ContainsCi(key, "Rocket") || ContainsCi(key, "Cannon") || ContainsCi(key, "Club") ||
                        ContainsCi(key, "Hammer") || ContainsCi(key, "Greatshield") || ContainsCi(key, "Gauntlet") ||
                        ContainsCi(key, "HeavyMace") || ContainsCi(key, "Heavy_Mace") || ContainsCi(key, "Valortread") ||
                        ContainsCi(key, "Belkandor") || ContainsCi(key, "Ashen_Wolf") || ContainsCi(key, "AshenWolf") ||
                        ContainsCi(key, "Brass_Warden") || ContainsCi(key, "BrassWarden") || ContainsCi(key, "Kuku") ||
                        ContainsCi(key, "Daeil") || ContainsCi(key, "Troll") || ContainsCi(key, "Fist") ||
                        ContainsCi(key, "Wells") || ContainsCi(key, "Well") || ContainsCi(key, "Betrayer") ||
                        ContainsCi(key, "TwoHanded") || ContainsCi(key, "WarHammer") || ContainsCi(key, "Alebard") ||
                        ContainsCi(key, "Silverwolf") || ContainsCi(key, "Silver_Wolf") || ContainsCi(key, "SilverWolf") ||
                        ContainsCi(key, "Axe") || ContainsCi(key, "PlateArmor") || ContainsCi(key, "Plate_Armor") ||
                        ContainsCi(key, "Horned") || ContainsCi(key, "HeavyPlate") || ContainsCi(key, "Heavy_Plate") ||
                        ContainsCi(key, "HeavyArmor") || ContainsCi(key, "Heavy_Armor")))
                return true;
            return false;
        }

        // Kliff (0)
        if (charIdx == 0)
        {
            if (IsItemForCharacter(1, typeId, name, key) || IsItemForCharacter(2, typeId, name, key))
                return false;
            return true;
        }

        return true;
    }

    bool Equipment::IsItemForSlot(uint16_t slotTag, uint16_t /*typeId*/, const char* name, const char* key)
    {
        // Tag 0 = Main Hand
        if (slotTag == 0)
        {
            if (name && (strstr(name, "Sword") || strstr(name, "Rapier") || strstr(name, "Mace") ||
                         strstr(name, "Axe") || strstr(name, "Blade") || strstr(name, "Branch") ||
                         strstr(name, "Stalk") || strstr(name, "Hand") || strstr(name, "Hammer") ||
                         strstr(name, "Club") || strstr(name, "Weapon") || strstr(name, "Greatsword") ||
                         strstr(name, "Hwando") || strstr(name, "DarknessKing") || strstr(name, "Balgran") ||
                         strstr(name, "Aeserion")))
                return true;
            if (key && (strstr(key, "Weapon") || strstr(key, "Sword") || strstr(key, "Rapier") ||
                        strstr(key, "Mace") || strstr(key, "Axe") || strstr(key, "Hammer") ||
                        strstr(key, "Club") || strstr(key, "Hwando") || strstr(key, "Blade")))
                return true;
            return false;
        }
        // Tag 1 = Off Hand
        if (slotTag == 1)
        {
            if (name && (strstr(name, "Shield") || strstr(name, "Buckler") || strstr(name, "Off-Hand") ||
                         strstr(name, "Sub") || strstr(name, "Dagger") || strstr(name, "Sheath") ||
                         strstr(name, "Guard")))
                return true;
            if (key && (strstr(key, "Shield") || strstr(key, "SubWeapon") || strstr(key, "Dagger")))
                return true;
            return false;
        }
        // Tag 2 = Ranged Weapon
        if (slotTag == 2)
        {
            if (name && (strstr(name, "Bow") || strstr(name, "Crossbow") || strstr(name, "Rocket") ||
                         strstr(name, "Cannon") || strstr(name, "Arrow")))
                return true;
            if (key && (strstr(key, "Bow") || strstr(key, "Ranged") || strstr(key, "Rocket") || strstr(key, "Cannon")))
                return true;
            return false;
        }
        // Tag 3 = Helmet
        if (slotTag == 3)
        {
            if (name && (strstr(name, "Helm") || strstr(name, "Head") || strstr(name, "Hood") ||
                         strstr(name, "Hat") || strstr(name, "Cap") || strstr(name, "Crown") ||
                         strstr(name, "Circlet") || strstr(name, "Mask")))
                return true;
            if (key && (strstr(key, "Helm") || strstr(key, "Head") || strstr(key, "Hood") || strstr(key, "Circlet")))
                return true;
            return false;
        }
        // Tag 4 = Chest Armor
        if (slotTag == 4)
        {
            if (name && (strstr(name, "Armor") || strstr(name, "Chest") || strstr(name, "Tunic") ||
                         strstr(name, "Robe") || strstr(name, "Vest") || strstr(name, "Plate") ||
                         strstr(name, "Coat") || strstr(name, "Cuirass") || strstr(name, "Mail")))
                return true;
            if (key && (strstr(key, "Armor") || strstr(key, "Chest") || strstr(key, "Tunic") ||
                        strstr(key, "Robe") || strstr(key, "Body")))
                return true;
            return false;
        }
        // Tag 5 = Gloves
        if (slotTag == 5)
        {
            if (name && (strstr(name, "Glove") || strstr(name, "Gauntlet") || strstr(name, "Bracer") ||
                         strstr(name, "Hand") || strstr(name, "Vambrace")))
                return true;
            if (key && (strstr(key, "Glove") || strstr(key, "Gauntlet") || strstr(key, "Bracer") || strstr(key, "Hand")))
                return true;
            return false;
        }
        // Tag 6 = Boots
        if (slotTag == 6)
        {
            if (name && (strstr(name, "Boot") || strstr(name, "Shoe") || strstr(name, "Greave") ||
                         strstr(name, "Sabaton") || strstr(name, "Foot") || strstr(name, "Footwear")))
                return true;
            if (key && (strstr(key, "Boot") || strstr(key, "Shoe") || strstr(key, "Greave") || strstr(key, "Foot")))
                return true;
            return false;
        }
        // Tag 7..11 = Accessories
        if (slotTag >= 7 && slotTag <= 11)
        {
            if (name && (strstr(name, "Ring") || strstr(name, "Earring") || strstr(name, "Necklace") ||
                         strstr(name, "Pendant") || strstr(name, "Amulet") || strstr(name, "Bracelet") ||
                         strstr(name, "Belt")))
                return true;
            if (key && (strstr(key, "Ring") || strstr(key, "Earring") || strstr(key, "Necklace") ||
                        strstr(key, "Accessory") || strstr(key, "Bracelet")))
                return true;
            return false;
        }
        // Tag 14..25 = Mount Gear
        if (slotTag >= 14 && slotTag <= 25)
        {
            if (name && (strstr(name, "Saddle") || strstr(name, "Chamfron") || strstr(name, "Barding") ||
                         strstr(name, "Horse") || strstr(name, "Stirrup") || strstr(name, "Shoe") ||
                         strstr(name, "Horseshoe") || strstr(name, "Mount")))
                return true;
            return false;
        }

        return true;
    }

    bool Equipment::Install()
    {
        {
            std::lock_guard<std::mutex> worker(g_profileWorkerMutex);
            g_profileStopping = false;
        }
        ClearRefineRequests();
        InvalidateSnapshot();
        g_refresh = nullptr;
        if (core::GetGameVersion().revision >= 2800)
            LOG("equipment: TU 2.02 false refresh signature disabled (native ChallengeDescription UI, not equipment).");
        else
        {
            g_refresh = reinterpret_cast<EquipRefresh_t>(mem::FindPattern(kSig_EquipEffectRefresh));
            if (!g_refresh)
                g_refresh = reinterpret_cast<EquipRefresh_t>(mem::FindPattern(kSig_EquipEffectRefresh_Legacy));
            if (g_refresh)
                LOG("equipment: EquipEffectRefresh resolved @ %p.", reinterpret_cast<void*>(g_refresh));
            else
                LOG_WARN("equipment: EquipEffectRefresh signature not found.");
        }

        // 1. Load Persistent Equipment Profiles from Disk (Trinity_EquipmentProfile.ini)
        LoadEquipProfilesFromDisk();

        return true;
    }

    void Equipment::Remove()
    {
        ClearRefineRequests();
        // Join an outstanding value-only save before the ASI can unload.
        std::lock_guard<std::mutex> worker(g_profileWorkerMutex);
        g_profileStopping = true;
        if (g_profileSaveWorker.valid())
        {
            try { g_profileSaveWorker.get(); } catch (...) {}
        }
        {
            std::lock_guard<std::mutex> lock(g_gearCatMutex);
            g_gearCat = -1;
            g_nextGearCatLookup = 0;
        }
        InvalidateSnapshot();
        g_refresh = nullptr;
        g_dirty.store(false, std::memory_order_release);
    }

    bool Equipment::Ready()
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        EnsureSnapshot();
        return g_snapshotReady;
    }

    void Equipment::ForceRefresh()
    {
        InvalidateSnapshot();
        g_dirty.store(true, std::memory_order_release);
    }

    bool Equipment::EditsPersist()
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        EnsureSnapshot();
        return g_snapshotPersist;
    }

    uintptr_t Equipment::ClientCompFor(int charIdx)
    {
        return charIdx >= 0 && charIdx < 3 ? ClientComp(false, charIdx) : 0;
    }

    Equipment::EditState Equipment::Status()
    {
        const EditState state = g_editState.load(std::memory_order_acquire);
        if (state != EditState::Pending && state != EditState::Idle)
        {
            EditState expected = state;
            g_editState.compare_exchange_strong(expected, EditState::Idle, std::memory_order_acq_rel);
        }
        return state;
    }

    bool Equipment::HasEditResult()
    {
        const EditState state = g_editState.load(std::memory_order_acquire);
        return state == EditState::Synced || state == EditState::DataOnly || state == EditState::Failed;
    }

    uintptr_t Equipment::ServerCompFor(int charIdx)
    {
        return charIdx >= 0 && charIdx < 3 ? ServerComp(charIdx) : 0;
    }

    void Equipment::SetActiveCharacter(int index)
    {
        if (s_activeCharIdx.exchange(index, std::memory_order_acq_rel) == index) return;
        InvalidateSnapshot();
        LOG("equipment: active character switched to '%s' (index %d).", CharacterName(index), index);
    }

    int Equipment::GetActiveCharacter()
    {
        const int selected = s_activeCharIdx.load(std::memory_order_acquire);
        if (selected < 0)
            return Inventory::ActivePlayerCharacterIdx();
        return selected;
    }

    int Equipment::SlotCount()
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        EnsureSnapshot();
        return g_slotCount;
    }

    int Equipment::MaxSocketsForTag(uint16_t tag)
    {
        return GetMaxSocketsForTag(tag);
    }

    bool Equipment::GetSlot(int idx, SlotInfo* out)
    {
        if (!out) return false;
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        // Fetch from the Ready/SlotCount publication; never start a second
        // native read mid-enumeration, even if a writer increments the epoch.
        SnapshotContextCurrent();
        ExpireSnapshot(GetTickCount64());
        if (idx < 0 || idx >= g_slotCount) return false;
        *out = g_slots[idx];
        return true;
    }

    int Equipment::GearCount()
    {
        const int c = GearCategory();
        return (c < 0) ? 0 : Inventory::CatalogItemCount(c);
    }

    bool Equipment::GetGear(int idx, uint16_t* typeId, const char** name, const char** icon)
    {
        const int c = GearCategory();
        if (c < 0) return false;
        Inventory::ItemInfo info{};
        if (!Inventory::GetCatalogItem(c, idx, &info)) return false;
        if (typeId) *typeId = info.typeId;
        if (name)   *name   = info.name;
        if (icon)   *icon   = info.icon;
        return true;
    }

    const char* Equipment::GetGearBuffDescription(const char* name)
    {
        return ResolveGearBuff(name);
    }

    // --- Edits -------------------------------------------------------------
    bool Equipment::AddGear(uint16_t tag, int socketIdx, uint16_t gearTypeId, bool* persisted, const SlotInfo* expected)
    {
        if (persisted) *persisted = false;
        if (socketIdx < 0 || socketIdx >= kMaxSockets || gearTypeId == 0 || gearTypeId == kSock_Empty) return false;
        if (Inventory::IsTransactionActive()) return false;

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        if (expected && (expected->tag != tag || !expected->socketsAvailable ||
            !DisplayedTargetMatches(entry, expected))) return false;
        const SocketDesc sockets = ReadWritableSocketDesc(entry);
        if (!sockets.valid || static_cast<uint32_t>(socketIdx) >= sockets.unlocked) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // zero-ID copies are component-only

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        return QueueEquipmentEdit(EquipmentEdit::Socket, tag, socketIdx, gearTypeId, expected);
    }

    bool Equipment::ClearGear(uint16_t tag, int socketIdx, bool* persisted, const SlotInfo* expected)
    {
        if (persisted) *persisted = false;
        if (socketIdx < 0 || socketIdx >= kMaxSockets) return false;
        if (Inventory::IsTransactionActive()) return false;

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        if (expected && (expected->tag != tag || !expected->socketsAvailable ||
            !DisplayedTargetMatches(entry, expected))) return false;
        const SocketDesc sockets = ReadWritableSocketDesc(entry);
        if (!sockets.valid || static_cast<uint32_t>(socketIdx) >= sockets.unlocked) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // zero-ID copies are component-only

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        return QueueEquipmentEdit(EquipmentEdit::Socket, tag, socketIdx, kSock_Empty, expected);
    }

    bool Equipment::SetRefine(uint16_t tag, int level, bool* persisted, const SlotInfo* expected)
    {
        // This API now reports enqueue success. No durable-write claim can be
        // made on the render thread; Tick logs actual component/holder writes.
        if (persisted) *persisted = false;
        if (!IsRefinableTag(tag) || !Player::Ready()) return false;

        RefineRequest req{};
        req.charIndex = GetActiveCharacter();
        if (req.charIndex < 0 || req.charIndex > 2) return false;
        req.tag = tag;
        req.component = ClientComp(false, req.charIndex);
        req.table = ReadEquipTableDesc(req.component);
        if (!req.table.valid ||
            !ReadPtr(req.component + kOff_EquipComp_Owner, &req.owner) || !IsValidCanonicalPtr(req.owner) ||
            !ReadPtr(req.owner + kOff_Container_Sub, &req.sub) || !IsValidCanonicalPtr(req.sub))
            return false;
        req.controlledOwner = Player::GetControlledOwner();
        if (!IsValidCanonicalPtr(req.controlledOwner)) return false;
        req.entry = FindEntryByTag(req.component, tag);
        if (!req.entry || !Read16(req.entry + kOff_InvSlot_TypeId, &req.typeId) ||
            req.typeId == 0 || req.typeId == kInvSlot_EmptyType ||
            !Read64(req.entry + kOff_ItemVal_InstanceId, &req.instanceId))
            return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(req.typeId, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(req.typeId, itemName)) return false;

        if (level < 0) level = 0;
        if (level > kRefine_Max) level = kRefine_Max;
        req.level = static_cast<uint16_t>(level);
        req.queuedAt = GetTickCount64();
        // Validate after stamping: never enqueue the new item B merely because
        // it occupies the tag displayed by cached item A. No socket dependency.
        if (expected && (expected->tag != tag || expected->characterIndex != req.charIndex ||
            expected->controlledOwner != req.controlledOwner || expected->typeId != req.typeId ||
            expected->instanceId != req.instanceId || !DisplayedTargetMatches(req.entry, expected))) return false;
        req.boundSelection = expected != nullptr;
        req.serial = g_editSerial.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!QueueRefine(req))
        {
            LOG_WARN("equipment: refinement queue full; rejected char=%d tag=%u.", req.charIndex, tag);
            return false;
        }
        g_editState.store(EditState::Pending, std::memory_order_release);
        LOG("equipment: refinement queued char=%d tag=%u type=%u instance=0x%llX level=%u comp=%p owner=%p.",
            req.charIndex, req.tag, req.typeId, static_cast<unsigned long long>(req.instanceId), req.level,
            reinterpret_cast<void*>(req.component), reinterpret_cast<void*>(req.owner));
        return true;
    }

    bool Equipment::UnlockAll(uint16_t tag, const SlotInfo* expected)
    {
        if (Inventory::IsTransactionActive()) return false;
        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        if (expected && (expected->tag != tag || !expected->socketsAvailable ||
            !DisplayedTargetMatches(entry, expected))) return false;
        const SocketDesc sockets = ReadWritableSocketDesc(entry);
        if (!sockets.valid || !sockets.size) return false;
        const int maxSock = static_cast<int>(sockets.size);
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // zero-ID copies are component-only

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        return QueueEquipmentEdit(EquipmentEdit::Unlock, tag, maxSock, kSock_Empty, expected);
    }

    bool Equipment::ClearAll(uint16_t tag, const SlotInfo* expected)
    {
        if (Inventory::IsTransactionActive()) return false;
        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        if (expected && (expected->tag != tag || !expected->socketsAvailable ||
            !DisplayedTargetMatches(entry, expected))) return false;
        const SocketDesc sockets = ReadWritableSocketDesc(entry);
        if (!sockets.valid || !sockets.unlocked) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // zero-ID copies are component-only

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        return QueueEquipmentEdit(EquipmentEdit::ClearSockets, tag, 0, kSock_Empty, expected);
    }

    bool Equipment::RepairAll(int* repairedCount)
    {
        if (repairedCount) *repairedCount = 0;
        int repaired = 0;

        auto repairEntry = [&](uintptr_t entry) {
            if (entry < kMinPointer) return;
            uint16_t tid = 0;
            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) return;
            uint16_t durability = 0;
            if (!Read16(entry + kOff_ItemVal_Durability, &durability) || durability >= 10000) return;
            char itemName[96] = "";
            Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
            if (IsDummyOrUnarmed(tid, itemName)) return;
            if (Write16(entry + kOff_ItemVal_Durability, 10000)) ++repaired;
        };

        // 1. Client Equip Component
        const uintptr_t comp = ClientComp();
        if (comp)
        {
            const EquipTableDesc tbl = ReadEquipTableDesc(comp);
            if (tbl.valid)
            {
                for (uint32_t i = 0; i < tbl.count; ++i)
                    repairEntry(tbl.array + static_cast<uintptr_t>(i) * tbl.stride);
            }
        }

        // 2. Server Equip Component
        const uintptr_t scomp = ServerComp();
        if (scomp && scomp != comp)
        {
            uint8_t oldFlag = 0;
            const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
            if (flagAddr && RawWrite8(flagAddr, 1))
            {
                const EquipTableDesc stbl = ReadEquipTableDesc(scomp);
                if (stbl.valid)
                {
                    for (uint32_t s = 0; s < stbl.count; ++s)
                        repairEntry(stbl.array + static_cast<uintptr_t>(s) * stbl.stride);
                }
                RawWrite8(flagAddr, oldFlag);
            }
        }

        // 3. Profile Equip Component for in-game inspect menu
        const uintptr_t profC = ProfileComp(GetActiveCharacter());
        if (profC && profC != comp && profC != scomp)
        {
            const EquipTableDesc ptbl = ReadEquipTableDesc(profC);
            if (ptbl.valid)
            {
                for (uint32_t p = 0; p < ptbl.count; ++p)
                    repairEntry(ptbl.array + static_cast<uintptr_t>(p) * ptbl.stride);
            }
        }

        if (repairedCount) *repairedCount = repaired;
        if (repaired > 0) ForceRefresh();
        if (repaired > 0)
            LOG("equipment: repaired %d equipped items to 100%% durability (10,000).", repaired);
        return repaired > 0;
    }

    bool Equipment::RefineAll(int level, int* refinedCount)
    {
        if (refinedCount) *refinedCount = 0;
        uint16_t tags[64]{};
        const int total = ReadEditTags(tags);
        if (total <= 0) return false;
        int count = 0;
        for (int i = 0; i < total; ++i)
        {
            if (IsRefinableTag(tags[i]) && SetRefine(tags[i], level)) ++count;
        }
        if (refinedCount) *refinedCount = count;
        LOG("equipment: [%s] Queued refinement for %d eligible equipped pieces to +%d.", CharacterName(GetActiveCharacter()), count, level);
        return count > 0;
    }

    bool Equipment::UnlockAllGears(int* unlockedCount)
    {
        if (unlockedCount) *unlockedCount = 0;
        uint16_t tags[64]{};
        const int total = ReadEditTags(tags);
        if (total <= 0) return false;
        int count = 0;
        for (int i = 0; i < total; ++i)
        {
            if (UnlockAll(tags[i])) ++count;
        }
        if (unlockedCount) *unlockedCount = count;
        LOG("equipment: [%s] Queued socket unlocks on %d pieces.", CharacterName(GetActiveCharacter()), count);
        return count > 0;
    }

    bool Equipment::EquipItemToSlot(uint16_t tag, uint16_t typeId, int64_t instId, const SlotInfo* expected)
    {
        if (typeId == 0 || typeId == kInvSlot_EmptyType) return false;
        if (Inventory::IsTransactionActive()) return false;
        const int charIdx = GetActiveCharacter();
        if (charIdx < 0 || charIdx > 2) return false;
        const uintptr_t comp = ClientComp(false, charIdx);
        const uintptr_t entry = comp ? FindEntryByTag(comp, tag) : 0;
        if (!entry || (expected && (expected->tag != tag || !DisplayedTargetMatches(entry, expected))))
            return false;
        if (instId == 0)
        {
            static std::atomic<int64_t> s_nextInstId{ 0x7000000000000000LL };
            instId = ++s_nextInstId;
        }

        auto stampItem = [&](uintptr_t entry) -> bool {
            if (Inventory::IsTransactionActive() || !DisplayedTargetMatches(entry, expected) ||
                !WritableDataRange(entry, kOff_ItemVal_Durability + sizeof(uint16_t))) return false;
            if (!Write64(entry + kOff_ItemVal_InstanceId, instId) ||
                !Write16(entry + kOff_InvSlot_TypeId, typeId) ||
                !Write16(entry + kOff_ItemVal_Durability, 10000) ||
                !Write64(entry + kOff_InvSlot_Quantity, 1) ||
                !Write16(entry + kOff_ItemVal_RefineLevel, 10)) return false;
            EnsureSocketVector(entry);
            return true;
        };

        // The displayed source item must succeed before mirroring its replacement.
        if (!stampItem(entry)) return false;

        // 2. Server Realm Mirror
        uint8_t oldFlag = 0;
        const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
        if (flagAddr && RawWrite8(flagAddr, 1))
        {
            const uintptr_t scomp = ServerComp(charIdx);
            if (scomp)
            {
                const uintptr_t se = FindEntryByTag(scomp, tag);
                if (se) stampItem(se);
            }
            RawWrite8(flagAddr, oldFlag);
        }

        if (const uintptr_t profC = ProfileComp(charIdx))
        {
            const uintptr_t pe = FindEntryByTag(profC, tag);
            if (pe) stampItem(pe);
        }

        SyncSlotToProfile(charIdx, tag);

        ForceRefresh();

        const char* charName = CharacterName(charIdx);
        char itemName[64] = "";
        if (!Inventory::NameForTypeId(typeId, itemName, sizeof(itemName)))
            snprintf(itemName, sizeof(itemName), "Item #%u", typeId);
        const char* slotName = SlotNameForTag(tag);
        LOG("equipment: [%s] Slot [%s (Tag %u)] -> Directly Equipped '%s' (TypeID %u, InstID 0x%llX).",
            charName, slotName ? slotName : "Unknown", tag, itemName, typeId, static_cast<unsigned long long>(instId));

        return true;
    }

    void Equipment::SaveEquipProfilesToDisk()
    {
        trinity::game::SaveEquipProfilesToDisk();
    }

    void Equipment::LoadEquipProfilesFromDisk()
    {
        trinity::game::LoadEquipProfilesFromDisk();
    }

    void Equipment::SavePlayerEquipSlot(int charIdx, uint16_t tag, uint16_t refineLvl, int maxSock, const uint16_t* gems)
    {
        if (charIdx < 0 || charIdx >= 3 || tag >= 32) return;
        {
            std::lock_guard<std::mutex> lock(g_profileMutex);
            auto& prof = s_savedEquipSlots[charIdx][tag];
            prof.active = true;
            prof.tag = tag;
            prof.refineLevel = refineLvl > 10 ? 10 : refineLvl;
            prof.unlockedSockets = maxSock < 0 ? 0 : (maxSock > 5 ? 5 : maxSock);
            for (int k = 0; k < kSocket_Max; ++k)
                prof.socketGems[k] = gems ? gems[k] : kSock_Empty;
        }
        SaveEquipProfilesToDisk();
    }

    void Equipment::ClearPlayerEquipSlot(int charIdx, uint16_t tag)
    {
        if (charIdx < 0 || charIdx >= 3 || tag >= 32) return;
        {
            std::lock_guard<std::mutex> lock(g_profileMutex);
            s_savedEquipSlots[charIdx][tag].active = false;
        }
        SaveEquipProfilesToDisk();
    }

    bool Equipment::HasCustomProfile(int charIdx, uint16_t tag)
    {
        if (charIdx < 0 || charIdx >= 3 || tag >= 32) return false;
        std::lock_guard<std::mutex> lock(g_profileMutex);
        return s_savedEquipSlots[charIdx][tag].active;
    }

    // Game-thread refinement queue and optional older-build effect upkeep.
    void Equipment::Tick()
    {
        PumpEquipProfileSave();
        if (!Player::Ready())
        {
            ClearRefineRequests();
            return;
        }

        ProcessPendingRefines();

        const State& st = State::Get();

        // Infinite Item Durability: keep all equipped weapons, shields, and armor
        // pinned at 100% (10,000 max durability) on both client and server realms.
        if (st.infDurability && !Inventory::IsTransactionActive())
        {
            static ULONGLONG s_lastRepair = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - s_lastRepair >= 1500)
            {
                RepairAll();
                s_lastRepair = now;
            }
        }

        // No automatic profile replay. Old versionless character/tag profiles
        // cannot establish item ownership after reload; native saves own the data.

        if (!g_dirty.exchange(false, std::memory_order_acq_rel)) return;
        if (core::GetGameVersion().revision >= 2800) return;
        if (!g_refresh) return;

        const uintptr_t comp = ClientComp();
        if (!comp)
        {
            g_dirty.store(true, std::memory_order_release); // not ready - retry next frame (HEAD behavior)
            return;
        }

        // HEAD behavior: g_refresh ran on whatever component ClientComp()
        // resolved, no off-screen gate. The DXGI-crash concern is handled by
        // the possessor probe below instead: only run the refresh when the
        // target comp actually has a local controller (i.e. it is the
        // possessed body's render component). Companion/profile comps fail
        // the probe and are skipped without touching render structs.
        uintptr_t probeActor = 0, probePoss = 0, probePawn = 0;
        const bool hasController =
            ReadPtr(comp + kOff_EquipComp_Owner, &probeActor) && probeActor >= kMinPointer &&
            ReadPtr(probeActor + kOff_Owner_Possessor, &probePoss) && probePoss >= kMinPointer &&
            ReadPtr(probePoss + kOff_Possessor_Pawn, &probePawn) && probePawn >= kMinPointer;
        if (!hasController)
        {
            // Companion/profile copy (no local controller). The data writes
            // already landed via the Sync* walks, but without an effect
            // rebuild they stay inert - which reads as "edit not working" on
            // Damiane/Oongka (persisted=1 yet no stat change). Run the
            // refresh here too when the comp's body walk is alive (spawned
            // companion); the SEH below catches any fault. A pure data-only
            // profile comp (no body walk at all) is still skipped.
            uintptr_t pawnSub = 0;
            if (!(probePawn >= kMinPointer &&
                  ReadPtr(probePawn + 0x68, &pawnSub) && pawnSub >= kMinPointer))
                return;
        }

        __try
        {
            int err = 0;
            g_refresh(reinterpret_cast<void*>(comp), &err);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LOG_WARN("equipment: effect refresh faulted - the gear will apply on reload.");
        }
    }

    bool Equipment::IsItemEquippedOnAnyCharacter(uint16_t typeId)
    {
        if (typeId == 0 || typeId == kInvSlot_EmptyType) return false;

        __try
        {
            // The aggregate check also covers an unidentified controlled body.
            for (int c = -1; c < 3; ++c)
            {
                const uintptr_t owner = c < 0 ? Player::GetControlledOwner() : Player::GetOwner(c);
                const uintptr_t actor = c < 0 ? 0 : Player::GetActor(c);
                uintptr_t comp = CompForCharacter(owner, c, c);
                if (!comp && actor != owner) comp = CompForCharacter(actor, c, c);
                if (comp < kMinPointer) continue;

                const EquipTableDesc tbl = ReadEquipTableDesc(comp);
                if (!tbl.valid || tbl.count == 0 || tbl.count > 64) continue;

                for (uint32_t i = 0; i < tbl.count; ++i)
                {
                    const uintptr_t entry = tbl.array + static_cast<uintptr_t>(i) * tbl.stride;
                    uint16_t tid = 0;
                    if (Read16(entry + kOff_InvSlot_TypeId, &tid) && tid == typeId)
                        return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }

    uintptr_t Equipment::EnsureSocketVector(uintptr_t entry)
    {
        return game::EnsureSocketVector(entry);
    }

    void Equipment::OpenAllSockets(uintptr_t entry, int maxSock)
    {
        if (game::OpenAllSockets(entry, maxSock)) InvalidateSnapshot();
    }
}
