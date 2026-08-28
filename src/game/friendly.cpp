#include "friendly.h"

#include <cstdint>
#include <vector>
#include <cstring>
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
        // 1. Direct SetNpc and SetPet Leaf Setters (Prologue level)
        // Modifying the source record HERE guarantees 1-SHOT MAX TRUST (1x Greet/Gift/Feed creates the slot directly at 100)
        using FriendlySet_t = void*(__fastcall*)(void* mapOwner, void* record);
        FriendlySet_t oSetNpc = nullptr;
        FriendlySet_t oSetPet = nullptr;
        void* g_npcTarget = nullptr;
        void* g_petTarget = nullptr;

        // 2. Inline Memory Trampoline Sites (Cheat Engine CDtrustA / CDtrustB)
        struct TrustHookSite
        {
            uintptr_t siteAddr = 0;
            uint8_t*  trampMem = nullptr;
        };

        std::vector<TrustHookSite> g_sites;
        uint8_t* g_trampPool = nullptr;

        bool g_hooksInstalled = false;
        bool g_hooksEnabled = false;

        void ApplyMaxTrustToRecord(void* record, const char* srcName)
        {
            if (!Player::Ready()) return; // Never touch records while loading or at main menu

            const uintptr_t r = reinterpret_cast<uintptr_t>(record);
            if (r < kMinPointer) return;

            uint32_t key = 0;
            if (!Read32(r + kOff_FriendlyRec_Key, &key)) return;

            // key == 0 is the save-loader at login / title screen (never touch baseline on load)
            // key != 0 is any active in-game gameplay action (Gift, Greet, Dialogue, Feed, Tame)
            if (key == 0) return;

            // Force 100 (Max Trust) to both 32-bit and 64-bit trust slots (+0x20 and +0x28)
            Write32(r + 0x20, 100);
            Write64(r + 0x20, 100);
            Write32(r + 0x28, 100);
            Write64(r + 0x28, 100);
            LOG_OK("friendly: %s source record forced to 100 (1-Shot Max Trust): key=%u", srcName, key);
        }

        void* __fastcall hkSetNpc(void* mapOwner, void* record)
        {
            const State& st = State::Get();
            if (Player::Ready() && st.trustMult && st.trustMultVal > 1.0f)
            {
                __try
                {
                    ApplyMaxTrustToRecord(record, "SetNpc");
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return oSetNpc(mapOwner, record);
        }

        void* __fastcall hkSetPet(void* mapOwner, void* record)
        {
            const State& st = State::Get();
            if (Player::Ready() && st.trustMult && st.trustMultVal > 1.0f)
            {
                __try
                {
                    ApplyMaxTrustToRecord(record, "SetPet");
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return oSetPet(mapOwner, record);
        }

        void BuildTrampoline(uint8_t* trampMem, uintptr_t returnAddr)
        {
            // Unconditional write of 100 to both +0x20 (dword) and +0x28 (qword)
            const uint8_t prefix[] = {
                0xC5, 0xFC, 0x11, 0x49, 0x20,                               // vmovups [rcx+20h], ymm1
                0xC7, 0x41, 0x20, 0x64, 0x00, 0x00, 0x00,                   // mov dword ptr [rcx+20h], 100
                0x48, 0xC7, 0x41, 0x28, 0x64, 0x00, 0x00, 0x00,             // mov qword ptr [rcx+28h], 100
                0xC5, 0xF8, 0x10, 0x47, 0x40,                               // vmovups xmm0, [rdi+40h]
                0xC5, 0xF8, 0x11, 0x41, 0x40,                               // vmovups [rcx+40h], xmm0
                0xFF, 0x25, 0x00, 0x00, 0x00, 0x00                          // jmp qword ptr [rip+0]
            };

            std::memcpy(trampMem, prefix, sizeof(prefix));
            std::memcpy(trampMem + sizeof(prefix), &returnAddr, sizeof(returnAddr));
        }

        void WriteHook(uintptr_t site, uint8_t* trampMem)
        {
            if (!site || !trampMem) return;

            DWORD oldProt = 0;
            if (VirtualProtect(reinterpret_cast<void*>(site), 15, PAGE_EXECUTE_READWRITE, &oldProt))
            {
                uint8_t jmpBytes[15] = {
                    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, // jmp qword ptr [rip+0]
                    0, 0, 0, 0, 0, 0, 0, 0,             // 8-byte tramp address
                    0x90                                // nop
                };
                uintptr_t addr = reinterpret_cast<uintptr_t>(trampMem);
                std::memcpy(&jmpBytes[6], &addr, sizeof(addr));

                std::memcpy(reinterpret_cast<void*>(site), jmpBytes, 15);
                VirtualProtect(reinterpret_cast<void*>(site), 15, oldProt, &oldProt);
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 15);
            }
        }

        void RestoreHook(uintptr_t site)
        {
            if (!site) return;

            DWORD oldProt = 0;
            if (VirtualProtect(reinterpret_cast<void*>(site), 15, PAGE_EXECUTE_READWRITE, &oldProt))
            {
                std::memcpy(reinterpret_cast<void*>(site), kOrig_FriendlyTrustBytes, 15);
                VirtualProtect(reinterpret_cast<void*>(site), 15, oldProt, &oldProt);
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 15);
            }
        }
    }

    bool Friendly::Install()
    {
        // 1. Install SetNpc and SetPet prologue hooks (1-Shot 1x Action Guarantee)
        const uintptr_t npcAddr = mem::FindPattern(kSig_FriendlySetNpc);
        if (npcAddr)
        {
            g_npcTarget = reinterpret_cast<void*>(npcAddr);
            if (MH_CreateHook(g_npcTarget, reinterpret_cast<void*>(&hkSetNpc), reinterpret_cast<void**>(&oSetNpc)) == MH_OK)
            {
                LOG_OK("friendly: SetNpc 1-Shot hook installed @ 0x%p", g_npcTarget);
                g_hooksInstalled = true;
            }
            else
            {
                g_npcTarget = nullptr;
            }
        }

        const uintptr_t petAddr = mem::FindPattern(kSig_FriendlySetPet);
        if (petAddr)
        {
            g_petTarget = reinterpret_cast<void*>(petAddr);
            if (MH_CreateHook(g_petTarget, reinterpret_cast<void*>(&hkSetPet), reinterpret_cast<void**>(&oSetPet)) == MH_OK)
            {
                LOG_OK("friendly: SetPet 1-Shot hook installed @ 0x%p", g_petTarget);
                g_hooksInstalled = true;
            }
            else
            {
                g_petTarget = nullptr;
            }
        }

        // 2. Scan for all inline Trust write sites
        std::vector<uintptr_t> foundSites;
        const char* kSig_15ByteSite = "C5 FC 11 49 20 C5 F8 10 47 40 C5 F8 11 41 40";

        uintptr_t sA = mem::FindPattern(kSig_FriendlyTrustSiteA);
        if (sA) foundSites.push_back(sA + kOff_FriendlyTrustSiteA_Hook);

        uintptr_t sB = mem::FindPattern(kSig_FriendlyTrustSiteB);
        if (sB) foundSites.push_back(sB + kOff_FriendlyTrustSiteB_Hook);

        uintptr_t sDirect = mem::FindPattern(kSig_15ByteSite);
        if (sDirect)
        {
            bool already = false;
            for (auto s : foundSites) { if (s == sDirect) { already = true; break; } }
            if (!already) foundSites.push_back(sDirect);
        }

        if (!foundSites.empty())
        {
            g_trampPool = reinterpret_cast<uint8_t*>(
                VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)
            );

            if (g_trampPool)
            {
                g_sites.clear();
                for (size_t i = 0; i < foundSites.size(); ++i)
                {
                    uint8_t* tramp = g_trampPool + (i * 0x80);
                    BuildTrampoline(tramp, foundSites[i] + 15);

                    TrustHookSite siteInfo;
                    siteInfo.siteAddr = foundSites[i];
                    siteInfo.trampMem = tramp;
                    g_sites.push_back(siteInfo);

                    LOG_OK("friendly: Inline Trust Site #%zu resolved @ 0x%p",
                           i + 1, reinterpret_cast<void*>(foundSites[i]));
                }
                g_hooksInstalled = true;
            }
        }

        return g_hooksInstalled;
    }

    void Friendly::Tick()
    {
        if (!g_hooksInstalled) return;

        const State& st = State::Get();
        // Strict safety guard: ONLY engage when Player is in-world (Player::Ready())
        const bool wantEnabled = Player::Ready() && st.trustMult && (st.trustMultVal > 1.0f);

        if (wantEnabled != g_hooksEnabled)
        {
            if (wantEnabled)
            {
                if (g_npcTarget) MH_EnableHook(g_npcTarget);
                if (g_petTarget) MH_EnableHook(g_petTarget);
                for (const auto& s : g_sites)
                {
                    WriteHook(s.siteAddr, s.trampMem);
                }
                g_hooksEnabled = true;
                LOG_OK("friendly: 1-Shot 1x Action Max Trust (100) Multi-Tier Injections ENGAGED.");
            }
            else
            {
                if (g_npcTarget) MH_DisableHook(g_npcTarget);
                if (g_petTarget) MH_DisableHook(g_petTarget);
                for (const auto& s : g_sites)
                {
                    RestoreHook(s.siteAddr);
                }
                g_hooksEnabled = false;
                LOG("friendly: 1-Shot 1x Action Max Trust (100) Multi-Tier Injections DISENGAGED.");
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

        for (const auto& s : g_sites)
        {
            RestoreHook(s.siteAddr);
        }
        g_sites.clear();

        if (g_trampPool)
        {
            VirtualFree(g_trampPool, 0, MEM_RELEASE);
            g_trampPool = nullptr;
        }

        g_hooksInstalled = false;
        g_hooksEnabled = false;
    }

    bool Friendly::Ready()
    {
        return g_hooksInstalled;
    }
}
