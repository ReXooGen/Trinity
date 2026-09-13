#include "equipment.h"
#include "equipment_logic.h"

#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cctype>

#include "offsets.h"
#include "player.h"
#include "inventory.h"
#include "dye.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../core/logger.h"
#include "../core/state.h"
#include "../core/version_detect.h"

// The equipment editor. All the RE background is in offsets.h (the "Abyss Gear
// sockets" section); this file is the plumbing:
//
//   Component walk  -> each realm's equip component, straight off that realm's
//                      player character (*(*(actor+0x68)+0x38)) - the same walk
//                      the dye editor uses, and self-validating via comp+0x08.
//   Socket record   -> a 6-byte entry in the pre-allocated 5-slot vector at
//                      itemVal+0x58; record[i] is socket i.
//   Edit            -> overwrite the record bytes (add/clear) or the unlocked
//                      count at itemVal+0x68 (unlock). No allocation, no engine
//                      call, so writes run inline and are mirrored into both
//                      realms - the client renders, the server persists.
//
// See equipment.h for what is durable (add/clear) and what is live-only (unlock).

namespace trinity::game
{
    namespace
    {
        using namespace trinity::mem;

        // The equipped-item effect refresh (see offsets.h): a socket edit updates
        // the record but not the derived effect structure, so we mark the state
        // dirty and the next game-thread Tick runs this on the client equip
        // component - the same full refresh the Witch's own socketing runs.
        using EquipRefresh_t = void* (__fastcall*)(void*, int*);
        EquipRefresh_t    g_refresh = nullptr; // sub_7C88A0
        std::atomic<bool> g_dirty{ false };

        struct EquipTableDesc
        {
            uintptr_t desc = 0;
            uintptr_t array = 0;
            uint32_t count = 0;
            uintptr_t stride = 0xD0;
            uintptr_t tagOffset = 0xC8;
            bool valid = false;
        };

        inline bool IsValidCanonicalPtr(uintptr_t p)
        {
            return p >= 0x100000000ULL && p <= 0x7FFFFFFFFFFFULL;
        }

        EquipTableDesc ReadEquipTableDesc(uintptr_t comp)
        {
            EquipTableDesc out{};
            if (!IsValidCanonicalPtr(comp)) return out;

            struct LayoutCandidate {
                uintptr_t stride;
                uintptr_t tagOffset;
            };
            // 208-byte stride with tag at 0xC8 is the verified engine layout for TU 2.01 / TU 2.02
            const LayoutCandidate layouts[] = {
                { 0xD0, 0xC8 }, // Primary: 208-byte stride (TU 2.01 / TU 2.02 / legacy)
                { 0xC8, 0xC0 }, // Secondary: 200-byte stride
            };

            auto evaluateSlots = [](uintptr_t arr, uint32_t cnt, uintptr_t stride, uintptr_t tagOff, int* outScore) -> bool {
                if (!IsValidCanonicalPtr(arr) || cnt == 0 || cnt > 64) return false;
                int items = 0;
                uint32_t tagMask = 0;
                for (uint32_t i = 0; i < cnt; ++i)
                {
                    const uintptr_t entry = arr + static_cast<uintptr_t>(i) * stride;
                    uint16_t tid = 0, tag = 0;
                    if (Read16(entry + kOff_InvSlot_TypeId, &tid) && tid != kInvSlot_EmptyType && tid != 0)
                    {
                        if (!Read16(entry + tagOff, &tag) || tag >= 32)
                            return false; // ANY slot tag >= 32 means this is NOT an equipment table!
                        items++;
                        tagMask |= (1u << tag);
                    }
                }
                int distinct = 0;
                for (uint32_t m = tagMask; m != 0; m &= (m - 1)) ++distinct;
                if (outScore) *outScore = distinct * 100 + items;
                return items > 0;
            };

            struct BestDesc {
                uintptr_t desc = 0;
                uintptr_t array = 0;
                uint32_t count = 0;
                uintptr_t stride = 0xD0;
                uintptr_t tagOffset = 0xC8;
                int score = 0;
            };

            BestDesc best;

            // 0x90 is the authoritative table offset on ServerEquipSlotActorComponent
            const uintptr_t tableOffsets[] = { 0x90, 0x88, 0x80, 0x50, 0x78, 0x38, 0x40, 0x48, 0x60, 0x70 };
            for (uintptr_t tOff : tableOffsets)
            {
                uintptr_t desc = 0;
                if (!ReadPtr(comp + tOff, &desc) || !IsValidCanonicalPtr(desc)) continue;
                uintptr_t array = 0;
                uint32_t count = 0;
                if (!ReadPtr(desc + kOff_EquipTable_Array, &array) || !IsValidCanonicalPtr(array)) continue;
                if (!Read32(desc + kOff_EquipTable_Count, &count) || count == 0 || count > 64) continue;

                for (const auto& l : layouts)
                {
                    int score = 0;
                    if (evaluateSlots(array, count, l.stride, l.tagOffset, &score))
                    {
                        if (score > best.score)
                        {
                            best.desc = desc;
                            best.array = array;
                            best.count = count;
                            best.stride = l.stride;
                            best.tagOffset = l.tagOffset;
                            best.score = score;
                        }
                    }
                }
            }

            if (best.score <= 0) return out;

            out.desc = best.desc;
            out.array = best.array;
            out.count = best.count;
            out.stride = best.stride;
            out.tagOffset = best.tagOffset;
            out.valid = true;
            return out;
        }

        // --- Each realm's equip component, by walk (mirrors dye.cpp) ----------
        bool CompValid(uintptr_t comp)
        {
            if (!IsValidCanonicalPtr(comp)) return false;
            const EquipTableDesc tbl = ReadEquipTableDesc(comp);
            return tbl.valid && tbl.count > 0 && tbl.count <= 64;
        }

        uintptr_t FindEquipCompFromActor(uintptr_t actor)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            // 1. Direct component at actor + 0x38 (actor is SubContainer)
            uintptr_t comp = 0;
            if (ReadPtr(actor + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                return comp;

            // 2. Standard character / mount container walk (*(*(actor+0x68)+0x38))
            uintptr_t sub = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && IsValidCanonicalPtr(sub))
            {
                if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                    return comp;
            }

            // 3. Alternate sub-container offsets on actor
            const uintptr_t subOffsets[] = { 0x60, 0x68, 0x70, 0x58, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0 };
            const uintptr_t compOffsets[] = { 0x38, 0x30, 0x40, 0x28, 0x48, 0x50, 0x58, 0x60, 0x68, 0x80, 0x88, 0x90, 0x168 };
            for (uintptr_t sOff : subOffsets)
            {
                if (ReadPtr(actor + sOff, &sub) && IsValidCanonicalPtr(sub))
                {
                    for (uintptr_t cOff : compOffsets)
                    {
                        if (ReadPtr(sub + cOff, &comp) && CompValid(comp))
                            return comp;
                    }
                }
            }

            // 4. Direct component pointer on actor
            const uintptr_t directOffsets[] = { 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0x168 };
            for (uintptr_t dOff : directOffsets)
            {
                if (ReadPtr(actor + dOff, &comp) && CompValid(comp))
                    return comp;
            }

            return 0;
        }

        uintptr_t CompForCharacter(uintptr_t actor)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            auto resolveFromActor = [](uintptr_t act) -> uintptr_t {
                if (!IsValidCanonicalPtr(act)) return 0;
                uintptr_t comp = 0;
                // Step 1: If act is already a SubContainer (e.g. from Player::GetActor), check act + 0x38 directly!
                if (ReadPtr(act + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                    return comp;
                // Step 2: Standard actor walk (*(*(act+0x68)+0x38))
                uintptr_t sub = 0;
                if (ReadPtr(act + kOff_Container_Sub, &sub) && IsValidCanonicalPtr(sub))
                {
                    if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                        return comp;
                }
                comp = FindEquipCompFromActor(act);
                if (comp && CompValid(comp))
                    return comp;
                return 0;
            };

            // 1. Direct actor check
            uintptr_t comp = resolveFromActor(actor);
            if (comp) return comp;

            // 2. If actor is an owner object, inspect inner actor (+0x68)
            uintptr_t innerAct = 0;
            if (ReadPtr(actor + kOff_Owner_Actor, &innerAct) && IsValidCanonicalPtr(innerAct) && innerAct != actor)
            {
                comp = resolveFromActor(innerAct);
                if (comp) return comp;
            }

            return 0;
        }

        uintptr_t FindTrackedCharacterComp(int targetIdx)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            for (int p = 0; p < 3; ++p)
            {
                const uintptr_t root = PreferEquipmentOwner(
                    Player::GetOwner(p), Player::GetActor(p));
                const uintptr_t comp = CompForCharacter(root);
                if (!comp) continue;
                const int id = Inventory::IdentifyCharacterFromComp(comp);
                if (AcceptCharacterComponent(targetIdx, id, p))
                    return comp;
            }
            return 0;
        }

