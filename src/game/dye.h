#pragma once

#include <cstddef>
#include <cstdint>
#include "dye_profiles.h"

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
        static uintptr_t ClientRegistryGlobal(); // resolved native anchor, read-only consumers

        // The raw equip-batch hook capture (g_comp), validated, WITHOUT any
        // character routing. Deliberately recursion-free - ActivePlayerCharacterIdx()
        // scans it to identify who is on screen, so it must never call back
        static uintptr_t HookedClientComp();
        static uintptr_t HookedMountComp();
        static uintptr_t HookedCharComp(int charIdx);

        // Character selection (0 = Kliff, 1 = Damiane, 2 = Oongka)
        static void SetActiveCharacter(int index);
        static int  GetActiveCharacter();

        // Target Mode (0 = Player Character, 1 = Mount / Horse)
        static void SetTargetMode(int mode);
        static int  GetTargetMode();
        static void SetActiveMount(int index);
        static int  GetActiveMount();

        // Native EquipBatch access for triggering mesh/model updates
        static void* GetEquipBatch();
        static void  TriggerEquipMeshRebuild(uintptr_t comp);

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
            // Navigation identity only. Never use these copies as write pointers.
            int characterIndex = -1, targetMode = 0, mountIndex = 0;
            uintptr_t controlledOwner = 0, worldRoot = 0;
            uintptr_t sourceComp = 0, sourceEntry = 0, sourceOwner = 0;
            uint64_t selectionGeneration = 0, worldGeneration = 0;
            bool SameTarget(const SlotInfo& other) const
            {
                return sourceComp && sourceEntry && tag == other.tag && typeId == other.typeId &&
                    instanceId == other.instanceId && characterIndex == other.characterIndex &&
                    targetMode == other.targetMode && mountIndex == other.mountIndex &&
                    controlledOwner == other.controlledOwner && worldRoot == other.worldRoot &&
                    sourceComp == other.sourceComp && sourceEntry == other.sourceEntry &&
                    sourceOwner == other.sourceOwner && selectionGeneration == other.selectionGeneration &&
                    worldGeneration == other.worldGeneration;
            }
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
        static bool GetChannel(uint16_t tag, int channel, Channel* out, const SlotInfo* expected = nullptr);

        // --- Apply / clear (queued to the game thread) -----------------------
        // `channel` 0..11, or -1 for all 12 channels at once. Calls into
        // engine code, so the request is queued and Tick() runs it within a
        // frame - same pattern as Inventory::AddItem.
        static bool Apply(uint16_t tag, int channel, const Channel& c, const SlotInfo* expected = nullptr);
        // Material/condition-only edit of existing dyed zones. Debounced on the
        // game-thread queue; each zone retains its color. Flags select which
        // field changes so material edits cannot reset per-zone condition.
        static bool Retouch(uint16_t tag, int channel, bool materialChanged, uint16_t material,
                            bool conditionChanged, uint8_t condition, const SlotInfo* expected = nullptr);
        static void CancelRetouch(); // cancel only the unstarted debounce, never a staged native apply
        static bool ApplyAllEquipped(const Channel& c);
        static bool Clear(uint16_t tag, int channel, const SlotInfo* expected = nullptr);
        static bool InjectAllToSave();

        // Named, on-demand presets read from the currently loaded equipment.
        // Saving/application is queued and bound to the displayed native item.
        static bool SaveProfile(uint16_t tag, const char* name, uint32_t replaceId = 0, const SlotInfo* expected = nullptr);
        static bool ApplyProfile(uint16_t tag, uint32_t id, const SlotInfo* expected = nullptr);
        static std::vector<DyeProfile> Profiles();
        static bool RenameProfile(uint32_t id, const char* name);
        static bool DeleteProfile(uint32_t id);
        static std::string ProfileError();
        static bool Busy();
        static bool HasResult();

        // Outcome of the most recent request, for a toast. Read-and-clear: a
        // Done/Failed is reported once, then the state returns to Idle.
        enum class OpState { Idle, Pending, Done, DoneDataOnly, Failed, ProfileSaved, ProfileSaveFailed, NoDyedZones };
        static OpState Status();

        // Game-thread pump - runs a queued Apply/Clear. Called from the same
        // per-frame driver as Inventory::Tick(); never from the render thread.
        static void Tick();
        // Game-thread only; bounded registry discovery with exact item matching.
        static uintptr_t FindClientEquipmentReplica(uintptr_t source, int character, uint16_t tag,
                                                    uint16_t type, int64_t instance);
    };
}
