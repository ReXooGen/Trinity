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
#include "../gui/framework.h"

namespace trinity::game
{
    using mem::Read8;
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

        // TU 2.04 ledger writer (IDA-verified sub_142076590): (a1 component, a2 tag,
        // a3 record*). The game copies the 0x78-byte record at a3 into a1's per-tag
        // ledger for whichever realm the TLS byte selects - so scaling a3 in place
        // propagates through the game's own copy into the server ledger too.
        using LedgerWrite_t = __int64(__fastcall*)(void* component, unsigned __int16 tag, void* record);
        LedgerWrite_t oLedgerWrite = nullptr;
        void* g_ledgerTarget = nullptr;

        // Per-relationship last-seen value: key = u32@(record+96) ^ (u16@(record+100)<<32)
        std::unordered_map<uint64_t, int64_t> g_ledgerLast;

        __int64 __fastcall hkLedgerWrite(void* component, unsigned __int16 tag, void* record)
        {
            const State& st = State::Get();
            if (st.trustMult && st.trustMultVal > 1.0f && record)
            {
                __try
                {
                    const uintptr_t r = reinterpret_cast<uintptr_t>(record);
                    if (r >= kMinPointer)
                    {
                        const uint32_t k32v = *reinterpret_cast<uint32_t*>(r + 96);
                        const uint16_t k16 = *reinterpret_cast<uint16_t*>(r + 100);
                        const uint64_t key = static_cast<uint64_t>(k32v) | (static_cast<uint64_t>(k16) << 32);
                        const int64_t incoming = *reinterpret_cast<int64_t*>(r + 8);
                        int64_t& last = g_ledgerLast[key];
                        if (incoming > last)
                        {
                            const int64_t gain = incoming - last;
                            const int64_t scaled = (std::min)(static_cast<int64_t>(gain * st.trustMultVal + 0.5f), int64_t{ 100 });
                            const int64_t target = (std::min)(last + (std::max)(gain, scaled), int64_t{ 100 });
                            if (target != incoming)
                            {
                                *reinterpret_cast<int64_t*>(r + 8) = target;
                                LOG_INFO("friendly: ledger-scale key=%llx gain=%lld mult=%.2f -> %lld (was %lld)",
                                         static_cast<unsigned long long>(key), static_cast<long long>(gain),
                                         st.trustMultVal, static_cast<long long>(target),
                                         static_cast<long long>(incoming));
                            }
                            last = target;
                        }
                        else if (incoming < last)
                        {
                            last = incoming;
                        }
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return oLedgerWrite(component, tag, record);
        }
    }

    bool Friendly::Install()
    {
        // 0. TU 2.04 ledger writer hook (SetNpc/SetPet were inlined; this shared
        //    record writer is their 3446 successor and serves both realms).
        //    3446-pinned: skip on 3460+ - the prologue bytes repeat across the
        //    image, and on the moved 2.04.03 layout FindPattern can land on an
        //    unrelated hot function (observed as during-play stutter).
        if (core::GetGameVersion().revision < 3460)
        {
            if (uintptr_t ledgerAddr = mem::FindPattern(kSig_FriendlyLedgerWrite))
            {
                g_ledgerTarget = reinterpret_cast<void*>(ledgerAddr);
                if (MH_CreateHook(g_ledgerTarget, reinterpret_cast<void*>(&hkLedgerWrite),
                                  reinterpret_cast<void**>(&oLedgerWrite)) == MH_OK)
                {
                    MH_EnableHook(g_ledgerTarget);
                    LOG_OK("friendly: ledger writer hook installed & enabled @ 0x%p", g_ledgerTarget);
                    g_hooksInstalled = true;
                }
                else
                {
                    LOG_ERR("friendly: failed to create ledger writer hook @ 0x%p", g_ledgerTarget);
                    g_ledgerTarget = nullptr;
                }
            }
            else
            {
                LOG_INFO("friendly: ledger writer signature not present on this TU - component poll only.");
            }
        }

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

        // ---- TU 2.04.03 (3460) re-derivation ladder, in-process ----------------
        // External RPM is refused by the updated anti-tamper, so the 3446 method
        // (probe the live process from outside) moved here. Read-only, 5s cadence:
        // 1. controlled owner -> actor -> scan component slots 0x00..0x400 for a
        //    component whose vtable RTTI name contains "Friendly" (same COL->TD
        //    read that verified the 3446 layout externally).
        // 2. dump that component's entry count + the last few record values.
        // 3. FindPattern the copy-tail anchors (XMM/YMM) + the category-table
        //    probe - the site candidates for the virtual dispatch (site B).
        // Every greet/gift diff between two dumps = the live ledger path.
        void SnapshotFriendly3460()
        {
            const uintptr_t owner = Player::GetControlledOwner();
            if (owner < kMinPointer) { LOG("f3460: no controlled owner"); return; }
            uintptr_t sub = 0;
            if (!mem::ReadPtr(owner + kOff_Owner_Actor, &sub) || sub < kMinPointer)
            { LOG("f3460: owner=%p no actor", reinterpret_cast<void*>(owner)); return; }
            uintptr_t fcomp = 0; int foff = -1;
            for (int off = 0x000; off <= 0x400; off += 8)
            {
                uintptr_t cand = 0;
                if (!mem::ReadPtr(sub + static_cast<uintptr_t>(off), &cand) || cand < kMinPointer) continue;
                uintptr_t vt = 0;
                if (!mem::ReadPtr(cand, &vt) || vt < 0x140000000ULL || vt > 0x155000000ULL) continue;
                uintptr_t col = 0;
                if (!mem::ReadPtr(vt - 8, &col) || col < 0x140000000ULL || col > 0x155000000ULL) continue;
                uint32_t tdRva = 0;
                if (!Read32(col + 12, &tdRva) || !tdRva || tdRva > 0x14000000u) continue;
                char name[96]{};
                const uintptr_t tdAddr = 0x140000000ULL + tdRva;
                for (unsigned c = 0; c < 90; ++c)
                {
                    uint8_t ch = 0;
                    if (!Read8(tdAddr + 16 + c, &ch) || !ch) break;
                    name[c] = static_cast<char>(ch);
                }
                if (name[0] != '.') continue;
                if (strstr(name, "Friendly"))
                {
                    fcomp = cand; foff = off;
                    LOG("f3460: friendly comp sub+%#X -> %p %s", off, reinterpret_cast<void*>(cand), name);
                }
            }
            if (!fcomp)
            {
                LOG("f3460: no Friendly component found under sub=%p (owner=%p)", reinterpret_cast<void*>(sub), reinterpret_cast<void*>(owner));
                return;
            }
            // dump the record table of the friendly component using the 3446 shape
            // (likely still close): +0x1C size, +0x30 entries, entry+0 key, entry+8 rec, rec+8 value
            uint32_t size = 0;
            if (Read32(fcomp + 0x1C, &size) && size > 0 && size <= 4096)
            {
                uintptr_t entries = 0;
                if (mem::ReadPtr(fcomp + 0x30, &entries) && entries >= kMinPointer)
                {
                    LOG("f3460: comp=%p size=%u entries=%p", reinterpret_cast<void*>(fcomp), size, reinterpret_cast<void*>(entries));
                    for (uint32_t i = size >= 6 ? size - 6 : 0; i < size; ++i)
                    {
                        uintptr_t e = 0, rec = 0; uint64_t key = 0; int64_t val = 0;
                        if (!mem::ReadPtr(entries + static_cast<uintptr_t>(i) * 8, &e) || e < kMinPointer) continue;
                        Read64(e, &key);
                        if (!mem::ReadPtr(e + 8, &rec) || rec < kMinPointer) continue;
                        Read64(rec + 8, reinterpret_cast<uint64_t*>(&val));
                        LOG("f3460: rec[%u] key=%llx val=%lld", i, static_cast<unsigned long long>(key), static_cast<long long>(val));
                    }
                }
                else LOG("f3460: comp=%p size=%u but entries unreadable", reinterpret_cast<void*>(fcomp), size);
            }
            else LOG("f3460: comp=%p size unreadable at +0x1C", reinterpret_cast<void*>(fcomp));
        }

        void Diagnose3460()
        {
            static bool s_sigOnce = false;
            if (!s_sigOnce)
            {
                s_sigOnce = true;
                struct Site { const char* name; const char* sig; };
                const Site sites[] = {
                    { "xmm-tail", "C5 FC 11 49 20 C5 F8 10 47 40 C5 F8 11 41 40" },
                    { "ymm-tail", "C5 FC 11 49 20 C5 FC 10 47 40 C5 FC 11 41 40" },
                    { "category", "0F B7 39 48 8B 1D ?? ?? ?? ?? 3B 7B 08 0F 83 ?? ?? ?? ?? 48 8D 34 FD 00 00 00 00 48 8B 43 58 48 8B 04 06" },
                    { "writer",   "48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9" },
                };
                for (const Site& s : sites)
                {
                    const uintptr_t a = mem::FindPattern(s.sig);
                    LOG("f3460: site %-9s -> %p", s.name, reinterpret_cast<void*>(a));
                }
            }
            SnapshotFriendly3460();
        }

        // Max Trust sweep: drive every discovered relationship in the ledger to
        // the game cap (100). The 3446/3460 sessions proved direct ledger writes
        // at any value <= cap are accepted and persist (105 survived minutes of
        // play; maxed NPCs read exactly 100), so a straight write per record is
        // safe. One full sweep per 2s; a toast fires once per sweep when at
        // least one record was raised.
        void SweepMaxTrust()
        {
            const uintptr_t client = Player::GetControlledOwner();
            if (client < kMinPointer) return;
            static ULONGLONG s_next = 0;
            const ULONGLONG now = GetTickCount64();
            if (now < s_next) return;
            s_next = now + 2000;
            int raised = 0, seen = 0;
            ForEachTrust(client, [&](uint64_t key, uintptr_t valueAddr)
            {
                ++seen;
                int64_t value = 0;
                if (!Read64(valueAddr, reinterpret_cast<uint64_t*>(&value)) || value < 0 || value > kFriendly_Max)
                    return;
                if (value >= kFriendly_Max) return;
                Write64(valueAddr, static_cast<uint64_t>(kFriendly_Max));
                ++raised;
            });
            if (raised > 0)
            {
                LOG_OK("friendly: max trust raised %d/%d relationship(s) to %lld.",
                       raised, seen, static_cast<long long>(kFriendly_Max));
                ui::Toast("Max Trust: %d relationship(s) -> %lld", raised,
                          static_cast<long long>(kFriendly_Max));
            }
        }

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
                    // Live-verified on PE 3446 (2026-11-01 session): a greet/gift
                    // APPENDS a brand-new record already carrying the base gain
                    // (0 -> 5/10 between two 120 ms passes; watched externally via
                    // RPM, entries 409/410 of the client component). First sight of
                    // a new key therefore IS the gain event - seed it AND scale it.
                    // Old relationships discovered later are no-ops: a maxed 100
                    // computes target=100 (single-step clamp keeps scaled<=20, so
                    // max(gain,scaled)=gain) and the target==value guard skips it.
                    s_last[key] = value;
                    if (value <= 0) return;
                    const int64_t gain = value;
                    const int64_t scaled = (std::min)(static_cast<int64_t>(gain * mult + 0.5f), int64_t{ 20 });
                    const int64_t target = (std::min)((std::max)(gain, scaled), kFriendly_Max);
                    if (target == value) return;
                    const auto throttle = s_nextWrite.find(key);
                    if (throttle != s_nextWrite.end() && now < throttle->second) return;
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
                    LOG_INFO("friendly: scale(new) key=%llu gain=%lld mult=%.2f target=%lld client=%llx server=%llx",
                             static_cast<unsigned long long>(key), static_cast<long long>(gain),
                             mult, static_cast<long long>(target),
                             static_cast<unsigned long long>(valueAddr),
                             static_cast<unsigned long long>(serverHit));
                    ui::Toast("Trust +%lld (x%.2f)", static_cast<long long>(target), mult);
                    Write64(valueAddr, static_cast<uint64_t>(target));
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
                ui::Toast("Trust +%lld (x%.2f)", static_cast<long long>(target), mult);
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
            // 30s ledger snapshot: proves the scaled writes stick in the ledger
            // (3460: sub+0x140 IS ServerFriendlyActorComponent - the authority).
            static ULONGLONG s_lastDump = 0;
            if (now - s_lastDump > 30000)
            {
                s_lastDump = now;
                uintptr_t comp = 0;
                if (mem::ReadPtr(client + kOff_Owner_Actor, &comp) && comp >= kMinPointer &&
                    mem::ReadPtr(comp + kOff_Sub_FriendlyComp, &comp) && comp >= kMinPointer)
                {
                    uint32_t size = 0;
                    if (Read32(comp + kOff_FriendlyComp_Size, &size) && size)
                    {
                        uintptr_t entries = 0;
                        if (mem::ReadPtr(comp + kOff_FriendlyComp_Entries, &entries) && entries >= kMinPointer)
                        {
                            for (uint32_t i = size >= 4 ? size - 4 : 0; i < size; ++i)
                            {
                                uintptr_t e = 0, rec = 0; uint64_t key = 0; int64_t val = 0;
                                if (!mem::ReadPtr(entries + static_cast<uintptr_t>(i) * 8, &e) || e < kMinPointer) continue;
                                Read64(e, &key);
                                if (!mem::ReadPtr(e + 8, &rec) || rec < kMinPointer) continue;
                                Read64(rec + 8, reinterpret_cast<uint64_t*>(&val));
                                LOG_INFO("friendly: ledger rec[%u] key=%llx val=%lld", i,
                                         static_cast<unsigned long long>(key), static_cast<long long>(val));
                            }
                        }
                    }
                }
            }
            g_trustSeeded = true;
        }
    }

    void Friendly::Tick()
    {
        // 2.04.03 (3460, live-verified via the f3460 in-process diagnostic):
        // the actor's sub+0x140 slot now resolves DIRECTLY to
        // ServerFriendlyActorComponent - the authoritative ledger the HUD reads
        // (on 3446 the same slot held the client replica). The 3446 poll and
        // its offsets work unchanged on the new layout, with the writes landing
        // in the ledger itself, so the poll is re-enabled here. The old
        // client-replica revert wound is gone by construction.
        // The ledger writer prologue sig is NOT re-armed on 3460 (the writer
        // moved to 0x142079B40 and the old pattern's uniqueness is unproven);
        // the poll alone carries the feature.
        const bool is3460 = core::GetGameVersion().revision >= 3460;
        if (core::GetGameVersion().revision < 3400) return;
        const State& st = State::Get();
        if (!st.trustMult || st.trustMultVal <= 1.0f)
        {
            // Re-arm the seeding so the next enable snapshots fresh baselines
            // instead of treating accumulated offline gains as one event.
            g_trustSeeded = false;
            if (!st.maxTrust) return;
            // Max Trust alone: sweep without the multiplier poll.
            __try { SweepMaxTrust(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            return;
        }
        if (st.maxTrust)
        {
            __try { SweepMaxTrust(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
        if (g_ledgerTarget)
        {
            MH_DisableHook(g_ledgerTarget);
            MH_RemoveHook(g_ledgerTarget);
            g_ledgerTarget = nullptr;
        }

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
