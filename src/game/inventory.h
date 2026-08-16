#pragma once

#include <cstddef>
#include <cstdint>

namespace trinity::game
{
    // Inventory browser / editor. See offsets.h (kSig_InvCoreGlobal,
    // kSig_InvGetItemQty, kSig_InvGetHolder, kStr_ItemInfoTable) for the RE
    // background: the live item holder is resolved primarily by a pure
    // pointer walk from the core global singleton (works from load), with a
    // hook on the HUD's item-count accessor as a second capture path.
    //
    // Item names AND category names are the game's OWN localised text, read
    // live (kSig_LocStringGet) rather than from any baked-in table, so they
    // match the installed version and the player's language. Grouping is the
    // game's own too: each item lists the "ItemGroupInfo" rows it belongs to,
    // and the one the inventory displays is picked by _orderIndex - the same
    // categories and order the game's own tabs use ("Ranged Weapon", "Elixir",
    // "Crafting and Refinement Material"). Icons likewise: both items and
    // categories name their sprite through the "stringinfo" table. Nothing
    // about any of it is hardcoded.
    //
    // The snapshot is STORAGE -> category -> item. The outer level is real: one
    // holder holds every storage you own (your pack, Private Storage, Wardrobe,
    // the Bank, the Kuku Cooler...), one bucket each, and each bucket names
    // itself via bucket+0x10 -> the "InventoryInfo" table (see offsets.h). Its
    // names are the game's own localised ones, like everything else here.
    // Before this split the levels were merged, so a stack in the Bank and one
    // in your pack appeared as two identical rows with no way to tell them
    // apart - and editing one silently touched a storage you were not looking
    // at.
    //
    // Editing a quantity writes the slot in BOTH the client mirror and the
    // server-authority holder - writing only the client reverts, because a
    // per-frame server reconcile overwrites it. The server holder is learned
    // from the transaction-commit hook (kSig_InvCommit), which sees it during
    // the save load itself; see that signature's comment for why it must be
    // that hook, and why the capture must not be filtered at capture time.
    class Inventory
    {
    public:
        static bool Install();
        static void Remove();

        // True once a live inventory holder resolves (usually immediately
        // after load via the global walk).
        static bool Ready();

        // Rebuild the grouped snapshot from live memory. Throttled internally,
        // so it is safe to call every frame while the inventory menu is open.
        static void Refresh();

        // Same as Refresh(), but bypasses the throttle - for a manual
        // "Refresh" action in the UI.
        static void ForceRefresh();

        // The storage list is rebuilt on every Refresh() from the storages that
        // actually hold something, in the game's own order, so indices are only
        // valid until the next Refresh() and empty storages never show. `name`
        // is the game's own localised one ("Private Storage", "Wardrobe"),
        // falling back to a readable form of the engine key ("WareHouse") and
        // finally to the raw type - never blank. `key` is the engine key, and
        // `Slots` reports the storage's own default/max slot counts (false if
        // the table did not resolve).
        static int         StorageCount();
        static const char* StorageName(int st);
        static const char* StorageKey(int st);
        static bool        StorageSlots(int st, int* defaultSlots, int* maxSlots);

        // Total items in a storage, across all its categories - for a count on
        // the storage row without walking its categories.
        static int         StorageItemCount(int st);

        // Categories within one storage, rebuilt per Refresh() from the
        // categories actually present there, in the game's own tab order. Never
        // empty categories.
        static int         CategoryCount(int st);
        static const char* CategoryName(int st, int cat);
        static int         ItemCount(int st, int cat);

        // The game's top-level tab this category sits under ("Equipment",
        // "Food", "Materials", "Documents", "Others"). Empty string when the
        // category has none - that is legitimate, not an error (e.g. Currency
        // items list no top tab). For grouping the category list in the UI.
        static const char* CategoryTab(int st, int cat);

        // The category's own icon, as a sprite name to hand to ui::DrawIcon
        // ("ItemIcon_ItemGroup_twohand_weapon"). Empty when it has none.
        static const char* CategoryIcon(int st, int cat);

        // idx is an index into the (unfiltered) item list of (st, cat); name and
        // icon point at internal buffers valid until the next Refresh(). `icon`
        // (optional) receives the game's own icon sprite name for this item
        // ("ItemIcon_Prefab_cd_phm_02_sword_0039"), for ui::DrawItemIcon;
        // empty when the item has no icon or stringinfo did not resolve.
        static bool GetItem(int st, int cat, int idx, const char** name, int64_t* qty,
                            const char** icon = nullptr);

