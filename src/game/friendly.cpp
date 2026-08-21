#include "friendly.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <MinHook.h>

#include "offsets.h"
#include "../mem/safe_memory.h"
#include "../mem/hooks.h"
#include "../core/state.h"

namespace trinity::game
{
    using mem::Read16;
    using mem::Read32;
    using mem::Read64;
    using mem::Write64;

    namespace
    {
        // The two leaf trust-record setters. Both take the destination map owner
        // in rcx and the SOURCE 0x58-byte record in rdx, and copy the record into
        // the matching slot. Every trust write - gift, feed, AI, save-load, sync
        // - funnels through one of these (live-confirmed: an NPC trust write
        // broke at 0xDBE114F inside setNpc). See offsets.h (kSig_FriendlySet*).
        using FriendlySet_t = void*(__fastcall*)(void* mapOwner, void* record);
        FriendlySet_t oSetNpc = nullptr;
        FriendlySet_t oSetPet = nullptr;
        void* g_npcTarget = nullptr;
        void* g_petTarget = nullptr;

        // Last trust value we let through, per relationship. The setter writes an
        // absolute value, so we scale the increase over what we last allowed for
        // that (map, group, key) - a stable per-gift/feed multiplier. Keyed so an
        // NPC and a pet, or two different targets, never collide. First sight of
        // a key SEEDS unscaled; because the save-loader drives these same setters
        // at login, every relationship is pre-seeded there, so the first in-game
        // gift is already scaled and a loaded save is never re-scaled. Losses and
        // already-max targets pass through but keep the cache tracking the true
        // value. Self-healing: after a reload the stored value drops below our
        // cache, reads as a "loss", and reseeds.
        std::mutex g_cacheMx;
        std::unordered_map<uint64_t, int64_t> g_lastVal;

        int64_t ScaleGain(int64_t oldVal, int64_t newVal, float mult, uint32_t mapId)
        {
            int64_t delta = newVal - oldVal;
            if (delta <= 0) return newVal;

            // Map 1 is Pet/Mount: Pet feeding/taming is completely safe for large multipliers.
            // Map 0 is NPC: Pearl Abyss server strictly validates all NPC quest/affinity changes.
            // Any single jump > +15 points on an NPC quest reward triggers Error 298648703 (0x11CD047F) and disconnects.
            // We clamp NPC delta to max +10 points to guarantee 100% safety from disconnects.
            double gain = static_cast<double>(delta) * static_cast<double>(mult);
            if (mapId == 0 && gain > 10.0)
            {
                gain = 10.0;
            }

            const double scaled = static_cast<double>(oldVal) + gain;
            if (scaled >= static_cast<double>(kFriendly_Max)) return kFriendly_Max;
            if (scaled <= 0.0) return 0;
            return static_cast<int64_t>(scaled + 0.5);
        }

        // mapId keeps the NPC (0) and pet (1) key spaces apart in the cache.
        void ScaleRecord(void* record, uint32_t mapId)
        {
            const State& st = State::Get();
            if (!st.trustMult || st.trustMultVal <= 1.0f) return;

            const uintptr_t r = reinterpret_cast<uintptr_t>(record);
            if (r < kMinPointer) return;

            uint32_t key = 0;
            uint16_t group = 0;
            int64_t  newVal = 0;
            if (!Read32(r + kOff_FriendlyRec_Key, &key)) return;
            if (!Read16(r + kOff_FriendlyRec_Group, &group)) return;
            if (!Read64(r + kOff_FriendlyRec_Value, &newVal)) return;

            // Ignore obviously-out-of-range values (a wrong offset or an
            // uninitialised insert) so we never write garbage into the record.
            if (newVal < 0 || newVal > kFriendly_Max) return;

            const uint64_t base   = (static_cast<uint64_t>(mapId) << 48) |
                                    (static_cast<uint64_t>(group) << 32);
            const uint64_t ckBase = base;        // key == 0: the persisted baseline
            const uint64_t ckLive = base | key;  // this live relationship

            std::unique_lock<std::mutex> lk(g_cacheMx, std::try_to_lock);
            if (!lk.owns_lock()) return;

            // key == 0 is the save-loader's persistent write - every NPC's stored
            // trust arrives this way at login, one record per group, before any
            // gameplay write. Record it as the group's baseline and NEVER scale
            // it, so a loaded save is never inflated.
            if (key == 0)
            {
                g_lastVal[ckBase] = newVal;
                return;
            }

            // Gameplay write. Old value = this relationship's last value if we've
            // seen it; else the group's persisted baseline
            int64_t oldVal;
            auto itLive = g_lastVal.find(ckLive);
            if (itLive != g_lastVal.end())
            {
                oldVal = itLive->second;
            }
            else
            {
                auto itBase = g_lastVal.find(ckBase);
                oldVal = (itBase != g_lastVal.end()) ? itBase->second : 0;
            }

            if (newVal > oldVal && oldVal < kFriendly_Max)
            {
                const int64_t s = ScaleGain(oldVal, newVal, st.trustMultVal, mapId);
                if (s != newVal && Write64(r + kOff_FriendlyRec_Value, s))
                {
                    g_lastVal[ckLive] = s;
                    return;
                }
            }
            g_lastVal[ckLive] = newVal;
        }

        void* __fastcall hkSetNpc(void* mapOwner, void* record)
        {
            const State& st = State::Get();
            if (st.trustMult && st.trustMultVal > 1.0f)
            {
                __try
                {
                    ScaleRecord(record, 0);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return oSetNpc(mapOwner, record);
        }

        void* __fastcall hkSetPet(void* mapOwner, void* record)
        {
            const State& st = State::Get();
            if (st.trustMult && st.trustMultVal > 1.0f)
            {
                __try
                {
                    ScaleRecord(record, 1);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return oSetPet(mapOwner, record);
        }
    }

    bool Friendly::Install()
    {
        // Non-fatal: only Trust Multiplier is lost if a setter doesn't resolve.
        mem::InstallHook("friendly: NPC trust setter", kSig_FriendlySetNpc,
                         "Trust Multiplier (NPCs) disabled",
                         &hkSetNpc, &oSetNpc, &g_npcTarget);
        mem::InstallHook("friendly: pet trust setter", kSig_FriendlySetPet,
                         "Trust Multiplier (pets/mounts) disabled",
                         &hkSetPet, &oSetPet, &g_petTarget);
        return true;
    }

    void Friendly::Remove()
    {
        mem::RemoveHook(&g_npcTarget);
        mem::RemoveHook(&g_petTarget);
        std::lock_guard<std::mutex> lk(g_cacheMx);
        g_lastVal.clear();
    }

    bool Friendly::Ready()
    {
        return g_npcTarget != nullptr || g_petTarget != nullptr;
    }
}
