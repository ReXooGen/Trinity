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
#include "inventory.h"
#include "../core/version_detect.h"
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
        // Direct SetNpc and SetPet Leaf Setters (Gift / Dialogue / Pet Taming records)
        // Strictly isolated to gifting, feeding, and taming interactions.
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
            if (mult <= 1.0f) return;

            const uintptr_t r = reinterpret_cast<uintptr_t>(record);
            if (r < kMinPointer) return;

            uint32_t key = 0;
            if (!Read32(r + kOff_FriendlyRec_Key, &key)) return;
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
        // 1. Install SetNpc prologue hook (Strictly for dialogue & gift giving)
        uintptr_t npcAddr = mem::FindPattern(kSig_FriendlySetNpc);
        if (!npcAddr)
            npcAddr = mem::FindPattern(kSig_FriendlySetNpc_Legacy);

        if (npcAddr)
        {
            g_npcTarget = reinterpret_cast<void*>(npcAddr);
            if (MH_CreateHook(g_npcTarget, reinterpret_cast<void*>(&hkSetNpc), reinterpret_cast<void**>(&oSetNpc)) == MH_OK)
            {
                MH_EnableHook(g_npcTarget);
                LOG_OK("friendly: SetNpc multiplier hook installed & enabled @ 0x%p", g_npcTarget);
                g_hooksInstalled = true;
            }
            else
            {
                LOG_ERR("friendly: failed to create SetNpc hook @ 0x%p", g_npcTarget);
                g_npcTarget = nullptr;
            }
        }
        else
        {
            // Expected on TU 2.04: both setters are gone/inlined (see the
            // offsets.h note). Trust Multiplier runs through the component
            // poll in Friendly::Tick instead - this hook was only the
            // instant-write capture path for gifting/dialogue.
            LOG_INFO("friendly: SetNpc signature not present on this TU - using the component poll path.");
        }

        // 2. Install SetPet prologue hook (Strictly for pet/mount taming & feeding)
        uintptr_t petAddr = mem::FindPattern(kSig_FriendlySetPet);
        if (!petAddr)
            petAddr = mem::FindPattern(kSig_FriendlySetPet_Legacy);

        if (petAddr)
        {
            g_petTarget = reinterpret_cast<void*>(petAddr);
            if (MH_CreateHook(g_petTarget, reinterpret_cast<void*>(&hkSetPet), reinterpret_cast<void**>(&oSetPet)) == MH_OK)
            {
                MH_EnableHook(g_petTarget);
                LOG_OK("friendly: SetPet multiplier hook installed & enabled @ 0x%p", g_petTarget);
                g_hooksInstalled = true;
            }
            else
            {
                LOG_ERR("friendly: failed to create SetPet hook @ 0x%p", g_petTarget);
                g_petTarget = nullptr;
            }
        }
        else
        {
            // Expected on TU 2.04 - see the SetNpc note above.
            LOG_INFO("friendly: SetPet signature not present on this TU - using the component poll path.");
        }

        g_hooksEnabled = true;
        return g_hooksInstalled;
    }

    namespace
    {
        // Walks one realm's friendly component; fn(relationshipKey, valueAddr).
        template <typename Fn>
        void ForEachTrust(uintptr_t owner, Fn&& fn)
        {
            uintptr_t sub = 0, comp = 0, back = 0, entries = 0;
            uint32_t size = 0;
            if (owner < kMinPointer || !mem::ReadPtr(owner + kOff_Owner_Actor, &sub) || sub < kMinPointer ||
                !mem::ReadPtr(sub + kOff_Sub_FriendlyComp, &comp) || comp < kMinPointer ||
                !mem::ReadPtr(comp + 0x08, &back) || back != owner ||
                !Read32(comp + kOff_FriendlyComp_Size, &size) || size > 4096 ||
                !mem::ReadPtr(comp + kOff_FriendlyComp_Entries, &entries) || entries < kMinPointer)
                return;
            for (uint32_t i = 0; i < size; ++i)
            {
                uintptr_t entry = 0, record = 0;
                uint64_t key = 0;
                if (mem::ReadPtr(entries + i * 8ull, &entry) && entry >= kMinPointer &&
                    Read64(entry, &key) &&
                    mem::ReadPtr(entry + kOff_FriendlyEntry_Record, &record) && record >= kMinPointer)
                    fn(key, record + kOff_FriendlyRecord_Value);
            }
        }

        // One-shot diagnostic: the friendly-comp offsets (sub+0x140, +0x1C size,
        // +0x30 entries) arrived from static 2.03 research and have never been
        // live-verified on PE 3446 - a silent walk failure means Trust Multiplier
        // never scales anything. When enabled and the walk finds no records, dump
        // the plausible component slots around sub+0x140 so the log names the real
        // layout on the next session (comp candidates: backref==owner at +8).
        void DiagnoseFriendlyChainOnce(uintptr_t owner)
        {
            static bool s_ran = false;
            if (s_ran || owner < kMinPointer) return;
            uintptr_t sub = 0;
            if (!mem::ReadPtr(owner + kOff_Owner_Actor, &sub) || sub < kMinPointer) return;
            s_ran = true;
            LOG("friendly: chain diagnostic (owner=%p sub=%p)", reinterpret_cast<void*>(owner), reinterpret_cast<void*>(sub));
            for (uintptr_t off = 0x100; off <= 0x1C0; off += 8)
            {
                uintptr_t cand = 0;
                if (!mem::ReadPtr(sub + off, &cand) || cand < kMinPointer) continue;
                uintptr_t back = 0;
                mem::ReadPtr(cand + 0x08, &back);
                uint32_t sz = 0;
                mem::Read32(cand + 0x1C, &sz);
                uintptr_t ent = 0;
                mem::ReadPtr(cand + 0x30, &ent);
                LOG("friendly: sub+%#llx -> %p back==owner=%d size@1C=%u entries@30=%s",
                    static_cast<unsigned long long>(off), reinterpret_cast<void*>(cand),
                    back == owner ? 1 : 0, sz, ent >= kMinPointer ? "yes" : "no");
            }
        }

        // TU 2.04: scale each rise of a client trust value, then mirror the result to
        // the server's copy of the same relationship so it persists. First sight of a
        // relationship only seeds the cache. The single-step clamp keeps the ledger
        // delta small (see the v1.2.5 Error 298648703 notes).
        // Seeding state: when the multiplier turns on, snapshot every current
        // value WITHOUT scaling, so the very next real gain (the first gift,
        // the "+5 Stranger" popup) is already a delta and gets multiplied.
        // Without this, the first gain of any relationship passes unscaled.
        // Lives at namespace scope: Friendly::Tick re-arms it on disable.
        bool g_trustSeeded = false;

        void PollTrustMultiplier(float mult)
        {
            static std::unordered_map<uint64_t, int64_t> s_last;
            static std::unordered_map<uint64_t, ULONGLONG> s_nextWrite;
            const ULONGLONG now = GetTickCount64();
            const uintptr_t client = Player::GetControlledOwner();
            const uintptr_t server = Inventory::ServerCharacterAddr();
            // Count what the walk yields this pass; zero records on an enabled
            // multiplier triggers the one-shot chain diagnostic (log next line).
            unsigned walked = 0;
            ForEachTrust(client, [&](uint64_t key, uintptr_t valueAddr)
            {
                ++walked;
                int64_t value = 0;
                if (!Read64(valueAddr, reinterpret_cast<uint64_t*>(&value)) || value < 0 || value > kFriendly_Max)
                    return;
                if (!g_trustSeeded)
                {
                    s_last[key] = value;
                    return;
                }
                const auto seen = s_last.find(key);
                if (seen == s_last.end())
                {
                    // A relationship that appeared after seeding (new NPC): seed it.
                    s_last[key] = value;
                    return;
                }
                if (value <= seen->second)
                {
                    s_last[key] = value;
                    return;
                }
                const int64_t gain = value - seen->second;
                const int64_t scaled = (std::min)(static_cast<int64_t>(gain * mult + 0.5f), int64_t{ 20 });
                int64_t target = (std::min)(seen->second + (std::max)(gain, scaled), kFriendly_Max);
                s_last[key] = target;
                if (target == value) return;
                const auto throttle = s_nextWrite.find(key);
                if (throttle != s_nextWrite.end() && now < throttle->second)
                {
                    // Too soon - fold this gain into the next write window.
                    return;
                }
                s_nextWrite[key] = now + 2000;
                uint64_t serverHit = 0;
                ForEachTrust(server, [&](uint64_t serverKey, uintptr_t serverValue)
                {
                    if (serverKey == key)
                    {
                        Write64(serverValue, static_cast<uint64_t>(target));
                        serverHit = serverValue;
                    }
                });
                // Scale trace: proves whether the scaled write happened and on
                // which realm. serverHit==0 means client-only - the same wound
                // add-item has, where the server reconcile wipes the gain.
                LOG_INFO("friendly: scale key=%llu base=%lld gain=%lld mult=%.2f target=%lld client=%llx server=%llx",
                         static_cast<unsigned long long>(key), static_cast<long long>(seen->second),
                         static_cast<long long>(gain), mult, static_cast<long long>(target),
                         static_cast<unsigned long long>(valueAddr),
                         static_cast<unsigned long long>(serverHit));
                Write64(valueAddr, static_cast<uint64_t>(target));
            });
            if (walked == 0)
            {
                // The walk found nothing on this pass. Dump the candidate layout
                // once so a running session reveals the real 3446 offsets.
                static ULONGLONG s_lastDiag = 0;
                if (now - s_lastDiag > 30000)
                {
                    s_lastDiag = now;
                    LOG_WARN("friendly: trust walk yielded 0 records (multiplier %.2fx) - running chain diagnostic.",
                             mult);
                    DiagnoseFriendlyChainOnce(client);
                }
            }
            g_trustSeeded = true;
        }
    }

    void Friendly::Tick()
    {
        if (core::GetGameVersion().revision < 3400) return;
        const State& st = State::Get();
        if (!st.trustMult || st.trustMultVal <= 1.0f)
        {
            // Re-arm the seeding so the next enable snapshots fresh baselines
            // instead of treating accumulated offline gains as one event.
            g_trustSeeded = false;
            return;
        }
        static ULONGLONG s_lastPoll = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastPoll < 120) return;
        s_lastPoll = now;
        __try { PollTrustMultiplier(st.trustMultVal); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
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

        g_hooksInstalled = false;
        g_hooksEnabled = false;

        std::lock_guard<std::mutex> lock(s_trustMutex);
        s_lastTrustMap.clear();
    }

    bool Friendly::Ready()
    {
        // TU 2.04: the SetNpc/SetPet hooks are gone (inlined upstream), but the
        // component poll in Friendly::Tick scales trust without them. The menu
        // must not claim "Unavailable right now." while the poll path works.
        return g_hooksInstalled || core::GetGameVersion().revision >= 3400;
    }
}