        static int s_activeCharIdx = -1; // -1 = auto-detect active player character

        // Strict per-character routing, mirroring dye.cpp:
        //   target == live  -> the live component (hook capture first, then a
        //                      walk of the live character), so the on-screen
        //                      character edits in real time.
        //   target != live  -> an identity-verified component from
        //                      Inventory::CharacterAddr / Player::GetActor -
        //                      NEVER the live capture or the live character.
        //                      In Chapter 4 that fallback handed Kliff's slot
        //                      Damiane's live mesh and her equipment list.
        uintptr_t ClientComp()
        {
            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx;

            if (targetIdx == liveIdx)
            {
                // The validated live character's walk leads - see dye.cpp for
                // why the hook capture must not lead (a server-realm capture
                // carries the same gear and passes every identity check, but
                // has no controller and no render state).
                const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                if (liveChar)
                {
                    const uintptr_t comp = CompForCharacter(liveChar);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                // Direct check on active controlled player
                const int liveActiveIdx = Player::GetActiveCharacterIdx();
                const uintptr_t liveOwner = Player::GetOwner(liveActiveIdx >= 0 && liveActiveIdx < 3 ? liveActiveIdx : 0);
                const uintptr_t liveActor = Player::GetActor(liveActiveIdx >= 0 && liveActiveIdx < 3 ? liveActiveIdx : 0);
                if (liveOwner || liveActor)
                {
                    const uintptr_t root = PreferEquipmentOwner(liveOwner, liveActor);
                    const uintptr_t comp = CompForCharacter(root);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                if (targetIdx > 0 && targetIdx < 3)
                {
                    const uintptr_t directActor = Player::GetActor(targetIdx);
                    if (directActor)
                    {
                        const uintptr_t comp = CompForCharacter(directActor);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                    const uintptr_t directOwner = Player::GetOwner(targetIdx);
                    if (directOwner)
                    {
                        const uintptr_t comp = CompForCharacter(directOwner);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                }

                // Fallback: resolve from live inventory holder's owner
                const uintptr_t h = Inventory::ClientHolderAddr();
                if (h)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(h + 8, &owner) && owner >= kMinPointer)
                    {
                        const uintptr_t comp = CompForCharacter(owner);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             liveIdx))
                            return comp;
                    }
                }

                const uintptr_t hooked = Dye::HookedClientComp();
                if (hooked)
                {
                    uintptr_t hookedOwner = 0;
                    const bool ownerKnown =
                        ReadPtr(hooked + kOff_EquipComp_Owner, &hookedOwner);
                    if (!liveChar || (ownerKnown && hookedOwner == liveChar))
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(hooked);
                        if (id < 0 || id == targetIdx) return hooked;
                    }
                }

                if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                    return comp;
                if (const uintptr_t profComp = Player::GetProfileEquipComp(targetIdx))
                    return profComp;
                return 0; // never another character's component
            }

