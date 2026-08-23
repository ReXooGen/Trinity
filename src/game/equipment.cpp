#include "equipment.h"

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

        using ResizeSocketVector_t = bool (__fastcall*)(void*, uint32_t);
        ResizeSocketVector_t g_resizeSocket = nullptr;

        struct EquipTableDesc
        {
            uintptr_t desc = 0;
            uintptr_t array = 0;
            uint32_t count = 0;
            uintptr_t stride = 0xD0;
            uintptr_t tagOffset = 0xC8;
            bool valid = false;
        };

        EquipTableDesc ReadEquipTableDesc(uintptr_t comp)
        {
            EquipTableDesc out{};
            if (comp < kMinPointer) return out;

            uintptr_t d = 0, a = 0;
            uint32_t c = 0;

            // Legacy TU 1.14 table (+0x88) - Priority for Legacy
            if (ReadPtr(comp + 0x88, &d) && d >= kMinPointer &&
                ReadPtr(d + kOff_EquipTable_Array, &a) && a >= kMinPointer &&
                Read32(d + kOff_EquipTable_Count, &c) && c >= 1 && c <= 64)
            {
                out.desc = d;
                out.array = a;
                out.count = c;
                out.stride = 0xC8;
                out.tagOffset = 0xC0;
                out.valid = true;
                return out;
            }

            // Modern TU 1.17+ table (+0x80)
            if (ReadPtr(comp + 0x80, &d) && d >= kMinPointer &&
                ReadPtr(d + kOff_EquipTable_Array, &a) && a >= kMinPointer &&
                Read32(d + kOff_EquipTable_Count, &c) && c >= 1 && c <= 64)
            {
                out.desc = d;
                out.array = a;
                out.count = c;
                out.stride = 0xD0;
                out.tagOffset = 0xC8;
                out.valid = true;
                return out;
            }

            // Alternate table offsets (+0x50, +0x38, +0x40, +0x48, +0x60, +0x70)
            const uintptr_t tableOffsets[] = { 0x50, 0x38, 0x40, 0x48, 0x60, 0x70 };
            for (uintptr_t tOff : tableOffsets)
            {
                if (!ReadPtr(comp + tOff, &d) || d < kMinPointer) continue;
                if (ReadPtr(d + kOff_EquipTable_Array, &a) && a >= kMinPointer &&
                    Read32(d + kOff_EquipTable_Count, &c) && c >= 1 && c <= 64)
                {
                    out.desc = d;
                    out.array = a;
                    out.count = c;
                    out.stride = 0xD0;
                    out.tagOffset = 0xC8;
                    out.valid = true;
                    return out;
                }
            }

            return out;
        }

        // --- Horse Gear Detection ------------------------------------------
        enum class HorseSlotType { None = 0, Chamfron = 1, HorseArmor = 2, Saddle = 3, Stirrups = 4, Horseshoes = 5 };

        HorseSlotType GetHorseSlotType(const char* name, const char* icon)
        {
            if (!name) return HorseSlotType::None;

            auto ContainsAny = [](const char* s, const char* const* list, size_t count) {
                if (!s) return false;
                for (size_t i = 0; i < count; ++i)
                    if (strstr(s, list[i])) return true;
                return false;
            };

            static const char* const kExcludeWords[] = {
                "Feed", "feed", "Food", "food", "Potion", "potion", "Meat", "Fruit",
                "Skill", "skill", "Recipe", "Book", "Horn", "Material", "Sugar", "sugar",
                "Hay", "hay", "Berry", "berry", "Juice", "juice", "Beet", "beet", "trade", "Trade",
                "AbyssGear", "Item_Skill", "Riding_Deer_Horn"
            };

            if (ContainsAny(name, kExcludeWords, sizeof(kExcludeWords) / sizeof(kExcludeWords[0])) ||
                (icon && ContainsAny(icon, kExcludeWords, sizeof(kExcludeWords) / sizeof(kExcludeWords[0]))))
                return HorseSlotType::None;

            // 1. Chamfron (Head / Helm)
            if (strstr(name, "_HorseArmor_Helm") || strstr(name, "_Chamfron") || strstr(name, "Chamfron") || strstr(name, "Champron") ||
                (icon && (strstr(icon, "chamfron") || strstr(icon, "horse_helm"))))
                return HorseSlotType::Chamfron;

            // 2. Horse Armor (Barding / Body Armor)
            if (strstr(name, "_HorseArmor_Armor") || strstr(name, "_Barding") || strstr(name, "HorseArmor_Armor") || strstr(name, "Barding") ||
                (icon && (strstr(icon, "horsearmor_armor") || strstr(icon, "barding"))))
                return HorseSlotType::HorseArmor;

            // 3. Saddle
            if (strstr(name, "Saddle") || strstr(name, "saddle") || strstr(name, "_HorseArmor_Saddle") ||
                (icon && strstr(icon, "saddle")))
                return HorseSlotType::Saddle;

            // 4. Stirrups
            if (strstr(name, "Stirrup") || strstr(name, "stirrup") || strstr(name, "_HorseArmor_Stirrup") ||
                (icon && strstr(icon, "stirrup")))
                return HorseSlotType::Stirrups;

            // 5. Horseshoes
            if (strstr(name, "Shoe") || strstr(name, "shoe") || strstr(name, "Horseshoe") || strstr(name, "horseshoe") || strstr(name, "_HorseArmor_Shoe") ||
                (icon && strstr(icon, "horseshoe")))
                return HorseSlotType::Horseshoes;

            return HorseSlotType::None;
        }

        bool CompHasHorseGear(uintptr_t comp)
        {
            const EquipTableDesc tbl = ReadEquipTableDesc(comp);
            if (!tbl.valid) return false;

            for (uint32_t i = 0; i < tbl.count; ++i)
            {
                const uintptr_t entry = tbl.array + static_cast<uintptr_t>(i) * tbl.stride;
                uint16_t tid = 0;
                int64_t qty = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                if (!Read64(entry + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;

                char itemName[96] = "";
                char icon[128] = "";
                if (Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                {
                    Inventory::IconForTypeId(tid, icon, sizeof(icon));
                    if (GetHorseSlotType(itemName, icon) != HorseSlotType::None)
                        return true;
                }
            }
            return false;
        }

        // --- Each realm's equip component, by walk (mirrors dye.cpp) ----------
        bool CompValid(uintptr_t comp)
        {
            if (comp < kMinPointer) return false;
            uintptr_t owner = 0;
            if (!ReadPtr(comp + kOff_EquipComp_Owner, &owner) || owner < kMinPointer) return false;
            return ReadEquipTableDesc(comp).valid;
        }

        uintptr_t FindEquipCompFromActor(uintptr_t actor)
        {
            if (actor < kMinPointer) return 0;

            uintptr_t sub = 0, comp = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && sub >= kMinPointer &&
                ReadPtr(sub + kOff_Sub_EquipComp, &comp) && CompValid(comp))
            {
                return comp;
            }

            const uintptr_t subOffsets[] = { 0x60, 0x70, 0x58, 0x78, 0x80, 0x88 };
            const uintptr_t compOffsets[] = { 0x38, 0x30, 0x40, 0x28, 0x48 };
            for (uintptr_t sOff : subOffsets)
            {
                if (ReadPtr(actor + sOff, &sub) && sub >= kMinPointer)
                {
                    for (uintptr_t cOff : compOffsets)
                    {
                        if (ReadPtr(sub + cOff, &comp) && CompValid(comp))
                            return comp;
                    }
                }
            }

            const uintptr_t directOffsets[] = { 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90 };
            for (uintptr_t dOff : directOffsets)
            {
                if (ReadPtr(actor + dOff, &comp) && CompValid(comp))
                    return comp;
            }

            return 0;
        }

        uintptr_t CompForCharacter(uintptr_t actor)
        {
            if (actor < kMinPointer) return 0;
            uintptr_t sub = 0, comp = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && sub >= kMinPointer)
            {
                if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                    return comp;
            }
            comp = FindEquipCompFromActor(actor);
            if (comp && CompValid(comp))
                return comp;
            return 0;
        }

        static int s_activeCharIdx = -1; // -1 = auto-detect active player character

        uintptr_t ClientComp()
        {
            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx;

            // If target matches currently controlled live character:
            if (targetIdx == liveIdx)
            {
                const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                if (liveChar)
                {
                    const uintptr_t comp = CompForCharacter(liveChar);
                    if (comp && !CompHasHorseGear(comp)) return comp;
                }
                const uintptr_t active = Dye::ActiveClientComp();
                if (active && CompValid(active) && !CompHasHorseGear(active)) return active;
            }

            // Otherwise resolve target companion or Kliff via Inventory::CharacterAddr:
            const uintptr_t actor = Inventory::CharacterAddr(targetIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp && !CompHasHorseGear(comp)) return comp;
            }

            return 0; // If not found in party/world, return 0 (no equipment).
        }

        uintptr_t ServerComp()
        {
            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx;

            if (targetIdx == 0 && liveIdx == 0)
            {
                const uintptr_t serverChar = Inventory::ServerCharacterAddr();
                if (serverChar)
                {
                    const uintptr_t comp = CompForCharacter(serverChar);
                    if (comp && !CompHasHorseGear(comp)) return comp;
                }
            }

            const uintptr_t actor = Inventory::CharacterAddr(targetIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp && !CompHasHorseGear(comp)) return comp;
            }

            return 0;
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
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType) return 0;
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
            uintptr_t data = SocketData(entry);
            if (data) return data;

            if (g_resizeSocket)
            {
                __try { g_resizeSocket(reinterpret_cast<void*>(entry), 5); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                data = SocketData(entry);
                if (data) return data;
            }

            return 0;
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

        bool SyncSocketAllRealms(uint16_t tag, int64_t instId, int idx, uint16_t gear)
        {
            bool ok = false;

            // 1. Client Equip Comp for Active Character & all player actors
            const uintptr_t clientC = ClientComp();
            if (clientC)
            {
                const uintptr_t ce = FindEntryByTag(clientC, tag);
                if (ce) ok |= WriteSocketToEntry(ce, idx, gear);
            }

            for (int c = 0; c < 3; ++c)
            {
                const uintptr_t act = Inventory::CharacterAddr(c);
                if (act)
                {
                    const uintptr_t comp = CompForCharacter(act);
                    if (comp && comp != clientC)
                    {
                        const uintptr_t ce = FindEntryByTag(comp, tag);
                        if (ce) ok |= WriteSocketToEntry(ce, idx, gear);
                    }
                }
            }

            // 2. Client Inventory Holders & All Companion Containers
            struct SockArg { int idx; uint16_t gear; bool* pOk; };
            SockArg sArg{ idx, gear, &ok };
            auto sockCb = [](uintptr_t slotEntry, void* ud) {
                auto* a = static_cast<SockArg*>(ud);
                if (WriteSocketToEntry(slotEntry, a->idx, a->gear))
                    *a->pOk = true;
            };
            Inventory::FindAndApplyAllHolders(instId, sockCb, &sArg);

            // 3. Server Realm (Equip Comp + All Companion Server Holders) with RealmFlag = 1
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

                for (int c = 0; c < 3; ++c)
                {
                    const uintptr_t act = Inventory::CharacterAddr(c);
                    if (act)
                    {
                        const uintptr_t comp = CompForCharacter(act);
                        if (comp && comp != serverC)
                        {
                            const uintptr_t se = FindEntryByTag(comp, tag);
                            if (se) ok |= WriteSocketToEntry(se, idx, gear);
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, sockCb, &sArg);
                RawWrite8(flagAddr, oldFlag);
            }

            Inventory::ForceRefresh();
            return ok;
        }

        bool SyncRefineAllRealms(uint16_t tag, int64_t instId, uint16_t lvl)
        {
            bool ok = false;

            // 1. Client Equip Comp
            const uintptr_t clientC = ClientComp();
            if (clientC)
            {
                const uintptr_t ce = FindEntryByTag(clientC, tag);
                if (ce) ok |= Write16(ce + kOff_ItemVal_RefineLevel, lvl);
            }

            for (int c = 0; c < 3; ++c)
            {
                const uintptr_t act = Inventory::CharacterAddr(c);
                if (act)
                {
                    const uintptr_t comp = CompForCharacter(act);
                    if (comp && comp != clientC)
                    {
                        const uintptr_t ce = FindEntryByTag(comp, tag);
                        if (ce) ok |= Write16(ce + kOff_ItemVal_RefineLevel, lvl);
                    }
                }
            }

            // 2. Client Inventory Holders & All Companion Containers
            struct RefineArg { uint16_t level; bool* pOk; };
            RefineArg rArg{ lvl, &ok };
            auto refineCb = [](uintptr_t slotEntry, void* ud) {
                auto* a = static_cast<RefineArg*>(ud);
                if (Write16(slotEntry + kOff_ItemVal_RefineLevel, a->level))
                    *a->pOk = true;
            };
            Inventory::FindAndApplyAllHolders(instId, refineCb, &rArg);

            // 3. Server Realm (Equip Comp + All Companion Server Holders) with RealmFlag = 1
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

                for (int c = 0; c < 3; ++c)
                {
                    const uintptr_t act = Inventory::CharacterAddr(c);
                    if (act)
                    {
                        const uintptr_t comp = CompForCharacter(act);
                        if (comp && comp != serverC)
                        {
                            const uintptr_t se = FindEntryByTag(comp, tag);
                            if (se) ok |= Write16(se + kOff_ItemVal_RefineLevel, lvl);
                        }
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, refineCb, &rArg);
                RawWrite8(flagAddr, oldFlag);
            }

            Inventory::ForceRefresh();
            return ok;
        }

        bool SyncUnlockAllRealms(uint16_t tag, int64_t instId, int maxSock)
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

            for (int c = 0; c < 3; ++c)
            {
                const uintptr_t act = Inventory::CharacterAddr(c);
                if (act)
                {
                    const uintptr_t comp = CompForCharacter(act);
                    if (comp && comp != clientC)
                        openOnEntry(FindEntryByTag(comp, tag));
                }
            }

            // 2. Client Inventory Holders & All Companion Containers
            struct UnlockArg { int maxS; bool* pOk; };
            UnlockArg uArg{ maxSock, &ok };
            auto unlockCb = [](uintptr_t slotEntry, void* ud) {
                auto* a = static_cast<UnlockArg*>(ud);
                OpenAllSockets(slotEntry, a->maxS);
                *a->pOk = true;
            };
            Inventory::FindAndApplyAllHolders(instId, unlockCb, &uArg);

            // 3. Server Realm
            uint8_t oldFlag = 0;
            const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
            if (flagAddr && RawWrite8(flagAddr, 1))
            {
                const uintptr_t serverC = ServerComp();
                if (serverC) openOnEntry(FindEntryByTag(serverC, tag));

                for (int c = 0; c < 3; ++c)
                {
                    const uintptr_t act = Inventory::CharacterAddr(c);
                    if (act)
                    {
                        const uintptr_t comp = CompForCharacter(act);
                        if (comp && comp != serverC)
                            openOnEntry(FindEntryByTag(comp, tag));
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, unlockCb, &uArg);
                RawWrite8(flagAddr, oldFlag);
            }

            Inventory::ForceRefresh();
            return ok;
        }

        bool SyncEmptyAllRealms(uint16_t tag, int64_t instId)
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

            for (int c = 0; c < 3; ++c)
            {
                const uintptr_t act = Inventory::CharacterAddr(c);
                if (act)
                {
                    const uintptr_t comp = CompForCharacter(act);
                    if (comp && comp != clientC)
                        emptyOnEntry(FindEntryByTag(comp, tag));
                }
            }

            // 2. Client Inventory Holders & All Companion Containers
            auto emptyCb = [](uintptr_t slotEntry, void* ud) {
                EmptyAllSockets(slotEntry);
                *static_cast<bool*>(ud) = true;
            };
            Inventory::FindAndApplyAllHolders(instId, emptyCb, &ok);

            // 3. Server Realm
            uint8_t oldFlag = 0;
            const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
            if (flagAddr && RawWrite8(flagAddr, 1))
            {
                const uintptr_t serverC = ServerComp();
                if (serverC) emptyOnEntry(FindEntryByTag(serverC, tag));

                for (int c = 0; c < 3; ++c)
                {
                    const uintptr_t act = Inventory::CharacterAddr(c);
                    if (act)
                    {
                        const uintptr_t comp = CompForCharacter(act);
                        if (comp && comp != serverC)
                            emptyOnEntry(FindEntryByTag(comp, tag));
                    }
                }

                Inventory::FindAndApplyAllHolders(instId, emptyCb, &ok);
                RawWrite8(flagAddr, oldFlag);
            }

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
            int abyssAny = -1;
            for (int c = 0; c < n; ++c)
            {
                char low[96];
                LowerCopy(Inventory::CatalogCategoryName(c), low, sizeof(low));
                if (!strstr(low, "abyss") && !strstr(low, "artifact") && !strstr(low, "geer")) continue;
                if (strstr(low, "gear") || strstr(low, "geer")) { g_gearCat = c; return c; } // prefer "Abyss Gear" / "Abyss Geer"
                if (abyssAny < 0) abyssAny = c;                       // else any "Abyss ..."
            }
            g_gearCat = abyssAny;
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

            while (fgets(line, sizeof(line), f))
            {
                char* p = line + strlen(line);
                while (p > line && (*(p - 1) == '\r' || *(p - 1) == '\n' || *(p - 1) == ' ')) { *(--p) = '\0'; }

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
            LOG("equipment: loaded persistent profiles from '%s'.", iniPath);
        }

        static void SyncSlotToProfile(int charIdx, uint16_t tag)
        {
            if (charIdx < 0 || charIdx >= 3 || tag >= 32) return;
            const uintptr_t comp = ClientComp();
            if (!comp) return;
            const uintptr_t entry = FindEntryByTag(comp, tag);
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

            SaveEquipProfilesToDisk();
        }

        // --- Menu-side snapshot ---------------------------------------------
        constexpr int          kMaxSlots = 64;
        Equipment::SlotInfo    g_slots[kMaxSlots];
        int                    g_slotCount = 0;

        const char* SlotNameForTag(uint16_t tag)
        {
            return Equipment::SlotNameForTag(tag);
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
                int64_t  inst = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                Read16(entry + tbl.tagOffset, &tag);

                char fallbackName[64] = "";
                const char* nm = SlotNameForTag(tag);
                if (!nm)
                {
                    snprintf(fallbackName, sizeof(fallbackName), "Slot #%u", tag);
                    nm = fallbackName;
                }
                Read64(entry + kOff_ItemVal_InstanceId, &inst);

                Equipment::SlotInfo& s = g_slots[g_slotCount++];
                s = Equipment::SlotInfo{};
                s.tag        = tag;
                s.typeId     = tid;
                s.instanceId = inst;

                snprintf(s.slotName, sizeof(s.slotName), "%s", nm);

                if (!Inventory::NameForTypeId(tid, s.itemName, sizeof(s.itemName)))
                    snprintf(s.itemName, sizeof(s.itemName), "Item #%u", tid);
                Inventory::IconForTypeId(tid, s.icon, sizeof(s.icon));

                uint16_t refine = 0;
                Read16(entry + kOff_ItemVal_RefineLevel, &refine);
                s.refineLevel = (refine > kRefine_Max) ? kRefine_Max : static_cast<int>(refine);

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

        // Damiane (1)
        if (charIdx == 1)
        {
            if (typeId == 53935 || typeId == 6324 || typeId == 6041 || typeId == 5306 || typeId == 5300 ||
                typeId == 5297 || typeId == 5277 || typeId == 3463 || (typeId >= 5450 && typeId <= 5468) ||
                (typeId >= 5270 && typeId <= 5310) || (typeId >= 6320 && typeId <= 6330))
                return true;
            if (name && (strstr(name, "Rapier") || strstr(name, "rapier") || strstr(name, "Damian") ||
                         strstr(name, "Demian") || strstr(name, "Spencer") || strstr(name, "Dewhaven") ||
                         strstr(name, "White Wind") || strstr(name, "Hwando") || strstr(name, "hwando") ||
                         strstr(name, "Demeniss") || strstr(name, "Fencing") || strstr(name, "Dual Blade")))
                return true;
            if (key && (strstr(key, "Rapier") || strstr(key, "rapier") || strstr(key, "Damian") ||
                        strstr(key, "Demian") || strstr(key, "Spencer") || strstr(key, "Dewhaven") ||
                        strstr(key, "White Wind") || strstr(key, "Hwando") || strstr(key, "hwando") ||
                        strstr(key, "Demeniss") || strstr(key, "Fencing")))
                return true;
            return false;
        }

        // Oongka (2)
        if (charIdx == 2)
        {
            if (typeId == 6560 || typeId == 6042 || typeId == 6305 || (typeId >= 6550 && typeId <= 6570))
                return true;
            if (name && (strstr(name, "Oongka") || strstr(name, "Giant") || strstr(name, "Tynion") ||
                         strstr(name, "Rocket") || strstr(name, "Cannon") || strstr(name, "Club") ||
                         strstr(name, "Hammer") || strstr(name, "Heavy Mace")))
                return true;
            if (key && (strstr(key, "Oongka") || strstr(key, "Giant") || strstr(key, "Tynion") ||
                        strstr(key, "Rocket") || strstr(key, "Cannon") || strstr(key, "Club") ||
                        strstr(key, "Hammer")))
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

        g_resizeSocket = reinterpret_cast<ResizeSocketVector_t>(mem::FindPattern("48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00"));
        if (g_resizeSocket)
            LOG("equipment: native ResizeSocketVector resolved @ %p.", reinterpret_cast<void*>(g_resizeSocket));

        // 1. Load Persistent Equipment Profiles from Disk (Trinity_EquipmentProfile.ini)
        LoadEquipProfilesFromDisk();

        return true;
    }

    void Equipment::Remove()
    {
        g_gearCat = -2;
        g_refresh = nullptr;
        g_resizeSocket = nullptr;
        g_dirty.store(false, std::memory_order_release);
    }

    bool Equipment::Ready()        { return ClientComp() != 0; }
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
        Read64(entry + kOff_ItemVal_InstanceId, &instId);

        const bool ok = SyncSocketAllRealms(tag, instId, socketIdx, gearTypeId);
        if (persisted) *persisted = ok;

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        g_dirty.store(true, std::memory_order_release); // re-apply effects on the next Game-Thread Tick

        const char* charName = CharacterName(charIdx);
        char gearName[64] = "";
        if (!Inventory::NameForTypeId(gearTypeId, gearName, sizeof(gearName)))
            snprintf(gearName, sizeof(gearName), "0x%04X", gearTypeId);
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
        Read64(entry + kOff_ItemVal_InstanceId, &instId);

        const bool ok = SyncSocketAllRealms(tag, instId, socketIdx, kSock_Empty);
        if (persisted) *persisted = ok;

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        g_dirty.store(true, std::memory_order_release);

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
        if (level < 0) level = 0;
        if (level > kRefine_Max) level = kRefine_Max;
        const uint16_t lvl = static_cast<uint16_t>(level);

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        Read64(entry + kOff_ItemVal_InstanceId, &instId);

        const bool ok = SyncRefineAllRealms(tag, instId, lvl);
        if (persisted) *persisted = ok;

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        g_dirty.store(true, std::memory_order_release); // re-apply effects on the next Tick

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
        Read64(entry + kOff_ItemVal_InstanceId, &instId);

        const bool ok = SyncUnlockAllRealms(tag, instId, maxSock);

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        g_dirty.store(true, std::memory_order_release);

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
        if (!Read64(entry + kOff_ItemVal_InstanceId, &instId)) return false;

        const bool ok = SyncEmptyAllRealms(tag, instId);

        const int charIdx = GetActiveCharacter();
        SyncSlotToProfile(charIdx, tag);

        g_dirty.store(true, std::memory_order_release);

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
        SyncSlotToProfile(charIdx, tag);

        g_dirty.store(true, std::memory_order_release);

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
            if (now - s_lastRepair >= 500)
            {
                RepairAll();
                s_lastRepair = now;
            }
        }

        // Auto-restore saved equipment profiles (Refine +10, 5 Unlocked Sockets, Abyss Gems)
        // across save/load, death, fast travel, and character swap.
        // SKIP while an inventory transaction is in flight (quest rewards, trades, etc.)
        // to avoid corrupting container metadata and triggering Error 298648703.
        static ULONGLONG s_lastEquipRestore = 0;
        const ULONGLONG nowEquipRestore = GetTickCount64();
        if (nowEquipRestore - s_lastEquipRestore >= 500 && !Inventory::IsTransactionActive())
        {
            s_lastEquipRestore = nowEquipRestore;

            for (int c = 0; c < 3; ++c)
            {
                uintptr_t comp = 0;
                const int liveIdx = Inventory::ActivePlayerCharacterIdx();
                if (c == liveIdx)
                {
                    comp = Dye::ActiveClientComp();
                    if (!comp)
                    {
                        const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                        if (liveChar) comp = CompForCharacter(liveChar);
                    }
                }
                if (!comp)
                {
                    const uintptr_t act = Inventory::CharacterAddr(c);
                    if (act) comp = CompForCharacter(act);
                    if (!comp)
                    {
                        const uintptr_t directAct = Player::GetActor(c);
                        if (directAct) comp = CompForCharacter(directAct);
                    }
                }

                if (!comp || !CompValid(comp)) continue;

                bool needRefresh = false;

                for (uint16_t t = 0; t < 32; ++t)
                {
                    const auto& prof = s_savedEquipSlots[c][t];
                    if (!prof.active) continue;

                    const uintptr_t entry = FindEntryByTag(comp, t);
                    if (!entry) continue;

                    // 1. Check Refinement Level
                    uint16_t liveRefine = 0;
                    Read16(entry + kOff_ItemVal_RefineLevel, &liveRefine);
                    if (liveRefine < prof.refineLevel)
                    {
                        Write16(entry + kOff_ItemVal_RefineLevel, prof.refineLevel);
                        needRefresh = true;
                    }

                    // 2. Check Sockets
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

                if (needRefresh)
                {
                    g_dirty.store(true, std::memory_order_release);
                }
            }
        }

        if (!g_dirty.exchange(false, std::memory_order_acq_rel)) return;
        if (!g_refresh) return;

        const uintptr_t comp = ClientComp();
        if (!comp)
        {
            g_dirty.store(true, std::memory_order_release); // not ready - retry next frame
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
}