        // Everything the snapshot knows about one item. The pointers are into
        // internal buffers valid until the next Refresh(). `key` is the engine
        // internal name ("Money_Copper"); `tier` is the game's own rarity
        // (0 = ordinary, rising to 5 for legendaries).
        struct ItemInfo
        {
            const char* name;
            const char* key;
            const char* icon;
            int64_t     qty;
            uint16_t    typeId;
            uint8_t     tier;
        };
        static bool GetItemInfo(int st, int cat, int idx, ItemInfo* out);

        // Set the quantity of item (st, cat, idx) to an exact value. Writes both
        // the client mirror and the server-authority holder so the edit is real
        // and survives the per-frame reconcile.
        static bool SetQuantity(int st, int cat, int idx, int64_t value);

        // Clear item (st, cat, idx) from its slot in both holders (typeId ->
        // empty, quantity -> 0).
        static bool RemoveItem(int st, int cat, int idx);

        // Add `qty` of `typeId` to the player's inventory - a genuinely new
        // stack in an empty slot, or a merge onto an existing one, exactly as a
        // pickup does. The item goes to whichever storage the item itself names
        // as its home; we do not choose.
        //
        // This does NOT write slots directly. It runs the game's own create
        // path - an allocator-issued unique instance id, the engine's insert
        // planner, its commit - because that is the only way an added item is
        // REAL. An earlier design fabricated a slot by copying a template and
        // stamping typeId+quantity; the items looked right, were unusable, and
        // it BRICKED A SAVE. Never go back to that. See the add-item note in
        // offsets.h for the full recipe and why each step is load-bearing.
        //
        // Both the client mirror and the server authority are written, each
        // built in its own realm and sharing one instance id - a server-only add
        // is invisible until a save/reload, and a client-only add is reconciled
        // away.
        //
        // Because it calls into engine code it must run on the GAME thread, so
        // this only QUEUES the request; Tick() performs it within a frame.
        // Returns false right away if the request is obviously invalid (unknown
        // typeId, qty < 1) or one is already pending; otherwise poll AddStatus()
        // for the outcome.
        static bool AddItem(uint16_t typeId, int64_t qty);

        // Outcome of the most recent AddItem() request. Pending until Tick()
        // has run it. Failed covers "the holders were not resolvable yet" as
        // well as a refusal, both of which are safe - nothing is written unless
        // the whole path is available.
        enum class AddState { Idle, Pending, Added, Failed };
        static AddState AddStatus();

        // Bulk add: queue `count` items (from `typeIds`), `qtyEach` of each, in
        // one action - "add X of every item in this category". Same engine path
        // as AddItem, just many of them, drained a few per Tick so the adds never
        // all land in one frame. Unknown/empty type ids are skipped. Returns
        // false if the batch is empty/invalid or a bulk add is already running
        // (one at a time); otherwise poll BulkAddStatus() for progress.
        static bool AddItemsBulk(const uint16_t* typeIds, int count, int64_t qtyEach);

        // Progress of the current/most recent bulk add. `active` is true while
        // the queue is draining; when it falls to false, `added`/`failed` hold
        // the final tally out of `total`. The counts latch until the next
        // AddItemsBulk() call resets them.
        struct BulkAdd { int total; int added; int failed; bool active; };
        static BulkAdd BulkAddStatus();

        // --- The catalog: every item the game defines -------------------------
        // Unlike the snapshot above (what you are carrying, rebuilt constantly),
        // this is the item TABLE - static data that cannot change while the game
        // runs, so it is built ONCE on first use and then just read.
        //
        // Grouped by category, and deliberately the same shape as the storage
        // browser's CategoryCount/CategoryName/ItemCount/GetItem above: it is
        // the same information, from the same game tables, so it should be the
        // same menu to walk. The only missing level is storage - a catalog item
        // is not in one yet, and when added it picks its own.
        //
        // Entries with no resolvable name are skipped: the table runs to
        // thousands of rows, plenty of them internal/unused, and an
        // "Item #4213" row is noise. `qty` in the ItemInfo is always 0 here - a
        // catalog entry is a definition, not a stack.
        static int         CatalogCategoryCount();
        static const char* CatalogCategoryName(int cat);
        static const char* CatalogCategoryTab(int cat);
        static const char* CatalogCategoryIcon(int cat);
        static int         CatalogItemCount(int cat);
        static bool        GetCatalogItem(int cat, int idx, ItemInfo* out);

