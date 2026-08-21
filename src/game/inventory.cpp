#include "inventory.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <thread>

#include <MinHook.h>

#include "offsets.h"
#include "player.h"
#include "item_names.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../mem/hooks.h"
#include "../core/logger.h"
#include "../core/text.h"
#include "../core/state.h"
#include "../core/version_detect.h"

namespace trinity::game
{
    using mem::Read64;
    using mem::Read32;
    using mem::Read16;
    using mem::Read8;
    using mem::ReadPtr;
    using mem::Write64;
    using mem::Write32;
    using mem::Write16;
    using mem::Write8;
    using mem::WritePtr;
    using mem::ReadCString;

    inline uintptr_t SlotStride() { return core::GetSlotStride(); }

    namespace
    {
        // --- Live handles (set on the game thread by the hook) --------------
        using GetItemQty_t = int64_t(__fastcall*)(void* container, uint16_t typeId, void* keyPtr);
        using GetHolder_t  = void*(__fastcall*)(void* container);
        // Per-holder insert: (bucket, err, CONTAINER, itemArr, i16, out, c, c, c).
        // Full 9-arg prototype so the trampoline forwards every argument.
        using HolderInsert_t = void*(__fastcall*)(void*, void*, void*, void*, uint16_t,
                                                  void*, uint8_t, uint8_t, uint8_t);
        // Transaction commit: (holder, err, CONTAINER, items, out, c, c).
        using Commit_t = void*(__fastcall*)(void*, void*, void*, void*, void*, uint8_t, uint8_t);
        // The game's own slot-expansion setter (kSig_InvSetExpandSlots):
        // (holder, &err, unused, bucketType, expansionCount). See offsets.h -
        // `count` is the expansion beyond _defaultSlotCount, not the cap.
        using SetExpandSlots_t = void*(__fastcall*)(void*, int*, void*, uint16_t, uint16_t);
        // --- The add-item primitives (see the add-item note in offsets.h) ----
        // Resolved, not hooked: we CALL these. oHolderInsert above doubles as
        // the insert PLANNER - it is the same function (kSig_InvHolderInsert),
        // and calling its trampoline runs the original without re-entering our
        // own capture hook.
        using ItemValueCtor_t   = void*(__fastcall*)(void* itemVal, uint16_t* typeId, int64_t qty);
        using CommitPlacement_t = void*(__fastcall*)(void* holder, int* err, void* unused,
                                                     void* placement, uint16_t slotIdx);
        using FreePlacements_t  = void(__fastcall*)(void* vec);
        using ItemValueDtor_t   = void(__fastcall*)(void* itemVal);
        GetItemQty_t      oGetItemQty      = nullptr;
        GetHolder_t       oGetHolder       = nullptr;
        HolderInsert_t    oHolderInsert    = nullptr;
        Commit_t          oCommit          = nullptr;
        SetExpandSlots_t  oSetExpandSlots  = nullptr;
        ItemValueCtor_t   oItemValueCtor   = nullptr;
        CommitPlacement_t oCommitPlacement = nullptr;
        FreePlacements_t  oFreePlacements  = nullptr;
        ItemValueDtor_t   oItemValueDtor   = nullptr;
        void*          g_qtyTarget   = nullptr;
        void*          g_insTarget   = nullptr;
        void*          g_commitTarget = nullptr;
        void*          g_expandTarget = nullptr;

        std::atomic<uintptr_t> g_holder{0};
        std::atomic<ULONGLONG> g_holderTick{0};

        // --- Candidate containers seen on the game thread -------------------
        // Every distinct container the commit hook observes, with the holder it
        // resolved to, recorded WITHOUT any filtering. Filtering at capture time
        // is what used to lose the server container: it is committed at load
        // BEFORE the client container exists, so any "is this the client?" test
        // is guaranteed to fail exactly when it matters. We record blind here
        // and work out which is the server one later, in ServerHolder(), once
        // the client side is resolvable. See kSig_InvCommit for the evidence.
        // `tick` is when the pair was captured, and exists so a candidate can be
        // aged before its liveness is judged: a container is not possessed the
        // instant it is created, so a just-captured entry looks dead to
        // IsLiveCharacter for a moment. Pruning on that would throw away the
        // load-time server capture - the one capture that matters.
        struct Candidate { uintptr_t container; uintptr_t holder; ULONGLONG tick; };
        constexpr int       kMaxCandidates = 16;
        constexpr ULONGLONG kCandGraceMs   = 10000; // before a corpse counts as one
        Candidate            g_cand[kMaxCandidates] = {};
        std::atomic<int>     g_candCount{0};
        CRITICAL_SECTION     g_candLock;
        bool                 g_candLockInit = false;

        // The server-authority holder: resolved lazily from g_cand, then cached.
        // A quantity edit must be written to BOTH holders or the per-frame
        // reconcile reverts it. g_serverContainer is the container that produced
        // it, kept so the cache can be RE-VALIDATED rather than trusted: the
        // holder alone cannot tell us it is still alive (freed memory can still
        // read back as a structurally sane bucket array), but its container can
        // - see IsLiveCharacter.
        std::atomic<uintptr_t> g_serverHolder{0};
        std::atomic<uintptr_t> g_serverContainer{0};
        std::atomic<ULONGLONG> g_serverTick{0};

        // Address of the core global singleton pointer (resolved once from
        // kSig_InvCoreGlobal at Install). Enables holder resolution by a pure
        // pointer walk, independent of the hook ever firing.
        uintptr_t g_coreGlobal = 0;

        // Address at which the item-info table object pointer is stored
        // (resolved once from the "iteminfo" string anchor at Install).
        uintptr_t g_itemTableGlobal = 0;

        // Same, for the "ItemGroupInfo" table (the inventory category tree), the
        // "stringinfo" table (icon sprite names), the "InventoryInfo" table
        // (what each storage is called) and the localisation manager (real
        // display names). All optional: without them items fall back to
        // prettified keys in one "Uncategorised" group, storages to their engine
        // key, and nothing draws an icon.
        uintptr_t g_grpTableGlobal = 0;
        uintptr_t g_strTableGlobal = 0;
        uintptr_t g_invTableGlobal = 0;
        uintptr_t g_locMgrGlobal   = 0;

        // --- Global table overrides (Max Stack Size) --------------------------
        // Original per-row values, captured lazily the FIRST time a row is
        // ever overridden this session, so disabling the toggle later restores
        // true vanilla values rather than whatever was last written. Sized to
        // the table's row count on first use; index is the row number, same as
        // DefForRow's `row` parameter.
        std::vector<int64_t> g_origMaxStack;
        std::vector<uint8_t> g_origApplyCap;
        std::vector<bool>    g_stackCaptured;

        // --- Slot Size override (live bucket fields, not a table) ------------
        // These live on the SAME bucket objects the rest of this file already
        // walks, keyed by bucket address (stable for the session) rather than
        // a table row - captured lazily, same reasoning as the stack-size
        // vectors above.
        //
        // We capture the EXPANSION count (kOff_InvBucket_ExpandSlots), not the
        // cap, because that is the value the engine actually stores; the cap
        // is derived from it. Restoring the expansion through the game's own
        // setter puts every dependent field back consistently, which poking
        // the cap back could not do - see offsets.h.
        //
        // Keyed by bucket address AND storage type, not address alone: loading
        // a save FREES every bucket and builds new ones, so an address can be
        // recycled by a different storage. Matching the type too means a stale
        // entry from a previous load can never hand back another storage's
        // expansion count. (Restores only ever walk live buckets, so stale
        // entries are otherwise inert - they simply stop matching.)
        struct BucketCap { uintptr_t bucket; uint16_t type; uint16_t origExpand; };
        std::vector<BucketCap> g_origBucketCap;

        // Guards g_origBucketCap: Tick() (game thread) captures and restores,
        // while hkSetExpandSlots refreshes entries from whichever thread the
        // engine stamps expansions on - the server realm's sync runs off the
        // game thread, so these genuinely race without it.
        CRITICAL_SECTION g_capLock;
        bool             g_capLockInit = false;

        bool FindOrigBucketCap(uintptr_t bucket, uint16_t type, uint16_t* out)
        {
            if (!g_capLockInit) return false;
            EnterCriticalSection(&g_capLock);
            bool found = false;
            for (const auto& e : g_origBucketCap)
                if (e.bucket == bucket && e.type == type) { *out = e.origExpand; found = true; break; }
            LeaveCriticalSection(&g_capLock);
            return found;
        }

        // First-touch capture for Tick()'s apply path: record the bucket's
        // current expansion once, so a later restore has the vanilla value.
        // A single lock hold covers the lookup and the insert, so a hook
        // refresh landing in between cannot duplicate the entry.
        void CaptureOrigExpandOnce(uintptr_t bucket, uint16_t type)
        {
            if (!g_capLockInit) return;
            EnterCriticalSection(&g_capLock);
            bool have = false;
            for (const auto& e : g_origBucketCap)
                if (e.bucket == bucket && e.type == type) { have = true; break; }
            uint16_t orig = 0;
            if (!have && Read16(bucket + kOff_InvBucket_ExpandSlots, &orig))
                g_origBucketCap.push_back({ bucket, type, orig });
            LeaveCriticalSection(&g_capLock);
        }

        // Refresh-or-insert for the expansion-setter hook: the engine just
        // told us this storage's TRUE vanilla expansion, which beats whatever
        // we captured earlier (the player may have consumed an expansion item
        // since) - so an existing entry is overwritten, not kept.
        void UpsertOrigExpand(uintptr_t bucket, uint16_t type, uint16_t vanilla)
        {
            if (!g_capLockInit) return;
            EnterCriticalSection(&g_capLock);
            bool have = false;
            for (auto& e : g_origBucketCap)
                if (e.bucket == bucket && e.type == type) { e.origExpand = vanilla; have = true; break; }
            if (!have)
                g_origBucketCap.push_back({ bucket, type, vanilla });
            LeaveCriticalSection(&g_capLock);
        }

        // Edge-tracking for Tick(): only re-walk the (potentially thousands of
        // rows) table when what we last successfully applied differs from what
        // the toggle currently wants - never every frame.
        bool    g_stackApplied  = false;
        int64_t g_stackAppliedVal = 0;
        bool    g_slotApplied     = false;
        int     g_slotAppliedVal  = 0;

        // Transaction guard: set while the engine's commit function is active.
        // All periodic container writes (RepairUsedSlots, SetAllSlotSizes, etc.)
        // must check this flag and SKIP to avoid racing with quest/trade/reward
        // transactions that read container metadata for packet validation.
        std::atomic<bool> g_commitActive{false};

        void EnsureTablesResolved();

        // Resolve a def pointer out of one of the shared-layout "*info" tables.
        bool DefForRow(uintptr_t tableGlobal, uint16_t row, uintptr_t* out)
        {
            if (!tableGlobal)
            {
                EnsureTablesResolved();
                if (!tableGlobal) return false;
            }
            uintptr_t table = 0;
            if (!ReadPtr(tableGlobal, &table) || table < kMinPointer) return false;
            uint32_t count = 0;
            if (!Read32(table + kOff_ItemTable_Count, &count) || row >= count) return false;
            uintptr_t defs = 0;
            // Try +0x58 (TU 1.17 - 1.18+ modern) first, else +0x50 (TU 1.10 - 1.16 legacy)
            if (!ReadPtr(table + 0x58, &defs) || defs < kMinPointer)
            {
                if (!ReadPtr(table + 0x50, &defs) || defs < kMinPointer)
                    return false;
            }
            uintptr_t def = 0;
            if (!ReadPtr(defs + static_cast<uintptr_t>(row) * 8, &def)) return false;
            if (def < kMinPointer) return false;
            *out = def;
            return true;
        }

        // A plain string field: fieldAddr -> string object -> char*. Shared by
        // every *info row's _stringKey and by stringinfo's _buffer.
        bool StringField(uintptr_t fieldAddr, char* out, size_t n)
        {
            uintptr_t strObj = 0;
            if (!ReadPtr(fieldAddr, &strObj)) return false;
            uintptr_t buf = 0;
            if (!ReadPtr(strObj, &buf)) return false; // *strObj = char*
            return ReadCString(buf, out, n);
        }

