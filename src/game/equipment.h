#pragma once

#include <cstddef>
#include <cstdint>
#include "equipment_logic.h"

namespace trinity::game
{
    // Abyss-Gear socket editor - the equipment editor, from the menu, for
    // whatever is currently equipped. See the abyss-gear section of offsets.h
    // for the RE background; the short version:
    //
    //  - In Crimson Desert a piece's base stats are nearly fixed; the meaningful
    //    modifiers ("buffs") all come from Abyss Gears socketed into it. So an
    //    equipment editor is a socket editor: put any abyss gear into a socket,
    //    clear one, or unlock more sockets.
    //  - Socket state lives ON the item value: modern vector at +0x60 (legacy
    //    +0x58), followed by one unlock byte at vector+0x10. Only the native
    //    constructed size (0..5) is editable; capacity may exceed five. See
    //    socket_layout.h for guarded reads/writes that preserve native storage.
    //  - Add/clear/refine synchronize exact item copies across client/server.
    //    Synced confirms those memory writes; save/load and effect recomputation
    //    are separate native behaviors and require runtime verification.
    //  - Unlock renders live but is NOT durable yet: a real unlock also grows a
    //    save-data sublist we do not reproduce, so it reverts on reload.
    //
    class Equipment
    {
    public:
        static bool Install();
        static void Remove();

        // Readiness of the copied UI snapshot (200 ms refresh; up to 750 ms
        // grace for failed table reads in the same world/selection).
        static bool Ready();
        static void ForceRefresh();

        // Historical API name: true means the snapshot source has Server RTTI.
        // It is not an acknowledgement of an edit or a completed native save.
        static bool EditsPersist();
        enum class EditState { Idle, Pending, Synced, DataOnly, Failed };
        static EditState Status();
        static bool HasEditResult();

        // Character selection (0 = Kliff, 1 = Damiane, 2 = Oongka)
        static void        SetActiveCharacter(int index);
        static int         GetActiveCharacter();
        static const char* CharacterName(int index);
        static uintptr_t   ClientCompFor(int charIdx);
        static uintptr_t   ServerCompFor(int charIdx);
        static bool        IsItemForCharacter(int charIdx, uint16_t typeId, const char* name = nullptr, const char* key = nullptr);
        static bool        IsItemForSlot(uint16_t slotTag, uint16_t typeId, const char* name = nullptr, const char* key = nullptr);
        static const char* SlotNameForTag(uint16_t tag);
        static bool        IsItemEquippedOnAnyCharacter(uint16_t typeId);

        static constexpr int kMaxSockets = 5;
        static constexpr int kRefineMax  = 10; // refinement caps at level 10

        // --- Equipped-piece snapshot (menu side; guarded reads only) ---------
        struct Socket
        {
            bool     unlocked;      // index < the piece's unlocked count
            bool     filled;        // holds a gear
            uint16_t gearTypeId;    // 0xFFFF when empty
            char     gearName[64];  // resolved gear name (empty when unfilled)
            char     gearIcon[96];  // sprite name for ui::DrawItemIcon (empty when unfilled)
            char     gearBuff[64];  // stat effect description (e.g. "Attack 1", "Abyss Dmg +15%")
        };
        struct SlotInfo
        {
            uint16_t tag;           // engine slot tag (helm 3, chest 4, main-hand 0, ...)
            uint16_t typeId;        // the equipped item
            int64_t  instanceId;
            // Value-only identity stamp for displayed-target validation. The
            // owner token is compared, never used as a cached write pointer.
            int      characterIndex = -1;
            uintptr_t controlledOwner = 0;
            bool     socketsAvailable = false; // false = unreadable, not "no sockets"
            int      unlockedCount; // sockets currently usable (0..5)
            int      filledCount;   // constructed records holding a gear
            int      maxSockets;    // constructed native size (0..5), not allocation capacity
            int      refineLevel;   // refinement/enhancement level (0..10)
            int      durability;    // durability (e.g. 10000 = 100%)
            int      attack;        // total weapon/gear attack power
            int      defense;       // total armor/gear defense power
            int      reinforceExp;  // reinforcement progress (e.g. 72 / 100)
            int      reinforceBonus;// reinforcement attack/defense bonus (e.g. +2)
            char     slotName[24];
            char     itemName[64];
            char     icon[96];      // sprite name for ui::DrawItemIcon
            Socket   sockets[kMaxSockets];
        };
        static int  SlotCount();                 // shares the throttled Ready() snapshot
        static bool GetSlot(int idx, SlotInfo* out);
        static int  MaxSocketsForTag(uint16_t tag);

