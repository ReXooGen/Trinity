#include "dye.h"
#include "dye_record.h"
#include "mount_equipment.h"
#include "../core/mod.h"

#include <Windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <future>
#include <chrono>

#include "offsets.h"
#include "player.h"
#include "dye_data.h"
#include "dye_slots_table.h"
#include "inventory.h"
#include "equipment.h"
#include "equipment_logic.h"
#include "equipment_table.h"
#include "../core/logger.h"
#include "../mem/hooks.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../core/version_detect.h"

// The dyehouse from the menu. All the RE background lives in offsets.h
// (the "Armor dye" section); this file is the plumbing:
//
//   Component walk   -> each realm's equip component, straight off that
//                       realm's player character (*(*(actor+0x68)+0x38)).
//                       The client's renders; the server's is the durable one.
//   Render discovery -> native client registry / owner roots, with RTTI,
//                       owner backlink and exact item identity validation.
//   DyeApplyBatch    -> the client's own dye-ack handler, called directly
//                       with a crafted batch: it upserts the records into the
//                       equipped entry and live-updates the rendered
//                       materials. This is the whole "apply" - no re-equip.
//   DyeUpsert        -> the engine's record upsert, used to write the same
//                       records into the SERVER realm's equip entry (plain
//                       data, no render calls) so the dye persists.
//   DyeVisualSet     -> the engine's per-slot visual material update leaf
//                       (sub_1409164c0), called per record by DyeApplyBatch.
//                       Works universally on companion bodies (Damiane / Oongka)
//                       without requiring a local player controller (possessor).
//
// Worn gear has no inventory slot to mirror onto - the equip table is where a
// worn item lives. See the persistence note in offsets.h.

namespace trinity::game
{
    namespace
    {
        using namespace trinity::mem;
        DyeProfileStore s_profiles;
        std::future<bool> s_profileSave;

        // --- Resolved engine entry points --------------------------------
        using EquipBatch_t    = void* (__fastcall*)(void*, void*, void*, void*);
        using DyeApplyBatch_t = int*  (__fastcall*)(void*, int*, void*);
        using DyeUpsert_t     = void* (__fastcall*)(void*, const void*);

        // Per-slot RENDER leaves (see kSig_DyeVisualSet / kSig_DyeVisualClear
        // in offsets.h). Possession-independent: safe on companions.
        using DyeVisualSet_t   = void* (__fastcall*)(void* comp, void* entry,
                                                     const void* rec, uint16_t tag,
                                                     uint64_t stackCh, uint64_t stackZero);
        using DyeVisualClear_t = void* (__fastcall*)(void* comp, void* entry,
                                                     uint16_t tag, uint8_t channel,
                                                     uint64_t stackZero);
        // Data remove-by-channel on an entry's dye vector.
        using DyeRecRemove_t   = void  (__fastcall*)(void* entry, uint8_t channel);

        // Historical signature only: 0x142B41E60 is NOT a TU 2.02 dye applier.
        using DyeApplySlot_t   = void* (__fastcall*)(void* comp, uint16_t slotTag,
                                                     int channel, const uint8_t* rec);

        // DyeApplyBatch's success-tail toast ("Item dyed successfully.",
        // sub_140926DC0): called once per batch, from the global UI object.
        using DyeNotify_t      = void* (__fastcall*)(void* ui, void* arg, int code);

        EquipBatch_t     oEquipBatch      = nullptr;
        void*            g_equipTarget    = nullptr;
        DyeApplyBatch_t  g_dyeApply       = nullptr;
        DyeUpsert_t      g_dyeUpsert       = nullptr;
        DyeVisualSet_t   g_dyeVisualSet   = nullptr;
        DyeVisualClear_t g_dyeVisualClear = nullptr;
        DyeRecRemove_t   g_dyeRecRemove   = nullptr;
        DyeApplySlot_t   g_dyeApplySlot   = nullptr;
        DyeNotify_t      oDyeNotify       = nullptr;
        void*            g_dyeNotifyTarget = nullptr;

        // TU 2.02 dye-ack dispatcher @ 0x140B4807E. The shorter prefix also
        // matches 0x140B689CE; the error-store (89 06) distinguishes this site.
        // Resolve the RIP global, never an absolute address or a profile root.
        constexpr const char* kSig_DyeClientRegistry =
            "48 8B 05 ?? ?? ?? ?? 4C 8B 60 38 48 8B 45 80 4C 8B 48 08 "
            "48 8D 05 ?? ?? ?? ?? 41 B8 04 00 00 00 48 8D 95 F0 07 00 00 "
            "48 8D 4D 80 4C 3B C8 75 07 E8 ?? ?? ?? ?? EB 03 41 FF D1 "
            "84 C0 75 15 8B 05 ?? ?? ?? ?? 89 06 4C 89 75 80 48 89 5D 80 "
            "E9 ?? ?? ?? ?? 8B 95 F0 07 00 00 85 D2 74 2F";
        uintptr_t g_clientRegistryGlobal = 0;

        // Set while the auto-restore replays batches: the toast hook drops the
        // "Item dyed successfully" popup for restore-driven applies. Only the
        // user's own dye action should toast.
        std::atomic<int> g_suppressDyeToast{ 0 };

