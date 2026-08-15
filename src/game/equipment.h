#pragma once

#include <cstddef>
#include <cstdint>

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
    //  - Socket state lives ON the item value, right beside the dye vector: a
    //    pre-allocated 5-record vector at +0x58 (record[i] = socket i) plus an
    //    unlocked-socket count at +0x68. Because it is pre-allocated, editing a
    //    socket is a plain record overwrite - no allocation, no engine call.
    //  - Add/clear is durable: the game's own Witch-socket touches only the
    //    record bytes, and those ride with the item value on save, so we mirror
    //    the write into both realms (like the dye editor) and it persists.
    //  - Unlock renders live but is NOT durable yet: a real unlock also grows a
    //    save-data sublist we do not reproduce, so it reverts on reload.
    //
    // The equip component is reached by the same per-realm walk the dye editor
    // uses, off each realm's player character (Inventory::Client/ServerCharacterAddr).
    class Equipment
    {
    public:
        static bool Install();
        static void Remove();

        // True once the client equip component reads back sane (i.e. the player
        // has loaded into the world). Mirrors Dye::Ready().
        static bool Ready();

        // True once the server-authority equip component is resolvable, i.e.
        // add/clear edits will persist. Until then they apply visually but a
        // reload will not keep them - the UI can warn with this.
        static bool EditsPersist();

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
        };
        struct SlotInfo
        {
            uint16_t tag;           // engine slot tag (helm 3, chest 4, main-hand 0, ...)
            uint16_t typeId;        // the equipped item
            int64_t  instanceId;
            int      unlockedCount; // sockets currently usable (0..5)
            int      filledCount;   // of those, how many hold a gear
            int      refineLevel;   // refinement/enhancement level (0..10)
            char     slotName[24];
            char     itemName[64];
            char     icon[96];      // sprite name for ui::DrawItemIcon
            Socket   sockets[kMaxSockets];
        };
        static int  SlotCount();                 // refreshes the snapshot
        static bool GetSlot(int idx, SlotInfo* out);

        // --- The abyss-gear catalog (for the picker) -------------------------
        // Every abyss gear the game defines, from Inventory's item catalog
        // (category "Abyss Gear"). Built once, then just read. name/icon point
        // at internal buffers valid until the next catalog access.
        static int  GearCount();
        static bool GetGear(int idx, uint16_t* typeId, const char** name, const char** icon);

        // --- Edits -----------------------------------------------------------
        // Raw record writes (no engine call), so these run inline and return
        // right away - no queue, unlike the dye/add-item paths. Each writes the
        // client realm (renders) and, when resolvable, the server realm (persists).
        //
        // socketIdx is 0..unlockedCount-1. AddGear puts `gearTypeId` in it;
        // ClearGear empties it. `persisted` (optional) reports whether the
        // durable server write also landed.
        static bool AddGear(uint16_t tag, int socketIdx, uint16_t gearTypeId, bool* persisted = nullptr);
        static bool ClearGear(uint16_t tag, int socketIdx, bool* persisted = nullptr);

        // Set this piece's refinement level (0..kRefineMax). Writes +0x0A on the
        // client realm (so the piece reads back at the new level) and mirrors it
        // into the server realm so it persists like the gear writes. `persisted`
        // (optional) reports whether the durable server write also landed.
        //
        // Live-verify caveats (see offsets.h): whether the stat change actually
        // takes hold - or only the displayed level - and whether the server keeps
        // an out-of-band level, can only be confirmed in-game. On success the
        // state is marked dirty so the next Tick runs the same effect refresh a
        // socket edit does, the best available "apply now" lever.
        static bool SetRefine(uint16_t tag, int level, bool* persisted = nullptr);

        // Unlock every socket on the piece (open all five). Durable, like the
        // gear writes - both realms get every record a real index.
        static bool UnlockAll(uint16_t tag);

        // Empty every unlocked socket on the piece (remove all gears, keep the
        // sockets open). Durable, both realms.
        static bool ClearAll(uint16_t tag);

        // Game-thread upkeep: after a socket edit, re-aggregates the equipped
        // items' effects (the same pass BatchEquip runs on a gear change) so a
        // newly socketed gear takes effect live instead of only after a reload.
        // Driven from the same per-frame game-thread driver as Dye::Tick();
        // never call from the render thread.
        static void Tick();
    };
}