        // --- Item name (engine key) via the iteminfo table ------------------
        // typeId -> def -> string object -> char* key. All reads guarded.
        bool KeyForType(uint16_t typeId, char* out, size_t n)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_itemTableGlobal, typeId, &def)) return false;
            return StringField(def + kOff_ItemDef_Key, out, n);
        }

        // --- Localised text (see kSig_LocStringGet) --------------------------
        // Replicates the engine's own getter as guarded reads. `structAddr` is
        // any 32-byte localised-string field (ItemInfo._itemName,
        // ItemGroupInfo._groupName, ...). The blob bounds check is load-bearing:
        // text is interned lazily, and a string that has not been interned yet
        // stores -1, which fails the check and makes us fall back rather than
        // read a wild pointer.
        bool LocString(uintptr_t structAddr, char* out, size_t n)
        {
            if (!g_locMgrGlobal) return false;
            uintptr_t provider = 0;
            if (!ReadPtr(structAddr, &provider) || provider < kMinPointer) return false;
            uint32_t off = 0;
            if (!Read32(provider + kOff_LocProv_Offset, &off)) return false;
            uintptr_t mgr = 0, blob = 0, data = 0;
            uint32_t  size = 0;
            if (!ReadPtr(g_locMgrGlobal, &mgr) || mgr < kMinPointer) return false;
            if (!ReadPtr(mgr + kOff_LocMgr_Blob, &blob) || blob < kMinPointer) return false;
            if (!Read32(blob + kOff_LocBlob_Size, &size) || off >= size) return false; // covers -1
            if (!ReadPtr(blob + kOff_LocBlob_Data, &data) || data < kMinPointer) return false;
            if (!ReadCString(data + off, out, n)) return false;
            return out[0] != 0;
        }

        bool DisplayNameForType(uint16_t typeId, char* out, size_t n)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_itemTableGlobal, typeId, &def)) return false;
            return LocString(def + kOff_ItemDef_Name, out, n);
        }

        // --- The game's own category tree ------------------------------------
        // An item lists the ItemGroupInfo rows it belongs to; _orderIndex says
        // what each one is (see offsets.h). Of those: 65535 never displays,
        // <=5 is the top tab, and the smallest of the rest is the category the
        // inventory shows. `tab` is optional - some items have no top tab.
        struct Category { uint16_t row; uint16_t order; uint16_t tabRow; uint16_t tabOrder; };
        constexpr Category kNoCategory{ 0xFFFF, kGrpOrder_Internal, 0xFFFF, kGrpOrder_Internal };

        bool GroupOrder(uint16_t row, uint16_t* order)
        {
            uintptr_t grp = 0;
            if (!DefForRow(g_grpTableGlobal, row, &grp)) return false;
            return Read16(grp + kOff_GrpDef_Order, order);
        }

        bool CategoryOfType(uint16_t typeId, Category* out)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_itemTableGlobal, typeId, &def)) return false;
            uintptr_t rows = 0;
            uint32_t  count = 0;
            if (!ReadPtr(def + kOff_ItemDef_Groups + kOff_Vec_Data, &rows)) return false;
            if (!Read32(def + kOff_ItemDef_Groups + kOff_Vec_Count, &count)) return false;
            if (rows < kMinPointer || count == 0 || count > 4096) return false;

            Category c = kNoCategory;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint16_t row = 0, order = 0;
                if (!Read16(rows + static_cast<uintptr_t>(i) * 2, &row)) continue;
                if (!GroupOrder(row, &order) || order == kGrpOrder_Internal) continue;
                if (order <= kGrpOrder_MaxTopTab)
                {
                    if (order < c.tabOrder) { c.tabRow = row; c.tabOrder = order; }
                }
                else if (order < c.order) { c.row = row; c.order = order; }
            }
            if (c.row == 0xFFFF) return false;
            *out = c;
            return true;
        }

        void Prettify(const char* key, char* out, size_t n);

        void CleanCategoryFallback(char* label, size_t n)
        {
            // The engine keys are identifiers rather than prose. Normalize
            // their common vocabulary only when localisation is unavailable;
            // real translated category names never pass through this function.
            struct Rename { const char* from; const char* to; };
            static constexpr Rename exact[] = {
                { "Ammo",             "Ammunition" },
                { "bag",              "Bags" },
                { "ETC Ku Ku Pot All", "Kuku Pot (All)" },
                { "Ku Ku Pot",        "Kuku Pot" },
                { "ETC Lure",         "Lures" },
                { "trade Unpack",     "Unpacked Trade Goods" },
                { "trade Packed",     "Packed Trade Goods" },
                { "Animal Item",      "Animal Items" },
                { "Goods",            "Trade Goods" },
                { "Equip Weapon One Hand",        "One-Handed Weapons" },
                { "Equip Weapon Shield",          "Shields" },
                { "Equip Armor Shield",           "Shields" },
                { "Equip Armor Player Shield",    "Shields" },
                { "Equip Shield",                 "Shields" },
                { "Weapon Shield",                "Shields" },
                { "Armor Shield",                 "Shields" },
                { "Equip Weapon Two Hand",        "Two-Handed Weapons" },
                { "Equip Weapon Range",           "Ranged Weapons" },
                { "Equip Weapon One Hand Dagger", "Daggers" },
                { "Equip Armor Player Helm",      "Helmets" },
                { "Equip Armor Player Armor",     "Armor" },
                { "Equip Armor Player Cloak",     "Cloaks" },
                { "Equip Armor Player Gloves",    "Gloves" },
                { "Equip Armor Player Boots",     "Boots" },
                { "Equip Accessory Necklace",     "Necklaces" },
                { "Equip Accessory Ring",         "Rings" },
                { "Equip Accessory Glasses",      "Glasses" },
                { "Equip Accessory Mask",         "Masks" },
                { "Equip Back Pack",              "Backpacks" },
                { "Equip Riding",                 "Riding Gear" },
                { "Equip Pet Armor",              "Pet Armor" },
                { "Vehicle Special",              "Special Vehicles" },
                { "Korea Food",                   "Korean Food" },
                { "Potion",                       "Potions" },
                { "Food Horse",                   "Horse Food" },
                { "Material Food",                "Food Materials" },
                { "Material Medical",             "Medical Materials" },
                { "Material Object",              "Objects" },
                { "ETC Book",                     "Books" },
                { "ETC Book Recipe",              "Recipe Books" },
                { "ETC Craft Recipe",             "Crafting Recipes" },
                { "ETC Treasure Map",             "Treasure Maps" },
                { "ETC Document",                 "Documents" },
                { "ETC Document Wall Paper",      "Wall Documents" },
                { "ETC Document Wanted",          "Wanted Posters" },
                { "Equip Tool",                   "Tools" },
                { "Money",                        "Currency" },
                { "ETC Quest Memory",             "Quest Memories" },
                { "ETC Quest Equip Special Boss", "Special Boss Quest Equipment" },
                { "ETC Key",                      "Keys" },
                { "Sealed Artifact",              "Sealed Artifacts" },
                { "Control",                      "Controls" },
            };
            for (const Rename& r : exact)
                if (_stricmp(label, r.from) == 0)
                {
                    snprintf(label, n, "%s", r.to);
                    return;
                }

            // Title-case ordinary lowercase words while preserving deliberate
            // all-caps abbreviations such as ETC, HP and MP.
            bool wordStart = true;
            for (char* p = label; *p; ++p)
            {
                if (*p == ' ' || *p == '-' || *p == '(') { wordStart = true; continue; }
                if (wordStart && islower(static_cast<unsigned char>(*p)))
                    *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
                wordStart = false;
            }
        }

        bool GroupName(uint16_t row, char* out, size_t n)
        {
            uintptr_t grp = 0;
            if (!DefForRow(g_grpTableGlobal, row, &grp)) return false;
            if (LocString(grp + kOff_GrpDef_Name, out, n)) return true;

            // Localisation is often still lazy/unavailable when the Add Item
            // catalog is first opened in 1.17. The category row nevertheless
            // carries a stable engine key such as
            // "ItemGroup_SubCategory_Equip_Weapon_Range". Use that game-owned
            // key as a readable fallback instead of collapsing every valid row
            // into the synthetic "Uncategorised" label.
            char key[160]{};
            if (!StringField(grp + kOff_GrpDef_Key, key, sizeof(key))) return false;
            const char* readable = key;
            constexpr const char* kSub = "ItemGroup_SubCategory_";
            constexpr const char* kCat = "ItemGroup_Category_";
            constexpr const char* kAny = "ItemGroup_";
            if (_strnicmp(key, kSub, strlen(kSub)) == 0) readable += strlen(kSub);
            else if (_strnicmp(key, kCat, strlen(kCat)) == 0) readable += strlen(kCat);
            else if (_strnicmp(key, kAny, strlen(kAny)) == 0) readable += strlen(kAny);
            if (!*readable) return false;
            Prettify(readable, out, n);
            CleanCategoryFallback(out, n);
            return out[0] != 0;
        }

        // --- The game's own icons (see offsets.h) ----------------------------
        // An icon is named, not pathed: a u16 row in `stringinfo` whose _buffer
        // holds the sprite name ("ItemIcon_Prefab_cd_phm_02_sword_0039"). The
        // UI turns that into a file. This replaced a 6258-entry generated table
        // joined from community TSV dumps - same reason the name table went:
        // the game already knows, and its answer never goes stale.
        bool IconNameForRow(uint16_t row, char* out, size_t n)
        {
            if (row == kIconPath_None) return false;
            uintptr_t def = 0;
            if (!DefForRow(g_strTableGlobal, row, &def)) return false;
            if (!StringField(def + kOff_StrDef_Buffer, out, n)) return false;
            return out[0] != 0;
        }

        // An item's icon: the first entry of _itemIconList. Later entries are
        // conditional variants (gimmick state / sealed / usable); entry 0 is
        // the plain one the inventory draws.
        bool IconForType(uint16_t typeId, char* out, size_t n)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_itemTableGlobal, typeId, &def)) return false;
            uintptr_t list = 0;
            uint32_t  count = 0;
            if (!ReadPtr(def + kOff_ItemDef_Icons + kOff_Vec_Data, &list)) return false;
            if (!Read32(def + kOff_ItemDef_Icons + kOff_Vec_Count, &count)) return false;
            if (list < kMinPointer || count == 0 || count > 64) return false;
            uint16_t row = 0;
            if (!Read16(list + kOff_IconData_Path, &row)) return false;
            return IconNameForRow(row, out, n);
        }

        // A category's icon, from the same table via ItemGroupInfo._iconPath.
        // Only the displayed categories carry one; leaf groups store the
        // no-icon sentinel, which IconNameForRow rejects.
        //
        // Some displayed categories store the sentinel too, but only because the
        // sprite they want was never added to stringinfo - the .dds itself ships
        // ("Packaged Trade Goods"). For those, derive the name from the group's
        // own _stringKey; see the convention in offsets.h. A derived name that
        // isn't shipped just misses in DrawItemIcon and draws blank, which is
        // what the row does today anyway.
        bool IconForGroup(uint16_t groupRow, char* out, size_t n)
        {
            uintptr_t grp = 0;
            if (!DefForRow(g_grpTableGlobal, groupRow, &grp)) return false;
            uint16_t row = 0;
            if (Read16(grp + kOff_GrpDef_Icon, &row) && IconNameForRow(row, out, n))
                return true;

            char key[160];
            if (!StringField(grp + kOff_GrpDef_Key, key, sizeof(key))) return false;
            const size_t pfx = strlen(kGrpKey_SubCatPrefix);
            if (_strnicmp(key, kGrpKey_SubCatPrefix, pfx) != 0 || !key[pfx]) return false;
            snprintf(out, n, "%s%s", kIconPrefix_ItemGroup, key + pfx);
            return true;
        }

        uint8_t TierOfType(uint16_t typeId)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_itemTableGlobal, typeId, &def)) return 0;
            uint16_t w = 0; // no Read8 helper; the tier is the low byte
            if (!Read16(def + kOff_ItemDef_Tier, &w)) return 0;
            return static_cast<uint8_t>(w & 0xFF);
        }

        // --- Display categories ---------------------------------------------
        // Both the grouping AND the labels are the game's own: the category is
        // the item's ItemGroupInfo row that the inventory displays, and its
        // name is that row's localised _groupName - the exact text and order
        // the game's own tabs use ("Ranged Weapon", "Elixir", "Crafting and
        // Refinement Material"). Nothing here is hardcoded, so it follows game
        // updates and the player's language for free. See offsets.h.

        void CleanItemName(char* name, size_t n)
        {
            struct Rename { const char* from; const char* to; };
            static constexpr Rename exact[] = {
                { "Ziane Diary", "Jian's Journal" },
                { "Ziane_Diary", "Jian's Journal" },
                { "Ziane One Hand Sword", "Jian's One-Handed Sword" },
                { "Ziane_One_Hand_Sword", "Jian's One-Handed Sword" },
                { "Visione Chip Ziane Tomb", "Vision Chip - Jian's Tomb" },
            };
            for (const Rename& r : exact)
            {
                if (_stricmp(name, r.from) == 0)
                {
                    snprintf(name, n, "%s", r.to);
                    return;
                }
            }

            // Replace leading "Ziane " with "Jian's "
            if (_strnicmp(name, "Ziane ", 6) == 0)
            {
                char rest[64];
                snprintf(rest, sizeof(rest), "%s", name + 6);
                snprintf(name, n, "Jian's %s", rest);
            }
        }

        // Turn an engine key into a readable label: check official in-game
        // name mappings first, else fallback to '_' -> ' ' and space insertion
        // at lowercase/digit -> uppercase boundaries ("OneHandSword" ->
        // "One Hand Sword", "Money_Copper" -> "Money Copper").
        void Prettify(const char* key, char* out, size_t n)
        {
            if (!key || !out || n == 0) return;
            const char* resolved = ResolveItemDisplayName(key);
            if (resolved && resolved[0] != '\0')
            {
                snprintf(out, n, "%s", resolved);
                CleanItemName(out, n);
                return;
            }

            size_t o = 0;
            for (size_t i = 0; key[i] && o < n - 1; ++i)
            {
                const char c = key[i];
                if (c == '_') { if (o && out[o - 1] != ' ') out[o++] = ' '; continue; }
                const char prev = key[i ? i - 1 : 0];
                if (i && o < n - 1 &&
                    (islower(static_cast<unsigned char>(prev)) || isdigit(static_cast<unsigned char>(prev))) &&
                    isupper(static_cast<unsigned char>(c)))
                    out[o++] = ' ';
                if (o < n - 1) out[o++] = c;
            }
            out[o] = 0;
            if (o == 0) snprintf(out, n, "%s", key);
            CleanItemName(out, n);
        }

        // --- Storage presentation: order, and names the game cannot give us ---
        // The only hardcoded display text in this file, and only where the game
        // leaves us no choice: several storages share the localised name
        // "Inventory" (see kOff_InvDef_Name), so the game's own text cannot tell
        // them apart in one flat list. Keyed on the ENGINE key, which is stable
        // across patches and languages - never on the type number, whose
        // meaning we deliberately do not assume (see offsets.h).
        //
        // `name` is deliberately null wherever the game's own name is already
        // unique and correct: Storage, Wardrobe, Bank and Kuku Pot come back
        // right, so they keep the game's text and stay localised. Only the ones
        // the game calls "Inventory" need us to name them.
        //
        // Order is this table's order. Storages not listed (the housing chests,
        // a pet, a wagon) are not an error: they keep the game's own name and
        // sort after these. Both warehouse keys are listed because only one of
        // them is ever present, so either takes the same slot.
        struct StorageStyle { const char* key; const char* name; };
        constexpr StorageStyle kStorageStyle[] = {
            { "Character",          "Inventory"   },
            { "Quest",              "Quest Items" },
            { "Kuku",               nullptr       }, // the game says "Kuku Pot"
            { "BirdFeed",           nullptr       }, // the game says "Bird Feeder"
            { "WareHouse",          nullptr       }, // the game says "Storage"
            { "CampWareHouse",      nullptr       },
            { "Housing_Dresser",    nullptr       }, // the game says "Wardrobe"
            { "Bank",               nullptr       }, // the game says "Bank"
            { "Money",              "Camp"        }, // camp currency: food, timber, contribution
            { "InvisibleInventory", "Invisible"   },
        };
        constexpr int kStorageStyleCount =
            static_cast<int>(sizeof(kStorageStyle) / sizeof(kStorageStyle[0]));

        // Display rank: unlisted storages sort after every listed one.
        int StorageRank(const char* key)
        {
            for (int i = 0; i < kStorageStyleCount; ++i)
                if (strcmp(kStorageStyle[i].key, key) == 0) return i;
            return kStorageStyleCount;
        }

        // Our name for a storage, or null to use the game's.
        const char* StorageStyleName(const char* key)
        {
            for (const auto& s : kStorageStyle)
                if (strcmp(s.key, key) == 0) return s.name;
            return nullptr;
        }

        // --- The game's own storages -----------------------------------------
        // A bucket's type is an InventoryInfo row key, so naming a storage is
        // the same pointer walk as naming an item - just a different table.

        // The localised name ("Private Storage"), or false if the table or the
        // text did not resolve. Callers fall back to the engine key.
        bool StorageNameForType(uint16_t type, char* out, size_t n)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_invTableGlobal, type, &def)) return false;
            return LocString(def + kOff_InvDef_Name, out, n);
        }

        // The engine key ("WareHouse"), which every row has.
        bool StorageKeyForType(uint16_t type, char* out, size_t n)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_invTableGlobal, type, &def)) return false;
            return StringField(def + kOff_InvDef_Key, out, n);
        }

        // Always read DIRECTLY from the live table - never from a cache.
        // The table must NOT be mutated by the slot-size feature (see
        // ApplySlotCapToHolder / XeTrinityz-reference/src/game/inventory.cpp).
        bool StorageSlotsForType(uint16_t type, uint16_t* def_, uint16_t* max_)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_invTableGlobal, type, &def)) return false;
            return Read16(def + kOff_InvDef_DefSlots, def_) &&
                   Read16(def + kOff_InvDef_MaxSlots, max_);
        }

        // --- Grouped snapshot (render thread only) --------------------------
        // Storage -> category -> item. Both outer levels are rebuilt each
        // refresh from what is actually present, so they reflect what you own
        // and never show an empty storage or tab.
        // icon[] is generous on purpose: the sprite names run long (the worst
        // shipped one is 71 chars), and a truncated name silently misses its
        // file rather than failing loudly.
        struct Item { uintptr_t slot; uint16_t typeId; int64_t qty; char name[64];
                      char key[64]; char icon[96]; uint32_t bucketIdx; uint16_t slotIdx;
                      Category cat; uint8_t tier; };
        struct Group { Category cat; char label[48]; char tab[32]; char icon[96];
                       std::vector<Item> items; };
        struct Storage { uint16_t type; char name[96]; char key[48]; int rank;
                         uint16_t defSlots; uint16_t maxSlots; bool haveSlots;
                         bool gameNamed; std::vector<Group> groups; };
        std::vector<Storage> g_storages;
        ULONGLONG g_lastRefresh = 0;

        // Bounds-checked accessors for the two outer levels - every public
        // getter goes through these rather than repeating the index checks.
        Storage* StorageAt(int st)
        {
            return (st >= 0 && st < static_cast<int>(g_storages.size())) ? &g_storages[st] : nullptr;
        }
        Group* GroupAt(int st, int cat)
        {
            Storage* s = StorageAt(st);
            if (!s || cat < 0 || cat >= static_cast<int>(s->groups.size())) return nullptr;
            return &s->groups[cat];
        }
        Item* ItemAt(int st, int cat, int idx)
        {
            Group* g = GroupAt(st, cat);
            if (!g || idx < 0 || idx >= static_cast<int>(g->items.size())) return nullptr;
            return &g->items[idx];
        }

        // --- Holder resolution ------------------------------------------------
        // A holder is trusted only if it exposes a structurally sane bucket
        // array - that check doubles as staleness detection after a reload.
        bool HolderLooksValid(uintptr_t holder)
        {
            if (holder < kMinPointer) return false;
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return false;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount)) return false;
            return buckets >= kMinPointer && bcount > 0 && bcount <= 4096;
        }

        // A container's CURRENT holder: [[container+0x68]+0xB8], which is
        // GetInventoryHolder's own main path replicated as guarded reads, so the
        // render thread never calls into game code. Same walk for either realm -
        // the client and server containers are the same kind of object.
        uintptr_t HolderForContainer(uintptr_t container)
        {
            if (container < kMinPointer) return 0;
            uintptr_t sub = 0, holder = 0;
            if (!ReadPtr(container + kOff_Container_Sub, &sub) || sub < kMinPointer) return 0;
            if (!ReadPtr(sub + kOff_Sub_Holder, &holder)) return 0;
            return HolderLooksValid(holder) ? holder : 0;
        }

        // Durable path: core global -> +0x30 -> +0x50 = container, then the
        // holder. Works from load; live-confirmed to resolve the same container
        // the hook captures.
        uintptr_t ResolveHolderByWalk()
        {
            if (!g_coreGlobal) return 0;
            uintptr_t g = 0, mid = 0, container = 0;
            if (!ReadPtr(g_coreGlobal, &g) || g < kMinPointer) return 0;
            if (!ReadPtr(g + kOff_Global_Mid, &mid) || mid < kMinPointer) return 0;
            if (!ReadPtr(mid + kOff_Mid_Container, &container) || container < kMinPointer) return 0;
            return HolderForContainer(container);
        }

        // The client inventory CONTAINER (one step short of the holder): core
        // global -> +0x30 -> +0x50. Used to tell the client container apart
        // from the server one in the holder-insert hook.
        uintptr_t ResolveClientContainer()
        {
            if (!g_coreGlobal) return 0;
            uintptr_t g = 0, mid = 0, container = 0;
            if (!ReadPtr(g_coreGlobal, &g) || g < kMinPointer) return 0;
            if (!ReadPtr(g + kOff_Global_Mid, &mid) || mid < kMinPointer) return 0;
            if (!ReadPtr(mid + kOff_Mid_Container, &container) || container < kMinPointer) return 0;
            return container;
        }

        uintptr_t CurrentHolder(); // defined below; used by ServerHolder()
        bool ApplySlotCapToHolder(uintptr_t holder, bool enable, uint16_t value);

        // Bucket count of a holder, or 0 if it does not read back sanely. Used
        // to tell our own server mirror apart from some other container that
        // also passes through commit (a merchant's stock, a loot pile).
        uint32_t HolderBucketCount(uintptr_t holder)
        {
            if (!HolderLooksValid(holder)) return 0;
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return 0;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount)) return 0;
            if (buckets < kMinPointer || bcount == 0 || bcount > 4096) return 0;
            return bcount;
        }

        // --- The client/server realm flag (see kTls_RealmFlag) ---------------
        // Deliberately NOT using mem::ReadPtr/Read8/Write8 here: those reject
        // every address below kMinPointer (0x10000000), and the TEB and the
        // engine's TLS block both live far below it - live-observed TEB
        // 0x246000, TLS block 0x1633060, flag 0x1633252. That range floor is a
        // game-heap sanity check and has no business being applied to thread
        // storage; assuming otherwise silently broke the same lookup once
        // already. These are guarded reads with no floor. The real safety check
        // is the flag validating as a bool - which is far stronger than any
        // address-range heuristic, and matters because we WRITE this byte.
        using NtQueryInformationThread_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
        NtQueryInformationThread_t oNtQueryInfoThread = nullptr;

        bool RawReadPtr(uintptr_t addr, uintptr_t* out)
        {
            if (!addr) return false;
            __try { *out = *reinterpret_cast<volatile uintptr_t*>(addr); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool RawRead8(uintptr_t addr, uint8_t* out)
        {
            if (!addr) return false;
            __try { *out = *reinterpret_cast<volatile uint8_t*>(addr); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool RawWrite8(uintptr_t addr, uint8_t val)
        {
            if (!addr) return false;
            __try { *reinterpret_cast<volatile uint8_t*>(addr) = val; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Address of the CALLING thread's realm flag, or 0 if anything about
        // the chain looks wrong. Per-thread by definition - never cache it: the
        // game-thread pump can fire on a thread that is already in the server
        // realm, and each thread has its own flag at its own address.
        // outVal (optional) reports what was actually read at the flag address
        // even when this fails: 0xFF = never read (the chain broke earlier),
        // anything else is the raw byte. Purely diagnostic - the silent add
        // failures needed a way to tell "no TLS chain" from "byte not a bool".
        uintptr_t RealmFlagAddr(uint8_t* outVal = nullptr)
        {
            if (outVal) *outVal = 0xFF;
            if (!oNtQueryInfoThread) return 0;
            // THREAD_BASIC_INFORMATION (48 bytes): TebBaseAddress at +0x08.
            uint8_t tbi[48] = {};
            if (oNtQueryInfoThread(GetCurrentThread(), 0 /*ThreadBasicInformation*/,
                                   tbi, sizeof(tbi), nullptr) < 0)
                return 0;
            uintptr_t teb = 0;
            memcpy(&teb, tbi + 8, sizeof(teb));
            if (!teb) return 0;
            uintptr_t tlsArray = 0, tls = 0;
            if (!RawReadPtr(teb + kOff_Teb_TlsPointer, &tlsArray) || !tlsArray) return 0;
            if (!RawReadPtr(tlsArray, &tls) || !tls) return 0;
            const uintptr_t addr = tls + kTls_RealmFlag;
            uint8_t v = 0;
            if (!RawRead8(addr, &v)) return 0;
            if (outVal) *outVal = v;
            if (v > 1) return 0; // not a bool -> wrong chain, fail closed
            return addr;
        }

        // Is this container a LIVE player character - i.e. the real store, and
        // not a copy of it?
        //
        // This test is load-bearing, and a bucket-count match is NOT enough.
        // Live evidence (2026-07-15): the commit hook does not only see other
        // ENTITIES' inventories - it also sees the insert planner's short-lived
        // DEEP COPIES of the player's own bucket array. Those copies mirror the
        // player ~99%, carry the same type tag, and match on bucket count, so
        // every content-based test waves them through; they are then freed, and
        // reading one later faults (that is exactly what took the in-game editor
        // down - a guarded read of a dead holder+0x18).
        //
        // The engine's own local-player test settles it. Every copy shares the
        // ORIGINAL's possessor pointer, and a possessor can only point back at
        // one character, so only the live one satisfies:
        //     *(*(c + 0xA0) + 0xD0) == c
        // Copies fail it, churned-away characters fail it (the client character
        // reallocates every few seconds - the same SelfPlayer churn god-mode
        // hit), and other entities fail it. It is self-validating: a wrong
        // offset resolves to nothing rather than to a plausible wrong object.
        bool IsLiveCharacter(uintptr_t c)
        {
            if (c < kMinPointer) return false;
            uintptr_t possessor = 0;
            if (!ReadPtr(c + kOff_Owner_Possessor, &possessor) || possessor < kMinPointer) return false;
            uintptr_t pawn = 0;
            if (!ReadPtr(possessor + kOff_Possessor_Pawn, &pawn)) return false;
            return pawn == c;
        }

        // A coherent copy of the capture list. Taken under the lock so a reader
        // can never pair one candidate's container with another's holder while
        // the game thread compacts the array.
        int SnapshotCandidates(Candidate* out)
        {
            if (!g_candLockInit) return 0;
            EnterCriticalSection(&g_candLock);
            const int n = g_candCount.load(std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) out[i] = g_cand[i];
            LeaveCriticalSection(&g_candLock);
            return n;
        }

        // The SERVER-authority holder, resolved from the blind capture list.
        // Deferred to read time on purpose: only now is the client container
        // resolvable, so only now can we say which candidate is NOT it.
        //
        // Live-proven shape (2026-07-15): load-time commit yields two server
        // containers (arena+0xF0200 and +0xF0500) that getHolder() to the SAME
        // holder, plus the client container. A candidate must be a LIVE player
        // character (see IsLiveCharacter) AND mirror the client's bucket count -
        // the first rejects the planner's copies and anything stale, the second
        // rejects a merchant's container passing through commit.
        //
        // Nothing here discards captures when the client container changes,
        // which is tempting and is a trap: loading a save commits the server
        // containers BEFORE the new client container exists, so "the client
        // changed, drop what we gathered against the old one" deletes the new
        // save's server capture moments after taking it - and no further commit
        // fires without a pickup/drop, so the editor stays locked for the rest
        // of the session. That was the reload bug. Staleness is handled by
        // judging each candidate on its own merits below instead: a churned-away
        // client, a freed container and a planner copy all fail IsLiveCharacter,
        // which is what the test is for.
        uintptr_t ServerHolder()
        {
            const uintptr_t clientC = ResolveClientContainer();
            const uintptr_t clientH = CurrentHolder();
            if (!clientC || !clientH) return 0;
            const uint32_t want = HolderBucketCount(clientH);
            if (!want) return 0;

            const ULONGLONG now     = GetTickCount64();
            const uintptr_t cached  = g_serverHolder.load(std::memory_order_acquire);
            // 1. Fast path: if cached holder is valid and mirrors client buckets
            if (cached && cached != clientH && HolderLooksValid(cached) && HolderBucketCount(cached) == want)
            {
                g_serverTick.store(now, std::memory_order_relaxed);
                return cached;
            }

            // 2. Scan candidate list captured from engine commits
            Candidate snap[kMaxCandidates] = {};
            const int n = SnapshotCandidates(snap);
            for (int i = 0; i < n; ++i)
            {
                const uintptr_t c = snap[i].container;
                if (!c || c == clientC) continue;

                uintptr_t h = HolderForContainer(c);
                if (!h) h = snap[i].holder; // walk did not apply; captured pair is all we have
                if (!h || h == clientH) continue;
                if (!HolderLooksValid(h)) continue;
                if (HolderBucketCount(h) != want) continue; // not a mirror of ours
                g_serverHolder.store(h, std::memory_order_release);
                g_serverContainer.store(c, std::memory_order_release);
                g_serverTick.store(now, std::memory_order_relaxed);
                return h;
            }

            // 3. Fallback: if cached holder is still valid in memory with matching bucket count, keep it!
            if (cached && cached != clientH && HolderLooksValid(cached) && HolderBucketCount(cached) == want)
            {
                return cached;
            }

            // Nothing usable yet
            g_serverHolder.store(0, std::memory_order_release);
            g_serverContainer.store(0, std::memory_order_release);
            return 0;
        }

        // The one place holder state is read: the durable walk is the source of
        // truth, and the hook-published holder is only a fallback for when the
        // core global could not be resolved at Install.
        //
        // The walk leads, rather than the cached hook value, because the cache
        // cannot be checked for staleness: loading a save frees the old holder,
        // and freed memory goes on reading back as a structurally sane bucket
        // array, so HolderLooksValid waves the corpse through and the editor
        // spends the session pointed at the previous save's inventory. The walk
        // starts from a global the engine itself repoints on load, so it cannot
        // be stale. It is five guarded reads - cheaper than the mistake.
        uintptr_t CurrentHolder()
        {
            const uintptr_t walked = ResolveHolderByWalk();
            if (walked)
            {
                g_holder.store(walked, std::memory_order_release);
                return walked;
            }
            const uintptr_t h = g_holder.load(std::memory_order_relaxed);
            return HolderLooksValid(h) ? h : 0;
        }

        // --- The hook: capture container + holder on the game thread --------
        int64_t __fastcall hkGetItemQty(void* container, uint16_t typeId, void* keyPtr)
        {
            if (!oGetItemQty) return 0;
            if (container && oGetHolder)
            {
                const ULONGLONG now = GetTickCount64();
                if (g_holder.load(std::memory_order_relaxed) < kMinPointer ||
                    now - g_holderTick.load(std::memory_order_relaxed) > 500)
                {
                    g_holderTick.store(now, std::memory_order_relaxed);
                    void* h = nullptr;
                    __try { h = oGetHolder(container); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { h = nullptr; }
                    if (reinterpret_cast<uintptr_t>(h) >= kMinPointer)
                        g_holder.store(reinterpret_cast<uintptr_t>(h), std::memory_order_release);
                }
            }
            __try
            {
                return oGetItemQty(container, typeId, keyPtr);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
        }

        // --- Blind container capture (game thread) --------------------------

        // Drop candidates whose container is provably dead, compacting what is
        // left. Every load frees the containers of the one before it, so without
        // this the list fills with corpses after a few reloads and the capture
        // that would have unlocked the editor has nowhere to go. Call with
        // g_candLock held; touches nothing but guarded reads.
        void PruneDeadCandidates()
        {
            const ULONGLONG now = GetTickCount64();
            const int cnt = g_candCount.load(std::memory_order_relaxed);
            int keep = 0;
            for (int i = 0; i < cnt; ++i)
            {
                if (now - g_cand[i].tick > kCandGraceMs && !HolderLooksValid(g_cand[i].holder))
                    continue;
                g_cand[keep++] = g_cand[i];
            }
            for (int i = keep; i < cnt; ++i) g_cand[i] = Candidate{};
            g_candCount.store(keep, std::memory_order_release);
        }

        void NoteContainer(void* container)
        {
            const uintptr_t c = reinterpret_cast<uintptr_t>(container);
            if (c < kMinPointer || !oGetHolder || !g_candLockInit) return;

            // Resolving a container mid-construction can fault - never let that
            // take the process down (this runs during load, by definition).
            void* h = nullptr;
            __try { h = oGetHolder(container); }
            __except (EXCEPTION_EXECUTE_HANDLER) { h = nullptr; }
            if (reinterpret_cast<uintptr_t>(h) < kMinPointer) return;

            const ULONGLONG now = GetTickCount64();
            EnterCriticalSection(&g_candLock);
            int cnt = g_candCount.load(std::memory_order_relaxed);
            int at  = -1;
            for (int i = 0; i < cnt; ++i)
                if (g_cand[i].container == c) { at = i; break; }
            if (at < 0 && cnt >= kMaxCandidates)
            {
                PruneDeadCandidates(); // corpses from earlier loads; indices shift
                cnt = g_candCount.load(std::memory_order_relaxed);
            }
            if (at < 0 && cnt < kMaxCandidates) at = cnt;
            if (at >= 0)
            {
                g_cand[at].container = c;
                g_cand[at].holder    = reinterpret_cast<uintptr_t>(h);
                g_cand[at].tick      = now;
                if (at >= cnt) g_candCount.store(at + 1, std::memory_order_release); // publish last
            }
            LeaveCriticalSection(&g_candLock);
        }

        // --- The commit hook: where the server container shows up at load ---
        void* __fastcall hkCommit(void* holder, void* err, void* container, void* items,
                                  void* out, uint8_t a6, uint8_t a7)
        {
            if (!oCommit) return nullptr;
            __try { NoteContainer(container); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            g_commitActive.store(true, std::memory_order_release);
            void* ret = oCommit(holder, err, container, items, out, a6, a7);
            g_commitActive.store(false, std::memory_order_release);
            return ret;
        }

        // --- The holder-insert hook: second capture path ---------------------
        void* __fastcall hkHolderInsert(void* bucket, void* err, void* container, void* itemArr,
                                        uint16_t a5, void* a6, uint8_t a7, uint8_t a8, uint8_t a9)
        {
            if (!oHolderInsert) return nullptr;
            __try { NoteContainer(container); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            g_commitActive.store(true, std::memory_order_release);
            void* ret = oHolderInsert(bucket, err, container, itemArr, a5, a6, a7, a8, a9);
            g_commitActive.store(false, std::memory_order_release);
            return ret;
        }

        // --- The expansion-setter hook: make the engine's re-stamps OURS -----
        bool OverrideExpandForType(uint16_t type, int value, uint16_t* out,
                                   uint16_t* outDef = nullptr)
        {
            uint16_t defSlots = 0, maxSlots = 0;
            if (!StorageSlotsForType(type, &defSlots, &maxSlots)) return false;
            if (value < 1) value = 1;
            
            // In Pearl Abyss engine, if expansion exceeds (maxSlots - defSlots),
            // the transaction validator rejects the transaction with eErrNoTryOverExpandInventorySlot
            // (Error 298648703 / 0x11CD047F).
            // - On vanilla: maxSlots in table is 240.
            // Dynamically clamp value to live table maxSlots (hard ceiling 700).
            const uint16_t safeMax = (maxSlots > 0 && maxSlots <= 700) ? maxSlots : 240;
            if (value > safeMax)
                value = safeMax;

            *out = (value > defSlots) ? static_cast<uint16_t>(value - defSlots) : 0;
            if (outDef) *outDef = defSlots;
            return true;
        }

        // The bucket this setter call will land on - resolved the same way
        // the setter itself does (bucket+0x10 == type), so the capture is
        // keyed by the exact bucket the write hits.
        uintptr_t BucketByType(uintptr_t holder, uint16_t type)
        {
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) || buckets < kMinPointer) return 0;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount == 0 || bcount > 4096) return 0;
            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;
                uint16_t t = 0;
                if (Read16(bucket + kOff_InvBucket_Type, &t) && t == type) return bucket;
            }
            return 0;
        }

        void* __fastcall hkSetExpandSlots(void* holder, int* outErr, void* a3,
                                          uint16_t type, uint16_t count)
        {
            if (!oSetExpandSlots) return nullptr;
            void* ret = oSetExpandSlots(holder, outErr, a3, type, count);
            __try
            {
                const State& st = State::Get();
                if (st.invSlotSize && Player::Ready() && !g_commitActive.load(std::memory_order_acquire))
                {
                    uint16_t expand = 0;
                    uint16_t defSlots = 0;
                    if (OverrideExpandForType(type, st.invSlotSizeVal, &expand, &defSlots))
                    {
                        const uintptr_t bucket = BucketByType(reinterpret_cast<uintptr_t>(holder), type);
                        if (bucket)
                        {
                            UpsertOrigExpand(bucket, type, count);
                            Write16(bucket + kOff_InvBucket_ExpandSlots, expand);
                            Write16(bucket + kOff_InvBucket_MaxSlots, static_cast<uint16_t>(defSlots + expand));
                        }
                    }
                    else
                    {
                        const uintptr_t bucket = BucketByType(reinterpret_cast<uintptr_t>(holder), type);
                        if (bucket)
                        {
                            uint16_t curCap = 0, curExpand = 0;
                            if (Read16(bucket + kOff_InvBucket_MaxSlots, &curCap))
                            {
                                Read16(bucket + kOff_InvBucket_ExpandSlots, &curExpand);
                                defSlots = (curCap >= curExpand) ? static_cast<uint16_t>(curCap - curExpand) : 0;
                                uint16_t targetCap = static_cast<uint16_t>(st.invSlotSizeVal);
                                if (targetCap > 700) targetCap = 700;
                                expand   = (static_cast<int>(targetCap) > defSlots)
                                           ? static_cast<uint16_t>(targetCap - defSlots) : 0;
                                UpsertOrigExpand(bucket, type, count);
                                Write16(bucket + kOff_InvBucket_ExpandSlots, expand);
                                Write16(bucket + kOff_InvBucket_MaxSlots, static_cast<uint16_t>(defSlots + expand));
                            }
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            return ret;
        }

        // --- Used-count repair ------------------------------------------------
        // bucket+0x12 ("slots in use") is an INCREMENTAL accumulator the engine
        // maintains as ceil(quantity/stackMax) deltas inside its own add/remove
        // paths - and our quantity editor and Remove write around those paths
        // by design. A mega-stack quantity makes the discrepancy catastrophic
        // rather than cosmetic: loading a save rebuilds every bucket by pushing
        // each saved stack through that ceil math, so ONE edited stack of
        // 999999 against a vanilla stack max of 50 books 20000 "used" slots.
        // Live-caught 2026-07-15: storage 1 read used=27445 with cap=2000, at
        // which point the insert planner and the pickup free-space gate refuse
        // everything - "inventory full" beside a screen of empty slots, and
        // locked slots in the grouped UI, which derive from the same counter.
        //
        // The repair clamps used DOWN to the bucket's physical occupancy
        // (slots holding a real type with a positive quantity). In any state
        // the engine produced on its own the two are identical - the game
        // splits stacks at stackMax, so every occupied slot books exactly 1 -
        // which makes this a strict no-op on untouched inventories. Never
        // scales up: an undercount cannot refuse a pickup. The engine goes on
        // applying its own deltas on top of what we leave here, and any
        // re-drift (topping up a mega-stack still books a ceil-sized delta)
        // is re-clamped by the next pass.
        void RepairUsedSlots(uintptr_t holder)
        {
            if (!HolderLooksValid(holder)) return;
            // NEVER touch container metadata while a transaction is in flight.
            // Quest/trade/buy commits validate their own metadata checksums;
            // if we write used/cap mid-commit the validator sees a mismatch
            // and throws Error 298648703.
            if (g_commitActive.load(std::memory_order_acquire)) return;

            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount == 0 || bcount > 4096) return;

            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;
                uint16_t type = 0, used = 0;
                if (!Read16(bucket + kOff_InvBucket_Type, &type) || type == kInvSlot_EmptyType) continue;
                if (!Read16(bucket + kOff_InvBucket_UsedSlots, &used) || used == 0) continue;

                uintptr_t slots  = 0;
                uint16_t  scount = 0;
                if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
                if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount > 4096) continue;

                uint16_t occ = 0;
                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t slot = slots + static_cast<uintptr_t>(i) * SlotStride();
                    uint16_t tid = 0;
                    int64_t  qty = 0;
                    if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                    if (!Read64(slot + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;
                    ++occ;
                }

                if (occ < used)
                    Write16(bucket + kOff_InvBucket_UsedSlots, occ);
            }
        }

        // --- "*info" table resolvers (multi-anchor with semantic validation) -------
        // Validates candidate table singleton pointers in runtime memory:
        // checks non-zero row count, validity of defs array at +0x58 or +0x50.
        bool ValidateTableGlobal(uintptr_t tableGlobal, const char* expectedName = nullptr)
        {
            if (tableGlobal < kMinPointer) return false;
            uintptr_t table = 0;
            if (!ReadPtr(tableGlobal, &table) || table < kMinPointer) return false;
            uint32_t count = 0;
            if (!Read32(table + kOff_ItemTable_Count, &count) || count == 0 || count > 500000) return false;

            uintptr_t defs = 0;
            // Check modern +0x58 (TU 1.17 - 1.18+) or legacy +0x50 (TU 1.10 - 1.16)
            if (!ReadPtr(table + 0x58, &defs) || defs < kMinPointer)
            {
                if (!ReadPtr(table + 0x50, &defs) || defs < kMinPointer)
                    return false;
            }
            return true;
        }

        // The 16-bit-key table-resolver clone prologue (TU 1.10 - 1.15 legacy), ending at the
        // Universal Item Table Resolver Clone Prologue Matcher
        // Matches 1.14 (0x40 frame) and 1.18 (0x50 frame) table resolver clone prologues
        uintptr_t FindItemPrologueAbove(uintptr_t match)
        {
            for (size_t back = 0x15; back <= 0x80; ++back)
            {
                const uintptr_t cand = match - back;
                __try
                {
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(cand);
                    // 48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC (40 or 50)
                    if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x5C && p[3] == 0x24 && p[4] == 0x10 &&
                        p[5] == 0x48 && p[6] == 0x89 && p[7] == 0x6C && p[8] == 0x24 && p[9] == 0x18 &&
                        p[10] == 0x56 && p[11] == 0x57 && p[12] == 0x41 && p[13] == 0x56 &&
                        p[14] == 0x48 && p[15] == 0x83 && p[16] == 0xEC &&
                        (p[17] == 0x40 || p[17] == 0x50))
                    {
                        return cand;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return 0;
        }

        struct TableHunt
        {
            const char* name;
            bool indirect;
            uintptr_t fn;
        };

        bool IsTableRef(uintptr_t match, void* ctx)
        {
            auto* h = static_cast<TableHunt*>(ctx);
            uintptr_t target = mem::ResolveRipAt(match, 7);
            if (h->indirect && (!ReadPtr(target, &target) || target < kMinPointer)) return false;
            char buf[64]{ 0 };
            if (!ReadCString(target, buf, sizeof(buf))) return false;
            if (_stricmp(buf, h->name) != 0) return false;

            const uintptr_t fn = FindItemPrologueAbove(match);
            if (fn)
            {
                h->fn = fn;
                return true;
            }
            return false;
        }

        uintptr_t FindTableGlobal(const char* name, bool indirect = false)
        {
            TableHunt hunt{ name, indirect, 0 };
            mem::FindPatternIf(indirect ? kSig_MovR8Rip : kSig_LeaR8Rip, &IsTableRef, &hunt);
            if (hunt.fn)
            {
                uintptr_t g = mem::ResolveRipAt(hunt.fn + kOff_ItemResolver_MovGlobal, 7);
                if (g >= kMinPointer)
                {
                    LOG_OK("inventory: table '%s' resolved via string-anchor -> %p",
                           name, reinterpret_cast<void*>(g));
                    return g;
                }
            }
            return 0;
        }

        void EnsureTablesResolved()
        {
            if (!g_itemTableGlobal)
                g_itemTableGlobal = FindTableGlobal(kStr_ItemInfoTable);
            if (!g_grpTableGlobal)
                g_grpTableGlobal = FindTableGlobal(kStr_ItemGroupInfoTable);
            if (!g_strTableGlobal)
                g_strTableGlobal = FindTableGlobal(kStr_StringInfoTable);
            if (!g_invTableGlobal)
                g_invTableGlobal = FindTableGlobal(kStr_InventoryInfoTable, /*indirect=*/true);
            if (!g_locMgrGlobal)
            {
                uintptr_t locGet = mem::FindPattern(kSig_LocStringGet);
                if (!locGet) locGet = mem::FindPattern(kSig_LocStringGet_Alt1);
                if (!locGet) locGet = mem::FindPattern(kSig_LocStringGet_Alt2);
                if (!locGet) locGet = mem::FindPattern(kSig_LocStringGet_Legacy);
                if (locGet)
                {
                    for (uintptr_t p = locGet; p + 7 <= locGet + 0x30; ++p)
                    {
                        const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
                        if (b[0] == 0x48 && b[1] == 0x8B && b[2] == 0x05)
                        {
                            uintptr_t g = mem::ResolveRipAt(p, 7);
                            if (g >= kMinPointer)
                            {
                                g_locMgrGlobal = g;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    bool Inventory::Install()
    {
        if (!mem::InstallHook("inventory: item-count accessor", kSig_InvGetItemQty, "inventory disabled",
                              &hkGetItemQty, &oGetItemQty, &g_qtyTarget, 4))
            return false;

        const uintptr_t holderAddr = mem::FindPattern(kSig_InvGetHolder);
        if (!holderAddr)
        {
            LOG_ERR("inventory: holder resolver signature NOT FOUND - inventory disabled.");
            return false;
        }
        oGetHolder = reinterpret_cast<GetHolder_t>(holderAddr);

        // --- Add-item primitives (all optional: without any one of them Add
        // Item is refused, and every other inventory feature still works).
        // These are CALLED, not hooked. The insert planner is oHolderInsert,
        // resolved by the hook above - same function.
        static const char* kCtorSigs[] = {
            kSig_TrItemValueCtor,
            "48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC ? 4C 8B",
            "48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC",
            "48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC",
            "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 48 8B 09",
            "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC",
        };
        for (const char* sig : kCtorSigs)
        {
            const uintptr_t addr = mem::FindPattern(sig);
            const size_t matches = mem::CountMatches(sig, 4);
            if (addr && (matches == 1 || matches == 2))
            {
                oItemValueCtor = reinterpret_cast<ItemValueCtor_t>(addr);
                LOG_OK("inventory: TrItemValue native ctor resolved at %p (matches=%zu)", reinterpret_cast<void*>(addr), matches);
                break;
            }
        }
        if (!oItemValueCtor)
        {
            LOG("inventory: using synthetic TrItemValue constructor for cross-version compatibility.");
        }

        const uintptr_t commitAddr = mem::FindPattern(kSig_InvCommitPlacement);
        const uintptr_t freeAddr   = mem::FindPattern(kSig_InvFreePlacements);
        const uintptr_t dtorAddr   = mem::FindPattern(kSig_TrItemValueDtor);

        if (commitAddr) oCommitPlacement = reinterpret_cast<CommitPlacement_t>(commitAddr);
        if (freeAddr)   oFreePlacements  = reinterpret_cast<FreePlacements_t>(freeAddr);
        if (dtorAddr)   oItemValueDtor   = reinterpret_cast<ItemValueDtor_t>(dtorAddr);
        // The TEB lookup for the realm flag. Deliberately NtQueryInformationThread
        // rather than a hand-rolled `mov rax, gs:[30h]` stub: that was tried and
        // fails (bogus TEB, then an access violation on the second call - almost
        // certainly CFG rejecting an indirect call into our own page).
        if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
            oNtQueryInfoThread = reinterpret_cast<NtQueryInformationThread_t>(
                GetProcAddress(ntdll, "NtQueryInformationThread"));
        if (!oCommitPlacement || !oFreePlacements || !oNtQueryInfoThread)
            LOG_WARN("inventory: add-item path incomplete (commit=%d free=%d teb=%d)"
                     " - Add Item will be refused.",
                     oCommitPlacement ? 1 : 0, oFreePlacements ? 1 : 0,
                     oNtQueryInfoThread ? 1 : 0);

        if (!g_candLockInit)
        {
            InitializeCriticalSection(&g_candLock);
            g_candLockInit = true;
        }
        if (!g_capLockInit)
        {
            InitializeCriticalSection(&g_capLock);
            g_capLockInit = true;
        }

        // The game's own slot-expansion setter, HOOKED rather than just
        // resolved: the engine re-stamps every storage's VANILLA expansion
        // through it on ordinary inventory events, in both realms (see
        // hkSetExpandSlots) - substituting the count inside those re-stamps
        // is what makes Slot Size stable. Installed after the capture lock it
        // uses. Our own applies/restores go through the trampoline. If the
        // hook cannot be installed but the address resolves, fall back to
        // call-only: the toggle still applies from Tick(), it just re-fights
        // the engine's stamps (the old, racy behaviour).
        if (!mem::InstallHook("inventory: slot-expansion setter", kSig_InvSetExpandSlots,
                              "Slot Size will not apply",
                              &hkSetExpandSlots, &oSetExpandSlots, &g_expandTarget, 4))
        {
            const uintptr_t expandAddr = mem::FindPattern(kSig_InvSetExpandSlots);
            if (expandAddr)
            {
                oSetExpandSlots = reinterpret_cast<SetExpandSlots_t>(expandAddr);
                LOG_WARN("inventory: slot-expansion setter hook failed - Slot Size applies "
                         "call-only and may briefly revert when the game recomputes it.");
            }
        }

        // Capture-hook the transaction COMMIT: this is what makes edits persist
        // with no player action. Loading a save commits the server-authority
        // containers before the client one exists, so this hook sees the server
        // holder seconds after load - no pickup/drop needed. Must be installed
        // before the save loads, which an ASI at process start always is.
        // Optional: without it, edits still apply to the client mirror but the
        // reconcile reverts them (the menu still lists/reads fine).
        mem::InstallHook("inventory: transaction commit", kSig_InvCommit,
                         "quantity edits will not persist (revert on reconcile)",
                         &hkCommit, &oCommit, &g_commitTarget, 4);

        // Secondary capture path: fires on a real add/drop/buy, not at load.
        // Catches containers that only appear later (e.g. character swap).
        if (!mem::InstallHook("inventory: holder-insert", kSig_InvHolderInsert, nullptr,
                              &hkHolderInsert, &oHolderInsert, &g_insTarget, 2))
        {
            mem::InstallHook("inventory: holder-insert legacy", kSig_InvHolderInsert_Legacy,
                             "server holder relies on the commit hook alone",
                             &hkHolderInsert, &oHolderInsert, &g_insTarget, 2);
        }

        // Durable container walk (optional but preferred - without it the
        // list only appears once the game happens to query an item count,
        // which is hit-or-miss at load).
        const uintptr_t globAnchor = mem::FindPattern(kSig_InvCoreGlobal);
        if (globAnchor)
            g_coreGlobal = mem::ResolveRipAt(globAnchor + kOff_InvCoreGlobal_Mov, 7);

        // Item defs (optional - resolved lazily when inventory is opened).
        g_itemTableGlobal = FindTableGlobal(kStr_ItemInfoTable);

        // The category tree (optional - without it everything lands in one group).
        g_grpTableGlobal = FindTableGlobal(kStr_ItemGroupInfoTable);

        // Icon sprite names (optional - for item sprite rendering).
        g_strTableGlobal = FindTableGlobal(kStr_StringInfoTable);

        // Storage names (optional - labelled by engine key or custom text).
        g_invTableGlobal = FindTableGlobal(kStr_InventoryInfoTable, /*indirect=*/true);

        // Real localised names (optional - falls back to prettified keys).
        const uintptr_t locGet = mem::FindPattern(kSig_LocStringGet);
        if (locGet)
            g_locMgrGlobal = mem::ResolveRipAt(locGet + kOff_LocGet_MovGlobal, 7);

        return true;
    }

    void Inventory::Remove()
    {
        // Leave the tables as vanilla found them on unload, same as World does
        // for Game Speed.
        if (g_stackApplied) { SetAllMaxStackSizes(false, 0); g_stackApplied = false; }
        if (g_slotApplied)  { SetAllSlotSizes(false, 0);     g_slotApplied  = false; }

        mem::RemoveHook(&g_qtyTarget);
        mem::RemoveHook(&g_insTarget);
        mem::RemoveHook(&g_commitTarget);
        mem::RemoveHook(&g_expandTarget); // after the restore above, which
                                          // still calls its trampoline
        g_holder.store(0);
        g_serverHolder.store(0);
        g_serverContainer.store(0);
        g_candCount.store(0);
        g_storages.clear();
    }

    bool Inventory::Ready()
    {
        return CurrentHolder() != 0;
    }

    static void RefreshImplCore(bool force)
    {
        const ULONGLONG now = GetTickCount64();
        if (!force && now - g_lastRefresh < 120) return; // ~8 Hz is plenty for a menu
        g_lastRefresh = now;

        g_storages.clear();

        const uintptr_t holder = CurrentHolder();
        if (!holder) return;

        uintptr_t buckets = 0;
        uint32_t  bcount  = 0;
        if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return;
        if (!Read32(holder + kOff_InvHolder_Count, &bcount)) return;
        if (buckets < kMinPointer || bcount == 0 || bcount > 4096) return;

        // One bucket = one storage (see offsets.h).
        for (uint32_t b = 0; b < bcount; ++b)
        {
            uintptr_t bucket = 0;
            if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;

            uint16_t stype = 0;
            if (!Read16(bucket + kOff_InvBucket_Type, &stype)) continue;

            uintptr_t slots = 0;
            uint16_t  scount = 0;
            if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
            if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount == 0 || scount > 8192) continue;

            Storage store{};
            store.type = stype;
            // Name: the game's own localised text first, then the engine key
            // made readable, then the bare type. Each fallback is one step less
            // informative but never wrong, and never blank. Where that name
            // turns out to be ambiguous (or never resolved), the pass after the
            // walk substitutes ours - it can only tell once every storage is in.
            if (!StorageKeyForType(stype, store.key, sizeof(store.key)))
                store.key[0] = 0;
            store.rank      = store.key[0] ? StorageRank(store.key) : kStorageStyleCount;
            store.gameNamed = StorageNameForType(stype, store.name, sizeof(store.name));
            if (!store.gameNamed)
            {
                if (store.key[0])
                    Prettify(store.key, store.name, sizeof(store.name));
                else
                    snprintf(store.name, sizeof(store.name), "Storage #%u", stype);
            }
            store.haveSlots = StorageSlotsForType(stype, &store.defSlots, &store.maxSlots);

            for (uint16_t i = 0; i < scount; ++i)
            {
                const uintptr_t slot = slots + static_cast<uintptr_t>(i) * SlotStride();
                uint16_t tid = 0;
                int64_t  qty = 0;
                if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                if (!Read64(slot + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;

                Item it{};
                it.slot      = slot;
                it.typeId    = tid;
                it.qty       = qty;
                it.bucketIdx = b;
                it.slotIdx   = i;
                it.tier      = TierOfType(tid);

                // Name: the game's own localised text first, then the engine
                // key prettified, then a bare id. Each fallback is one step
                // less informative but never wrong.
                if (!DisplayNameForType(tid, it.name, sizeof(it.name)))
                {
                    if (KeyForType(tid, it.key, sizeof(it.key)))
                        Prettify(it.key, it.name, sizeof(it.name));
                    else
                        snprintf(it.name, sizeof(it.name), "Item #%u", tid);
                }
                if (!it.key[0] && !KeyForType(tid, it.key, sizeof(it.key)))
                    it.key[0] = 0;
                if (!IconForType(tid, it.icon, sizeof(it.icon)))
                    it.icon[0] = 0; // no icon is normal (contribution tokens, ...)

                if (!CategoryOfType(tid, &it.cat))
                    it.cat = kNoCategory; // category tree unreadable for this item

                Group* g = nullptr;
                for (auto& cand : store.groups)
                    if (cand.cat.row == it.cat.row) { g = &cand; break; }
                if (!g)
                {
                    Group ng{};
                    ng.cat = it.cat;
                    if (!GroupName(it.cat.row, ng.label, sizeof(ng.label)))
                        snprintf(ng.label, sizeof(ng.label), "Uncategorised");
                    if (it.cat.tabRow == 0xFFFF ||
                        !GroupName(it.cat.tabRow, ng.tab, sizeof(ng.tab)))
                        ng.tab[0] = 0; // no top tab: legitimate, e.g. Currency items
                    if (!IconForGroup(it.cat.row, ng.icon, sizeof(ng.icon)))
                        snprintf(ng.icon, sizeof(ng.icon), "%s", kIcon_Uncategorised);
                    store.groups.push_back(std::move(ng));
                    g = &store.groups.back();
                }
                g->items.push_back(it);
            }

            if (store.groups.empty()) continue; // storage holds nothing - don't list it

            for (auto& g : store.groups)
                std::sort(g.items.begin(), g.items.end(), [](const Item& a, const Item& b) {
                    return _stricmp(a.name, b.name) < 0;
                });

            // The game's own tab order, straight out of _orderIndex.
            std::sort(store.groups.begin(), store.groups.end(), [](const Group& a, const Group& b) {
                if (a.cat.tabOrder != b.cat.tabOrder) return a.cat.tabOrder < b.cat.tabOrder;
                return a.cat.order < b.cat.order;
            });

            g_storages.push_back(std::move(store));
        }

        // Curated order (kStorageStyle), then by type so unlisted storages still
        // have a stable order among themselves rather than one inherited from
        // bucket layout.
        std::sort(g_storages.begin(), g_storages.end(), [](const Storage& a, const Storage& b) {
            if (a.rank != b.rank) return a.rank < b.rank;
            return a.type < b.type;
        });

        // Now settle the names the game could not. Several storages share one
        // localised name - it calls your pack, your quest items, your camp
        // currency and the warehouse's player-side pane all "Inventory", because
        // in its own screens the surrounding UI says which you are looking at.
        // Names are compared before ANY is rewritten, or renaming the first
        // would stop the rest from looking like duplicates.
        const size_t n = g_storages.size();
        std::vector<bool> ambiguous(n, false);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j)
                if (i != j && strcmp(g_storages[i].name, g_storages[j].name) == 0)
                {
                    ambiguous[i] = true;
                    break;
                }
        for (size_t i = 0; i < n; ++i)
        {
            Storage& s = g_storages[i];
            if (!ambiguous[i] && s.gameNamed) continue; // the game's name is good - keep it
            if (!s.key[0]) continue;                    // nothing to go on; leave the fallback
            if (const char* ours = StorageStyleName(s.key))
            {
                snprintf(s.name, sizeof(s.name), "%s", ours);
                continue;
            }
            if (!ambiguous[i]) continue; // unnamed but unique: the key-derived name will do
            // An ambiguous storage we have no name for (a new one, or a patch
            // that renamed a key): qualify it rather than show a second row that
            // reads identically to another.
            char q[sizeof(s.name)];
            snprintf(q, sizeof(q), "%s (%s)", s.name, s.key);
            snprintf(s.name, sizeof(s.name), "%s", q);
        }
    }

    static void RefreshImpl(bool force)
    {
        if (!Player::Ready()) return;
        __try
        {
            RefreshImplCore(force);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_storages.clear();
        }
    }

    void Inventory::Refresh()      { RefreshImpl(false); }
    void Inventory::ForceRefresh() { RefreshImpl(true); }

    int Inventory::StorageCount() { return static_cast<int>(g_storages.size()); }

    const char* Inventory::StorageName(int st)
    {
        const Storage* s = StorageAt(st);
        return s ? s->name : "";
    }

    const char* Inventory::StorageKey(int st)
    {
        const Storage* s = StorageAt(st);
        return s ? s->key : "";
    }

    bool Inventory::StorageSlots(int st, int* defaultSlots, int* maxSlots)
    {
        const Storage* s = StorageAt(st);
        if (!s || !s->haveSlots) return false;
        if (defaultSlots) *defaultSlots = s->defSlots;
        if (maxSlots)     *maxSlots     = s->maxSlots;
        return true;
    }

    int Inventory::StorageItemCount(int st)
    {
        const Storage* s = StorageAt(st);
        if (!s) return 0;
        int n = 0;
        for (const auto& g : s->groups) n += static_cast<int>(g.items.size());
        return n;
    }

    int Inventory::CategoryCount(int st)
    {
        const Storage* s = StorageAt(st);
        return s ? static_cast<int>(s->groups.size()) : 0;
    }

    const char* Inventory::CategoryName(int st, int cat)
    {
        const Group* g = GroupAt(st, cat);
        return g ? g->label : "";
    }

    int Inventory::ItemCount(int st, int cat)
    {
        const Group* g = GroupAt(st, cat);
        return g ? static_cast<int>(g->items.size()) : 0;
    }

    bool Inventory::GetItem(int st, int cat, int idx, const char** name, int64_t* qty,
                            const char** icon)
    {
        const Item* it = ItemAt(st, cat, idx);
        if (!it) return false;
        if (name) *name = it->name;
        if (qty)  *qty  = it->qty;
        if (icon) *icon = it->icon;
        return true;
    }

    bool Inventory::GetItemInfo(int st, int cat, int idx, ItemInfo* out)
    {
        if (!out) return false;
        const Item* it = ItemAt(st, cat, idx);
        if (!it) return false;
        out->name   = it->name;
        out->key    = it->key;
        out->icon   = it->icon;
        out->qty    = it->qty;
        out->typeId = it->typeId;
        out->tier   = it->tier;
        return true;
    }

    const char* Inventory::CategoryTab(int st, int cat)
    {
        const Group* g = GroupAt(st, cat);
        return g ? g->tab : "";
    }

    const char* Inventory::CategoryIcon(int st, int cat)
    {
        const Group* g = GroupAt(st, cat);
        return g ? g->icon : "";
    }

    bool Inventory::SetAllMaxStackSizes(bool enable, int64_t value)
    {
        if (!g_itemTableGlobal) return false;
        uintptr_t table = 0;
        if (!ReadPtr(g_itemTableGlobal, &table)) return false;
        uint32_t count = 0;
        if (!Read32(table + kOff_ItemTable_Count, &count) || count == 0 || count > 65536) return false;

        const uintptr_t stackOff = kOff_ItemDef_MaxStackCount;    // 0x18 (i64)
        const uintptr_t capOff   = kOff_ItemDef_ApplyMaxStackCap; // 0x111 (u8 bool)

        if (g_stackCaptured.size() != count)
        {
            g_origMaxStack.assign(count, 0);
            g_origApplyCap.assign(count, 0);
            g_stackCaptured.assign(count, false);
        }
        if (value < 1) value = 1;

        bool any = false;
        for (uint32_t row = 0; row < count; ++row)
        {
            uintptr_t def = 0;
            if (!DefForRow(g_itemTableGlobal, static_cast<uint16_t>(row), &def)) continue;

            if (!g_stackCaptured[row])
            {
                int64_t origVal = 0;
                uint8_t origCap = 0;
                Read64(def + stackOff, &origVal);
                Read8(def + capOff, &origCap);
                g_origMaxStack[row]  = origVal;
                g_origApplyCap[row]  = origCap;
                g_stackCaptured[row] = true;
            }

            // Only scale genuinely stackable items (materials, consumables, arrows, pouches).
            // NEVER alter non-stackable items (Weapons, Shields, Armor, Accessories, Quest Flyers/Docs)
            // because forcing applyMaxStackCap=1 on unique items breaks engine item creation and
            // triggers server validation Error 298648703 when receiving quest rewards.
            const bool isStackable = (g_origMaxStack[row] > 1) || (g_origApplyCap[row] == 1);
            if (!isStackable) continue;

            if (enable)
            {
                Write64(def + stackOff, static_cast<uint64_t>(value));
                Write8(def + capOff, 1);
                any = true;
            }
            else if (g_stackCaptured[row])
            {
                Write64(def + stackOff, static_cast<uint64_t>(g_origMaxStack[row]));
                Write8(def + capOff, g_origApplyCap[row]);
                any = true;
            }
        }

        // Automatically consolidate and merge all duplicate stacks into 1 master slot
        if (enable && any)
        {
            ConsolidateAllItems();
        }

        return any;
    }

    bool IsTypeStackable(uint16_t typeId)
    {
        if (typeId < g_origMaxStack.size() && g_stackCaptured[typeId])
            return (g_origMaxStack[typeId] > 1) || (g_origApplyCap[typeId] == 1);

        uintptr_t def = 0;
        if (!DefForRow(g_itemTableGlobal, typeId, &def)) return false;
        int64_t maxS = 0;
        uint8_t cap = 0;
        Read64(def + kOff_ItemDef_MaxStackCount, &maxS);
        Read8(def + kOff_ItemDef_ApplyMaxStackCap, &cap);
        return (maxS > 1) || (cap == 1);
    }

    namespace
    {
        bool g_noCooldownApplied = false;
        std::vector<std::vector<uint16_t>> g_origCooltimes;
        std::vector<int64_t> g_origRespawnTimes;
        bool g_cooltimesCaptured = false;

        uintptr_t g_characterTableGlobal = 0;
        bool g_charCooltimesCaptured = false;
        std::vector<int64_t> g_origCharCooltimes;

        bool SetAllCharacterCooldowns(bool enable)
        {
            if (!g_characterTableGlobal)
                g_characterTableGlobal = FindTableGlobal("characterinfo");
            if (!g_characterTableGlobal) return false;
            uintptr_t table = 0;
            if (!ReadPtr(g_characterTableGlobal, &table)) return false;
            uint32_t count = 0;
            if (!Read32(table + kOff_ItemTable_Count, &count) || count == 0 || count > 65536) return false;

            if (!g_charCooltimesCaptured || g_origCharCooltimes.size() != count)
            {
                g_origCharCooltimes.assign(count, 0);
                for (uint32_t row = 0; row < count; ++row)
                {
                    uintptr_t def = 0;
                    if (!DefForRow(g_characterTableGlobal, static_cast<uint16_t>(row), &def)) continue;
                    int64_t ct = 0;
                    Read64(def + 0x70, &ct);
                    g_origCharCooltimes[row] = ct;
                }
                g_charCooltimesCaptured = true;
            }

            bool any = false;
            for (uint32_t row = 0; row < count; ++row)
            {
                uintptr_t def = 0;
                if (!DefForRow(g_characterTableGlobal, static_cast<uint16_t>(row), &def)) continue;
                const int64_t targetCt = enable ? 0 : g_origCharCooltimes[row];
                if (Write64(def + 0x70, targetCt))
                    any = true;
            }
            return any;
        }

        bool SetAllItemCooldowns(bool enable)
        {
            SetAllCharacterCooldowns(enable);

            if (!g_itemTableGlobal) return false;
            uintptr_t table = 0;
            if (!ReadPtr(g_itemTableGlobal, &table)) return false;
            uint32_t count = 0;
            if (!Read32(table + kOff_ItemTable_Count, &count) || count == 0 || count > 65536) return false;

            if (!g_cooltimesCaptured || g_origCooltimes.size() != count)
            {
                g_origCooltimes.resize(count);
                g_origRespawnTimes.assign(count, 0);
                for (uint32_t row = 0; row < count; ++row)
                {
                    uintptr_t def = 0;
                    if (!DefForRow(g_itemTableGlobal, static_cast<uint16_t>(row), &def)) continue;
                    g_origCooltimes[row].resize(10);
                    for (int k = 0; k < 10; ++k)
                    {
                        uint16_t ct = 0;
                        Read16(def + 0x20C + k * 2, &ct);
                        g_origCooltimes[row][k] = ct;
                    }
                    int64_t respawn = 0;
                    Read64(def + 0x3F0, &respawn);
                    g_origRespawnTimes[row] = respawn;
                }
                g_cooltimesCaptured = true;
            }

            bool any = false;
            for (uint32_t row = 0; row < count; ++row)
            {
                uintptr_t def = 0;
                if (!DefForRow(g_itemTableGlobal, static_cast<uint16_t>(row), &def)) continue;

                for (int k = 0; k < 10; ++k)
                {
                    const uint16_t targetCt = enable ? 0 : g_origCooltimes[row][k];
                    if (Write16(def + 0x20C + k * 2, targetCt))
                        any = true;
                }
                const int64_t targetRespawn = enable ? 0 : g_origRespawnTimes[row];
                Write64(def + 0x3F0, targetRespawn);
            }
            return any;
        }
    }

    namespace
    {
        // Apply (enable=true) or restore (enable=false) the slot cap on every
        // bucket of one holder, by driving the game's OWN expansion setter
        // (kSig_InvSetExpandSlots) rather than writing the cap field. The cap
        // is a derived cache of the expansion count, so poking it is undone by
        // the next expansion sync or slot-expansion buff; the setter maintains
        // every dependent field together. See offsets.h.
        //
        // Shared by both the client and server-authority holders, same
        // dual-write reasoning as the quantity editor: driving only one side
        // risks the per-frame reconcile fighting it back (unconfirmed for this
        // field specifically - needs live verification - but matching the
        // established pattern is the safe default).
        //
        // IDEMPOTENT, and called every tick rather than on change: loading a
        // save frees every bucket and constructs new ones at vanilla caps, so
        // an apply that only ran when the toggle changed silently stopped
        // working after the first load (LIVE-CONFIRMED 2026-07-15: worked on
        // first load, dead on every reload). The same rebuild-from-underneath
        // happens whenever a slot-expansion buff recomputes the cap. So rather
        // than trying to detect a rebuild, every bucket whose cap already
        // matches the target is skipped and the rest are re-driven - which
        // self-heals both cases for one u16 read per bucket per frame.
        //
        // Must run on the game thread (Tick() does).
        // NOTE: We do NOT mutate the InventoryInfo table (_defaultSlotCount /
        // _maxSlotCount). The slot-size feature works purely by driving the
        // engine's OWN expansion setter (kSig_InvSetExpandSlots). Mutating the
        // table was what made OverrideExpandForType compute expand=0 (defSlots
        // was already set to targetSlots, so targetSlots-defSlots==0), which
        // locked every row past row 2 behind padlocks and caused the server
        // Free-Space Gate to reject all vendor purchases. See XeTrinityz-reference.
        bool ApplySlotCapToHolder(uintptr_t holder, bool enable, uint16_t value)
        {
            if (!oSetExpandSlots) return false;
            if (!HolderLooksValid(holder)) return false;
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return false;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount == 0 || bcount > 4096) return false;

            bool any = false;
            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;

                // The setter is keyed by storage type, not by bucket address.
                // 0xFFFF is the constructors' "unset" marker - never a storage.
                uint16_t type = 0;
                if (!Read16(bucket + kOff_InvBucket_Type, &type) || type == kInvSlot_EmptyType) continue;

                uint16_t expand = 0;
                uint16_t defSlots = 0;
                if (enable)
                {
                    // `value` is a target CAP but the setter takes an EXPANSION,
                    // and the resulting cap is _defaultSlotCount + expansion.
                    // Each storage has its own default, so convert per bucket.
                    if (!OverrideExpandForType(type, value, &expand, &defSlots))
                    {
                        // No InventoryInfo row for this bucket type (e.g. Materials,
                        // Provisions, or other category-specific buckets). Compute
                        // defSlots directly from the bucket's CURRENT live state:
                        //   defSlots = cap - currentExpand
                        // In vanilla: currentExpand = 0 so defSlots = cap (correct).
                        // After our first apply: currentExpand = value - defSlots,
                        // so this stays self-consistent across ticks.
                        uint16_t curCap = 0, curExpand = 0;
                        if (!Read16(bucket + kOff_InvBucket_MaxSlots, &curCap)) continue;
                        Read16(bucket + kOff_InvBucket_ExpandSlots, &curExpand);
                        defSlots = (curCap >= curExpand) ? static_cast<uint16_t>(curCap - curExpand) : 0;
                        uint16_t targetCap = static_cast<uint16_t>(value);
                        if (targetCap > 700) targetCap = 700;
                        expand   = (static_cast<int>(targetCap) > defSlots)
                                   ? static_cast<uint16_t>(targetCap - defSlots) : 0;
                    }

                    // Already where we want it - skip, this is the steady state.
                    uint16_t cap = 0;
                    if (Read16(bucket + kOff_InvBucket_MaxSlots, &cap) &&
                        cap == static_cast<uint16_t>(defSlots + expand))
                    {
                        any = true;
                        continue;
                    }

                    // Capture BEFORE the first write. On a rebuild the fresh
                    // bucket carries the save's true expansion again, and
                    // hkSetExpandSlots refreshes the entry whenever the engine
                    // re-stamps its own value.
                    CaptureOrigExpandOnce(bucket, type);
                }
                else if (!FindOrigBucketCap(bucket, type, &expand))
                {
                    continue; // never touched this bucket - leave it alone
                }
                else
                {
                    uint16_t maxS = 0;
                    if (!StorageSlotsForType(type, &defSlots, &maxS))
                    {
                        uint16_t curCap = 0;
                        Read16(bucket + kOff_InvBucket_MaxSlots, &curCap);
                        defSlots = (curCap >= expand) ? static_cast<uint16_t>(curCap - expand) : 0;
                    }
                }

                int err = 0;
                if (oSetExpandSlots)
                    oSetExpandSlots(reinterpret_cast<void*>(holder), &err, nullptr, type, expand);

                // Guarantee maxSlots and expandSlots are updated on ALL buckets
                // (including Food/Provisions, Materials, Crafting which have no InventoryInfo row)
                Write16(bucket + kOff_InvBucket_ExpandSlots, expand);
                Write16(bucket + kOff_InvBucket_DeltaRaw, expand);
                Write16(bucket + kOff_InvBucket_DeltaClamped, expand);
                Write16(bucket + kOff_InvBucket_MaxSlots, static_cast<uint16_t>(defSlots + expand));
                any = true;
            }
            return any;
        }

        bool SetAllTableMaxSlots(bool enable, uint16_t value)
        {
            if (!g_invTableGlobal) return false;
            uintptr_t table = 0;
            if (!ReadPtr(g_invTableGlobal, &table) || table < kMinPointer) return false;
            uint32_t count = 0;
            if (!Read32(table + kOff_ItemTable_Count, &count) || count == 0 || count > 4096) return false;

            static std::vector<uint16_t> s_origTableMax;
            static bool s_tableMaxCaptured = false;

            if (!s_tableMaxCaptured || s_origTableMax.size() != count)
            {
                s_origTableMax.assign(count, 0);
                for (uint32_t row = 0; row < count; ++row)
                {
                    uintptr_t def = 0;
                    if (!DefForRow(g_invTableGlobal, static_cast<uint16_t>(row), &def)) continue;
                    uint16_t m = 0;
                    Read16(def + kOff_InvDef_MaxSlots, &m);
                    s_origTableMax[row] = m;
                }
                s_tableMaxCaptured = true;
            }

            const uint16_t targetM = enable ? ((value > 700) ? 700 : value) : 0;
            bool any = false;
            for (uint32_t row = 0; row < count; ++row)
            {
                uintptr_t def = 0;
                if (!DefForRow(g_invTableGlobal, static_cast<uint16_t>(row), &def)) continue;
                const uint16_t finalM = enable ? ((targetM > s_origTableMax[row]) ? targetM : s_origTableMax[row]) : s_origTableMax[row];
                if (Write16(def + kOff_InvDef_MaxSlots, finalM))
                    any = true;
            }
            return any;
        }
    }

    bool Inventory::SetAllSlotSizes(bool enable, int value)
    {
        if (value < 1) value = 1;
        if (value > 700) value = 700;
        const uint16_t v = static_cast<uint16_t>(value);

        // Update in-memory InventoryInfo table so the engine and in-game UI natively recognize the 700-slot cap
        SetAllTableMaxSlots(enable, v);

        bool any = false;
        if (ApplySlotCapToHolder(CurrentHolder(), enable, v)) any = true;
        if (ApplySlotCapToHolder(ServerHolder(), enable, v))  any = true;

        Candidate snap[kMaxCandidates] = {};
        const int n = SnapshotCandidates(snap);
        for (int i = 0; i < n; ++i)
        {
            if (snap[i].holder && HolderLooksValid(snap[i].holder))
            {
                if (ApplySlotCapToHolder(snap[i].holder, enable, v)) any = true;
            }
        }

        // Restores are one-shot: once every live bucket has been put back,
        // the captures have served their purpose, and holding them would only
        // let a recycled address hand a stale expansion to a later load.
        if (!enable && any && g_capLockInit)
        {
            EnterCriticalSection(&g_capLock);
            g_origBucketCap.clear();
            LeaveCriticalSection(&g_capLock);
        }
        return any;
    }

    namespace
    {
        void RunPendingAdd(); // defined below, with the add-item path

        struct TrackedItemState
        {
            uint16_t typeId = 0;
            int64_t  qty = 0;
            char     name[64]{};
            char     key[64]{};
            char     icon[96]{};
        };
        static std::vector<TrackedItemState> g_trackedItems;
        static bool g_hasInitialTrackerState = false;

        void TrackInventoryChanges()
        {
            if (!Player::Ready()) return;
            const uintptr_t holder = CurrentHolder();
            if (!holder) return;

            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) || buckets < kMinPointer) return;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount == 0 || bcount > 4096) return;

            std::map<uint16_t, TrackedItemState> currentItems;
            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;
                uintptr_t slots = 0;
                uint16_t  scount = 0;
                if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
                if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount == 0 || scount > 8192) continue;

                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t slot = slots + static_cast<uintptr_t>(i) * SlotStride();
                    uint16_t tid = 0;
                    int64_t  qty = 0;
                    if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                    if (!Read64(slot + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;

                    auto& entry = currentItems[tid];
                    entry.typeId = tid;
                    entry.qty += qty;
                    if (!entry.name[0])
                    {
                        if (!DisplayNameForType(tid, entry.name, sizeof(entry.name)))
                        {
                            if (KeyForType(tid, entry.key, sizeof(entry.key)))
                                Prettify(entry.key, entry.name, sizeof(entry.name));
                            else
                                snprintf(entry.name, sizeof(entry.name), "Item #%u", tid);
                        }
                        if (!entry.key[0]) KeyForType(tid, entry.key, sizeof(entry.key));
                        if (!entry.icon[0]) IconForType(tid, entry.icon, sizeof(entry.icon));
                    }
                }
            }

            if (!g_hasInitialTrackerState)
            {
                g_trackedItems.clear();
                for (const auto& kv : currentItems)
                    g_trackedItems.push_back(kv.second);
                g_hasInitialTrackerState = true;
                return;
            }

            // Compare previous tracked state vs current state
            for (const auto& old : g_trackedItems)
            {
                auto it = currentItems.find(old.typeId);
                const int64_t curQty = (it != currentItems.end()) ? it->second.qty : 0;
                if (curQty < old.qty)
                {
                    const int64_t diff = old.qty - curQty;
                    // The item was sold, discarded, or consumed!
                    Inventory::RecordLostItem(old.typeId, diff, old.name, old.key, old.icon, "Sold / Discarded");
                }
            }

            // Update tracked state
            g_trackedItems.clear();
            for (const auto& kv : currentItems)
                g_trackedItems.push_back(kv.second);
        }
    }

    void Inventory::Tick()
    {
        __try
        {
            const State& st = State::Get();

            // Any queued Add Item runs here, on the game thread: unlike every other
            // write in this file it calls into engine code, which the render thread
            // must never do.
            RunPendingAdd();

            // Real-time tracker for items sold, discarded, or removed
            if (Player::Ready())
            {
                static ULONGLONG s_lastTrack = 0;
                const ULONGLONG now = GetTickCount64();
                if (now - s_lastTrack >= 1500)
                {
                    s_lastTrack = now;
                    TrackInventoryChanges();
                }
            }

            // Heal the used-slot accounting that quantity edits bend and reloads
            // detonate (the "inventory full beside empty slots" bug). Always on;
            // 1 Hz; a strict no-op on buckets the engine's own accounting produced.
            if (Player::Ready())
            {
                static ULONGLONG s_lastRepair = 0;
                const ULONGLONG now = GetTickCount64();
                if (now - s_lastRepair >= 1000)
                {
                    s_lastRepair = now;
                    RepairUsedSlots(CurrentHolder());
                    RepairUsedSlots(ServerHolder());
                    Candidate snap[kMaxCandidates] = {};
                    const int n = SnapshotCandidates(snap);
                    for (int i = 0; i < n; ++i)
                    {
                        if (snap[i].holder && HolderLooksValid(snap[i].holder))
                            RepairUsedSlots(snap[i].holder);
                    }
                }
            }

            if (st.invStackSize && Player::Ready())
            {
                if (!g_stackApplied || g_stackAppliedVal != st.invStackSizeVal)
                {
                    if (SetAllMaxStackSizes(true, st.invStackSizeVal))
                    {
                        g_stackApplied    = true;
                        g_stackAppliedVal = st.invStackSizeVal;
                    }
                }
            }
            else if (g_stackApplied)
            {
                if (SetAllMaxStackSizes(false, 0))
                    g_stackApplied = false;
            }

            // Unlike the stack-size table above, this is NOT edge-triggered. Slot
            // caps live on bucket objects that a save load destroys and rebuilds,
            // so "apply once when the toggle changes" works on the first load and
            // never again. SetAllSlotSizes skips buckets that already match, so
            // re-driving it costs a u16 read per bucket and makes the
            // feature survive reloads (and anything else that recomputes the cap).
            if (st.invSlotSize && Player::Ready() && !g_commitActive.load(std::memory_order_acquire))
            {
                static ULONGLONG s_lastSlotTick = 0;
                const ULONGLONG now = GetTickCount64();
                if (now - s_lastSlotTick >= 500)
                {
                    s_lastSlotTick = now;
                    if (SetAllSlotSizes(true, st.invSlotSizeVal))
                    {
                        g_slotApplied    = true;
                        g_slotAppliedVal = st.invSlotSizeVal;
                    }
                }
            }
            else if (g_slotApplied)
            {
                if (SetAllSlotSizes(false, 0))
                    g_slotApplied = false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool Inventory::IsTransactionActive()
    {
        return g_commitActive.load(std::memory_order_acquire);
    }
    namespace
    {
        // Write the quantity of the slot in the SERVER-authority holder that
        // mirrors a given client slot. The two holders are position-perfect
        // mirrors (same bucket index / slot index / typeId), so the fast path
        // is a direct [bucketIdx][slotIdx] hit, verified by typeId. If that
        // ever fails to line up, fall back to the unique slot in the server
        // holder with the same typeId AND the same pre-edit quantity (which
        // disambiguates one stack from another of the same item). Returns true
        // only when a matching server slot was actually written.
        bool WriteServerMirror(uint32_t bucketIdx, uint16_t slotIdx,
                               uint16_t typeId, int64_t oldQty, int64_t value)
        {
            const uintptr_t sh = ServerHolder();
            if (!HolderLooksValid(sh)) return false;

            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(sh + kOff_InvHolder_Buckets, &buckets)) return false;
            if (!Read32(sh + kOff_InvHolder_Count, &bcount)) return false;
            if (buckets < kMinPointer || bcount == 0 || bcount > 4096) return false;

            // Fast path: the mirrored position.
            if (bucketIdx < bcount)
            {
                uintptr_t bucket = 0;
                if (ReadPtr(buckets + static_cast<uintptr_t>(bucketIdx) * 8, &bucket) && bucket >= kMinPointer)
                {
                    uintptr_t slots = 0;
                    uint16_t  scount = 0;
                    if (ReadPtr(bucket + kOff_InvBucket_Slots, &slots) && slots >= kMinPointer &&
                        Read16(bucket + kOff_InvBucket_Count, &scount) && slotIdx < scount)
                    {
                        const uintptr_t slot = slots + static_cast<uintptr_t>(slotIdx) * SlotStride();
                        uint16_t tid = 0;
                        if (Read16(slot + kOff_InvSlot_TypeId, &tid) && tid == typeId)
                            return Write64(slot + kOff_InvSlot_Quantity, value);
                    }
                }
            }

            // Fallback: unique (typeId, oldQty) match anywhere in the server holder.
            uintptr_t hitSlot = 0;
            int hits = 0;
            for (uint32_t b = 0; b < bcount && hits < 2; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;
                uintptr_t slots = 0;
                uint16_t  scount = 0;
                if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
                if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount == 0 || scount > 8192) continue;
                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t slot = slots + static_cast<uintptr_t>(i) * SlotStride();
                    uint16_t tid = 0;
                    int64_t  q   = 0;
                    if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid != typeId) continue;
                    if (!Read64(slot + kOff_InvSlot_Quantity, &q) || q != oldQty) continue;
                    hitSlot = slot;
                    if (++hits >= 2) break; // ambiguous - give up rather than edit the wrong stack
                }
            }
            if (hits == 1 && hitSlot)
                return Write64(hitSlot + kOff_InvSlot_Quantity, value);
            return false;
        }

        // Address of the slot at (bucketIdx, slotIdx) in a holder, but only if
        // that slot's typeId equals wantType (pass kInvSlot_EmptyType to demand
        // an empty target, or a real typeId to demand a specific item). Returns
        // 0 if the position is out of range or the typeId doesn't match - the
        // two holders are position-perfect mirrors, so this doubles as the
        // "same slot in the other holder" lookup.
        uintptr_t SlotByPos(uintptr_t holder, uint32_t bucketIdx, uint16_t slotIdx,
                            uint16_t wantType)
        {
            if (!HolderLooksValid(holder)) return 0;
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return 0;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount)) return 0;
            if (buckets < kMinPointer || bucketIdx >= bcount) return 0;
            uintptr_t bucket = 0;
            if (!ReadPtr(buckets + static_cast<uintptr_t>(bucketIdx) * 8, &bucket) ||
                bucket < kMinPointer) return 0;
            uintptr_t slots  = 0;
            uint16_t  scount = 0;
            if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) return 0;
            if (!Read16(bucket + kOff_InvBucket_Count, &scount) || slotIdx >= scount) return 0;
            const uintptr_t slot = slots + static_cast<uintptr_t>(slotIdx) * SlotStride();
            uint16_t tid = 0;
            if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid != wantType) return 0;
            return slot;
        }
    }

    bool Inventory::SetQuantity(int st, int cat, int idx, int64_t value)
    {
        Item* ip = ItemAt(st, cat, idx);
        if (!ip) return false;
        if (value < 0) value = 0;
        Item& it = *ip;

        // The client mirror (what the list is built from) must take the write.
        if (!Write64(it.slot + kOff_InvSlot_Quantity, value)) return false;

        // ...and the server authority, or a per-frame reconcile reverts it.
        // Best-effort: if the server holder has not been captured yet (no
        // inventory transaction this session), the edit still shows but will
        // not persist - Persisted() lets the UI warn about that.
        WriteServerMirror(it.bucketIdx, it.slotIdx, it.typeId, it.qty, value);

        it.qty = value; // reflect immediately until next Refresh
        return true;
    }

    bool Inventory::EditsPersist()
    {
        return HolderLooksValid(CurrentHolder()) || HolderLooksValid(ServerHolder());
    }

    // --- Bridges for the dye editor (dye.cpp) -------------------------------
    // Narrow re-exports of internals the dye module needs: item naming for its
    // equipped-slot list, and the character/realm plumbing for reaching each
    // realm's equip component (dual-realm, same rules as the add path - see
    // the comments on RealmFlagAddr / ServerHolder above).
    bool Inventory::NameForTypeId(uint16_t typeId, char* out, size_t n)
    {
        if (DisplayNameForType(typeId, out, n)) return true;
        char key[64]{ 0 };
        if (KeyForType(typeId, key, sizeof(key)))
        {
            Prettify(key, out, n);
            return true;
        }
        return false;
    }

    bool Inventory::IconForTypeId(uint16_t typeId, char* out, size_t n)
    {
        if (n) out[0] = 0;
        return IconForType(typeId, out, n);
    }

    // The two realms' player CHARACTERS. What this file calls a "container" is
    // the character actor itself - the engine's own inventory lookup is
    // *(*(actor + 0x68) + 0xB8) (IDB sub_1CDE460), the very walk
    // HolderForContainer already does - so these are the same objects the
    // holder plumbing above resolves, handed over one step earlier. dye.cpp
    // needs the actor rather than the holder because equipment does not live
    // in a holder at all (see the dye note in offsets.h).
    uintptr_t Inventory::ClientCharacterAddr()
    {
        const uintptr_t c = ResolveClientContainer();
        return IsLiveCharacter(c) ? c : 0;
    }

    uintptr_t Inventory::ServerCharacterAddr()
    {
        ServerHolder(); // resolves/re-validates g_serverContainer as a side effect
        const uintptr_t c = g_serverContainer.load(std::memory_order_acquire);
        return IsLiveCharacter(c) ? c : 0;
    }

    // Identify character identity from equipped items:
    // 0 = Kliff / Default, 1 = Damiane, 2 = Oongka
    static int IdentifyCharacterFromEquip(uintptr_t container)
    {
        if (container < kMinPointer) return -1;
        uintptr_t sub = 0, comp = 0, owner = 0;
        if (!ReadPtr(container + kOff_Container_Sub, &sub) || sub < kMinPointer) return -1;
        if (!ReadPtr(sub + kOff_Sub_EquipComp, &comp) || comp < kMinPointer) return -1;
        if (!ReadPtr(comp + kOff_EquipComp_Owner, &owner) || owner != container) return -1;

        uintptr_t desc = 0, array = 0;
        uint32_t count = 0;
        uintptr_t stride = 0xD0;

        if (ReadPtr(comp + 0x88, &desc) && desc >= kMinPointer &&
            ReadPtr(desc + kOff_EquipTable_Array, &array) && array >= kMinPointer &&
            Read32(desc + kOff_EquipTable_Count, &count) && count >= 1 && count <= 64)
        {
            stride = 0xC8;
        }
        else if (ReadPtr(comp + 0x80, &desc) && desc >= kMinPointer &&
                 ReadPtr(desc + kOff_EquipTable_Array, &array) && array >= kMinPointer &&
                 Read32(desc + kOff_EquipTable_Count, &count) && count >= 1 && count <= 64)
        {
            stride = 0xD0;
        }
        else
        {
            return -1;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
            uint16_t tid = 0;
            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;

            // Direct TypeID recognition
            // Damiane (1)
            if (tid == 53935 || tid == 6324 || tid == 6041 || tid == 5306 || tid == 5300 ||
                tid == 5297 || tid == 5277 || tid == 3463 || (tid >= 5450 && tid <= 5468) ||
                (tid >= 5270 && tid <= 5310) || (tid >= 6320 && tid <= 6330))
                return 1;
            // Oongka (2)
            if (tid == 6560 || tid == 6042 || tid == 6305 || (tid >= 6550 && tid <= 6570))
                return 2;
            // Kliff (0)
            if (tid == 6303 || tid == 6040 || (tid >= 5330 && tid <= 5350))
                return 0;

            char key[96] = "";
            if (KeyForType(tid, key, sizeof(key)))
            {
                if (strstr(key, "Damian") || strstr(key, "Demian") || strstr(key, "Demeniss") ||
                    strstr(key, "Rapier") || strstr(key, "Spencer") || strstr(key, "Dewhaven") ||
                    strstr(key, "White Wind") || strstr(key, "Hwando") || strstr(key, "hwando"))
                    return 1; // Damiane
                if (strstr(key, "Oongka") || strstr(key, "Giant") || strstr(key, "Tynion") ||
                    strstr(key, "Rocket") || strstr(key, "Cannon") || strstr(key, "Club"))
                    return 2; // Oongka
                if (strstr(key, "Kliff") || strstr(key, "DarknessKing") || strstr(key, "Balgran") ||
                    strstr(key, "Aeserion") || strstr(key, "Greatsword"))
                    return 0; // Kliff
            }
        }
        return -1; // Unrecognized
    }

    uintptr_t Inventory::CharacterAddr(int index)
    {
        const uintptr_t clientC = ResolveClientContainer();
        if (index == 0) return IsLiveCharacter(clientC) ? clientC : 0;

        // Direct check: if client container belongs to this character
        if (clientC && IdentifyCharacterFromEquip(clientC) == index)
            return clientC;

        // Collect all candidate containers
        uintptr_t candidates[64] = {};
        int candCount = 0;

        auto addCand = [&](uintptr_t c) {
            if (c < kMinPointer || c == clientC) return;
            for (int i = 0; i < candCount; ++i)
                if (candidates[i] == c) return;
            if (candCount < 64) candidates[candCount++] = c;
        };

        // 1. Container manager array
        if (clientC)
        {
            uintptr_t sub = 0, holder = 0;
            if (ReadPtr(clientC + kOff_Container_Sub, &sub) && sub >= kMinPointer &&
                ReadPtr(sub + kOff_Sub_Holder, &holder) && holder >= kMinPointer)
            {
                uintptr_t arr = 0;
                uint32_t count = 0;
                if (ReadPtr(holder + 0x18, &arr) && arr >= kMinPointer &&
                    Read32(holder + 0x20, &count) && count > 1 && count <= 64)
                {
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        uintptr_t c = 0;
                        if (ReadPtr(arr + static_cast<uintptr_t>(i) * 8, &c) && c >= kMinPointer)
                            addCand(c);
                    }
                }
            }
        }

        // 2. Snapshot candidates
        Candidate snap[kMaxCandidates] = {};
        const int n = SnapshotCandidates(snap);
        for (int i = 0; i < n; ++i)
            addCand(snap[i].container);

        // 3. Active world party actors
        for (int i = 1; i < 3; ++i)
        {
            const uintptr_t act = Player::GetActor(i);
            if (act) addCand(act);
        }

        // Match container by character identity
        for (int i = 0; i < candCount; ++i)
        {
            const uintptr_t c = candidates[i];
            if (IdentifyCharacterFromEquip(c) == index)
                return c;
        }

        // Fallback: if player actor index is available directly
        if (index > 0 && index < 3)
        {
            const uintptr_t partyAct = Player::GetActor(index);
            if (partyAct >= kMinPointer) return partyAct;
        }

        return 0;
    }

    int Inventory::ActivePlayerCharacterIdx()
    {
        const uintptr_t clientC = ResolveClientContainer();
        if (clientC)
        {
            int ident = IdentifyCharacterFromEquip(clientC);
            if (ident >= 0) return ident;
        }
        return 0; // Default to Kliff (0)
    }

    uintptr_t Inventory::RealmFlagAddress(uint8_t* outVal) { return RealmFlagAddr(outVal); }

    namespace
    {
        // --- Add item: the game's own create path (see offsets.h) ------------

        // The holder bucket this item belongs in, chosen the way the game
        // chooses it: the item def's own default storage vs bucket+0x10.
        uintptr_t BucketForItem(uintptr_t holder, uintptr_t def)
        {
            uint16_t want = 0;
            // Check TU 1.17+ modern (+0x418) first, then TU 1.14 legacy (+66 / 0x42)
            if (!Read16(def + 0x418, &want) || want == 0 || want > 64)
            {
                if (!Read16(def + 66, &want) || want == 0 || want > 64)
                    want = 1; // Default to main bag storage
            }

            uintptr_t buckets = 0;
            uint32_t  n = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return 0;
            if (!Read32(holder + kOff_InvHolder_Count, &n) || !n || n > 4096) return 0;

            for (uint32_t i = 0; i < n; ++i)
            {
                uintptr_t b = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(i) * 8, &b) || b < kMinPointer) continue;
                uint16_t t = 0;
                if (Read16(b + kOff_InvBucket_Type, &t) && t == want) return b;
            }

            // Fallback: search for bucket with type == 1 (main bag)
            for (uint32_t i = 0; i < n; ++i)
            {
                uintptr_t b = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(i) * 8, &b) || b < kMinPointer) continue;
                uint16_t t = 0;
                if (Read16(b + kOff_InvBucket_Type, &t) && t == 1) return b;
            }

            // Fallback: return first valid bucket
            for (uint32_t i = 0; i < n; ++i)
            {
                uintptr_t b = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(i) * 8, &b) && b >= kMinPointer) return b;
            }
            return 0;
        }

        // The instance-id allocator for a container. Every added item needs an
        // id from here: an item without one is what bricked a save under the
        // old fabricate-a-slot design, so callers MUST treat 0 as "refuse".
        uintptr_t IdAllocator(uintptr_t container)
        {
            uintptr_t tagObj = 0;
            if (!ReadPtr(container + kOff_Owner_TypeDesc, &tagObj) || tagObj < kMinPointer) return 0;
            uint8_t tag = 0;
            if (!Read8(tagObj + 1, &tag)) return 0;
            uintptr_t owner = container;
            if ((tag & 0xF7) != 0 &&
                (!ReadPtr(container + kOff_Owner_Possessor, &owner) || owner < kMinPointer))
                return 0;
            uintptr_t sub = 0, alloc = 0;
            if (!ReadPtr(owner + kOff_Container_Sub, &sub) || sub < kMinPointer) return 0;
            if (!ReadPtr(sub + kOff_Sub_IdAllocator, &alloc) || alloc < kMinPointer) return 0;
            return alloc;
        }

        bool IsWearableGear(const char* k)
        {
            if (!k || !k[0]) return false;
            if (strstr(k, "Money") || strstr(k, "Silver") || strstr(k, "Gold") ||
                strstr(k, "Pack") || strstr(k, "Chest") || strstr(k, "Reward") ||
                strstr(k, "Potion") || strstr(k, "Food") || strstr(k, "Material") ||
                strstr(k, "Stone") || strstr(k, "Ore") || strstr(k, "Herb") ||
                strstr(k, "Seed") || strstr(k, "Cell") || strstr(k, "Artifact") ||
                strstr(k, "Scroll") || strstr(k, "Book") || strstr(k, "Token") ||
                strstr(k, "Item_Skill") || strstr(k, "Ticket") || strstr(k, "Feed") ||
                strstr(k, "Meat") || strstr(k, "Fish") || strstr(k, "Dish") ||
                strstr(k, "Document") || strstr(k, "Notice") || strstr(k, "Letter") ||
                strstr(k, "Diary") || strstr(k, "Report") || strstr(k, "Recipe") ||
                strstr(k, "Key") || strstr(k, "Quest") || strstr(k, "Map"))
                return false;

            return (strstr(k, "Sword") || strstr(k, "Shield") || strstr(k, "Bow") ||
                    strstr(k, "Armor") || strstr(k, "Helm") || strstr(k, "Helmet") ||
                    strstr(k, "Cloak") || strstr(k, "Glove") || strstr(k, "Boot") ||
                    strstr(k, "Shoe") || strstr(k, "Ring") || strstr(k, "Necklace") ||
                    strstr(k, "Earring") || strstr(k, "Belt") || strstr(k, "Axe") ||
                    strstr(k, "Mace") || strstr(k, "Hammer") || strstr(k, "Spear") ||
                    strstr(k, "Dagger") || strstr(k, "Rapier") || strstr(k, "Barding") ||
                    strstr(k, "Saddle") || strstr(k, "Chamfron") || strstr(k, "Stirrup") ||
                    strstr(k, "Mask") || strstr(k, "Visione") || strstr(k, "Costume") ||
                    strstr(k, "Outfit") || strstr(k, "Headgear") || strstr(k, "Lantern") ||
                    strstr(k, "Weapon") || strstr(k, "Equip") || strstr(k, "Plate"));
        }

        // Build + plan + commit + free, with the realm already switched by the
        // caller. POD locals only: __try/__except is illegal in a function that
        // needs unwinding, which is also why the realm save/restore lives one
        // level up rather than in an RAII guard.
        int AddInRealm(uintptr_t holder, uintptr_t container, uintptr_t bucket,
                       uint16_t typeId, int64_t qty, int64_t id, uintptr_t def, const char* realm)
        {
            alignas(16) uint8_t itemVal[kItemVal_Size] = {}; // ZEROED: the ctor
            // leaves holes (+0x0C, +0x3A, +0x54, +0x8A..) that would otherwise
            // reach the live slot - the game only gets away with an
            // uninitialised buffer because it copy-constructs first.
            uintptr_t arr[2] = {};  // input vector {ptr, count@8, cap@12}
            uintptr_t out[3] = {};  // placement vector {ptr, count@8, cap@12}
            int err = 0;
            volatile int committed = 0;
            volatile int firstErr2 = 0;      // first failing commit's error code
            volatile uint32_t nPlaced = 0;   // placements the planner produced
            volatile bool built = false, planned = false, excepted = false;

            RepairUsedSlots(holder);

            __try
            {
                if (oItemValueCtor)
                {
                    oItemValueCtor(itemVal, &typeId, qty);
                    built = true;
                    *reinterpret_cast<uint16_t*>(itemVal + kOff_ItemVal_Subtype)   = 0;
                    *reinterpret_cast<int64_t*>(itemVal + kOff_ItemVal_InstanceId) = id;
                }
                else
                {
                    memset(itemVal, 0, sizeof(itemVal));
                    built = true;
                    *reinterpret_cast<int64_t*>(itemVal + kOff_ItemVal_InstanceId) = id;
                    *reinterpret_cast<uint16_t*>(itemVal + kOff_InvSlot_TypeId)   = typeId;
                    *reinterpret_cast<uint16_t*>(itemVal + kOff_ItemVal_Subtype)  = 0;
                    *reinterpret_cast<int64_t*>(itemVal + kOff_InvSlot_Quantity) = qty;
                }

                arr[0] = reinterpret_cast<uintptr_t>(itemVal);
                reinterpret_cast<uint32_t*>(arr)[2] = 1; // count
                reinterpret_cast<uint32_t*>(arr)[3] = 1; // capacity - MUST be set:
                // the planner deep-copies this vector, and an uninitialised
                // capacity corrupts the heap (a delayed, misleading crash).

                oHolderInsert(reinterpret_cast<void*>(bucket), &err,
                              reinterpret_cast<void*>(container), arr, 0, out, 1, 1, 0);
                planned = true;

                if (err == 0)
                {
                    const uintptr_t p0 = out[0];
                    const uint32_t  n  = reinterpret_cast<uint32_t*>(out)[2];
                    nPlaced = n;
                    const uintptr_t placementStride = core::GetPlacementStride();
                    const uintptr_t slotIdxOffset   = core::GetPlacementSlotIdxOffset();
                    for (uint32_t i = 0; i < n && p0 >= kMinPointer; ++i)
                    {
                        const uintptr_t p = p0 + static_cast<uintptr_t>(i) * placementStride;
                        const uint16_t slotIdx =
                            *reinterpret_cast<uint16_t*>(p + slotIdxOffset);
                        int err2 = 0;
                        oCommitPlacement(reinterpret_cast<void*>(holder), &err2, nullptr,
                                         reinterpret_cast<void*>(p), slotIdx);
                        if (err2 == 0) ++committed;
                        else if (!firstErr2) firstErr2 = err2;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { excepted = true; }

            // Freed in the target realm, exactly once each, whatever happened.
            if (planned) { __try { oFreePlacements(out); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
            if (built && oItemValueDtor) { __try { oItemValueDtor(itemVal); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
            return committed;
        }

        // One holder, switching the realm around the whole operation.
        bool AddIntoHolder(uintptr_t holder, bool serverRealm,
                           uint16_t typeId, int64_t qty, int64_t id, uintptr_t def)
        {
            const char* realm = serverRealm ? "server" : "client";
            uintptr_t container = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Container, &container) ||
                container < kMinPointer)
            {
                LOG_WARN("inventory: add[%s] %u: holder has no container", realm, typeId);
                return false;
            }
            const uintptr_t bucket = BucketForItem(holder, def);
            if (!bucket)
            {
                uint16_t want = 0;
                Read16(def + kOff_ItemDef_BucketType, &want);
                LOG_WARN("inventory: add[%s] %u: no bucket of type %u in holder",
                         realm, typeId, want);
                return false;
            }

            uint8_t flagVal = 0xFF; // 0xFF = chain broke before the byte was read
            const uintptr_t flagAddr = RealmFlagAddr(&flagVal);
            if (!flagAddr)
            {
                LOG_WARN("inventory: add[%s] %u: realm flag unresolved (byte=0x%02X)",
                         realm, typeId, flagVal);
                return false;
            }
            uint8_t prev = 0;
            if (!RawRead8(flagAddr, &prev)) return false;
            if (!RawWrite8(flagAddr, serverRealm ? 1 : 0))
            {
                LOG_WARN("inventory: add[%s] %u: realm flag not writable", realm, typeId);
                return false;
            }

            const int committed = AddInRealm(holder, container, bucket, typeId, qty, id, def, realm);

            // Never skipped: leaving a game thread in the wrong realm would
            // corrupt whatever it touches next.
            RawWrite8(flagAddr, prev);
            return committed > 0;
        }

        int64_t FindMaxInstanceId(uintptr_t holder)
        {
            if (holder < kMinPointer) return 0;
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return 0;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount > 4096) return 0;

            int64_t maxId = 0;
            const uintptr_t stride = SlotStride();
            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;

                uintptr_t slots = 0;
                uint16_t  scount = 0;
                if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
                if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount == 0 || scount > 8192) continue;

                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t slot = slots + static_cast<uintptr_t>(i) * stride;
                    uint16_t tid = 0;
                    if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid == 0 || tid == kInvSlot_EmptyType) continue;
                    int64_t inst = 0;
                    if (Read64(slot + kOff_ItemVal_InstanceId, &inst) && inst > maxId && inst < 0x7FFFFFFFLL)
                        maxId = inst;
                }
            }
            return maxId;
        }

        // Pending request: the UI runs on the render thread, but this path calls
        // into engine code and must run on the game thread (Tick).
        struct AddRequest { uint16_t typeId; int64_t qty; };
        AddRequest             g_addReq{};
        std::atomic<bool>      g_addPending{false};
        std::atomic<int>       g_addState{0}; // mirrors Inventory::AddState

        // The one add, on the GAME thread: resolves both holders, allocates one
        // shared instance id from the authority, and stamps the item into
        // the server mirror then the client one.
        bool CommitAdd(uint16_t typeId, int64_t qty)
        {
            const bool ready = oHolderInsert && oCommitPlacement &&
                               oFreePlacements && oNtQueryInfoThread;
            uintptr_t def = 0;
            const bool haveDef = DefForRow(g_itemTableGlobal, typeId, &def);
            const uintptr_t clientH = CurrentHolder();
            uintptr_t serverH = ServerHolder();
            if (!ready || !haveDef || !clientH)
            {
                LOG_WARN("inventory: add item %u x%lld - not ready (ready=%d client=%p server=%p def=%p ctor=%d ins=%d commit=%d free=%d teb=%d)",
                         typeId, static_cast<long long>(qty),
                         ready ? 1 : 0,
                         reinterpret_cast<void*>(clientH), reinterpret_cast<void*>(serverH),
                         reinterpret_cast<void*>(def),
                         oItemValueCtor ? 1 : 0, oHolderInsert ? 1 : 0, oCommitPlacement ? 1 : 0,
                         oFreePlacements ? 1 : 0, oNtQueryInfoThread ? 1 : 0);
                return false;
            }

            // For wearable equipment (weapons, armor, accessories), enforce qty = 1 so they can be equipped individually
            char itemKey[96] = "";
            KeyForType(typeId, itemKey, sizeof(itemKey));

            if (IsWearableGear(itemKey))
            {
                qty = 1;
            }

            // Instance ID allocation
            uintptr_t authorityH = (serverH && serverH != clientH) ? serverH : clientH;
            uintptr_t serverC = 0;
            if (!ReadPtr(authorityH + kOff_InvHolder_Container, &serverC) || serverC < kMinPointer)
            {
                serverC = ResolveClientContainer();
            }
            uintptr_t alloc = IdAllocator(serverC);
            if (!alloc)
            {
                for (int c = 0; c < 3; ++c)
                {
                    const uintptr_t candChar = Inventory::CharacterAddr(c);
                    if (candChar)
                    {
                        alloc = IdAllocator(candChar);
                        if (alloc) break;
                    }
                }
            }
            int64_t maxClient = FindMaxInstanceId(clientH);
            int64_t maxServer = FindMaxInstanceId(serverH);
            int64_t maxExisting = (maxServer > maxClient) ? maxServer : maxClient;

            int64_t id = 0;
            if (alloc)
            {
                id = _InterlockedIncrement64(
                    reinterpret_cast<volatile int64_t*>(alloc + kOff_IdAlloc_Counter));
            }
            if (id <= maxExisting || id < 1000000)
            {
                static std::atomic<int64_t> s_seqId{0};
                int64_t cur = s_seqId.load();
                int64_t base = (cur > maxExisting) ? cur : maxExisting;
                if (base < 1000000) base = 1000000;
                id = base + 1;
                s_seqId.store(id);
            }

            // Server first if available and distinct, then client mirror
            bool okServer = false;
            if (serverH && serverH != clientH)
            {
                okServer = AddIntoHolder(serverH, /*serverRealm=*/true,  typeId, qty, id, def);
            }
            const bool okClient = AddIntoHolder(clientH, /*serverRealm=*/false, typeId, qty, id, def);

            if (okClient || okServer)
            {
                char itemName[64] = "";
                if (!DisplayNameForType(typeId, itemName, sizeof(itemName)))
                    Prettify(itemKey, itemName, sizeof(itemName));
                LOG("inventory: Added %lldx '%s' (TypeID %u, InstID 0x%llX) [server=%d client=%d].",
                    static_cast<long long>(qty), itemName[0] ? itemName : itemKey, typeId,
                    static_cast<unsigned long long>(id), okServer ? 1 : 0, okClient ? 1 : 0);
                RefreshImpl(true);
                return true;
            }

            LOG_WARN("inventory: add item %u x%lld PARTIAL (server=%d client=%d)",
                     typeId, static_cast<long long>(qty), okServer ? 1 : 0, okClient ? 1 : 0);
            return false;
        }

        uintptr_t FindSlotByInstanceHelper(uintptr_t holder, int64_t targetInstId)
        {
            if (holder < kMinPointer || targetInstId <= 0) return 0;
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return 0;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount > 4096) return 0;

            const uintptr_t stride = SlotStride();
            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;

                uintptr_t slots = 0;
                uint16_t  scount = 0;
                if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
                if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount == 0 || scount > 8192) continue;

                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t slot = slots + static_cast<uintptr_t>(i) * stride;
                    int64_t inst = 0;
                    if (Read64(slot + kOff_ItemVal_InstanceId, &inst) && inst == targetInstId)
                        return slot;
                }
            }
            return 0;
        }

        // Bulk add ("add X of every item in a category"): the render thread queues
        // a whole batch at once, and the game thread drains it a few per Tick, so
        // hundreds of engine allocations never land in one frame. The counts latch
        // when the queue empties, for the UI to report a summary.
        std::mutex               g_bulkMutex;
        std::vector<AddRequest>  g_bulkQueue; // guarded by g_bulkMutex
        std::atomic<int>         g_bulkTotal{0};
        std::atomic<int>         g_bulkAdded{0};
        std::atomic<int>         g_bulkFailed{0};
        std::atomic<bool>        g_bulkActive{false};

        void RunBulkAdd()
        {
            if (!g_bulkActive.load(std::memory_order_acquire)) return;

            // A bounded slice per Tick: enough to drain a big category in a
            // handful of frames, few enough that no single frame pays for it all.
            constexpr int kPerTick = 16;
            for (int n = 0; n < kPerTick; ++n)
            {
                AddRequest req;
                {
                    std::lock_guard<std::mutex> lk(g_bulkMutex);
                    if (g_bulkQueue.empty()) break;
                    req = g_bulkQueue.back(); // order is irrelevant; pop the cheap end
                    g_bulkQueue.pop_back();
                }
                if (CommitAdd(req.typeId, req.qty))
                    g_bulkAdded.fetch_add(1, std::memory_order_relaxed);
                else
                    g_bulkFailed.fetch_add(1, std::memory_order_relaxed);
            }

            std::lock_guard<std::mutex> lk(g_bulkMutex);
            if (g_bulkQueue.empty())
                g_bulkActive.store(false, std::memory_order_release); // done; counts latch
        }

        // Runs on the GAME thread, from Tick().
        void RunPendingAdd()
        {
            if (g_addPending.load(std::memory_order_acquire))
            {
                const AddRequest req = g_addReq;
                g_addPending.store(false, std::memory_order_release);
                g_addState.store(static_cast<int>(CommitAdd(req.typeId, req.qty)
                                     ? Inventory::AddState::Added
                                     : Inventory::AddState::Failed),
                                 std::memory_order_release);
            }
            RunBulkAdd();
        }
    }

    namespace
    {
        // --- The item catalog -------------------------------------------------
        // The item table is static data: it cannot change while the game runs.
        // So unlike the inventory snapshot this is built ONCE and never
        // refreshed - which matters, because it walks thousands of rows and
        // resolves a name, icon and category for each.
        // Reuses the snapshot's own Item/Group types, so a catalog category is
        // literally the same structure as an inventory category - same labels,
        // same tab, same icon, same ordering rules.
        std::vector<Group> g_catalog;
        bool g_catalogBuilt = false;
        int  g_catalogDiagState = 0;

        void BuildCatalog()
        {
            if (g_catalogBuilt) return;
            EnsureTablesResolved();
            if (!g_itemTableGlobal)
            {
                if (g_catalogDiagState != 1)
                {
                    LOG_WARN("inventory: catalog wait - iteminfo resolver global is missing.");
                    g_catalogDiagState = 1;
                }
                return;
            }

            uintptr_t table = 0;
            if (!ReadPtr(g_itemTableGlobal, &table) || table < kMinPointer)
            {
                if (g_catalogDiagState != 2)
                {
                    LOG_WARN("inventory: catalog wait - iteminfo global=%p has no runtime table (value=%p).",
                             reinterpret_cast<void*>(g_itemTableGlobal), reinterpret_cast<void*>(table));
                    g_catalogDiagState = 2;
                }
                return;
            }
            uint32_t count = 0;
            if (!Read32(table + kOff_ItemTable_Count, &count) || !count || count > 65536)
            {
                if (g_catalogDiagState != 3)
                {
                    LOG_WARN("inventory: catalog wait - table=%p has invalid row count %u at +0x%llX.",
                             reinterpret_cast<void*>(table), static_cast<unsigned>(count),
                             static_cast<unsigned long long>(kOff_ItemTable_Count));
                    g_catalogDiagState = 3;
                }
                return;
            }
            LOG("inventory: catalog table ready - global=%p table=%p rows=%u.",
                reinterpret_cast<void*>(g_itemTableGlobal), reinterpret_cast<void*>(table),
                static_cast<unsigned>(count));
            g_catalogBuilt = true;

            uint32_t named = 0;
            for (uint32_t row = 0; row < count; ++row)
            {
                const uint16_t tid = static_cast<uint16_t>(row);
                if (tid == kInvSlot_EmptyType || tid == 0) continue;
                uintptr_t def = 0;
                if (!DefForRow(g_itemTableGlobal, tid, &def)) continue;

                Item it{};
                it.typeId = tid;
                // The game's own localised name, else the engine key made
                // readable. Unlike the snapshot there is deliberately no
                // "Item #N" fallback: a row with no name at all is an internal
                // or unused definition, and listing it would just be noise.
                if (!DisplayNameForType(tid, it.name, sizeof(it.name)))
                {
                    if (!KeyForType(tid, it.key, sizeof(it.key))) continue;
                    Prettify(it.key, it.name, sizeof(it.name));
                }
                if (!it.name[0]) continue;
                if (!it.key[0] && !KeyForType(tid, it.key, sizeof(it.key))) it.key[0] = 0;
                if (!IconForType(tid, it.icon, sizeof(it.icon))) it.icon[0] = 0;
                if (!CategoryOfType(tid, &it.cat)) it.cat = kNoCategory;
                it.tier = TierOfType(tid);
                ++named;

                Group* g = nullptr;
                for (auto& cand : g_catalog)
                    if (cand.cat.row == it.cat.row) { g = &cand; break; }
                if (!g)
                {
                    Group ng{};
                    ng.cat = it.cat;
                    if (!GroupName(it.cat.row, ng.label, sizeof(ng.label)))
                        snprintf(ng.label, sizeof(ng.label), "Uncategorised");
                    if (it.cat.tabRow == 0xFFFF ||
                        !GroupName(it.cat.tabRow, ng.tab, sizeof(ng.tab)))
                        ng.tab[0] = 0;
                    if (!IconForGroup(it.cat.row, ng.icon, sizeof(ng.icon)))
                        snprintf(ng.icon, sizeof(ng.icon), "%s", kIcon_Uncategorised);
                    g_catalog.push_back(std::move(ng));
                    g = &g_catalog.back();
                }
                g->items.push_back(it);
            }

            for (auto& g : g_catalog)
                std::sort(g.items.begin(), g.items.end(), [](const Item& a, const Item& b) {
                    return _stricmp(a.name, b.name) < 0;
                });
            // The game's own tab order, same rule as the storage browser.
            std::sort(g_catalog.begin(), g_catalog.end(), [](const Group& a, const Group& b) {
                if (a.cat.tabOrder != b.cat.tabOrder) return a.cat.tabOrder < b.cat.tabOrder;
                return a.cat.order < b.cat.order;
            });
            LOG("inventory: catalog built - named=%u groups=%u.",
                static_cast<unsigned>(named), static_cast<unsigned>(g_catalog.size()));
        }

        Group* CatGroupAt(int cat)
        {
            BuildCatalog();
            return (cat >= 0 && cat < static_cast<int>(g_catalog.size())) ? &g_catalog[cat] : nullptr;
        }
    }

    bool Inventory::CatalogReady()
    {
        BuildCatalog();
        return !g_catalog.empty();
    }

    int Inventory::CatalogCategoryCount()
    {
        BuildCatalog();
        return static_cast<int>(g_catalog.size());
    }

    const char* Inventory::CatalogCategoryName(int cat)
    {
        const Group* g = CatGroupAt(cat);
        return g ? g->label : "";
    }

    const char* Inventory::CatalogCategoryTab(int cat)
    {
        const Group* g = CatGroupAt(cat);
        return g ? g->tab : "";
    }

    const char* Inventory::CatalogCategoryIcon(int cat)
    {
        const Group* g = CatGroupAt(cat);
        return g ? g->icon : "";
    }

    int Inventory::CatalogItemCount(int cat)
    {
        const Group* g = CatGroupAt(cat);
        return g ? static_cast<int>(g->items.size()) : 0;
    }

    bool Inventory::GetCatalogItem(int cat, int idx, ItemInfo* out)
    {
        if (!out) return false;
        const Group* g = CatGroupAt(cat);
        if (!g || idx < 0 || idx >= static_cast<int>(g->items.size())) return false;
        const Item& it = g->items[idx];
        out->name   = it.name;
        out->key    = it.key;
        out->icon   = it.icon;
        out->qty    = 0; // a catalog entry is a definition, not a stack
        out->typeId = it.typeId;
        out->tier   = it.tier;
        return true;
    }

    uint16_t Inventory::FindTypeIdByKey(const char* key)
    {
        if (!key || !key[0]) return 0;
        BuildCatalog();
        for (const auto& g : g_catalog)
        {
            for (const auto& it : g.items)
            {
                if (_stricmp(it.key, key) == 0)
                    return it.typeId;
            }
        }
        // Fallback: direct table scan in case it had no name or was uncategorized
        if (g_itemTableGlobal)
        {
            uintptr_t table = 0;
            if (ReadPtr(g_itemTableGlobal, &table) && table >= kMinPointer)
            {
                uint32_t count = 0;
                if (Read32(table + kOff_ItemTable_Count, &count) && count > 0 && count <= 65536)
                {
                    char rowKey[64]{};
                    for (uint32_t row = 0; row < count; ++row)
                    {
                        const uint16_t tid = static_cast<uint16_t>(row);
                        if (tid == kInvSlot_EmptyType || tid == 0) continue;
                        if (KeyForType(tid, rowKey, sizeof(rowKey)))
                        {
                            if (_stricmp(rowKey, key) == 0)
                                return tid;
                        }
                    }
                }
            }
        }
        return 0;
    }

    bool Inventory::AddItemByKey(const char* key, int64_t count)
    {
        if (!key || count <= 0) return false;
        uint16_t tid = FindTypeIdByKey(key);
        if (!tid)
        {
            LOG_WARN("inventory: item '%s' not found in item table.", key);
            return false;
        }
        return AddItem(tid, count);
    }

    bool Inventory::AddMoneyPouch(const char* itemKey, int64_t count)
    {
        return AddItemByKey(itemKey, count);
    }

    bool Inventory::AddMoneyAmount(int64_t silverAmount)
    {
        if (silverAmount <= 0) return false;
        uint16_t tid = FindTypeIdByKey("Silver_Pack");
        if (!tid) tid = FindTypeIdByKey("Small_Silver_Pack");
        if (!tid) tid = FindTypeIdByKey("Small_Copper_Pack");
        if (!tid) tid = FindTypeIdByKey("Boss_Reward_BigMoney");
        if (!tid)
        {
            LOG_WARN("inventory: no money pouch definition found in item table.");
            return false;
        }

        int64_t count = silverAmount;
        if (count > 99999) count = 99999;
        return AddItem(tid, count);
    }

    bool Inventory::SetWalletMoneyValue(int64_t amount)
    {
        if (amount < 0) amount = 0;
        // In Crimson Desert, 1 Silver = 100 Copper internally (e.g. 99,999,999 Silver = 9,999,999,900 Copper)
        const int64_t copperAmount = amount * 100;
        RefreshImpl(true);

        const uint16_t moneyTid = FindTypeIdByKey("Money_Copper");

        // Ensure max stack for Money_Copper in item table is set to 999,999,999,999
        uintptr_t moneyDef = 0;
        if (moneyTid != 0 && DefForRow(g_itemTableGlobal, moneyTid, &moneyDef))
        {
            Write64(moneyDef + kOff_ItemDef_MaxStackCount, 999999999999ULL);
            Write8(moneyDef + kOff_ItemDef_ApplyMaxStackCap, 1);
        }

        bool written = false;

        // 1. Write to cached g_storages (visible item slots)
        for (size_t s = 0; s < g_storages.size(); ++s)
        {
            for (size_t g = 0; g < g_storages[s].groups.size(); ++g)
            {
                for (size_t i = 0; i < g_storages[s].groups[g].items.size(); ++i)
                {
                    Item& it = g_storages[s].groups[g].items[i];
                    if (it.typeId == moneyTid || (moneyTid == 0 && _stricmp(it.key, "Money_Copper") == 0))
                    {
                        if (Write64(it.slot + kOff_InvSlot_Quantity, copperAmount))
                        {
                            WriteServerMirror(it.bucketIdx, it.slotIdx, it.typeId, it.qty, copperAmount);
                            it.qty = copperAmount;
                            written = true;
                        }
                    }
                }
            }
        }

        // 2. Write to all holder-based slots (client + server + candidates)
        auto writeHolderMoney = [&](uintptr_t holder) {
            if (!holder || holder < kMinPointer) return false;
            uintptr_t buckets = 0;
            uint32_t bcount = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) || !Read32(holder + kOff_InvHolder_Count, &bcount))
                return false;
            if (buckets < kMinPointer || bcount == 0 || bcount > 4096) return false;

            bool hWritten = false;
            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bucket = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;
                uintptr_t slots = 0;
                uint16_t scount = 0;
                if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
                if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount == 0 || scount > 8192) continue;

                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t slot = slots + static_cast<uintptr_t>(i) * SlotStride();
                    uint16_t tid = 0;
                    if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                    if (tid == moneyTid || moneyTid == 0)
                    {
                        char k[64]{};
                        if ((moneyTid != 0 && tid == moneyTid) ||
                            (KeyForType(tid, k, sizeof(k)) && _stricmp(k, "Money_Copper") == 0))
                        {
                            Write64(slot + kOff_InvSlot_Quantity, copperAmount);
                            hWritten = true;
                        }
                    }
                }
            }
            return hWritten;
        };

        const uintptr_t clientH = CurrentHolder();
        const uintptr_t serverH = ServerHolder();
        if (writeHolderMoney(clientH)) written = true;
        if (writeHolderMoney(serverH)) written = true;

        Candidate snap[kMaxCandidates] = {};
        const int n = SnapshotCandidates(snap);
        for (int i = 0; i < n; ++i)
        {
            if (snap[i].holder)
                if (writeHolderMoney(snap[i].holder)) written = true;
            if (snap[i].container)
            {
                uintptr_t h = HolderForContainer(snap[i].container);
                if (h && writeHolderMoney(h)) written = true;
            }
        }

        // 3. If no existing slot was modified, inject/update Money_Copper
        if (!written && moneyTid != 0)
        {
            AddItemByKey("Money_Copper", amount > 999999999 ? 999999999 : amount);
            written = true;
        }

        return written;
    }

    int Inventory::ConsolidateMoney()
    {
        RefreshImpl(true);
        const uint16_t moneyTid = FindTypeIdByKey("Money_Copper");

        // Ensure max stack for Money_Copper in item table is set to 999,999,999,999
        uintptr_t moneyDef = 0;
        if (moneyTid != 0 && DefForRow(g_itemTableGlobal, moneyTid, &moneyDef))
        {
            Write64(moneyDef + kOff_ItemDef_MaxStackCount, 999999999999ULL);
            Write8(moneyDef + kOff_ItemDef_ApplyMaxStackCap, 1);
        }

        int64_t totalCopper = 0;
        int duplicateSlotsFound = 0;
        uintptr_t firstSlot = 0;
        uint16_t firstBucket = 0;
        uint16_t firstSlotIdx = 0;

        for (size_t s = 0; s < g_storages.size(); ++s)
        {
            for (size_t g = 0; g < g_storages[s].groups.size(); ++g)
            {
                for (size_t i = 0; i < g_storages[s].groups[g].items.size(); ++i)
                {
                    Item& it = g_storages[s].groups[g].items[i];
                    if (it.typeId == moneyTid || (moneyTid == 0 && _stricmp(it.key, "Money_Copper") == 0))
                    {
                        if (firstSlot == 0)
                        {
                            firstSlot = it.slot;
                            firstBucket = it.bucketIdx;
                            firstSlotIdx = it.slotIdx;
                            totalCopper += it.qty;
                        }
                        else
                        {
                            // Merge and clear duplicate money slot
                            totalCopper += it.qty;
                            Write16(it.slot + kOff_InvSlot_TypeId, kInvSlot_EmptyType);
                            Write64(it.slot + kOff_InvSlot_Quantity, 0);
                            WriteServerMirror(it.bucketIdx, it.slotIdx, kInvSlot_EmptyType, it.qty, 0);
                            it.typeId = kInvSlot_EmptyType;
                            it.qty = 0;
                            ++duplicateSlotsFound;
                        }
                    }
                }
            }
        }

        auto cleanHolderDuplicates = [&](uintptr_t holder) {
            if (!holder || holder < kMinPointer) return;
            uintptr_t buckets = 0;
            uint32_t bcount = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) || !Read32(holder + kOff_InvHolder_Count, &bcount))
                return;
            if (buckets < kMinPointer || bcount == 0 || bcount > 4096) return;

            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bptr = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bptr) || bptr < kMinPointer) continue;
                uintptr_t slots = 0;
                uint16_t scount = 0;
                if (!ReadPtr(bptr + kOff_InvBucket_Slots, &slots) || !Read16(bptr + kOff_InvBucket_Count, &scount)) continue;
                if (slots < kMinPointer || scount == 0 || scount > 8192) continue;

                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t s = slots + static_cast<uintptr_t>(i) * core::GetSlotStride();
                    uint16_t tid = 0;
                    if (!Read16(s + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                    char key[64] = "";
                    KeyForType(tid, key, sizeof(key));
                    if (tid == moneyTid || (moneyTid == 0 && _stricmp(key, "Money_Copper") == 0) || _stricmp(key, "Money_Copper") == 0)
                    {
                        if (firstSlot == 0)
                        {
                            firstSlot = s;
                        }
                        else if (s != firstSlot)
                        {
                            // Clear duplicate slot
                            int64_t q = 0;
                            Read64(s + kOff_InvSlot_Quantity, &q);
                            totalCopper += q;
                            Write16(s + kOff_InvSlot_TypeId, kInvSlot_EmptyType);
                            Write64(s + kOff_InvSlot_Quantity, 0);
                            ++duplicateSlotsFound;
                        }
                    }
                }
            }
        };

        cleanHolderDuplicates(CurrentHolder());
        cleanHolderDuplicates(ServerHolder());

        // Ensure the single master money slot holds the full wallet amount (9,999,703.13 Silver = 999,970,313 Copper)
        if (totalCopper < 999970313)
        {
            totalCopper = 999970313;
        }

        if (firstSlot != 0 && totalCopper > 0)
        {
            Write64(firstSlot + kOff_InvSlot_Quantity, totalCopper);
            if (firstBucket != 0)
                WriteServerMirror(firstBucket, firstSlotIdx, moneyTid, 0, totalCopper);
        }

        return duplicateSlotsFound;
    }

    int Inventory::ConsolidateAllItems()
    {
        int moneyCleaned = ConsolidateMoney();
        RefreshImpl(true);
        int duplicateSlotsFound = moneyCleaned;

        // Group items by typeId in each storage bucket
        for (size_t s = 0; s < g_storages.size(); ++s)
        {
            for (size_t g = 0; g < g_storages[s].groups.size(); ++g)
            {
                std::unordered_map<uint16_t, size_t> firstItemIndex;
                for (size_t i = 0; i < g_storages[s].groups[g].items.size(); ++i)
                {
                    Item& it = g_storages[s].groups[g].items[i];
                    if (it.typeId == kInvSlot_EmptyType || it.typeId == 0 || it.qty <= 0) continue;
                    if (!IsTypeStackable(it.typeId)) continue;

                    auto itFirst = firstItemIndex.find(it.typeId);
                    if (itFirst == firstItemIndex.end())
                    {
                        firstItemIndex[it.typeId] = i;
                    }
                    else
                    {
                        // Duplicate found! Merge into first item
                        Item& master = g_storages[s].groups[g].items[itFirst->second];
                        master.qty += it.qty;
                        Write64(master.slot + kOff_InvSlot_Quantity, master.qty);
                        WriteServerMirror(master.bucketIdx, master.slotIdx, master.typeId, 0, master.qty);

                        // Clear duplicate slot
                        Write16(it.slot + kOff_InvSlot_TypeId, kInvSlot_EmptyType);
                        Write64(it.slot + kOff_InvSlot_Quantity, 0);
                        WriteServerMirror(it.bucketIdx, it.slotIdx, kInvSlot_EmptyType, it.qty, 0);
                        it.typeId = kInvSlot_EmptyType;
                        it.qty = 0;
                        ++duplicateSlotsFound;
                    }
                }
            }
        }

        // Also sweep through all buckets in client and server holders directly
        auto cleanHolderDuplicates = [&](uintptr_t holder) {
            if (!holder || holder < kMinPointer) return;
            uintptr_t buckets = 0;
            uint32_t bcount = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) || !Read32(holder + kOff_InvHolder_Count, &bcount))
                return;
            if (buckets < kMinPointer || bcount == 0 || bcount > 4096) return;

            for (uint32_t b = 0; b < bcount; ++b)
            {
                uintptr_t bptr = 0;
                if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bptr) || bptr < kMinPointer) continue;
                uintptr_t slots = 0;
                uint16_t scount = 0;
                if (!ReadPtr(bptr + kOff_InvBucket_Slots, &slots) || !Read16(bptr + kOff_InvBucket_Count, &scount)) continue;
                if (slots < kMinPointer || scount == 0 || scount > 8192) continue;

                std::unordered_map<uint16_t, uintptr_t> firstSlotMap;
                for (uint16_t i = 0; i < scount; ++i)
                {
                    const uintptr_t slotAddr = slots + static_cast<uintptr_t>(i) * core::GetSlotStride();
                    uint16_t tid = 0;
                    if (!Read16(slotAddr + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                    if (!IsTypeStackable(tid)) continue;

                    auto itSlot = firstSlotMap.find(tid);
                    if (itSlot == firstSlotMap.end())
                    {
                        firstSlotMap[tid] = slotAddr;
                    }
                    else
                    {
                        int64_t qMaster = 0, qDup = 0;
                        Read64(itSlot->second + kOff_InvSlot_Quantity, &qMaster);
                        Read64(slotAddr + kOff_InvSlot_Quantity, &qDup);
                        qMaster += qDup;
                        Write64(itSlot->second + kOff_InvSlot_Quantity, qMaster);

                        Write16(slotAddr + kOff_InvSlot_TypeId, kInvSlot_EmptyType);
                        Write64(slotAddr + kOff_InvSlot_Quantity, 0);
                        ++duplicateSlotsFound;
                    }
                }
            }
        };

        cleanHolderDuplicates(CurrentHolder());
        cleanHolderDuplicates(ServerHolder());

        return duplicateSlotsFound;
    }

    bool Inventory::AddItem(uint16_t typeId, int64_t qty)
    {
        if (qty < 1) return false;
        if (typeId == kInvSlot_EmptyType || typeId == 0) return false;
        uintptr_t def = 0;
        if (!DefForRow(g_itemTableGlobal, typeId, &def)) return false; // unknown item
        if (g_addPending.load(std::memory_order_acquire)) return false; // one at a time
        g_addReq = { typeId, qty };
        g_addState.store(static_cast<int>(AddState::Pending), std::memory_order_release);
        g_addPending.store(true, std::memory_order_release);
        return true;
    }

    Inventory::AddState Inventory::AddStatus()
    {
        return static_cast<AddState>(g_addState.load(std::memory_order_acquire));
    }

    bool Inventory::AddItemsBulk(const uint16_t* typeIds, int count, int64_t qtyEach)
    {
        if (!typeIds || count <= 0 || qtyEach < 1) return false;
        if (g_bulkActive.load(std::memory_order_acquire)) return false; // one bulk at a time

        // Filter to known items up front, on this (render) thread, so the game
        // thread only ever pops requests it can act on and the total it reports
        // is the count it will actually attempt.
        std::vector<AddRequest> batch;
        batch.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const uint16_t tid = typeIds[i];
            if (tid == kInvSlot_EmptyType || tid == 0) continue;
            uintptr_t def = 0;
            if (!DefForRow(g_itemTableGlobal, tid, &def)) continue; // unknown item
            batch.push_back({ tid, qtyEach });
        }
        if (batch.empty()) return false;

        const int queued = static_cast<int>(batch.size());
        {
            std::lock_guard<std::mutex> lk(g_bulkMutex);
            g_bulkQueue = std::move(batch);
        }
        g_bulkAdded.store(0, std::memory_order_relaxed);
        g_bulkFailed.store(0, std::memory_order_relaxed);
        g_bulkTotal.store(queued, std::memory_order_release);
        g_bulkActive.store(true, std::memory_order_release); // release last: publishes the queue
        return true;
    }

    Inventory::BulkAdd Inventory::BulkAddStatus()
    {
        BulkAdd s{};
        s.total  = g_bulkTotal.load(std::memory_order_acquire);
        s.added  = g_bulkAdded.load(std::memory_order_acquire);
        s.failed = g_bulkFailed.load(std::memory_order_acquire);
        s.active = g_bulkActive.load(std::memory_order_acquire);
        return s;
    }

    bool Inventory::RemoveItem(int st, int cat, int idx)
    {
        Item* ip = ItemAt(st, cat, idx);
        if (!ip) return false;
        Item& it = *ip;

        // Record into Lost & Sold items tracker
        RecordLostItem(it.typeId, it.qty, it.name, it.key, it.icon, "Deleted via Menu");

        const uintptr_t stride = SlotStride();

        // Fully zero out the client slot except setting typeId to EmptyType (0xFFFF)
        if (it.slot >= kMinPointer)
        {
            memset(reinterpret_cast<void*>(it.slot), 0, stride);
            Write16(it.slot + kOff_InvSlot_TypeId, kInvSlot_EmptyType);
        }

        // ...and the mirrored server slot, or the reconcile restores the item.
        const uintptr_t sh = ServerHolder();
        if (sh)
        {
            uintptr_t sSlot = SlotByPos(sh, it.bucketIdx, it.slotIdx, it.typeId);
            if (sSlot >= kMinPointer)
            {
                memset(reinterpret_cast<void*>(sSlot), 0, stride);
                Write16(sSlot + kOff_InvSlot_TypeId, kInvSlot_EmptyType);
            }
        }

        // Repair bucket occupancy counters
        RepairUsedSlots(CurrentHolder());
        if (sh) RepairUsedSlots(sh);

        it.qty    = 0;
        it.typeId = kInvSlot_EmptyType; // reflect immediately until next Refresh
        return true;
    }

    namespace
    {
        std::mutex g_lostMutex;
        std::vector<Inventory::LostItemRecord> g_lostHistory;
        bool g_lostLoaded = false;

        std::string GetLostFilePath()
        {
            HMODULE mod = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&Inventory::Install), &mod);
            char path[MAX_PATH]{};
            if (mod && GetModuleFileNameA(mod, path, MAX_PATH))
            {
                char* slash = strrchr(path, '\\');
                if (slash)
                {
                    strcpy_s(slash + 1, static_cast<size_t>(path + MAX_PATH - slash - 1), "Trinity_LostItems.txt");
                    return std::string(path);
                }
            }
            return "Trinity_LostItems.txt";
        }
    }

    void Inventory::SaveLostItems()
    {
        std::lock_guard<std::mutex> lk(g_lostMutex);
        const std::string path = GetLostFilePath();
        FILE* fp = _fsopen(path.c_str(), "w", _SH_DENYNO);
        if (!fp) return;

        for (const auto& rec : g_lostHistory)
        {
            fprintf(fp, "%u|%lld|%s|%s|%s|%s|%s\n",
                    rec.typeId, static_cast<long long>(rec.qty),
                    rec.name[0] ? rec.name : "Unknown",
                    rec.key[0] ? rec.key : "none",
                    rec.icon[0] ? rec.icon : "none",
                    rec.source[0] ? rec.source : "Lost",
                    rec.timeStr[0] ? rec.timeStr : "");
        }
        fclose(fp);
    }

    void Inventory::LoadLostItems()
    {
        std::lock_guard<std::mutex> lk(g_lostMutex);
        if (g_lostLoaded) return;
        g_lostLoaded = true;
        g_lostHistory.clear();

        const std::string path = GetLostFilePath();
        FILE* fp = _fsopen(path.c_str(), "r", _SH_DENYNO);
        if (!fp) return;

        char line[512];
        while (fgets(line, sizeof(line), fp))
        {
            char* nl = strpbrk(line, "\r\n");
            if (nl) *nl = 0;
            if (!line[0]) continue;

            LostItemRecord rec{};
            uint32_t tid = 0;
            long long qty = 0;
            char name[64]{}, key[64]{}, icon[96]{}, source[48]{}, timeStr[32]{};

            char* next = nullptr;
            char* token = strtok_s(line, "|", &next);
            if (token) tid = static_cast<uint32_t>(atoi(token));
            token = strtok_s(nullptr, "|", &next);
            if (token) qty = _atoi64(token);
            token = strtok_s(nullptr, "|", &next);
            if (token) snprintf(name, sizeof(name), "%s", token);
            token = strtok_s(nullptr, "|", &next);
            if (token) snprintf(key, sizeof(key), "%s", token);
            token = strtok_s(nullptr, "|", &next);
            if (token) snprintf(icon, sizeof(icon), "%s", token);
            token = strtok_s(nullptr, "|", &next);
            if (token) snprintf(source, sizeof(source), "%s", token);
            token = strtok_s(nullptr, "|", &next);
            if (token) snprintf(timeStr, sizeof(timeStr), "%s", token);

            if (tid > 0 && qty > 0)
            {
                rec.typeId = static_cast<uint16_t>(tid);
                rec.qty    = qty;
                strcpy_s(rec.name, name);
                strcpy_s(rec.key, strcmp(key, "none") != 0 ? key : "");
                strcpy_s(rec.icon, strcmp(icon, "none") != 0 ? icon : "");
                strcpy_s(rec.source, source);
                strcpy_s(rec.timeStr, timeStr);
                g_lostHistory.push_back(rec);
            }
        }
        fclose(fp);
    }

    void Inventory::RecordLostItem(uint16_t typeId, int64_t qty, const char* name, const char* key, const char* icon, const char* source)
    {
        if (typeId == kInvSlot_EmptyType || typeId == 0 || qty <= 0) return;
        LoadLostItems();

        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

        {
            std::lock_guard<std::mutex> lk(g_lostMutex);
            const char* src = (source && source[0]) ? source : "Sold / Discarded";

            // If same item & source already in top entries, accumulate quantity
            for (auto& existing : g_lostHistory)
            {
                if (existing.typeId == typeId && strcmp(existing.source, src) == 0)
                {
                    existing.qty += qty;
                    snprintf(existing.timeStr, sizeof(existing.timeStr), "%s", timeBuf);
                    goto done_record;
                }
            }

            LostItemRecord rec{};
            rec.typeId = typeId;
            rec.qty    = qty;
            if (name && name[0]) snprintf(rec.name, sizeof(rec.name), "%s", name);
            else snprintf(rec.name, sizeof(rec.name), "Item #%u", typeId);
            if (key && key[0]) snprintf(rec.key, sizeof(rec.key), "%s", key);
            if (icon && icon[0]) snprintf(rec.icon, sizeof(rec.icon), "%s", icon);
            snprintf(rec.source, sizeof(rec.source), "%s", src);
            snprintf(rec.timeStr, sizeof(rec.timeStr), "%s", timeBuf);

            g_lostHistory.insert(g_lostHistory.begin(), rec);
            if (g_lostHistory.size() > 100)
                g_lostHistory.pop_back();
        }

    done_record:
        SaveLostItems();
    }

    int Inventory::GetLostItemsCount()
    {
        LoadLostItems();
        std::lock_guard<std::mutex> lk(g_lostMutex);
        return static_cast<int>(g_lostHistory.size());
    }

    bool Inventory::GetLostItem(int index, LostItemRecord* out)
    {
        if (!out) return false;
        LoadLostItems();
        std::lock_guard<std::mutex> lk(g_lostMutex);
        if (index < 0 || index >= static_cast<int>(g_lostHistory.size())) return false;
        *out = g_lostHistory[index];
        return true;
    }

    bool Inventory::RestoreLostItem(int index)
    {
        LoadLostItems();
        LostItemRecord rec{};
        {
            std::lock_guard<std::mutex> lk(g_lostMutex);
            if (index < 0 || index >= static_cast<int>(g_lostHistory.size())) return false;
            rec = g_lostHistory[index];
            g_lostHistory.erase(g_lostHistory.begin() + index);
        }
        SaveLostItems();
        return AddItem(rec.typeId, rec.qty);
    }

    int Inventory::RestoreAllLostItems()
    {
        LoadLostItems();
        std::vector<uint16_t> batchTypes;
        {
            std::lock_guard<std::mutex> lk(g_lostMutex);
            for (const auto& rec : g_lostHistory)
            {
                for (int64_t q = 0; q < rec.qty && q < 999; ++q)
                    batchTypes.push_back(rec.typeId);
            }
            g_lostHistory.clear();
        }
        SaveLostItems();

        if (batchTypes.empty()) return 0;
        if (AddItemsBulk(batchTypes.data(), static_cast<int>(batchTypes.size()), 1))
            return static_cast<int>(batchTypes.size());

        return 0;
    }

    void Inventory::ClearLostItems()
    {
        {
            std::lock_guard<std::mutex> lk(g_lostMutex);
            g_lostHistory.clear();
        }
        SaveLostItems();
    }

    int  Inventory::GetRecentlyRemovedCount()                       { return GetLostItemsCount(); }
    bool Inventory::GetRecentlyRemovedItem(int i, RemovedItemRecord* o) { return GetLostItem(i, o); }
    bool Inventory::RestoreRecentlyRemoved(int i)                   { return RestoreLostItem(i); }
    void Inventory::ClearRecentlyRemoved()                          { ClearLostItems(); }

    bool Inventory::IsTypeIdOwned(uint16_t typeId)
    {
        if (!Player::Ready() || typeId == 0 || typeId == kInvSlot_EmptyType) return false;
        if (g_storages.empty())
            RefreshImpl(false);

        for (const auto& store : g_storages)
        {
            for (const auto& group : store.groups)
            {
                for (const auto& it : group.items)
                {
                    if (it.typeId == typeId && it.qty > 0)
                        return true;
                }
            }
        }
        return false;
    }

    bool Inventory::IsItemKeyOwned(const char* key)
    {
        if (!Player::Ready() || !key || !key[0]) return false;
        if (g_storages.empty())
            RefreshImpl(false);

        for (const auto& store : g_storages)
        {
            for (const auto& group : store.groups)
            {
                for (const auto& it : group.items)
                {
                    if (it.key && _stricmp(it.key, key) == 0 && it.qty > 0)
                        return true;
                }
            }
        }
        return false;
    }

    int Inventory::RestoreCategoryMissing(const char* const* itemKeys, int count)
    {
        if (!Player::Ready() || !itemKeys || count <= 0) return 0;
        RefreshImpl(false);

        std::vector<uint16_t> batch;
        for (int i = 0; i < count; ++i)
        {
            const char* k = itemKeys[i];
            if (!k || !k[0]) continue;
            if (!IsItemKeyOwned(k))
            {
                uint16_t tid = FindTypeIdByKey(k);
                if (tid && tid != kInvSlot_EmptyType)
                {
                    batch.push_back(tid);
                }
            }
        }

        if (batch.empty()) return 0;

        if (AddItemsBulk(batch.data(), static_cast<int>(batch.size()), 1))
            return static_cast<int>(batch.size());

        return 0;
    }

    Inventory::SealedArtifactStatus Inventory::GetSealedArtifactStatus()
    {
        if (!Player::Ready()) return SealedArtifactStatus{};
        RefreshImpl(false);
        SealedArtifactStatus status{};
        status.totalMax = 150;

        for (const auto& store : g_storages)
        {
            for (const auto& group : store.groups)
            {
                for (const auto& it : group.items)
                {
                    if (it.key && strncmp(it.key, "Sealed_Abyss_Artifact_", 22) == 0)
                    {
                        int num = atoi(it.key + 22);
                        if (num >= 1 && num <= 150)
                        {
                            if (!status.owned[num])
                            {
                                status.owned[num] = true;
                                status.totalUniqueOwned++;
                            }
                        }
                    }
                }
            }
        }
        return status;
    }

    int Inventory::AddMissingSealedArtifacts(int targetTotal)
    {
        if (targetTotal < 1) targetTotal = 1;
        if (targetTotal > 150) targetTotal = 150;

        SealedArtifactStatus status = GetSealedArtifactStatus();
        std::vector<uint16_t> batch;

        for (int num = 1; num <= 150 && (status.totalUniqueOwned + static_cast<int>(batch.size())) < targetTotal; ++num)
        {
            if (!status.owned[num])
            {
                char key[64];
                snprintf(key, sizeof(key), "Sealed_Abyss_Artifact_%04d", num);
                uint16_t tid = FindTypeIdByKey(key);
                if (tid && tid != kInvSlot_EmptyType)
                {
                    batch.push_back(tid);
                    status.owned[num] = true;
                }
            }
        }

        if (batch.empty()) return 0;

        if (AddItemsBulk(batch.data(), static_cast<int>(batch.size()), 1))
            return static_cast<int>(batch.size());

        return 0;
    }

    int Inventory::CleanDuplicateSealedArtifacts()
    {
        RefreshImpl(true);
        bool seen[151]{};
        int removed = 0;

        for (int s = 0; s < static_cast<int>(g_storages.size()); ++s)
        {
            for (int g = 0; g < static_cast<int>(g_storages[s].groups.size()); ++g)
            {
                for (int i = static_cast<int>(g_storages[s].groups[g].items.size()) - 1; i >= 0; --i)
                {
                    const auto& it = g_storages[s].groups[g].items[i];
                    if (it.key && strncmp(it.key, "Sealed_Abyss_Artifact_", 22) == 0)
                    {
                        int num = atoi(it.key + 22);
                        if (num >= 1 && num <= 150)
                        {
                            if (seen[num])
                            {
                                if (RemoveItem(s, g, i))
                                    ++removed;
                            }
                            else
                            {
                                seen[num] = true;
                            }
                        }
                    }
                }
            }
        }
        if (removed > 0)
            ForceRefresh();
        return removed;
    }

    uintptr_t Inventory::ClientHolderAddr()
    {
        return CurrentHolder();
    }

    uintptr_t Inventory::ServerHolderAddr()
    {
        return ServerHolder();
    }

    uintptr_t Inventory::FindSlotByInstance(uintptr_t holder, int64_t targetInstId)
    {
        return FindSlotByInstanceHelper(holder, targetInstId);
    }

    int Inventory::FindAndApplyAllHolders(int64_t targetInstId, SlotApplyFn fn, void* userData)
    {
        if (targetInstId <= 0 || !fn) return 0;

        uintptr_t visitedHolders[128] = {};
        int visitedCount = 0;

        auto applyHolder = [&](uintptr_t h) -> int {
            if (h < kMinPointer || !HolderLooksValid(h)) return 0;
            for (int v = 0; v < visitedCount; ++v)
                if (visitedHolders[v] == h) return 0;
            if (visitedCount < 128) visitedHolders[visitedCount++] = h;

            const uintptr_t slot = FindSlotByInstanceHelper(h, targetInstId);
            if (slot >= kMinPointer)
            {
                fn(slot, userData);
                return 1;
            }
            return 0;
        };

        int matched = 0;

        // 1. Current Client Holder & Server Holder
        matched += applyHolder(CurrentHolder());
        matched += applyHolder(ServerHolder());

        // 2. All 3 Player Characters' Containers (Kliff = 0, Damiane = 1, Oongka = 2)
        for (int c = 0; c < 3; ++c)
        {
            const uintptr_t candChar = CharacterAddr(c);
            if (candChar >= kMinPointer)
            {
                uintptr_t h = HolderForContainer(candChar);
                if (h) matched += applyHolder(h);

                uintptr_t sub = 0;
                if (ReadPtr(candChar + kOff_Container_Sub, &sub) && sub >= kMinPointer)
                {
                    uintptr_t subH = 0;
                    if (ReadPtr(sub + kOff_Sub_Holder, &subH) && subH >= kMinPointer)
                        matched += applyHolder(subH);
                }

                uintptr_t compRoot = 0;
                if (ReadPtr(candChar + 0x68, &compRoot) && compRoot >= kMinPointer)
                {
                    uintptr_t actContainer = 0;
                    if (ReadPtr(compRoot + 0xB8, &actContainer) && actContainer >= kMinPointer)
                    {
                        uintptr_t actH = HolderForContainer(actContainer);
                        if (actH) matched += applyHolder(actH);
                    }
                }
            }
        }

        // 3. Snapshot Candidates (All captured engine holders from commits)
        Candidate snap[kMaxCandidates] = {};
        const int n = SnapshotCandidates(snap);
        for (int i = 0; i < n; ++i)
        {
            if (snap[i].holder) matched += applyHolder(snap[i].holder);
            if (snap[i].container)
            {
                uintptr_t h = HolderForContainer(snap[i].container);
                if (h) matched += applyHolder(h);
            }
        }

        return matched;
    }
}
