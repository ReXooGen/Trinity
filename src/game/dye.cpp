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
        using DyeApplySlot_t  = void* (__fastcall*)(void*, uint16_t, const void*, int);
        using DyeUpsert_t     = void* (__fastcall*)(void*, const void*);
        using EquipRefresh_t  = void* (__fastcall*)(void*, int*);

        EquipBatch_t    oEquipBatch   = nullptr;
        void*           g_equipTarget = nullptr;
        DyeApplyBatch_t g_dyeApply    = nullptr;
        DyeApplySlot_t  g_dyeApplySlot = nullptr;
        DyeUpsert_t     g_dyeUpsert   = nullptr;
        EquipRefresh_t  g_refresh     = nullptr;

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
                "AbyssGear", "Item_Skill", "Riding_Deer_Horn"
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
            return 12; // Full 12 zones supported for all player and mount gear
        }

        // The hook's captured components
        std::atomic<uintptr_t> g_comp{ 0 };
        std::atomic<uintptr_t> g_mountComp{ 0 };

        bool ReadEquipTable(uintptr_t comp, uintptr_t& outArray, uint32_t& outCount,
                            uintptr_t* outStride = nullptr, uintptr_t* outSlotTag = nullptr,
                            uintptr_t* outDyeData = nullptr, uintptr_t* outDyeCount = nullptr)
        {
            if (comp < kMinPointer) return false;

            uintptr_t desc = 0, array = 0;
            uint32_t count = 0;

            // Modern TU 1.17+ (+0x80)
            if (ReadPtr(comp + 0x80, &desc) && desc >= kMinPointer &&
                ReadPtr(desc + kOff_EquipTable_Array, &array) && array >= kMinPointer &&
                Read32(desc + kOff_EquipTable_Count, &count) && count > 0 && count <= 64)
            {
                outArray = array;
                outCount = count;
                if (outStride) *outStride = 0xD0;
                if (outSlotTag) *outSlotTag = 0xC8;
                if (outDyeData) *outDyeData = 0x78;
                if (outDyeCount) *outDyeCount = 0x80;
                return true;
            }

            // Legacy TU 1.14 (+0x88)
            if (ReadPtr(comp + 0x88, &desc) && desc >= kMinPointer &&
                ReadPtr(desc + kOff_EquipTable_Array, &array) && array >= kMinPointer &&
                Read32(desc + kOff_EquipTable_Count, &count) && count > 0 && count <= 64)
            {
                outArray = array;
                outCount = count;
                if (outStride) *outStride = 0xC8;
                if (outSlotTag) *outSlotTag = 0xC0;
                if (outDyeData) *outDyeData = 0x70;
                if (outDyeCount) *outDyeCount = 0x78;
                return true;
            }

            const uintptr_t tableOffsets[] = { 0x50, 0x38, 0x40, 0x48, 0x60, 0x70 };
            for (uintptr_t tOff : tableOffsets)
            {
                if (!ReadPtr(comp + tOff, &desc) || desc < kMinPointer) continue;
                if (ReadPtr(desc + kOff_EquipTable_Array, &array) && array >= kMinPointer &&
                    Read32(desc + kOff_EquipTable_Count, &count) && count > 0 && count <= 64)
                {
                    outArray = array;
                    outCount = count;
                    if (outStride) *outStride = 0xD0;
                    if (outSlotTag) *outSlotTag = 0xC8;
                    if (outDyeData) *outDyeData = 0x78;
                    if (outDyeCount) *outDyeCount = 0x80;
                    return true;
                }
            }
            return false;
        }

        bool CompValid(uintptr_t comp)
        {
            if (comp < kMinPointer) return false;
            uintptr_t array = 0;
            uint32_t  count = 0;
            return ReadEquipTable(comp, array, count);
        }

        bool CompHasHorseGear(uintptr_t comp)
        {
            uintptr_t array = 0;
            uint32_t  count = 0;
            uintptr_t stride = 0xD0;
            if (!ReadEquipTable(comp, array, count, &stride)) return false;

            for (uint32_t i = 0; i < count; ++i)
            {
                const uintptr_t entry = array + static_cast<uintptr_t>(i) * stride;
                uint16_t tid = 0;
                int64_t qty = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType || tid == 0) continue;
                if (!Read64(entry + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;

                char itemName[96] = "";
                char icon[128] = "";
                if (Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                {
                    Inventory::IconForTypeId(tid, icon, sizeof(icon));
                    if (GetHorseSlotType(itemName, icon) != HorseSlotType::None)
                        return true;
                }
            }
            return false;
        }

        uintptr_t FindEquipCompFromActor(uintptr_t actor)
        {
            if (actor < kMinPointer) return 0;

            // 1. Standard character / mount container walk (*(*(actor+0x68)+0x38))
            uintptr_t sub = 0, comp = 0;
            if (ReadPtr(actor + kOff_Container_Sub, &sub) && sub >= kMinPointer &&
                ReadPtr(sub + kOff_Sub_EquipComp, &comp) && CompValid(comp))
            {
                return comp;
            }

            // 2. Alternate sub-container offsets on actor
            const uintptr_t subOffsets[] = { 0x60, 0x70, 0x58, 0x78, 0x80, 0x88 };
            const uintptr_t compOffsets[] = { 0x38, 0x30, 0x40, 0x28, 0x48 };
            for (uintptr_t sOff : subOffsets)
            {
                if (ReadPtr(actor + sOff, &sub) && sub >= kMinPointer)
                {
                    for (uintptr_t cOff : compOffsets)
                    {
                        if (ReadPtr(sub + cOff, &comp) && CompValid(comp))
                            return comp;
                    }
                }
            }

            // 3. Direct component pointer on actor
            const uintptr_t directOffsets[] = { 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90 };
            for (uintptr_t dOff : directOffsets)
            {
                if (ReadPtr(actor + dOff, &comp) && CompValid(comp))
                    return comp;
            }

            return 0;
        }

        // A character's own component, required to point back at that
        // character (comp+0x08 = the owning actor).  That back-reference is
        // what makes this safe: a wrong offset, a freed actor or a component
        // belonging to somebody else resolves to nothing rather than to a
        // plausible wrong object.  FindEquipCompFromActor is too aggressive
        // for player characters and can find false positives that crash the
        // render-path applier.
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
        static int s_targetMode = 0; // 0 = Player Character, 1 = Mount / Horse
        static int s_activeMountIdx = 0;

        uintptr_t FindMountComp(int index)
        {
            if (index < 0 || index >= 4) return 0;

            // 1. Hooked component captured during equip changes
            const uintptr_t hooked = g_mountComp.load(std::memory_order_acquire);
            if (hooked && CompValid(hooked))
            {
                if (index == 0) return hooked;
            }

            // 2. Direct lookup by mount actor index
            const uintptr_t act = Player::GetMountActor(index);
            if (act)
            {
                const uintptr_t comp = FindEquipCompFromActor(act);
                if (comp) return comp;
            }

            // 3. Fallback for Active Mount (index == 0): scan all tracked mount actors
            if (index == 0)
            {
                for (int m = 0; m < 4; ++m)
                {
                    const uintptr_t mAct = Player::GetMountActor(m);
                    if (!mAct) continue;
                    const uintptr_t comp = FindEquipCompFromActor(mAct);
                    if (comp) return comp;
                }
            }

            return 0;
        }

        // The component we render through, and the one every read in this file
        // reports. The walk leads and the hook capture is only a fallback -
        // same doctrine as the inventory holder: a capture cannot be checked
        // for staleness or for which realm it came from, while the walk starts
        // from the character the engine itself repoints on load.
        uintptr_t ClientComp()
        {
            if (s_targetMode == 1)
            {
                const uintptr_t mountComp = FindMountComp(s_activeMountIdx);
                if (mountComp) return mountComp;
                return 0;
            }

            const uintptr_t actor = Inventory::CharacterAddr(s_activeCharIdx);
            if (actor)
            {
                const uintptr_t comp = CompForCharacter(actor);
                if (comp) return comp;
            }
            if (s_activeCharIdx == 0)
            {
                const uintptr_t hooked = g_comp.load(std::memory_order_acquire);
                if (CompValid(hooked)) return hooked;
            }
            return 0;
        }

        // The server-authority component: what a save reload will show. No
        // fallback - a wrong guess here writes another actor's wardrobe.
        uintptr_t ServerComp()
        {
            if (s_targetMode == 1)
            {
                const uintptr_t mountComp = FindMountComp(s_activeMountIdx);
                if (mountComp) return mountComp;
                return 0;
            }

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

        // Read-only 1.17 component-chain diagnostic. Besides reporting the
        // legacy walk, inspect a small pointer-aligned window in the actor's
        // sub-object. A candidate is only reported when its +8 owner points
        // back to the actor, which keeps the scan narrow and self-validating.
        void ReportComponentChain(const char* realm, uintptr_t actor)
        {
            uintptr_t sub = 0, legacyComp = 0, legacyOwner = 0;
            const bool subOk = actor >= kMinPointer &&
                ReadPtr(actor + kOff_Container_Sub, &sub) && sub >= kMinPointer;
            const bool compOk = subOk &&
                ReadPtr(sub + kOff_Sub_EquipComp, &legacyComp) && legacyComp >= kMinPointer;
            const bool ownerOk = compOk &&
                ReadPtr(legacyComp + kOff_EquipComp_Owner, &legacyOwner);

            DyeWatchFile("chain realm=%s actor=%p subOk=%u sub=%p legacyOff=0x%llX compOk=%u comp=%p ownerOk=%u owner=%p valid=%u hooked=%p",
                realm, reinterpret_cast<void*>(actor), subOk ? 1u : 0u,
                reinterpret_cast<void*>(sub),
                static_cast<unsigned long long>(kOff_Sub_EquipComp), compOk ? 1u : 0u,
                reinterpret_cast<void*>(legacyComp), ownerOk ? 1u : 0u,
                reinterpret_cast<void*>(legacyOwner), CompValid(legacyComp) ? 1u : 0u,
                reinterpret_cast<void*>(g_comp.load(std::memory_order_acquire)));

            if (!subOk) return;
            for (uintptr_t subOff = 0; subOff <= 0x100; subOff += sizeof(uintptr_t))
            {
                uintptr_t candidate = 0, owner = 0;
                if (!ReadPtr(sub + subOff, &candidate) || candidate < kMinPointer) continue;
                if (!ReadPtr(candidate + kOff_EquipComp_Owner, &owner) || owner != actor) continue;

                bool foundTable = false;
                for (uintptr_t tableOff = 0x70; tableOff <= 0xA0; tableOff += sizeof(uintptr_t))
                {
                    uintptr_t desc = 0, array = 0;
                    uint32_t count = 0;
                    if (!ReadPtr(candidate + tableOff, &desc) || desc < kMinPointer) continue;
                    if (!ReadPtr(desc + kOff_EquipTable_Array, &array) || array < kMinPointer) continue;
                    if (!Read32(desc + kOff_EquipTable_Count, &count) || count == 0 || count > 64) continue;
                    DyeWatchFile("candidate realm=%s subOff=0x%llX comp=%p owner=%p tableOff=0x%llX desc=%p array=%p count=%u",
                        realm, static_cast<unsigned long long>(subOff),
                        reinterpret_cast<void*>(candidate), reinterpret_cast<void*>(owner),
                        static_cast<unsigned long long>(tableOff), reinterpret_cast<void*>(desc),
                        reinterpret_cast<void*>(array), count);
                    foundTable = true;
                }
                if (!foundTable)
                    DyeWatchFile("candidate realm=%s subOff=0x%llX comp=%p owner=%p table=not-found",
                        realm, static_cast<unsigned long long>(subOff),
                        reinterpret_cast<void*>(candidate), reinterpret_cast<void*>(owner));
            }
        }

        void* __fastcall hkEquipBatch(void* a1, void* a2, void* a3, void* a4)
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
                }
            }
            return oEquipBatch(a1, a2, a3, a4);
        }

        // --- Equipped-entry access (guarded reads) ------------------------
        // entry = the TrItemValue copy the component keeps per equipped slot.
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
                int64_t  qty = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType) return 0;
                if (!Read64(entry + kOff_InvSlot_Quantity, &qty) || qty <= 0) return 0;
                return entry;
            }
            return 0;
        }

        // Read an item value's dye records (up to 12) into `out`, one slot per
        // channel index. Returns a bitmask of channels present.
        uint32_t ReadRecords(uintptr_t itemVal, uint8_t out[kDye_MaxChannels][16])
        {
            memset(out, 0, kDye_MaxChannels * 16);
            uintptr_t data = 0;
            uint32_t  count = 0;

            // Modern TU 1.17+ (+0x78 data, +0x80 count)
            if (ReadPtr(itemVal + 0x78, &data) && data >= kMinPointer &&
                Read32(itemVal + 0x80, &count) && count > 0)
            {
                // valid
            }
            // Legacy TU 1.14 (+0x70 data, +0x78 count)
            else if (ReadPtr(itemVal + 0x70, &data) && data >= kMinPointer &&
                     Read32(itemVal + 0x78, &count) && count > 0)
            {
                // valid
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

        // --- Record builders ----------------------------------------------
        // Shape mirrors the engine's natural records byte for byte (see the
        // record map in offsets.h). +13 = 0x04 on channels 0/3 matches what
        // natural captures show.
        void BuildSetRecord(uint8_t out[16], int channel, const Dye::Channel& c)
        {
            memset(out, 0, 16);
            memcpy(out + 0, &c.groupKey, 4);
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

        // The applier's own "remove this channel" shape: RGB and +10/+12 zero,
        // material 0xFFFF, repair 0xFF (high bit = sentinel). It deletes the
        // record and clears the rendered override for the channel.
        void BuildClearRecord(uint8_t out[16], int channel)
        {
            memset(out, 0, 16);
            out[4] = 0xFF; out[5] = 0xFF; // material 0xFFFF
            out[6]  = static_cast<uint8_t>(channel);
            out[11] = 0xFF;
        }

        // --- SEH wrappers around engine calls (POD locals only) -----------
        bool CallDyeApply(uintptr_t comp, void* batch, int* outErr)
        {
            __try
            {
                g_dyeApply(reinterpret_cast<void*>(comp), outErr, batch);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool CallDyeUpsert(uintptr_t itemVal, const uint8_t rec[16])
        {
            __try
            {
                g_dyeUpsert(reinterpret_cast<void*>(itemVal), rec);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Raw (floorless) byte access for the TLS realm flag - it lives far
        // below kMinPointer, same rationale as the inventory add path.
        bool RawWrite8(uintptr_t addr, uint8_t val)
        {
            if (!addr) return false;
            __try { *reinterpret_cast<volatile uint8_t*>(addr) = val; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        uintptr_t FindSlotByInstance(uintptr_t holder, int64_t targetInstId);

        // --- The server-authority mirror -------------------------------------
        // Write the post-apply records onto the SERVER realm's copy of the same
        // equipped item, which is the copy a save reload reads back. Data only:
        // the applier (sub_7D9C50) is a render path and has no business running
        // against a server actor, and the upsert primitive is all the durable
        // side needs.
        //
        // Count is reset first so cleared channels disappear too; the upserts
        // then rebuild the exact state (reusing the vector's existing capacity,
        // growing - realm-correctly - only if the item never had this many
        // records). The realm flip is for that growth, exactly as in the
        // add-item path, and is always restored.
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

            // 1. Write to server-authority equip component
            const uintptr_t comp = ServerComp();
            if (comp)
            {
                const uintptr_t entry = FindEntryByTag(comp, tag);
                if (entry)
                {
                    int64_t serverId = 0;
                    if (Read64(entry + kOff_ItemVal_InstanceId, &serverId) && serverId == instId)
                    {
                        Write32(entry + kOff_ItemVal_DyeCount, 0);
                        for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                            if (mask & (1u << ch))
                                ok |= CallDyeUpsert(entry, recs[ch]);
                    }
                }
            }

            // 2. Also write to server inventory holder if slot exists there
            if (instId > 0)
            {
                const uintptr_t serverSlot = FindSlotByInstance(Inventory::ServerHolderAddr(), instId);
                if (serverSlot)
                {
                    Write32(serverSlot + kOff_ItemVal_DyeCount, 0);
                    for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                        if (mask & (1u << ch))
                            ok |= CallDyeUpsert(serverSlot, recs[ch]);
                }
            }

            RawWrite8(flagAddr, oldFlag); // never leave a game thread realm-flipped
            return ok;
        }

        // --- The queued request --------------------------------------------
        struct Request
        {
            uint16_t     tag     = 0;
            int          channel = -1;   // -1 = all 12
            bool         clear   = false;
            Dye::Channel value{};
        };
        Request          g_req;
        std::atomic<int> g_state{ static_cast<int>(Dye::OpState::Idle) };

        // Mount dye auto-restore profile across save/load & summon
        struct SavedMountSlot
        {
            bool     active = false;
            uint16_t tag = 0;
            uint32_t dyeCount = 0;
            uint8_t  records[kDye_MaxChannels][16] = {};
            uint32_t mask = 0;
        };
        static SavedMountSlot s_savedMountSlots[32];

        // The full 22-tag slot taxonomy (read out of the engine's own slot
        // dispatch; tags 3/4/5/6/16 re-confirmed live in this
        // build). 14 is an experimental engine slot with no user-facing
        // identity - it and anything new render as "Slot N", with the item
        // name doing the real talking.
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
            case 21: return "Rocket"; // Oongka's launcher
            case 22: return "Chamfron";
            case 23: return "Horse Armor";
            case 24: return "Stirrups";
            case 25: return "Horseshoes";
            default: return nullptr;
            }
        }

        // --- Dyeability -----------------------------------------------------
        // The game's own dyehouse only offers items whose part prefab is in
        // the partprefabdyeslotinfo registry - everything else has no dye
        // channels, so applying records changes nothing visually. dye_data.h
        // carries that registry as sorted hashes of the prefab names, and an
        // item's icon sprite name embeds exactly that prefab
        // ("ItemIcon_Prefab_cd_phm_02_sword_0039").

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
            // No prefab-shaped icon name = cannot classify = keep the item
            // visible. Hiding something we merely failed to parse would be
            // worse than showing a piece the dye cannot touch.
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

            // Exact name first, then progressively drop trailing "_xxx"
            // tokens: icon sprites sometimes carry variant suffixes the
            // registry entry does not.
            size_t len = strlen(p);
            for (int strip = 0; strip < 4 && len > 3; ++strip)
            {
                if (DyeRegistryHas(HashPrefabLower(p, len)))
                    return true;
                size_t cut = len;
                while (cut > 0 && p[cut - 1] != '_') --cut;
                if (cut == 0) break;
                len = cut - 1; // drop the '_' as well
            }
            return false;
        }

        // Menu-side snapshot of the equipped slots.
        constexpr int    kMaxSlots = 64;
        Dye::SlotInfo    g_slots[kMaxSlots];
        int              g_slotCount = 0;

        // Helper to locate a TrItemValue slot by instance ID in an inventory holder
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
                // First try live EquipComponent for mount
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
                            Read16(entry + tagOffset, &tag);
                            Read64(entry + kOff_ItemVal_InstanceId, &inst);

                            char itemName[96] = "";
                            char icon[128] = "";
                            if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                                snprintf(itemName, sizeof(itemName), "Item #%u", tid);
                            Inventory::IconForTypeId(tid, icon, sizeof(icon));

                            const HorseSlotType slotType = GetHorseSlotType(itemName, icon);
                            const char* sName = (slotType != HorseSlotType::None) ? MountSlotName(slotType) : SlotNameForTag(tag);

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
                        Read64(slot + kOff_ItemVal_InstanceId, &inst);

                        char itemName[96] = "";
                        char icon[128] = "";
                        if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                            snprintf(itemName, sizeof(itemName), "Item #%u", tid);
                        Inventory::IconForTypeId(tid, icon, sizeof(icon));

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
                int64_t  qty = 0, inst = 0;
                if (!Read16(entry + kOff_InvSlot_TypeId, &tid) || tid == kInvSlot_EmptyType) continue;
                if (!Read64(entry + kOff_InvSlot_Quantity, &qty) || qty <= 0) continue;
                if (!Read16(entry + tagOffset, &tag)) continue;
                Read64(entry + kOff_ItemVal_InstanceId, &inst);

                if (tag == 14 || tag == 22 || tag == 23 || tag == 24 || tag == 25)
                    continue;

                char itemName[96] = "";
                char icon[128] = "";
                if (!Inventory::NameForTypeId(tid, itemName, sizeof(itemName)))
                    snprintf(itemName, sizeof(itemName), "Item #%u", tid);
                Inventory::IconForTypeId(tid, icon, sizeof(icon));

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
                s.dyeable = IconPrefabDyeable(s.icon);
            }
        }

        // --- The game-thread apply -----------------------------------------
        // --- The game-thread apply -----------------------------------------
        // Mount mode: data-only via g_dyeUpsert (render-path functions crash
        // on mount equip components whose internal layout differs from player
        // components).  Visual update requires re-equip or area reload.
        //
        // Player mode: original proven approach using the engine's own batch
        // apply function (g_dyeApply) which upserts records AND live-updates
        // the rendered materials in one call.
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

                // Write to server holder with TLS Realm Flag flipped so it's 100% durable in save files!
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

                Inventory::ForceRefresh();

                g_state.store(static_cast<int>((clientOk || serverOk) ? Dye::OpState::Done : Dye::OpState::Failed),
                              std::memory_order_release);
                return;
            }

            // ===== MOUNT MODE: data-only via upsert =========================
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

                int maxZones = 2;
                for (int i = 0; i < g_slotCount; ++i)
                {
                    if (g_slots[i].tag == req.tag)
                    {
                        maxZones = g_slots[i].maxZones;
                        break;
                    }
                }

                const int chFirst = (req.channel < 0) ? 0 : req.channel;
                const int chLast  = (req.channel < 0) ? 11 : req.channel;
                bool upsertOk = false;

                for (int ch = chFirst; ch <= chLast; ++ch)
                {
                    uint8_t rec[16] = {};
                    if (req.clear) BuildClearRecord(rec, ch);
                    else           BuildSetRecord(rec, ch, req.value);
                    if (g_dyeUpsert) upsertOk |= CallDyeUpsert(entry, rec);
                }

                // Mirror to server realm for permanent save persistence across save & load
                bool durableOk = false;
                if (instId > 0)
                {
                    uint8_t recs[kDye_MaxChannels][16];
                    const uint32_t mask = ReadRecords(entry, recs);
                    durableOk = MirrorToServer(req.tag, instId, recs, mask);
                }

                // Multi-Actor Server Sync: write to all tracked mount actors in CharMgr with RealmFlag = 1
                uint8_t oldFlag = 0;
                const uintptr_t flagAddr = Inventory::RealmFlagAddress(&oldFlag);
                if (flagAddr && RawWrite8(flagAddr, 1))
                {
                    const int mountCount = Player::GetTrackedMountCount();
                    for (int m = 0; m < mountCount; ++m)
                    {
                        const uintptr_t mAct = Player::GetMountActor(m);
                        if (!mAct) continue;
                        const uintptr_t mComp = FindEquipCompFromActor(mAct);
                        if (!mComp || mComp == comp) continue;
                        const uintptr_t mEntry = FindEntryByTag(mComp, req.tag);
                        if (mEntry)
                        {
                            Write32(mEntry + kOff_ItemVal_DyeCount, 0);
                            for (int ch = chFirst; ch <= chLast; ++ch)
                            {
                                uint8_t rec[16] = {};
                                if (req.clear) BuildClearRecord(rec, ch);
                                else           BuildSetRecord(rec, ch, req.value);
                                CallDyeUpsert(mEntry, rec);
                            }
                        }
                    }
                    RawWrite8(flagAddr, oldFlag);
                }

                // Cache for auto-restore across save/load and summon
                if (req.tag < 32)
                {
                    if (req.clear)
                    {
                        s_savedMountSlots[req.tag].active = false;
                    }
                    else
                    {
                        s_savedMountSlots[req.tag].active = true;
                        s_savedMountSlots[req.tag].tag = req.tag;
                        s_savedMountSlots[req.tag].mask = ReadRecords(entry, s_savedMountSlots[req.tag].records);
                        Read32(entry + kOff_ItemVal_DyeCount, &s_savedMountSlots[req.tag].dyeCount);
                    }
                }

                bool slotApplyOk = false;
                bool batchApplyOk = false;
                bool refreshOk = false;

                // 2. Batch applier
                if (comp && g_dyeApply)
                {
                    static uint8_t mountBatch[kDyeBatch_Size];
                    memset(mountBatch, 0, sizeof(mountBatch));
                    for (size_t blk = 0; blk < kDyeBatch_Blocks; ++blk)
                    {
                        uint8_t* block = mountBatch + blk * kDyeBatch_BlockSize;
                        const uint16_t tag = (blk == 0) ? req.tag : 0xFFFF;
                        memcpy(block, &tag, 2);
                        for (uint32_t r = 0; r < kDye_MaxChannels; ++r)
                            block[kDyeBatch_RecordsOff + r * 16 + 6] = 0xFF;
                    }
                    for (int ch = chFirst; ch <= chLast; ++ch)
                    {
                        uint8_t* rec = mountBatch + kDyeBatch_RecordsOff + static_cast<size_t>(ch) * 16;
                        if (req.clear) BuildClearRecord(rec, ch);
                        else           BuildSetRecord(rec, ch, req.value);
                    }
                    int mountErr = 0;
                    __try {
                        g_dyeApply(reinterpret_cast<void*>(comp), &mountErr, mountBatch);
                        batchApplyOk = (mountErr == 0);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // 3. Force mesh/material refresh
                if (comp && g_refresh)
                {
                    __try {
                        int refreshErr = 0;
                        g_refresh(reinterpret_cast<void*>(comp), &refreshErr);
                        refreshOk = true;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                DyeWatchFile("ProcessRequest: mount tag=%u comp=%p entry=%p instId=%lld upsertOk=%d durableOk=%d slotApply=%d batchApply=%d refresh=%d",
                    req.tag, reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry),
                    static_cast<long long>(instId), upsertOk ? 1 : 0, durableOk ? 1 : 0,
                    slotApplyOk ? 1 : 0, batchApplyOk ? 1 : 0, refreshOk ? 1 : 0);

                g_state.store(static_cast<int>((upsertOk || durableOk || slotApplyOk || batchApplyOk) ? Dye::OpState::Done : Dye::OpState::Failed),
                              std::memory_order_release);
                return;
            }

            // ===== PLAYER CHARACTER MODE: original proven approach ===========
            const uintptr_t comp = ClientComp();
            if (!comp || !g_dyeApply)
            {
                LOG_WARN("dye: apply refused - equip component not resolved / applier unresolved.");
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

            // Build the batch: block 0 targets our slot, the other 9 blocks
            // are disabled (tag 0xFFFF), every untouched record slot is
            // skipped (channel byte 0xFF - the applier only processes records
            // whose channel byte has the high bit clear).
            static uint8_t batch[kDyeBatch_Size]; // game-thread only; static keeps the frame small
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

            int err = 0;
            if (!CallDyeApply(comp, batch, &err) || err != 0)
            {
                LOG_WARN("dye: applier refused (err=%d, slot tag %u).", err, req.tag);
                g_state.store(static_cast<int>(Dye::OpState::Failed), std::memory_order_release);
                return;
            }

            DyeWatchFile("ProcessRequest: player tag=%u comp=%p err=%d applyOk=1",
                req.tag, reinterpret_cast<void*>(comp), err);

            // Mirror the post-apply state (the client entry is the source of
            // truth now - the applier upserted/removed our channels there)
            // onto the server realm's copy, so it persists.
            // Re-find the entry first: the applier may have shuffled the table.
            entry = FindEntryByTag(comp, req.tag);
            int64_t instId = 0;
            if (entry) Read64(entry + kOff_ItemVal_InstanceId, &instId);
            if (entry && instId > 0)
            {
                uint8_t recs[kDye_MaxChannels][16];
                const uint32_t mask = ReadRecords(entry, recs);
                MirrorToServer(req.tag, instId, recs, mask);
            }

            g_state.store(static_cast<int>(Dye::OpState::Done), std::memory_order_release);
        }
    }

    bool Dye::Install()
    {
        // Optional gear-change listener (dye apply/upsert works directly regardless)
        if (!mem::InstallHook("dye: equip-batch", kSig_EquipBatch, nullptr,
                              &hkEquipBatch, &oEquipBatch, &g_equipTarget))
        {
            mem::InstallHook("dye: equip-batch legacy", kSig_EquipBatch_Legacy, nullptr,
                             &hkEquipBatch, &oEquipBatch, &g_equipTarget);
        }

        uintptr_t apply = mem::FindPattern(kSig_DyeApplyBatch);
        if (!apply)
            apply = mem::FindPattern(kSig_DyeApplyBatch_Legacy);
        if (apply)
            g_dyeApply = reinterpret_cast<DyeApplyBatch_t>(apply);

        const uintptr_t slotApply = mem::FindPattern(kSig_DyeApplySlot);
        if (slotApply)
            g_dyeApplySlot = reinterpret_cast<DyeApplySlot_t>(slotApply);

        uintptr_t upsert = mem::FindPattern(kSig_DyeUpsert);
        if (!upsert)
            upsert = mem::FindPattern(kSig_DyeUpsert_Legacy);

        if (!upsert)
            LOG_WARN("dye: upsert signature not found - dye will apply but not persist.");
        else
        {
            LOG("dye: batch apply @ %p, slot apply @ %p, durable upsert @ %p.",
                reinterpret_cast<void*>(apply), reinterpret_cast<void*>(slotApply), reinterpret_cast<void*>(upsert));
        }
        g_dyeUpsert = reinterpret_cast<DyeUpsert_t>(upsert);

        const uintptr_t refresh = mem::FindPattern(kSig_EquipEffectRefresh);
        if (refresh)
            g_refresh = reinterpret_cast<EquipRefresh_t>(refresh);

        return true;
    }

    void Dye::Remove()
    {
        mem::RemoveHook(&g_equipTarget);
        oEquipBatch = nullptr;
        g_dyeApply  = nullptr;
        g_dyeUpsert = nullptr;
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
            return false; // one at a time

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

    // Read-and-clear: a Done/Failed is reported once (for the toast) and the
    // state returns to Idle so the next request is accepted.
    Dye::OpState Dye::Status()
    {
        const int cur = g_state.load(std::memory_order_acquire);
        if (cur == static_cast<int>(OpState::Done) || cur == static_cast<int>(OpState::Failed))
            g_state.store(static_cast<int>(OpState::Idle), std::memory_order_release);
        return static_cast<OpState>(cur);
    }

    void Dye::Tick()
    {
        // One-time raw field map for entries that claim existing dye records.
        // This locates the 1.17 vector pointer without interpreting or writing.
        static bool vectorDumped = true; // diagnostic completed in 0.13.39
        if (!vectorDumped)
        {
            const uintptr_t dumpComp = ClientComp();
            uintptr_t dumpDesc = 0, dumpArray = 0;
            uint32_t dumpCount = 0;
            if (dumpComp && ReadPtr(dumpComp + kOff_EquipComp_Table, &dumpDesc) &&
                ReadPtr(dumpDesc + kOff_EquipTable_Array, &dumpArray) &&
                Read32(dumpDesc + kOff_EquipTable_Count, &dumpCount) && dumpCount <= 64)
            {
                for (uint32_t i = 0; i < dumpCount; ++i)
                {
                    const uintptr_t e = dumpArray + static_cast<uintptr_t>(i) * kEquipEntry_Stride;
                    uint16_t tag = 0, type = 0;
                    Read16(e + kOff_EquipEntry_SlotTag, &tag);
                    if (tag != 16) continue;
                    uint32_t oldCount = 0;
                    Read32(e + kOff_ItemVal_DyeCount, &oldCount);
                    Read16(e + kOff_InvSlot_TypeId, &type);
                    DyeWatchFile("vector-map begin entry=%p index=%u tag=%u type=%u oldCount=%u",
                        reinterpret_cast<void*>(e), i, tag, type, oldCount);
                    for (uintptr_t off = 0x40; off <= 0xC8; off += 8)
                    {
                        uintptr_t q = 0;
                        uint32_t lo = 0, hi = 0;
                        ReadPtr(e + off, &q); Read32(e + off, &lo); Read32(e + off + 4, &hi);
                        DyeWatchFile("vector-map off=0x%02llX q=%p lo=%u hi=%u",
                            static_cast<unsigned long long>(off), reinterpret_cast<void*>(q), lo, hi);
                    }
                    vectorDumped = true;
                    break;
                }
            }
        }

        static ULONGLONG lastChain = 0;
        const ULONGLONG chainNow = GetTickCount64();
        if (false && chainNow - lastChain >= 2000)
        {
            lastChain = chainNow;
            ReportComponentChain("client", Inventory::ClientCharacterAddr());
            ReportComponentChain("server", Inventory::ServerCharacterAddr());
        }

        // Read-only dye watch. Log equipped entry/vector addresses and any
        // record-vector change made by the game's normal dye interface.
        static ULONGLONG last = 0;
        static uintptr_t oldData[64]{};
        static uint32_t oldCount[64]{};
        static uint64_t oldHash[64]{};
        static bool seen[64]{};
        const ULONGLONG now = GetTickCount64();
        if (false && now - last >= 500)
        {
            last = now;
            const uintptr_t comp = ClientComp();
            uintptr_t desc = 0, array = 0;
            uint32_t count = 0;
            if (comp && ReadPtr(comp + kOff_EquipComp_Table, &desc) &&
                ReadPtr(desc + kOff_EquipTable_Array, &array) &&
                Read32(desc + kOff_EquipTable_Count, &count) && count <= 64)
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    const uintptr_t entry = array + static_cast<uintptr_t>(i) * kEquipEntry_Stride;
                    uint16_t tag = 0, tid = 0;
                    uintptr_t data = 0;
                    uint32_t dyeCount = 0;
                    Read16(entry + kOff_EquipEntry_SlotTag, &tag);
                    Read16(entry + kOff_InvSlot_TypeId, &tid);
                    ReadPtr(entry + kOff_ItemVal_DyeData, &data);
                    Read32(entry + kOff_ItemVal_DyeCount, &dyeCount);
                    if (dyeCount > kDye_MaxChannels) dyeCount = kDye_MaxChannels;
                    uint64_t hash = 1469598103934665603ull;
                    if (data >= kMinPointer)
                        for (uint32_t b = 0; b < dyeCount * 16; ++b)
                        {
                            uint8_t v = 0;
                            if (!Read8(data + b, &v)) break;
                            hash = (hash ^ v) * 1099511628211ull;
                        }
                    if (!seen[i] || oldData[i] != data || oldCount[i] != dyeCount || oldHash[i] != hash)
                    {
                        LOG("dye-watch: comp=%p entry=%p index=%u tag=%u type=%u data=%p count=%u hash=%016llX countAddr=%p.",
                            reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry), i,
                            tag, tid, reinterpret_cast<void*>(data), dyeCount,
                            static_cast<unsigned long long>(hash),
                            reinterpret_cast<void*>(entry + kOff_ItemVal_DyeCount));
                        DyeWatchFile("comp=%p entry=%p index=%u tag=%u type=%u data=%p count=%u hash=%016llX countAddr=%p",
                            reinterpret_cast<void*>(comp), reinterpret_cast<void*>(entry), i,
                            tag, tid, reinterpret_cast<void*>(data), dyeCount,
                            static_cast<unsigned long long>(hash),
                            reinterpret_cast<void*>(entry + kOff_ItemVal_DyeCount));
                        seen[i] = true;
                        oldData[i] = data;
                        oldCount[i] = dyeCount;
                        oldHash[i] = hash;
                    }
                }
            }
        }
        // Auto-restore saved mount dye across save/load and summon
        static ULONGLONG s_lastMountRestore = 0;
        const ULONGLONG nowRestore = GetTickCount64();
        if (nowRestore - s_lastMountRestore >= 1000)
        {
            s_lastMountRestore = nowRestore;
            const uintptr_t mComp = FindMountComp(0);
            if (mComp && g_dyeUpsert)
            {
                for (uint16_t tag = 0; tag < 32; ++tag)
                {
                    if (!s_savedMountSlots[tag].active) continue;
                    const uintptr_t mEntry = FindEntryByTag(mComp, tag);
                    if (!mEntry) continue;
                    uint32_t liveCount = 0;
                    Read32(mEntry + kOff_ItemVal_DyeCount, &liveCount);
                    if (liveCount == 0) // needs auto-restore!
                    {
                        for (int ch = 0; ch < static_cast<int>(kDye_MaxChannels); ++ch)
                        {
                            if (s_savedMountSlots[tag].mask & (1u << ch))
                            {
                                CallDyeUpsert(mEntry, s_savedMountSlots[tag].records[ch]);
                            }
                        }
                    }
                }
            }
        }

        if (g_state.load(std::memory_order_acquire) == static_cast<int>(OpState::Pending))
            ProcessRequest();
    }
}