            // Off-screen selection: strict identity lookup only.
            const uintptr_t actor = Inventory::CharacterAddr(targetIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp)
                {
                    const int id = Inventory::IdentifyCharacterFromComp(comp);
                    if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                }
            }
            if (targetIdx >= 0 && targetIdx < 3)
            {
                const uintptr_t directActor = Player::GetActor(targetIdx);
                if (directActor)
                {
                    const uintptr_t comp = CompForCharacter(directActor);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
                const uintptr_t directOwner = Player::GetOwner(targetIdx);
                if (directOwner)
                {
                    const uintptr_t comp = CompForCharacter(directOwner);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
            }
            if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                return comp;
            if (const uintptr_t profComp = Player::GetProfileEquipComp(targetIdx))
                return profComp;
            return 0;
        }

        // Server-authority mirror with the same strict routing as dye.cpp.
        uintptr_t ServerComp()
        {
            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx;

            if (targetIdx == liveIdx)
            {
                const uintptr_t serverChar = Inventory::ServerCharacterAddr();
                if (serverChar)
                {
                    const uintptr_t comp = CompForCharacter(serverChar);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                // Direct check on active controlled player
                const int liveActiveIdx = Player::GetActiveCharacterIdx();
                const uintptr_t liveOwner = Player::GetOwner(liveActiveIdx >= 0 && liveActiveIdx < 3 ? liveActiveIdx : 0);
                const uintptr_t liveActor = Player::GetActor(liveActiveIdx >= 0 && liveActiveIdx < 3 ? liveActiveIdx : 0);
                if (liveOwner || liveActor)
                {
                    const uintptr_t root = PreferEquipmentOwner(liveOwner, liveActor);
                    const uintptr_t comp = CompForCharacter(root);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                if (targetIdx > 0 && targetIdx < 3)
                {
                    const uintptr_t directActor = Player::GetActor(targetIdx);
                    if (directActor)
                    {
                        const uintptr_t comp = CompForCharacter(directActor);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                    const uintptr_t directOwner = Player::GetOwner(targetIdx);
                    if (directOwner)
                    {
                        const uintptr_t comp = CompForCharacter(directOwner);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                }
            }

            const uintptr_t actor = Inventory::CharacterAddr(targetIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp)
                {
                    const int id = Inventory::IdentifyCharacterFromComp(comp);
                    if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                }
            }
            if (targetIdx >= 0 && targetIdx < 3)
            {
                const uintptr_t directActor = Player::GetActor(targetIdx);
                if (directActor && directActor != actor)
                {
                    const uintptr_t comp = CompForCharacter(directActor);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
                const uintptr_t directOwner = Player::GetOwner(targetIdx);
                if (directOwner && directOwner != actor)
                {
                    const uintptr_t comp = CompForCharacter(directOwner);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
            }
            if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                return comp;
            if (const uintptr_t profComp = Player::GetProfileEquipComp(targetIdx))
                return profComp;
            return 0;
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
        uintptr_t FindEntryByTag(uintptr_t comp, uint16_t tag)
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

                char itemName[96] = "";
                Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
                if (IsDummyOrUnarmed(tid, itemName)) continue;

                return entry;
            }
            return 0;
        }

        // --- Socket record access -------------------------------------------
        // The socket vector's data pointer for an item value, or 0.
        uintptr_t SocketData(uintptr_t entry)
        {
            if (entry < kMinPointer) return 0;

            uintptr_t data = 0;
            // First try TU 1.18+ offset (+0x60)
            if (ReadPtr(entry + 0x60, &data) && data >= kMinPointer)
            {
                // Verify valid pointer range
            }
            else
            {
                // Fallback to legacy (+0x58)
                data = 0;
                if (!ReadPtr(entry + 0x58, &data) || data < kMinPointer) return 0;
            }

            // Guard against module/table memory pointers (like static ItemDef tables)
            static uintptr_t s_modBase = 0, s_modEnd = 0;
            if (!s_modBase)
            {
                HMODULE hMod = GetModuleHandleA(nullptr);
                s_modBase = reinterpret_cast<uintptr_t>(hMod);
                auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
                auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
                s_modEnd = s_modBase + nt->OptionalHeader.SizeOfImage;
            }
            if (data >= s_modBase && data < s_modEnd) return 0;

            return data;
        }

        int UnlockedCount(uintptr_t entry)
        {
            if (entry < kMinPointer) return 0;
            const bool isLegacy = core::IsLegacyTU();
            const uintptr_t unlockOff = isLegacy ? 0x68 : 0x70;

            uint32_t n = 0;
            if (Read32(entry + unlockOff, &n))
            {
                if (n <= 5)
                    return static_cast<int>(n);
            }
            return 0;
        }

        uint16_t GearAt(uintptr_t data, int i)
        {
            if (!data || i < 0 || i >= kSocket_Max) return kSock_Empty;

            uint16_t g = kSock_Empty;
            if (!Read16(data + static_cast<uintptr_t>(i) * kSocketRec_Stride + kOff_SockRec_GearId, &g))
                return kSock_Empty;
            if (g == 0 || g == 0xFFFF) return kSock_Empty;
            return g;
        }

        // Raw (floorless) byte access for the TLS realm flag
        bool RawWrite8(uintptr_t addr, uint8_t val)
        {
            if (!addr) return false;
            __try { *reinterpret_cast<volatile uint8_t*>(addr) = val; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Overwrite record `i` with a filled (gear != 0xFFFF) or empty gear,
        // byte for byte the way the game's own socketing writes it.
        bool WriteRecord(uintptr_t data, int i, uint16_t gear)
        {
            if (!data || i < 0 || i >= kSocket_Max) return false;
            const uintptr_t rec = data + static_cast<uintptr_t>(i) * kSocketRec_Stride;
            const bool filled = (gear != kSock_Empty && gear != 0);
            bool ok = true;
            ok &= Write16(rec + kOff_SockRec_GearId, filled ? gear : 0xFFFF);
            ok &= Write16(rec + kOff_SockRec_Marker, filled ? 0xFFFF : 0x0000);
            ok &= Write8 (rec + kOff_SockRec_Index,  static_cast<uint8_t>(i));
            ok &= Write8 (rec + kOff_SockRec_State,  filled ? 0x05 : 0x00);
            return ok;
        }

        // Ensure an item value has an allocated 5-socket vector
        uintptr_t EnsureSocketVector(uintptr_t entry)
        {
            if (entry < kMinPointer) return 0;
            return SocketData(entry);
        }

        bool WriteSocketToEntry(uintptr_t entry, int idx, uint16_t gear)
        {
            if (entry < kMinPointer || idx < 0 || idx >= kSocket_Max) return false;
            const uintptr_t data = EnsureSocketVector(entry);
            if (!data) return false;

            for (int k = 0; k < kSocket_Max; ++k)
            {
                if (k == idx)
                {
                    WriteRecord(data, k, gear);
                }
                else
                {
                    uint16_t existing = GearAt(data, k);
                    if (existing == 0 || existing == 0xFFFF)
                        WriteRecord(data, k, kSock_Empty);
                }
            }

            const bool isLegacy = core::IsLegacyTU();
            const uintptr_t sizeOff   = isLegacy ? 0x60 : 0x68;
            const uintptr_t capOff    = isLegacy ? 0x64 : 0x6C;
            const uintptr_t unlockOff = isLegacy ? 0x68 : 0x70;

            Write32(entry + sizeOff, 5);
            Write32(entry + capOff, 5);
            Write32(entry + unlockOff, 5);
            return true;
        }

        int GetMaxSocketsForTag(uint16_t /*tag*/)
        {
            // All equipped gear pieces support up to 5 full abyss sockets
            return kSocket_Max;
        }

        // Open every socket on one realm's copy of the item up to its natural capacity
        void OpenAllSockets(uintptr_t entry, int maxSock)
        {
            if (maxSock <= 0 || entry < kMinPointer) return;
            const uintptr_t data = EnsureSocketVector(entry);
            if (!data) return;
            for (int k = 0; k < maxSock; ++k)
            {
                uint16_t g = GearAt(data, k);
                if (g == 0) g = kSock_Empty;
                WriteRecord(data, k, g);
            }

            const bool isLegacy = core::IsLegacyTU();
            const uintptr_t sizeOff   = isLegacy ? 0x60 : 0x68;
            const uintptr_t capOff    = isLegacy ? 0x64 : 0x6C;
            const uintptr_t unlockOff = isLegacy ? 0x68 : 0x70;

            Write32(entry + sizeOff, 5);
            Write32(entry + capOff, 5);
            Write32(entry + unlockOff, static_cast<uint32_t>(maxSock));
        }

        // Remove every gear from an unlocked socket on one realm's copy, leaving
        // the sockets open (index kept, just emptied).
        void EmptyAllSockets(uintptr_t entry)
        {
            if (entry < kMinPointer) return;
            const uintptr_t data = SocketData(entry);
            if (!data) return;
            const int n = UnlockedCount(entry);
            const int count = (n > 0) ? n : 5;
            for (int k = 0; k < count; ++k)
                WriteRecord(data, k, kSock_Empty);
        }

        // Safety gate for the profile auto-restore in Tick(): only let it touch
        // an entry that really is a piece of player gear from this slot. After a
        // game update the tag -> item mapping can shift, and blindly writing
        // refine/socket bytes into a quest item or container is what produced
        // the random menu crashes on newer game versions.
        bool ProfileTargetValid(uintptr_t entry, uint16_t tag)
        {
            if (entry < kMinPointer) return false;

            uint16_t tid = 0;
            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0)
                return false;

            char name[96] = "";
            Inventory::NameForTypeId(tid, name, sizeof(name));
            if (IsDummyOrUnarmed(tid, name)) return false;

            return Equipment::IsItemForSlot(tag, tid, name, name);
        }

        bool SyncSocketAllRealms(uint16_t tag, int64_t instId, int idx, uint16_t gear, uint16_t typeId = 0)
        {
            bool ok = false;

            // 1. Client Equip Comp for Active Character & all player actors
            const uintptr_t clientC = ClientComp();
            if (clientC)
            {
                const uintptr_t ce = FindEntryByTag(clientC, tag);
                if (ce) ok |= WriteSocketToEntry(ce, idx, gear);
            }

            // Direct profile component update (authoritative for in-game pause/party menu)
            const uintptr_t profC = Player::GetProfileEquipComp(Equipment::GetActiveCharacter());
            if (profC && profC != clientC)
            {
                const uintptr_t pe = FindEntryByTag(profC, tag);
                if (pe) ok |= WriteSocketToEntry(pe, idx, gear);
            }

            // Every realm copy of the SELECTED character only - never the
            // other protagonists' same-tag items.
            uintptr_t sockCopies[16] = {};
            const int sockNCopies = Inventory::CharacterAddrs(Equipment::GetActiveCharacter(), sockCopies, 16);
            for (int i = 0; i < sockNCopies; ++i)
            {
                const uintptr_t comp = CompForCharacter(sockCopies[i]);
                if (comp && comp != clientC && comp != profC)
                {
                    const uintptr_t ce = FindEntryByTag(comp, tag);
                    if (ce) ok |= WriteSocketToEntry(ce, idx, gear);
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
                                const uintptr_t comp = CompForCharacter(cand);
                                if (comp && comp != clientC && comp != profC)
                                {
                                    const uintptr_t ce = FindEntryByTag(comp, tag);
                                    if (ce) ok |= WriteSocketToEntry(ce, idx, gear);
                                }
                            }
                        }
                    }
                }
            }

            // 2. Direct bag slot sync across ALL containers and holders
            struct SockCtx { int idx; uint16_t gear; bool* ok; };
            SockCtx ctx{ idx, gear, &ok };
            Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                auto* c = reinterpret_cast<SockCtx*>(u);
                if (WriteSocketToEntry(slot, c->idx, c->gear))
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
                    if (se) ok |= WriteSocketToEntry(se, idx, gear);
                }

                if (profC && profC != serverC)
                {
                    const uintptr_t pe = FindEntryByTag(profC, tag);
                    if (pe) ok |= WriteSocketToEntry(pe, idx, gear);
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
                                    const uintptr_t comp = CompForCharacter(cand);
                                    if (comp && comp != serverC && comp != profC)
                                    {
                                        const uintptr_t se = FindEntryByTag(comp, tag);
                                        if (se) ok |= WriteSocketToEntry(se, idx, gear);
                                    }
                                }
                            }
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                    auto* c = reinterpret_cast<SockCtx*>(u);
                    WriteSocketToEntry(slot, c->idx, c->gear);
                }, &ctx, typeId);

