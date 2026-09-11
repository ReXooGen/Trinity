#include "friendly.h"
#include "friendly_logic.h"

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <windows.h>
#include <MinHook.h>

#include "offsets.h"
#include "player.h"
#include "../mem/safe_memory.h"
#include "../mem/scanner.h"
#include "../core/logger.h"
#include "../core/state.h"

namespace trinity::game
{
    using mem::Read16;
    using mem::Read32;
    using mem::Read64;
    using mem::Write64;

    namespace
    {
        using FriendlySet_t = void*(__fastcall*)(void* mapOwner, void* record);
        FriendlySet_t oSetNpc = nullptr;
        FriendlySet_t oSetPet = nullptr;
        using FriendlyGet_t = void*(__fastcall*)(void* mapOwner, void* actor);
        FriendlyGet_t oGetNpc = nullptr;
        FriendlyGet_t oGetPet = nullptr;
        void* g_npcTarget = nullptr;
        void* g_petTarget = nullptr;
        void* g_npcGetTarget = nullptr;
        void* g_petGetTarget = nullptr;

        bool g_hooksInstalled = false;
        bool g_hooksEnabled = false;
        bool g_featureReportedEnabled = false;

        // Keep observing while the option is off so load/update calls establish
        // an unscaled baseline before the first multiplied gameplay gain.
        std::mutex s_trustMutex;
        std::unordered_map<uint64_t, int64_t> s_lastTrustMap;