        void DyeWatchFile(const char* fmt, ...)
        {
            char dir[MAX_PATH]{};
            HMODULE self = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&DyeWatchFile), &self);
            if (!self || !GetModuleFileNameA(self, dir, MAX_PATH)) return;
            char* slash = strrchr(dir, '\\');
            if (!slash) return;
            snprintf(slash + 1, static_cast<size_t>(dir + MAX_PATH - slash - 1),
                     "Trinity_DyeWatch.txt");
            FILE* f = fopen(dir, "a");
            if (!f) return;
            SYSTEMTIME st{};
            GetLocalTime(&st);
            fprintf(f, "%02u:%02u:%02u ", st.wHour, st.wMinute, st.wSecond);
            va_list ap;
            va_start(ap, fmt);
            vfprintf(f, fmt, ap);
            va_end(ap);
            fputc('\n', f);
            fflush(f);
            fclose(f);
        }

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
                "AbyssGear", "Item_Skill", "Riding_Deer_Horn",
                "Bottle", "bottle", "Water", "water", "Arrow", "arrow", "Quiver", "quiver"
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

        const char* MountSlotName(HorseSlotType type)
        {
            switch (type)
            {
            case HorseSlotType::Chamfron:   return "Chamfron";
            case HorseSlotType::HorseArmor: return "Horse Armor";
            case HorseSlotType::Saddle:     return "Saddle";
            case HorseSlotType::Stirrups:   return "Stirrups";
            case HorseSlotType::Horseshoes: return "Horseshoes";
            default:                        return "Mount Gear";
            }
        }

        uint32_t HashPrefabLower(const char* s, size_t n)
        {
            uint32_t h = 2166136261u;
            for (size_t i = 0; i < n; ++i)
            {
                uint8_t c = static_cast<uint8_t>(s[i]);
                if (c >= 'A' && c <= 'Z') c += 32;
                h = (h ^ c) * 16777619u;
            }
            return h;
        }

        int LookupExactZoneForIcon(const char* icon)
        {
            if (!icon || !icon[0]) return 0;
            const char* p = nullptr;
            for (const char* c = icon; *c; ++c)
            {
                if ((*c == 'p' || *c == 'P') && _strnicmp(c, "prefab_", 7) == 0)
                {
                    p = c + 7;
                    break;
                }
            }
            if (!p || !p[0]) return 0;

            size_t len = strlen(p);
            for (int strip = 0; strip < 4 && len > 3; ++strip)
            {
                const int cnt = LookupExactZoneCount(HashPrefabLower(p, len));
                if (cnt > 0) return cnt;
                size_t cut = len;
                while (cut > 0 && p[cut - 1] != '_') --cut;
                if (cut == 0) break;
                len = cut - 1;
            }
            return 0;
        }

        int MaxZonesForSlot(int targetMode, uint16_t tag, const char* itemName, const char* icon)
        {
            (void)targetMode; (void)tag; (void)itemName; (void)icon;
            return 12; // Full 12 zones supported for all player and mount gear
        }

        // The hook's captured components
        std::atomic<uintptr_t> g_comp{ 0 };
        std::atomic<uintptr_t> g_mountComp{ 0 };

        // Tick of the last validated equipment-batch capture. Gear
        // changes rebuild the GPU material instances back to natural colors
        // while dye records persist as data, so the auto-restore pass forces
        // a bounded visual replay right after each change.
        std::atomic<ULONGLONG> s_lastEquipChangeMs{ 0 };

        inline bool IsValidCanonicalPtr(uintptr_t p)
        {
            return IsEquipmentPointer(p);
        }

        bool ReadEquipTable(uintptr_t comp, uintptr_t& outArray, uint32_t& outCount,
                            uintptr_t* outStride = nullptr, uintptr_t* outSlotTag = nullptr,
                            uintptr_t* outDyeData = nullptr, uintptr_t* outDyeCount = nullptr)
        {
            const EquipTableDesc table = ReadNativeEquipmentTable(comp);
            if (!table.valid) return false;

            outArray = table.array;
            outCount = table.count;
            if (outStride) *outStride = table.stride;
            if (outSlotTag) *outSlotTag = table.tagOffset;
            if (outDyeData) *outDyeData = 0x78;
            if (outDyeCount) *outDyeCount = 0x80;
            return true;
        }

        bool CompValid(uintptr_t comp)
        {
            if (!IsValidCanonicalPtr(comp)) return false;
            uintptr_t array = 0;
            uint32_t  count = 0;
            return ReadEquipTable(comp, array, count);
        }

        bool IsRenderComp(uintptr_t comp, uintptr_t* outContext = nullptr)
        {
            if (outContext) *outContext = 0;
            if (!IsNativeClientEquip(comp) || !CompValid(comp)) return false;
            // DyeVisualSet @ 0x1409170F1 and the part lookup @ 0x14091762D:
            // context=[[sub+40]+130]; 142C5A600 -> 150687B10 consumes
            // [context] -> +8 -> +78 -> part map (+58 array, +60 count).
            // Validate VALUES, not merely readable fields (the failing server
            // object had 0x3F80000000000000 in the supposed context field).
            uintptr_t act = 0, sub = 0, back = 0, rcx20 = 0, rcx40 = 0;
            uintptr_t context = 0, handle = 0, resource = 0, parts = 0, array = 0, probe = 0;
            uint16_t characterKey = 0;
            uint32_t count = 0;
            if (!ReadPtr(comp + 8, &act) || !IsValidCanonicalPtr(act)) return false;
            if (!ReadPtr(act + 0x68, &sub) || !IsValidCanonicalPtr(sub)) return false;
            if (!ReadPtr(sub + 0x38, &back) || back != comp) return false;
            if (!ReadPtr(sub + 0x20, &rcx20) || !IsValidCanonicalPtr(rcx20)) return false;
            // 140383820 reads a u16 catalog key here, not a function pointer.
            if (!Read16(rcx20 + 0x30, &characterKey) || characterKey == 0xFFFF) return false;
            if (!ReadPtr(sub + 0x40, &rcx40) || !IsValidCanonicalPtr(rcx40)) return false;
            if (!ReadPtr(rcx40 + 0x130, &context) || !IsValidCanonicalPtr(context)) return false;
            if (!ReadPtr(context, &handle) || !IsValidCanonicalPtr(handle)) return false;
            if (!ReadPtr(handle + 8, &resource) || !IsValidCanonicalPtr(resource)) return false;
            if (!ReadPtr(resource + 0x78, &parts) || !IsValidCanonicalPtr(parts)) return false;
            // 1403F6F90 binary-searches 16-byte {key, part*} rows.
            if (!Read32(parts + 0x60, &count) || !count || count > 65536) return false;
            if (!ReadPtr(parts + 0x58, &array) || !IsValidCanonicalPtr(array)) return false;
            if (!ReadPtr(array, &probe) ||
                !ReadPtr(array + static_cast<uintptr_t>(count - 1) * 16 + 8, &probe)) return false;
            if (outContext) *outContext = context;
            return true;
        }

        bool CompHasHorseGear(uintptr_t comp)
        {
            return ReadNativeMountGear(comp);
        }

        // rootIdx permits unidentified gear only on a trusted per-character root.
        // Reject mismatched candidates here so the rest of the walk still runs.
        uintptr_t FindEquipCompFromActor(uintptr_t actor, int targetIdx = -1, int rootIdx = -1)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            uintptr_t seen = 0;
            auto accept = [&](uintptr_t candidate) {
                if (!IsValidCanonicalPtr(candidate) || candidate == seen) return false;
                seen = candidate;
                return CompValid(candidate) && (targetIdx < 0 ||
                    AcceptCharacterComponent(targetIdx,
                        Inventory::IdentifyCharacterFromComp(candidate), rootIdx));
            };

            // 1. Direct component at actor + 0x38 (actor is SubContainer)
            uintptr_t comp = 0;
            if (ReadPtr(actor + kOff_Sub_EquipComp, &comp) && accept(comp))
                return comp;

            // 2. Standard character / mount container walk (*(*(actor+0x68)+0x38))
            uintptr_t sub = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && IsValidCanonicalPtr(sub))
            {
                if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && accept(comp))
                    return comp;
            }

            return 0;
        }

        uintptr_t CompForCharacter(uintptr_t actor, int targetIdx = -1, int rootIdx = -1)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            // 1. Direct actor check
            uintptr_t comp = FindEquipCompFromActor(actor, targetIdx, rootIdx);
            if (comp) return comp;

            // 2. If actor is an owner object, inspect inner actor (+0x68)
            uintptr_t innerAct = 0;
            if (ReadPtr(actor + kOff_Owner_Actor, &innerAct) && IsValidCanonicalPtr(innerAct) && innerAct != actor)
            {
                comp = FindEquipCompFromActor(innerAct, targetIdx, rootIdx);
                if (comp) return comp;
            }

            return 0;
        }

        uintptr_t FindTrackedCharacterComp(int targetIdx)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            for (int pass = 0; pass < 3; ++pass)
            {
                const int p = (targetIdx + pass) % 3;
                const uintptr_t roots[] = { Player::GetOwner(p), Player::GetActor(p) };
                for (int r = 0; r < 2; ++r)
                {
                    if (!roots[r] || (r == 1 && roots[r] == roots[0])) continue;
                    if (const uintptr_t comp = CompForCharacter(roots[r], targetIdx, p))
                        return comp;
                }
            }
            return 0;
        }

        static std::atomic<int> s_activeCharIdx{ -1 }; // -1 = auto-detect active player character
        static std::atomic<int> s_targetMode{ 0 };     // 0 = Player Character, 1 = Mount / Horse, 2 = Bag Item
        static std::atomic<int> s_activeMountIdx{ 0 };
        static std::atomic<uint64_t> s_selectionGeneration{ 0 };
        static std::atomic<uint64_t> s_worldGeneration{ 0 };
        static std::atomic<uint64_t> s_uiEpoch{ 0 };
        static std::atomic_flag s_dyeOperation = ATOMIC_FLAG_INIT;
        struct DyeOperationGuard
        {
            bool acquired = !s_dyeOperation.test_and_set(std::memory_order_acquire);
            ~DyeOperationGuard() { if (acquired) s_dyeOperation.clear(std::memory_order_release); }
        };

        uintptr_t FindMountComp(int index)
        {
            if (index < 0 || index >= 4) return 0;
            const uintptr_t trackedOwner = Player::GetMountOwner(index);
            const uintptr_t trackedActor = Player::GetMountActor(index);

            // 1. Direct query from Player mount descriptor (already resolved with horse gear)
            Player::MountDescriptor desc{};
            if (Player::GetMountDescriptor(index, &desc) && desc.equipComp >= kMinPointer &&
                desc.owner == trackedOwner && desc.actor == trackedActor)
            {
                uintptr_t owner = 0;
                if (ReadPtr(desc.equipComp + kOff_EquipComp_Owner, &owner) &&
                    (owner == trackedOwner || owner == trackedActor) && CompHasHorseGear(desc.equipComp))
                    return desc.equipComp;
            }

            const uintptr_t roots[] = {trackedOwner, trackedActor};
            for (uintptr_t root : roots)
            {
                const uintptr_t comp = NativeMountEquipmentFromRoot(root);
                if (comp && CompHasHorseGear(comp)) return comp;
            }

            const uintptr_t hooked = g_mountComp.load(std::memory_order_acquire);
            uintptr_t hookedOwner = 0;
            // A capture is useful only while bound to this tracked mount.
            if (hooked && ReadPtr(hooked + kOff_EquipComp_Owner, &hookedOwner) &&
                ((trackedOwner && hookedOwner == trackedOwner) || (trackedActor && hookedOwner == trackedActor)) &&
                CompHasHorseGear(hooked)) return hooked;

            return 0;
        }

        uintptr_t ProfileComp(int targetIdx, uintptr_t* candidate = nullptr, int* identity = nullptr)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            const uintptr_t comp = Player::GetProfileEquipComp(targetIdx);
            if (candidate) *candidate = comp;
            if (!CompValid(comp)) return 0;
            const int id = Inventory::IdentifyCharacterFromComp(comp);
            if (identity) *identity = id;
            return AcceptCharacterComponent(targetIdx, id, targetIdx) ? comp : 0;
        }

        uintptr_t FindCharacterFallback(int targetIdx, bool logFailure = false, int liveIdx = -1)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            // Player owns the cached manager/core-profile search. Query it once,
            // before walking fallback candidates; no repeated profile root scans.
            uintptr_t profileCandidate = 0;
            int profileId = -1;
            if (const uintptr_t profile = ProfileComp(targetIdx, &profileCandidate, &profileId))
                return profile;
            if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                return comp;

            // Last-resort manager candidates are sliced too. Inventory's
            // CharacterAddrs eagerly walks the whole list even with maxOut=1.
            // Its live-capture roots are already covered by ClientComp above.
            struct CandidateCache
            {
                uintptr_t world = 0, root = 0, manager = 0, data = 0, owner = 0;
                uint64_t generation = 0;
                uint32_t count = 0, cursor = 0, ownerIndex = 0;
                ULONGLONG nextScan = 0;
            };
            static thread_local CandidateCache candidates[3];
            static std::atomic<ULONGLONG> nextCandidateScan[3]{};
            CandidateCache& cache = candidates[targetIdx];
            uintptr_t root = 0, manager = 0, data = 0;
            uint32_t count = 0;
            const uintptr_t world = Player::GetControlledOwner();
            const uint64_t generation = s_worldGeneration.load(std::memory_order_acquire);
            const uintptr_t global = Player::GetCharMgrGlobal();
            if (world && global && ReadPtr(global, &root) && IsValidCanonicalPtr(root) &&
                ReadPtr(root, &manager) && IsValidCanonicalPtr(manager) &&
                ReadPtr(manager + kOff_CharMgr_ListData, &data) && IsValidCanonicalPtr(data) &&
                Read32(manager + kOff_CharMgr_ListCount, &count) && count <= kCharList_MaxCount)
            {
                if (cache.world != world || cache.generation != generation || cache.root != root ||
                    cache.manager != manager || cache.data != data || cache.count != count)
                {
                    const ULONGLONG nextScan = cache.nextScan;
                    cache = { world, root, manager, data, 0, generation, count, 0, 0, nextScan };
                }
                auto linked = [&](uint32_t index, uintptr_t owner) {
                    uintptr_t current = 0;
                    uint32_t currentCount = 0, party = 0;
                    return Player::GetControlledOwner() == world &&
                        generation == s_worldGeneration.load(std::memory_order_acquire) &&
                        ReadPtr(global, &current) && current == root &&
                        ReadPtr(root, &current) && current == manager &&
                        ReadPtr(manager + kOff_CharMgr_ListData, &current) && current == data &&
                        Read32(manager + kOff_CharMgr_ListCount, &currentCount) && index < currentCount &&
                        currentCount == count && ReadPtr(data + static_cast<uintptr_t>(index) * 8, &current) && current == owner &&
                        Read32(owner + kOff_Owner_PartyIndex, &party) && party == static_cast<uint32_t>(targetIdx + 1);
                };
                if (cache.owner && linked(cache.ownerIndex, cache.owner))
                    if (const uintptr_t comp = CompForCharacter(cache.owner, targetIdx, targetIdx)) return comp;
                cache.owner = 0;
                const ULONGLONG now = GetTickCount64();
                ULONGLONG due = nextCandidateScan[targetIdx].load(std::memory_order_acquire);
                if (now >= cache.nextScan && now >= due &&
                    nextCandidateScan[targetIdx].compare_exchange_strong(due, now + 500, std::memory_order_acq_rel))
                {
                    cache.nextScan = now + 500;
                    for (uint32_t visited = 0; visited < 32 && cache.cursor < count; ++visited)
                    {
                        const uint32_t index = cache.cursor++;
                        uintptr_t owner = 0;
                        uint32_t party = 0;
                        if (!ReadPtr(data + static_cast<uintptr_t>(index) * 8, &owner) || !IsValidCanonicalPtr(owner) ||
                            !Read32(owner + kOff_Owner_PartyIndex, &party) || party != static_cast<uint32_t>(targetIdx + 1)) continue;
                        const uintptr_t comp = CompForCharacter(owner, targetIdx, targetIdx);
                        if (!comp || !linked(index, owner)) continue;
                        cache.owner = owner;
                        cache.ownerIndex = index;
                        return comp;
                    }
                    if (cache.cursor >= count) cache.cursor = 0;
                }
            }
            if (logFailure)
            {
                static thread_local ULONGLONG lastLog[3] = {};
                const ULONGLONG now = GetTickCount64();
                if (!lastLog[targetIdx] || now - lastLog[targetIdx] >= 10000)
                {
                    lastLog[targetIdx] = now;
                    LOG_WARN("dye: Ready failed selected=%d live=%d active=%d profile=%p profileId=%d (no accepted table)",
                        targetIdx, liveIdx, Player::GetActiveCharacterIdx(),
                        reinterpret_cast<void*>(profileCandidate), profileId);
                }
            }
            return 0;
        }

        uintptr_t ClientComp(bool logFailure = false)
        {
            if (s_targetMode == 1)
            {
                const uintptr_t mountComp = FindMountComp(s_activeMountIdx);
                if (mountComp) return mountComp;
                return 0;
            }

            const int liveIdx = Player::GetActiveCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx.load();
            if (targetIdx < 0 || targetIdx > 2) return 0;

            if (targetIdx == liveIdx)
            {
                const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                if (const uintptr_t comp = CompForCharacter(liveChar, targetIdx, liveIdx))
                    return comp;

                const int liveActiveIdx = Player::GetActiveCharacterIdx();
                if (liveActiveIdx >= 0 && liveActiveIdx < 3)
                {
                    const uintptr_t owner = Player::GetOwner(liveActiveIdx);
                    const uintptr_t actor = Player::GetActor(liveActiveIdx);
                    if (owner != liveChar)
                        if (const uintptr_t comp = CompForCharacter(owner, targetIdx, liveActiveIdx))
                            return comp;
                    if (actor != owner && actor != liveChar)
                        if (const uintptr_t comp = CompForCharacter(actor, targetIdx, liveActiveIdx))
                            return comp;
                }

                const uintptr_t h = Inventory::ClientHolderAddr();
                if (h)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(h + 8, &owner) && owner >= kMinPointer)
                    {
                        if (const uintptr_t comp = CompForCharacter(owner, targetIdx, liveIdx))
                            return comp;
                    }
                }

                const uintptr_t hooked = g_comp.load(std::memory_order_acquire);
                if (CompValid(hooked))
                {
                    uintptr_t hookedOwner = 0;
                    const bool ownerKnown =
                        ReadPtr(hooked + kOff_EquipComp_Owner, &hookedOwner);
                    if (!liveChar || (ownerKnown && hookedOwner == liveChar))
                    {
                        if (AcceptCharacterComponent(targetIdx,
                            Inventory::IdentifyCharacterFromComp(hooked),
                            liveChar && ownerKnown && hookedOwner == liveChar ? liveIdx : -1))
                            return hooked;
                    }
                }
            }
            return FindCharacterFallback(targetIdx, logFailure, liveIdx);
        }

        uintptr_t ServerComp()
        {
            if (s_targetMode == 1)
            {
                const uintptr_t mountComp = FindMountComp(s_activeMountIdx);
                if (mountComp) return mountComp;
                return 0;
            }

            const int liveIdx = Player::GetActiveCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx.load();
            if (targetIdx < 0 || targetIdx > 2) return 0;

            if (targetIdx == liveIdx)
            {
                const uintptr_t serverChar = Inventory::ServerCharacterAddr();
                if (const uintptr_t comp = CompForCharacter(serverChar, targetIdx, liveIdx))
                    return comp;

                const int liveActiveIdx = Player::GetActiveCharacterIdx();
                if (liveActiveIdx >= 0 && liveActiveIdx < 3)
                {
                    const uintptr_t owner = Player::GetOwner(liveActiveIdx);
                    const uintptr_t actor = Player::GetActor(liveActiveIdx);
                    if (owner != serverChar)
                        if (const uintptr_t comp = CompForCharacter(owner, targetIdx, liveActiveIdx))
                            return comp;
                    if (actor != owner && actor != serverChar)
                        if (const uintptr_t comp = CompForCharacter(actor, targetIdx, liveActiveIdx))
                            return comp;
                }

                const uintptr_t h = Inventory::ServerHolderAddr();
                if (h)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(h + 8, &owner) && owner >= kMinPointer)
                    {
                        if (const uintptr_t comp = CompForCharacter(owner, targetIdx, liveIdx))
                            return comp;
                    }
                }

            }
            return FindCharacterFallback(targetIdx);
        }

        void* __fastcall hkEquipBatch(void* a1, void* a2, void* a3, void* a4)
        {
            __try
            {
                const uintptr_t comp = reinterpret_cast<uintptr_t>(a1);
                if (comp >= kMinPointer && CompValid(comp))
                {
                    s_uiEpoch.fetch_add(1, std::memory_order_release);
                    s_lastEquipChangeMs.store(GetTickCount64(), std::memory_order_release);
                    if (CompHasHorseGear(comp))
                    {
                        g_mountComp.store(comp, std::memory_order_release);
                    }
                    else if (g_comp.load(std::memory_order_relaxed) != comp)
                    {
                        g_comp.store(comp, std::memory_order_release);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            void* result = oEquipBatch(a1, a2, a3, a4);
            s_uiEpoch.fetch_add(1, std::memory_order_release);
            return result;
        }

        // Drops the "Item dyed successfully." popup while the auto-restore is
        // replaying dye batches; the user's own dye action still toasts.
        void* __fastcall hkDyeNotify(void* ui, void* arg, int code)
        {
            if (g_suppressDyeToast.load(std::memory_order_acquire) > 0) return nullptr;
            return oDyeNotify(ui, arg, code);
        }

        uintptr_t FindEntryByTag(uintptr_t comp, uint16_t tag)
        {
            uintptr_t array = 0;
            uint32_t  count = 0;
            uintptr_t stride = 0xD0;
            uintptr_t tagOffset = 0xC8;
            if (!ReadEquipTable(comp, array, count, &stride, &tagOffset)) return 0;

            for (uint32_t i = 0; i < count; ++i)
            {
                const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
                uint16_t t = 0;
                if (!Read16(entry + tagOffset, &t) || t != tag) continue;
                uint16_t tid = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                int64_t quantity = 0;
                if (!Read64(entry + kOff_InvSlot_Quantity, &quantity) || quantity <= 0) continue;
                return entry;
            }
            return 0;
        }

        struct DyeItemIdentity
        {
            int character = -1;
            uint16_t tag = 0;
            uint16_t type = 0;
            int64_t instance = 0;
            int mode = 0;
            int mount = -1;
            uintptr_t sourceComp = 0;   // equipment component, or bag holder in mode 2
            uintptr_t sourceEntry = 0;
            uintptr_t sourceOwner = 0;
            uintptr_t controlledOwner = 0;
            uintptr_t worldRoot = 0;
            uint64_t worldGeneration = 0;
        };

        bool ReadDyeItem(uintptr_t comp, uint16_t tag, int character, DyeItemIdentity& item,
                         int mode = 0, int mount = -1)
        {
            const uintptr_t entry = FindEntryByTag(comp, tag);
            if (!entry || (mode != 0 && mode != 1) ||
                (mode == 0 && (character < 0 || character > 2))) return false;
            item = { character, tag, 0, 0 };
            item.mode = mode;
            item.mount = mount;
            item.sourceComp = comp;
            item.sourceEntry = entry;
            item.controlledOwner = Player::GetControlledOwner();
            if (g_clientRegistryGlobal) ReadPtr(g_clientRegistryGlobal, &item.worldRoot);
            item.worldGeneration = s_worldGeneration.load(std::memory_order_acquire);
            return Read16(entry + kOff_InvSlot_TypeId, &item.type) && item.type != 0 &&
                   item.type != kInvSlot_EmptyType &&
                   Read64(entry + kOff_ItemVal_InstanceId, &item.instance) &&
                   ReadPtr(comp + kOff_EquipComp_Owner, &item.sourceOwner);
        }

        uintptr_t MatchingDyeEntry(uintptr_t comp, const DyeItemIdentity& item)
        {
            if ((item.mode != 0 && item.mode != 1) ||
                (item.mode == 0 && (item.character < 0 || item.character > 2)) ||
                !item.type || item.type == kInvSlot_EmptyType)
                return 0;
            const uintptr_t entry = FindEntryByTag(comp, item.tag);
            uint16_t type = 0;
            int64_t instance = 0;
            uintptr_t owner = 0;
            uint32_t party = 0;
            if (!entry || !Read16(entry + kOff_InvSlot_TypeId, &type) || type != item.type ||
                !Read64(entry + kOff_ItemVal_InstanceId, &instance) ||
                !ReadPtr(comp + kOff_EquipComp_Owner, &owner)) return 0;

            const bool hasParty = core::GetGameVersion().revision < 2800 &&
                Read32(owner + kOff_Owner_PartyIndex, &party) && party >= 1 && party <= 3;
            if (item.mode == 0 && hasParty && party != static_cast<uint32_t>(item.character + 1)) return 0;
            // Only the original, still-linked source may have an absent ID.
            // Party identity establishes a wearer, never a particular item instance.
            if (comp == item.sourceComp)
                return entry == item.sourceEntry && owner == item.sourceOwner && instance == item.instance ? entry : 0;
            return item.instance > 0 && instance == item.instance ? entry : 0;
        }

        bool MatchesItemValue(uintptr_t entry, const DyeItemIdentity& item)
        {
            uint16_t type = 0;
            int64_t instance = 0, quantity = 0;
            return entry && item.type && item.type != kInvSlot_EmptyType &&
                   Read16(entry + kOff_InvSlot_TypeId, &type) && type == item.type &&
                   Read64(entry + kOff_ItemVal_InstanceId, &instance) && instance == item.instance &&
                   (item.instance > 0 || entry == item.sourceEntry) &&
                   Read64(entry + kOff_InvSlot_Quantity, &quantity) && quantity > 0;
        }

        struct DyeRenderTarget
        {
            uintptr_t comp = 0;
            uintptr_t entry = 0;
            uintptr_t context = 0;
        };
        struct DyeVisualRetry
        {
            DyeItemIdentity item{};
            uint32_t mask = 0;
        };
        static DyeVisualRetry s_manualVisualRetry[3][32];

        DyeRenderTarget RenderTargetFromOwner(uintptr_t owner, const DyeItemIdentity& item, bool requireRender = true)
        {
            if (item.mode == 1 && (!IsNativeMountOwner(owner) || item.instance <= 0)) return {};
            uintptr_t sub = 0, comp = 0, back = 0;
            if (!IsValidCanonicalPtr(owner) ||
                !ReadPtr(owner + kOff_Container_Sub, &sub) || !IsValidCanonicalPtr(sub) ||
                !ReadPtr(sub + kOff_Sub_EquipComp, &comp) || !IsNativeClientEquip(comp) ||
                !ReadPtr(comp + kOff_EquipComp_Owner, &back) || back != owner) return {};
            const uintptr_t entry = MatchingDyeEntry(comp, item);
            uintptr_t context = 0;
            if (!entry || (requireRender && !IsRenderComp(comp, &context))) return {};
            return { comp, entry, context };
        }

        constexpr ULONGLONG kRenderScanIntervalMs = 500;
        constexpr uint32_t kRegistryBucketBudget = 16;
        constexpr uint32_t kRegistryNodeBudget = 64;
        constexpr uint32_t kMaxRegistryEntries = 65536;

        struct DyeRegistryView
        {
            uintptr_t root = 0, registry = 0, map = 0, buckets = 0, nodes = 0;
            uint32_t bucketCount = 0, occupied = 0;

            bool Same(const DyeRegistryView& other) const
            {
                return root == other.root && registry == other.registry && map == other.map &&
                    buckets == other.buckets && nodes == other.nodes &&
                    bucketCount == other.bucketCount;
            }
        };

        DyeRegistryView ReadDyeRegistry()
        {
            DyeRegistryView view{};
            if (!g_clientRegistryGlobal || !ReadPtr(g_clientRegistryGlobal, &view.root) || !IsValidCanonicalPtr(view.root))
                return {};
            if (!ReadPtr(view.root + 0x38, &view.registry) || !IsValidCanonicalPtr(view.registry) ||
                !ReadPtr(view.registry + 8, &view.map) || !IsValidCanonicalPtr(view.map) ||
                !Read32(view.map + 0x88, &view.bucketCount) || !view.bucketCount || view.bucketCount > kMaxRegistryEntries ||
                !Read32(view.map + 0x8C, &view.occupied) || view.occupied > kMaxRegistryEntries ||
                !ReadPtr(view.map + 0x98, &view.buckets) || !IsValidCanonicalPtr(view.buckets) ||
                !ReadPtr(view.map + 0xA0, &view.nodes) || !IsValidCanonicalPtr(view.nodes))
            {
                // Preserve the world root even while its registry is streaming.
                const uintptr_t root = view.root;
                view = {};
                view.root = root;
            }
            return view;
        }

        struct DyeRendererCache
        {
            uintptr_t dataComp = 0, dataOwner = 0, controlledOwner = 0;
            uint64_t generation = 0, worldGeneration = 0;
            DyeRegistryView registry{};
            uintptr_t comp = 0, owner = 0, hintRoot = 0;
            uintptr_t pair = 0, node = 0;
            uint32_t key = 0, index = 0;
            uint32_t bucket = 0, row = 0;
            ULONGLONG nextScan = 0;
        };
        // Only accessed under DyeOperationGuard. A component is shared across
        // tags, but entries/contexts are NEVER cached for a subsequent call.
        DyeRendererCache s_renderers[3];
        DyeRendererCache s_equipmentReplicas[3];
        DyeRendererCache s_mountRenderers[4];

        bool RegistryBindingValid(const DyeRendererCache& cache)
        {
            uintptr_t current = 0;
            uint32_t value = 0;
            if (!cache.registry.bucketCount || !cache.pair) return false;
            const uintptr_t bucket = cache.registry.buckets +
                static_cast<uintptr_t>(cache.key % cache.registry.bucketCount) * 0x100;
            // Pairs beyond a shrunken bucket count can remain readable.
            const uintptr_t row = cache.pair >= bucket + 8 ? (cache.pair - bucket - 8) / 8 : 31;
            return cache.registry.Same(ReadDyeRegistry()) &&
                Read32(bucket, &value) && value <= 31 && row < value &&
                Read32(cache.pair, &value) && value == cache.key &&
                Read32(cache.pair + 4, &value) && value == cache.index &&
                ReadPtr(cache.registry.nodes + static_cast<uintptr_t>(cache.index) * 8, &current) && current == cache.node &&
                Read32(cache.node + 4, &value) && value == cache.key &&
                ReadPtr(cache.node + 8, &current) && current == cache.owner;
        }

        DyeRenderTarget ResolveDyeRenderTarget(const DyeItemIdentity& item, uintptr_t dataComp,
                                              uintptr_t profileComp = 0, bool requireRender = true)
        {
            const bool mount = item.mode == 1;
            if ((mount ? (item.mount < 0 || item.mount >= 4 || item.instance <= 0)
                       : (item.character < 0 || item.character > 2)) || !item.controlledOwner ||
                Player::GetControlledOwner() != item.controlledOwner || !MatchingDyeEntry(dataComp, item)) return {};
            const ULONGLONG now = GetTickCount64();
            const uint64_t generation = s_selectionGeneration.load(std::memory_order_acquire);
            const uint64_t worldGeneration = s_worldGeneration.load(std::memory_order_acquire);
            const DyeRegistryView registry = ReadDyeRegistry();
            if (registry.root != item.worldRoot || worldGeneration != item.worldGeneration) return {};
            uintptr_t dataOwner = 0, profileOwner = 0;
            if (!ReadPtr(dataComp + kOff_EquipComp_Owner, &dataOwner)) return {};
            if (profileComp) ReadPtr(profileComp + kOff_EquipComp_Owner, &profileOwner);
            DyeRendererCache& cache = mount ? s_mountRenderers[item.mount] :
                requireRender ? s_renderers[item.character] : s_equipmentReplicas[item.character];
            if (cache.dataComp != dataComp || cache.dataOwner != dataOwner ||
                cache.controlledOwner != item.controlledOwner || cache.generation != generation ||
                cache.worldGeneration != worldGeneration ||
                !cache.registry.Same(registry))
            {
                // Map churn must not bypass the per-character failed-scan budget.
                const ULONGLONG nextScan = cache.nextScan;
                cache = {};
                cache.dataComp = dataComp;
                cache.dataOwner = dataOwner;
                cache.controlledOwner = item.controlledOwner;
                cache.generation = generation;
                cache.worldGeneration = worldGeneration;
                cache.registry = registry;
                cache.nextScan = nextScan;
            }
            // All roots are discovery hints only. Every resulting owner must
            // supply its OWN client component and the same equipped item.
            auto fromRoot = [&](uintptr_t root) -> DyeRenderTarget {
                if (!IsValidCanonicalPtr(root)) return {};
                uintptr_t inner = 0;
                ReadPtr(root + kOff_Owner_Actor, &inner);
                const uintptr_t owners[] = { root, inner };
                for (uintptr_t owner : owners)
                {
                    if (!IsValidCanonicalPtr(owner)) continue;
                    DyeRenderTarget target = RenderTargetFromOwner(owner, item, requireRender);
                    if (target.comp) return target;
                    uintptr_t possessor = 0, pawn = 0;
                    if (!mount && ReadPtr(owner + kOff_Owner_Possessor, &possessor) && IsValidCanonicalPtr(possessor) &&
                        ReadPtr(possessor + kOff_Possessor_Pawn, &pawn))
                    {
                        // A shared possessor may point at a different protagonist.
                        target = RenderTargetFromOwner(pawn, item, requireRender);
                        if (target.comp) return target;
                    }
                    // Player::GetActor can already be the component container.
                    uintptr_t comp = 0, back = 0;
                    if (ReadPtr(owner + kOff_Sub_EquipComp, &comp) && IsNativeClientEquip(comp) &&
                        ReadPtr(comp + kOff_EquipComp_Owner, &back))
                    {
                        target = RenderTargetFromOwner(back, item, requireRender);
                        if (target.comp) return target;
                    }
                }
                return {};
            };

            const uintptr_t roots[] = {
                dataOwner, profileOwner, mount ? Player::GetMountOwner(item.mount) : Player::GetOwner(item.character),
                mount ? Player::GetMountActor(item.mount) : Player::GetActor(item.character),
                mount ? 0 : Player::GetControlledOwner(), mount ? 0 : Inventory::ClientCharacterAddr()
            };
            if (cache.comp)
            {
                DyeRenderTarget target{};
                if (cache.pair)
                {
                    if (RegistryBindingValid(cache)) target = RenderTargetFromOwner(cache.owner, item, requireRender);
                }
                else
                {
                    for (uintptr_t root : roots)
                        if (root && root == cache.hintRoot) { target = fromRoot(root); break; }
                }
                uintptr_t owner = 0;
                if (target.comp == cache.comp && target.comp &&
                    ReadPtr(target.comp + kOff_EquipComp_Owner, &owner) && owner == cache.owner &&
                    registry.Same(ReadDyeRegistry()) && MatchingDyeEntry(dataComp, item) &&
                    Player::GetControlledOwner() == item.controlledOwner &&
                    worldGeneration == s_worldGeneration.load(std::memory_order_acquire) &&
                    generation == s_selectionGeneration.load(std::memory_order_acquire)) return target;
                // No stale positive result, even within the backoff window.
                cache.comp = cache.owner = cache.hintRoot = cache.pair = cache.node = 0;
            }
            if (now < cache.nextScan) return {};
            cache.nextScan = now + kRenderScanIntervalMs;
            for (uintptr_t root : roots)
            {
                const DyeRenderTarget target = fromRoot(root);
                if (target.comp && Player::GetControlledOwner() == item.controlledOwner &&
                    registry.Same(ReadDyeRegistry()) && MatchingDyeEntry(dataComp, item) &&
                    worldGeneration == s_worldGeneration.load(std::memory_order_acquire) &&
                    generation == s_selectionGeneration.load(std::memory_order_acquire))
                {
                    if (!ReadPtr(target.comp + kOff_EquipComp_Owner, &cache.owner)) continue;
                    cache.comp = target.comp;
                    cache.hintRoot = root;
                    return target;
                }
            }

            // Native dye-ack: [global]+38 -> registry; registry+8 -> map
            // (140B4807E, 140B481D0 -> 14082FBC0 -> 14083D188).
            // 1403F4940 uses 0x100-byte buckets, each containing a count and
            // up to 31 {u32 key,u32 nodeIndex} pairs at +8. nodes is an array
            // of POINTERS: node+4 must repeat key, node+8 is the owner. There
            // is no guessed node stride or PartyIndex-to-network-key mapping.
            if (!registry.map || !registry.occupied) return {};
            // Owner+60 is a native entity key (14083D2AE..2B2). It is only
            // a lookup hint: realm copies need not share it. Validate the map
            // binding and exact item before accepting the resulting owner.
            uint32_t hint = 0;
            if (Read32(dataOwner + 0x60, &hint) && hint)
            {
                const uintptr_t bucket = registry.buckets + uintptr_t{hint % registry.bucketCount} * 0x100;
                uint32_t count = 0;
                if (Read32(bucket, &count) && count <= 31)
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        const uintptr_t pair = bucket + 8 + uintptr_t{i} * 8;
                        uint32_t key = 0, index = 0, nodeKey = 0;
                        uintptr_t node = 0, owner = 0;
                        if (!Read32(pair, &key) || key != hint || !Read32(pair + 4, &index) || index >= kMaxRegistryEntries ||
                            !ReadPtr(registry.nodes + uintptr_t{index} * 8, &node) || !IsValidCanonicalPtr(node) ||
                            !Read32(node + 4, &nodeKey) || nodeKey != key || !ReadPtr(node + 8, &owner)) continue;
                        const DyeRenderTarget target = RenderTargetFromOwner(owner, item, requireRender);
                        if (!target.comp) continue;
                        cache.owner = owner; cache.pair = pair; cache.node = node;
                        cache.key = key; cache.index = index;
                        if (RegistryBindingValid(cache) && MatchingDyeEntry(dataComp, item) &&
                            Player::GetControlledOwner() == item.controlledOwner)
                        {
                            cache.comp = target.comp;
                            return target;
                        }
                        cache.owner = cache.pair = cache.node = 0;
                    }
            }
            uint32_t visitedBuckets = 0, visitedNodes = 0;
            while (cache.bucket < registry.bucketCount && visitedBuckets < kRegistryBucketBudget &&
                   visitedNodes < kRegistryNodeBudget)
            {
                const uint32_t b = cache.bucket;
                const uintptr_t bucket = registry.buckets + static_cast<uintptr_t>(b) * 0x100;
                uint32_t count = 0;
                ++visitedBuckets;
                if (!Read32(bucket, &count) || count > 31) { cache.bucket = cache.row = 0; return {}; }
                while (cache.row < count && visitedNodes < kRegistryNodeBudget)
                {
                    const uint32_t i = cache.row++;
                    ++visitedNodes;
                    const uintptr_t pair = bucket + 8 + static_cast<uintptr_t>(i) * 8;
                    uint32_t key = 0, index = 0, nodeKey = 0;
                    uintptr_t node = 0, owner = 0;
                    if (!Read32(pair, &key) || !key || key % registry.bucketCount != b ||
                        !Read32(pair + 4, &index) || index >= kMaxRegistryEntries ||
                        !ReadPtr(registry.nodes + static_cast<uintptr_t>(index) * 8, &node) || !IsValidCanonicalPtr(node) ||
                        !Read32(node + 4, &nodeKey) || nodeKey != key || !ReadPtr(node + 8, &owner)) continue;
                    const DyeRenderTarget target = RenderTargetFromOwner(owner, item, requireRender);
                    if (!target.comp) continue;
                    // Recheck links after enumeration in case the registry changed.
                    cache.owner = owner;
                    cache.pair = pair;
                    cache.node = node;
                    cache.key = key;
                    cache.index = index;
                    if (RegistryBindingValid(cache) && Player::GetControlledOwner() == item.controlledOwner &&
                        worldGeneration == s_worldGeneration.load(std::memory_order_acquire) &&
                        generation == s_selectionGeneration.load(std::memory_order_acquire) &&
                        MatchingDyeEntry(dataComp, item))
                    {
                        cache.comp = target.comp;
                        return target;
                    }
                    cache.owner = cache.pair = cache.node = 0;
                    return {};
                }
                if (cache.row >= count) { ++cache.bucket; cache.row = 0; }
            }
            if (cache.bucket >= registry.bucketCount) cache.bucket = cache.row = 0;
            return {};
        }

        bool DyeMemoryRange(uintptr_t address, size_t bytes, bool writable)
        {
            if (!bytes) return true;
            if (!IsValidCanonicalPtr(address) || bytes - 1 > mem::kMaxPointer - address) return false;
            const uintptr_t end = address + bytes;
            while (address < end)
            {
                MEMORY_BASIC_INFORMATION region{};
                if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
                    region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
                const DWORD protection = region.Protect & 0xFF;
                const bool canWrite = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                                      protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
                const bool canRead = canWrite || protection == PAGE_READONLY || protection == PAGE_EXECUTE_READ;
                if (!canRead || (writable && (!canWrite || region.Type == MEM_IMAGE))) return false;
                const uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
                if (next <= address) return false;
                address = next < end ? next : end;
            }
            return true;
        }

        struct DyeVector
        {
            uintptr_t header = 0;
            uintptr_t data = 0;
            uint32_t count = 0;
            uint32_t capacity = 0;
        };

        bool ReadDyeVector(uintptr_t entry, DyeVector& vector, bool writable = false)
        {
            vector = {};
            if (!IsValidCanonicalPtr(entry) || !core::GetGameVersion().revision) return false;
            // Select by detected build, never reinterpret a modern empty/invalid
            // vector as the legacy +70 layout.
            vector.header = entry + (core::IsLegacyTU() ? 0x70 : 0x78);
            if (!DyeMemoryRange(vector.header, 16, writable) ||
                !ReadPtr(vector.header, &vector.data) || !Read32(vector.header + 8, &vector.count) ||
                !Read32(vector.header + 12, &vector.capacity) || vector.count > kDye_MaxChannels ||
                vector.count > vector.capacity || vector.capacity > 1024) return false;
            if (!vector.capacity) return !vector.data && !vector.count;
            if (!DyeMemoryRange(vector.data, static_cast<size_t>(vector.capacity) * 16, writable)) return false;
            uint32_t channels = 0;
            for (uint32_t i = 0; i < vector.count; ++i)
            {
                uint8_t channel = 0;
                if (!Read8(vector.data + static_cast<uintptr_t>(i) * 16 + 6, &channel) ||
                    channel >= kDye_MaxChannels || (channels & (1u << channel))) return false;
                channels |= 1u << channel;
            }
            return true;
        }

        uint32_t ReadRecords(uintptr_t itemVal, uint8_t out[kDye_MaxChannels][16], bool* valid = nullptr)
        {
            if (valid) *valid = false;
            memset(out, 0, kDye_MaxChannels * 16);
            DyeVector vector{};
            if (!ReadDyeVector(itemVal, vector)) return 0;

            uint32_t mask = 0;
            for (uint32_t i = 0; i < vector.count; ++i)
            {
                uint8_t rec[16];
                bool ok = true;
                for (int b = 0; b < 16 && ok; ++b)
                    ok = Read8(vector.data + i * 16 + b, &rec[b]);
                if (!ok) return 0;
                const uint8_t ch = rec[6];
                if (ch >= kDye_MaxChannels || (mask & (1u << ch))) return 0;
                memcpy(out[ch], rec, 16);
                mask |= 1u << ch;
            }
            uintptr_t currentData = 0;
            uint32_t currentCount = 0, currentCapacity = 0;
            if (!ReadPtr(vector.header, &currentData) || currentData != vector.data ||
                !Read32(vector.header + 8, &currentCount) || currentCount != vector.count ||
                !Read32(vector.header + 12, &currentCapacity) || currentCapacity != vector.capacity) return 0;
            if (valid) *valid = true;
            return mask;
        }

        void BuildSetRecord(uint8_t out[16], int channel, const Dye::Channel& c)
        {
            memset(out, 0, 16);
            memcpy(out + 0, &c.groupKey, 4);
            // Natural material is a sentinel, not material template 1. A clear
            // record also requires empty color/alpha and the condition sentinel.
            memcpy(out + 4, &c.materialId, 2);
            out[6]  = static_cast<uint8_t>(channel);
            out[7]  = c.r;
            out[8]  = c.g;
            out[9]  = c.b;
            out[10] = 0xFF;
            out[11] = c.repair;
            if (channel == 0 || channel == 3)
                out[13] = 0x04;
        }

        void BuildClearRecord(uint8_t out[16], int channel)
        {
            memset(out, 0, 16);
            out[4]  = 0xFF; out[5] = 0xFF; // material 0xFFFF
            out[6]  = static_cast<uint8_t>(channel);
            out[11] = 0xFF;
        }

        // Comps whose engine apply/visual leaves faulted (0xC0000005) recently.
        // The faults come from half-initialized or torn-down render structures
        // (bodies still streaming at load, off-screen bodies, companion weapon
        // entries whose render leaf permanently faults) and retrying on a fixed
        // timer re-AVs and re-logs forever. Escalating backoff: strike 1 waits
        // 15s, strike 2 waits 5min, strike 3+ waits 30min - one diagnostic line
        // per stage, then effectively silent, while a comp that genuinely
        // recovers (body re-rendered) still gets retried after each stage.
        struct FaultedComp { uintptr_t comp; ULONGLONG until; uint32_t strikes; };
        static FaultedComp s_faultedComps[8];
        static constexpr ULONGLONG kFaultCooldownMs[3] = { 15000, 300000, 1800000 };

        static bool IsCompFaulted(uintptr_t comp)
        {
            if (!comp) return false;
            const ULONGLONG now = GetTickCount64();
            for (const FaultedComp& e : s_faultedComps)
                if (e.comp == comp) return now < e.until;
            return false;
        }

        static uint32_t CompFaultStrikes(uintptr_t comp)
        {
            for (const FaultedComp& e : s_faultedComps)
                if (e.comp == comp) return e.strikes;
            return 0;
        }

        static void MarkCompFaulted(uintptr_t comp)
        {
            if (!IsValidCanonicalPtr(comp)) return;
            const ULONGLONG now = GetTickCount64();
            FaultedComp* freeSlot = nullptr;
            for (FaultedComp& e : s_faultedComps)
            {
                if (e.comp == comp)
                {
                    e.strikes++;
                    e.until = now + kFaultCooldownMs[e.strikes >= 3 ? 2 : e.strikes - 1];
                    return;
                }
                if (!freeSlot && (e.comp == 0 || e.until <= now)) freeSlot = &e;
            }
            if (!freeSlot) freeSlot = &s_faultedComps[0];
            freeSlot->comp = comp;
            freeSlot->strikes = 1;
            freeSlot->until = now + kFaultCooldownMs[0];
        }

        static void ClearCompFault(uintptr_t comp)
        {
            for (FaultedComp& e : s_faultedComps)
                if (e.comp == comp) { e.comp = 0; e.until = 0; e.strikes = 0; }
        }

        // Companion containers via CharacterManager PartyIndex - the copy the
        // native inspect/pause UI reads. Inventory::CharacterAddrs filters by
        // gear identity and can miss exactly this copy, which is why the
        // equipment editor's Sync* walks it separately (the proven path for
        // companion edits). Duplicates and the skip pointers are filtered.
        static int PartyCompanionComps(int targetIdx, uintptr_t* out, int maxOut,
                                       uintptr_t skipA = 0, uintptr_t skipB = 0)
        {
            if (targetIdx < 0 || targetIdx > 2 || !out || maxOut <= 0) return 0;
            int n = 0;
            auto add = [&](uintptr_t c) {
                if (!c || n >= maxOut) return;
                if (c == skipA || c == skipB) return;
                for (int i = 0; i < n; ++i) if (out[i] == c) return;
                out[n++] = c;
            };
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
                        add(CompForCharacter(cand, targetIdx, targetIdx));
                    }
                }
            }
            return n;
        }

        bool CallDyeApply(uintptr_t comp, void* batch, int* outErr,
                          const DyeItemIdentity& item, bool forceRetry = false)
        {
            if (!item.controlledOwner || Player::GetControlledOwner() != item.controlledOwner ||
                MatchingDyeEntry(item.sourceComp, item) != item.sourceEntry) return false;
            if (!g_dyeApply) {
                LOG_WARN("dye: CallDyeApply skipped - g_dyeApply is NULL");
                return false;
            }
            if (!IsRenderComp(comp) || !MatchingDyeEntry(comp, item)) {
                return false;
            }
            if (!batch || !outErr) {
                return false;
            }
            DyeVector dyeVector{};
            if (!ReadDyeVector(MatchingDyeEntry(comp, item), dyeVector, true)) return false;
            // Explicit user actions force past the fault cache: the restore's
            // escalating backoff must never lock the player out of dyeing -
            // the comp may have fully recovered since the last fault.
            if (!forceRetry && IsCompFaulted(comp)) return false;
            // The batch has additional prerequisites beyond the visual leaves.
            // TU 2.02 @ 1409165F6..692: descriptor, optional bind key,
            // possessor lock and pawn equipment view. No server-kind retries.
            uintptr_t act = 0, poss = 0, pawn = 0, sub = 0, view = 0, td = 0, lock = 0, probe = 0;
            uint8_t actorClass = 0;
            if (!ReadPtr(comp + 8, &act) || !IsValidCanonicalPtr(act)) return false;
            if (!ReadPtr(act + kOff_Owner_Possessor, &poss) || !IsValidCanonicalPtr(poss)) return false;
            if (!ReadPtr(poss + kOff_Possessor_Pawn, &pawn) || !IsValidCanonicalPtr(pawn)) return false;
            if (!ReadPtr(act + kOff_Owner_TypeDesc, &td) || !IsValidCanonicalPtr(td) ||
                !Read8(td + 1, &actorClass)) return false;
            if (actorClass >= 4 && actorClass <= 6)
            {
                uintptr_t bind = 0;
                if (!ReadPtr(act + 0x68, &sub) || !IsValidCanonicalPtr(sub) ||
                    !ReadPtr(sub + 0x118, &bind)) return false;
                // Null means the native -1/no-bind sentinel, not an error.
                if (bind && (!IsValidCanonicalPtr(bind) || !ReadPtr(bind + 0x20, &probe))) return false;
            }
            if (!ReadPtr(pawn + 0x68, &sub) || !IsValidCanonicalPtr(sub) ||
                !ReadPtr(sub + 0x110, &view) || !IsValidCanonicalPtr(view) ||
                !ReadPtr(view + 0x18, &probe)) return false;
            if (!ReadPtr(poss + 8, &lock) || !IsValidCanonicalPtr(lock) ||
                !ReadPtr(lock, &probe) || !IsValidCanonicalPtr(probe)) return false;

            *outErr = -999999;
            DWORD exCode = 0;
            __try
            {
                g_dyeApply(reinterpret_cast<void*>(comp), outErr, batch);
                if (*outErr == 0) ClearCompFault(comp); // recovered - stop backing off
                if (forceRetry)
                    LOG("dye: CallDyeApply comp=%p completed, outErr=%d",
                        reinterpret_cast<void*>(comp), *outErr);
                return true;
            }
            __except (exCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
            {
                const bool firstFault = (CompFaultStrikes(comp) == 0);
                MarkCompFaulted(comp);
                // Handled, expected engine fault (broken render structures on
                // this comp). Info-level, first occurrence only - applyOk in
                // the diag line is the user-visible signal.
                if (firstFault)
                    LOG("dye: CallDyeApply EXCEPTION 0x%08X on comp=%p - apply leaf unavailable on this comp, backing off",
                        exCode, reinterpret_cast<void*>(comp));
                return false;
            }
        }

        bool CallDyeUpsert(uintptr_t itemVal, const uint8_t rec[16])
        {
            DyeVector vector{};
            if (!g_dyeUpsert || !rec || rec[6] >= kDye_MaxChannels ||
                !ReadDyeVector(itemVal, vector, true)) return false;
            // Read-only comparison first: repeated restore of unchanged data
            // should not call the native allocator/upsert on every visit.
            uint8_t existing[kDye_MaxChannels][16]{};
            bool recordsValid = false;
            const uint32_t mask = ReadRecords(itemVal, existing, &recordsValid);
            if (recordsValid && (mask & (1u << rec[6])) && SameDyePayload(existing[rec[6]], rec)) return true;
            __try
            {
                g_dyeUpsert(reinterpret_cast<void*>(itemVal), rec);
                // A full vector can make the native function return without an
                // insert. Only report success when the channel actually reads back.
                if (!ReadDyeVector(itemVal, vector)) return false;
                for (uint32_t i = 0; i < vector.count; ++i)
                {
                    const auto* stored = reinterpret_cast<const uint8_t*>(vector.data + i * 16);
                    if (stored[6] == rec[6]) return SameDyePayload(stored, rec);
                }
                return false;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool TriggerEquipRefresh(uintptr_t comp)
        {
            (void)comp;
            return false;
        }

        bool CallDyeVisualSet(uintptr_t comp, uintptr_t entry, const uint8_t rec[16],
                              uint16_t tag, int channel, bool forceRetry = false)
        {
            if (!g_dyeVisualSet || !IsValidCanonicalPtr(comp) || !IsValidCanonicalPtr(entry) || !rec) {
                return false;
            }
            if (!forceRetry && IsCompFaulted(comp)) return false;
            if (channel < 0 || channel >= static_cast<int>(kDye_MaxChannels) ||
                FindEntryByTag(comp, tag) != entry) return false;
            if (!IsRenderComp(comp)) return false;

            DWORD exCode = 0;
            __try
            {
                g_dyeVisualSet(reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry),
                               rec, tag, static_cast<uint64_t>(channel), 0);
                return true;
            }
            __except (exCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
            {
                const bool firstFault = (CompFaultStrikes(comp) == 0);
                MarkCompFaulted(comp);
                if (firstFault)
                    LOG("dye: CallDyeVisualSet EXCEPTION 0x%08X on comp=%p entry=%p tag=%u ch=%d - visual push unavailable on this comp, backing off",
                        exCode, reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry), tag, channel);
                return false;
            }
        }

        bool CallDyeVisualClear(uintptr_t comp, uintptr_t entry, uint16_t tag, int channel, bool forceRetry = false)
        {
            if (!g_dyeVisualClear || !IsValidCanonicalPtr(comp) || !IsValidCanonicalPtr(entry)) return false;
            if (!forceRetry && IsCompFaulted(comp)) return false;
            if (channel < 0 || channel >= static_cast<int>(kDye_MaxChannels) ||
                FindEntryByTag(comp, tag) != entry) return false;
            if (!IsRenderComp(comp)) return false;

            DWORD exCode = 0;
            __try
            {
                g_dyeVisualClear(reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry),
                                 tag, static_cast<uint8_t>(channel), 0);
                return true;
            }
            __except (exCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
            {
                const bool firstFault = (CompFaultStrikes(comp) == 0);
                MarkCompFaulted(comp);
                if (firstFault)
                    LOG("dye: CallDyeVisualClear EXCEPTION 0x%08X on comp=%p entry=%p tag=%u ch=%d - visual clear unavailable on this comp, backing off",
                        exCode, reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry), tag, channel);
                return false;
            }
        }

        bool CallDyeApplySlot(uintptr_t comp, uint16_t tag, int channel, const uint8_t rec[16])
        {
            // TU 2.02 VERIFICATION RESULT: kSig_DyeApplySlot resolves to
            // 0x142B41E60, but that function is NOT the dye applier anymore.
            // Disasm of its only caller (0x142A8333B) shows the 2.02 contract is
            // (container-like, u16 key, counter, out: lea r9=[rsp+0x50]) - a
            // hash/registry utility that WRITES [out]=counter. Calling it with
            // dye arguments corrupts the record buffer and free-runs its inner
            // loop - the mount-dye freeze/crash. Until the real 2.02 per-slot
            // applier is located (via xrefs to DyeUpsert @ 0x142355870), this
            // path must stay disabled; DyeUpsert + DyeApplyBatch remain the
            // safe write/visual pipeline.
            (void)comp; (void)tag; (void)channel; (void)rec;
            return false;
        }

        bool CallDyeRecordRemove(uintptr_t entry, int channel)
        {
            DyeVector vector{};
            if (channel < 0 || channel >= 12 || !ReadDyeVector(entry, vector, true)) return false;
            if (g_dyeRecRemove)
            {
                __try
                {
                    g_dyeRecRemove(reinterpret_cast<void*>(entry), static_cast<uint8_t>(channel));
                    return true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            // In-place record removal fallback
            const uint32_t count = vector.count;
            const uintptr_t data = vector.data;
            if (count > 0)
            {
                if (data)
                {
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        uint8_t ch = 0;
                        if (Read8(data + static_cast<uintptr_t>(i) * 16 + 6, &ch) && ch == static_cast<uint8_t>(channel))
                        {
                            for (uint32_t j = i; j + 1 < count; ++j)
                            {
                                uint64_t w1 = 0, w2 = 0;
                                if (Read64(data + static_cast<uintptr_t>(j + 1) * 16, &w1) &&
                                    Read64(data + static_cast<uintptr_t>(j + 1) * 16 + 8, &w2))
                                {
                                    if (!Write64(data + static_cast<uintptr_t>(j) * 16, w1) ||
                                        !Write64(data + static_cast<uintptr_t>(j) * 16 + 8, w2)) return false;
                                }
                                else return false;
                            }
                            return Write32(vector.header + 8, count - 1);
                        }
                    }
                }
            }
            return false;
        }

        bool RawWrite8(uintptr_t addr, uint8_t val)
        {
            if (!addr) return false;
            __try { *reinterpret_cast<volatile uint8_t*>(addr) = val; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        uintptr_t FindSlotByInstance(uintptr_t holder, int64_t targetInstId);

        struct SavedPlayerSlot
        {
            bool     active = false;
            uint16_t tag = 0;
            uint16_t typeId = 0;          // Specific item TypeID
            int64_t  instanceId = 0;      // Specific item InstanceID
            uint32_t dyeCount = 0;
            uint8_t  records[kDye_MaxChannels][16] = {};
            uint32_t mask = 0;
        };
        static SavedPlayerSlot s_savedPlayerSlots[3][32];
        static DyeItemIdentity s_savedPlayerOrigins[3][32]; // session proof for absent instance IDs

        struct SavedMountSlot
        {
            bool     active = false;
            uint16_t tag = 0;
            uint16_t typeId = 0;          // Specific mount gear TypeID
            int64_t  instanceId = 0;      // Specific mount gear InstanceID
            uint32_t dyeCount = 0;
            uint8_t  records[kDye_MaxChannels][16] = {};
            uint32_t mask = 0;
        };
        static SavedMountSlot s_savedMountSlots[32];
        static DyeItemIdentity s_savedMountOrigins[32];

        struct SavedItemDyeRecord
        {
            uint16_t typeId = 0;
            uint32_t mask = 0;
            uint8_t  records[kDye_MaxChannels][16] = {};
            uint32_t dyeCount = 0;
        };
        static constexpr int kMaxSavedItemDyes = 512;
        static SavedItemDyeRecord s_itemDyeMap[kMaxSavedItemDyes];
        static int s_itemDyeCount = 0;

        static SavedItemDyeRecord* FindSavedItemDye(uint16_t typeId)
        {
            if (typeId == 0 || typeId == kInvSlot_EmptyType) return nullptr;
            for (int i = 0; i < s_itemDyeCount; ++i)
                if (s_itemDyeMap[i].typeId == typeId) return &s_itemDyeMap[i];
            return nullptr;
        }

        static void UpsertSavedItemDye(uint16_t typeId, uint32_t mask, const uint8_t recs[kDye_MaxChannels][16], uint32_t count)
        {
            if (typeId == 0 || typeId == kInvSlot_EmptyType) return;
            SavedItemDyeRecord* rec = FindSavedItemDye(typeId);
            if (!rec)
            {
                if (s_itemDyeCount < kMaxSavedItemDyes)
                    rec = &s_itemDyeMap[s_itemDyeCount++];
            }
            if (rec)
            {
                rec->typeId = typeId;
                rec->mask = mask;
                memcpy(rec->records, recs, sizeof(rec->records));
                rec->dyeCount = count;
            }
        }

        static void ClearSavedItemDye(uint16_t typeId)
        {
            for (int i = 0; i < s_itemDyeCount; ++i)
            {
                if (s_itemDyeMap[i].typeId == typeId)
                {
                    s_itemDyeMap[i] = s_itemDyeMap[--s_itemDyeCount];
                    break;
                }
            }
        }

        static const char* GetDyeCachePath()
        {
            static char path[MAX_PATH] = "";
            if (path[0] == 0)
            {
                GetModuleFileNameA(GetModuleHandleA("Trinity.asi"), path, MAX_PATH);
                char* lastSlash = strrchr(path, '\\');
                if (!lastSlash) lastSlash = strrchr(path, '/');
                if (lastSlash) *(lastSlash + 1) = 0;
                strcat_s(path, "Trinity_DyeCache.dat");
            }
            return path;
        }

        struct DyeCacheHeader
        {
            char     magic[8] = "TRDYE02";
            uint32_t version  = 2;
            uint32_t count    = 0;
        };

        static void SaveDyeCacheToFile()
        {
            const char* path = GetDyeCachePath();
            FILE* f = nullptr;
            if (fopen_s(&f, path, "wb") == 0 && f)
            {
                DyeCacheHeader hdr;
                hdr.count = static_cast<uint32_t>(s_itemDyeCount);
                fwrite(&hdr, sizeof(hdr), 1, f);
                fwrite(s_savedPlayerSlots, sizeof(s_savedPlayerSlots), 1, f);
                fwrite(s_savedMountSlots, sizeof(s_savedMountSlots), 1, f);
                if (s_itemDyeCount > 0)
                    fwrite(s_itemDyeMap, sizeof(SavedItemDyeRecord), s_itemDyeCount, f);
                fclose(f);
            }
        }

        static void LoadDyeCacheFromFile()
        {
            const char* path = GetDyeCachePath();
            FILE* f = nullptr;
            if (fopen_s(&f, path, "rb") == 0 && f)
            {
                DyeCacheHeader hdr{};
                if (fread(&hdr, sizeof(hdr), 1, f) == 1 && strcmp(hdr.magic, "TRDYE02") == 0)
                {
                    fread(s_savedPlayerSlots, sizeof(s_savedPlayerSlots), 1, f);
                    fread(s_savedMountSlots, sizeof(s_savedMountSlots), 1, f);
                    s_itemDyeCount = static_cast<int>(hdr.count);
                    if (s_itemDyeCount > kMaxSavedItemDyes) s_itemDyeCount = kMaxSavedItemDyes;
                    if (s_itemDyeCount > 0)
                        fread(s_itemDyeMap, sizeof(SavedItemDyeRecord), s_itemDyeCount, f);
                }
                else
                {
                    fseek(f, 0, SEEK_SET);
                    fread(s_savedPlayerSlots, sizeof(s_savedPlayerSlots), 1, f);
                    fread(s_savedMountSlots, sizeof(s_savedMountSlots), 1, f);
                }
                fclose(f);
            }
        }

        struct DyeRealmGuard
        {
            uint8_t previous = 0;
            uintptr_t address = Inventory::RealmFlagAddress(&previous);
            bool active = false;
            explicit DyeRealmGuard(uint8_t realm) { active = address && RawWrite8(address, realm); }
            ~DyeRealmGuard() { if (active) RawWrite8(address, previous); }
        };

        bool SourceItemValid(const DyeItemIdentity& item)
        {
            uintptr_t root = 0;
            if (g_clientRegistryGlobal) ReadPtr(g_clientRegistryGlobal, &root);
            if (!Player::Ready() || !item.controlledOwner || Player::GetControlledOwner() != item.controlledOwner)
                return false;
            if (root != item.worldRoot || item.worldGeneration != s_worldGeneration.load(std::memory_order_acquire))
                return false;
            if (item.mode == 2)
                return item.instance > 0 && Inventory::ClientHolderAddr() == item.sourceComp &&
                       FindSlotByInstance(item.sourceComp, item.instance) == item.sourceEntry &&
                       MatchesItemValue(item.sourceEntry, item);
            if (item.mode == 1 && FindMountComp(item.mount) != item.sourceComp) return false;
            return MatchingDyeEntry(item.sourceComp, item) == item.sourceEntry && item.sourceEntry;
        }

        bool SavedItemMatches(const DyeItemIdentity& item, uint16_t type, int64_t instance,
                              const DyeItemIdentity& origin)
        {
            if (type != item.type || !SourceItemValid(item)) return false;
            if (instance > 0) return item.instance == instance;
            // Disk records without IDs cannot identify a new allocation. Only
            // retain them on the original source during this same world lifetime.
            return item.instance == instance && SourceItemValid(origin) &&
                   item.sourceComp == origin.sourceComp && item.sourceEntry == origin.sourceEntry &&
                   item.sourceOwner == origin.sourceOwner && item.controlledOwner == origin.controlledOwner;
        }

        bool CallEquipDyeUpsert(uintptr_t comp, const DyeItemIdentity& item, const uint8_t rec[16])
        {
            if (!SourceItemValid(item)) return false;
            const uintptr_t entry = MatchingDyeEntry(comp, item);
            const NativeEquipKind kind = GetNativeEquipKind(comp);
            if (!entry || (kind != NativeEquipKind::Client && kind != NativeEquipKind::Server)) return false;
            DyeRealmGuard realm(kind == NativeEquipKind::Server ? 1 : 0);
            return realm.active && SourceItemValid(item) && MatchingDyeEntry(comp, item) == entry &&
                   CallDyeUpsert(entry, rec);
        }

        uint32_t UpsertDyeMask(uintptr_t entry, const DyeItemIdentity& item,
                               const uint8_t recs[kDye_MaxChannels][16], uint32_t mask,
                               uintptr_t targetComp = 0, uintptr_t targetHolder = 0)
        {
            uint32_t written = 0;
            for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
            {
                if (!(mask & (1u << ch))) continue;
                if (!SourceItemValid(item) || !MatchesItemValue(entry, item)) break;
                if (targetComp && MatchingDyeEntry(targetComp, item) != entry) break;
                if (targetHolder && FindSlotByInstance(targetHolder, item.instance) != entry) break;
                if (recs[ch][6] == ch && CallDyeUpsert(entry, recs[ch])) written |= 1u << ch;
            }
            return written;
        }

        bool MirrorToServer(const DyeItemIdentity& item,
                            const uint8_t recs[kDye_MaxChannels][16], uint32_t mask, bool background = false)
        {
            constexpr uint32_t allChannels = (1u << kDye_MaxChannels) - 1;
            if (!g_dyeUpsert || !mask || (mask & ~allChannels) || !SourceItemValid(item)) return false;
            const uintptr_t controlledOwner = Player::GetControlledOwner();
            DyeRealmGuard realm(1);
            if (!realm.active) return false;

            // Explicit identity/mode from the caller, never current UI selection.
            // Patch only the supplied channels. Clearing uses explicit clear
            // records; no count reset can erase untouched channels on partial edits.
            bool ok = false;
            uintptr_t seen[40] = {};
            int seenCount = 0;
            auto mirrorComp = [&](uintptr_t comp) {
                if (!controlledOwner || Player::GetControlledOwner() != controlledOwner ||
                    !SourceItemValid(item) || GetNativeEquipKind(comp) != NativeEquipKind::Server) return;
                for (int i = 0; i < seenCount; ++i) if (seen[i] == comp) return;
                if (seenCount >= 40) return;
                seen[seenCount++] = comp;
                const uintptr_t entry = MatchingDyeEntry(comp, item);
                if (entry) ok |= UpsertDyeMask(entry, item, recs, mask, comp) == mask;
            };
            mirrorComp(item.sourceComp);
            if (item.mode == 0)
            {
                mirrorComp(ProfileComp(item.character));
                mirrorComp(CompForCharacter(Player::GetOwner(item.character), item.character, item.character));
                mirrorComp(CompForCharacter(Player::GetActor(item.character), item.character, item.character));
                if (!background)
                {
                    uintptr_t roots[16] = {};
                    const int count = Inventory::CharacterAddrs(item.character, roots, 16);
                    for (int i = 0; i < count; ++i)
                        mirrorComp(CompForCharacter(roots[i], item.character, item.character));
                    uintptr_t party[8] = {};
                    const int partyCount = PartyCompanionComps(item.character, party, 8);
                    for (int i = 0; i < partyCount; ++i) mirrorComp(party[i]);
                }
                if (item.character == Player::GetActiveCharacterIdx())
                    mirrorComp(CompForCharacter(Inventory::ServerCharacterAddr(), item.character, item.character));
            }
            else if (item.mode == 1)
            {
                for (int m = 0; m < 4; ++m) mirrorComp(FindMountComp(m));
            }
            // Holder copies always require a positive exact instance AND type.
            if (!background && item.instance > 0 && Player::GetControlledOwner() == controlledOwner && SourceItemValid(item))
            {
                const uintptr_t holder = Inventory::ServerHolderAddr();
                const uintptr_t slot = FindSlotByInstance(holder, item.instance);
                if (MatchesItemValue(slot, item)) ok |= UpsertDyeMask(slot, item, recs, mask, 0, holder) == mask;
            }
            return ok;
        }

        struct DyeHolderCursor
        {
            uintptr_t holder = 0, buckets = 0;
            uint32_t count = 0, bucket = 0, row = 0;
            uintptr_t currentBucket = 0, slots = 0;
            uint16_t slotCount = 0;

            void ResetBucket()
            {
                row = 0;
                currentBucket = slots = 0;
                slotCount = 0;
            }
        };
        enum class DyeHolderMirrorResult { Pending, Unavailable, Missing, Done };

        // Background durability still reaches the exact server bag copy, but
        // its bucket walk is resumed across slot visits, never 4096*8192 reads.
        DyeHolderMirrorResult MirrorHolderSlice(const DyeItemIdentity& item,
                                               const uint8_t recs[kDye_MaxChannels][16], uint32_t mask,
                                               DyeHolderCursor& cursor)
        {
            constexpr uint32_t allChannels = (1u << kDye_MaxChannels) - 1;
            if (item.instance <= 0 || !mask || (mask & ~allChannels) || !SourceItemValid(item))
                return DyeHolderMirrorResult::Unavailable;
            DyeRealmGuard realm(1);
            if (!realm.active) return DyeHolderMirrorResult::Unavailable;
            const uintptr_t holder = Inventory::ServerHolderAddr();
            uintptr_t buckets = 0;
            uint32_t count = 0;
            if (!IsValidCanonicalPtr(holder) || !ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) ||
                !IsValidCanonicalPtr(buckets) || !Read32(holder + kOff_InvHolder_Count, &count) || count > 4096)
            {
                cursor = {};
                return DyeHolderMirrorResult::Unavailable;
            }
            if (cursor.holder != holder || cursor.buckets != buckets || cursor.count != count)
                cursor = { holder, buckets, count, 0, 0 };
            uint32_t visitedBuckets = 0, visitedSlots = 0;
            while (cursor.bucket < count && visitedBuckets++ < 16 && visitedSlots < 256)
            {
                const uintptr_t bucketAddress = buckets + static_cast<uintptr_t>(cursor.bucket) * 8;
                uintptr_t bucket = 0, slots = 0;
                uint16_t slotCount = 0;
                if (!ReadPtr(bucketAddress, &bucket))
                {
                    cursor.ResetBucket();
                    return DyeHolderMirrorResult::Unavailable;
                }
                if (!bucket) { ++cursor.bucket; cursor.ResetBucket(); continue; }
                if (!IsValidCanonicalPtr(bucket) || !ReadPtr(bucket + kOff_InvBucket_Slots, &slots) ||
                    !Read16(bucket + kOff_InvBucket_Count, &slotCount) || slotCount > 8192 ||
                    (slotCount && !IsValidCanonicalPtr(slots)))
                {
                    cursor.ResetBucket();
                    return DyeHolderMirrorResult::Unavailable;
                }
                // A stable holder does not imply a stable bucket allocation.
                // Restart this bucket before consuming a saved row offset.
                if (cursor.currentBucket != bucket || cursor.slots != slots || cursor.slotCount != slotCount)
                {
                    cursor.ResetBucket();
                    cursor.currentBucket = bucket;
                    cursor.slots = slots;
                    cursor.slotCount = slotCount;
                }
                auto bucketLinked = [&]() {
                    uintptr_t current = 0;
                    uint32_t currentCount = 0;
                    uint16_t currentSlots = 0;
                    return SourceItemValid(item) && Inventory::ServerHolderAddr() == holder &&
                        ReadPtr(holder + kOff_InvHolder_Buckets, &current) && current == buckets &&
                        Read32(holder + kOff_InvHolder_Count, &currentCount) && currentCount == count &&
                        ReadPtr(bucketAddress, &current) && current == bucket &&
                        ReadPtr(bucket + kOff_InvBucket_Slots, &current) && current == slots &&
                        Read16(bucket + kOff_InvBucket_Count, &currentSlots) && currentSlots == slotCount;
                };
                while (cursor.row < slotCount && visitedSlots++ < 256)
                {
                    const uint32_t row = cursor.row++;
                    const uintptr_t entry = slots + static_cast<uintptr_t>(row) * core::GetSlotStride();
                    if (!MatchesItemValue(entry, item)) continue;
                    auto linked = [&]() {
                        return bucketLinked() && row < slotCount && MatchesItemValue(entry, item);
                    };
                    for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
                    {
                        if (!(mask & (1u << ch))) continue;
                        if (!linked() || recs[ch][6] != ch || !CallDyeUpsert(entry, recs[ch]))
                        {
                            cursor.row = row;
                            return DyeHolderMirrorResult::Unavailable;
                        }
                    }
                    if (!linked())
                    {
                        cursor.ResetBucket();
                        return DyeHolderMirrorResult::Unavailable;
                    }
                    return DyeHolderMirrorResult::Done;
                }
                if (!bucketLinked())
                {
                    cursor.ResetBucket();
                    return DyeHolderMirrorResult::Unavailable;
                }
                if (cursor.row >= slotCount) { ++cursor.bucket; cursor.ResetBucket(); }
            }
            if (cursor.bucket >= count)
            {
                // Missing is not durable success. Retry a new bounded walk
                // after backoff; equipment can stream into an earlier bucket.
                cursor = {};
                return DyeHolderMirrorResult::Missing;
            }
            return DyeHolderMirrorResult::Pending;
        }

        struct Request
        {
            uint16_t     tag     = 0;
            int          channel = -1;   // -1 = all 12
            bool         clear   = false;
            Dye::Channel value{};
            int mode = 0;
            int selectedCharacter = -1;
            int selectedMount = 0;
            uint64_t selectionGeneration = 0;
            uintptr_t controlledOwner = 0;
            DyeItemIdentity item{};
            bool saveProfile = false;
            bool applyProfile = false;
            uint32_t replaceProfileId = 0;
            DyeProfile profile{};
            unsigned mountNext = 0;
            uint32_t mountDataMask = 0, mountVisualMask = 0;
            ULONGLONG nextVisit = 0;
            unsigned retouchFields = 0;
            bool recordsPrepared = false;
            uint32_t retouchMask = 0;
            uint8_t retouchRecords[kDye_MaxChannels][16]{};
        };
        Request          g_req;
        std::atomic<int> g_state{ static_cast<int>(Dye::OpState::Idle) };

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

        bool DyeRegistryHas(uint32_t h)
        {
            int lo = 0, hi = kDyeablePrefabCount - 1;
            while (lo <= hi)
            {
                const int mid = (lo + hi) / 2;
                if (kDyeablePrefabHashes[mid] == h) return true;
                if (kDyeablePrefabHashes[mid] < h)  lo = mid + 1;
                else                                hi = mid - 1;
            }
            return false;
        }

        bool IconPrefabDyeable(const char* icon)
        {
            if (!icon || !icon[0]) return true;
            const char* p = nullptr;
            for (const char* c = icon; *c; ++c)
            {
                if ((*c == 'p' || *c == 'P') && _strnicmp(c, "prefab_", 7) == 0)
                {
                    p = c + 7;
                    break;
                }
            }
            if (!p || !p[0]) return true;

            size_t len = strlen(p);
            for (int strip = 0; strip < 4 && len > 3; ++strip)
            {
                if (DyeRegistryHas(HashPrefabLower(p, len)))
                    return true;
                size_t cut = len;
                while (cut > 0 && p[cut - 1] != '_') --cut;
                if (cut == 0) break;
                len = cut - 1;
            }
            return false;
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

        constexpr int    kMaxSlots = 64;
        constexpr ULONGLONG kUiRefreshMs = 200;
        constexpr ULONGLONG kUiGraceMs = 750;
        struct DyeSelectionKey
        {
            uintptr_t controlledOwner = 0, worldRoot = 0, mountOwner = 0, mountActor = 0;
            int character = -1, selectedCharacter = -1, mode = 0, mount = 0;
            uint64_t generation = 0, worldGeneration = 0;
            bool Same(const DyeSelectionKey& other) const
            {
                return controlledOwner == other.controlledOwner && worldRoot == other.worldRoot &&
                    mountOwner == other.mountOwner && mountActor == other.mountActor &&
                    character == other.character && selectedCharacter == other.selectedCharacter &&
                    mode == other.mode && mount == other.mount && generation == other.generation &&
                    worldGeneration == other.worldGeneration;
            }
        };

        DyeSelectionKey CurrentDyeSelection()
        {
            DyeSelectionKey key{};
            key.generation = s_selectionGeneration.load(std::memory_order_acquire);
            key.worldGeneration = s_worldGeneration.load(std::memory_order_acquire);
            key.controlledOwner = Player::GetControlledOwner();
            if (g_clientRegistryGlobal) ReadPtr(g_clientRegistryGlobal, &key.worldRoot);
            key.selectedCharacter = s_activeCharIdx.load();
            // Identity-only cached accessor: never scan equipment to key a UI read.
            key.character = key.selectedCharacter < 0 ? Player::GetActiveCharacterIdx() : key.selectedCharacter;
            key.mode = s_targetMode.load();
            key.mount = s_activeMountIdx.load();
            if (key.mode == 1)
            {
                key.mountOwner = Player::GetMountOwner(key.mount);
                key.mountActor = Player::GetMountActor(key.mount);
            }
            return key;
        }

        struct DyeSnapshotRow
        {
            DyeItemIdentity item{};
            uint32_t mask = 0;
            uint8_t records[kDye_MaxChannels][16] = {};
        };
        struct DyeSnapshot
        {
            Dye::SlotInfo slots[kMaxSlots]{};
            DyeSnapshotRow rows[kMaxSlots]{};
            int count = 0;
        };
        // Menu-owned copies. Tick/hooks/setters only publish an atomic epoch;
        // lock contention must never make GetSlot drop half the displayed rows.
        Dye::SlotInfo    g_slots[kMaxSlots];
        int              g_slotCount = 0;
        DyeSnapshotRow   s_uiRows[kMaxSlots];
        DyeSelectionKey  s_uiKey{};
        ULONGLONG        s_uiLastAttempt = 0, s_uiLastSuccess = 0;
        bool             s_uiReady = false, s_uiAttempted = false;

        uintptr_t FindSlotByInstance(uintptr_t holder, int64_t targetInstId)
        {
            if (holder < kMinPointer || targetInstId <= 0) return 0;
            uintptr_t buckets = 0;
            uint32_t  bcount  = 0;
            if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets)) return 0;
            if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount > 4096) return 0;

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
                    const uintptr_t slot = slots + static_cast<uintptr_t>(i) * core::GetSlotStride();
                    int64_t inst = 0;
                    if (Read64(slot + kOff_ItemVal_InstanceId, &inst) && inst == targetInstId)
                        return slot;
                }
            }
            return 0;
        }

        bool CopySnapshotRow(DyeSnapshot& snapshot, const DyeSelectionKey& key,
                             uintptr_t comp, uintptr_t owner, uintptr_t entry, uint16_t tag)
        {
            uint16_t type = 0;
            int64_t quantity = 0, instance = 0;
            if (!Read16(entry + kOff_InvSlot_TypeId, &type)) return false;
            if (!type || type == kInvSlot_EmptyType) return true;
            if (!Read64(entry + kOff_InvSlot_Quantity, &quantity) ||
                !Read64(entry + kOff_ItemVal_InstanceId, &instance)) return false;
            if (quantity <= 0 || (key.mode == 2 && instance <= 0)) return true;
            char name[96]{}, icon[128]{};
            if (!Inventory::NameForTypeId(type, name, sizeof(name)))
                snprintf(name, sizeof(name), "Item #%u", type);
            Inventory::IconForTypeId(type, icon, sizeof(icon));
            const HorseSlotType horse = GetHorseSlotType(name, icon);
            const char* slotName = nullptr;
            if (key.mode == 1)
            {
                // Native local tags outrank name/icon substrings (e.g.
                // HorseShoe_HorseArmor_Stirrup_V is the stirrup in tag 3).
                slotName = NativeMountSlotName(tag);
                if (!slotName) return true;
            }
            else
            {
                if (IsDummyOrUnarmed(type, name)) return true;
                if (key.mode == 0)
                {
                    if (tag == 14 || (tag >= 22 && tag <= 25)) return true;
                    slotName = SlotNameForTag(tag);
                }
                else
                {
                    if (!IconPrefabDyeable(icon) && horse == HorseSlotType::None) return true;
                    if (horse != HorseSlotType::None) slotName = MountSlotName(horse);
                }
            }
            Dye::SlotInfo& slot = snapshot.slots[snapshot.count];
            DyeSnapshotRow& row = snapshot.rows[snapshot.count];
            row.item = { key.character, tag, type, instance };
            row.item.mode = key.mode;
            row.item.mount = key.mount;
            row.item.sourceComp = comp;
            row.item.sourceEntry = entry;
            row.item.sourceOwner = owner;
            row.item.controlledOwner = key.controlledOwner;
            row.item.worldRoot = key.worldRoot;
            row.item.worldGeneration = key.worldGeneration;
            bool recordsValid = false;
            row.mask = ReadRecords(entry, row.records, &recordsValid);
            if (!recordsValid || !MatchesItemValue(entry, row.item)) return false;
            slot.tag = tag;
            slot.typeId = type;
            slot.instanceId = instance;
            slot.maxZones = 12;
            slot.dyeable = true;
            for (uint32_t mask = row.mask; mask; mask &= mask - 1) ++slot.dyeCount;
            if (slotName) snprintf(slot.slotName, sizeof(slot.slotName), "%s", slotName);
            else snprintf(slot.slotName, sizeof(slot.slotName), key.mode == 2 ? "Bag Item %u" : "Slot %u",
                          key.mode == 2 ? tag + 1 : tag);
            snprintf(slot.itemName, sizeof(slot.itemName), "%s", name);
            snprintf(slot.icon, sizeof(slot.icon), "%s", icon);
            ++snapshot.count;
            return true;
        }

        bool RebuildSnapshot(const DyeSelectionKey& key, DyeSnapshot& snapshot)
        {
            if (!key.controlledOwner || !Player::Ready() ||
                (key.mode == 0 && (key.character < 0 || key.character > 2))) return false;
            if (key.mode == 2)
            {
                const uintptr_t holder = Inventory::ClientHolderAddr();
                uintptr_t buckets = 0;
                uint32_t count = 0;
                if (!IsValidCanonicalPtr(holder) || !ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) ||
                    !IsValidCanonicalPtr(buckets) || !Read32(holder + kOff_InvHolder_Count, &count) || count > 4096) return false;
                uint32_t visited = 0;
                for (uint32_t b = 0; b < count && snapshot.count < kMaxSlots; ++b)
                {
                    uintptr_t bucket = 0, slots = 0;
                    uint16_t slotCount = 0;
                    if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket)) return false;
                    if (!bucket) continue;
                    if (!IsValidCanonicalPtr(bucket) || !Read16(bucket + kOff_InvBucket_Count, &slotCount) ||
                        slotCount > 8192) return false;
                    if (!slotCount) continue;
                    if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || !IsValidCanonicalPtr(slots)) return false;
                    for (uint16_t i = 0; i < slotCount && snapshot.count < kMaxSlots; ++i)
                    {
                        if (++visited > 8192) return false; // total work, not per bucket
                        if (!CopySnapshotRow(snapshot, key, holder, 0,
                            slots + static_cast<uintptr_t>(i) * core::GetSlotStride(),
                            static_cast<uint16_t>(snapshot.count))) return false;
                    }
                    uintptr_t current = 0;
                    uint16_t currentCount = 0;
                    if (!ReadPtr(bucket + kOff_InvBucket_Slots, &current) || current != slots ||
                        !Read16(bucket + kOff_InvBucket_Count, &currentCount) || currentCount != slotCount) return false;
                }
                uintptr_t current = 0;
                uint32_t currentCount = 0;
                return Inventory::ClientHolderAddr() == holder &&
                    ReadPtr(holder + kOff_InvHolder_Buckets, &current) && current == buckets &&
                    Read32(holder + kOff_InvHolder_Count, &currentCount) && currentCount == count;
            }
            const uintptr_t comp = ClientComp(true);
            const EquipTableDesc table = ReadNativeEquipmentTable(comp);
            uintptr_t owner = 0;
            if (!table.valid || !ReadPtr(comp + kOff_EquipComp_Owner, &owner)) return false;
            for (uint32_t i = 0; i < table.count && snapshot.count < kMaxSlots; ++i)
            {
                const uintptr_t entry = table.array + static_cast<uintptr_t>(i) * table.stride;
                uint16_t tag = 0;
                if (!Read16(entry + table.tagOffset, &tag) ||
                    !CopySnapshotRow(snapshot, key, comp, owner, entry, tag)) return false;
            }
            const EquipTableDesc current = ReadNativeEquipmentTable(comp);
            uintptr_t currentOwner = 0;
            if (!current.valid || current.desc != table.desc || current.array != table.array ||
                current.count != table.count || !ReadPtr(comp + kOff_EquipComp_Owner, &currentOwner) ||
                currentOwner != owner) return false;
            for (int i = 0; i < snapshot.count; ++i)
            {
                const DyeItemIdentity& item = snapshot.rows[i].item;
                uint16_t tag = 0;
                if (!MatchesItemValue(item.sourceEntry, item) ||
                    !Read16(item.sourceEntry + table.tagOffset, &tag) || tag != item.tag) return false;
            }
            return true;
        }

        bool UiSnapshotUsable()
        {
            if (!s_uiKey.Same(CurrentDyeSelection()))
                return false;
            return s_uiReady && GetTickCount64() - s_uiLastSuccess <= kUiGraceMs;
        }

        void EnsureUiSnapshot()
        {
            const DyeSelectionKey key = CurrentDyeSelection();
            const bool changed = !s_uiKey.Same(key);
            if (changed)
            {
                s_uiKey = key;
                g_slotCount = 0;
                s_uiReady = s_uiAttempted = false;
            }
            const ULONGLONG now = GetTickCount64();
            // Ready/SlotCount share a 200ms sample; an epoch change during the
            // copy rejects publication instead of exposing a torn equipment set.
            const uint64_t epoch = s_uiEpoch.load(std::memory_order_acquire);
            if (s_uiAttempted && now - s_uiLastAttempt < kUiRefreshMs) return;
            s_uiAttempted = true;
            s_uiLastAttempt = now;
            DyeOperationGuard operation;
            if (!operation.acquired) return; // keep the owned rows through contention
            static thread_local DyeSnapshot next{};
            next = {};
            if (RebuildSnapshot(key, next) && key.Same(CurrentDyeSelection()) &&
                epoch == s_uiEpoch.load(std::memory_order_acquire))
            {
                memcpy(g_slots, next.slots, sizeof(g_slots));
                memcpy(s_uiRows, next.rows, sizeof(s_uiRows));
                g_slotCount = next.count;
                s_uiReady = true;
                s_uiLastSuccess = GetTickCount64();
            }
            // Failure/lock contention retains only owned copies, never live
            // pointers for mutation, and never extends the last-success TTL.
        }

        bool RequestStillValid(const Request& req)
        {
            if (!Player::Ready() || !req.controlledOwner ||
                Player::GetControlledOwner() != req.controlledOwner ||
                s_selectionGeneration.load(std::memory_order_acquire) != req.selectionGeneration ||
                s_targetMode.load() != req.mode || s_activeCharIdx.load() != req.selectedCharacter ||
                s_activeMountIdx.load() != req.selectedMount || !SourceItemValid(req.item)) return false;
            if (req.mode == 2) return true;
            if (req.mode == 0 && req.selectedCharacter < 0 &&
                Player::GetActiveCharacterIdx() != req.item.character) return false;
            return ClientComp() == req.item.sourceComp;
        }

        bool EnqueueDye(uint16_t tag, int channel, bool clear, const Dye::Channel& value,
                        const DyeProfile* profile = nullptr, bool saveProfile = false, uint32_t replaceId = 0,
                        unsigned retouchFields = 0)
        {
            DyeOperationGuard operation;
            if (!operation.acquired || channel < -1 || channel >= static_cast<int>(kDye_MaxChannels)) return false;
            const bool pending = g_state.load(std::memory_order_acquire) == static_cast<int>(Dye::OpState::Pending);
            // Only an unstarted retouch can be coalesced/replaced. Once a mount
            // begins its staged channels, the payload is immutable until done.
            if (pending && (!g_req.retouchFields || g_req.recordsPrepared)) return false;
            Request req{ tag, channel, clear, value };
            req.selectionGeneration = s_selectionGeneration.load(std::memory_order_acquire);
            req.mode = s_targetMode.load();
            req.selectedCharacter = s_activeCharIdx.load();
            req.selectedMount = s_activeMountIdx.load();
            req.controlledOwner = Player::GetControlledOwner();
            if (!UiSnapshotUsable()) return false;
            const DyeSnapshotRow* selectedRow = nullptr;
            for (int i = 0; i < g_slotCount; ++i)
                if (g_slots[i].tag == tag) { selectedRow = &s_uiRows[i]; break; }
            if (!selectedRow) return false;
            if (req.mode == 2)
            {
                // Bag tags index the UI snapshot. Capture its identity now;
                // ProcessRequest never remaps a possibly rebuilt g_slots index.
                const DyeItemIdentity& selected = selectedRow->item;
                if (selected.instance <= 0) return false;
                req.item.mode = 2;
                req.item.tag = tag;
                req.item.type = selected.type;
                req.item.instance = selected.instance;
                req.item.sourceComp = Inventory::ClientHolderAddr();
                req.item.sourceEntry = FindSlotByInstance(req.item.sourceComp, req.item.instance);
                req.item.controlledOwner = req.controlledOwner;
                req.item.worldRoot = selectedRow->item.worldRoot;
                req.item.worldGeneration = selectedRow->item.worldGeneration;
            }
            else
            {
                const int character = req.selectedCharacter < 0
                    ? Player::GetActiveCharacterIdx() : req.selectedCharacter;
                if (!ReadDyeItem(ClientComp(), tag, character, req.item, req.mode, req.selectedMount)) return false;
            }
            // A grace-period row is display-only. Do not dye replacement gear
            // that appeared under its tag while the old row was still visible.
            const DyeItemIdentity& displayed = selectedRow->item;
            if (req.item.type != displayed.type || req.item.instance != displayed.instance ||
                req.item.controlledOwner != displayed.controlledOwner ||
                req.item.worldRoot != displayed.worldRoot || req.item.worldGeneration != displayed.worldGeneration ||
                (req.mode != 2 && (req.item.sourceComp != displayed.sourceComp ||
                    req.item.sourceOwner != displayed.sourceOwner || req.item.sourceEntry != displayed.sourceEntry))) return false;
            if (!RequestStillValid(req)) return false;
            if (pending)
            {
                if (!RequestStillValid(g_req) || req.selectionGeneration != g_req.selectionGeneration ||
                    req.tag != g_req.tag || req.channel != g_req.channel ||
                    req.item.type != g_req.item.type || req.item.instance != g_req.item.instance ||
                    req.item.sourceComp != g_req.item.sourceComp || req.item.sourceEntry != g_req.item.sourceEntry ||
                    req.item.worldGeneration != g_req.item.worldGeneration) return false;
                if (retouchFields)
                {
                    // Keep the previous pending field when the other control
                    // changes during debounce; latest value wins per field.
                    if (!(retouchFields & DyeRetouchMaterial)) req.value.materialId = g_req.value.materialId;
                    if (!(retouchFields & DyeRetouchCondition)) req.value.repair = g_req.value.repair;
                    retouchFields |= g_req.retouchFields;
                }
            }
            req.retouchFields = retouchFields;
            if (retouchFields) req.nextVisit = GetTickCount64() + 350;
            if (profile)
            {
                if (req.mode > 1) return false;
                req.profile = *profile;
                req.saveProfile = saveProfile;
                req.applyProfile = !saveProfile;
                req.replaceProfileId = replaceId;
                if (saveProfile)
                {
                    req.profile.typeId = req.item.type;
                    req.profile.mode = static_cast<uint8_t>(req.mode);
                    req.profile.revision = core::GetGameVersion().revision;
                    Inventory::NameForTypeId(req.item.type, req.profile.itemName, sizeof(req.profile.itemName));
                }
                else if (!DyeProfileCompatible(req.profile, core::GetGameVersion().revision, req.item.type, req.mode)) return false;
            }
            g_req = req;
            g_state.store(static_cast<int>(Dye::OpState::Pending), std::memory_order_release);
            return true;
        }

        void BuildRequestRecord(const Request& req, int channel, uint8_t (&record)[16])
        {
            if (req.retouchFields) memcpy(record, req.retouchRecords[channel], sizeof(record));
            else if (req.applyProfile) DyeProfileRecord(req.profile, channel, record);
            else if (req.clear) BuildClearRecord(record, channel);
            else BuildSetRecord(record, channel, req.value);
        }

        bool ClearRequestRecord(const uint8_t (&rec)[16])
        {
            return IsClearDyeRecord(rec);
        }

        uint32_t RequestedDyeMask(const Request& req)
        {
            return req.retouchFields ? req.retouchMask : req.channel < 0 ? 0xFFFu : 1u << req.channel;
        }

        bool ProfileReadbackMatches(const Request& req, uintptr_t entry)
        {
            if (!req.applyProfile && !req.retouchFields) return true;
            bool valid = false;
            uint8_t records[12][16]{};
            const uint32_t mask = ReadRecords(entry, records, &valid);
            if (!valid || !RequestStillValid(req)) return false;
            if (req.applyProfile && mask != 0xFFFu) return false;
            const uint32_t requested = RequestedDyeMask(req);
            if ((mask & requested) != requested) return false;
            for (unsigned ch = 0; ch < 12; ++ch)
            {
                uint8_t expected[16]{};
                if (!(requested & (1u << ch))) continue;
                BuildRequestRecord(req, ch, expected);
                if (!SameDyePayload(records[ch], expected)) return false;
            }
            return true;
        }

        void ProcessRequest()
        {
            if (g_req.retouchFields && !g_req.recordsPrepared)
            {
                // Capture at execution, after preceding color operations, not
                // from a potentially stale UI snapshot. No source pointer is
                // trusted until the original queued identity is revalidated.
                bool valid = false;
                uint8_t source[kDye_MaxChannels][16]{};
                if (!RequestStillValid(g_req))
                {
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }
                const uint32_t mask = ReadRecords(g_req.item.sourceEntry, source, &valid);
                if (!valid || !RequestStillValid(g_req))
                {
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }
                g_req.retouchMask = BuildDyeRetouchRecords(source, mask, g_req.channel, g_req.retouchFields,
                    g_req.value.materialId, g_req.value.repair, g_req.retouchRecords);
                g_req.recordsPrepared = true;
                if (!g_req.retouchMask)
                {
                    g_state.store(static_cast<int>(Dye::OpState::NoDyedZones), std::memory_order_release);
                    return;
                }
            }
            const Request req = g_req;
            if (!RequestStillValid(req))
            {
                LOG_WARN("dye: queued target changed; request discarded.");
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }
            const DyeItemIdentity& item = req.item;
            if (req.saveProfile)
            {
                DyeProfile captured = req.profile;
                bool valid = false;
                captured.mask = ReadRecords(item.sourceEntry, captured.records, &valid);
                if (!valid || !RequestStillValid(req))
                {
                    g_state.store(static_cast<int>(Dye::OpState::ProfileSaveFailed), std::memory_order_release);
                    return;
                }
                try
                {
                    const uint32_t replaceId = req.replaceProfileId;
                    s_profileSave = std::async(std::launch::async, [captured, replaceId] {
                        // Owned value copy only; file I/O never reads game memory.
                        return s_profiles.Save(captured, replaceId);
                    });
                }
                catch (...) { g_state.store(static_cast<int>(Dye::OpState::ProfileSaveFailed), std::memory_order_release); }
                return; // capture only: no native write or dye cache replay
            }
            DyeRealmGuard clientRealm(0);
            if (!clientRealm.active)
            {
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }

            // ===== INVENTORY BAG ITEM MODE (Mode 2) ==========================
            if (req.mode == 2)
            {
                const uintptr_t clientSlot = item.sourceEntry;
                const int chFirst = (req.channel < 0) ? 0 : req.channel;
                const int chLast  = (req.channel < 0) ? static_cast<int>(kDye_MaxChannels) - 1 : req.channel;
                uint8_t recs[kDye_MaxChannels][16] = {};
                uint32_t mask = 0;
                for (int ch = chFirst; ch <= chLast; ++ch)
                {
                    if (!(RequestedDyeMask(req) & (1u << ch))) continue;
                    BuildRequestRecord(req, ch, recs[ch]);
                    mask |= 1u << ch;
                }
                uint32_t written = 0;
                {
                    DyeRealmGuard realm(0);
                    if (realm.active && RequestStillValid(req))
                        written = UpsertDyeMask(clientSlot, item, recs, mask, 0, item.sourceComp);
                }
                const bool serverOk = RequestStillValid(req) && MirrorToServer(item, recs, written);
                if (!RequestStillValid(req))
                {
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }
                g_state.store(static_cast<int>((written == mask && serverOk) ? Dye::OpState::Done :
                                              written ? Dye::OpState::DoneDataOnly : Dye::OpState::Failed),
                              std::memory_order_release);
                return;
            }

            // ===== MOUNT MODE: data upsert + visual leaves ===================
            if (req.mode == 1)
            {
                const uintptr_t comp = item.sourceComp;
                if (!comp)
                {
                    LOG_WARN("dye: mount equip component not resolved.");
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }

                uintptr_t entry = MatchingDyeEntry(comp, item);
                if (!entry)
                {
                    LOG_WARN("dye: no equipped entry for mount slot tag %u.", req.tag);
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }

                int64_t instId = 0;
                Read64(entry + kOff_ItemVal_InstanceId, &instId);

                // One channel per visit, including profiles. Keep the original
                // request identity until the full appearance has completed.
                int chFirst = (req.channel < 0) ? static_cast<int>(req.mountNext) : req.channel;
                const uint32_t requested = RequestedDyeMask(req);
                while (chFirst < static_cast<int>(kDye_MaxChannels) && !(requested & (1u << chFirst))) ++chFirst;
                if (chFirst >= static_cast<int>(kDye_MaxChannels))
                {
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }
                const int chLast = chFirst;
                bool upsertOk = false;
                const DyeRenderTarget render = ResolveDyeRenderTarget(item, comp);

                for (int ch = chFirst; ch <= chLast; ++ch)
                {
                    uint8_t rec[16] = {};
                    BuildRequestRecord(req, ch, rec);
                    if (RequestStillValid(req)) upsertOk = CallEquipDyeUpsert(comp, item, rec);
                    if (upsertOk) g_req.mountDataMask |= 1u << ch;
                    if (upsertOk && render.comp && RequestStillValid(req) &&
                        CallEquipDyeUpsert(render.comp, item, rec) && RequestStillValid(req))
                    {
                        // The leaf resolves parts from this horse's catalog key
                        // and local slot. Do not send a player dye-ack batch.
                        const bool painted = ClearRequestRecord(rec)
                            ? CallDyeVisualClear(render.comp, render.entry, req.tag, ch)
                            : CallDyeVisualSet(render.comp, render.entry, rec, req.tag, ch);
                        if (painted) g_req.mountVisualMask |= 1u << ch;
                    }
                    // An explicit clear record allows exact mirrors to clear
                    // only this channel without erasing the other channels.
                }

                if (!upsertOk || !RequestStillValid(req))
                {
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }
                g_req.nextVisit = GetTickCount64() + 16;
                g_req.mountNext = static_cast<unsigned>(chFirst + 1);
                if (requested & (0xFFFu << g_req.mountNext)) return;
                const bool visualComplete = (g_req.mountVisualMask & requested) == requested;
                uint16_t mountItemTypeId = 0;
                Read16(entry + kOff_InvSlot_TypeId, &mountItemTypeId);

                uint8_t recs[kDye_MaxChannels][16];
                bool recordsValid = false;
                const uint32_t mask = ReadRecords(entry, recs, &recordsValid);
                const bool profileComplete = ProfileReadbackMatches(req, entry);
                const bool durableOk = upsertOk && recordsValid && profileComplete && RequestStillValid(req) && MirrorToServer(item, recs, mask, true);

                // Cache for auto-restore across summon & reload
                if (req.tag < 32 && upsertOk && recordsValid && profileComplete && RequestStillValid(req))
                {
                    // Save the entire read-back state, including explicit clears,
                    // so a partial clear preserves and restores untouched channels.
                    s_savedMountOrigins[req.tag] = item;
                    s_savedMountSlots[req.tag].active = true;
                    s_savedMountSlots[req.tag].tag = req.tag;
                    s_savedMountSlots[req.tag].typeId = mountItemTypeId;
                    s_savedMountSlots[req.tag].instanceId = instId;
                    s_savedMountSlots[req.tag].mask = mask;
                    memcpy(s_savedMountSlots[req.tag].records, recs, sizeof(recs));
                    Read32(entry + kOff_ItemVal_DyeCount, &s_savedMountSlots[req.tag].dyeCount);
                    UpsertSavedItemDye(mountItemTypeId, mask, recs, s_savedMountSlots[req.tag].dyeCount);
                    SaveDyeCacheToFile();
                }

                LOG("dye: mount tag=%u instance=%lld data=%03X visual=%03X client=%p mirror=%d (native calls; verify appearance)",
                    req.tag, item.instance, g_req.mountDataMask, g_req.mountVisualMask,
                    reinterpret_cast<void*>(render.comp), durableOk ? 1 : 0);
                g_state.store(static_cast<int>(RequestStillValid(req) && profileComplete && (upsertOk || durableOk)
                                              ? (visualComplete ? Dye::OpState::Done : Dye::OpState::DoneDataOnly) : Dye::OpState::Failed),
                              std::memory_order_release);
                return;
            }

            // ===== PLAYER CHARACTER MODE (Kliff, Damiane, Oongka) ============
            const uintptr_t comp = item.sourceComp;
            if (!comp)
            {
                LOG_WARN("dye: apply refused - equip component not resolved.");
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }

            uintptr_t entry = MatchingDyeEntry(comp, item);
            if (!entry)
            {
                LOG_WARN("dye: no live equipped entry for slot tag %u.", req.tag);
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }

            static uint8_t batch[kDyeBatch_Size];
            memset(batch, 0, sizeof(batch));
            for (size_t blk = 0; blk < kDyeBatch_Blocks; ++blk)
            {
                uint8_t* block = batch + blk * kDyeBatch_BlockSize;
                const uint16_t tag = (blk == 0) ? req.tag : 0xFFFF;
                memcpy(block, &tag, 2);
                for (uint32_t r = 0; r < kDye_MaxChannels; ++r)
                    block[kDyeBatch_RecordsOff + r * 16 + 6] = 0xFF;
            }

            const int chFirst = (req.channel < 0) ? 0 : req.channel;
            const int chLast  = (req.channel < 0) ? static_cast<int>(kDye_MaxChannels) - 1 : req.channel;
            const uint32_t requestedMask = RequestedDyeMask(req);
            for (int ch = chFirst; ch <= chLast; ++ch)
            {
                if (!(requestedMask & (1u << ch))) continue;
                uint8_t rec[16]{};
                BuildRequestRecord(req, ch, rec);
                memcpy(batch + kDyeBatch_RecordsOff + static_cast<size_t>(ch) * 16, rec, sizeof(rec));
            }

            const int curCharIdx = item.character;
            if (!RequestStillValid(req))
            {
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }
            // Profiles are data sources. A render target is resolved separately,
            // including the native client registry when these roots are server copies.
            const uintptr_t profComp = ProfileComp(curCharIdx);
            const uintptr_t profEntry = MatchingDyeEntry(profComp, item);
            const DyeRenderTarget render = ResolveDyeRenderTarget(item, comp, profComp);
            const uintptr_t liveComp = render.comp;
            const uintptr_t liveEntry = render.entry;
            int err = 0;
            const bool applyOk = RequestStillValid(req) && liveComp &&
                                 CallDyeApply(liveComp, batch, &err, item, true) && err == 0;

            // Always upsert records directly into TrItemValue for durability
            bool upsertOk = false;
            for (int ch = chFirst; ch <= chLast; ++ch)
            {
                if (!(requestedMask & (1u << ch))) continue;
                if (!RequestStillValid(req)) break;
                uint8_t rec[16] = {};
                BuildRequestRecord(req, ch, rec);
                if (g_dyeUpsert)
                {
                    if (MatchingDyeEntry(comp, item) == entry)
                        upsertOk |= CallEquipDyeUpsert(comp, item, rec);
                    if (liveComp && liveComp != comp && liveEntry && MatchingDyeEntry(liveComp, item) == liveEntry)
                        upsertOk |= CallEquipDyeUpsert(liveComp, item, rec);
                    if (profComp && profComp != comp && profComp != liveComp && profEntry &&
                        MatchingDyeEntry(profComp, item) == profEntry)
                        upsertOk |= CallEquipDyeUpsert(profComp, item, rec);
                }
            }

            // The leaves do not need a possessor: use the resolved client body
            // even when its batch is unavailable. Data components are never painted.
            uint32_t visualMask = 0;
            for (int ch = chFirst; ch <= chLast; ++ch)
            {
                if (!(requestedMask & (1u << ch))) continue;
                if (!RequestStillValid(req)) break;
                uint8_t rec[16] = {};
                BuildRequestRecord(req, ch, rec);

                if (ClearRequestRecord(rec))
                {
                    if (liveComp && MatchingDyeEntry(liveComp, item) == liveEntry)
                        if (CallDyeVisualClear(liveComp, liveEntry, req.tag, ch, true)) visualMask |= 1u << ch;
                    // The data loop stored an explicit clear record on each
                    // exact copy; preserve it for mirroring and partial replay.
                }
                else
                {
                    // ApplySlot is DISABLED on 2.02 (signature false-positive:
                    // 0x142B41E60 is a hash/registry utility, see the wrapper).
                    // Live visuals ride on DyeApplyBatch (possessed player) and
                    // DyeVisualSet (render leaves) instead.
                    if (liveComp && MatchingDyeEntry(liveComp, item) == liveEntry)
                        if (CallDyeVisualSet(liveComp, liveEntry, rec, req.tag, ch, true)) visualMask |= 1u << ch;
                }
            }
            const bool visualOk = visualMask == requestedMask;
            if (curCharIdx >= 0 && curCharIdx < 3 && req.tag < 32 && RequestStillValid(req))
            {
                DyeVisualRetry& retry = s_manualVisualRetry[curCharIdx][req.tag];
                const bool sameSource = retry.item.sourceComp == item.sourceComp &&
                    retry.item.sourceEntry == item.sourceEntry && retry.item.instance == item.instance &&
                    retry.item.type == item.type && retry.item.controlledOwner == item.controlledOwner;
                if (!sameSource) retry.mask = 0;
                retry.item = item;
                retry.mask = (retry.mask & ~requestedMask) | (applyOk ? 0 : requestedMask & ~visualMask);
            }

            const char* charName = Equipment::CharacterName(curCharIdx);
            LOG("dye: apply diag [%s tag=%u] comp=%p entry=%p liveComp=%p liveEntry=%p profComp=%p profEntry=%p -> applyOk=%d upsertOk=%d visualOk=%d",
                charName, req.tag,
                reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(liveComp), reinterpret_cast<void*>(liveEntry),
                reinterpret_cast<void*>(profComp), reinterpret_cast<void*>(profEntry),
                applyOk ? 1 : 0, upsertOk ? 1 : 0, visualOk ? 1 : 0);

            if (!visualOk && !applyOk)
            {
                LOG_WARN("dye: [%s tag=%u] live visual not applied (applyOk=0, visualOk=0). Records saved to data.",
                         charName, req.tag);
            }

            if (!applyOk && !upsertOk && !visualOk)
            {
                LOG_WARN("dye: applier refused (err=%d, slot tag %u).", err, req.tag);
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }

            DyeWatchFile("ProcessRequest: player tag=%u comp=%p err=%d applyOk=%d upsertOk=%d visualOk=%d",
                req.tag, reinterpret_cast<void*>(comp), err, applyOk ? 1 : 0, upsertOk ? 1 : 0,
                visualOk ? 1 : 0);

            entry = MatchingDyeEntry(comp, item);
            int64_t instId = 0;
            if (entry) Read64(entry + kOff_ItemVal_InstanceId, &instId);
            if (entry)
            {
                uint8_t recs[kDye_MaxChannels][16];
                bool recordsValid = false;
                const uint32_t mask = ReadRecords(entry, recs, &recordsValid);
                if (!recordsValid || !RequestStillValid(req) || !ProfileReadbackMatches(req, entry))
                {
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }
                MirrorToServer(item, recs, mask);

                uint16_t itemTypeId = 0;
                Read16(entry + kOff_InvSlot_TypeId, &itemTypeId);

                const int charIdx = item.character;
                if (charIdx >= 0 && charIdx < 3 && req.tag < 32)
                {
                    s_savedPlayerOrigins[charIdx][req.tag] = item;
                    s_savedPlayerSlots[charIdx][req.tag].active = true;
                    s_savedPlayerSlots[charIdx][req.tag].tag = req.tag;
                    s_savedPlayerSlots[charIdx][req.tag].typeId = itemTypeId;
                    s_savedPlayerSlots[charIdx][req.tag].instanceId = instId;
                    s_savedPlayerSlots[charIdx][req.tag].mask = mask;
                    memcpy(s_savedPlayerSlots[charIdx][req.tag].records, recs, sizeof(recs));
                    Read32(entry + kOff_ItemVal_DyeCount, &s_savedPlayerSlots[charIdx][req.tag].dyeCount);
                    UpsertSavedItemDye(itemTypeId, mask, recs, s_savedPlayerSlots[charIdx][req.tag].dyeCount);
                    SaveDyeCacheToFile();
                }

                const char* charName = Equipment::CharacterName(charIdx);
                const char* slotName = Equipment::SlotNameForTag(req.tag);
                if (req.retouchFields)
                {
                    LOG("dye: [%s tag=%u] retouch zones=%03X fields=%u material=0x%04X condition=%u (per-zone colors preserved).",
                        charName, req.tag, requestedMask, req.retouchFields, req.value.materialId, req.value.repair);
                }
                else if (req.applyProfile)
                {
                    LOG("dye: [%s] Slot [Tag %u] -> Applied profile '%s' (id=%u).", charName, req.tag, req.profile.name, req.profile.id);
                }
                else if (req.clear)
                {
                    LOG("dye: [%s] Slot [%s (Tag %u)] -> Cleared dye color.",
                        charName, slotName ? slotName : "Unknown", req.tag);
                }
                else
                {
                    LOG("dye: [%s] Slot [%s (Tag %u)] Channel %d -> Applied RGB=(%u,%u,%u) Material=0x%04X.",
                        charName, slotName ? slotName : "Unknown", req.tag, req.channel,
                        req.value.r, req.value.g, req.value.b, req.value.materialId);
                }
            }

            if (!RequestStillValid(req))
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
            else if (visualOk || applyOk)
                g_state.store(static_cast<int>(Dye::OpState::Done), std::memory_order_release);
            else if (upsertOk)
                g_state.store(static_cast<int>(Dye::OpState::DoneDataOnly), std::memory_order_release);
            else
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
        }

        // Same resolved client target as manual dye; never replay the client
        // acknowledgement on a profile/server data component. The direct
        // leaves also cover unpossessed client bodies. Game-thread only.
        static uint32_t RestoreApplySlot(const DyeItemIdentity& item,
                                     const uint8_t recs[kDye_MaxChannels][16], uint32_t mask,
                                     const DyeRenderTarget& render)
        {
            if (!mask || (mask & ~((1u << kDye_MaxChannels) - 1)) || !SourceItemValid(item) ||
                !render.comp || IsCompFaulted(render.comp) || !IsRenderComp(render.comp) ||
                MatchingDyeEntry(render.comp, item) != render.entry) return 0;
            // Return only the channel completed this visit. The caller retains
            // residual bits; avoid twelve visual/native calls in a single tick.
            mask = FirstDyeChannel(mask);
            DyeRealmGuard clientRealm(0);
            if (!clientRealm.active) return 0;

            static uint8_t batch[kDyeBatch_Size];
            memset(batch, 0, sizeof(batch));
            for (size_t blk = 0; blk < kDyeBatch_Blocks; ++blk)
            {
                uint8_t* block = batch + blk * kDyeBatch_BlockSize;
                const uint16_t btag = (blk == 0) ? item.tag : 0xFFFF;
                memcpy(block, &btag, 2);
                for (uint32_t r = 0; r < kDye_MaxChannels; ++r)
                    block[kDyeBatch_RecordsOff + r * 16 + 6] = 0xFF;
            }
            for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
            {
                if (!(mask & (1u << ch))) continue;
                if (recs[ch][6] != ch) return 0;
                memcpy(batch + kDyeBatch_RecordsOff + ch * 16, recs[ch], 16);
            }

            // Try the possession-independent leaves first. A batch-only fault
            // must not prevent this request from attempting the render path.
            uint32_t visualMask = 0;
            for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
            {
                if (!(mask & (1u << ch))) continue;
                if (!SourceItemValid(item) || MatchingDyeEntry(render.comp, item) != render.entry) break;
                if (!CallEquipDyeUpsert(render.comp, item, recs[ch])) continue;
                const bool clear = recs[ch][4] == 0xFF && recs[ch][5] == 0xFF &&
                                   !recs[ch][7] && !recs[ch][8] && !recs[ch][9] && !recs[ch][10] &&
                                   (recs[ch][11] & 0x80) && !recs[ch][12];
                if (clear ? CallDyeVisualClear(render.comp, render.entry, item.tag, ch)
                          : CallDyeVisualSet(render.comp, render.entry, recs[ch], item.tag, ch))
                    visualMask |= 1u << ch;
            }
            if (visualMask == mask) return mask;
            int errDummy = 0;
            // Restore-driven batch applies suppress the success-tail toast.
            g_suppressDyeToast.fetch_add(1, std::memory_order_acq_rel);
            const bool applied = SourceItemValid(item) && CallDyeApply(render.comp, batch, &errDummy, item) && errDummy == 0;
            g_suppressDyeToast.fetch_sub(1, std::memory_order_acq_rel);
            return applied ? mask : visualMask;
        }
    }

    bool Dye::Install()
    {
        LoadDyeCacheFromFile();
        wchar_t profilePath[MAX_PATH]{};
        const DWORD pathLength = GetModuleFileNameW(Mod::Get().Module(), profilePath, MAX_PATH);
        if (pathLength && pathLength < MAX_PATH)
        {
            wchar_t* slash = wcsrchr(profilePath, L'\\');
            if (slash)
            {
                const std::wstring path(profilePath, slash + 1);
                if (!s_profiles.Load(path + L"Trinity_DyeProfiles.dat"))
                    LOG_WARN("dye: %s", s_profiles.Error().c_str());
            }
        }

        const auto registryAnchors = mem::FindAllMatches(kSig_DyeClientRegistry, 2);
        g_clientRegistryGlobal = registryAnchors.size() == 1
            ? mem::ResolveRipAt(registryAnchors[0], 7) : 0;
        if (g_clientRegistryGlobal)
            LOG("dye: native client registry global @ %p (unique dye-ack anchor).",
                reinterpret_cast<void*>(g_clientRegistryGlobal));
        else
            LOG_WARN("dye: native client registry anchor missing/ambiguous; owner-root discovery only.");

        if (!mem::InstallHook("dye: equip-batch", kSig_EquipBatch, nullptr,
                              &hkEquipBatch, &oEquipBatch, &g_equipTarget))
        {
            mem::InstallHook("dye: equip-batch legacy", kSig_EquipBatch_Legacy, nullptr,
                             &hkEquipBatch, &oEquipBatch, &g_equipTarget);
        }

        // Dye toast suppressor: the auto-restore replays batches silently.
        if (!mem::InstallHook("dye: ui-notify", kSig_DyeNotify, nullptr,
                              &hkDyeNotify, &oDyeNotify, &g_dyeNotifyTarget))
            LOG_WARN("dye: ui-notify hook not installed - restore replays will toast.");

        uintptr_t apply = mem::FindPattern(kSig_DyeApplyBatch);
        if (!apply)
            apply = mem::FindPattern(kSig_DyeApplyBatch_TU200);
        if (!apply)
            apply = mem::FindPattern(kSig_DyeApplyBatch_Legacy);
        if (apply)
            g_dyeApply = reinterpret_cast<DyeApplyBatch_t>(apply);

        uintptr_t upsert = mem::FindPattern(kSig_DyeUpsert);
        if (!upsert)
            upsert = mem::FindPattern(kSig_DyeUpsert_Legacy);
        if (!upsert)
            upsert = mem::FindPattern(kSig_DyeUpsert_TU116);

        if (!upsert)
            LOG_WARN("dye: upsert signature not found - dye will apply but not persist.");
        else
        {
            LOG("dye: batch apply @ %p, durable upsert @ %p.",
                reinterpret_cast<void*>(apply), reinterpret_cast<void*>(upsert));
        }
        g_dyeUpsert = reinterpret_cast<DyeUpsert_t>(upsert);

        // Universal per-slot render leaves - the live-visual path that
        // works on companion bodies (Damiane / Oongka)
        uintptr_t visualSet = mem::FindPattern(kSig_DyeVisualSet);
        if (!visualSet)
            visualSet = mem::FindPattern(kSig_DyeVisualSet_Legacy);
        g_dyeVisualSet = reinterpret_cast<DyeVisualSet_t>(visualSet);

        uintptr_t visualClear = mem::FindPattern(kSig_DyeVisualClear);
        if (!visualClear)
            visualClear = mem::FindPattern(kSig_DyeVisualClear_Legacy);
        g_dyeVisualClear = reinterpret_cast<DyeVisualClear_t>(visualClear);

        uintptr_t recRemove = mem::FindPattern(kSig_DyeRecordRemove);
        if (!recRemove)
            recRemove = mem::FindPattern(kSig_DyeRecordRemove_Legacy);
        g_dyeRecRemove = reinterpret_cast<DyeRecRemove_t>(recRemove);

        g_dyeApplySlot = nullptr; // TU 2.02 signature matches an unrelated registry routine.
        LOG("dye: DyeVisualSet @ %p, DyeVisualClear @ %p (validated client targets); DyeApplySlot disabled.",
            reinterpret_cast<void*>(g_dyeVisualSet), reinterpret_cast<void*>(g_dyeVisualClear));

        return true;
    }

    void Dye::Remove()
    {
        if (s_profileSave.valid()) s_profileSave.wait();
        mem::RemoveHook(&g_equipTarget);
        mem::RemoveHook(&g_dyeNotifyTarget);
        oEquipBatch = nullptr;
        g_dyeApply  = nullptr;
        g_dyeUpsert = nullptr;
        g_dyeVisualSet = nullptr;
        g_dyeVisualClear = nullptr;
        g_dyeRecRemove = nullptr;
        g_dyeApplySlot = nullptr;
        g_clientRegistryGlobal = 0;
        oDyeNotify = nullptr;
        g_comp.store(0, std::memory_order_release);
        g_mountComp.store(0, std::memory_order_release);
        s_selectionGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_worldGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_uiEpoch.fetch_add(1, std::memory_order_release);
        for (auto& renderer : s_renderers) renderer = {};
        for (auto& renderer : s_mountRenderers) renderer = {};
        g_state.store(static_cast<int>(Dye::OpState::Idle), std::memory_order_release);
        for (auto& character : s_savedPlayerOrigins)
            for (auto& origin : character) origin = {};
        for (auto& origin : s_savedMountOrigins) origin = {};
        for (auto& character : s_manualVisualRetry)
            for (auto& retry : character) retry = {};
    }

    bool Dye::Ready()
    {
        EnsureUiSnapshot();
        return UiSnapshotUsable();
    }

    uintptr_t Dye::ClientRegistryGlobal() { return g_clientRegistryGlobal; }

    void Dye::SetActiveCharacter(int index)
    {
        if (index < 0) index = 0;
        if (index > 2) index = 2;
        // Selection must not be silently lost while a native dye operation is
        // running. Requests carry explicit targets and abort on generation change.
        if (s_activeCharIdx.exchange(index) != index)
            s_selectionGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_uiEpoch.fetch_add(1, std::memory_order_release);
    }

    int Dye::GetActiveCharacter()
    {
        return s_activeCharIdx;
    }

    void Dye::SetTargetMode(int mode)
    {
        if (mode < 0 || mode > 2) return;
        if (s_targetMode.exchange(mode) != mode)
            s_selectionGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_uiEpoch.fetch_add(1, std::memory_order_release);
    }

    int Dye::GetTargetMode()
    {
        return s_targetMode;
    }

    void Dye::SetActiveMount(int index)
    {
        if (index < 0) index = 0;
        if (index > 3) index = 3;
        if (s_activeMountIdx.exchange(index) != index)
            s_selectionGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_uiEpoch.fetch_add(1, std::memory_order_release);
    }

    int Dye::GetActiveMount()
    {
        return s_activeMountIdx;
    }

    uintptr_t Dye::ActiveClientComp()
    {
        return ClientComp();
    }

    uintptr_t Dye::HookedClientComp()
    {
        const uintptr_t hooked = g_comp.load(std::memory_order_acquire);
        if (CompValid(hooked)) return hooked;
        return 0;
    }

    uintptr_t Dye::HookedMountComp()
    {
        const uintptr_t hooked = g_mountComp.load(std::memory_order_acquire);
        if (CompValid(hooked)) return hooked;
        return 0;
    }

    uintptr_t Dye::HookedCharComp(int charIdx)
    {
        (void)charIdx;
        return 0;
    }

    uintptr_t Dye::FindClientEquipmentReplica(uintptr_t source, int character, uint16_t tag,
                                              uint16_t type, int64_t instance)
    {
        DyeOperationGuard operation;
        if (!operation.acquired || instance <= 0) return 0;
        DyeItemIdentity item{};
        if (!ReadDyeItem(source, tag, character, item) || item.type != type || item.instance != instance)
            return 0;
        // Native dye acknowledgements resolve the client's owner in this registry
        // before updating +68/+38. Inventory data does not require a spawned mesh.
        return ResolveDyeRenderTarget(item, source, 0, false).comp;
    }

    void* Dye::GetEquipBatch()
    {
        return reinterpret_cast<void*>(oEquipBatch);
    }

    void Dye::TriggerEquipMeshRebuild(uintptr_t comp)
    {
        TriggerEquipRefresh(comp);
    }

    int Dye::SlotCount()
    {
        EnsureUiSnapshot();
        return UiSnapshotUsable() ? g_slotCount : 0;
    }

    bool Dye::GetSlot(int idx, SlotInfo* out)
    {
        if (!UiSnapshotUsable() || !out || idx < 0 || idx >= g_slotCount) return false;
        *out = g_slots[idx];
        return true;
    }

    bool Dye::GetChannel(uint16_t tag, int channel, Channel* out)
    {
        if (!out) return false;
        if (channel < 0 || channel >= static_cast<int>(kDye_MaxChannels)) return false;
        if (!UiSnapshotUsable()) return false;
        const DyeSnapshotRow* row = nullptr;
        for (int i = 0; i < g_slotCount; ++i)
            if (g_slots[i].tag == tag) { row = &s_uiRows[i]; break; }
        if (!row || !(row->mask & (1u << channel))) return false;
        const uint8_t* r = row->records[channel];
        memcpy(&out->groupKey, r + 0, 4);
        memcpy(&out->materialId, r + 4, 2);
        out->r = r[7]; out->g = r[8]; out->b = r[9];
        out->repair = r[11];
        return true;
    }

    bool Dye::Apply(uint16_t tag, int channel, const Channel& c)
    {
        return EnqueueDye(tag, channel, false, c);
    }

    bool Dye::Retouch(uint16_t tag, int channel, bool materialChanged, uint16_t material,
                      bool conditionChanged, uint8_t condition)
    {
        const unsigned fields = (materialChanged ? DyeRetouchMaterial : 0u) |
                                (conditionChanged ? DyeRetouchCondition : 0u);
        if (!fields || (materialChanged && material != 0xFFFF && (material < 1 || material > 10)) ||
            (conditionChanged && condition > 127)) return false;
        Channel value{};
        value.materialId = material;
        value.repair = condition;
        return EnqueueDye(tag, channel, false, value, nullptr, false, 0, fields);
    }

    bool Dye::Clear(uint16_t tag, int channel)
    {
        return EnqueueDye(tag, channel, true, Channel{});
    }

    Dye::OpState Dye::Status()
    {
        int cur = g_state.load(std::memory_order_acquire);
        if (cur != static_cast<int>(OpState::Idle) && cur != static_cast<int>(OpState::Pending))
            g_state.compare_exchange_strong(cur, static_cast<int>(OpState::Idle), std::memory_order_acq_rel);
        return static_cast<OpState>(cur);
    }

    bool Dye::Busy() { return g_state.load(std::memory_order_acquire) == static_cast<int>(OpState::Pending); }
    bool Dye::HasResult()
    {
        const auto state = static_cast<OpState>(g_state.load(std::memory_order_acquire));
        return state != OpState::Idle && state != OpState::Pending;
    }
    std::vector<DyeProfile> Dye::Profiles() { return s_profiles.List(); }
    std::string Dye::ProfileError() { return s_profiles.Error(); }
    bool Dye::RenameProfile(uint32_t id, const char* name) { return !Busy() && s_profiles.Rename(id, name); }
    bool Dye::DeleteProfile(uint32_t id) { return !Busy() && s_profiles.Erase(id); }
    bool Dye::SaveProfile(uint16_t tag, const char* name, uint32_t replaceId)
    {
        if (!name || !*name || strnlen_s(name, 64) >= 64) return false;
        DyeProfile profile{};
        strcpy_s(profile.name, name);
        return EnqueueDye(tag, -1, false, Channel{}, &profile, true, replaceId);
    }
    bool Dye::ApplyProfile(uint16_t tag, uint32_t id)
    {
        DyeProfile profile{};
        if (!s_profiles.Get(id, profile)) return false;
        return EnqueueDye(tag, -1, false, Channel{}, &profile);
    }

    void Dye::Tick()
    {
        DyeOperationGuard operation;
        if (!operation.acquired) return; // native calls may re-enter the movement pump
        static uintptr_t lastWorld = 0, lastRoot = 0;
        const uintptr_t world = Player::GetControlledOwner();
        uintptr_t root = 0;
        if (g_clientRegistryGlobal) ReadPtr(g_clientRegistryGlobal, &root);
        const bool ready = Player::Ready();
        if (world != lastWorld || root != lastRoot)
        {
            lastWorld = world;
            lastRoot = root;
            s_worldGeneration.fetch_add(1, std::memory_order_acq_rel);
            s_uiEpoch.fetch_add(1, std::memory_order_release);
        }
        if (s_profileSave.valid())
        {
            // Observe world transitions even while committing an owned profile
            // copy. File completion itself no longer depends on the live item.
            if (s_profileSave.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            bool saved = false;
            try { saved = s_profileSave.get(); } catch (...) {}
            g_state.store(static_cast<int>(saved ? OpState::ProfileSaved : OpState::ProfileSaveFailed), std::memory_order_release);
            return;
        }
        if (!ready)
        {
            if (g_state.load(std::memory_order_acquire) == static_cast<int>(OpState::Pending))
                g_state.store(static_cast<int>(OpState::Failed), std::memory_order_release);
            return;
        }

        if (g_state.load(std::memory_order_acquire) == static_cast<int>(OpState::Pending))
        {
            if (GetTickCount64() < g_req.nextVisit) return;
            ProcessRequest();
            s_uiEpoch.fetch_add(1, std::memory_order_release);
            return; // explicit requests have this tick's native-work budget
        }

        // One eligible saved slot total (player OR mount) per 100ms. The cursor
        // examines only local flags/due times before any component discovery.
        // A full set takes several ticks; empty profiles perform no game scans.
        constexpr int kRestoreSlots = (3 + 4) * 32;
        constexpr ULONGLONG kRestoreTickMs = 100;
        constexpr ULONGLONG kRestoreSlotMs = 2500;
        static int cursor = 0;
        static ULONGLONG nextTick = 0, nextSlot[kRestoreSlots]{};
        const ULONGLONG now = GetTickCount64();
        if (now < nextTick) return;
        nextTick = now + kRestoreTickMs;
        int selected = -1;
        for (int n = 0; n < kRestoreSlots; ++n)
        {
            const int candidate = cursor;
            cursor = (cursor + 1) % kRestoreSlots;
            const int tag = candidate % 32;
            const bool active = candidate < 96
                ? s_savedPlayerSlots[candidate / 32][tag].active && s_savedPlayerSlots[candidate / 32][tag].mask
                : s_savedMountSlots[tag].active && s_savedMountSlots[tag].mask;
            if (!active || now < nextSlot[candidate]) continue;
            selected = candidate;
            nextSlot[candidate] = now + kRestoreSlotMs;
            break;
        }
        if (selected < 0) return;
        const int selectedCharacter = selected < 96 ? selected / 32 : -1;
        const int selectedMount = selected >= 96 ? (selected - 96) / 32 : -1;
        const uint16_t selectedTag = static_cast<uint16_t>(selected % 32);
        if (selectedCharacter >= 0)
        {
            const int c = selectedCharacter;
            const uint16_t tag = selectedTag;
            const SavedPlayerSlot& saved = s_savedPlayerSlots[c][tag];
            const int liveIdx = Player::GetActiveCharacterIdx();
            uintptr_t comp = 0;
            if (c == liveIdx) comp = CompForCharacter(Inventory::ClientCharacterAddr(), c, liveIdx);
            const uintptr_t profC = ProfileComp(c);
            if (!comp) comp = profC;
            if (!comp) comp = FindCharacterFallback(c);
            DyeItemIdentity item{};
            if (!ReadDyeItem(comp, tag, c, item) ||
                !SavedItemMatches(item, saved.typeId, saved.instanceId, s_savedPlayerOrigins[c][tag])) return;
            const uintptr_t entry = MatchingDyeEntry(comp, item);
            if (!entry) return;
            const DyeRenderTarget render = ResolveDyeRenderTarget(item, comp, profC);
            const uintptr_t profEntry = MatchingDyeEntry(profC, item);

            // Positive replay bookkeeping is independent of renderer discovery.
            // No stored target here authorizes a native call on a later tick.
            struct ReplayState
            {
                DyeItemIdentity source{};
                DyeRenderTarget painted{}, target{};
                uint32_t pending = 0, savedMask = 0;
                uint8_t records[kDye_MaxChannels][16]{};
                ULONGLONG equipChange = 0, nextMirror = 0, nextHolder = 0, lastApply = 0;
                DyeHolderCursor holder{};
                bool mirrorPending = false, holderPending = false;
            };
            constexpr ULONGLONG kMirrorRetryMs = 5000;
            constexpr ULONGLONG kMirrorRefreshMs = 60000;
            constexpr ULONGLONG kHolderMissingRetryMs = 15000;
            static ReplayState states[3][32];
            ReplayState& state = states[c][tag];
            const DyeItemIdentity& previous = state.source;
            const bool changedSource = previous.controlledOwner != item.controlledOwner ||
                previous.worldRoot != item.worldRoot || previous.worldGeneration != item.worldGeneration ||
                previous.sourceComp != item.sourceComp || previous.sourceOwner != item.sourceOwner ||
                previous.sourceEntry != item.sourceEntry || previous.type != item.type || previous.instance != item.instance;
            if (changedSource) state = {};
            state.source = item;
            const bool changedRecords = changedSource || state.savedMask != saved.mask ||
                [&]() {
                    for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
                        if ((saved.mask & (1u << ch)) && !SameDyePayload(state.records[ch], saved.records[ch])) return true;
                    return false;
                }();
            const bool changedTarget = render.comp != state.target.comp || render.entry != state.target.entry ||
                render.context != state.target.context;
            if (changedTarget || changedRecords)
            {
                state.target = render;
                state.pending = saved.mask;
                state.savedMask = saved.mask;
                memcpy(state.records, saved.records, sizeof(state.records));
            }
            if (changedRecords)
            {
                // A new source/record set needs its own durable acknowledgement,
                // even when the client already contains these exact records.
                state.mirrorPending = true;
                state.nextMirror = 0;
                state.holder = {};
                state.holderPending = item.instance > 0;
                state.nextHolder = 0;
            }
            if (!render.comp) state.painted = {};
            const bool newRender = render.comp && (render.comp != state.painted.comp ||
                render.entry != state.painted.entry || render.context != state.painted.context);
            DyeVisualRetry& manualRetry = s_manualVisualRetry[c][tag];
            const bool manualPending = manualRetry.mask && SourceItemValid(manualRetry.item) &&
                MatchingDyeEntry(comp, manualRetry.item) == entry;
            if (manualPending) state.pending |= manualRetry.mask & saved.mask;
            manualRetry.mask = 0; // residual is now owned by state.pending

            uint8_t liveRecs[kDye_MaxChannels][16];
            bool liveValid = false;
            const uint32_t liveMask = ReadRecords(entry, liveRecs, &liveValid);
            if (!liveValid) return;
            uint32_t dataPending = 0;
            for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
            {
                if ((saved.mask & (1u << ch)) && (!(liveMask & (1u << ch)) ||
                    !SameDyePayload(liveRecs[ch], saved.records[ch])))
                {
                    dataPending |= 1u << ch;
                    state.pending |= 1u << ch;
                }
            }
            if (render.comp)
            {
                uint8_t renderRecs[kDye_MaxChannels][16];
                bool renderValid = false;
                const uint32_t renderMask = ReadRecords(render.entry, renderRecs, &renderValid);
                if (!renderValid) return;
                for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
                    if ((saved.mask & (1u << ch)) && (!(renderMask & (1u << ch)) ||
                        !SameDyePayload(renderRecs[ch], saved.records[ch]))) state.pending |= 1u << ch;
            }
            const ULONGLONG equipChange = s_lastEquipChangeMs.load(std::memory_order_acquire);
            const bool afterEquipChange = equipChange && state.equipChange != equipChange;
            if (afterEquipChange)
            {
                state.pending = saved.mask;
                state.equipChange = equipChange; // queue once, preserve partial success
            }
            if (dataPending)
            {
                // Queue before repairing the client. A failed server write must
                // survive the next visit finding needsData == false.
                state.mirrorPending = true;
                if (!state.holderPending && item.instance > 0)
                {
                    state.holder = {};
                    state.holderPending = true;
                }
            }
            if (dataPending && g_dyeUpsert)
            {
                const uint32_t channelMask = FirstDyeChannel(dataPending);
                bool progressed = false;
                for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
                {
                    if (!(channelMask & (1u << ch))) continue;
                    if (MatchingDyeEntry(comp, item) == entry) progressed = CallEquipDyeUpsert(comp, item, saved.records[ch]);
                    if (profC && profC != comp && profEntry && MatchingDyeEntry(profC, item) == profEntry)
                        CallEquipDyeUpsert(profC, item, saved.records[ch]);
                }
                nextSlot[selected] = now + (progressed ? kRestoreTickMs : kRestoreSlotMs);
                return; // data, mirror and visual stages use separate ticks
            }
            // Independent authoritative work: failures retry after 5s, while a
            // known-success copy keeps the existing 60s reconcile cooldown.
            if (state.mirrorPending && now >= state.nextMirror)
            {
                state.nextMirror = now + kMirrorRetryMs;
                if (MirrorToServer(item, saved.records, saved.mask, true))
                {
                    state.mirrorPending = false;
                    state.nextMirror = now + kMirrorRefreshMs;
                }
                nextSlot[selected] = now + kRestoreTickMs;
                return;
            }
            if (state.holderPending && now >= state.nextHolder)
            {
                const DyeHolderMirrorResult result = MirrorHolderSlice(item, saved.records, saved.mask, state.holder);
                switch (result)
                {
                case DyeHolderMirrorResult::Done:
                    state.holderPending = false;
                    state.nextHolder = now + kMirrorRefreshMs;
                    break;
                case DyeHolderMirrorResult::Missing:
                    state.nextHolder = now + kHolderMissingRetryMs;
                    break;
                case DyeHolderMirrorResult::Unavailable:
                    state.nextHolder = now + kMirrorRetryMs;
                    break;
                case DyeHolderMirrorResult::Pending:
                    break; // next normal slot visit resumes the finite slice
                }
                // Give visuals a visit between holder slices, including when
                // the holder remains unavailable for its longer retry period.
                if (result == DyeHolderMirrorResult::Pending) state.nextHolder = now + 500;
                nextSlot[selected] = now + kRestoreTickMs;
                return;
            }

            // Missing targets consume no paint attempt. Residual channels retry
            // on subsequent slot visits; native-fault cooldown still applies.
            if (render.comp && (newRender || state.pending) &&
                (changedTarget || changedRecords || manualPending || afterEquipChange || now - state.lastApply >= kRestoreTickMs))
            {
                state.lastApply = now;
                if (!state.pending) state.pending = saved.mask;
                const uint32_t painted = RestoreApplySlot(item, saved.records, state.pending, render);
                state.pending &= ~painted;
                if (!state.pending) state.painted = render;
                if (state.pending && painted) nextSlot[selected] = now + kRestoreTickMs;
            }
        }

        // Tracked mounts share the same single-slot budget.
        if (selectedMount >= 0)
        {
            const int m = selectedMount;
            const uint16_t tag = selectedTag;
            const SavedMountSlot& saved = s_savedMountSlots[tag];
            const uintptr_t comp = FindMountComp(m);
            DyeItemIdentity item{};
            if (!ReadDyeItem(comp, tag, -1, item, 1, m) ||
                !SavedItemMatches(item, saved.typeId, saved.instanceId, s_savedMountOrigins[tag])) return;
            uint8_t liveRecs[kDye_MaxChannels][16];
            bool valid = false;
            const uint32_t liveMask = ReadRecords(item.sourceEntry, liveRecs, &valid);
            if (!valid) return;
            bool dataChanged = false;
            for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
                if ((saved.mask & (1u << ch)) && (!(liveMask & (1u << ch)) ||
                    !SameDyePayload(liveRecs[ch], saved.records[ch])))
                {
                    if (CallEquipDyeUpsert(comp, item, saved.records[ch]))
                    { nextSlot[selected] = now + kRestoreTickMs; dataChanged = true; }
                    break;
                }
            if (dataChanged) return;
            struct MountReplay
            {
                DyeItemIdentity source{};
                uintptr_t comp = 0, context = 0;
                uint32_t pending = 0, mask = 0;
                uint8_t records[kDye_MaxChannels][16]{};
                ULONGLONG equipChange = 0;
            };
            static MountReplay replay[4][32];
            auto& state = replay[m][tag];
            const auto render = ResolveDyeRenderTarget(item, comp);
            if (!render.comp) return;
            const auto equipChange = s_lastEquipChangeMs.load(std::memory_order_acquire);
            bool changed = state.source.sourceComp != item.sourceComp || state.source.sourceEntry != item.sourceEntry ||
                state.source.instance != item.instance || state.source.type != item.type ||
                state.source.worldGeneration != item.worldGeneration || state.comp != render.comp ||
                state.context != render.context || state.mask != saved.mask || state.equipChange != equipChange;
            for (uint32_t ch = 0; ch < kDye_MaxChannels && !changed; ++ch)
                if ((saved.mask & (1u << ch)) && !SameDyePayload(state.records[ch], saved.records[ch])) changed = true;
            if (changed)
            {
                state.source = item; state.comp = render.comp; state.context = render.context;
                state.mask = state.pending = saved.mask; state.equipChange = equipChange;
                memcpy(state.records, saved.records, sizeof(state.records));
            }
            const uint32_t channelMask = FirstDyeChannel(state.pending);
            DyeRealmGuard clientRealm(0);
            if (!clientRealm.active) return;
            for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
            {
                if (!(channelMask & (1u << ch))) continue;
                if (!SourceItemValid(item) || MatchingDyeEntry(render.comp, item) != render.entry ||
                    !CallEquipDyeUpsert(render.comp, item, saved.records[ch]) || !SourceItemValid(item)) return;
                const bool painted = ClearRequestRecord(saved.records[ch])
                    ? CallDyeVisualClear(render.comp, render.entry, tag, ch)
                    : CallDyeVisualSet(render.comp, render.entry, saved.records[ch], tag, ch);
                if (painted) { state.pending &= ~channelMask; nextSlot[selected] = now + kRestoreTickMs; }
                break;
            }
        }
    }

    bool Dye::InjectAllToSave()
    {
        DyeOperationGuard operation;
        if (!operation.acquired || !Player::Ready()) return false;

        bool ok = false;

        for (int c = 0; c < 3; ++c)
        {
            uintptr_t clientComp = 0;

            const uintptr_t clientAct = Inventory::CharacterAddr(c);
            if (clientAct) clientComp = CompForCharacter(clientAct, c, c);
            if (!clientComp && c == 0 && c == Inventory::ActivePlayerCharacterIdx())
                clientComp = CompForCharacter(Inventory::ClientCharacterAddr(), c, c);

            if (!clientComp) clientComp = ProfileComp(c);

            if (clientComp)
            {
                uintptr_t array = 0;
                uint32_t  count = 0;
                uintptr_t stride = 0xD0, tagOffset = 0xC8, dyeDataOffset = 0x78, dyeCountOffset = 0x80;
                if (ReadEquipTable(clientComp, array, count, &stride, &tagOffset, &dyeDataOffset, &dyeCountOffset))
                {
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        const uintptr_t cEntry = array + static_cast<uintptr_t>(i) * stride;
                        uint16_t tid = 0, tag = 0;
                        if (!Read16(cEntry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                        Read16(cEntry + tagOffset, &tag);
                        DyeItemIdentity item{};
                        if (!ReadDyeItem(clientComp, tag, c, item) || item.sourceEntry != cEntry) continue;

                        uint8_t recs[kDye_MaxChannels][16];
                        bool valid = false;
                        const uint32_t mask = ReadRecords(cEntry, recs, &valid);
                        if (valid && mask > 0)
                        {
                            ok |= MirrorToServer(item, recs, mask);
                        }
                    }
                }
            }

            for (uint16_t tag = 0; tag < 32; ++tag)
            {
                if (s_savedPlayerSlots[c][tag].active && s_savedPlayerSlots[c][tag].mask > 0)
                {
                    DyeItemIdentity item{};
                    if (ReadDyeItem(clientComp, tag, c, item) &&
                        SavedItemMatches(item, s_savedPlayerSlots[c][tag].typeId, s_savedPlayerSlots[c][tag].instanceId,
                                         s_savedPlayerOrigins[c][tag]))
                        ok |= MirrorToServer(item, s_savedPlayerSlots[c][tag].records, s_savedPlayerSlots[c][tag].mask);
                }
            }
        }

        const int mountCount = Player::GetTrackedMountCount();
        for (int m = 0; m < mountCount; ++m)
        {
            const uintptr_t mComp = FindMountComp(m);
            if (!mComp) continue;

            uintptr_t array = 0;
            uint32_t  count = 0;
            uintptr_t stride = 0xD0, tagOffset = 0xC8, dyeDataOffset = 0x78, dyeCountOffset = 0x80;
            if (ReadEquipTable(mComp, array, count, &stride, &tagOffset, &dyeDataOffset, &dyeCountOffset))
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    const uintptr_t mEntry = array + static_cast<uintptr_t>(i) * stride;
                    uint16_t tid = 0, tag = 0;
                    if (!Read16(mEntry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                    Read16(mEntry + tagOffset, &tag);
                    DyeItemIdentity item{};
                    if (!ReadDyeItem(mComp, tag, -1, item, 1, m) || item.sourceEntry != mEntry) continue;

                    uint8_t recs[kDye_MaxChannels][16];
                    bool valid = false;
                    const uint32_t mask = ReadRecords(mEntry, recs, &valid);
                    if (valid && mask > 0)
                    {
                        ok |= MirrorToServer(item, recs, mask);
                    }
                }
            }
        }

        return ok;
    }

    bool Dye::ApplyAllEquipped(const Channel& c)
    {
        const uintptr_t comp = ClientComp();
        if (!comp) return false;
        uintptr_t array = 0;
        uint32_t count = 0;
        uintptr_t stride = 0xD0;
        uintptr_t tagOffset = 0xC8;
        if (!ReadEquipTable(comp, array, count, &stride, &tagOffset)) return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
            uint16_t tid = 0, tag = 0;
            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
            Read16(entry + tagOffset, &tag);
            if (s_targetMode == 0 && (tag == 14 || (tag >= 22 && tag <= 25))) continue;
            Apply(tag, -1, c);
        }
        return true;
    }
}