        // True once the catalog has been built and holds something (it is built
        // on the first call to any of the above, which walks the whole table).
        static bool CatalogReady();

        // True once the server-authority holder is known. Normally true within
        // a second of loading a save, with no player action: loading commits
        // the server containers (before the client one even exists), and the
        // commit hook records them. Expected to be false only very early in a
        // load. Until then, edits apply visually but revert on the next
        // reconcile - the UI can use this to gate/warn.
        static bool EditsPersist();

        // --- Global overrides ------------------------------------------------
        // Two different kinds of write, despite the similar API:
        //
        // SetAllMaxStackSizes writes ItemInfo, the shared DEFINITION table
        // every item instance already reads its stack cap from - one pass
        // re-stamps every row at once, no live instance to reconcile against.
        // LIVE-CONFIRMED (2026-07-15, in-game): raising the cap works.
        //
        // SetAllSlotSizes has no table to stamp: a storage's slot cap is LIVE
        // state on the bucket objects themselves. It CALLS the game's own
        // slot-expansion setter (kSig_InvSetExpandSlots) once per bucket - the
        // same function the game runs when your expansion count changes.
        //
        // It used to write the cap field (kOff_InvBucket_MaxSlots) directly.
        // That field turned out to be a derived cache - the engine recomputes
        // it as `_defaultSlotCount + bucket._varyExpandSlotCount` on every
        // expansion sync, and as `clamp(_defaultSlotCount + buffAccumulator,
        // _maxSlotCount)` on every slot-expansion buff apply/expire - so the
        // write was reverted by anything that recomputed, and left the storage
        // internally inconsistent meanwhile. Driving the setter maintains the
        // expansion count, the buff accumulator and the cap together. See
        // kOff_InvBucket_ExpandSlots in offsets.h for the full field map.
        //
        // `value` is a target CAP; the setter takes an expansion, so each
        // bucket converts using its own storage's _defaultSlotCount. A cap
        // BELOW a storage's default is not expressible (expansion floors at 0),
        // and a storage whose InventoryInfo row does not resolve is skipped -
        // without its default there is no correct expansion to ask for.
        // Both client and server holders are driven, matching the quantity
        // editor's dual-write pattern (unconfirmed whether this state is even
        // reconciled per-frame the same way, but matching the established
        // pattern is the safe default).
        //
        // Unlike SetAllMaxStackSizes, this is re-driven EVERY tick, not on
        // change: a save load frees every bucket and builds new ones at
        // vanilla caps, so an edge-triggered apply works on the first load and
        // silently dies on every reload (LIVE-CONFIRMED 2026-07-15 - the bug
        // that sent this whole path back for a second look). Buckets already
        // at the target are skipped, so the steady state is one u16 read each.
        //
        // Both restore each row/bucket's own original value on `enable=false`
        // (captured the first time it was ever touched this session), so the
        // toggle behaves like every other one in the mod rather than leaving
        // anything permanently stomped.
        static bool SetAllMaxStackSizes(bool enable, int64_t value);
        static bool SetAllSlotSizes(bool enable, int value);

        // Game-thread upkeep for the two overrides above: applies (or
        // restores) whichever changed since the last call, retrying every
        // frame until a write actually lands (the tables may not be resolved
        // yet at the moment a toggle is flipped). Called from the same
        // per-frame driver as World::Tick() - never call from the render
        // thread.
        static void Tick();

        // --- Bridges for the dye editor (dye.cpp) -----------------------------
        // Item display name (localised, falling back to the engine key) and
        // icon sprite name for an arbitrary typeId - the same lookups the
        // browser rows use, for items dye.cpp reads off the equip component.
        static bool NameForTypeId(uint16_t typeId, char* out, size_t n);
        static bool IconForTypeId(uint16_t typeId, char* out, size_t n);

        // The live player CHARACTER of each realm (0 while unresolved), and the
        // calling thread's client/server realm flag address - the dual-realm
        // plumbing dye.cpp uses to reach both equip components and to write the
        // server-authority one. See the add-item notes for the rules (write
        // both, switch the realm around the server write, always restore the
        // flag).
        static uintptr_t ClientCharacterAddr();
        static uintptr_t ServerCharacterAddr();
        static uintptr_t CharacterAddr(int index);
        static uintptr_t RealmFlagAddress(uint8_t* outVal);
    };
}
