#include "friendly.h"

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <algorithm>
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
    using mem::Write32;
    using mem::Write64;

    namespace
    {
        // Direct SetNpc and SetPet Leaf Setters (Prologue level)
        // Intercepting here allows proportional scaling of the trust gain (1x .. 100x).
        using FriendlySet_t = void*(__fastcall*)(void* mapOwner, void* record);
        FriendlySet_t oSetNpc = nullptr;
        FriendlySet_t oSetPet = nullptr;
        void* g_npcTarget = nullptr;
        void* g_petTarget = nullptr;

        bool g_hooksInstalled = false;
        bool g_hooksEnabled = false;

        // Tracks last known trust value per record key to scale trust gains accurately (thread-safe)
        std::mutex s_trustMutex;
        std::unordered_map<uint32_t, int64_t> s_lastTrustMap;

        void ApplyTrustMultiplierToRecord(void* record, float mult, const char* srcName)
        {
            if (!Player::Ready() || mult <= 1.0f) return;

            const uintptr_t r = reinterpret_cast<uintptr_t>(record);
            if (r < kMinPointer) return;

            uint32_t key = 0;
            if (!Read32(r + kOff_FriendlyRec_Key, &key)) return;

            // key == 0 is the save-loader at login / title screen (never scale baseline on load)
            if (key == 0) return;

            uint64_t rawVal64 = 0;
            Read64(r + 0x28, &rawVal64);
            uint32_t rawVal32 = 0;
            Read32(r + 0x20, &rawVal32);

            const int64_t currentIncoming = (rawVal64 > 0) ? static_cast<int64_t>(rawVal64) : static_cast<int64_t>(rawVal32);

            std::lock_guard<std::mutex> lock(s_trustMutex);
            int64_t oldVal = 0;
            auto it = s_lastTrustMap.find(key);
            if (it != s_lastTrustMap.end())
            {
                oldVal = it->second;
            }

            int64_t gain = currentIncoming - oldVal;
            if (gain <= 0)
            {
                // If initial touch where oldVal wasn't recorded, the gain is the incoming value
                if (oldVal == 0 && currentIncoming > 0)
                {
                    gain = currentIncoming;
                }
                else
                {
                    s_lastTrustMap[key] = currentIncoming;
                    return;
                }
            }

            const int64_t scaledGain = static_cast<int64_t>(static_cast<float>(gain) * mult);
            int64_t newTrust = oldVal + scaledGain;
            if (newTrust > 100) newTrust = 100;
            if (newTrust < 0) newTrust = 0;

            Write32(r + 0x20, static_cast<uint32_t>(newTrust));
            Write64(r + 0x20, static_cast<uint64_t>(newTrust));
            Write32(r + 0x28, static_cast<uint32_t>(newTrust));
            Write64(r + 0x28, static_cast<uint64_t>(newTrust));

            s_lastTrustMap[key] = newTrust;
        }

        using NpcTrustWriter_t = int64_t(__fastcall*)(void* factionMgr, void* targetActor, uint16_t relationGroup, int32_t delta, void* a5, void* a6);
        NpcTrustWriter_t oNpcTrustWriter = nullptr;
        void* g_writerTarget = nullptr;

        int64_t __fastcall hkFriendlyNpcTrustWriter(void* factionMgr, void* targetActor, uint16_t relationGroup, int32_t delta, void* a5, void* a6)
        {
            const State& st = State::Get();
            if (st.trustMult && st.trustMultVal > 1.0f && delta > 0)
            {
                int64_t scaled = static_cast<int64_t>(static_cast<float>(delta) * st.trustMultVal);
                if (scaled > 100) scaled = 100;
                delta = static_cast<int32_t>(scaled);
            }
            return oNpcTrustWriter(factionMgr, targetActor, relationGroup, delta, a5, a6);
        }

        using AlertDisp_t = int64_t(__fastcall*)(void* factionMgr, void* actorCtx, uint16_t relationGroup, int32_t delta, void* a5, void* a6);
        AlertDisp_t oAlertDisp = nullptr;
        void* g_alertDispTarget = nullptr;

        int64_t __fastcall hkAlertDisp(void* factionMgr, void* actorCtx, uint16_t relationGroup, int32_t delta, void* a5, void* a6)
        {
            const State& st = State::Get();
            if (st.trustMult && st.trustMultVal > 1.0f && delta > 0)
            {
                int64_t scaled = static_cast<int64_t>(static_cast<float>(delta) * st.trustMultVal);
                if (scaled > 100) scaled = 100;
                delta = static_cast<int32_t>(scaled);
            }
            return oAlertDisp(factionMgr, actorCtx, relationGroup, delta, a5, a6);
        }

        void* __fastcall hkSetNpc(void* mapOwner, void* record)
        {
            const State& st = State::Get();
            if (st.trustMult && st.trustMultVal > 1.0f)
            {
                __try
                {
                    ApplyTrustMultiplierToRecord(record, st.trustMultVal, "SetNpc");
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
                    ApplyTrustMultiplierToRecord(record, st.trustMultVal, "SetPet");
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return oSetPet(mapOwner, record);
        }
    }

    bool Friendly::Install()
    {
        // 1. Install SetNpc prologue hook
        const uintptr_t npcAddr = mem::FindPattern(kSig_FriendlySetNpc);
        if (npcAddr)
        {
            g_npcTarget = reinterpret_cast<void*>(npcAddr);
            if (MH_CreateHook(g_npcTarget, reinterpret_cast<void*>(&hkSetNpc), reinterpret_cast<void**>(&oSetNpc)) == MH_OK)
            {
                LOG_OK("friendly: SetNpc multiplier hook installed @ 0x%p", g_npcTarget);
                g_hooksInstalled = true;
            }
            else
            {
                g_npcTarget = nullptr;
            }
        }

        // 2. Install SetPet prologue hook
        const uintptr_t petAddr = mem::FindPattern(kSig_FriendlySetPet);
        if (petAddr)
        {
            g_petTarget = reinterpret_cast<void*>(petAddr);
            if (MH_CreateHook(g_petTarget, reinterpret_cast<void*>(&hkSetPet), reinterpret_cast<void**>(&oSetPet)) == MH_OK)
            {
                LOG_OK("friendly: SetPet multiplier hook installed @ 0x%p", g_petTarget);
                g_hooksInstalled = true;
            }
            else
            {
                g_petTarget = nullptr;
            }
        }

        // 3. Install NpcTrustWriter direct relation writer hook
        const uintptr_t writerAddr = mem::FindPattern(kSig_FriendlyNpcTrustWriter);
        if (writerAddr)
        {
            g_writerTarget = reinterpret_cast<void*>(writerAddr);
            if (MH_CreateHook(g_writerTarget, reinterpret_cast<void*>(&hkFriendlyNpcTrustWriter), reinterpret_cast<void**>(&oNpcTrustWriter)) == MH_OK)
            {
                LOG_OK("friendly: NpcTrustWriter multiplier hook installed @ 0x%p", g_writerTarget);
                g_hooksInstalled = true;
            }
            else
            {
                g_writerTarget = nullptr;
            }
        }

        // 4. Install AlertDispatcher UI & Faction dispatcher hook
        const uintptr_t alertAddr = mem::FindPattern(kSig_FriendlyAlertDisp);
        if (alertAddr)
        {
            g_alertDispTarget = reinterpret_cast<void*>(alertAddr);
            if (MH_CreateHook(g_alertDispTarget, reinterpret_cast<void*>(&hkAlertDisp), reinterpret_cast<void**>(&oAlertDisp)) == MH_OK)
            {
                LOG_OK("friendly: AlertDispatcher multiplier hook installed @ 0x%p", g_alertDispTarget);
                g_hooksInstalled = true;
            }
            else
            {
                g_alertDispTarget = nullptr;
            }
        }

        return g_hooksInstalled;
    }

    void Friendly::Tick()
    {
        if (!g_hooksInstalled) return;

        const State& st = State::Get();
        const bool wantEnabled = st.trustMult && (st.trustMultVal > 1.0f);

        if (wantEnabled != g_hooksEnabled)
        {
            if (wantEnabled)
            {
                if (g_npcTarget) MH_EnableHook(g_npcTarget);
                if (g_petTarget) MH_EnableHook(g_petTarget);
                if (g_writerTarget) MH_EnableHook(g_writerTarget);
                if (g_alertDispTarget) MH_EnableHook(g_alertDispTarget);
                g_hooksEnabled = true;
                LOG_OK("friendly: Trust Multiplier (%.1fx) ENGAGED.", st.trustMultVal);
            }
            else
            {
                if (g_npcTarget) MH_DisableHook(g_npcTarget);
                if (g_petTarget) MH_DisableHook(g_petTarget);
                if (g_writerTarget) MH_DisableHook(g_writerTarget);
                if (g_alertDispTarget) MH_DisableHook(g_alertDispTarget);
                g_hooksEnabled = false;
                LOG("friendly: Trust Multiplier standby.");
            }
        }
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

        if (g_writerTarget)
        {
            MH_DisableHook(g_writerTarget);
            MH_RemoveHook(g_writerTarget);
            g_writerTarget = nullptr;
        }

        if (g_alertDispTarget)
        {
            MH_DisableHook(g_alertDispTarget);
            MH_RemoveHook(g_alertDispTarget);
            g_alertDispTarget = nullptr;
        }

        g_hooksInstalled = false;
        g_hooksEnabled = false;
        {
            std::lock_guard<std::mutex> lock(s_trustMutex);
            s_lastTrustMap.clear();
        }
    }

    bool Friendly::Ready()
    {
        return g_hooksInstalled;
    }
}
