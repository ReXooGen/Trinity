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
            // Legacy TU 1.14 table (+0x88)
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
            return out;
        }

        // --- Each realm's equip component, by walk (mirrors dye.cpp) ----------
        bool CompValid(uintptr_t comp)
        {
            if (comp < kMinPointer) return false;
            uintptr_t owner = 0;
            if (!ReadPtr(comp + kOff_EquipComp_Owner, &owner) || owner < kMinPointer) return false;
            return ReadEquipTableDesc(comp).valid;
        }

        uintptr_t CompForCharacter(uintptr_t actor)
        {
            if (actor < kMinPointer) return 0;
            uintptr_t sub = 0, comp = 0, owner = 0;
            if (!ReadPtr(actor + kOff_Container_Sub, &sub) || sub < kMinPointer) return 0;
            if (!ReadPtr(sub + kOff_Sub_EquipComp, &comp) || comp < kMinPointer) return 0;
            if (!ReadPtr(comp + kOff_EquipComp_Owner, &owner) || owner != actor) return 0;
            return CompValid(comp) ? comp : 0;
        }

        static int s_activeCharIdx = 0;

        uintptr_t ClientComp()
        {
            const uintptr_t actor = Inventory::CharacterAddr(s_activeCharIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp) return comp;
            }
            if (s_activeCharIdx == 0)
            {
                const uintptr_t active = Dye::ActiveClientComp();
                if (active) return active;
            }
            return 0;
        }

        uintptr_t ServerComp()
        {
            if (s_activeCharIdx == 0)
                return CompForCharacter(Inventory::ServerCharacterAddr());

            const uintptr_t actor = Inventory::CharacterAddr(s_activeCharIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp) return comp;
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
            const bool isLegacy = core::IsLegacyTU();
            const uintptr_t dataOff = isLegacy ? 0x58 : 0x60;

            uintptr_t data = 0;
            if (!ReadPtr(entry + dataOff, &data) || data < kMinPointer) return 0;

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
            uint8_t state = 0;
            if (!Read8(data + static_cast<uintptr_t>(i) * kSocketRec_Stride + kOff_SockRec_State, &state))
                return kSock_Empty;
            if (state != 0x05) return kSock_Empty;

            uint16_t g = kSock_Empty;
            if (!Read16(data + static_cast<uintptr_t>(i) * kSocketRec_Stride + kOff_SockRec_GearId, &g))
                return kSock_Empty;
            if (g == 0 || g == 0xFFFF) return kSock_Empty;
            return g;
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

        // Write a socket record into one realm's copy of the item, verifying it
        // is the same physical item first (same instance id) so a mid-gear-change
        // drift can never edit the wrong piece.
        bool WriteRealm(uintptr_t comp, uint16_t tag, int idx, uint16_t gear, int64_t instId)
        {
            if (!comp) return false;
            const uintptr_t entry = FindEntryByTag(comp, tag);
            if (!entry) return false;
            int64_t id = 0;
            if (!Read64(entry + kOff_ItemVal_InstanceId, &id) || id != instId) return false;
            const uintptr_t data = SocketData(entry);
            if (!data) return false;

            bool ok = WriteRecord(data, idx, gear);
            const bool isLegacy = core::IsLegacyTU();
            const uintptr_t unlockOff = isLegacy ? 0x68 : 0x70;
            const uint32_t needed = static_cast<uint32_t>(idx + 1);
            uint32_t cur = 0;
            if (Read32(entry + unlockOff, &cur) && needed > cur && needed <= 5)
                Write32(entry + unlockOff, needed);
            return ok;
        }

        int GetMaxSocketsForTag(uint16_t tag)
        {
            switch (tag)
            {
            case 0:  return 3; // Main Hand (Wolf's Fang, Parashu Axe, swords, maces, axes)
            case 13: return 3; // Two-Handed Weapon (Greatswords, Halberds, Greataxes)
            case 4:  return 3; // Chest (Plate Armor, Robes, Tunics)
            case 1:  return 2; // Off-Hand (Shields)
            case 5:  return 2; // Gloves (Plate Gloves, Bracers)
            case 6:  return 2; // Boots (Plate Boots, Greaves)
            case 3:  return 1; // Helmet (Plate Helm, Hats)
            default: return 0; // Bows, Rings, Necklaces, Earrings, Dagger, Cloak, Lantern, Bracelet, etc.
            }
        }

        // Open every socket on one realm's copy of the item up to its natural capacity
        void OpenAllSockets(uintptr_t entry, int maxSock)
        {
            if (maxSock <= 0) return;
            const uintptr_t data = SocketData(entry);
            if (!data) return;
            for (int k = 0; k < maxSock; ++k)
            {
                uint16_t g = GearAt(data, k);
                if (g == 0) g = kSock_Empty;
                WriteRecord(data, k, g);
            }
            const bool isLegacy = core::IsLegacyTU();
            const uintptr_t unlockOff = isLegacy ? 0x68 : 0x70;
            Write32(entry + unlockOff, static_cast<uint32_t>(maxSock));
        }

        // Remove every gear from an unlocked socket on one realm's copy, leaving
        // the sockets open (index kept, just emptied).
        void EmptyAllSockets(uintptr_t entry)
        {
            const uintptr_t data = SocketData(entry);
            if (!data) return;
            const int n = UnlockedCount(entry);
            for (int k = 0; k < n; ++k)
                WriteRecord(data, k, kSock_Empty); // empty, idx = k (stays unlocked)
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

        // --- Menu-side snapshot ---------------------------------------------
        constexpr int          kMaxSlots = 64;
        Equipment::SlotInfo    g_slots[kMaxSlots];
        int                    g_slotCount = 0;

        const char* SlotNameForTag(uint16_t tag)
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
            case 15: return "Lantern";
            case 16: return "Cloak";
            case 17: return "Glasses";
            case 18: return "Mask";
            case 19: return "Backpack";
            case 20: return "Bracelet";
            case 21: return "Rocket";
            default: return nullptr;
            }
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
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0 || tid > 50000) continue;
                if (!Read16(entry + tbl.tagOffset, &tag)) continue;
                const char* nm = SlotNameForTag(tag);
                if (!nm) continue;
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

    bool Equipment::Install()
    {
        // No hooks: the walk resolves the component from the player character and
        // every edit is a guarded memory write. We only resolve the two effect
        // re-aggregators so a socket edit can take hold live (see Tick). If they
        // do not resolve, editing still works - the effect just waits for a
        // reload, exactly as it did before.
        g_refresh = reinterpret_cast<EquipRefresh_t>(mem::FindPattern(kSig_EquipEffectRefresh));
        return true;
    }

    void Equipment::Remove()
    {
        g_gearCat = -2;
        g_refresh = nullptr;
        g_dirty.store(false, std::memory_order_release);
    }

    bool Equipment::Ready()        { return ClientComp() != 0; }
    bool Equipment::EditsPersist() { return ServerComp() != 0; }

    void Equipment::SetActiveCharacter(int index)
    {
        if (index < 0) index = 0;
        s_activeCharIdx = index;
        g_slotCount = 0; // invalidate snapshot so next read rebuilds fresh
    }

    int Equipment::GetActiveCharacter()
    {
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
        if (!Read64(entry + kOff_ItemVal_InstanceId, &instId)) return false;
        const uintptr_t data = SocketData(entry);
        if (!data) return false;

        if (!WriteRecord(data, socketIdx, gearTypeId)) return false;

        const bool isLegacy = core::IsLegacyTU();
        const uintptr_t unlockOff = isLegacy ? 0x68 : 0x70;
        const uint32_t needed = static_cast<uint32_t>(socketIdx + 1);
        uint32_t cur = 0;
        if (Read32(entry + unlockOff, &cur) && needed > cur && needed <= 5)
            Write32(entry + unlockOff, needed);

        const bool durable = WriteRealm(ServerComp(), tag, socketIdx, gearTypeId, instId);
        if (persisted) *persisted = durable;
        g_dirty.store(true, std::memory_order_release); // re-apply effects on the next Tick
        return true;
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
        if (!Read64(entry + kOff_ItemVal_InstanceId, &instId)) return false;
        const uintptr_t data = SocketData(entry);
        if (!data) return false;

        if (!WriteRecord(data, socketIdx, kSock_Empty)) return false;

        const bool durable = WriteRealm(ServerComp(), tag, socketIdx, kSock_Empty, instId);
        if (persisted) *persisted = durable;
        g_dirty.store(true, std::memory_order_release);
        return true;
    }

    bool Equipment::SetRefine(uint16_t tag, int level, bool* persisted)
    {
        if (persisted) *persisted = false;
        if (level < 0) level = 0;
        if (level > kRefine_Max) level = kRefine_Max;
        const uint16_t lvl = static_cast<uint16_t>(level);

        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        if (!Read64(entry + kOff_ItemVal_InstanceId, &instId)) return false;

        if (!Write16(entry + kOff_ItemVal_RefineLevel, lvl)) return false;

        // Mirror into the server realm's copy of the same physical item so the
        // level survives the reconcile and the save - the dual-realm recipe the
        // gear writes use, instance-guarded against a mid-change drift.
        bool durable = false;
        if (const uintptr_t scomp = ServerComp())
        {
            const uintptr_t se = FindEntryByTag(scomp, tag);
            int64_t sid = 0;
            if (se && Read64(se + kOff_ItemVal_InstanceId, &sid) && sid == instId)
                durable = Write16(se + kOff_ItemVal_RefineLevel, lvl);
        }
        if (persisted) *persisted = durable;
        g_dirty.store(true, std::memory_order_release); // re-apply effects on the next Tick
        return true;
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
        if (!Read64(entry + kOff_ItemVal_InstanceId, &instId)) return false;

        OpenAllSockets(entry, maxSock);

        const uintptr_t scomp = ServerComp();
        if (scomp)
        {
            const uintptr_t se = FindEntryByTag(scomp, tag);
            int64_t sid = 0;
            if (se && Read64(se + kOff_ItemVal_InstanceId, &sid) && sid == instId)
                OpenAllSockets(se, maxSock);
        }
        g_dirty.store(true, std::memory_order_release);
        return true;
    }

    bool Equipment::ClearAll(uint16_t tag)
    {
        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        if (!Read64(entry + kOff_ItemVal_InstanceId, &instId)) return false;

        EmptyAllSockets(entry);

        const uintptr_t scomp = ServerComp();
        if (scomp)
        {
            const uintptr_t se = FindEntryByTag(scomp, tag);
            int64_t sid = 0;
            if (se && Read64(se + kOff_ItemVal_InstanceId, &sid) && sid == instId)
                EmptyAllSockets(se);
        }
        g_dirty.store(true, std::memory_order_release);
        return true;
    }

    bool Equipment::RepairAll(int* repairedCount)
    {
        if (repairedCount) *repairedCount = 0;
        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const EquipTableDesc tbl = ReadEquipTableDesc(comp);
        if (!tbl.valid) return false;

        const uintptr_t scomp = ServerComp();
        const EquipTableDesc stbl = scomp ? ReadEquipTableDesc(scomp) : EquipTableDesc{};

        int repaired = 0;
        for (uint32_t i = 0; i < tbl.count; ++i)
        {
            const uintptr_t entry = tbl.array + static_cast<uintptr_t>(i) * tbl.stride;
            uint16_t tid = 0;
            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
            int64_t instId = 0;
            Read64(entry + kOff_ItemVal_InstanceId, &instId);

            // Restore max durability (10000)
            Write16(entry + kOff_ItemVal_Durability, 10000);

            if (stbl.valid)
            {
                for (uint32_t s = 0; s < stbl.count; ++s)
                {
                    const uintptr_t se = stbl.array + static_cast<uintptr_t>(s) * stbl.stride;
                    int64_t sid = 0;
                    if (Read64(se + kOff_ItemVal_InstanceId, &sid) && sid == instId)
                    {
                        Write16(se + kOff_ItemVal_Durability, 10000);
                        break;
                    }
                }
            }
            ++repaired;
        }
        if (repairedCount) *repairedCount = repaired;
        g_dirty.store(true, std::memory_order_release);
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
                SetRefine(info.tag, level);
                ++count;
            }
        }
        if (refinedCount) *refinedCount = count;
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
        return count > 0;
    }

    // Game thread: if a socket was edited, re-aggregate the equipped items'
    // effects on the client component so the change takes hold now instead of
    // waiting for a reload. This is the same pair BatchEquip runs on a gear
    // change; POD locals only, guarded, because it calls into engine code.
    void Equipment::Tick()
    {
        const State& st = State::Get();

        // Infinite Item Durability: keep all equipped weapons, shields, and armor
        // pinned at 100% (10,000 max durability) on both client and server realms.
        if (st.infDurability)
        {
            static ULONGLONG s_lastRepair = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - s_lastRepair >= 500)
            {
                RepairAll();
                s_lastRepair = now;
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