        uint64_t TrustCacheKey(void* mapOwner, uint16_t group, uint32_t key)
        {
            const uint64_t owner = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mapOwner));
            return (owner >> 4) ^ (static_cast<uint64_t>(group) << 48) ^ key;
        }

        // The setter receives the replacement record before copying it into
        // the map. Recover the old value from that map so the very first event
        // after enabling the option has a real baseline instead of being
        // mistaken for one. TU 2.01's setter at 0x141E2AD40 uses exactly this
        // layout: 0x100-byte hash buckets at +0x48 and node pointers at +0x50;
        // each node owns a 0x68-stride relationship-record vector at +0x08.
        bool FindStoredTrust(void* mapOwner, uint16_t group, uint32_t key,
                             int64_t* outValue)
        {
            const uintptr_t owner = reinterpret_cast<uintptr_t>(mapOwner);
            if (owner < kMinPointer || !outValue) return false;

            uint32_t bucketCount = 0;
            uintptr_t buckets = 0, nodes = 0;
            if (!Read32(owner + 0x3C, &bucketCount) || bucketCount == 0 || bucketCount > 4096)
                return false;
            if (!mem::ReadPtr(owner + 0x48, &buckets) || buckets < kMinPointer)
                return false;
            if (!mem::ReadPtr(owner + 0x50, &nodes) || nodes < kMinPointer)
                return false;

            for (uint32_t bucketIndex = 0; bucketIndex < bucketCount; ++bucketIndex)
            {
                const uintptr_t bucket = buckets + static_cast<uintptr_t>(bucketIndex) * 0x100;
                uint32_t entryCount = 0;
                if (!Read32(bucket, &entryCount) || entryCount > 31) continue;

                for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
                {
                    uint32_t nodeIndex = 0;
                    if (!Read32(bucket + 0x0C + static_cast<uintptr_t>(entryIndex) * 8,
                                &nodeIndex) || nodeIndex > 0xFFFF)
                        continue;

                    uintptr_t node = 0, records = 0;
                    uint32_t recordCount = 0;
                    if (!mem::ReadPtr(nodes + static_cast<uintptr_t>(nodeIndex) * 8, &node) ||
                        node < kMinPointer)
                        continue;
                    if (!mem::ReadPtr(node + 0x08, &records) || records < kMinPointer)
                        continue;
                    if (!Read32(node + 0x10, &recordCount) || recordCount > 4096)
                        continue;

                    for (uint32_t i = 0; i < recordCount; ++i)
                    {
                        const uintptr_t existing = records + static_cast<uintptr_t>(i) * 0x68;
                        uint32_t existingKey = 0;
                        uint16_t existingGroup = 0;
                        uint64_t existingValue = 0;
                        if (Read32(existing + kOff_FriendlyRec_Key, &existingKey) &&
                            Read16(existing + kOff_FriendlyRec_Group, &existingGroup) &&
                            existingKey == key && existingGroup == group &&
                            Read64(existing + kOff_FriendlyRec_Value, &existingValue) &&
                            static_cast<int64_t>(existingValue) >= 0 &&
                            static_cast<int64_t>(existingValue) <= kFriendly_Max)
                        {
                            *outValue = static_cast<int64_t>(existingValue);
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        void ObserveAndScaleTrust(void* mapOwner, void* record)
        {
            const uintptr_t r = reinterpret_cast<uintptr_t>(record);
            if (r < kMinPointer) return;

            uint32_t key = 0;
            uint16_t group = 0;
            uint64_t rawValue = 0;
            if (!Read32(r + kOff_FriendlyRec_Key, &key) || key == 0) return;
            if (!Read16(r + kOff_FriendlyRec_Group, &group)) return;
            if (!Read64(r + kOff_FriendlyRec_Value, &rawValue)) return;

            const int64_t incoming = static_cast<int64_t>(rawValue);
            if (incoming < 0 || incoming > kFriendly_Max) return;

            const uint64_t cacheKey = TrustCacheKey(mapOwner, group, key);
            const State& st = State::Get();
            const bool scale = Player::Ready() && st.trustMult && st.trustMultVal > 1.0f;

            std::lock_guard<std::mutex> lock(s_trustMutex);
            const auto found = s_lastTrustMap.find(cacheKey);
            int64_t storedValue = 0;
            const bool hasStoredValue = FindStoredTrust(
                mapOwner, group, key, &storedValue);
            int64_t oldValue = 0;
            const bool hasOldValue = SelectTrustBaseline(
                storedValue, hasStoredValue,
                found != s_lastTrustMap.end() ? found->second : 0,
                found != s_lastTrustMap.end(), &oldValue);

            const int64_t newValue = ScaleTrustValue(
                oldValue, hasOldValue, incoming, st.trustMultVal, scale);
            s_lastTrustMap[cacheKey] = newValue;
            if (newValue == incoming) return;

            Write64(r + kOff_FriendlyRec_Value, static_cast<uint64_t>(newValue));
        }

        // TU 2.01 can mutate a record returned by the lookup directly, without
        // passing a replacement record through either setter. Observe those
        // records as a secondary path. The first read only seeds a baseline;
        // this deliberately avoids turning pre-existing trust into a gain.
        void ObserveLiveTrust(void* mapOwner, void* record)
        {
            const uintptr_t r = reinterpret_cast<uintptr_t>(record);
            if (r < kMinPointer) return;

            uint32_t key = 0;
            uint16_t group = 0;
            uint64_t rawValue = 0;
            if (!Read32(r + kOff_FriendlyRec_Key, &key) || key == 0 ||
                !Read16(r + kOff_FriendlyRec_Group, &group) ||
                !Read64(r + kOff_FriendlyRec_Value, &rawValue))
                return;

            const int64_t incoming = static_cast<int64_t>(rawValue);
            if (incoming < 0 || incoming > kFriendly_Max) return;

            const uint64_t cacheKey = TrustCacheKey(mapOwner, group, key);
            const State& st = State::Get();
            const bool scale = Player::Ready() && st.trustMult && st.trustMultVal > 1.0f;

            std::lock_guard<std::mutex> lock(s_trustMutex);
            const auto found = s_lastTrustMap.find(cacheKey);
            if (found == s_lastTrustMap.end())
            {
                s_lastTrustMap[cacheKey] = incoming;
                return;
            }

            const int64_t oldValue = found->second;
            const int64_t newValue = ScaleTrustValue(
                oldValue, true, incoming, st.trustMultVal, scale);
            s_lastTrustMap[cacheKey] = newValue;
            if (newValue == incoming) return;

            Write64(r + kOff_FriendlyRec_Value, static_cast<uint64_t>(newValue));
        }

        void* __fastcall hkSetNpc(void* mapOwner, void* record)
        {
            __try { ObserveAndScaleTrust(mapOwner, record); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            return oSetNpc(mapOwner, record);
        }

        void* __fastcall hkSetPet(void* mapOwner, void* record)
        {
            __try { ObserveAndScaleTrust(mapOwner, record); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            return oSetPet(mapOwner, record);
        }

        void* __fastcall hkGetNpc(void* mapOwner, void* actor)
        {
            void* record = oGetNpc(mapOwner, actor);
            __try { ObserveLiveTrust(mapOwner, record); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            return record;
        }

        void* __fastcall hkGetPet(void* mapOwner, void* actor)
        {
            void* record = oGetPet(mapOwner, actor);
            __try { ObserveLiveTrust(mapOwner, record); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            return record;
        }

        bool CreateAndEnable(void* target, void* detour, void** original)
        {
            if (MH_CreateHook(target, detour, original) != MH_OK) return false;
            if (MH_EnableHook(target) == MH_OK) return true;
            MH_RemoveHook(target);
            *original = nullptr;
            return false;
        }
    }

    bool Friendly::Install()
    {
        uintptr_t npcAddr = mem::FindPattern(kSig_FriendlySetNpc201);
        uintptr_t petAddr = mem::FindPattern(kSig_FriendlySetPet201);
        if (!npcAddr && !petAddr)
        {
            npcAddr = mem::FindPattern(kSig_FriendlySetNpc);
            petAddr = mem::FindPattern(kSig_FriendlySetPet);
        }

        if (npcAddr)
        {
            g_npcTarget = reinterpret_cast<void*>(npcAddr);
            if (CreateAndEnable(g_npcTarget, reinterpret_cast<void*>(&hkSetNpc),
                                reinterpret_cast<void**>(&oSetNpc)))
            {
                LOG_OK("friendly: NPC Trust Multiplier observer installed @ %p", g_npcTarget);
                g_hooksInstalled = true;
            }
            else g_npcTarget = nullptr;
        }

        if (petAddr)
        {
            g_petTarget = reinterpret_cast<void*>(petAddr);
            if (CreateAndEnable(g_petTarget, reinterpret_cast<void*>(&hkSetPet),
                                reinterpret_cast<void**>(&oSetPet)))
            {
                LOG_OK("friendly: pet/mount Trust Multiplier observer installed @ %p", g_petTarget);
                g_hooksInstalled = true;
            }
            else g_petTarget = nullptr;
        }

        const uintptr_t npcGetAddr = mem::FindPattern(kSig_FriendlyGetNpc201);
        const uintptr_t petGetAddr = mem::FindPattern(kSig_FriendlyGetPet201);
        if (npcGetAddr)
        {
            g_npcGetTarget = reinterpret_cast<void*>(npcGetAddr);
            if (CreateAndEnable(g_npcGetTarget, reinterpret_cast<void*>(&hkGetNpc),
                                reinterpret_cast<void**>(&oGetNpc)))
            {
                LOG_OK("friendly: NPC in-place trust observer installed @ %p", g_npcGetTarget);
                g_hooksInstalled = true;
            }
            else g_npcGetTarget = nullptr;
        }
        if (petGetAddr)
        {
            g_petGetTarget = reinterpret_cast<void*>(petGetAddr);
            if (CreateAndEnable(g_petGetTarget, reinterpret_cast<void*>(&hkGetPet),
                                reinterpret_cast<void**>(&oGetPet)))
            {
                LOG_OK("friendly: pet/mount in-place trust observer installed @ %p", g_petGetTarget);
                g_hooksInstalled = true;
            }
            else g_petGetTarget = nullptr;
        }

        g_hooksEnabled = g_hooksInstalled;
        if (!g_hooksInstalled)
            LOG_ERR("friendly: Trust Multiplier setters NOT FOUND - feature disabled.");
        return g_hooksInstalled;
    }

    void Friendly::Tick()
    {
        if (!g_hooksInstalled) return;
        const State& st = State::Get();
        const bool active = Player::Ready() && st.trustMult && st.trustMultVal > 1.0f;
        if (active == g_featureReportedEnabled) return;
        g_featureReportedEnabled = active;
        if (active)
            LOG_OK("friendly: Trust Multiplier (%.1fx) ENGAGED.", st.trustMultVal);
        else
            LOG("friendly: Trust Multiplier DISENGAGED.");
    }

    void Friendly::Remove()
    {
        if (g_npcTarget)
        {
            MH_DisableHook(g_npcTarget);
            MH_RemoveHook(g_npcTarget);
            g_npcTarget = nullptr;
        }
        if (g_petTarget)
        {
            MH_DisableHook(g_petTarget);
            MH_RemoveHook(g_petTarget);
            g_petTarget = nullptr;
        }
        if (g_npcGetTarget)
        {
            MH_DisableHook(g_npcGetTarget);
            MH_RemoveHook(g_npcGetTarget);
            g_npcGetTarget = nullptr;
        }
        if (g_petGetTarget)
        {
            MH_DisableHook(g_petGetTarget);
            MH_RemoveHook(g_petGetTarget);
            g_petGetTarget = nullptr;
        }
        std::lock_guard<std::mutex> lock(s_trustMutex);
        s_lastTrustMap.clear();
        oSetNpc = nullptr;
        oSetPet = nullptr;
        g_hooksInstalled = false;
        g_hooksEnabled = false;
        g_featureReportedEnabled = false;
    }

    bool Friendly::Ready()
    {
        return g_hooksInstalled;
    }
}
