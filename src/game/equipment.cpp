#include "equipment.h"

#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cctype>

#include "offsets.h"
#include "inventory.h"
#include "dye.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../core/logger.h"

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

        // --- Each realm's equip component, by walk (mirrors dye.cpp) ----------
        bool CompValid(uintptr_t comp)
        {
            if (comp < kMinPointer) return false;
            uintptr_t owner = 0;
            if (!ReadPtr(comp + kOff_EquipComp_Owner, &owner) || owner < kMinPointer) return false;
            uintptr_t desc = 0, array = 0;
            uint32_t  count = 0;
            if (!ReadPtr(comp + kOff_EquipComp_Table, &desc) || desc < kMinPointer) return false;
            if (!ReadPtr(desc + kOff_EquipTable_Array, &array) || array < kMinPointer) return false;
            if (!Read32(desc + kOff_EquipTable_Count, &count)) return false;
            return count >= 1 && count <= 64;
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

        uintptr_t ClientComp()
        {
            const uintptr_t active = Dye::ActiveClientComp();
            if (active) return active;
            return CompForCharacter(Inventory::ClientCharacterAddr());
        }
        uintptr_t ServerComp() { return CompForCharacter(Inventory::ServerCharacterAddr()); }

        // The TrItemValue copy the component keeps for the equipped slot `tag`.
        uintptr_t FindEntryByTag(uintptr_t comp, uint16_t tag)
        {
            uintptr_t desc = 0, array = 0;
            uint32_t  count = 0;
            if (!ReadPtr(comp + kOff_EquipComp_Table, &desc) || desc < kMinPointer) return 0;
            if (!ReadPtr(desc + kOff_EquipTable_Array, &array) || array < kMinPointer) return 0;
            if (!Read32(desc + kOff_EquipTable_Count, &count) || count == 0 || count > 64) return 0;

            for (uint32_t i = 0; i < count; ++i)
            {
                const uintptr_t entry = array + static_cast<uintptr_t>(i) * kEquipEntry_Stride;
                uint16_t t = 0;
                if (!Read16(entry + kOff_EquipEntry_SlotTag, &t) || t != tag) continue;
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
            uintptr_t data = 0;
            if (!ReadPtr(entry + kOff_ItemVal_SocketData, &data) || data < kMinPointer) return 0;
            return data;
        }

        int UnlockedCount(uintptr_t entry)
        {
            uint32_t n = 0;
            if (!Read32(entry + kOff_ItemVal_SocketUnlocked, &n)) return 0;
            return (n > static_cast<uint32_t>(kSocket_Max)) ? kSocket_Max : static_cast<int>(n);
        }

        uint16_t GearAt(uintptr_t data, int i)
        {
            uint16_t g = kSock_Empty;
            Read16(data + static_cast<uintptr_t>(i) * kSocketRec_Stride + kOff_SockRec_GearId, &g);
            return g;
        }

        // Overwrite record `i` with a filled (gear != 0xFFFF) or empty gear,
        // byte for byte the way the game's own socketing writes it.
        bool WriteRecord(uintptr_t data, int i, uint16_t gear)
        {
            const uintptr_t rec = data + static_cast<uintptr_t>(i) * kSocketRec_Stride;
            const bool filled = (gear != kSock_Empty);
            bool ok = true;
            ok &= Write16(rec + kOff_SockRec_GearId, gear);
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
            return WriteRecord(data, idx, gear);
        }

        // Open every socket on one realm's copy of the item: give each record a
        // real index (so the save, which counts records by their index byte -
        // 0xFF meaning "not a socket" - counts all five), keeping any gear the
        // record already holds, and set the live unlocked-count field to match.
        // This is the same per-record write that makes add/remove persist,
        // applied to all five slots; a bare +0x68 bump alone does NOT survive a
        // reload (the save counts records, not that field).
        void OpenAllSockets(uintptr_t entry)
        {
            const uintptr_t data = SocketData(entry);
            if (!data) return;
            for (int k = 0; k < kSocket_Max; ++k)
                WriteRecord(data, k, GearAt(data, k)); // normalise idx = k, keep the gear
            Write32(entry + kOff_ItemVal_SocketUnlocked, static_cast<uint32_t>(kSocket_Max));
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
                if (!strstr(low, "abyss")) continue;
                if (strstr(low, "gear")) { g_gearCat = c; return c; } // prefer "Abyss Gear"
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

            uintptr_t desc = 0, array = 0;
            uint32_t  count = 0;
            if (!ReadPtr(comp + kOff_EquipComp_Table, &desc)) return;
            if (!ReadPtr(desc + kOff_EquipTable_Array, &array) || array < kMinPointer) return;
            if (!Read32(desc + kOff_EquipTable_Count, &count) || count > 64) return;

            for (uint32_t i = 0; i < count && g_slotCount < kMaxSlots; ++i)
            {
                const uintptr_t entry = array + static_cast<uintptr_t>(i) * kEquipEntry_Stride;
                uint16_t tid = 0, tag = 0;
                int64_t  inst = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0 || tid > 50000) continue;
                if (!Read16(entry + kOff_EquipEntry_SlotTag, &tag)) continue;
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

                s.unlockedCount = UnlockedCount(entry);
                const uintptr_t data = SocketData(entry);
                for (int k = 0; k < Equipment::kMaxSockets; ++k)
                {
                    Equipment::Socket& so = s.sockets[k];
                    so.unlocked = (k < s.unlockedCount);
                    so.gearTypeId = data ? GearAt(data, k) : kSock_Empty;
                    so.filled = (so.gearTypeId != kSock_Empty);
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
        if (!g_refresh)
            LOG_WARN("equipment: effect-refresh signature not found - socketed gears "
                     "will only take effect after a reload.");
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

    int Equipment::SlotCount()
    {
        RebuildSnapshot();
        return g_slotCount;
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
        // Open all five sockets on the client (renders now) and mirror the same
        // per-record write to the server realm (so it persists) - exactly the
        // dual-realm path add/remove uses. Existing gears are preserved.
        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        const uintptr_t entry = FindEntryByTag(comp, tag);
        if (!entry) return false;
        int64_t instId = 0;
        if (!Read64(entry + kOff_ItemVal_InstanceId, &instId)) return false;

        OpenAllSockets(entry);

        const uintptr_t scomp = ServerComp();
        if (scomp)
        {
            const uintptr_t se = FindEntryByTag(scomp, tag);
            int64_t sid = 0;
            if (se && Read64(se + kOff_ItemVal_InstanceId, &sid) && sid == instId)
                OpenAllSockets(se);
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

    // Game thread: if a socket was edited, re-aggregate the equipped items'
    // effects on the client component so the change takes hold now instead of
    // waiting for a reload. This is the same pair BatchEquip runs on a gear
    // change; POD locals only, guarded, because it calls into engine code.
    void Equipment::Tick()
    {
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
