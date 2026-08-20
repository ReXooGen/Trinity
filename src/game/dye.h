#pragma once

#include <cstddef>
#include <cstdint>

namespace trinity::game
{
    // Armor dye / material / repair-condition editor - the dyehouse, from the
    // menu, for whatever is currently equipped. See the dye section of
    // offsets.h for the full RE background; the short version:
    //
    //  - Dye state is 16-byte records ON the item value (color group, RGB,
    //    material template, repair/weathering byte, keyed by a 0..11 channel =
    //    one colorable zone of the mesh).
    //  - We apply through the client's own dye-ack handler - the exact code
    //    the dyehouse transaction drives - so records land in the equipped
    //    entry AND the rendered materials update live, no re-equip needed.
    //    Unlike the dyehouse there is no palette restriction: any RGB works.
    //  - The equipped entry is the render source but not the durable copy, so
    //    the same records are mirrored into the item value in the inventory
    //    holders (both realms, matched by instance id) with the engine's own
    //    upsert - that is the state the dyehouse itself persists.
    //
    // The equip component is captured from the game's own player-equip batch
    // function, which fires during load-in dress-up and on every gear change.
    class Dye
    {
    public:
        static bool Install();
        static void Remove();

        // True once the equip component has been captured and its slot table
        // reads back sane. False until the player has loaded into the world
        // (or, worst case, until they change any equipment piece once).
        static bool Ready();
        static uintptr_t ActiveClientComp();

        // Character selection (0 = Kliff, 1 = Damiane, 2 = Oongka)
        static void SetActiveCharacter(int index);
        static int  GetActiveCharacter();

        // Target Mode (0 = Player Character, 1 = Mount / Horse)
        static void SetTargetMode(int mode);
        static int  GetTargetMode();
        static void SetActiveMount(int index);
        static int  GetActiveMount();

        // --- Equipped-slot snapshot (menu side; guarded reads only) ---------
        // Rebuilt on every call cheap enough for a menu frame: the table is
        // ~a dozen entries. Indices are positions in the snapshot, valid only
        // until the next call.
        struct SlotInfo
        {
            uint16_t tag;        // engine slot tag (helm 3, chest 4, ...)
            uint16_t typeId;     // item type
            int64_t  instanceId; // item instance (allocator id)
            uint32_t dyeCount;   // records currently on the equipped entry
            int      maxZones;   // detected dyeable zones count (e.g. 2 for Chamfron/Barding, 8 for Chest)
            bool     dyeable;    // item's prefab is in the game's dye registry
                                 // (partprefabdyeslotinfo) - see dye_data.h
            char     slotName[24];
            char     itemName[64];
            char     icon[96];   // game sprite name ("ItemIcon_Prefab_...")
        };
        static int  SlotCount();               // refreshes the snapshot
        static bool GetSlot(int idx, SlotInfo* out);

        // One channel's current record on the equipped entry of `tag`, straight
        // from live memory. Returns false when the channel has no record.
        struct Channel
        {
            uint32_t groupKey;
            uint8_t  r, g, b;
            uint16_t materialId; // 0xFFFF = natural
            uint8_t  repair;     // 0 pristine .. 127 weathered (0xFF legacy)
        };
        static bool GetChannel(uint16_t tag, int channel, Channel* out);

        // --- Apply / clear (queued to the game thread) -----------------------
        // `channel` 0..11, or -1 for all 12 channels at once. Calls into
        // engine code, so the request is queued and Tick() runs it within a
        // frame - same pattern as Inventory::AddItem.
        static bool Apply(uint16_t tag, int channel, const Channel& c);
        static bool Clear(uint16_t tag, int channel);
        static bool InjectAllToSave();

        // Outcome of the most recent request, for a toast. Read-and-clear: a
        // Done/Failed is reported once, then the state returns to Idle.
        enum class OpState { Idle, Pending, Done, Failed };
        static OpState Status();

        // Game-thread pump - runs a queued Apply/Clear. Called from the same
        // per-frame driver as Inventory::Tick(); never from the render thread.
        static void Tick();
    };
}