        // --- The abyss-gear catalog (for the picker) -------------------------
        // Every abyss gear the game defines, from Inventory's item catalog
        // (category "Abyss Gear"). Built once, then just read. name/icon point
        // at internal buffers valid until the next catalog access.
        static int  GearCount();
        static bool GetGear(int idx, uint16_t* typeId, const char** name, const char** icon);
        static const char* GetGearBuffDescription(const char* name);

        // --- Edits -----------------------------------------------------------
        // Queued, identity-stamped writes consumed one per game tick. true means
        // queued; Status reports client/server synchronization separately.
        //
        // socketIdx is 0..unlockedCount-1. AddGear puts `gearTypeId` in it;
        // ClearGear empties it. `persisted` stays false at enqueue time.
        // UI callers MUST pass the displayed row as expected. Its character,
        // world, tag, type and positive instance ID must match fresh native
        // reads; cached rows never authorize writes by tag/type alone. A row
        // without a positive instance ID cannot safely identify a cached item.
        // nullptr is for fresh native/batch operations, independent of UI state.
        static bool AddGear(uint16_t tag, int socketIdx, uint16_t gearTypeId, bool* persisted = nullptr, const SlotInfo* expected = nullptr);
        static bool ClearGear(uint16_t tag, int socketIdx, bool* persisted = nullptr, const SlotInfo* expected = nullptr);

        // Enqueues an identity-stamped game-thread refinement (0..kRefineMax).
        // true means queued; persisted stays false, with actual writes logged
        // by Tick. UI expected identity is checked before enqueue and the native
        // stamp is revalidated at apply time. Socket availability is independent.
        static bool SetRefine(uint16_t tag, int level, bool* persisted = nullptr, const SlotInfo* expected = nullptr);

        // Check whether this equipment slot is refinable (weapons, armor, accessories).
        // Strictly excludes utility items (Lantern, Axiom Bracelet, Tools, Mount Gear)
        // to prevent engine 0xC0000005 crashes during effect stat recomputation.
        static bool IsRefinableTag(uint16_t tag);

        // Unlock only constructed sockets; preserves the byte-sized unlock
        // field's neighbors. Native save-list growth is not reproduced.
        static bool UnlockAll(uint16_t tag, const SlotInfo* expected = nullptr);

        // Empty every unlocked socket on the piece (remove all gears, keep the
        // sockets open). Durable, both realms.
        static bool ClearAll(uint16_t tag, const SlotInfo* expected = nullptr);

        // --- 1-Click Batch Enhancers -----------------------------------------
        // Restore durability to 100% on all equipped weapons & armor
        static bool RepairAll(int* repairedCount = nullptr);

        // Refine all equipped gear to level (default 10)
        static bool RefineAll(int level = 10, int* refinedCount = nullptr);

        // Equip any weapon, shield, or armor directly to slot, bypassing quest progression locks
        // expected identifies the displayed item being replaced, not the new item.
        static bool EquipItemToSlot(uint16_t tag, uint16_t typeId, int64_t instId = 0, const SlotInfo* expected = nullptr);

        // Unlock constructed sockets (up to five) on freshly resolved equipment.
        static bool UnlockAllGears(int* unlockedCount = nullptr);

        // Direct low-level entry socket vector management
        static uintptr_t EnsureSocketVector(uintptr_t entry);
        static void      OpenAllSockets(uintptr_t entry, int maxSock = 5);

        // Historical profiles: saves are debounced/value-copied to an I/O worker;
        // loading never automatically replays equipment writes.
        static void SaveEquipProfilesToDisk();
        static void LoadEquipProfilesFromDisk();
        static void SavePlayerEquipSlot(int charIdx, uint16_t tag, uint16_t refineLvl, int maxSock, const uint16_t* gems);
        static void ClearPlayerEquipSlot(int charIdx, uint16_t tag);
        static bool HasCustomProfile(int charIdx, uint16_t tag);

        // Game-thread upkeep: after a socket edit, re-aggregates the equipped
        // items' effects (the same pass BatchEquip runs on a gear change) so a
        // newly socketed gear takes effect live instead of only after a reload.
        // Driven from the same per-frame game-thread driver as Dye::Tick();
        // never call from the render thread.
        static void Tick();
        static void TriggerEquipEffectRefresh(uintptr_t targetComp = 0);
    };
}