                RawWrite8(flagAddr, oldFlag);
            }

            // Re-drive the inventory snapshot + used-slot repair (HEAD
            // behavior): makes the edit visible immediately and lets the
            // engine reconcile the touched containers.
            Inventory::ForceRefresh();

            return ok;
        }

        bool SyncRefineAllRealms(uint16_t tag, int64_t instId, uint16_t lvl, uint16_t typeId = 0)
        {
            bool ok = false;
            __try
            {
                // 1. Client Equip Comp
                const uintptr_t clientC = ClientComp();
                if (clientC)
                {
                    const uintptr_t ce = FindEntryByTag(clientC, tag);
                    if (ce) ok |= Write16(ce + kOff_ItemVal_RefineLevel, lvl);
                }

                // Direct profile component update (authoritative for in-game pause/party menu)
                const uintptr_t profC = Player::GetProfileEquipComp(Equipment::GetActiveCharacter());
                if (profC && profC != clientC)
                {
                    const uintptr_t pe = FindEntryByTag(profC, tag);
                    if (pe) ok |= Write16(pe + kOff_ItemVal_RefineLevel, lvl);
                }

                uintptr_t refCopies[16] = {};
                const int refNCopies = Inventory::CharacterAddrs(Equipment::GetActiveCharacter(), refCopies, 16);
                for (int i = 0; i < refNCopies; ++i)
                {
                    const uintptr_t comp = CompForCharacter(refCopies[i]);
                    if (comp && comp != clientC && comp != profC)
                    {
                        const uintptr_t ce = FindEntryByTag(comp, tag);
                        if (ce) ok |= Write16(ce + kOff_ItemVal_RefineLevel, lvl);
                    }
                }

                // 1b. Companion containers via PartyIndex (the copy the native
                // in-game inspect/pause UI reads). CharacterAddrs filters by
                // gear-identity and can miss these copies; the game reads THIS
                // one, so write it directly regardless of instanceId.
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
                                    const uintptr_t comp = CompForCharacter(cand);
                                    if (comp && comp != clientC && comp != profC)
                                    {
                                        const uintptr_t ce = FindEntryByTag(comp, tag);
                                        if (ce) ok |= Write16(ce + kOff_ItemVal_RefineLevel, lvl);
                                    }
                                }
                            }
                        }
                    }
                }

                // 2. Direct bag slot sync across ALL containers and holders
                struct RefCtx { uint16_t lvl; bool* ok; };
                RefCtx ctx{ lvl, &ok };
                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                    auto* c = reinterpret_cast<RefCtx*>(u);
                    Write16(slot + kOff_ItemVal_RefineLevel, c->lvl);
                    Write8(slot + kOff_ItemVal_RefineLevel, static_cast<uint8_t>(c->lvl));
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
                        if (se) ok |= Write16(se + kOff_ItemVal_RefineLevel, lvl);
                    }

                    if (profC && profC != serverC)
                    {
                        const uintptr_t pe = FindEntryByTag(profC, tag);
                        if (pe) ok |= Write16(pe + kOff_ItemVal_RefineLevel, lvl);
                    }

                    // Server-realm companion containers via PartyIndex. The
                    // client-side PartyIndex walk (1b) resolves through the
                    // CLIENT realm; the copy the native inspect UI reads is the
                    // SERVER realm's - it needs this walk with RealmFlag = 1
                    // active (the .bak server-realm CharacterAddrs loop that
                    // fix5 removed was doing exactly this, not duplicating).
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
                                        const uintptr_t comp = CompForCharacter(cand);
                                        if (comp && comp != serverC && comp != profC)
                                        {
                                            const uintptr_t se = FindEntryByTag(comp, tag);
                                            if (se) ok |= Write16(se + kOff_ItemVal_RefineLevel, lvl);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                        auto* c = reinterpret_cast<RefCtx*>(u);
                        Write16(slot + kOff_ItemVal_RefineLevel, c->lvl);
                        Write8(slot + kOff_ItemVal_RefineLevel, static_cast<uint8_t>(c->lvl));
                    }, &ctx, typeId);

                    RawWrite8(flagAddr, oldFlag);
                }

                // Re-drive snapshot + used-slot repair (HEAD behavior).
                Inventory::ForceRefresh();
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            return ok;
        }

        bool SyncUnlockAllRealms(uint16_t tag, int64_t instId, int maxSock, uint16_t typeId = 0)
        {
            bool ok = false;
            auto openOnEntry = [&](uintptr_t entry) {
                if (entry < kMinPointer) return;
                OpenAllSockets(entry, maxSock);
                ok = true;
            };

            // 1. Client Equip Comp
            const uintptr_t clientC = ClientComp();
            if (clientC) openOnEntry(FindEntryByTag(clientC, tag));

            // Direct profile component update (authoritative for in-game pause/party menu)
            const uintptr_t profC = Player::GetProfileEquipComp(Equipment::GetActiveCharacter());
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
                                const uintptr_t comp = CompForCharacter(cand);
                                if (comp && comp != clientC && comp != profC)
                                    openOnEntry(FindEntryByTag(comp, tag));
                            }
                        }
                    }
                }
            }

            // 2. Direct bag slot sync across ALL containers and holders
            struct UnlCtx { int maxSock; bool* ok; };
            UnlCtx ctx{ maxSock, &ok };
            Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                auto* c = reinterpret_cast<UnlCtx*>(u);
                OpenAllSockets(slot, c->maxSock);
                *(c->ok) = true;
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
                                    const uintptr_t comp = CompForCharacter(cand);
                                    if (comp && comp != serverC && comp != profC)
                                        openOnEntry(FindEntryByTag(comp, tag));
                                }
                            }
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                    auto* c = reinterpret_cast<UnlCtx*>(u);
                    OpenAllSockets(slot, c->maxSock);
                }, &ctx, typeId);

                RawWrite8(flagAddr, oldFlag);
            }

            // Re-drive the inventory snapshot + used-slot repair (HEAD
            // behavior): makes the edit visible immediately and lets the
            // engine reconcile the touched containers.
            Inventory::ForceRefresh();

            return ok;
        }

        bool SyncEmptyAllRealms(uint16_t tag, int64_t instId, uint16_t typeId = 0)
        {
            bool ok = false;
            auto emptyOnEntry = [&](uintptr_t entry) {
                if (entry < kMinPointer) return;
                EmptyAllSockets(entry);
                ok = true;
            };

            // 1. Client Equip Comp
            const uintptr_t clientC = ClientComp();
            if (clientC) emptyOnEntry(FindEntryByTag(clientC, tag));

            // Direct profile component update (authoritative for in-game pause/party menu)
            const uintptr_t profC = Player::GetProfileEquipComp(Equipment::GetActiveCharacter());
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
                                const uintptr_t comp = CompForCharacter(cand);
                                if (comp && comp != clientC && comp != profC)
                                    emptyOnEntry(FindEntryByTag(comp, tag));
                            }
                        }
                    }
                }
            }

            // 2. Direct bag slot sync across ALL containers and holders
            Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* u) {
                EmptyAllSockets(slot);
                if (u) *reinterpret_cast<bool*>(u) = true;
            }, &ok, typeId);

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
                                    const uintptr_t comp = CompForCharacter(cand);
                                    if (comp && comp != serverC && comp != profC)
                                        emptyOnEntry(FindEntryByTag(comp, tag));
                                }
                            }
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* /*u*/) {
                    EmptyAllSockets(slot);
                }, nullptr, typeId);

                RawWrite8(flagAddr, oldFlag);
            }

            // Re-drive the inventory snapshot + used-slot repair (HEAD
            // behavior): makes the edit visible immediately and lets the
            // engine reconcile the touched containers.
            Inventory::ForceRefresh();

            return ok;
        }


        // --- The abyss-gear catalog category (found once) --------------------
        int  g_gearCat = -2; // -2 = not looked up, -1 = none found
        void LowerCopy(const char* s, char* out, size_t n)
        {
            size_t i = 0;
            for (; s && s[i] && i + 1 < n; ++i) out[i] = static_cast<char>(tolower(static_cast<unsigned char>(s[i])));
            out[i] = 0;
        }
        int GearCategory()
        {
            if (g_gearCat != -2) return g_gearCat;
            const int n = Inventory::CatalogCategoryCount(); // builds the catalog
            for (int c = 0; c < n; ++c)
            {
                const char* name = Inventory::CatalogCategoryName(c);
                if (_stricmp(name, "Abyss Gear") == 0)
                {
                    g_gearCat = c;
                    return c;
                }
            }
            // Fallback
            for (int c = 0; c < n; ++c)
            {
                char low[96];
                LowerCopy(Inventory::CatalogCategoryName(c), low, sizeof(low));
                if (strstr(low, "abyss gear") || strstr(low, "abyss geer"))
                {
                    g_gearCat = c;
                    return c;
                }
            }
            g_gearCat = -1;
            return g_gearCat;
        }

        // --- Persistent Disk Profiles (Auto-Saved & Auto-Restored) ----------
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

        static void SaveEquipProfilesToDisk()
        {
            char iniPath[MAX_PATH];
            GetModuleFileNameA(NULL, iniPath, MAX_PATH);
            char* lastSlash = strrchr(iniPath, '\\');
            if (lastSlash) *(lastSlash + 1) = '\0';
            strcat_s(iniPath, "Trinity_EquipmentProfile.ini");

            FILE* f = nullptr;
            fopen_s(&f, iniPath, "w");
            if (!f) return;

            fprintf(f, "# Trinity Persistent Equipment Profile (Auto-Saved)\n");
            // Version stamp: profiles saved under a different game build are
            // refused on load - a patch can reshuffle the tag -> item mapping,
            // and re-applying a stale profile would then corrupt whatever now
            // lives in that slot (the "crash after update, fixed by deleting
            // the INI" report).
            fprintf(f, "# GameVersion=%s\n", core::GetGameVersionDisplay());
            fprintf(f, "# Saves Refinement, Unlocked Sockets, and Abyss Gems for Kliff, Damiane, and Oongka\n\n");

            for (int c = 0; c < 3; ++c)
            {
                for (int t = 0; t < 32; ++t)
                {
                    const auto& s = s_savedEquipSlots[c][t];
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
            fclose(f);
        }

        static void LoadEquipProfilesFromDisk()
        {
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
                LOG("equipment: loaded persistent profiles (will re-save under %s).",
                    core::GetGameVersionDisplay());
            }
            else
            {
                LOG("equipment: loaded persistent profiles from '%s'.", iniPath);
            }
        }

        static std::atomic<ULONGLONG> s_profileSavePendingMs{ 0 };

        static void SyncSlotToProfile(int charIdx, uint16_t tag)
        {
            if (charIdx < 0 || charIdx >= 3 || tag >= 32) return;
            const uintptr_t comp = ClientComp();
            uintptr_t entry = comp ? FindEntryByTag(comp, tag) : 0;
            if (!entry && charIdx > 0)
            {
                const uintptr_t profC = Player::GetProfileEquipComp(charIdx);
                if (profC) entry = FindEntryByTag(profC, tag);
            }
            if (!entry) return;

            auto& prof = s_savedEquipSlots[charIdx][tag];
            prof.active = true;
            prof.tag = tag;

            uint16_t refine = 0;
            Read16(entry + kOff_ItemVal_RefineLevel, &refine);
            prof.refineLevel = (refine > 10) ? 10 : refine;

            prof.unlockedSockets = UnlockedCount(entry);
            if (prof.unlockedSockets > 5) prof.unlockedSockets = 5;

            const uintptr_t data = SocketData(entry);
            for (int k = 0; k < kSocket_Max; ++k)
            {
                prof.socketGems[k] = data ? GearAt(data, k) : kSock_Empty;
            }

            // Debounce disk write: avoid blocking the render thread on every slider step
            s_profileSavePendingMs.store(GetTickCount64() + 600, std::memory_order_release);
        }

        // --- Menu-side snapshot ---------------------------------------------
        constexpr int          kMaxSlots = 64;
        Equipment::SlotInfo    g_slots[kMaxSlots];
        int                    g_slotCount = 0;

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
            if (strstr(name, "Destruction II")) return "Attack 2";
            if (strstr(name, "Destruction III")) return "Attack 3";
            if (strstr(name, "Greater Destruction")) return "Attack 5";
            if (strstr(name, "Colossal Might")) return "Attack +10%";
            if (strstr(name, "Aegis I") && !strstr(name, "II") && !strstr(name, "III")) return "Damage Reduction 1";
            if (strstr(name, "Aegis II")) return "Damage Reduction 2";
            if (strstr(name, "Aegis III")) return "Damage Reduction 3";
            if (strstr(name, "Fortification I") && !strstr(name, "II") && !strstr(name, "III")) return "Defense +5";
            if (strstr(name, "Fortification II")) return "Defense +10";
            if (strstr(name, "Fortification III")) return "Defense +15";
            if (strstr(name, "Insight I") && !strstr(name, "II") && !strstr(name, "III")) return "Critical Rate +2%";
            if (strstr(name, "Insight II")) return "Critical Rate +4%";
            if (strstr(name, "Insight III")) return "Critical Rate +6%";
            if (strstr(name, "Greater Insight")) return "Critical Rate +10%";
            if (strstr(name, "Swift I") && !strstr(name, "II") && !strstr(name, "III")) return "Attack Speed +4%";
            if (strstr(name, "Swift II")) return "Attack Speed +8%";
            if (strstr(name, "Swift III")) return "Attack Speed +12%";
            if (strstr(name, "Greater Swift")) return "Attack Speed +20%";
            if (strstr(name, "Vitality I") && !strstr(name, "II") && !strstr(name, "III")) return "HP Recovery +5";
            if (strstr(name, "Vitality II")) return "HP Recovery +10";
            if (strstr(name, "Vitality III")) return "HP Recovery +15";
            if (strstr(name, "Vigor I") && !strstr(name, "II") && !strstr(name, "III")) return "Stamina Recovery +5%";
            if (strstr(name, "Vigor II")) return "Stamina Recovery +10%";
            if (strstr(name, "Vigor III")) return "Stamina Recovery +15%";
            if (strstr(name, "Composure I") && !strstr(name, "II") && !strstr(name, "III")) return "Spirit Recovery +5%";
            if (strstr(name, "Composure II")) return "Spirit Recovery +10%";
            if (strstr(name, "Composure III")) return "Spirit Recovery +15%";
            if (strstr(name, "Haste I") && !strstr(name, "II") && !strstr(name, "III")) return "Move Speed +3%";
            if (strstr(name, "Haste II")) return "Move Speed +6%";
            if (strstr(name, "Haste III")) return "Move Speed +10%";
            if (strstr(name, "Abyssbane I") && !strstr(name, "II") && !strstr(name, "III")) return "Abyss Damage +5%";
            if (strstr(name, "Abyssbane II")) return "Abyss Damage +10%";
            if (strstr(name, "Abyssbane III")) return "Abyss Damage +15%";
            if (strstr(name, "Beastbane I") && !strstr(name, "II") && !strstr(name, "III")) return "Beast Damage +5%";
            if (strstr(name, "Beastbane II")) return "Beast Damage +10%";
            if (strstr(name, "Beastbane III")) return "Beast Damage +15%";
            if (strstr(name, "Bloodbane I") && !strstr(name, "II") && !strstr(name, "III")) return "Humanoid Damage +5%";
            if (strstr(name, "Bloodbane II")) return "Humanoid Damage +10%";
            if (strstr(name, "Bloodbane III")) return "Humanoid Damage +15%";
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
            if (strstr(name, "Aptitude II")) return "Skill EXP +20%";
            if (strstr(name, "Aptitude III")) return "Skill EXP +30%";
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

        void RebuildSnapshot()
        {
            g_slotCount = 0;
            const uintptr_t comp = ClientComp();
            if (!comp) return;

            const EquipTableDesc tbl = ReadEquipTableDesc(comp);
            if (!tbl.valid) return;

            for (uint32_t i = 0; i < tbl.count && g_slotCount < kMaxSlots; ++i)
            {
                const uintptr_t entry = tbl.array + static_cast<uintptr_t>(i) * tbl.stride;
                uint16_t tid = 0, tag = 0;
                int64_t  inst = 0, qty = 1;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                Read64(entry + kOff_InvSlot_Quantity, &qty);
                Read64(entry + kOff_ItemVal_InstanceId, &inst);

                char itemName[96] = "";
                if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                    snprintf(itemName, sizeof(itemName), "Item #%u", tid);

                // Exclude dummy unarmed placeholder gear (Ordinary Gloves) from Edit Equipment
                if (IsDummyOrUnarmed(tid, itemName)) continue;

                Read16(entry + tbl.tagOffset, &tag);

                char fallbackName[64] = "";
                const char* nm = SlotNameForTag(tag);
                if (!nm)
                {
                    snprintf(fallbackName, sizeof(fallbackName), "Slot #%u", tag);
                    nm = fallbackName;
                }

                Equipment::SlotInfo& s = g_slots[g_slotCount++];
                s = Equipment::SlotInfo{};
                s.tag        = tag;
                s.typeId     = tid;
                s.instanceId = inst;

                snprintf(s.slotName, sizeof(s.slotName), "%s", nm);
                snprintf(s.itemName, sizeof(s.itemName), "%s", itemName);
                Inventory::IconForTypeId(tid, s.icon, sizeof(s.icon));

                uint16_t refine = 0;
                Read16(entry + kOff_ItemVal_RefineLevel, &refine);
                s.refineLevel = (refine > kRefine_Max) ? kRefine_Max : static_cast<int>(refine);

                uint16_t dura = 0;
                if (Read16(entry + kOff_ItemVal_Durability, &dura) && dura > 0)
                    s.durability = static_cast<int>(dura);
                else
                    s.durability = 10000;

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

                const int maxSock = GetMaxSocketsForTag(tag);
                s.maxSockets = maxSock;
                s.unlockedCount = (maxSock > 0) ? UnlockedCount(entry) : 0;
                if (s.unlockedCount > maxSock) s.unlockedCount = maxSock;

                const uintptr_t data = (maxSock > 0) ? SocketData(entry) : 0;
                if (data && maxSock > 0)
                {
                    for (int k = 0; k < maxSock; ++k)
                    {
                        const uint16_t gear = GearAt(data, k);
                        if (gear != kSock_Empty && s.unlockedCount <= k)
                        {
                            s.unlockedCount = k + 1;
                        }
                    }
                }

                for (int k = 0; k < maxSock; ++k)
                {
                    Equipment::Socket& so = s.sockets[k];
                    so.unlocked = (k < s.unlockedCount);
                    so.gearTypeId = data ? GearAt(data, k) : kSock_Empty;
                    so.filled = (so.unlocked && so.gearTypeId != kSock_Empty);
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
            }
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
        g_refresh = reinterpret_cast<EquipRefresh_t>(mem::FindPattern(kSig_EquipEffectRefresh));
        if (!g_refresh)
            g_refresh = reinterpret_cast<EquipRefresh_t>(mem::FindPattern(kSig_EquipEffectRefresh_Legacy));
        if (g_refresh)
            LOG("equipment: EquipEffectRefresh resolved @ %p.", reinterpret_cast<void*>(g_refresh));
        else
            LOG_WARN("equipment: EquipEffectRefresh signature not found.");

        // 1. Load Persistent Equipment Profiles from Disk (Trinity_EquipmentProfile.ini)
        LoadEquipProfilesFromDisk();

        return true;
    }

    void Equipment::Remove()
    {
        g_gearCat = -2;
        g_refresh = nullptr;
        g_dirty.store(false, std::memory_order_release);
    }

    bool Equipment::Ready()        { return ClientComp() != 0; }
    void Equipment::ForceRefresh() { g_dirty.store(true, std::memory_order_release); }
    bool Equipment::EditsPersist() { return ServerComp() != 0; }

    void Equipment::SetActiveCharacter(int index)
    {
        s_activeCharIdx = index;
        g_slotCount = 0; // invalidate snapshot so next read rebuilds fresh
        LOG("equipment: active character switched to '%s' (index %d).", CharacterName(index), index);
    }

    int Equipment::GetActiveCharacter()
    {
        if (s_activeCharIdx < 0)
            return Inventory::ActivePlayerCharacterIdx();
        return s_activeCharIdx;
    }

    int Equipment::SlotCount()
    {
        RebuildSnapshot();
        return g_slotCount;
    }

    int Equipment::MaxSocketsForTag(uint16_t tag)
    {
        return GetMaxSocketsForTag(tag);
    }

    bool Equipment::GetSlot(int idx, SlotInfo* out)
    {
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
    bool Equipment::AddGear(uint16_t tag, int socketIdx, uint16_t gearTypeId, bool* persisted)
    {
        if (persisted) *persisted = false;
        if (socketIdx < 0 || socketIdx >= kMaxSockets || gearTypeId == kSock_Empty) return false;

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // companion copies may carry instId == 0;
        // do not refuse the edit - FindAndApplyAllHolders falls back to typeId matching.

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        const bool ok = SyncSocketAllRealms(tag, instId, socketIdx, gearTypeId, tid);
        if (persisted) *persisted = ok;

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        const char* charName = CharacterName(charIdx);
        char gearName[64] = "";
        if (!Inventory::NameForTypeId(gearTypeId, gearName, sizeof(gearName)))
            snprintf(gearName, sizeof(gearName), "0x%04X", gearTypeId);

        // Update in-memory snapshot immediately for 0ms UI response
        for (int i = 0; i < g_slotCount; ++i)
        {
            if (g_slots[i].tag == tag && socketIdx >= 0 && socketIdx < kMaxSockets)
            {
                g_slots[i].sockets[socketIdx].unlocked = true;
                g_slots[i].sockets[socketIdx].filled = true;
                g_slots[i].sockets[socketIdx].gearTypeId = gearTypeId;
                snprintf(g_slots[i].sockets[socketIdx].gearName, sizeof(g_slots[i].sockets[socketIdx].gearName), "%s", gearName);
                Inventory::IconForTypeId(gearTypeId, g_slots[i].sockets[socketIdx].gearIcon, sizeof(g_slots[i].sockets[socketIdx].gearIcon));
                const char* desc = GetGearBuffDescription(gearName);
                snprintf(g_slots[i].sockets[socketIdx].gearBuff, sizeof(g_slots[i].sockets[socketIdx].gearBuff), "%s", desc);
                int filled = 0;
                for (int s = 0; s < g_slots[i].unlockedCount; ++s)
                    if (g_slots[i].sockets[s].filled) ++filled;
                g_slots[i].filledCount = filled;
                break;
            }
        }

        // Unconditional (HEAD behavior): refresh effects for whichever
        // character was edited - the Tick-side gate decides how to run it.
        g_dirty.store(true, std::memory_order_release);

        const char* slotName = SlotNameForTag(tag);
        LOG("equipment: [%s] Slot [%s (Tag %u)] Socket %d -> Added '%s' (0x%04X) [persisted=%d].",
            charName, slotName ? slotName : "Unknown", tag, socketIdx + 1, gearName, gearTypeId, ok ? 1 : 0);

        return ok;
    }

    bool Equipment::ClearGear(uint16_t tag, int socketIdx, bool* persisted)
    {
        if (persisted) *persisted = false;
        if (socketIdx < 0 || socketIdx >= kMaxSockets) return false;

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // companion copies may carry instId == 0;
        // do not refuse the edit - FindAndApplyAllHolders falls back to typeId matching.

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        const bool ok = SyncSocketAllRealms(tag, instId, socketIdx, kSock_Empty, tid);
        if (persisted) *persisted = ok;

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        // Update in-memory snapshot immediately
        for (int i = 0; i < g_slotCount; ++i)
        {
            if (g_slots[i].tag == tag && socketIdx >= 0 && socketIdx < kMaxSockets)
            {
                g_slots[i].sockets[socketIdx].filled = false;
                g_slots[i].sockets[socketIdx].gearTypeId = 0xFFFF;
                g_slots[i].sockets[socketIdx].gearName[0] = 0;
                g_slots[i].sockets[socketIdx].gearIcon[0] = 0;
                g_slots[i].sockets[socketIdx].gearBuff[0] = 0;
                int filled = 0;
                for (int s = 0; s < g_slots[i].unlockedCount; ++s)
                    if (g_slots[i].sockets[s].filled) ++filled;
                g_slots[i].filledCount = filled;
                break;
            }
        }

        g_dirty.store(true, std::memory_order_release); // unconditional (HEAD behavior)

        const char* charName = CharacterName(charIdx);
        const char* slotName = SlotNameForTag(tag);
        LOG("equipment: [%s] Slot [%s (Tag %u)] Socket %d -> Cleared [persisted=%d].",
            charName, slotName ? slotName : "Unknown", tag, socketIdx + 1, ok ? 1 : 0);

        return ok;
    }

    bool Equipment::SetRefine(uint16_t tag, int level, bool* persisted)
    {
        if (persisted) *persisted = false;
        if (!IsRefinableTag(tag)) return false; // Strictly protect non-refinable utility gadgets (Lantern, Axiom Bracelet)

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // companion copies may carry instId == 0;
        // do not refuse the edit - FindAndApplyAllHolders falls back to typeId matching.

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false; // Strictly refuse to refine dummy unarmed gloves

        if (level < 0) level = 0;
        if (level > kRefine_Max) level = kRefine_Max;
        const uint16_t lvl = static_cast<uint16_t>(level);

        const bool ok = SyncRefineAllRealms(tag, instId, lvl, tid);
        if (persisted) *persisted = ok;

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        // Update in-memory snapshot immediately
        for (int i = 0; i < g_slotCount; ++i)
        {
            if (g_slots[i].tag == tag)
            {
                g_slots[i].refineLevel = lvl;
                const bool isWeapon = (tag == 0 || tag == 12 || tag == 13 || tag == 2);
                const bool isShield = (tag == 1);
                const bool isArmor  = (tag >= 3 && tag <= 6);
                int baseAtk = 0, baseDef = 0;
                if (isWeapon) baseAtk = 14 + lvl;
                else if (isShield) { baseAtk = 0; baseDef = 20 + lvl * 2; }
                else if (isArmor) baseDef = (tag == 4 ? 35 : 20) + lvl * 2;
                g_slots[i].reinforceBonus = (lvl >= 4) ? 2 : 1;
                if (isWeapon) g_slots[i].attack = baseAtk + g_slots[i].reinforceBonus;
                if (isArmor || isShield) g_slots[i].defense = baseDef + g_slots[i].reinforceBonus;
                break;
            }
        }

        g_dirty.store(true, std::memory_order_release); // unconditional (HEAD behavior)

        const char* charName = CharacterName(charIdx);
        const char* slotName = SlotNameForTag(tag);
        LOG("equipment: [%s] Slot [%s (Tag %u)] -> Refinement set to +%u [persisted=%d].",
            charName, slotName ? slotName : "Unknown", tag, lvl, ok ? 1 : 0);

        return ok;
    }

    bool Equipment::UnlockAll(uint16_t tag)
    {
        const int maxSock = GetMaxSocketsForTag(tag);
        if (maxSock <= 0) return false;

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // companion copies may carry instId == 0;
        // do not refuse the edit - FindAndApplyAllHolders falls back to typeId matching.

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        const bool ok = SyncUnlockAllRealms(tag, instId, maxSock, tid);

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        // Update in-memory snapshot immediately
        for (int i = 0; i < g_slotCount; ++i)
        {
            if (g_slots[i].tag == tag)
            {
                g_slots[i].unlockedCount = maxSock;
                for (int s = 0; s < maxSock; ++s)
                    g_slots[i].sockets[s].unlocked = true;
                break;
            }
        }

        g_dirty.store(true, std::memory_order_release); // unconditional (HEAD behavior)

        const char* charName = CharacterName(charIdx);
        const char* slotName = SlotNameForTag(tag);
        LOG("equipment: [%s] Slot [%s (Tag %u)] -> All %d sockets unlocked.",
            charName, slotName ? slotName : "Unknown", tag, maxSock);

        return ok;
    }

    bool Equipment::ClearAll(uint16_t tag)
    {
        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId); // companion copies may carry instId == 0;
        // do not refuse the edit - FindAndApplyAllHolders falls back to typeId matching.

        uint16_t tid = 0;
        if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) return false;
        char itemName[96] = "";
        Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
        if (IsDummyOrUnarmed(tid, itemName)) return false;

        const bool ok = SyncEmptyAllRealms(tag, instId, tid);

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        // Update in-memory snapshot immediately
        for (int i = 0; i < g_slotCount; ++i)
        {
            if (g_slots[i].tag == tag)
            {
                g_slots[i].filledCount = 0;
                for (int s = 0; s < kMaxSockets; ++s)
                {
                    g_slots[i].sockets[s].filled = false;
                    g_slots[i].sockets[s].gearTypeId = 0xFFFF;
                    g_slots[i].sockets[s].gearName[0] = 0;
                    g_slots[i].sockets[s].gearIcon[0] = 0;
                    g_slots[i].sockets[s].gearBuff[0] = 0;
                }
                break;
            }
        }

        g_dirty.store(true, std::memory_order_release); // unconditional (HEAD behavior)

        const char* charName = CharacterName(charIdx);
        const char* slotName = SlotNameForTag(tag);
        LOG("equipment: [%s] Slot [%s (Tag %u)] -> Cleared all socket gems.",
            charName, slotName ? slotName : "Unknown", tag);

        return ok;
    }

    bool Equipment::RepairAll(int* repairedCount)
    {
        if (repairedCount) *repairedCount = 0;
        int repaired = 0;

        auto repairEntry = [&](uintptr_t entry) {
            if (entry < kMinPointer) return;
            uint16_t tid = 0;
            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) return;
            char itemName[96] = "";
            Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
            if (IsDummyOrUnarmed(tid, itemName)) return;
            Write16(entry + kOff_ItemVal_Durability, 10000);
            ++repaired;
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
        if (scomp)
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
        if (const uintptr_t profC = Player::GetProfileEquipComp(GetActiveCharacter()))
        {
            const EquipTableDesc ptbl = ReadEquipTableDesc(profC);
            if (ptbl.valid)
            {
                for (uint32_t p = 0; p < ptbl.count; ++p)
                    repairEntry(ptbl.array + static_cast<uintptr_t>(p) * ptbl.stride);
            }
        }

        if (repairedCount) *repairedCount = repaired;
        g_dirty.store(true, std::memory_order_release);
        if (repaired > 0)
            LOG("equipment: repaired %d equipped items to 100%% durability (10,000).", repaired);
        return repaired > 0;
    }

    bool Equipment::RefineAll(int level, int* refinedCount)
    {
        if (refinedCount) *refinedCount = 0;
        const int total = SlotCount();
        if (total <= 0) return false;
        int count = 0;
        for (int i = 0; i < total; ++i)
        {
            SlotInfo info{};
            if (GetSlot(i, &info))
            {
                if (IsRefinableTag(info.tag))
                {
                    if (SetRefine(info.tag, level))
                        ++count;
                }
            }
        }
        if (refinedCount) *refinedCount = count;
        LOG("equipment: [%s] Refined all %d eligible equipped pieces to +%d.", CharacterName(GetActiveCharacter()), count, level);
        return count > 0;
    }

    bool Equipment::UnlockAllGears(int* unlockedCount)
    {
        if (unlockedCount) *unlockedCount = 0;
        const int total = SlotCount();
        if (total <= 0) return false;
        int count = 0;
        for (int i = 0; i < total; ++i)
        {
            SlotInfo info{};
            if (GetSlot(i, &info))
            {
                if (MaxSocketsForTag(info.tag) > 0)
                {
                    if (UnlockAll(info.tag))
                        ++count;
                }
            }
        }
        if (unlockedCount) *unlockedCount = count;
        LOG("equipment: [%s] Unlocked all sockets on %d pieces.", CharacterName(GetActiveCharacter()), count);
        return count > 0;
    }

    bool Equipment::EquipItemToSlot(uint16_t tag, uint16_t typeId, int64_t instId)
    {
        if (typeId == 0 || typeId == kInvSlot_EmptyType) return false;
        if (instId == 0)
        {
            static std::atomic<int64_t> s_nextInstId{ 0x7000000000000000LL };
            instId = ++s_nextInstId;
        }

        auto stampItem = [&](uintptr_t entry) {
            if (entry < kMinPointer) return;
            Write64(entry + kOff_ItemVal_InstanceId, instId);
            Write16(entry + kOff_InvSlot_TypeId, typeId);
            Write16(entry + kOff_ItemVal_Durability, 10000);
            Write64(entry + kOff_InvSlot_Quantity, 1);
            Write16(entry + kOff_ItemVal_RefineLevel, 10);
            EnsureSocketVector(entry);
        };

        // 1. Client Equip Component
        const uintptr_t comp = ClientComp();
        if (comp)
        {
            const uintptr_t entry = FindEntryByTag(comp, tag);
            if (entry) stampItem(entry);
        }

        // 2. Server Realm Mirror
        uint8_t oldFlag = 0;
        const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
        if (flagAddr && RawWrite8(flagAddr, 1))
        {
            const uintptr_t scomp = ServerComp();
            if (scomp)
            {
                const uintptr_t se = FindEntryByTag(scomp, tag);
                if (se) stampItem(se);
            }
            RawWrite8(flagAddr, oldFlag);
        }

        const int charIdx = GetActiveCharacter();
        if (const uintptr_t profC = Player::GetProfileEquipComp(charIdx))
        {
            const uintptr_t pe = FindEntryByTag(profC, tag);
            if (pe) stampItem(pe);
        }

        SyncSlotToProfile(charIdx, tag);

        g_dirty.store(true, std::memory_order_release); // unconditional (HEAD behavior)

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
        auto& prof = s_savedEquipSlots[charIdx][tag];
        prof.active = true;
        prof.tag = tag;
        prof.refineLevel = refineLvl > 10 ? 10 : refineLvl;
        prof.unlockedSockets = maxSock > 5 ? 5 : maxSock;
        for (int k = 0; k < kSocket_Max; ++k)
        {
            prof.socketGems[k] = gems ? gems[k] : kSock_Empty;
        }
        SaveEquipProfilesToDisk();
    }

    void Equipment::ClearPlayerEquipSlot(int charIdx, uint16_t tag)
    {
        if (charIdx < 0 || charIdx >= 3 || tag >= 32) return;
        s_savedEquipSlots[charIdx][tag].active = false;
        SaveEquipProfilesToDisk();
    }

    bool Equipment::HasCustomProfile(int charIdx, uint16_t tag)
    {
        if (charIdx < 0 || charIdx >= 3 || tag >= 32) return false;
        return s_savedEquipSlots[charIdx][tag].active;
    }

    // Game thread: if a socket was edited, re-aggregate the equipped items'
    // effects on the client component so the change takes hold now instead of
    // waiting for a reload. This is the same pair BatchEquip runs on a gear
    // change; POD locals only, guarded, because it calls into engine code.
    void Equipment::Tick()
    {
        if (!Player::Ready()) return;

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

        // Flush pending debounced profile saves to disk
        const ULONGLONG pendingSave = s_profileSavePendingMs.load(std::memory_order_acquire);
        if (pendingSave != 0 && GetTickCount64() >= pendingSave)
        {
            s_profileSavePendingMs.store(0, std::memory_order_release);
            SaveEquipProfilesToDisk();
        }

        // Auto-restore saved equipment profiles (Refine +10, 5 Unlocked Sockets, Abyss Gems)
        // across save/load, death, fast travel, and character swap.
        // SKIP while an inventory transaction is in flight (quest rewards, trades, etc.)
        // to avoid corrupting container metadata and triggering Error 298648703.
        static ULONGLONG s_lastEquipRestore = 0;
        const ULONGLONG nowEquipRestore = GetTickCount64();
        if (nowEquipRestore - s_lastEquipRestore >= 1500 && !Inventory::IsTransactionActive())
        {
            s_lastEquipRestore = nowEquipRestore;

            __try
            {
                for (int c = 0; c < 3; ++c)
                {
                    uintptr_t comp = 0;
                    const int liveIdx = Inventory::ActivePlayerCharacterIdx();
                    if (c == liveIdx)
                    {
                        // Resolve the live character directly - ActiveClientComp()
                        // is routed by the dye MENU selection, which may be a
                        // different character than `c`.
                        const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                        if (liveChar) comp = CompForCharacter(liveChar);
                        if (comp && Inventory::IdentifyCharacterFromComp(comp) != c)
                            comp = 0;
                    }
                    if (!comp)
                    {
                        const uintptr_t act = Inventory::CharacterAddr(c);
                        if (act) comp = CompForCharacter(act);
                    }
                    if (!comp && c > 0 && c < 3)
                    {
                        const uintptr_t direct = Player::GetActor(c);
                        if (direct) comp = CompForCharacter(direct);
                    }
                    if (!comp) comp = FindTrackedCharacterComp(c);
                    const uintptr_t profC = (c > 0) ? Player::GetProfileEquipComp(c) : 0;
                    if (!comp && profC) comp = profC;
                    if ((!comp || !CompValid(comp)) && (!profC || !CompValid(profC))) continue;

                    bool needRefresh = false;

                    for (uint16_t t = 0; t < 32; ++t)
                    {
                        const auto& prof = s_savedEquipSlots[c][t];
                        if (!prof.active) continue;

                        // 1. Live 3D / Client Component Update
                        if (comp && CompValid(comp))
                        {
                            const uintptr_t entry = FindEntryByTag(comp, t);
                            if (entry && ProfileTargetValid(entry, t))
                            {
                                // Check Refinement Level
                                uint16_t liveRefine = 0;
                                Read16(entry + kOff_ItemVal_RefineLevel, &liveRefine);
                                if (liveRefine < prof.refineLevel)
                                {
                                    Write16(entry + kOff_ItemVal_RefineLevel, prof.refineLevel);
                                    needRefresh = true;
                                }

                                // Check Sockets
                                const int liveUnlocked = UnlockedCount(entry);
                                if (liveUnlocked < static_cast<int>(prof.unlockedSockets) && prof.unlockedSockets > 0)
                                {
                                    OpenAllSockets(entry, static_cast<int>(prof.unlockedSockets));
                                    needRefresh = true;
                                }

                                const uintptr_t data = SocketData(entry);
                                if (data)
                                {
                                    for (int k = 0; k < static_cast<int>(prof.unlockedSockets) && k < kSocket_Max; ++k)
                                    {
                                        if (prof.socketGems[k] != kSock_Empty && prof.socketGems[k] != 0)
                                        {
                                            const uint16_t liveGem = GearAt(data, k);
                                            if (liveGem != prof.socketGems[k])
                                            {
                                                WriteRecord(data, k, prof.socketGems[k]);
                                                needRefresh = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 2. Profile component for in-game pause/party inspect UI (companions c > 0)
                        if (profC && profC != comp)
                        {
                            const uintptr_t pe = FindEntryByTag(profC, t);
                            if (pe && ProfileTargetValid(pe, t))
                            {
                                uint16_t peRefine = 0;
                                Read16(pe + kOff_ItemVal_RefineLevel, &peRefine);
                                if (peRefine < prof.refineLevel)
                                    Write16(pe + kOff_ItemVal_RefineLevel, prof.refineLevel);

                                const int peUnlocked = UnlockedCount(pe);
                                if (peUnlocked < static_cast<int>(prof.unlockedSockets) && prof.unlockedSockets > 0)
                                    OpenAllSockets(pe, static_cast<int>(prof.unlockedSockets));

                                const uintptr_t pedata = SocketData(pe);
                                if (pedata)
                                {
                                    for (int k = 0; k < static_cast<int>(prof.unlockedSockets) && k < kSocket_Max; ++k)
                                    {
                                        if (prof.socketGems[k] != kSock_Empty && prof.socketGems[k] != 0)
                                        {
                                            const uint16_t peGem = GearAt(pedata, k);
                                            if (peGem != prof.socketGems[k])
                                                WriteRecord(pedata, k, prof.socketGems[k]);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (needRefresh && c == Inventory::ActivePlayerCharacterIdx())
                    {
                        g_dirty.store(true, std::memory_order_release);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (!g_dirty.exchange(false, std::memory_order_acq_rel)) return;
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
            for (int c = 0; c < 3; ++c)
            {
                const uintptr_t actor = PreferEquipmentOwner(
                    Player::GetOwner(c), Player::GetActor(c));
                if (actor < kMinPointer) continue;

                const uintptr_t comp = FindEquipCompFromActor(actor);
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
        game::OpenAllSockets(entry, maxSock);
    }
}
