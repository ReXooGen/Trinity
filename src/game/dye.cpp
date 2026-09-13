#include "dye.h"

#include <Windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "offsets.h"
#include "player.h"
#include "dye_data.h"
#include "dye_slots_table.h"
#include "inventory.h"
#include "equipment.h"
#include "equipment_logic.h"
#include "../core/logger.h"
#include "../mem/hooks.h"
#include "../mem/safe_memory.h"
#include "../core/version_detect.h"

// The dyehouse from the menu. All the RE background lives in offsets.h
// (the "Armor dye" section); this file is the plumbing:
//
//   Component walk   -> each realm's equip component, straight off that
//                       realm's player character (*(*(actor+0x68)+0x38)).
//                       The client's renders; the server's is the durable one.
//   EquipBatch hook  -> a fallback capture of the same component (rcx) on
//                       every equip change, for when the walk cannot resolve.
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

        // Per-slot applier. TU 2.02 ABI SWAP (disasm 0x142B41E60, arg3 is a
        // SIGNED int [test/jle], arg4 the record pointer - opposite of 2.01):
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

        // Tick of the last NEW player-component equip-batch capture. Gear
        // changes rebuild the GPU material instances back to natural colors
        // while dye records persist as data, so the auto-restore pass forces
        // a bounded visual replay right after each change.
        std::atomic<ULONGLONG> s_lastEquipChangeMs{ 0 };

        inline bool IsValidCanonicalPtr(uintptr_t p)
        {
            return p >= 0x100000000ULL && p <= 0x7FFFFFFFFFFFULL;
        }

        bool ReadEquipTable(uintptr_t comp, uintptr_t& outArray, uint32_t& outCount,
                            uintptr_t* outStride = nullptr, uintptr_t* outSlotTag = nullptr,
                            uintptr_t* outDyeData = nullptr, uintptr_t* outDyeCount = nullptr)
        {
            if (!IsValidCanonicalPtr(comp)) return false;

            struct LayoutDef {
                uintptr_t stride;
                uintptr_t tagOffset;
                uintptr_t dyeDataOffset;
                uintptr_t dyeCountOffset;
            };
            const LayoutDef layouts[] = {
                { 0xD0, 0xC8, 0x78, 0x80 }, // Primary: TU 2.01 / TU 2.02 / legacy (208-byte stride)
                { 0xC8, 0xC0, 0x78, 0x80 }, // Secondary: 200-byte stride
            };

            auto evaluateSlots = [](uintptr_t arr, uint32_t cnt, uintptr_t stride, uintptr_t tagOff, int* outScore) -> bool {
                if (!IsValidCanonicalPtr(arr) || cnt == 0 || cnt > 64) return false;
                int items = 0;
                uint32_t tagMask = 0;
                for (uint32_t i = 0; i < cnt; ++i)
                {
                    const uintptr_t entry = arr + static_cast<uintptr_t>(i) * stride;
                    uint16_t tid = 0, tag = 0;
                    if (Read16(entry + kOff_InvSlot_TypeId, &tid) && tid != kInvSlot_EmptyType && tid != 0)
                    {
                        if (!Read16(entry + tagOff, &tag) || tag >= 32)
                            return false; // ANY slot tag >= 32 means this is NOT an equipment table!
                        items++;
                        tagMask |= (1u << tag);
                    }
                }
                int distinct = 0;
                for (uint32_t m = tagMask; m != 0; m &= (m - 1)) ++distinct;
                if (outScore) *outScore = distinct * 100 + items;
                return items > 0;
            };

            struct BestTable {
                uintptr_t array = 0;
                uint32_t count = 0;
                uintptr_t stride = 0xD0;
                uintptr_t tagOffset = 0xC8;
                uintptr_t dyeDataOffset = 0x78;
                uintptr_t dyeCountOffset = 0x80;
                int score = 0;
            };

            BestTable best;

            // 0x90 is authoritative table descriptor offset on ServerEquipSlotActorComponent
            const uintptr_t tableOffsets[] = { 0x90, 0x88, 0x80, 0x50, 0x78, 0x38, 0x40, 0x48, 0x60, 0x70 };
            for (uintptr_t tOff : tableOffsets)
            {
                uintptr_t desc = 0;
                if (!ReadPtr(comp + tOff, &desc) || !IsValidCanonicalPtr(desc)) continue;
                uintptr_t array = 0;
                uint32_t count = 0;
                if (!ReadPtr(desc + kOff_EquipTable_Array, &array) || !IsValidCanonicalPtr(array)) continue;
                if (!Read32(desc + kOff_EquipTable_Count, &count) || count == 0 || count > 64) continue;

                for (const auto& l : layouts)
                {
                    int score = 0;
                    if (evaluateSlots(array, count, l.stride, l.tagOffset, &score))
                    {
                        if (score > best.score)
                        {
                            best.array = array;
                            best.count = count;
                            best.stride = l.stride;
                            best.tagOffset = l.tagOffset;
                            best.dyeDataOffset = l.dyeDataOffset;
                            best.dyeCountOffset = l.dyeCountOffset;
                            best.score = score;
                        }
                    }
                }
            }

            if (best.score <= 0) return false;

            outArray = best.array;
            outCount = best.count;
            if (outStride) *outStride = best.stride;
            if (outSlotTag) *outSlotTag = best.tagOffset;
            if (outDyeData) *outDyeData = best.dyeDataOffset;
            if (outDyeCount) *outDyeCount = best.dyeCountOffset;
            return true;
        }

        bool CompValid(uintptr_t comp)
        {
            if (!IsValidCanonicalPtr(comp)) return false;
            uintptr_t array = 0;
            uint32_t  count = 0;
            return ReadEquipTable(comp, array, count);
        }

        bool IsRenderComp(uintptr_t comp)
        {
            if (!IsValidCanonicalPtr(comp)) return false;
            // TU 2.02 DyeVisualSet (0x1409170C0) / DyeVisualClear (0x140918770)
            // walk: actor=[comp+8]; sub=[actor+0x68]; a helper call through
            // [sub+0x20]+0x30 and a read of [[sub+0x40]+0x130]. Validating only
            // the four pointers left those deeper derefs faulting 0xC0000005
            // (90k+ caught exceptions per session on stale bodies), so probe
            // the exact qwords the leaves touch with guarded reads.
            uintptr_t act = 0, sub = 0, rcx20 = 0, rcx40 = 0, probe = 0;
            if (!ReadPtr(comp + 8, &act) || !IsValidCanonicalPtr(act)) return false;
            if (!ReadPtr(act + 0x68, &sub) || !IsValidCanonicalPtr(sub)) return false;
            if (!ReadPtr(sub + 0x20, &rcx20) || !IsValidCanonicalPtr(rcx20)) return false;
            if (!ReadPtr(rcx20 + 0x30, &probe)) return false;
            if (!ReadPtr(sub + 0x40, &rcx40) || !IsValidCanonicalPtr(rcx40)) return false;
            if (!ReadPtr(rcx40 + 0x130, &probe)) return false;
            return true;
        }

        bool CompHasHorseGear(uintptr_t comp)
        {
            uintptr_t array = 0;
            uint32_t  count = 0;
            uintptr_t stride = 0xD0;
            uintptr_t tagOffset = 0xC8;
            if (!ReadEquipTable(comp, array, count, &stride, &tagOffset)) return false;

            for (uint32_t i = 0; i < count; ++i)
            {
                const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
                uint16_t tid = 0;
                int64_t qty = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                if (!Read64(entry + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;

                char itemName[96] = "";
                char icon[128] = "";
                Inventory::NameForTypeId(tid, itemName, sizeof(itemName));
                Inventory::IconForTypeId(tid, icon, sizeof(icon));
                if (GetHorseSlotType(itemName, icon) != HorseSlotType::None)
                    return true;

                uint16_t tag = 0;
                Read16(entry + tagOffset, &tag);
                if ((tag <= 4 || tag == 14 || (tag >= 22 && tag <= 25)) && itemName[0] &&
                    GetHorseSlotType(itemName, icon) != HorseSlotType::None)
                    return true;
            }
            return false;
        }

        uintptr_t FindEquipCompFromActor(uintptr_t actor)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            // 1. Direct component at actor + 0x38 (actor is SubContainer)
            uintptr_t comp = 0;
            if (ReadPtr(actor + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                return comp;

            // 2. Standard character / mount container walk (*(*(actor+0x68)+0x38))
            uintptr_t sub = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && IsValidCanonicalPtr(sub))
            {
                if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                    return comp;
            }

            // 3. Alternate sub-container offsets on actor
            const uintptr_t subOffsets[] = { 0x60, 0x68, 0x70, 0x58, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0 };
            const uintptr_t compOffsets[] = { 0x38, 0x30, 0x40, 0x28, 0x48, 0x50, 0x58, 0x60, 0x68, 0x80, 0x88, 0x90, 0x168 };
            for (uintptr_t sOff : subOffsets)
            {
                if (ReadPtr(actor + sOff, &sub) && IsValidCanonicalPtr(sub))
                {
                    for (uintptr_t cOff : compOffsets)
                    {
                        if (ReadPtr(sub + cOff, &comp) && CompValid(comp))
                            return comp;
                    }
                }
            }

            // 4. Direct component pointer on actor
            const uintptr_t directOffsets[] = { 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0x168 };
            for (uintptr_t dOff : directOffsets)
            {
                if (ReadPtr(actor + dOff, &comp) && CompValid(comp))
                    return comp;
            }

            return 0;
        }

        uintptr_t CompForCharacter(uintptr_t actor)
        {
            if (!IsValidCanonicalPtr(actor)) return 0;

            auto resolveFromActor = [](uintptr_t act) -> uintptr_t {
                if (!IsValidCanonicalPtr(act)) return 0;
                uintptr_t comp = 0;
                // Step 1: If act is already a SubContainer (e.g. from Player::GetActor), check act + 0x38 directly!
                if (ReadPtr(act + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                    return comp;
                // Step 2: Standard actor walk (*(*(act+0x68)+0x38))
                uintptr_t sub = 0;
                if (ReadPtr(act + kOff_Container_Sub, &sub) && IsValidCanonicalPtr(sub))
                {
                    if (ReadPtr(sub + kOff_Sub_EquipComp, &comp) && CompValid(comp))
                        return comp;
                }
                comp = FindEquipCompFromActor(act);
                if (comp && CompValid(comp))
                    return comp;
                return 0;
            };

            // 1. Direct actor check
            uintptr_t comp = resolveFromActor(actor);
            if (comp) return comp;

            // 2. If actor is an owner object, inspect inner actor (+0x68)
            uintptr_t innerAct = 0;
            if (ReadPtr(actor + kOff_Owner_Actor, &innerAct) && IsValidCanonicalPtr(innerAct) && innerAct != actor)
            {
                comp = resolveFromActor(innerAct);
                if (comp) return comp;
            }

            return 0;
        }

        uintptr_t FindTrackedCharacterComp(int targetIdx)
        {
            if (targetIdx < 0 || targetIdx > 2) return 0;
            for (int p = 0; p < 3; ++p)
            {
                const uintptr_t root = PreferEquipmentOwner(
                    Player::GetOwner(p), Player::GetActor(p));
                const uintptr_t comp = CompForCharacter(root);
                if (!comp) continue;
                const int id = Inventory::IdentifyCharacterFromComp(comp);
                if (AcceptCharacterComponent(targetIdx, id, p))
                    return comp;
            }
            return 0;
        }

        static int s_activeCharIdx = -1; // -1 = auto-detect active player character
        static int s_targetMode = 0;     // 0 = Player Character, 1 = Mount / Horse, 2 = Bag Item
        static int s_activeMountIdx = 0;

        uintptr_t FindMountComp(int index)
        {
            if (index < 0 || index >= 4) return 0;

            // 1. Direct query from Player mount descriptor (already resolved with horse gear)
            Player::MountDescriptor desc{};
            if (Player::GetMountDescriptor(index, &desc) && desc.equipComp >= kMinPointer)
            {
                if (CompValid(desc.equipComp) || CompHasHorseGear(desc.equipComp))
                    return desc.equipComp;
            }

            const uintptr_t root = PreferEquipmentOwner(
                Player::GetMountOwner(index), Player::GetMountActor(index));
            if (root)
            {
                const uintptr_t comp = CompForCharacter(root);
                if (comp && CompHasHorseGear(comp)) return comp;
                if (comp && CompValid(comp)) return comp;
            }

            const uintptr_t hooked = g_mountComp.load(std::memory_order_acquire);
            if (hooked && CompValid(hooked) && index == 0 && CompHasHorseGear(hooked))
                return hooked;

            if (index == 0)
            {
                for (int m = 0; m < 4; ++m)
                {
                    Player::MountDescriptor mDesc{};
                    if (Player::GetMountDescriptor(m, &mDesc) && mDesc.equipComp >= kMinPointer)
                    {
                        if (CompValid(mDesc.equipComp) || CompHasHorseGear(mDesc.equipComp))
                            return mDesc.equipComp;
                    }

                    const uintptr_t mRoot = PreferEquipmentOwner(
                        Player::GetMountOwner(m), Player::GetMountActor(m));
                    if (!mRoot) continue;
                    const uintptr_t comp = CompForCharacter(mRoot);
                    if (comp && CompHasHorseGear(comp)) return comp;
                }
                for (int m = 0; m < 4; ++m)
                {
                    const uintptr_t mRoot = PreferEquipmentOwner(
                        Player::GetMountOwner(m), Player::GetMountActor(m));
                    if (!mRoot) continue;
                    const uintptr_t comp = CompForCharacter(mRoot);
                    if (comp && CompValid(comp)) return comp;
                }
            }

            return 0;
        }

        uintptr_t ClientComp()
        {
            if (s_targetMode == 1)
            {
                const uintptr_t mountComp = FindMountComp(s_activeMountIdx);
                if (mountComp) return mountComp;
                return 0;
            }

            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx;

            if (targetIdx == liveIdx)
            {
                const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                if (liveChar)
                {
                    const uintptr_t comp = CompForCharacter(liveChar);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                // Direct check on active controlled player (slot 0)
                const uintptr_t liveOwner = Player::GetOwner(0);
                const uintptr_t liveActor = Player::GetActor(0);
                if (liveOwner || liveActor)
                {
                    const uintptr_t root = PreferEquipmentOwner(liveOwner, liveActor);
                    const uintptr_t comp = CompForCharacter(root);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                if (targetIdx > 0 && targetIdx < 3)
                {
                    const uintptr_t directActor = Player::GetActor(targetIdx);
                    if (directActor)
                    {
                        const uintptr_t comp = CompForCharacter(directActor);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                    const uintptr_t directOwner = Player::GetOwner(targetIdx);
                    if (directOwner)
                    {
                        const uintptr_t comp = CompForCharacter(directOwner);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                }

                const uintptr_t h = Inventory::ClientHolderAddr();
                if (h)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(h + 8, &owner) && owner >= kMinPointer)
                    {
                        const uintptr_t comp = CompForCharacter(owner);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             liveIdx))
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
                        const int id = Inventory::IdentifyCharacterFromComp(hooked);
                        if (id < 0 || id == targetIdx) return hooked;
                    }
                }

                if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                    return comp;
                if (const uintptr_t profComp = Player::GetProfileEquipComp(targetIdx))
                    return profComp;
                return 0;
            }

            // Off-screen selection: strict identity lookup with direct companion actor fallback.
            const uintptr_t actor = Inventory::CharacterAddr(targetIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp)
                {
                    const int id = Inventory::IdentifyCharacterFromComp(comp);
                    if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                }
            }
            // Kliff (0) included: his tracked live actor/owner is the world
            // body that owns the render chain DyeApplyBatch needs, so prefer
            // it over falling straight through to the profile comp.
            if (targetIdx >= 0 && targetIdx < 3)
            {
                const uintptr_t directActor = Player::GetActor(targetIdx);
                if (directActor)
                {
                    const uintptr_t comp = CompForCharacter(directActor);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
                const uintptr_t directOwner = Player::GetOwner(targetIdx);
                if (directOwner)
                {
                    const uintptr_t comp = CompForCharacter(directOwner);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
            }
            if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                return comp;
            if (const uintptr_t profComp = Player::GetProfileEquipComp(targetIdx))
                return profComp;
            return 0;
        }

        uintptr_t ServerComp()
        {
            if (s_targetMode == 1)
            {
                const uintptr_t mountComp = FindMountComp(s_activeMountIdx);
                if (mountComp) return mountComp;
                return 0;
            }

            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx;

            if (targetIdx == liveIdx)
            {
                const uintptr_t serverChar = Inventory::ServerCharacterAddr();
                if (serverChar)
                {
                    const uintptr_t comp = CompForCharacter(serverChar);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                // Direct check on active controlled player (slot 0)
                const uintptr_t liveOwner = Player::GetOwner(0);
                const uintptr_t liveActor = Player::GetActor(0);
                if (liveOwner || liveActor)
                {
                    const uintptr_t root = PreferEquipmentOwner(liveOwner, liveActor);
                    const uintptr_t comp = CompForCharacter(root);
                    if (comp && AcceptCharacterComponent(targetIdx,
                                                         Inventory::IdentifyCharacterFromComp(comp),
                                                         liveIdx))
                        return comp;
                }

                if (targetIdx > 0 && targetIdx < 3)
                {
                    const uintptr_t directActor = Player::GetActor(targetIdx);
                    if (directActor)
                    {
                        const uintptr_t comp = CompForCharacter(directActor);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                    const uintptr_t directOwner = Player::GetOwner(targetIdx);
                    if (directOwner)
                    {
                        const uintptr_t comp = CompForCharacter(directOwner);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             targetIdx))
                            return comp;
                    }
                }

                const uintptr_t h = Inventory::ServerHolderAddr();
                if (h)
                {
                    uintptr_t owner = 0;
                    if (ReadPtr(h + 8, &owner) && owner >= kMinPointer)
                    {
                        const uintptr_t comp = CompForCharacter(owner);
                        if (comp && AcceptCharacterComponent(targetIdx,
                                                             Inventory::IdentifyCharacterFromComp(comp),
                                                             liveIdx))
                            return comp;
                    }
                }

                if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                    return comp;
                if (const uintptr_t profComp = Player::GetProfileEquipComp(targetIdx))
                    return profComp;
                return 0;
            }

            const uintptr_t actor = Inventory::CharacterAddr(targetIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp)
                {
                    const int id = Inventory::IdentifyCharacterFromComp(comp);
                    if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                }
            }
            // Kliff (0) included - mirror of the ClientComp off-screen fix.
            if (targetIdx >= 0 && targetIdx < 3)
            {
                const uintptr_t directActor = Player::GetActor(targetIdx);
                if (directActor && directActor != actor)
                {
                    const uintptr_t comp = CompForCharacter(directActor);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
                const uintptr_t directOwner = Player::GetOwner(targetIdx);
                if (directOwner && directOwner != actor)
                {
                    const uintptr_t comp = CompForCharacter(directOwner);
                    if (comp)
                    {
                        const int id = Inventory::IdentifyCharacterFromComp(comp);
                        if (AcceptCharacterComponent(targetIdx, id, targetIdx)) return comp;
                    }
                }
            }
            if (const uintptr_t comp = FindTrackedCharacterComp(targetIdx))
                return comp;
            if (const uintptr_t profComp = Player::GetProfileEquipComp(targetIdx))
                return profComp;
            return 0;
        }

        void* __fastcall hkEquipBatch(void* a1, void* a2, void* a3, void* a4)
        {
            __try
            {
                const uintptr_t comp = reinterpret_cast<uintptr_t>(a1);
                if (comp >= kMinPointer && CompValid(comp))
                {
                    if (CompHasHorseGear(comp))
                    {
                        g_mountComp.store(comp, std::memory_order_release);
                    }
                    else if (g_comp.load(std::memory_order_relaxed) != comp)
                    {
                        g_comp.store(comp, std::memory_order_release);
                        s_lastEquipChangeMs.store(GetTickCount64(), std::memory_order_release);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            return oEquipBatch(a1, a2, a3, a4);
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
                return entry;
            }
            return 0;
        }

        uint32_t ReadRecords(uintptr_t itemVal, uint8_t out[kDye_MaxChannels][16])
        {
            memset(out, 0, kDye_MaxChannels * 16);
            uintptr_t data = 0;
            uint32_t  count = 0;

            // Modern TU 1.17+ / TU 2.xx (+0x78 data, +0x80 count)
            if (ReadPtr(itemVal + 0x78, &data) && data >= kMinPointer &&
                Read32(itemVal + 0x80, &count) && count > 0)
            {
            }
            // Alternate / legacy (+0x70 data, +0x78 count)
            else if (ReadPtr(itemVal + 0x70, &data) && data >= kMinPointer &&
                     Read32(itemVal + 0x78, &count) && count > 0)
            {
            }
            else
            {
                return 0;
            }
            if (count > kDye_MaxChannels) count = kDye_MaxChannels;

            uint32_t mask = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint8_t rec[16];
                bool ok = true;
                for (int b = 0; b < 16 && ok; ++b)
                    ok = Read8(data + i * 16 + b, &rec[b]);
                if (!ok) continue;
                const uint8_t ch = rec[6];
                if (ch >= kDye_MaxChannels) continue;
                memcpy(out[ch], rec, 16);
                mask |= 1u << ch;
            }
            return mask;
        }

        void BuildSetRecord(uint8_t out[16], int channel, const Dye::Channel& c)
        {
            memset(out, 0, 16);
            memcpy(out + 0, &c.groupKey, 4);
            const uint16_t mat = (c.materialId == 0xFFFF) ? 0x0001 : c.materialId;
            memcpy(out + 4, &mat, 2);
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
                        add(CompForCharacter(cand));
                    }
                }
            }
            return n;
        }

        bool CallDyeApply(uintptr_t comp, void* batch, int* outErr, bool forceRetry = false)
        {
            if (!g_dyeApply) {
                LOG_WARN("dye: CallDyeApply skipped - g_dyeApply is NULL");
                return false;
            }
            if (!IsValidCanonicalPtr(comp)) {
                return false;
            }
            if (!batch || !outErr) {
                return false;
            }
            // Explicit user actions force past the fault cache: the restore's
            // escalating backoff must never lock the player out of dyeing -
            // the comp may have fully recovered since the last fault.
            if (!forceRetry && IsCompFaulted(comp)) return false;
            // Guard kept at the PROVEN minimal contract (the pre-2.02 working
            // build shipped exactly this): a live actor -> possessor -> pawn
            // chain. The deeper links the 2.02 prologue also touches
            // ([act+0x88], [act+0x68], [pawn+0x68], comp+0x90) are deliberately
            // NOT rejected here - static rejects cost real successes when the
            // engine recovers internally, and any fault lands in the SEH below
            // where the escalating fault cache bounds the retry cost anyway.
            uintptr_t act = 0, poss = 0, pawn = 0;
            if (!ReadPtr(comp + 8, &act) || !IsValidCanonicalPtr(act)) return false;
            if (!ReadPtr(act + kOff_Owner_Possessor, &poss) || !IsValidCanonicalPtr(poss)) return false;
            if (!ReadPtr(poss + kOff_Possessor_Pawn, &pawn) || !IsValidCanonicalPtr(pawn)) return false;

            *outErr = -999999;
            DWORD exCode = 0;
            __try
            {
                g_dyeApply(reinterpret_cast<void*>(comp), outErr, batch);
                if (*outErr == 0) ClearCompFault(comp); // recovered - stop backing off
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
            if (!g_dyeUpsert || itemVal < kMinPointer || !rec) return false;
            __try
            {
                g_dyeUpsert(reinterpret_cast<void*>(itemVal), rec);
                return true;
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
            if (!IsRenderComp(comp)) {
                LOG_WARN("dye: CallDyeVisualSet skipped - comp=%p is not a render comp", reinterpret_cast<void*>(comp));
                return false;
            }

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
            if (!IsRenderComp(comp)) {
                LOG_WARN("dye: CallDyeVisualClear skipped - comp=%p is not a render comp", reinterpret_cast<void*>(comp));
                return false;
            }

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
            // (container-like, u16 key, out: lea r8=[rsp+0x50], counter) - a
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
            if (entry < kMinPointer || channel < 0 || channel >= 12) return false;
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
            uintptr_t dyeDataOff = 0x78;
            uintptr_t dyeCountOff = 0x80;
            uint32_t count = 0;
            uintptr_t data = 0;
            if (Read32(entry + dyeCountOff, &count) && count > 0 && count <= 12)
            {
                if (ReadPtr(entry + dyeDataOff, &data) && data >= kMinPointer)
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
                                    Write64(data + static_cast<uintptr_t>(j) * 16, w1);
                                    Write64(data + static_cast<uintptr_t>(j) * 16 + 8, w2);
                                }
                            }
                            Write32(entry + dyeCountOff, count - 1);
                            return true;
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

        bool MirrorToServer(uint16_t tag, int64_t instId,
                            const uint8_t recs[kDye_MaxChannels][16], uint32_t mask)
        {
            if (!g_dyeUpsert)
            {
                LOG_WARN("dye: visual test only - durable upsert unresolved; server entry untouched.");
                return false;
            }

            uint8_t   oldFlag = 0;
            const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
            if (!flagAddr)
            {
                LOG_WARN("dye: realm flag unresolved - skipping the durable write.");
                return false;
            }
            if (!RawWrite8(flagAddr, 1)) return false;

            bool ok = false;

            // 1. Write to server-authority equip component for active selection
            const uintptr_t comp = ServerComp();
            if (comp)
            {
                const uintptr_t entry = FindEntryByTag(comp, tag);
                if (entry)
                {
                    Write32(entry + kOff_ItemVal_DyeCount, 0);
                    for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                        if (mask & (1u << ch))
                            ok |= CallDyeUpsert(entry, recs[ch]);
                }
            }

            // 2. Multi-copy sync for the SELECTED character only
            const int liveIdx = Inventory::ActivePlayerCharacterIdx();
            const int targetIdx = (s_activeCharIdx < 0) ? liveIdx : s_activeCharIdx;
            uintptr_t copies[16] = {};
            const int nCopies = (targetIdx >= 0 && targetIdx < 3)
                ? Inventory::CharacterAddrs(targetIdx, copies, 16) : 0;
            for (int i = 0; i < nCopies; ++i)
            {
                const uintptr_t act = copies[i];
                if (!act) continue;
                const uintptr_t cComp = CompForCharacter(act);
                if (cComp && cComp != comp)
                {
                    const uintptr_t cEntry = FindEntryByTag(cComp, tag);
                    if (cEntry)
                    {
                        Write32(cEntry + kOff_ItemVal_DyeCount, 0);
                        for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                            if (mask & (1u << ch))
                                ok |= CallDyeUpsert(cEntry, recs[ch]);
                    }
                }
            }

            // 2b. PartyIndex companion containers - the copy the native UI
            // reads; CharacterAddrs can miss it (equipment.cpp's proven walk).
            if (targetIdx >= 0 && targetIdx < 3)
            {
                uintptr_t party[8] = {};
                const int nParty = PartyCompanionComps(targetIdx, party, 8, comp);
                for (int i = 0; i < nParty; ++i)
                {
                    const uintptr_t pEntry = FindEntryByTag(party[i], tag);
                    if (!pEntry) continue;
                    Write32(pEntry + kOff_ItemVal_DyeCount, 0);
                    for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                        if (mask & (1u << ch))
                            ok |= CallDyeUpsert(pEntry, recs[ch]);
                }
            }

            // 3. Active character server container mirror
            if (targetIdx == liveIdx)
            {
                const uintptr_t sChar = Inventory::ServerCharacterAddr();
                if (sChar)
                {
                    const uintptr_t sComp = CompForCharacter(sChar);
                    if (sComp && sComp != comp)
                    {
                        const uintptr_t sEntry = FindEntryByTag(sComp, tag);
                        if (sEntry)
                        {
                            Write32(sEntry + kOff_ItemVal_DyeCount, 0);
                            for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                                if (mask & (1u << ch))
                                    ok |= CallDyeUpsert(sEntry, recs[ch]);
                        }
                    }
                }
            }

            // 4. Also write to server inventory holder if slot exists there
            if (instId > 0)
            {
                const uintptr_t serverSlot = Inventory::FindSlotByInstance(Inventory::ServerHolderAddr(), instId);
                if (serverSlot)
                {
                    Write32(serverSlot + kOff_ItemVal_DyeCount, 0);
                    for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                        if (mask & (1u << ch))
                            ok |= CallDyeUpsert(serverSlot, recs[ch]);
                }
            }

            // 5. Also write to client inventory holder
            if (instId > 0)
            {
                const uintptr_t clientSlot = Inventory::FindSlotByInstance(Inventory::ClientHolderAddr(), instId);
                if (clientSlot)
                {
                    Write32(clientSlot + kOff_ItemVal_DyeCount, 0);
                    for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                        if (mask & (1u << ch))
                            CallDyeUpsert(clientSlot, recs[ch]);
                }
            }

            RawWrite8(flagAddr, oldFlag);
            return ok;
        }

        struct Request
        {
            uint16_t     tag     = 0;
            int          channel = -1;   // -1 = all 12
            bool         clear   = false;
            Dye::Channel value{};
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
        Dye::SlotInfo    g_slots[kMaxSlots];
        int              g_slotCount = 0;

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

        void RebuildSnapshot()
        {
            g_slotCount = 0;

            if (s_targetMode == 1)
            {
                const uintptr_t comp = ClientComp();
                if (comp)
                {
                    uintptr_t array = 0;
                    uint32_t  count = 0;
                    uintptr_t stride = 0xD0;
                    uintptr_t tagOffset = 0xC8;
                    uintptr_t dyeDataOffset = 0x78;
                    uintptr_t dyeCountOffset = 0x80;
                    if (ReadEquipTable(comp, array, count, &stride, &tagOffset, &dyeDataOffset, &dyeCountOffset))
                    {
                        for (uint32_t i = 0; i < count && g_slotCount < kMaxSlots; ++i)
                        {
                            const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
                            uint16_t tid = 0, tag = 0;
                            int64_t  qty = 0, inst = 0;
                            if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                            if (!Read64(entry + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;
                            Read64(entry + kOff_ItemVal_InstanceId, &inst); // Worn mount gear may have inst == 0
                            Read16(entry + tagOffset, &tag);

                            char itemName[96] = "";
                            char icon[128] = "";
                            if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                                snprintf(itemName, sizeof(itemName), "Item #%u", tid);
                            Inventory::IconForTypeId(tid, icon, sizeof(icon));

                            const HorseSlotType slotType = GetHorseSlotType(itemName, icon);
                            const char* sName = nullptr;
                            if (slotType != HorseSlotType::None)
                            {
                                sName = MountSlotName(slotType);
                            }
                            else
                            {
                                switch (tag)
                                {
                                case 22: case 0: sName = "Chamfron"; break;
                                case 23: case 1: sName = "Horse Armor"; break;
                                case 14: case 2: sName = "Saddle"; break;
                                case 24: case 3: sName = "Stirrups"; break;
                                case 25: case 4: sName = "Horseshoes"; break;
                                default:
                                    // Skip cargo / non-mount items (e.g. potions, water bottles, food)
                                    continue;
                                }
                            }

                            const int maxZones = 12;
                            Dye::SlotInfo& s = g_slots[g_slotCount++];
                            s = Dye::SlotInfo{};
                            s.tag        = tag;
                            s.typeId     = tid;
                            s.instanceId = inst;
                            s.maxZones   = maxZones;
                            uint32_t rawDye = 0;
                            Read32(entry + dyeCountOffset, &rawDye);
                            s.dyeCount = (rawDye <= 12) ? rawDye : 12;

                            snprintf(s.slotName, sizeof(s.slotName), "%s", sName ? sName : "Mount Gear");
                            snprintf(s.itemName, sizeof(s.itemName), "%s", itemName);
                            snprintf(s.icon, sizeof(s.icon), "%s", icon);
                            s.dyeable = true;
                        }
                    }
                }
                return;
            }

            // ===== INVENTORY BAG ITEM MODE (Mode 2) ==========================
            if (s_targetMode == 2)
            {
                const uintptr_t holder = Inventory::ClientHolderAddr();
                if (holder < kMinPointer) return;

                uintptr_t buckets = 0;
                uint32_t  bcount  = 0;
                if (!ReadPtr(holder + kOff_InvHolder_Buckets, &buckets) || buckets < kMinPointer) return;
                if (!Read32(holder + kOff_InvHolder_Count, &bcount) || bcount > 4096) return;

                uint16_t itemIdx = 0;
                for (uint32_t b = 0; b < bcount && g_slotCount < kMaxSlots; ++b)
                {
                    uintptr_t bucket = 0;
                    if (!ReadPtr(buckets + static_cast<uintptr_t>(b) * 8, &bucket) || bucket < kMinPointer) continue;

                    uintptr_t slots = 0;
                    uint16_t  scount = 0;
                    if (!ReadPtr(bucket + kOff_InvBucket_Slots, &slots) || slots < kMinPointer) continue;
                    if (!Read16(bucket + kOff_InvBucket_Count, &scount) || scount == 0 || scount > 8192) continue;

                    for (uint16_t i = 0; i < scount && g_slotCount < kMaxSlots; ++i)
                    {
                        const uintptr_t slot = slots + static_cast<uintptr_t>(i) * core::GetSlotStride();
                        uint16_t tid = 0;
                        int64_t  qty = 0, inst = 0;
                        if (!Read16(slot + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                        if (!Read64(slot + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;
                        if (!Read64(slot + kOff_ItemVal_InstanceId, &inst) || inst <= 0) continue;

                        char itemName[96] = "";
                        char icon[128] = "";
                        if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                            snprintf(itemName, sizeof(itemName), "Item #%u", tid);
                        Inventory::IconForTypeId(tid, icon, sizeof(icon));

                        if (IsDummyOrUnarmed(tid, itemName)) continue;

                        const HorseSlotType hType = GetHorseSlotType(itemName, icon);
                        if (!IconPrefabDyeable(icon) && hType == HorseSlotType::None)
                            continue;

                        const int maxZones = 12;
                        Dye::SlotInfo& s = g_slots[g_slotCount++];
                        s = Dye::SlotInfo{};
                        s.tag        = itemIdx++;
                        s.typeId     = tid;
                        s.instanceId = inst;
                        s.maxZones   = maxZones;
                        uint32_t rawDye = 0;
                        Read32(slot + kOff_ItemVal_DyeCount, &rawDye);
                        s.dyeCount = (rawDye <= 12) ? rawDye : 12;

                        if (hType != HorseSlotType::None)
                            snprintf(s.slotName, sizeof(s.slotName), "%s", MountSlotName(hType));
                        else
                            snprintf(s.slotName, sizeof(s.slotName), "Bag Item %u", s.tag + 1);

                        snprintf(s.itemName, sizeof(s.itemName), "%s", itemName);
                        snprintf(s.icon, sizeof(s.icon), "%s", icon);
                        s.dyeable = true;
                    }
                }
                return;
            }

            // Player Mode
            const uintptr_t comp = ClientComp();
            if (!comp) return;

            uintptr_t array = 0;
            uint32_t  count = 0;
            uintptr_t stride = 0xD0;
            uintptr_t tagOffset = 0xC8;
            uintptr_t dyeDataOffset = 0x78;
            uintptr_t dyeCountOffset = 0x80;
            if (!ReadEquipTable(comp, array, count, &stride, &tagOffset, &dyeDataOffset, &dyeCountOffset)) return;

            for (uint32_t i = 0; i < count && g_slotCount < kMaxSlots; ++i)
            {
                const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
                uint16_t tid = 0, tag = 0;
                int64_t  inst = 0, qty = 1;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                Read64(entry + kOff_InvSlot_Quantity, &qty);
                Read64(entry + kOff_ItemVal_InstanceId, &inst);

                char itemName[96] = "";
                char icon[128] = "";
                if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                    snprintf(itemName, sizeof(itemName), "Item #%u", tid);
                Inventory::IconForTypeId(tid, icon, sizeof(icon));

                if (IsDummyOrUnarmed(tid, itemName)) continue;

                Read16(entry + tagOffset, &tag);

                if (tag == 14 || tag == 22 || tag == 23 || tag == 24 || tag == 25)
                    continue;

                const int maxZones = 12;
                Dye::SlotInfo& s = g_slots[g_slotCount++];
                s = Dye::SlotInfo{};
                s.tag        = tag;
                s.typeId     = tid;
                s.instanceId = inst;
                s.maxZones   = maxZones;
                uint32_t rawDye = 0;
                Read32(entry + dyeCountOffset, &rawDye);
                s.dyeCount = (rawDye <= 12) ? rawDye : 12;

                if (const char* n = SlotNameForTag(tag))
                    snprintf(s.slotName, sizeof(s.slotName), "%s", n);
                else
                    snprintf(s.slotName, sizeof(s.slotName), "Slot %u", tag);

                snprintf(s.itemName, sizeof(s.itemName), "%s", itemName);
                snprintf(s.icon, sizeof(s.icon), "%s", icon);
                s.dyeable = true;
            }
        }

        void ProcessRequest()
        {
            const Request req = g_req;

            // ===== INVENTORY BAG ITEM MODE (Mode 2) ==========================
            if (s_targetMode == 2)
            {
                int64_t instId = 0;
                for (int i = 0; i < g_slotCount; ++i)
                {
                    if (g_slots[i].tag == req.tag)
                    {
                        instId = g_slots[i].instanceId;
                        break;
                    }
                }

                if (instId <= 0)
                {
                    LOG_WARN("dye: inventory item instance not found for tag %u.", req.tag);
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }

                const uintptr_t clientSlot = FindSlotByInstance(Inventory::ClientHolderAddr(), instId);
                const int chFirst = (req.channel < 0) ? 0 : req.channel;
                const int chLast  = (req.channel < 0) ? static_cast<int>(kDye_MaxChannels) - 1 : req.channel;
                bool clientOk = false;

                if (clientSlot && g_dyeUpsert)
                {
                    for (int ch = chFirst; ch <= chLast; ++ch)
                    {
                        uint8_t rec[16] = {};
                        if (req.clear) BuildClearRecord(rec, ch);
                        else           BuildSetRecord(rec, ch, req.value);
                        clientOk |= CallDyeUpsert(clientSlot, rec);
                    }
                }

                bool serverOk = false;
                uint8_t oldFlag = 0;
                const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
                if (flagAddr && RawWrite8(flagAddr, 1))
                {
                    const uintptr_t serverSlot = FindSlotByInstance(Inventory::ServerHolderAddr(), instId);
                    if (serverSlot && g_dyeUpsert)
                    {
                        Write32(serverSlot + kOff_ItemVal_DyeCount, 0);
                        for (int ch = chFirst; ch <= chLast; ++ch)
                        {
                            uint8_t rec[16] = {};
                            if (req.clear) BuildClearRecord(rec, ch);
                            else           BuildSetRecord(rec, ch, req.value);
                            serverOk |= CallDyeUpsert(serverSlot, rec);
                        }
                    }
                    RawWrite8(flagAddr, oldFlag);
                }

                g_state.store(static_cast<int>((clientOk || serverOk) ? Dye::OpState::Done : Dye::OpState::Failed),
                              std::memory_order_release);
                return;
            }

            // ===== MOUNT MODE: data upsert + visual leaves ===================
            if (s_targetMode == 1)
            {
                const uintptr_t comp = ClientComp();
                if (!comp)
                {
                    LOG_WARN("dye: mount equip component not resolved.");
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }

                uintptr_t entry = FindEntryByTag(comp, req.tag);
                if (!entry)
                {
                    LOG_WARN("dye: no equipped entry for mount slot tag %u.", req.tag);
                    g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                    return;
                }

                int64_t instId = 0;
                Read64(entry + kOff_ItemVal_InstanceId, &instId);

                const int chFirst = (req.channel < 0) ? 0 : req.channel;
                const int chLast  = (req.channel < 0) ? 11 : req.channel;
                bool upsertOk = false;
                bool visualOk = false;

                for (int ch = chFirst; ch <= chLast; ++ch)
                {
                    uint8_t rec[16] = {};
                    if (req.clear) BuildClearRecord(rec, ch);
                    else           BuildSetRecord(rec, ch, req.value);
                    if (g_dyeUpsert) upsertOk |= CallDyeUpsert(entry, rec);

                    if (req.clear)
                    {
                        // VisualClear also walks mount render structures on 2.02 -
                        // keep the data-side removal only; visual refreshes on
                        // re-equip/summon replay like the set path.
                        CallDyeRecordRemove(entry, ch);
                    }
                    else
                    {
                        // Mount mode: DyeApplySlot is disabled on 2.02 (false
                        // signature) and DyeVisualSet faults on mount comps
                        // (see design note in the original version). Visual
                        // update comes from the auto-restore replay on
                        // summon/reload; here we only keep the data write.
                        (void)0;
                    }
                }

                uint16_t mountItemTypeId = 0;
                Read16(entry + kOff_InvSlot_TypeId, &mountItemTypeId);

                uint8_t recs[kDye_MaxChannels][16];
                const uint32_t mask = ReadRecords(entry, recs);
                bool durableOk = false;
                if (instId > 0)
                {
                    durableOk = MirrorToServer(req.tag, instId, recs, mask);
                }

                // Mirror to all inventory holders so gear retains custom dye upon unequip/re-equip.
                // Mount gear copies often carry instId == 0, so pass typeId as
                // the matching fallback (FindAndApplyAllHolders matches by
                // instId when > 0, else by typeId).
                struct DyeSyncCtx {
                    const uint8_t (*recs)[16];
                    uint32_t mask;
                    bool clear;
                } syncCtx{ recs, mask, req.clear };

                Inventory::FindAndApplyAllHolders(instId, [](uintptr_t slot, void* ud) {
                    auto* ctx = static_cast<DyeSyncCtx*>(ud);
                    if (!slot || !ctx) return;
                    if (ctx->clear)
                    {
                        Write32(slot + kOff_ItemVal_DyeCount, 0);
                    }
                    else
                    {
                        Write32(slot + kOff_ItemVal_DyeCount, 0);
                        for (int c = 0; c < static_cast<int>(kDye_MaxChannels); ++c)
                        {
                            if (ctx->mask & (1u << c))
                                CallDyeUpsert(slot, ctx->recs[c]);
                        }
                    }
                }, &syncCtx, mountItemTypeId);

                // Cache for auto-restore across summon & reload
                if (req.tag < 32)
                {
                    if (req.clear)
                    {
                        s_savedMountSlots[req.tag].active = false;
                        s_savedMountSlots[req.tag].typeId = 0;
                        s_savedMountSlots[req.tag].instanceId = 0;
                        s_savedMountSlots[req.tag].mask = 0;
                        ClearSavedItemDye(mountItemTypeId);
                    }
                    else
                    {
                        s_savedMountSlots[req.tag].active = true;
                        s_savedMountSlots[req.tag].tag = req.tag;
                        s_savedMountSlots[req.tag].typeId = mountItemTypeId;
                        s_savedMountSlots[req.tag].instanceId = instId;
                        s_savedMountSlots[req.tag].mask = mask;
                        memcpy(s_savedMountSlots[req.tag].records, recs, sizeof(recs));
                        Read32(entry + kOff_ItemVal_DyeCount, &s_savedMountSlots[req.tag].dyeCount);

                        UpsertSavedItemDye(mountItemTypeId, s_savedMountSlots[req.tag].mask, s_savedMountSlots[req.tag].records, s_savedMountSlots[req.tag].dyeCount);
                    }
                    SaveDyeCacheToFile();
                }

                g_state.store(static_cast<int>((upsertOk || durableOk || visualOk) ? Dye::OpState::Done : Dye::OpState::Failed),
                              std::memory_order_release);
                return;
            }

            // ===== PLAYER CHARACTER MODE (Kliff, Damiane, Oongka) ============
            const uintptr_t comp = ClientComp();
            if (!comp)
            {
                LOG_WARN("dye: apply refused - equip component not resolved.");
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }

            uintptr_t entry = FindEntryByTag(comp, req.tag);
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
            for (int ch = chFirst; ch <= chLast; ++ch)
            {
                uint8_t* rec = batch + kDyeBatch_RecordsOff + static_cast<size_t>(ch) * 16;
                if (req.clear) BuildClearRecord(rec, ch);
                else           BuildSetRecord(rec, ch, req.value);
            }

            const int curCharIdx = (s_activeCharIdx < 0) ? Inventory::ActivePlayerCharacterIdx() : s_activeCharIdx;
            // Profile equip comp for EVERY character including Kliff (0):
            // GetProfileOwner prefers an owner whose full live render chain is
            // alive - exactly what the native DyeApplyBatch prologue requires -
            // so this is the candidate that still applies when the comp
            // resolved above carries a dangling chain (companions AND
            // un-possessed Kliff). Excluding 0 here was why Kliff's dye always
            // fell through to data-only while Damiane's worked.
            const uintptr_t profComp = (curCharIdx >= 0) ? Player::GetProfileEquipComp(curCharIdx) : 0;
            const uintptr_t profEntry = profComp ? FindEntryByTag(profComp, req.tag) : 0;

            uintptr_t liveComp = 0;
            // Live-comp re-resolution is not companion-only: when Kliff (0) is
            // targeted while another body is possessed, `comp` can be a
            // profile/server-side comp whose apply chain is dead. Try the
            // tracked live actor/owner comps for him too; duplicates are
            // deduplicated by the != comp checks below.
            if (curCharIdx >= 0 && curCharIdx < 3)
            {
                const uintptr_t directActor = Player::GetActor(curCharIdx);
                if (directActor) liveComp = CompForCharacter(directActor);
                if (!liveComp)
                {
                    const uintptr_t directOwner = Player::GetOwner(curCharIdx);
                    if (directOwner) liveComp = CompForCharacter(directOwner);
                }
                if (!liveComp)
                {
                    const uintptr_t cAddr = Inventory::CharacterAddr(curCharIdx);
                    if (cAddr) liveComp = CompForCharacter(cAddr);
                }
                if (!liveComp)
                {
                    liveComp = FindTrackedCharacterComp(curCharIdx);
                }
            }
            if (!liveComp && IsRenderComp(comp))
                liveComp = comp;
            const uintptr_t liveEntry = liveComp ? FindEntryByTag(liveComp, req.tag) : 0;

            int err = 0;
            bool applyOk = false;
            if (g_dyeApply && comp)
            {
                // Candidate comps, deduplicated. When every resolver above
                // collapses onto ONE comp and that comp's render chain is
                // broken (Damiane 2026-09-13 03:25: comp == liveComp ==
                // profComp -> the engine leaf faults -> data-only), the
                // character's other realm copies from the CharacterManager and
                // the hook-captured live comp are the remaining candidates.
                // A copy is accepted only when its tag entry holds the SAME
                // item as the target entry, so another character's comp can
                // never be dyed by accident.
                uintptr_t applyComps[10] = { comp };
                int nApply = 1;
                auto addApplyComp = [&](uintptr_t c) {
                    if (!c || nApply >= static_cast<int>(sizeof(applyComps) / sizeof(applyComps[0]))) return;
                    for (int i = 0; i < nApply; ++i)
                        if (applyComps[i] == c) return;
                    applyComps[nApply++] = c;
                };
                addApplyComp(liveComp);
                addApplyComp(profComp);

                uint16_t targetTypeId = 0;
                if (entry) Read16(entry + kOff_InvSlot_TypeId, &targetTypeId);
                if (targetTypeId != 0 && targetTypeId != kInvSlot_EmptyType)
                {
                    if (curCharIdx >= 0)
                    {
                        uintptr_t copies[16] = {};
                        const int nCopies = Inventory::CharacterAddrs(curCharIdx, copies, 16);
                        for (int i = 0; i < nCopies; ++i)
                        {
                            const uintptr_t cComp = CompForCharacter(copies[i]);
                            if (!cComp) continue;
                            const uintptr_t cEntry = FindEntryByTag(cComp, req.tag);
                            if (!cEntry) continue;
                            uint16_t cTypeId = 0;
                            if (!Read16(cEntry + kOff_InvSlot_TypeId, &cTypeId) || cTypeId != targetTypeId)
                                continue;
                            addApplyComp(cComp);
                        }

                        // PartyIndex companion containers (equipment.cpp's proven
                        // walk) - CharacterAddrs can miss the copy the native UI
                        // reads; on companions that copy is frequently THE live
                        // one, and its absence is what read as "cannot dye
                        // Damiane/Oongka" when every resolver collapsed.
                        uintptr_t party[8] = {};
                        const int nParty = PartyCompanionComps(curCharIdx, party, 8, comp);
                        for (int i = 0; i < nParty; ++i)
                        {
                            const uintptr_t cEntry = FindEntryByTag(party[i], req.tag);
                            if (!cEntry) continue;
                            uint16_t cTypeId = 0;
                            if (!Read16(cEntry + kOff_InvSlot_TypeId, &cTypeId) || cTypeId != targetTypeId)
                                continue;
                            addApplyComp(party[i]);
                        }
                    }
                    // Hook-captured live comp (the possessed body's equip comp
                    // from the game's own EquipBatch). The same-item gate above
                    // makes it safe no matter which character it belongs to.
                    const uintptr_t hooked = g_comp.load(std::memory_order_acquire);
                    if (hooked)
                    {
                        const uintptr_t hEntry = FindEntryByTag(hooked, req.tag);
                        if (hEntry)
                        {
                            uint16_t hTypeId = 0;
                            if (Read16(hEntry + kOff_InvSlot_TypeId, &hTypeId) && hTypeId == targetTypeId)
                                addApplyComp(hooked);
                        }
                    }
                }

                for (int i = 0; i < nApply; ++i)
                {
                    int cErr = 0;
                    applyOk |= (CallDyeApply(applyComps[i], batch, &cErr, true) && (cErr == 0));
                    if (i == 0) err = cErr;
                }
            }

            // Always upsert records directly into TrItemValue for durability
            bool upsertOk = false;
            for (int ch = chFirst; ch <= chLast; ++ch)
            {
                uint8_t rec[16] = {};
                if (req.clear) BuildClearRecord(rec, ch);
                else           BuildSetRecord(rec, ch, req.value);
                if (g_dyeUpsert)
                {
                    if (entry)
                        upsertOk |= CallDyeUpsert(entry, rec);
                    if (liveComp && liveComp != comp && liveEntry)
                        upsertOk |= CallDyeUpsert(liveEntry, rec);
                    if (profComp && profComp != comp && profComp != liveComp && profEntry)
                        upsertOk |= CallDyeUpsert(profEntry, rec);
                }
            }

            // Universal LIVE-material update: drives render leaves directly
            // Works for companions (Damiane / Oongka) where DyeApplyBatch skips due to no local controller
            bool visualOk = false;
            for (int ch = chFirst; ch <= chLast; ++ch)
            {
                uint8_t rec[16] = {};
                if (req.clear) BuildClearRecord(rec, ch);
                else           BuildSetRecord(rec, ch, req.value);

                if (req.clear)
                {
                    if (comp && entry)
                    {
                        visualOk |= CallDyeVisualClear(comp, entry, req.tag, ch, true);
                        CallDyeRecordRemove(entry, ch);
                        CallDyeUpsert(entry, rec);
                    }
                    if (liveComp && liveComp != comp && liveEntry)
                    {
                        visualOk |= CallDyeVisualClear(liveComp, liveEntry, req.tag, ch);
                        CallDyeRecordRemove(liveEntry, ch);
                        CallDyeUpsert(liveEntry, rec);
                    }
                    if (profComp && profComp != comp && profComp != liveComp && profEntry)
                    {
                        CallDyeRecordRemove(profEntry, ch);
                        CallDyeUpsert(profEntry, rec);
                    }
                }
                else
                {
                    // ApplySlot is DISABLED on 2.02 (signature false-positive:
                    // 0x142B41E60 is a hash/registry utility, see the wrapper).
                    // Live visuals ride on DyeApplyBatch (possessed player) and
                    // DyeVisualSet (render leaves) instead.
                    if (comp && entry && IsRenderComp(comp))
                        visualOk |= CallDyeVisualSet(comp, entry, rec, req.tag, ch, true);

                    if (liveComp && liveComp != comp && liveEntry && IsRenderComp(liveComp))
                    {
                        visualOk |= CallDyeVisualSet(liveComp, liveEntry, rec, req.tag, ch, true);
                        CallDyeUpsert(liveEntry, rec);
                    }
                    if (profComp && profComp != comp && profComp != liveComp && profEntry)
                    {
                        CallDyeUpsert(profEntry, rec);
                    }
                }
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

            entry = FindEntryByTag(comp, req.tag);
            int64_t instId = 0;
            if (entry) Read64(entry + kOff_ItemVal_InstanceId, &instId);
            if (entry && instId > 0)
            {
                uint8_t recs[kDye_MaxChannels][16];
                const uint32_t mask = ReadRecords(entry, recs);
                MirrorToServer(req.tag, instId, recs, mask);

                // Multi-copy server sync for the SELECTED character only
                uint8_t oldFlag = 0;
                const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
                if (flagAddr && RawWrite8(flagAddr, 1))
                {
                    const int syncIdx = (s_activeCharIdx < 0) ? Inventory::ActivePlayerCharacterIdx() : s_activeCharIdx;
                    uintptr_t copies[16] = {};
                    const int nCopies = (syncIdx >= 0 && syncIdx < 3)
                        ? Inventory::CharacterAddrs(syncIdx, copies, 16) : 0;
                    for (int i = 0; i < nCopies; ++i)
                    {
                        const uintptr_t act = copies[i];
                        if (!act) continue;
                        const uintptr_t pComp = CompForCharacter(act);
                        if (pComp && pComp != comp)
                        {
                            const uintptr_t pEntry = FindEntryByTag(pComp, req.tag);
                            if (pEntry)
                            {
                                Write32(pEntry + kOff_ItemVal_DyeCount, 0);
                                for (int ch = chFirst; ch <= chLast; ++ch)
                                {
                                    uint8_t rec[16] = {};
                                    if (req.clear) BuildClearRecord(rec, ch);
                                    else           BuildSetRecord(rec, ch, req.value);
                                    CallDyeUpsert(pEntry, rec);
                                }
                            }
                        }
                    }
                    RawWrite8(flagAddr, oldFlag);
                }

                uint16_t itemTypeId = 0;
                Read16(entry + kOff_InvSlot_TypeId, &itemTypeId);

                const int charIdx = (s_activeCharIdx < 0) ? Inventory::ActivePlayerCharacterIdx() : s_activeCharIdx;
                if (charIdx >= 0 && charIdx < 3 && req.tag < 32)
                {
                    if (req.clear)
                    {
                        s_savedPlayerSlots[charIdx][req.tag].active = false;
                        s_savedPlayerSlots[charIdx][req.tag].typeId = 0;
                        s_savedPlayerSlots[charIdx][req.tag].instanceId = 0;
                        s_savedPlayerSlots[charIdx][req.tag].mask = 0;
                        ClearSavedItemDye(itemTypeId);
                    }
                    else
                    {
                        s_savedPlayerSlots[charIdx][req.tag].active = true;
                        s_savedPlayerSlots[charIdx][req.tag].tag = req.tag;
                        s_savedPlayerSlots[charIdx][req.tag].typeId = itemTypeId;
                        s_savedPlayerSlots[charIdx][req.tag].instanceId = instId;
                        s_savedPlayerSlots[charIdx][req.tag].mask = mask;
                        memcpy(s_savedPlayerSlots[charIdx][req.tag].records, recs, sizeof(recs));
                        Read32(entry + kOff_ItemVal_DyeCount, &s_savedPlayerSlots[charIdx][req.tag].dyeCount);

                        UpsertSavedItemDye(itemTypeId, mask, recs, s_savedPlayerSlots[charIdx][req.tag].dyeCount);
                    }
                    SaveDyeCacheToFile();
                }

                const char* charName = Equipment::CharacterName(charIdx);
                const char* slotName = Equipment::SlotNameForTag(req.tag);
                if (req.clear)
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

            if (visualOk || applyOk)
                g_state.store(static_cast<int>(Dye::OpState::Done), std::memory_order_release);
            else if (upsertOk)
                g_state.store(static_cast<int>(Dye::OpState::DoneDataOnly), std::memory_order_release);
            else
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
        }

        // Drives the engine's own dye-ack batch (the proven live-apply path the
        // dye action itself uses) for one saved slot across every candidate
        // comp of character `c`. The auto-restore needs this because the
        // VisualSet leaf it previously relied on faults on most comps (fault
        // cache skips them), leaving restored records invisible until a
        // re-equip. Data lands on every healthy copy AND the render leaf
        // paints live - so a dye survives restart-without-save.
        // Game-thread only (called from Dye::Tick).
        static void RestoreApplySlot(int charIdx, uint16_t tag, uint16_t typeId,
                                     const uint8_t recs[kDye_MaxChannels][16], uint32_t mask,
                                     uintptr_t comp, uintptr_t profC, uintptr_t liveComp)
        {
            if (!g_dyeApply || mask == 0) return;

            static uint8_t batch[kDyeBatch_Size];
            memset(batch, 0, sizeof(batch));
            for (size_t blk = 0; blk < kDyeBatch_Blocks; ++blk)
            {
                uint8_t* block = batch + blk * kDyeBatch_BlockSize;
                const uint16_t btag = (blk == 0) ? tag : 0xFFFF;
                memcpy(block, &btag, 2);
                for (uint32_t r = 0; r < kDye_MaxChannels; ++r)
                    block[kDyeBatch_RecordsOff + r * 16 + 6] = 0xFF;
            }
            for (uint32_t ch = 0; ch < kDye_MaxChannels; ++ch)
            {
                if (!(mask & (1u << ch))) continue;
                memcpy(batch + kDyeBatch_RecordsOff + ch * 16, recs[ch], 16);
            }

            uintptr_t cands[10] = { comp, profC, liveComp, 0, 0, 0, 0, 0, 0, 0 };
            int n = 3;
            auto addCand = [&](uintptr_t c) {
                if (!c || n >= 10) return;
                for (int i = 0; i < n; ++i) if (cands[i] == c) return;
                cands[n++] = c;
            };
            // Hook-captured live comp: only when its tag entry holds the SAME
            // item (prevents dyeing another character's gear by accident).
            const uintptr_t hooked = g_comp.load(std::memory_order_acquire);
            if (hooked && typeId != 0 && typeId != kInvSlot_EmptyType)
            {
                const uintptr_t hEntry = FindEntryByTag(hooked, tag);
                uint16_t hTypeId = 0;
                if (hEntry && Read16(hEntry + kOff_InvSlot_TypeId, &hTypeId) && hTypeId == typeId)
                    addCand(hooked);
            }
            // PartyIndex companion containers - the copy the native UI reads
            // (CharacterAddrs misses it); same-item gate still applies.
            if (charIdx >= 0 && typeId != 0 && typeId != kInvSlot_EmptyType)
            {
                uintptr_t party[8] = {};
                const int nParty = PartyCompanionComps(charIdx, party, 8, comp);
                for (int i = 0; i < nParty; ++i)
                {
                    const uintptr_t pEntry = FindEntryByTag(party[i], tag);
                    if (!pEntry) continue;
                    uint16_t pTypeId = 0;
                    if (Read16(pEntry + kOff_InvSlot_TypeId, &pTypeId) && pTypeId == typeId)
                        addCand(party[i]);
                }
            }
            int errDummy = 0;
            // Restore-driven applies must not pop the game's "Item dyed
            // successfully." toast (the batch's success tail raises it) - only
            // the user's own dye action should notify.
            g_suppressDyeToast.store(true, std::memory_order_release);
            for (int i = 0; i < n; ++i)
                CallDyeApply(cands[i], batch, &errDummy);
            g_suppressDyeToast.store(false, std::memory_order_release);
        }
    }

    bool Dye::Install()
    {
        LoadDyeCacheFromFile();

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

        uintptr_t applySlot = mem::FindPattern(kSig_DyeApplySlot);
        if (!applySlot)
            applySlot = mem::FindPattern(kSig_DyeApplySlot_Legacy);
        g_dyeApplySlot = reinterpret_cast<DyeApplySlot_t>(applySlot);

        LOG("dye: DyeVisualSet @ %p, DyeVisualClear @ %p, DyeApplySlot @ %p (companion-safe live renderer).",
            reinterpret_cast<void*>(g_dyeVisualSet), reinterpret_cast<void*>(g_dyeVisualClear),
            reinterpret_cast<void*>(g_dyeApplySlot));

        return true;
    }

    void Dye::Remove()
    {
        mem::RemoveHook(&g_equipTarget);
        mem::RemoveHook(&g_dyeNotifyTarget);
        oEquipBatch = nullptr;
        g_dyeApply  = nullptr;
        g_dyeUpsert = nullptr;
        g_dyeVisualSet = nullptr;
        g_dyeVisualClear = nullptr;
        g_dyeRecRemove = nullptr;
        g_dyeApplySlot = nullptr;
        oDyeNotify = nullptr;
        g_comp.store(0, std::memory_order_release);
    }

    bool Dye::Ready()
    {
        if (s_targetMode == 2)
            return Inventory::ClientHolderAddr() != 0;
        return ClientComp() != 0;
    }

    void Dye::SetActiveCharacter(int index)
    {
        if (index < 0) index = 0;
        s_activeCharIdx = index;
        g_slotCount = 0;
    }

    int Dye::GetActiveCharacter()
    {
        return s_activeCharIdx;
    }

    void Dye::SetTargetMode(int mode)
    {
        s_targetMode = mode;
        g_slotCount = 0;
    }

    int Dye::GetTargetMode()
    {
        return s_targetMode;
    }

    void Dye::SetActiveMount(int index)
    {
        if (index < 0) index = 0;
        s_activeMountIdx = index;
        g_slotCount = 0;
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
        RebuildSnapshot();
        return g_slotCount;
    }

    bool Dye::GetSlot(int idx, SlotInfo* out)
    {
        if (idx < 0 || idx >= g_slotCount) return false;
        *out = g_slots[idx];
        return true;
    }

    bool Dye::GetChannel(uint16_t tag, int channel, Channel* out)
    {
        if (!out) return false;
        if (channel < 0 || channel >= static_cast<int>(kDye_MaxChannels)) return false;

        uintptr_t entry = 0;
        if (s_targetMode == 2)
        {
            for (int i = 0; i < g_slotCount; ++i)
            {
                if (g_slots[i].tag == tag)
                {
                    entry = FindSlotByInstance(Inventory::ClientHolderAddr(), g_slots[i].instanceId);
                    break;
                }
            }
        }
        else
        {
            const uintptr_t comp = ClientComp();
            if (comp) entry = FindEntryByTag(comp, tag);
        }
        if (!entry) return false;

        uint8_t recs[kDye_MaxChannels][16];
        const uint32_t mask = ReadRecords(entry, recs);
        if (!(mask & (1u << channel))) return false;

        const uint8_t* r = recs[channel];
        memcpy(&out->groupKey, r + 0, 4);
        memcpy(&out->materialId, r + 4, 2);
        out->r = r[7]; out->g = r[8]; out->b = r[9];
        out->repair = r[11];
        return true;
    }

    bool Dye::Apply(uint16_t tag, int channel, const Channel& c)
    {
        if (channel < -1 || channel >= static_cast<int>(kDye_MaxChannels)) return false;
        if (g_state.load(std::memory_order_acquire) == static_cast<int>(OpState::Pending))
            return false;

        g_req = Request{ tag, channel, false, c };
        g_state.store(static_cast<int>(OpState::Pending), std::memory_order_release);
        return true;
    }

    bool Dye::Clear(uint16_t tag, int channel)
    {
        if (channel < -1 || channel >= static_cast<int>(kDye_MaxChannels)) return false;
        if (g_state.load(std::memory_order_acquire) == static_cast<int>(OpState::Pending))
            return false;

        g_req = Request{ tag, channel, true, Channel{} };
        g_state.store(static_cast<int>(OpState::Pending), std::memory_order_release);
        return true;
    }

    Dye::OpState Dye::Status()
    {
        const int cur = g_state.load(std::memory_order_acquire);
        if (cur == static_cast<int>(OpState::Done) || cur == static_cast<int>(OpState::DoneDataOnly) || cur == static_cast<int>(OpState::Failed))
            g_state.store(static_cast<int>(OpState::Idle), std::memory_order_release);
        return static_cast<OpState>(cur);
    }

    void Dye::Tick()
    {
        if (!Player::Ready()) return;

        if (g_state.load(std::memory_order_acquire) == static_cast<int>(OpState::Pending))
            ProcessRequest();

        // Continuous Auto-Restore: re-applies custom saved dye profile across transitions
        static ULONGLONG s_lastRestore = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastRestore > 2500)
        {
            s_lastRestore = now;

            __try
            {
                for (int c = 0; c < 3; ++c)
                {
                    const int liveIdx = Inventory::ActivePlayerCharacterIdx();
                    uintptr_t comp = 0;
                    if (c == liveIdx)
                    {
                        const uintptr_t liveChar = Inventory::ClientCharacterAddr();
                        if (liveChar) comp = CompForCharacter(liveChar);
                        if (!comp && c > 0 && c < 3)
                        {
                            const uintptr_t liveActor = Player::GetActor(c);
                            if (liveActor) comp = CompForCharacter(liveActor);
                        }
                    }
                    if (!comp)
                    {
                        const uintptr_t act = Inventory::CharacterAddr(c);
                        if (act) comp = CompForCharacter(act);
                    }
                    if (!comp)
                    {
                        const uintptr_t direct = Player::GetActor(c);
                        if (direct) comp = CompForCharacter(direct);
                    }
                    const uintptr_t profC = (c > 0) ? Player::GetProfileEquipComp(c) : 0;
                    uintptr_t liveComp = 0;
                    if (c > 0 && c < 3)
                    {
                        const uintptr_t directActor = Player::GetActor(c);
                        if (directActor) liveComp = CompForCharacter(directActor);
                    }
                    if (!comp && profC) comp = profC;
                    if (!comp && liveComp) comp = liveComp;
                    if (!comp) continue;

                    for (uint16_t tag = 0; tag < 32; ++tag)
                    {
                        const uintptr_t entry = FindEntryByTag(comp, tag);
                        if (!entry) continue;

                        uint16_t liveTypeId = 0;
                        Read16(entry + kOff_InvSlot_TypeId, &liveTypeId);
                        if (liveTypeId == 0 || liveTypeId == kInvSlot_EmptyType) continue;

                        uint32_t liveDyeCount = 0;
                        Read32(entry + kOff_ItemVal_DyeCount, &liveDyeCount);

                        if (s_savedPlayerSlots[c][tag].active && s_savedPlayerSlots[c][tag].typeId != 0 &&
                            s_savedPlayerSlots[c][tag].typeId != liveTypeId)
                        {
                            SavedItemDyeRecord* customDye = FindSavedItemDye(liveTypeId);
                            if (customDye && customDye->mask > 0)
                            {
                                s_savedPlayerSlots[c][tag].active = true;
                                s_savedPlayerSlots[c][tag].tag = tag;
                                s_savedPlayerSlots[c][tag].typeId = liveTypeId;
                                Read64(entry + kOff_ItemVal_InstanceId, &s_savedPlayerSlots[c][tag].instanceId);
                                s_savedPlayerSlots[c][tag].mask = customDye->mask;
                                s_savedPlayerSlots[c][tag].dyeCount = customDye->dyeCount;
                                memcpy(s_savedPlayerSlots[c][tag].records, customDye->records, sizeof(customDye->records));
                            }
                            else
                            {
                                s_savedPlayerSlots[c][tag].active = false;
                                s_savedPlayerSlots[c][tag].typeId = liveTypeId;
                                s_savedPlayerSlots[c][tag].mask = 0;
                                continue;
                            }
                        }
                        else if (!s_savedPlayerSlots[c][tag].active)
                        {
                            SavedItemDyeRecord* customDye = FindSavedItemDye(liveTypeId);
                            if (customDye && customDye->mask > 0)
                            {
                                s_savedPlayerSlots[c][tag].active = true;
                                s_savedPlayerSlots[c][tag].tag = tag;
                                s_savedPlayerSlots[c][tag].typeId = liveTypeId;
                                Read64(entry + kOff_ItemVal_InstanceId, &s_savedPlayerSlots[c][tag].instanceId);
                                s_savedPlayerSlots[c][tag].mask = customDye->mask;
                                s_savedPlayerSlots[c][tag].dyeCount = customDye->dyeCount;
                                memcpy(s_savedPlayerSlots[c][tag].records, customDye->records, sizeof(customDye->records));
                            }
                        }

                        if (s_savedPlayerSlots[c][tag].active && s_savedPlayerSlots[c][tag].mask > 0 &&
                            (s_savedPlayerSlots[c][tag].typeId == 0 || s_savedPlayerSlots[c][tag].typeId == liveTypeId))
                        {
                            uint8_t liveRecs[kDye_MaxChannels][16];
                            const uint32_t liveMask = ReadRecords(entry, liveRecs);

                            bool needsData = (liveDyeCount == 0);
                            bool needsVisual = false;
                            for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                            {
                                if (!(s_savedPlayerSlots[c][tag].mask & (1u << ch))) continue;
                                if (!(liveMask & (1u << ch)))
                                {
                                    needsData = true;
                                    needsVisual = true;
                                }
                                else if (memcmp(liveRecs[ch], s_savedPlayerSlots[c][tag].records[ch], 16) != 0)
                                {
                                    needsData = true;
                                    needsVisual = true;
                                }
                            }

                            if (!needsVisual && liveDyeCount > 0 &&
                                s_lastEquipChangeMs != 0 &&
                                GetTickCount64() - s_lastEquipChangeMs < 3000)
                            {
                                needsVisual = true;
                            }

                            if (needsData && g_dyeUpsert)
                            {
                                for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                                {
                                    if (s_savedPlayerSlots[c][tag].mask & (1u << ch))
                                    {
                                        CallDyeUpsert(entry, s_savedPlayerSlots[c][tag].records[ch]);
                                        if (profC && profC != comp)
                                        {
                                            const uintptr_t pe = FindEntryByTag(profC, tag);
                                            if (pe) CallDyeUpsert(pe, s_savedPlayerSlots[c][tag].records[ch]);
                                        }
                                        if (liveComp && liveComp != comp)
                                        {
                                            const uintptr_t le = FindEntryByTag(liveComp, tag);
                                            if (le) CallDyeUpsert(le, s_savedPlayerSlots[c][tag].records[ch]);
                                        }
                                    }
                                }

                                // Re-establish the SERVER realm copy too. After a
                                // restart without a game save the server state is
                                // the last save's - without this the game's
                                // server->client reconcile keeps wiping the client
                                // records this loop just re-wrote. Rate-limited so
                                // it cannot fight the reconcile every tick.
                                static ULONGLONG s_lastServerMirror[3][32] = {};
                                const int64_t savedInst = s_savedPlayerSlots[c][tag].instanceId;
                                if (GetTickCount64() - s_lastServerMirror[c][tag] > 60000)
                                {
                                    s_lastServerMirror[c][tag] = GetTickCount64();
                                    MirrorToServer(tag, savedInst,
                                                   s_savedPlayerSlots[c][tag].records,
                                                   s_savedPlayerSlots[c][tag].mask);
                                }
                            }

                            if (needsVisual)
                            {
                                // The game periodically reverts dye records on
                                // live copies, so WITHOUT a rate limit this
                                // batch replays every 2.5s tick forever - and
                                // each replay is real render work on the game
                                // thread whose micro-hitch reads as slow-motion
                                // when it lands mid-vault. Data repair above
                                // stays cheap and silent every tick; the heavy
                                // batch repaint runs at most once a minute per
                                // slot (plus right after an equip change).
                                static ULONGLONG s_lastBatchApply[3][32] = {};
                                const ULONGLONG batchNow = GetTickCount64();
                                const bool afterEquipChange =
                                    (s_lastEquipChangeMs != 0 &&
                                     batchNow - s_lastEquipChangeMs < 3000);
                                if (afterEquipChange ||
                                    batchNow - s_lastBatchApply[c][tag] > 60000)
                                {
                                    s_lastBatchApply[c][tag] = batchNow;
                                    RestoreApplySlot(c, tag, s_savedPlayerSlots[c][tag].typeId,
                                                     s_savedPlayerSlots[c][tag].records,
                                                     s_savedPlayerSlots[c][tag].mask,
                                                     comp, profC, liveComp);
                                }

                                for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                                {
                                    if (s_savedPlayerSlots[c][tag].mask & (1u << ch))
                                    {
                                        const uint8_t* rec = s_savedPlayerSlots[c][tag].records[ch];
                                        // ApplySlot disabled on 2.02 (false signature)
                                        if (comp && entry && IsRenderComp(comp))
                                            CallDyeVisualSet(comp, entry, rec, tag, ch);
                                        if (liveComp && liveComp != comp && IsRenderComp(liveComp))
                                        {
                                            const uintptr_t le = FindEntryByTag(liveComp, tag);
                                            if (le)
                                                CallDyeVisualSet(liveComp, le, rec, tag, ch);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            // 2. Tracked Mounts Auto-Restore
            const int mountCount = Player::GetTrackedMountCount();
            for (int m = 0; m < mountCount; ++m)
            {
                const uintptr_t mComp = FindMountComp(m);
                if (!mComp) continue;

                for (uint16_t tag = 0; tag < 32; ++tag)
                {
                    const uintptr_t entry = FindEntryByTag(mComp, tag);
                    if (!entry) continue;

                    uint16_t liveTypeId = 0;
                    Read16(entry + kOff_InvSlot_TypeId, &liveTypeId);
                    if (liveTypeId == 0 || liveTypeId == kInvSlot_EmptyType) continue;

                    uint32_t liveDyeCount = 0;
                    Read32(entry + kOff_ItemVal_DyeCount, &liveDyeCount);

                    if (s_savedMountSlots[tag].active && s_savedMountSlots[tag].typeId != 0 &&
                        s_savedMountSlots[tag].typeId != liveTypeId)
                    {
                        SavedItemDyeRecord* customDye = FindSavedItemDye(liveTypeId);
                        if (customDye && customDye->mask > 0)
                        {
                            s_savedMountSlots[tag].active = true;
                            s_savedMountSlots[tag].tag = tag;
                            s_savedMountSlots[tag].typeId = liveTypeId;
                            Read64(entry + kOff_ItemVal_InstanceId, &s_savedMountSlots[tag].instanceId);
                            s_savedMountSlots[tag].mask = customDye->mask;
                            s_savedMountSlots[tag].dyeCount = customDye->dyeCount;
                            memcpy(s_savedMountSlots[tag].records, customDye->records, sizeof(customDye->records));
                        }
                        else
                        {
                            s_savedMountSlots[tag].active = false;
                            s_savedMountSlots[tag].typeId = liveTypeId;
                            s_savedMountSlots[tag].mask = 0;
                            continue;
                        }
                    }
                    else if (!s_savedMountSlots[tag].active)
                    {
                        SavedItemDyeRecord* customDye = FindSavedItemDye(liveTypeId);
                        if (customDye && customDye->mask > 0)
                        {
                            s_savedMountSlots[tag].active = true;
                            s_savedMountSlots[tag].tag = tag;
                            s_savedMountSlots[tag].typeId = liveTypeId;
                            Read64(entry + kOff_ItemVal_InstanceId, &s_savedMountSlots[tag].instanceId);
                            s_savedMountSlots[tag].mask = customDye->mask;
                            s_savedMountSlots[tag].dyeCount = customDye->dyeCount;
                            memcpy(s_savedMountSlots[tag].records, customDye->records, sizeof(customDye->records));
                        }
                    }

                    if (s_savedMountSlots[tag].active && s_savedMountSlots[tag].mask > 0 &&
                        (s_savedMountSlots[tag].typeId == 0 || s_savedMountSlots[tag].typeId == liveTypeId))
                    {
                        uint8_t liveRecs[kDye_MaxChannels][16];
                        const uint32_t liveMask = ReadRecords(entry, liveRecs);

                        bool needsData = (liveDyeCount == 0);
                        bool needsVisual = false;
                        for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                        {
                            if (!(s_savedMountSlots[tag].mask & (1u << ch))) continue;
                            if (!(liveMask & (1u << ch)))
                            {
                                needsData = true;
                                needsVisual = true;
                            }
                            else if (memcmp(liveRecs[ch], s_savedMountSlots[tag].records[ch], 16) != 0)
                            {
                                needsData = true;
                                needsVisual = true;
                            }
                        }

                        if (!needsVisual && liveDyeCount > 0 &&
                            s_lastEquipChangeMs != 0 &&
                            GetTickCount64() - s_lastEquipChangeMs < 3000)
                        {
                            needsVisual = true;
                        }

                        if (needsData && g_dyeUpsert)
                        {
                            for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                            {
                                if (s_savedMountSlots[tag].mask & (1u << ch))
                                    CallDyeUpsert(entry, s_savedMountSlots[tag].records[ch]);
                            }
                        }

                        if (needsVisual || liveDyeCount == 0)
                        {
                            for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                            {
                                if (s_savedMountSlots[tag].mask & (1u << ch))
                                {
                                    const uint8_t* rec = s_savedMountSlots[tag].records[ch];
                                    // ApplySlot disabled + VisualSet faults on mount
                                    // comps on 2.02 - records only; render refreshes
                                    // via re-equip/summon replay.
                                    (void)rec;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    bool Dye::InjectAllToSave()
    {
        uint8_t oldFlag = 0;
        const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
        if (!flagAddr) return false;
        if (!RawWrite8(flagAddr, 1)) return false;

        bool ok = false;

        for (int c = 0; c < 3; ++c)
        {
            uintptr_t clientComp = 0;
            uintptr_t serverComp = 0;

            const uintptr_t clientAct = Inventory::CharacterAddr(c);
            if (clientAct) clientComp = CompForCharacter(clientAct);
            if (!clientComp && c == 0 && c == Inventory::ActivePlayerCharacterIdx())
                clientComp = ActiveClientComp();

            if (c == 0 && c == Inventory::ActivePlayerCharacterIdx())
                serverComp = CompForCharacter(Inventory::ServerCharacterAddr());
            if (!serverComp)
            {
                const uintptr_t directAct = Player::GetActor(c);
                if (directAct) serverComp = CompForCharacter(directAct);
            }

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
                        int64_t instId = 0;
                        if (!Read16(cEntry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                        Read16(cEntry + tagOffset, &tag);
                        Read64(cEntry + kOff_ItemVal_InstanceId, &instId);

                        uint8_t recs[kDye_MaxChannels][16];
                        const uint32_t mask = ReadRecords(cEntry, recs);
                        if (mask > 0 && instId > 0)
                        {
                            ok |= MirrorToServer(tag, instId, recs, mask);
                        }
                    }
                }
            }

            for (uint16_t tag = 0; tag < 32; ++tag)
            {
                if (s_savedPlayerSlots[c][tag].active && s_savedPlayerSlots[c][tag].mask > 0)
                {
                    const uintptr_t entry = clientComp ? FindEntryByTag(clientComp, tag) : 0;
                    int64_t instId = 0;
                    if (entry) Read64(entry + kOff_ItemVal_InstanceId, &instId);
                    ok |= MirrorToServer(tag, instId, s_savedPlayerSlots[c][tag].records, s_savedPlayerSlots[c][tag].mask);
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
                    int64_t instId = 0;
                    if (!Read16(mEntry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                    Read16(mEntry + tagOffset, &tag);
                    Read64(mEntry + kOff_ItemVal_InstanceId, &instId);

                    uint8_t recs[kDye_MaxChannels][16];
                    const uint32_t mask = ReadRecords(mEntry, recs);
                    if (mask > 0 && instId > 0)
                    {
                        ok |= MirrorToServer(tag, instId, recs, mask);
                    }
                }
            }
        }

        RawWrite8(flagAddr, oldFlag);
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
