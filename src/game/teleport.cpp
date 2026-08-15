#include "teleport.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <Xinput.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <MinHook.h>

#include "offsets.h"
#include "player.h"
#include "world.h"
#include "inventory.h"
#include "dye.h"
#include "equipment.h"
#include "../mem/scanner.h"
#include "../mem/safe_memory.h"
#include "../mem/hooks.h"
#include "../hooks/xinput_hook.h"
#include "../core/logger.h"
#include "../core/state.h"

namespace trinity::game
{
    using mem::ReadPtr;
    using mem::Read32;
    using mem::Read8;
    using mem::Write32;
    using mem::ReadCString;
    using mem::ReadVec3;
    using mem::ReadEngineString;

    namespace
    {
        std::atomic<float>    g_posX{0.0f}, g_posY{0.0f}, g_posZ{0.0f};
        std::atomic<bool>     g_posValid{false};

        // sub_3A3E140(rcx=moveController, rdx, r8, r9, stackArg5, stackArg6,
        // stackArg7) - only rcx matters to us; the rest are passed through
        // untouched so the original keeps working exactly as before.
        using MoveUpdate_t = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
        MoveUpdate_t oMoveUpdate = nullptr;
        void* g_moveUpdateTarget = nullptr;

        // --- Fast-travel catalog / trigger ---------------------------------
        // sub_505140(ignored, sceneId, nodeIndex) - the game's own fast travel.
        using TravelFn = char(__fastcall*)(void*, int, unsigned int);
        TravelFn   g_travelFn = nullptr;

        // Data-table resolvers, found by the string-anchored scan in Install()
        // (their bodies are shared template clones - a byte pattern matches ~25
        // sibling resolvers, so each is anchored on its unique table-name
        // string; see offsets.h). Each takes &key and returns the row pointer,
        // lazy-loading the row on first touch => call on the game thread only.
        using TableResolve_t = uintptr_t(__fastcall*)(uint32_t*);
        TableResolve_t g_sceneResolver = nullptr; // LevelGimmickSceneObjectInfo
        uintptr_t      g_registryGlobal = 0;      // qword holding the scene registry ptr
        TableResolve_t g_lvlResolver = nullptr;   // FieldLevelNameTableInfo (area names)
        uintptr_t      g_lvlRegistryGlobal = 0;

        // Locomotion sub-step driver (IDB sub_2F49550) - Super Run's hook
        // point. arg3 (r8) is the drive velocity (f32 x,y,z) the movement
        // servo is about to feed physics; dt rides in xmm1 as a float, so the
        // prototype must declare it to keep the register intact through the
        // trampoline. Everything else is passed through untouched.
        using LocoStep_t = void(__fastcall*)(uintptr_t comp, float dt, float* vel,
                                             uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
        LocoStep_t oLocoStep = nullptr;
        void* g_locoStepTarget = nullptr;

        // A travel request queued from the menu thread, fired once on the game
        // thread inside hkMoveUpdate (matching how the game itself calls it).
        std::atomic<int>  g_pendScene{-1};
        std::atomic<int>  g_pendIndex{-1};
        std::atomic<bool> g_pendValid{false};

        // A node carries its own source sceneId (not just its index) because a
        // curated category can merge nodes from several raw engine scenes -
        // TravelToNode needs the scene the node actually came from.
        struct TpNode { int sceneId = 0; int index = 0; float x = 0.0f, y = 0.0f, z = 0.0f; std::string label; };
        struct TpCategory
        {
            std::string name;
            std::vector<TpNode> nodes;
            // Label-assignment bookkeeping only (running "#N" counters, keyed
            // by whatever prefix that node's LabelMode produces) - build-time
            // scratch state, not part of the public catalog surface.
            std::map<std::string, int> counters;
        };
        std::vector<TpCategory> g_categories;

        // The catalog is built ON THE GAME THREAD (inside hkMoveUpdate): the
        // area-box table lazy-loads rows on first touch, so its resolver must
        // only ever be called where the game itself would call it. The menu
        // thread requests a build and polls the ready flag; g_categories is
        // never mutated again once ready - the whole catalog (every scene in
        // the curated table below) is built in one eager pass.
        std::atomic<bool> g_catalogRequested{false};
        std::atomic<bool> g_catalogReady{false};

        // Named world-space AABBs from FieldLevelNameTableInfo (game data:
        // every level chunk has a name + bounds). Built once, game thread.
        struct AreaBox { float mn[3] = {}; float mx[3] = {}; std::string name; };
        std::vector<AreaBox> g_areaBoxes;
        bool g_areaBoxesBuilt = false;

        // --- Map Marker Teleport state & hooks (clean-room marker capture) --
        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct CandidateSlot
        {
            alignas(8) std::atomic<uint64_t> xyBits{0};
            std::atomic<uint32_t>            zBits{0};
            alignas(4) std::atomic<uint32_t> writer{0};
            std::atomic<uint32_t>            valid{0};
            alignas(8) std::atomic<uint64_t> seq{0};
        };

        struct InlineHook
        {
            uintptr_t target = 0;
            size_t length = 0;
            std::array<uint8_t, 16> original{};
            void* stub = nullptr;
        };

        alignas(8) std::atomic<uintptr_t> g_markerPlayer{0};
        std::atomic<uintptr_t> g_playerMoveOwner{0};
        constexpr uintptr_t    kOff_MoveComp_MoveOwner = 0x298;

        std::array<CandidateSlot, kExpected_MarkerMatches> g_markerCandidates{};
        std::atomic<uint64_t> g_markerProtectFlag{0};
        std::atomic<uint64_t> g_markerProtectDeadline{0};
        uintptr_t g_markerOriginAddress = 0;
        int g_markerCachedCandidate = -1;
        bool g_markerReady = false;
        bool g_markerProtectionReady = false;
        std::vector<InlineHook> g_markerHooks;

        class ThreadSuspender final
        {
        public:
            bool SuspendOthersAvoiding(uintptr_t rangeStart, size_t rangeLength)
            {
                for (unsigned attempt = 0; attempt < 64; ++attempt)
                {
                    if (!CollectAndSuspend()) return false;
                    bool conflict = false;
                    if (rangeLength != 0)
                    {
                        for (HANDLE thread : handles_)
                        {
                            CONTEXT context{};
                            context.ContextFlags = CONTEXT_CONTROL;
                            if (!GetThreadContext(thread, &context))
                            {
                                conflict = true;
                                break;
                            }
                            const uintptr_t rip = static_cast<uintptr_t>(context.Rip);
                            if (rip >= rangeStart && rip < rangeStart + rangeLength)
                            {
                                conflict = true;
                                break;
                            }
                        }
                    }
                    if (!conflict) return true;
                    ResumeAll();
                    Sleep(1);
                }
                return false;
            }

            ~ThreadSuspender()
            {
                ResumeAll();
            }

        private:
            bool CollectAndSuspend()
            {
                const DWORD processId = GetCurrentProcessId();
                const DWORD currentThreadId = GetCurrentThreadId();
                HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                if (snapshot == INVALID_HANDLE_VALUE) return false;

                THREADENTRY32 entry{ sizeof(entry) };
                if (Thread32First(snapshot, &entry))
                {
                    do
                    {
                        if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == currentThreadId)
                            continue;
                        HANDLE thread = OpenThread(
                            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                            FALSE, entry.th32ThreadID);
                        if (thread == nullptr)
                        {
                            const DWORD error = GetLastError();
                            if (error == ERROR_INVALID_PARAMETER) continue;
                            CloseHandle(snapshot);
                            ResumeAll();
                            return false;
                        }
                        handles_.push_back(thread);
                    } while (Thread32Next(snapshot, &entry));
                }
                CloseHandle(snapshot);

                for (HANDLE thread : handles_)
                {
                    if (SuspendThread(thread) == static_cast<DWORD>(-1))
                    {
                        ResumeAll();
                        return false;
                    }
                    ++suspendedCount_;
                }
                return true;
            }

            void ResumeAll()
            {
                while (suspendedCount_ != 0)
                {
                    --suspendedCount_;
                    ResumeThread(handles_[suspendedCount_]);
                }
                for (HANDLE thread : handles_)
                    CloseHandle(thread);
                handles_.clear();
            }

            std::vector<HANDLE> handles_;
            size_t suspendedCount_{ 0 };
        };

        void* AllocateNear(uintptr_t target, size_t size)
        {
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            const uintptr_t granularity = info.dwAllocationGranularity;
            const uintptr_t aligned = target & ~(granularity - 1);
            constexpr uintptr_t kMaxDistance = 0x7FFF0000ULL;

            for (uintptr_t distance = 0; distance <= kMaxDistance; distance += granularity)
            {
                const uintptr_t high = aligned + distance;
                if (high >= aligned)
                {
                    if (void* block = VirtualAlloc(reinterpret_cast<void*>(high), size,
                                                   MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                        return block;
                }
                if (distance != 0 && aligned > distance)
                {
                    const uintptr_t low = aligned - distance;
                    if (void* block = VirtualAlloc(reinterpret_cast<void*>(low), size,
                                                   MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                        return block;
                }
            }
            return nullptr;
        }

        void AppendBytes(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes)
        {
            code.insert(code.end(), bytes.begin(), bytes.end());
        }

        template <typename T>
        void AppendVal(std::vector<uint8_t>& code, T value)
        {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            code.insert(code.end(), bytes, bytes + sizeof(T));
        }

        bool AppendRel32Jump(std::vector<uint8_t>& code, uintptr_t codeBase, uintptr_t destination)
        {
            const uintptr_t instructionEnd = codeBase + code.size() + 5;
            const auto delta = static_cast<int64_t>(destination) - static_cast<int64_t>(instructionEnd);
            if (delta < (std::numeric_limits<int32_t>::min)() || delta > (std::numeric_limits<int32_t>::max)())
                return false;
            code.push_back(0xE9);
            AppendVal(code, static_cast<int32_t>(delta));
            return true;
        }

        bool PatchTarget(InlineHook& hook, const std::vector<uint8_t>& stubCode, const uint8_t* expectedBytes)
        {
            if (hook.length < 5 || hook.length > hook.original.size() || hook.stub == nullptr)
                return false;
            memcpy(hook.original.data(), reinterpret_cast<const void*>(hook.target), hook.length);
            memcpy(hook.stub, stubCode.data(), stubCode.size());

            const auto delta = static_cast<int64_t>(reinterpret_cast<uintptr_t>(hook.stub)) -
                               static_cast<int64_t>(hook.target + 5);
            if (delta < (std::numeric_limits<int32_t>::min)() || delta > (std::numeric_limits<int32_t>::max)())
                return false;

            ThreadSuspender suspension;
            if (!suspension.SuspendOthersAvoiding(hook.target, hook.length))
                return false;
            if (memcmp(reinterpret_cast<const void*>(hook.target), expectedBytes, hook.length) != 0)
                return false;

            DWORD oldProtect = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(hook.target), hook.length,
                                PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;

            auto* target = reinterpret_cast<uint8_t*>(hook.target);
            target[0] = 0xE9;
            const auto relative = static_cast<int32_t>(delta);
            memcpy(target + 1, &relative, sizeof(relative));
            std::fill(target + 5, target + hook.length, static_cast<uint8_t>(0x90));
            FlushInstructionCache(GetCurrentProcess(), target, hook.length);
            DWORD ignored = 0;
            VirtualProtect(target, hook.length, oldProtect, &ignored);
            return true;
        }

        bool InstallMarkerHook(uintptr_t target, CandidateSlot& slot)
        {
            static const uint8_t kExpected[7] = { 0xC5, 0xFB, 0x11, 0x02, 0x8B, 0x47, 0x08 };
            InlineHook hook{ target, 7 };
            if (memcmp(reinterpret_cast<const void*>(target), kExpected, hook.length) != 0)
                return false;

            hook.stub = AllocateNear(target, 160);
            if (hook.stub == nullptr) return false;

            const auto stubBase = reinterpret_cast<uintptr_t>(hook.stub);
            std::vector<uint8_t> code;
            code.push_back(0x9C);                                         // pushfq
            code.push_back(0x51);                                         // push rcx
            AppendBytes(code, { 0x41, 0x53 });                            // push r11
            AppendBytes(code, { 0x49, 0xBB });                            // mov r11, &slot.writer
            AppendVal(code, reinterpret_cast<uintptr_t>(&slot.writer));
            AppendBytes(code, { 0x31, 0xC0 });                            // xor eax, eax
            AppendBytes(code, { 0xB9, 0x01, 0x00, 0x00, 0x00 });         // mov ecx, 1
            AppendBytes(code, { 0xF0, 0x41, 0x0F, 0xB1, 0x0B });         // lock cmpxchg [r11], ecx
            const size_t jumpIfBusy = code.size();
            AppendBytes(code, { 0x75, 0x00 });                            // jne busyReplay
            AppendBytes(code, { 0x41, 0x5B });                            // pop r11
            code.push_back(0x59);                                         // pop rcx
            code.push_back(0x9D);                                         // popfq
            AppendBytes(code, { 0x48, 0x8B, 0x07 });                      // mov rax, [rdi]
            AppendBytes(code, { 0x48, 0xA3 });                            // mov [abs], rax
            AppendVal(code, reinterpret_cast<uintptr_t>(&slot.xyBits));
            AppendBytes(code, { 0x8B, 0x47, 0x08 });                      // mov eax, [rdi+8]
            code.push_back(0xA3);                                         // mov [abs], eax
            AppendVal(code, reinterpret_cast<uintptr_t>(&slot.zBits));
            code.push_back(0x50);                                         // push rax
            AppendBytes(code, { 0xB8, 0x01, 0x00, 0x00, 0x00 });         // mov eax, 1
            code.push_back(0xA3);                                         // mov [abs], eax
            AppendVal(code, reinterpret_cast<uintptr_t>(&slot.valid));
            AppendBytes(code, { 0x48, 0xB8 });                            // mov rax, &slot.seq
            AppendVal(code, reinterpret_cast<uintptr_t>(&slot.seq));
            AppendBytes(code, { 0xF0, 0x48, 0xFF, 0x00 });                // lock inc qword ptr [rax]
            AppendBytes(code, { 0x48, 0xB8 });                            // mov rax, &slot.writer
            AppendVal(code, reinterpret_cast<uintptr_t>(&slot.writer));
            AppendBytes(code, { 0xC7, 0x00, 0x00, 0x00, 0x00, 0x00 });   // mov dword ptr [rax], 0
            code.push_back(0x58);                                         // pop rax
            AppendBytes(code, { 0xC5, 0xFB, 0x11, 0x02 });                // original store XY
            AppendBytes(code, { 0x8B, 0x47, 0x08 });                      // original load Z
            if (!AppendRel32Jump(code, stubBase, target + hook.length))
            {
                VirtualFree(hook.stub, 0, MEM_RELEASE);
                return false;
            }

            const size_t busyReplay = code.size();
            AppendBytes(code, { 0x41, 0x5B });                            // pop r11
            code.push_back(0x59);                                         // pop rcx
            code.push_back(0x9D);                                         // popfq
            AppendBytes(code, { 0xC5, 0xFB, 0x11, 0x02 });                // original store XY
            AppendBytes(code, { 0x8B, 0x47, 0x08 });                      // original load Z
            if (!AppendRel32Jump(code, stubBase, target + hook.length))
            {
                VirtualFree(hook.stub, 0, MEM_RELEASE);
                return false;
            }

            const auto busyDelta = static_cast<int64_t>(busyReplay) - static_cast<int64_t>(jumpIfBusy + 2);
            if (busyDelta < (std::numeric_limits<int8_t>::min)() || busyDelta > (std::numeric_limits<int8_t>::max)())
            {
                VirtualFree(hook.stub, 0, MEM_RELEASE);
                return false;
            }
            code[jumpIfBusy + 1] = static_cast<uint8_t>(static_cast<int8_t>(busyDelta));

            if (!PatchTarget(hook, code, kExpected))
            {
                VirtualFree(hook.stub, 0, MEM_RELEASE);
                return false;
            }
            g_markerHooks.push_back(hook);
            return true;
        }

        bool InstallMarkerPlayerHook(uintptr_t target)
        {
            static const uint8_t kExpected[8] = { 0xC5, 0xF8, 0x11, 0x88, 0xB0, 0x01, 0x00, 0x00 };
            InlineHook hook{ target, 8 };
            if (memcmp(reinterpret_cast<const void*>(target), kExpected, hook.length) != 0)
                return false;

            hook.stub = AllocateNear(target, 128);
            if (hook.stub == nullptr) return false;

            const auto stubBase = reinterpret_cast<uintptr_t>(hook.stub);
            std::vector<uint8_t> code;
            AppendBytes(code, { 0x48, 0xA3 });                            // mov [abs], rax
            AppendVal(code, reinterpret_cast<uintptr_t>(&g_markerPlayer));
            AppendBytes(code, { 0xC5, 0xF8, 0x11, 0x88, 0xB0, 0x01, 0x00, 0x00 }); // original
            if (!AppendRel32Jump(code, stubBase, target + hook.length) ||
                !PatchTarget(hook, code, kExpected))
            {
                VirtualFree(hook.stub, 0, MEM_RELEASE);
                return false;
            }
            g_markerHooks.push_back(hook);
            return true;
        }

        bool InstallMarkerProtectionHook(uintptr_t target)
        {
            static const uint8_t kExpected[7] = { 0x48, 0x8B, 0x46, 0x08, 0x48, 0x89, 0xF1 };
            InlineHook hook{ target, 7 };
            if (memcmp(reinterpret_cast<const void*>(target), kExpected, hook.length) != 0)
                return false;

            hook.stub = AllocateNear(target, 160);
            if (hook.stub == nullptr) return false;

            const auto stubBase = reinterpret_cast<uintptr_t>(hook.stub);
            std::vector<uint8_t> code;
            code.push_back(0x9C);                                         // pushfq
            AppendBytes(code, { 0x48, 0xA1 });                            // mov rax, [abs]
            AppendVal(code, reinterpret_cast<uintptr_t>(&g_markerProtectFlag));
            AppendBytes(code, { 0x48, 0x85, 0xC0 });                      // test rax, rax
            const size_t jumpIfOff = code.size();
            AppendBytes(code, { 0x74, 0x00 });                            // jz replay
            AppendBytes(code, { 0x80, 0x3E, 0x00 });                      // cmp byte ptr [rsi], 0
            const size_t jumpIfBusy = code.size();
            AppendBytes(code, { 0x75, 0x00 });                            // jne replay
            AppendBytes(code, { 0x48, 0x8B, 0x46, 0x18 });                // mov rax, [rsi+18]
            AppendBytes(code, { 0x48, 0x89, 0x46, 0x08 });                // mov [rsi+8], rax
            const size_t replay = code.size();
            code[jumpIfOff + 1] = static_cast<uint8_t>(replay - (jumpIfOff + 2));
            code[jumpIfBusy + 1] = static_cast<uint8_t>(replay - (jumpIfBusy + 2));
            code.push_back(0x9D);                                         // popfq
            AppendBytes(code, { 0x48, 0x8B, 0x46, 0x08 });                // original: mov rax, [rsi+8]
            AppendBytes(code, { 0x48, 0x89, 0xF1 });                      // original: mov rcx, rsi
            if (!AppendRel32Jump(code, stubBase, target + hook.length) ||
                !PatchTarget(hook, code, kExpected))
            {
                VirtualFree(hook.stub, 0, MEM_RELEASE);
                return false;
            }
            g_markerHooks.push_back(hook);
            return true;
        }

        void RemoveMarkerHooks()
        {
            for (auto it = g_markerHooks.rbegin(); it != g_markerHooks.rend(); ++it)
            {
                ThreadSuspender suspension;
                if (!suspension.SuspendOthersAvoiding(it->target, it->length))
                    continue;
                auto* target = reinterpret_cast<uint8_t*>(it->target);
                int32_t relative = 0;
                if (target[0] != 0xE9) continue;
                memcpy(&relative, target + 1, sizeof(relative));
                if (it->target + 5 + relative != reinterpret_cast<uintptr_t>(it->stub))
                    continue;

                DWORD oldProtect = 0;
                if (VirtualProtect(reinterpret_cast<void*>(it->target), it->length,
                                   PAGE_EXECUTE_READWRITE, &oldProtect))
                {
                    memcpy(reinterpret_cast<void*>(it->target), it->original.data(), it->length);
                    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(it->target), it->length);
                    DWORD ignored = 0;
                    VirtualProtect(reinterpret_cast<void*>(it->target), it->length, oldProtect, &ignored);
                }
            }
            g_markerHooks.clear();
        }

        void ServiceProtectionExpiry()
        {
            const uint64_t deadline = g_markerProtectDeadline.load(std::memory_order_relaxed);
            if (deadline != 0 && GetTickCount64() >= deadline)
            {
                g_markerProtectFlag.store(0, std::memory_order_release);
                g_markerProtectDeadline.store(0, std::memory_order_relaxed);
            }
        }

        bool InitMarkerSubsystem()
        {
            g_markerReady = false;
            g_markerProtectionReady = false;
            g_markerProtectDeadline.store(0, std::memory_order_relaxed);
            g_markerPlayer.store(0, std::memory_order_relaxed);
            g_markerProtectFlag.store(0, std::memory_order_relaxed);
            g_markerCachedCandidate = -1;
            for (auto& slot : g_markerCandidates)
            {
                slot.writer.store(0, std::memory_order_relaxed);
                slot.valid.store(0, std::memory_order_relaxed);
            }

            const auto players = mem::FindAllMatches(kSig_MarkerPlayer, 2);
            const auto markers = mem::FindAllMatches(kSig_MarkerPattern, 16);
            const auto origins = mem::FindAllMatches(kSig_MarkerOriginPrefix, 32);
            const auto protections = mem::FindAllMatches(kSig_MarkerProtection, 2);

            if (markers.size() != kExpected_MarkerMatches || origins.size() != kExpected_OriginMatches)
            {
                LOG_WARN("teleport: marker signatures count mismatch (markers=%zu exp=%zu, origins=%zu exp=%zu)",
                         markers.size(), kExpected_MarkerMatches, origins.size(), kExpected_OriginMatches);
                return false;
            }

            uintptr_t origin = 0;
            for (const uintptr_t hit : origins)
            {
                int32_t displacement = 0;
                memcpy(&displacement, reinterpret_cast<const void*>(hit + 4), sizeof(displacement));
                const uintptr_t resolved = hit + 8 + displacement;
                if (origin == 0) origin = resolved;
                else if (resolved != origin)
                {
                    LOG_ERR("teleport: origin address mismatch across prefix matches.");
                    return false;
                }
            }
            g_markerOriginAddress = origin;

            if (players.size() == 1)
            {
                if (!InstallMarkerPlayerHook(players.front() + 3))
                    LOG_WARN("teleport: marker player hook failed (fallback to move-owner).");
            }

            for (size_t i = 0; i < markers.size(); ++i)
            {
                if (!InstallMarkerHook(markers[i] + 4, g_markerCandidates[i]))
                {
                    LOG_ERR("teleport: failed to install marker hook index %zu.", i);
                    RemoveMarkerHooks();
                    return false;
                }
            }

            if (protections.size() == 1)
                g_markerProtectionReady = InstallMarkerProtectionHook(protections.front());

            g_markerReady = true;
            LOG_OK("teleport: map marker teleport subsystem initialized (origin=0x%p, hooks=%zu, protection=%s).",
                   reinterpret_cast<void*>(g_markerOriginAddress), g_markerHooks.size(),
                   g_markerProtectionReady ? "yes" : "no");
            return true;
        }

        bool FiniteCoordinate(const Vec3& value)
        {
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                return false;
            if (std::abs(value.x) > kMarker_CoordLimit || std::abs(value.y) > kMarker_CoordLimit ||
                std::abs(value.z) > kMarker_CoordLimit)
                return false;
            return true;
        }

        bool ValidMarker(const Vec3& value)
        {
            return FiniteCoordinate(value) && (value.x != 0.0f || value.y != 0.0f || value.z != 0.0f);
        }

        bool ReadMarkerCandidate(size_t index, Vec3& value, uint64_t& outSeq)
        {
            if (index >= g_markerCandidates.size()) return false;
            CandidateSlot& slot = g_markerCandidates[index];
            uint32_t expected = 0;
            if (!slot.writer.compare_exchange_strong(expected, 1, std::memory_order_acquire, std::memory_order_relaxed))
                return false;
            const bool valid = (slot.valid.load(std::memory_order_relaxed) != 0);
            const uint64_t xy = slot.xyBits.load(std::memory_order_relaxed);
            const uint32_t z = slot.zBits.load(std::memory_order_relaxed);
            outSeq = slot.seq.load(std::memory_order_relaxed);
            slot.writer.store(0, std::memory_order_release);
            if (!valid) return false;
            memcpy(&value.x, &xy, sizeof(xy));
            memcpy(&value.z, &z, sizeof(z));
            return ValidMarker(value);
        }

        bool FindActiveMarker(Vec3& marker)
        {
            uint64_t bestSeq = 0;
            Vec3 bestMarker{};
            bool found = false;

            for (size_t i = 0; i < g_markerCandidates.size(); ++i)
            {
                Vec3 cand{};
                uint64_t seq = 0;
                if (ReadMarkerCandidate(i, cand, seq))
                {
                    if (!found || seq >= bestSeq)
                    {
                        bestSeq = seq;
                        bestMarker = cand;
                        found = true;
                    }
                }
            }
            if (found)
            {
                marker = bestMarker;
                return true;
            }
            return false;
        }

        // Calls a data-table resolver (game thread only - it lazy-loads the
        // row from the data table on first touch).
        uintptr_t CallTableResolver(TableResolve_t fn, uint32_t key)
        {
            if (!fn) return 0;
            __try { return fn(&key); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        uintptr_t Registry()
        {
            uintptr_t r = 0;
            if (g_registryGlobal && ReadPtr(g_registryGlobal, &r) && r >= kMinPointer) return r;
            return 0;
        }

        // The gimmick object's first std::string (inline or heap) is the node's
        // sector key - the cheap per-node label for the POI browser.
        std::string ReadNodeLabel(uintptr_t gimmick)
        {
            if (gimmick < kMinPointer) return std::string();
            char buf[80];
            uintptr_t p = 0;
            if (ReadPtr(gimmick, &p) && ReadCString(p, buf, sizeof(buf))) return std::string(buf);
            if (ReadCString(gimmick, buf, sizeof(buf))) return std::string(buf);
            return std::string();
        }

        // --- Named area boxes (FieldLevelNameTableInfo) ---------------------

        // Level names that are streaming/technical chunks, not places.
        bool IsNoiseAreaName(const char* s)
        {
            static const char* kSkip[] = {
                "sector_", "fx_", "TerrainHeight_", "RoadLevel", "ExportRoadLevel",
                "TwoLaneTrafficLight", "GameRoadLevel",
            };
            for (const char* p : kSkip)
                if (strncmp(s, p, strlen(p)) == 0) return true;
            return false;
        }

        // Builds the global list of named area boxes. Game thread only.
        void BuildAreaBoxes()
        {
            if (g_areaBoxesBuilt) return;
            g_areaBoxesBuilt = true; // one attempt; partial results are fine
            if (!g_lvlResolver || !g_lvlRegistryGlobal) return;

            uintptr_t reg = 0;
            if (!ReadPtr(g_lvlRegistryGlobal, &reg) || reg < kMinPointer) return;
            uint32_t fieldCount = 0;
            if (!Read32(reg + kOff_Registry_SceneCount, &fieldCount) ||
                fieldCount == 0 || fieldCount > 4096)
                return;

            for (uint32_t f = 0; f < fieldCount; ++f)
            {
                const uintptr_t row = CallTableResolver(g_lvlResolver, f);
                if (row < kMinPointer) continue;

                uint32_t buckets = 0, size = 0;
                if (!Read32(row + kOff_LvlRow_BucketCount, &buckets) ||
                    !Read32(row + kOff_LvlRow_Size, &size))
                    continue;
                if (buckets == 0 || buckets > 100000 || size == 0 || size > 1000000)
                    continue;
                uintptr_t bArr = 0, eArr = 0;
                if (!ReadPtr(row + kOff_LvlRow_Buckets, &bArr) || bArr < kMinPointer) continue;
                if (!ReadPtr(row + kOff_LvlRow_Entries, &eArr) || eArr < kMinPointer) continue;

                for (uint32_t b = 0; b < buckets; ++b)
                {
                    const uintptr_t bucket = bArr + kLvlBucket_Stride * b;
                    uint32_t n = 0;
                    if (!Read32(bucket, &n) || n == 0 || n > 31) continue;
                    for (uint32_t k = 0; k < n; ++k)
                    {
                        uint32_t idx = 0;
                        if (!Read32(bucket + kOff_LvlBucket_Pairs + 8ull * k + 4, &idx)) continue;
                        uintptr_t e = 0;
                        if (!ReadPtr(eArr + 8ull * idx, &e) || e < kMinPointer) continue;

                        uint8_t isSector = 0;
                        if (!Read8(e + kOff_LvlEntry_IsSector, &isSector) || isSector) continue;

                        char name[96];
                        if (!ReadEngineString(e + kOff_LvlEntry_Name, name, sizeof(name))) continue;
                        if (IsNoiseAreaName(name)) continue;

                        float box[6];
                        bool ok = ReadVec3(e + kOff_LvlEntry_Box, box) &&
                                  ReadVec3(e + kOff_LvlEntry_Box + 12, box + 3);
                        if (!ok) continue;
                        bool sane = true;
                        for (float v : box) if (!(v > -200000.0f && v < 200000.0f)) { sane = false; break; }
                        if (!sane || box[0] > box[3] || box[1] > box[4] || box[2] > box[5]) continue;

                        AreaBox ab;
                        ab.mn[0] = box[0]; ab.mn[1] = box[1]; ab.mn[2] = box[2];
                        ab.mx[0] = box[3]; ab.mx[1] = box[4]; ab.mx[2] = box[5];
                        ab.name  = name;
                        g_areaBoxes.push_back(std::move(ab));
                    }
                }
            }
        }

        // Smallest named box containing (x,z), with some vertical slack - the
        // game's own area name for a position.
        const AreaBox* AreaAt(float x, float y, float z)
        {
            const AreaBox* best = nullptr;
            float bestArea = 3.4e38f;
            for (const AreaBox& b : g_areaBoxes)
            {
                if (x < b.mn[0] || x > b.mx[0] || z < b.mn[2] || z > b.mx[2]) continue;
                if (y < b.mn[1] - 64.0f || y > b.mx[1] + 64.0f) continue;
                const float area = (b.mx[0] - b.mn[0]) * (b.mx[2] - b.mn[2]);
                if (area < bestArea) { bestArea = area; best = &b; }
            }
            return best;
        }

        // --- Curated fast-travel menu -----------------------------------------
        // The engine exposes ~150 gimmick scenes and 15k+ nodes (chests, ore
        // veins, quest bells, ...); most are noise. This table is a manual
        // allowlist built from a live raw-string dump: only a scene whose
        // _stringKey appears here is shown at all, and several raw scenes can
        // fold into one merged menu category. Every raw scene key must match
        // the engine's string EXACTLY - a game update that renames one
        // silently drops that scene from the menu until this table is
        // updated to match.
        enum class LabelMode
        {
            RegionMiddle,    // raw "<x>_<Tok>_<digits>" -> "{Region} #n" (per-region counter)
            RegionSuffix,    // raw "..._<Tok>" (last token) -> "{Region} {noun} #n" (per-region counter)
            RegionPrefix,    // raw "{prefix}<Tok>[_digits]" -> "{Region|token} {noun} #n" (per-token counter)
            LastToken,       // raw's last '_'-token, renamed via kBellRenames, no index
            Sequential,      // "{labelBase} #n", plain running counter, raw content ignored
            PerSceneName,    // fixed sceneName; "#n" appended only if the scene has >1 node
            RawPassthrough,  // keep whatever label the existing box/gimmick read already produced
        };

        struct SceneRule
        {
            const char* rawKey;     // exact scene _stringKey to match
            const char* category;   // destination (possibly merged) menu category
            LabelMode   mode;
            const char* noun;       // RegionSuffix / RegionPrefix: trailing word ("Artifact", "Boss")
            const char* prefix;     // RegionPrefix: literal prefix before the token ("BossRematch_")
            const char* sceneName;  // PerSceneName: this scene's fixed label ("Mine Fortress")
            const char* labelBase;  // Sequential: per-node prefix (defaults to `category` if null)
        };

        constexpr SceneRule kSceneRules[] = {
            { "AbyssRuins_Field",                        "Abyss Nexus (Land)",   LabelMode::RegionMiddle, nullptr,    nullptr,        nullptr,            nullptr },
            { "AbyssRuins_AbyssIsland",                  "Abyss Nexus (Abyss)",  LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Abyss Nexus" },
            { "AbyssBridge_AbyssIsland",                 "Abyss Nexus (Abyss)",  LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Abyss Nexus" },
            { "AbyssGate",                                "Abyss Gate",           LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Abyss Gate" },
            { "Puzzle_StandStone",                        "Abyss Cresset",        LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Abyss Cresset" },
            { "Adventure_StandStone",                     "Abyss Cresset",        LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Abyss Cresset" },
            { "Challenge_Sealed_Artifact",                "Sealed Artifact",      LabelMode::RegionSuffix, "Artifact", nullptr,        nullptr,            nullptr },
            { "Quest_Bell",                               "Bells",                LabelMode::LastToken,    nullptr,    nullptr,        nullptr,            nullptr },
            { "Visione_Chip_Boss",                        "Boss Rematch",         LabelMode::RegionPrefix, "Boss",     "BossRematch_", nullptr,            nullptr },
            { "Vision_Chip",                              "Vision Memories",      LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Memory" },
            { "Vision_Chip_Sector",                       "Vision Memories",      LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Memory" },
            { "TreasureBox",                              "Treasure Box",         LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Treasure Box" },
            { "TreasureBox_Sector",                       "Treasure Box",         LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Treasure Box" },
            { "Neut_ATAG_Tunnel_Common",                  "A.T.A.G. Repair Shop", LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Repair Shop" },
            { "Neut_ATAG_Tunnel_Defence",                 "A.T.A.G. Repair Shop", LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Repair Shop" },
            { "Neut_ATAG_Tunnel_Tutorial",                "A.T.A.G. Repair Shop", LabelMode::Sequential,   nullptr,    nullptr,        nullptr,            "Repair Shop" },
            { "Marni_Teleportation_Gate_MineFortress",    "Teleportation Gate",   LabelMode::PerSceneName, nullptr,    nullptr,        "Mine Fortress",    nullptr },
            { "Marni_Teleportation_Gate_MarniLab",        "Teleportation Gate",   LabelMode::PerSceneName, nullptr,    nullptr,        "Marni Lab",        nullptr },
            { "Marni_Teleportation_Gate_BarrierFortress", "Teleportation Gate",   LabelMode::PerSceneName, nullptr,    nullptr,        "Barrier Fortress", nullptr },
            { "Marni_Teleportation_Gate_SecretBase",      "Teleportation Gate",   LabelMode::PerSceneName, nullptr,    nullptr,        "Secret Base",      nullptr },
            { "Marni_Teleportation_Gate_MarniMansion",    "Teleportation Gate",   LabelMode::PerSceneName, nullptr,    nullptr,        "Marni Mansion",    nullptr },
            { "Shop",                                     "Shop",                 LabelMode::RawPassthrough, nullptr, nullptr,        nullptr,            nullptr },

            // Resources - many level-index/variant scenes merge into one material.
            { "MineBlueStone_01", "Azurite Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Azurite Mine" },
            { "MineBlueStone_03", "Azurite Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Azurite Mine" },
            { "MineBlueStone_04", "Azurite Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Azurite Mine" },
            { "MineCopper_01", "Copper Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Copper Mine" },
            { "MineCopper_03", "Copper Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Copper Mine" },
            { "MineCopper_04", "Copper Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Copper Mine" },
            { "MineDiamond_01", "Diamond Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Diamond Mine" },
            { "MineDiamond_02", "Diamond Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Diamond Mine" },
            { "MineDiamond_04", "Diamond Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Diamond Mine" },
            { "MineGreenstone_01", "Epidote Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Epidote Mine" },
            { "MineGreenstone_02", "Epidote Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Epidote Mine" },
            { "MineGreenstone_04", "Epidote Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Epidote Mine" },
            { "MineIron_01", "Iron Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Iron Mine" },
            { "MineIron_02", "Iron Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Iron Mine" },
            { "MineIron_04", "Iron Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Iron Mine" },
            { "MineRedstone_01", "Bloodstone Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Bloodstone Mine" },
            { "MineRedstone_04", "Bloodstone Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Bloodstone Mine" },
            { "MineRuby_01", "Garnet Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Garnet Mine" },
            { "MineRuby_02", "Garnet Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Garnet Mine" },
            { "MineRuby_04", "Garnet Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Garnet Mine" },
            { "MineWhitestone_01", "Scolecite Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Scolecite Mine" },
            { "MineWhitestone_02", "Scolecite Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Scolecite Mine" },
            { "MineWhitestone_04", "Scolecite Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Scolecite Mine" },
            { "MineBismuth_01", "Bismuth Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Bismuth Mine" },
            { "MineBismuth_02", "Bismuth Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Bismuth Mine" },
            { "MineBismuth_04", "Bismuth Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Bismuth Mine" },
            { "SulfurStone_01", "Sulfur Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Sulfur Mine" },
            { "SulfurStone_02", "Sulfur Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Sulfur Mine" },
            { "SulfurStone_03", "Sulfur Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Sulfur Mine" },
            { "SulfurStone_04", "Sulfur Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Sulfur Mine" },
            { "Mercury",  "Mercury",      LabelMode::Sequential, nullptr, nullptr, nullptr, "Mercury" },
            { "Rubber",   "Rubber",       LabelMode::Sequential, nullptr, nullptr, nullptr, "Rubber" },
            { "Jijeongta_leaf", "Palmar Leaf",  LabelMode::Sequential, nullptr, nullptr, nullptr, "Palmar Leaf" },
            { "Opuntia",  "Prickly Pear", LabelMode::Sequential, nullptr, nullptr, nullptr, "Prickly Pear" },
            { "Chaya",    "Chaya",        LabelMode::Sequential, nullptr, nullptr, nullptr, "Chaya" },
            { "Ensete",   "Enset",        LabelMode::Sequential, nullptr, nullptr, nullptr, "Enset" },
            { "Chlorella","Green Algae",  LabelMode::Sequential, nullptr, nullptr, nullptr, "Green Algae" },
            { "Dulse",    "Red Seaweed",  LabelMode::Sequential, nullptr, nullptr, nullptr, "Red Seaweed" },
            { "Amaranth", "Amaranth",     LabelMode::Sequential, nullptr, nullptr, nullptr, "Amaranth" },
            { "Taro",     "Taro",         LabelMode::Sequential, nullptr, nullptr, nullptr, "Taro" },

            { "Vein_Minerals_South_Gold_Levelindex_01", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_South_Gold_Levelindex_04", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_South_Gold_Levelindex_05", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_South_Gold_Levelindex_10", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_North_Gold_Levelindex_02", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_North_Gold_Levelindex_03", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_Desert_Gold_Levelindex_06", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_Desert_Gold_Levelindex_07", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_Desert_Gold_Levelindex_08", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_Desert_Gold_Levelindex_09", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_Desert_Gold_Levelindex_11", "Gold Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Gold Mine" },
            { "Vein_Minerals_North_Silver_Levelindex_61", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_62", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_63", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_64", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_65", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_66", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_68", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_69", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_South_Silver_Levelindex_70", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_Desert_Silver_Levelindex_67", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
            { "Vein_Minerals_Desert_Silver_Levelindex_71", "Silver Mine", LabelMode::Sequential, nullptr, nullptr, nullptr, "Silver Mine" },
        };

        const SceneRule* FindSceneRule(const char* rawKey)
        {
            for (const SceneRule& r : kSceneRules)
                if (strcmp(r.rawKey, rawKey) == 0) return &r;
            return nullptr;
        }

        // A category's on-screen position follows the order its FIRST rule
        // appears in kSceneRules above, not raw engine scan order.
        int CategoryDeclRank(const std::string& name)
        {
            for (size_t i = 0; i < std::size(kSceneRules); ++i)
                if (name == kSceneRules[i].category) return static_cast<int>(i);
            return static_cast<int>(std::size(kSceneRules));
        }

        // Region codes used throughout the raw location strings
        // ("AbyssRuins_Her_0021", "Challenge_Sealed_Artifact_Del", ...).
        const char* PrettyRegionToken(const std::string& tok)
        {
            if (tok == "Her") return "Hernand";
            if (tok == "Dem") return "Demeniss";
            if (tok == "Del") return "Delesyia";
            if (tok == "Kwe") return "Kweiden";
            if (tok == "CD")  return "Crimson Desert";
            return nullptr;
        }

        // Manual per-value renames for LabelMode::LastToken (Bells).
        const char* RenameLastToken(const std::string& tok)
        {
            if (tok == "ScholastoneInstitute") return "Scholastone Institute";
            return nullptr;
        }

        // "AbyssRuins_CD_0001" -> "CD" (token between the 1st and 2nd '_').
        std::string MiddleToken(const std::string& raw)
        {
            const size_t a = raw.find('_');
            if (a == std::string::npos) return std::string();
            const size_t b = raw.find('_', a + 1);
            if (b == std::string::npos) return std::string();
            return raw.substr(a + 1, b - a - 1);
        }

        // "Challenge_Sealed_Artifact_Del" -> "Del" (token after the last '_').
        std::string LastToken(const std::string& raw)
        {
            const size_t p = raw.rfind('_');
            return p == std::string::npos ? raw : raw.substr(p + 1);
        }

        std::string NextIndexed(TpCategory& cat, const std::string& base)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), " #%d", ++cat.counters[base]);
            return base + buf;
        }

        std::string ApplyLabelMode(TpCategory& cat, const SceneRule& rule,
                                   const std::string& raw, uint32_t sceneNodeCount)
        {
            switch (rule.mode)
            {
            case LabelMode::Sequential:
                return NextIndexed(cat, rule.labelBase ? rule.labelBase : cat.name);

            case LabelMode::RegionMiddle:
            {
                const std::string tok = MiddleToken(raw);
                const char* pretty = PrettyRegionToken(tok);
                return NextIndexed(cat, pretty ? pretty : (tok.empty() ? raw : tok));
            }

            case LabelMode::RegionSuffix:
            {
                const std::string tok = LastToken(raw);
                const char* pretty = PrettyRegionToken(tok);
                return NextIndexed(cat, std::string(pretty ? pretty : tok) + " " + rule.noun);
            }

            case LabelMode::RegionPrefix:
            {
                const size_t plen = strlen(rule.prefix);
                std::string tok;
                if (raw.compare(0, plen, rule.prefix) == 0)
                {
                    const std::string rest = raw.substr(plen);
                    const size_t us = rest.find('_');
                    tok = (us == std::string::npos) ? rest : rest.substr(0, us);
                }
                const char* pretty = PrettyRegionToken(tok);
                const std::string name = pretty ? pretty : (tok.empty() ? raw : tok);
                return NextIndexed(cat, name + " " + rule.noun);
            }

            case LabelMode::LastToken:
            {
                const std::string tok = LastToken(raw);
                const char* renamed = RenameLastToken(tok);
                return renamed ? renamed : tok;
            }

            case LabelMode::PerSceneName:
                return sceneNodeCount > 1 ? NextIndexed(cat, rule.sceneName) : std::string(rule.sceneName);

            case LabelMode::RawPassthrough:
            default:
                return raw;
            }
        }

        bool BuildCatalogGameThread()
        {
            uintptr_t reg = Registry();
            if (!reg || !g_sceneResolver) return false;
            uint32_t sceneCount = 0;
            if (!Read32(reg + kOff_Registry_SceneCount, &sceneCount) ||
                sceneCount == 0 || sceneCount > 100000)
                return false;

            BuildAreaBoxes();

            std::vector<TpCategory> cats;

            for (uint32_t s = 0; s < sceneCount; ++s)
            {
                // The resolver (not the raw table slot): it lazy-loads the row,
                // which is exactly why this build runs on the game thread.
                uintptr_t d = CallTableResolver(g_sceneResolver, s);
                if (d < kMinPointer) continue;
                uint32_t nc = 0;
                uintptr_t na = 0;
                if (!Read32(d + kOff_SceneDesc_NodeCount, &nc) || nc == 0 || nc > 1000000) continue;
                if (!ReadPtr(d + kOff_SceneDesc_NodeArray, &na) || na < kMinPointer) continue;

                uint8_t blocked = 0, useTeleport = 0;
                Read8(d + kOff_SceneDesc_IsBlocked, &blocked);
                Read8(d + kOff_SceneDesc_UseTeleport, &useTeleport);
                if (blocked) continue;

                char sceneKey[96];
                if (!ReadEngineString(d + kOff_SceneDesc_StringKey, sceneKey, sizeof(sceneKey)))
                    continue; // no key -> can't match the curated table below

                const SceneRule* rule = FindSceneRule(sceneKey);
                if (!rule) continue; // not on the curated list - drop entirely

                TpCategory* cat = nullptr;
                for (TpCategory& c : cats)
                    if (c.name == rule->category) { cat = &c; break; }
                if (!cat)
                {
                    cats.emplace_back();
                    cat = &cats.back();
                    cat->name = rule->category;
                }

                for (uint32_t i = 0; i < nc; ++i)
                {
                    const uintptr_t node = na + kNode_Stride * i;
                    float pos[3] = { 0.0f, 0.0f, 0.0f };
                    ReadVec3(node + kOff_Node_Position, pos);

                    // Raw per-node label, same source the manual mapping above
                    // was built from: the area-box name for a real fast-travel
                    // (_useTeleport) scene, or the gimmick's own string for a
                    // POI scene.
                    std::string raw;
                    if (useTeleport)
                    {
                        if (const AreaBox* box = AreaAt(pos[0], pos[1], pos[2]))
                            raw = box->name;
                    }
                    else
                    {
                        uintptr_t g = 0;
                        if (ReadPtr(node + kOff_Node_Gimmick, &g)) raw = ReadNodeLabel(g);
                    }
                    if (raw.empty())
                    {
                        char b[24];
                        snprintf(b, sizeof(b), "#%u", i);
                        raw = b;
                    }

                    TpNode n;
                    n.sceneId = static_cast<int>(s);
                    n.index   = static_cast<int>(i);
                    n.x = pos[0]; n.y = pos[1]; n.z = pos[2];
                    n.label   = ApplyLabelMode(*cat, *rule, raw, nc);
                    cat->nodes.push_back(std::move(n));
                }
            }

            if (cats.empty()) return false;

            std::stable_sort(cats.begin(), cats.end(),
                             [](const TpCategory& a, const TpCategory& b)
                             { return CategoryDeclRank(a.name) < CategoryDeclRank(b.name); });

            g_categories = std::move(cats);
            return true;
        }

        // Write a float to game memory by its bit pattern (Write32 takes the
        // raw dword; the proxy velocity fields are plain f32).
        void WriteFloat(uintptr_t addr, float f)
        {
            uint32_t bits = 0;
            memcpy(&bits, &f, sizeof(bits));
            Write32(addr, bits);
        }

        // Super Jump: scale the physics proxy's desired-velocity vector
        // (moveOwner+0xC0) BEFORE the integrator consumes it. Rising-only
        // (positive up-component above the threshold) so falling and walking
        // over steps are never amplified. A jump is a one-shot impulse, not
        // sustained per-frame scaling, so the fast/slow-path heuristic below
        // doesn't meaningfully apply to it.
        void ApplyJumpScaling(uintptr_t moveOwner)
        {
            const State& st = State::Get();
            if (!st.superJump || st.superJumpMult == 1.0f) return;

            const uintptr_t vel = moveOwner + kOff_MoveOwner_DesiredVel;
            float v[3];
            if (!ReadVec3(vel, v)) return;
            if (v[kIdx_MoveOwner_Up] <= kSuperJump_RiseThreshold) return;

            WriteFloat(vel + 4u * kIdx_MoveOwner_Up,
                       v[kIdx_MoveOwner_Up] * st.superJumpMult);
        }

        // Super Run: scale the locomotion servo's DRIVE VELOCITY at its
        // source, before the movement component feeds it to physics (see
        // offsets.h kSig_LocoStepper for why every downstream layer fails:
        // the servo measures the resulting displacement per tick and cancels
        // any velocity injected later). arg3 is the caller's stack vector,
        // rewritten fresh every sub-step - scaling it is stateless, nothing
        // to restore. Horizontal only: gravity rides in the vertical
        // component (~-55 while grounded) and must pass through unscaled.
        // Live-verified (Frida arg3 scaling): smooth at any multiplier,
        // uphill included.
        // Raw SEH-guarded float access for a pointer the game hands us
        // directly as an argument. This deliberately bypasses mem::Read32 /
        // Write32, whose kMinPointer (0x10000000) floor rejects the drive
        // vector outright: the engine passes a scratch buffer at a very low
        // address (~0x013FDD70) and the shared helpers would silently fail,
        // leaving the scale a no-op. That floor exists to reject garbage while
        // walking pointer CHAINS through engine objects; an argument the
        // callee is about to dereference needs crash safety, not a
        // plausibility heuristic.
        bool RawReadFloat(const float* p, float* out)
        {
            __try { *out = *reinterpret_cast<const volatile float*>(p); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool RawWriteFloat(float* p, float v)
        {
            __try { *reinterpret_cast<volatile float*>(p) = v; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        // Live-identified airborne mover (module-relative, IDB imagebase 0).
        // sub_2F4FA90 drives jump/fall/glide and writes the character's velocity
        // through our shared driver, so gating Free Flight on "the caller is
        // inside it" limits altitude control to when the player is genuinely off
        // the ground - no ground super-jump. The ground mover sub_2F4E780 sits
        // just below it and is naturally excluded. (Confirmed live 2026-07-21:
        // standing ret ~2F4F9F9 = ground mover; airborne ret ~2F500AC = here.)
        constexpr uintptr_t kAirMover_Lo = 0x2F4FA90;
        constexpr uintptr_t kAirMover_Hi = 0x2F4FA90 + 0x9A4;  // 0x2F50434

        // True on frames where Free Flight is actively driving the player's
        // vertical velocity (a direction key/button is held while airborne).
        // There is no hover clamp anymore: releasing simply stops writing and
        // hands control straight back to the game's physics, so jumps and aerial
        // attacks are never touched. Published so the HUD can light "FLY".
        std::atomic<bool> g_flightEngaged{false};

        // Current real-pad mask for Free Flight (buttons + trigger sentinels),
        // read on the movement thread. XInputGetState on an empty slot is slow,
        // so the poll is throttled to ~8 ms and backs off for a second after a
        // disconnect. XInputReadReal bypasses the menu-open neutralisation (the
        // caller already gates on !menuOpen), so it sees the true controller.
        unsigned PollFlyPadMask()
        {
            static ULONGLONG s_lastPoll   = 0;
            static ULONGLONG s_nextRetry  = 0;
            static unsigned  s_cachedMask = 0;

            const ULONGLONG now = GetTickCount64();
            if (now - s_lastPoll < 8)
                return s_cachedMask;
            if (s_nextRetry && now < s_nextRetry)
                return 0;
            s_lastPoll = now;

            XINPUT_STATE xs;
            ZeroMemory(&xs, sizeof(xs));
            if (hooks::XInputReadReal(0, &xs) != ERROR_SUCCESS)
            {
                s_nextRetry  = now + 1000; // disconnected - stop hammering the slot
                s_cachedMask = 0;
                return 0;
            }
            s_nextRetry = 0;

            unsigned mask = xs.Gamepad.wButtons;
            if (xs.Gamepad.bLeftTrigger  > 64) mask |= kPadLTrigger;
            if (xs.Gamepad.bRightTrigger > 64) mask |= kPadRTrigger;
            s_cachedMask = mask;
            return mask;
        }

        void __fastcall hkLocoStep(uintptr_t comp, float dt, float* vel,
                                   uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
        {
            const State& st = State::Get();

            // This stepper fires for EVERY character every frame (the mod's
            // highest-frequency hook). Both features it drives are off in the
            // common case, so bail before the player-identity chase and the
            // return-address/air-mover math when neither is on - NPCs then cost
            // nothing. g_flightEngaged only matters while Free Flight is on, so
            // leaving it unchanged here is correct.
            if (!st.superRun && !st.freeFlight)
            {
                oLocoStep(comp, dt, vel, a4, a5, a6, a7);
                return;
            }

            // Is this the local player? The stepper also fires for NPCs, so
            // everything state-gated below must be isolated to the player or it
            // reads/writes the wrong character.
            bool isPlayer = false;
            if (comp)
            {
                const uintptr_t player = g_playerMoveOwner.load(std::memory_order_relaxed);
                uintptr_t owner = 0;
                if (player && ReadPtr(comp + kOff_MoveComp_MoveOwner, &owner) && owner == player)
                    isPlayer = true;
            }

            // Are we airborne? The stepper is a shared helper the ground and air
            // movers both call, so the CALLER - not any field on the component -
            // identifies the mode: the component's own state fields turned out to
            // be either a constant class id (comp+0x190, live-read 27 in every
            // air state) or a plain "airborne" flag (comp+0x4E2, set for jump and
            // glide alike), neither of which separates a glide from a jump - that
            // distinction lives on the actor's state controller, not on this
            // object. So we turn the return address into a module offset and
            // check whether it lands in the airborne mover's range. Since Free
            // Flight only ever acts while a direction is held, that is all the
            // gating we need: a jump or aerial attack the player isn't steering
            // is left completely untouched.
            bool inAirMover = false;
            if (isPlayer)
            {
                static const uintptr_t base =
                    reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                const uintptr_t off =
                    reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
                inAirMover = off >= kAirMover_Lo && off < kAirMover_Hi;
            }

            if (st.superRun && st.superRunMult != 1.0f && vel)
            {
                float x = 0.0f, z = 0.0f; // vel[0]=x, vel[1]=up, vel[2]=z
                if (RawReadFloat(vel, &x) && RawReadFloat(vel + 2, &z))
                {
                    RawWriteFloat(vel,     x * st.superRunMult);
                    RawWriteFloat(vel + 2, z * st.superRunMult);
                }
            }

            // Free Flight: drive the vertical velocity (vel[1]) while airborne,
            // but ONLY while the player is actively holding a direction. With
            // neither key held (or both) we write nothing at all - no hover
            // clamp - so the game's own physics run untouched: glide sinks,
            // jumps arc, and aerial attacks land normally. Local-player +
            // air-mover only, so it never disturbs grounded movement or NPCs.
            // Suspended while the menu / text capture is up.
            bool flyingNow = false;
            if (st.freeFlight && vel && inAirMover && !st.menuOpen && !st.textCapture)
            {
                bool up   = st.flyUpKeyVk   != 0 && (GetAsyncKeyState(st.flyUpKeyVk)   & 0x8000) != 0;
                bool down = st.flyDownKeyVk != 0 && (GetAsyncKeyState(st.flyDownKeyVk) & 0x8000) != 0;

                // Controller: the mask must be held in full (a single button or
                // a combo), matching the keyboard binds. Only polled when a pad
                // mask is actually configured, so keyboard-only players pay
                // nothing.
                if (st.flyUpPadMask || st.flyDownPadMask)
                {
                    const unsigned pad = PollFlyPadMask();
                    if (st.flyUpPadMask   && (pad & st.flyUpPadMask)   == st.flyUpPadMask)   up   = true;
                    if (st.flyDownPadMask && (pad & st.flyDownPadMask) == st.flyDownPadMask) down = true;
                }

                flyingNow = up ^ down; // exactly one direction held
                if (flyingNow)
                    RawWriteFloat(vel + 1, up ? st.flightSpeed : -st.flightSpeed);
            }
            if (isPlayer)
                g_flightEngaged.store(flyingNow, std::memory_order_relaxed);

            oLocoStep(comp, dt, vel, a4, a5, a6, a7);
        }

        uint64_t __fastcall hkMoveUpdate(uint64_t moveOwner, uint64_t a2, uint64_t a3, uint64_t a4,
                                          uint64_t a5, uint64_t a6, uint64_t a7)
        {
            const uintptr_t owner = static_cast<uintptr_t>(moveOwner);

            // Publish the local player's move-owner so the loco-stepper can tell
            // the player apart from the NPCs it also fires for (Free Flight gates
            // on this). This hook tracks the player's own position, so its
            // moveOwner is the player's proxy.
            g_playerMoveOwner.store(owner, std::memory_order_relaxed);

            // Super Jump scales the desired velocity before the integrator
            // reads it (+0xC0). (Super Run lives upstream, in hkLocoStep.)
            ApplyJumpScaling(owner);

            const uint64_t result = oMoveUpdate(moveOwner, a2, a3, a4, a5, a6, a7);

            // Per-frame, game-thread driver for the churn-proof player resolve:
            // refresh the current-player stat entries from a fresh char-manager
            // walk so god mode / infinite stamina / spirit always target the
            // live player (this is the movement tick the mod already owns).
            Player::RefreshSelf();

            // Apply Game Speed here too: the fixed-timestep override must be
            // held on the game thread, once per frame, same as the resolve.
            World::Tick();

            // Slot Size / Max Stack Size table overrides: same reasoning as
            // Game Speed - held/retried on the game thread, not the render one.
            Inventory::Tick();

            // Run a queued armor-dye apply (calls engine code, so it must be
            // here on the game thread, same as the inventory add path).
            Dye::Tick();

            // Re-apply equipped effects after an abyss-gear socket edit (same
            // engine pass, same game-thread requirement as the dye apply).
            Equipment::Tick();

            // Expire marker teleport temporary fall protection
            ServiceProtectionExpiry();

            // Fire a queued fast-travel on the game thread (matching the game).
            if (g_pendValid.load(std::memory_order_acquire) && g_travelFn)
            {
                const int scene = g_pendScene.load(std::memory_order_relaxed);
                const int index = g_pendIndex.load(std::memory_order_relaxed);
                g_pendValid.store(false, std::memory_order_release);
                if (scene >= 0 && index >= 0)
                {
                    __try { g_travelFn(nullptr, scene, static_cast<unsigned int>(index)); }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // Build the catalog here: the area-name table lazy-loads its rows,
            // so this must run where the game itself would run it.
            if (g_catalogRequested.load(std::memory_order_acquire) &&
                !g_catalogReady.load(std::memory_order_acquire))
            {
                if (BuildCatalogGameThread())
                {
                    g_catalogReady.store(true, std::memory_order_release);
                    g_catalogRequested.store(false, std::memory_order_release);
                }
                else
                {
                    // Registry not up yet (not in-world). Retry on a later tick;
                    // clear the request so the menu re-arms it while open.
                    g_catalogRequested.store(false, std::memory_order_release);
                }
            }

            float pos[3];
            if (ReadVec3(static_cast<uintptr_t>(moveOwner) + kOff_MoveOwner_Position, pos))
            {
                g_posX.store(pos[0], std::memory_order_relaxed);
                g_posY.store(pos[1], std::memory_order_relaxed);
                g_posZ.store(pos[2], std::memory_order_relaxed);
                g_posValid.store(true, std::memory_order_relaxed);
            }
            return result;
        }

        // --- Data-table resolver discovery -----------------------------------
        // The resolver body is a template clone shared by ~25 data-table
        // resolvers, so we anchor on the unique table-name string instead:
        // find the `lea r8, "<TableName>"` (the lazy-load path passes the
        // table name), then walk back to the clone's prologue.
        //
        // Each string has SEVERAL `lea r8` references (the table-open helper
        // and sibling functions reference it too), so the prologue check is
        // part of the match test: only the lea that sits INSIDE the resolver
        // clone has the prologue within range above it.
        //
        // Two clone variants exist, differing only in the key load: 32-bit
        // keys (`mov edi,[rcx]`, most tables) and 16-bit keys
        // (`movzx edi, word ptr [rcx]`, e.g. iteminfo and buffinfo); the
        // trailing `mov rbx, cs:<registry>` we resolve shifts by the one-byte
        // difference. No current caller uses key16 (Super Run once did); it
        // stays for the u16-keyed tables the buff work will need.
        uintptr_t FindResolverPrologueAbove(uintptr_t lea, bool key16)
        {
            // Concrete prologue bytes of the resolver clone (kSig_TableResolverPrologue).
            static const uint8_t kPrologue32[] = {
                0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
                0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x8B, 0x39,
                0x48, 0x8B, 0x1D,
            };
            static const uint8_t kPrologue16[] = {
                0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
                0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x0F, 0xB7, 0x39,
                0x48, 0x8B, 0x1D,
            };
            static const uint8_t kPrologueShort[] = {
                0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
                0x56, 0x57, 0x41, 0x56
            };
            const uint8_t* pro = key16 ? kPrologue16 : kPrologue32;
            const size_t   len = key16 ? sizeof(kPrologue16) : sizeof(kPrologue32);
            for (size_t back = 0x20; back <= kMax_LeaToPrologue; ++back)
            {
                const uintptr_t cand = lea - back;
                bool hit = true;
                __try
                {
                    for (size_t i = 0; i < len; ++i)
                        if (*reinterpret_cast<const uint8_t*>(cand + i) != pro[i]) { hit = false; break; }
                    if (!hit)
                    {
                        hit = true;
                        for (size_t i = 0; i < sizeof(kPrologueShort); ++i)
                            if (*reinterpret_cast<const uint8_t*>(cand + i) != kPrologueShort[i]) { hit = false; break; }
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) { hit = false; }
                if (hit) return cand;
            }
            return 0;
        }

        struct TableScan { const char* tableName; bool key16; uintptr_t fn; };

        bool LeaIsResolverTableRef(uintptr_t match, void* ctx)
        {
            TableScan* scan = static_cast<TableScan*>(ctx);
            const uintptr_t target = mem::ResolveRipAt(match, 7);
            char buf[40];
            if (!ReadCString(target, buf, sizeof(buf))) return false;
            if (strcmp(buf, scan->tableName) != 0) return false;
            const uintptr_t fn = FindResolverPrologueAbove(match, scan->key16);
            if (!fn) return false; // right string, wrong function - keep scanning
            scan->fn = fn;
            return true;
        }

        bool ResolveTableResolver(const char* tableName, TableResolve_t* fnOut, uintptr_t* globalOut,
                                  bool key16 = false)
        {
            TableScan scan{ tableName, key16, 0 };
            mem::FindPatternIf(kSig_LeaR8Rip, &LeaIsResolverTableRef, &scan);
            if (!scan.fn) return false;
            const uintptr_t movOff = key16 ? kOff_ItemResolver_MovGlobal : kOff_TableResolver_MovGlobal;
            *fnOut     = reinterpret_cast<TableResolve_t>(scan.fn);
            *globalOut = mem::ResolveRipAt(scan.fn + movOff, kLen_MovGlobalInstr);
            return *globalOut != 0;
        }
    }

    bool Teleport::Install()
    {
        if (!mem::InstallHook("teleport: movement-update", kSig_MoveUpdate, "position tracking disabled",
                              &hkMoveUpdate, &oMoveUpdate, &g_moveUpdateTarget))
            return false;

        // Resolve the fast-travel trigger + the destination registry global.
        // Non-fatal if missing: position tracking still works, the fast-travel
        // menu just stays empty (logged).
        if (const uintptr_t travel = mem::FindPattern(kSig_TravelToNode))
        {
            if (mem::CountMatches(kSig_TravelToNode, 2) != 1)
                LOG_WARN("teleport: fast-travel signature ambiguous; using first match.");
            g_travelFn = reinterpret_cast<TravelFn>(travel);
        }
        else
        {
            LOG_ERR("teleport: fast-travel trigger signature NOT FOUND - fast-travel menu disabled.");
        }

        if (!ResolveTableResolver(kStr_GimmickSceneTable, &g_sceneResolver, &g_registryGlobal))
            LOG_ERR("teleport: scene-registry resolver NOT FOUND - fast-travel menu disabled.");

        // Named area boxes for waypoint labels (optional - degrades gracefully).
        if (!ResolveTableResolver(kStr_LevelNameTable, &g_lvlResolver, &g_lvlRegistryGlobal))
            LOG_WARN("teleport: area-name resolver not found - waypoint names fall back to indices.");

        // Locomotion sub-step driver for Super Run (optional - Super Jump and
        // everything else still works without it).
        mem::InstallHook("teleport: locomotion-stepper", kSig_LocoStepper, "Super Run disabled",
                         &hkLocoStep, &oLocoStep, &g_locoStepTarget);

        // Map Marker Teleport subsystem (clean-room marker capture from crimsondesert-main).
        InitMarkerSubsystem();

        return true;
    }

    void Teleport::Remove()
    {
        RemoveMarkerHooks();
        g_markerReady = false;
        mem::RemoveHook(&g_locoStepTarget);
        mem::RemoveHook(&g_moveUpdateTarget);
        g_posValid.store(false, std::memory_order_relaxed);
    }

    bool Teleport::GetLastPosition(float* x, float* y, float* z)
    {
        if (!g_posValid.load(std::memory_order_relaxed)) return false;
        *x = g_posX.load(std::memory_order_relaxed);
        *y = g_posY.load(std::memory_order_relaxed);
        *z = g_posZ.load(std::memory_order_relaxed);
        return true;
    }

    bool Teleport::GetFlightEngaged()
    {
        return g_flightEngaged.load(std::memory_order_relaxed);
    }

    bool Teleport::CopyPositionToClipboard()
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!GetLastPosition(&x, &y, &z)) return false;

        char text[96];
        const int len = snprintf(text, sizeof(text), "%.2f %.2f %.2f", x, y, z);
        if (len <= 0) return false;
        const size_t bytes = static_cast<size_t>(len) + 1; // include the terminator

        if (!OpenClipboard(nullptr)) return false;

        bool ok = false;
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem)
        {
            if (void* dst = GlobalLock(mem))
            {
                memcpy(dst, text, bytes);
                GlobalUnlock(mem);
                EmptyClipboard();
                ok = SetClipboardData(CF_TEXT, mem) != nullptr;
            }
            if (!ok) GlobalFree(mem); // ownership only transfers to the clipboard on success
        }
        CloseClipboard();
        return ok;
    }

    // --- Fast travel / map-gimmick catalog ---------------------------------
    bool Teleport::LoadCatalog()
    {
        if (g_catalogReady.load(std::memory_order_acquire)) return true;
        if (!g_registryGlobal || !g_sceneResolver) return false; // discovery failed
        // Ask the game thread to build it; ready on a later frame.
        g_catalogRequested.store(true, std::memory_order_release);
        return false;
    }

    bool Teleport::CatalogReady()
    {
        return g_catalogReady.load(std::memory_order_acquire);
    }

    size_t Teleport::CategoryCount()
    {
        if (!CatalogReady()) return 0;
        return g_categories.size();
    }

    bool Teleport::GetCategory(size_t cat, const char** name, size_t* nodeCount)
    {
        if (!CatalogReady() || cat >= g_categories.size()) return false;
        *name = g_categories[cat].name.c_str();
        *nodeCount = g_categories[cat].nodes.size();
        return true;
    }

    // The catalog is built whole in one eager pass (BuildCatalogGameThread),
    // so by the time it's ready every category's nodes already exist.
    bool Teleport::EnsureCategoryNodes(size_t cat)
    {
        return CatalogReady() && cat < g_categories.size();
    }

    size_t Teleport::NodeCount(size_t cat)
    {
        if (!CatalogReady() || cat >= g_categories.size()) return 0;
        return g_categories[cat].nodes.size();
    }

    bool Teleport::GetNode(size_t cat, size_t node, const char** label, float* x, float* y, float* z)
    {
        if (!CatalogReady() || cat >= g_categories.size()) return false;
        const TpCategory& c = g_categories[cat];
        if (node >= c.nodes.size()) return false;
        *label = c.nodes[node].label.c_str();
        *x = c.nodes[node].x;
        *y = c.nodes[node].y;
        *z = c.nodes[node].z;
        return true;
    }

    bool Teleport::TravelToNode(size_t cat, size_t node)
    {
        if (!g_travelFn || !CatalogReady() || cat >= g_categories.size()) return false;
        const TpCategory& c = g_categories[cat];
        if (node >= c.nodes.size()) return false;

        g_pendScene.store(c.nodes[node].sceneId, std::memory_order_relaxed);
        g_pendIndex.store(c.nodes[node].index, std::memory_order_relaxed);
        g_pendValid.store(true, std::memory_order_release);
        return true;
    }

    // --- Map Marker Teleport ------------------------------------------------
    bool Teleport::MarkerReady()
    {
        return g_markerReady;
    }

    bool Teleport::HasMarker()
    {
        Vec3 marker{};
        return FindActiveMarker(marker);
    }

    bool Teleport::GetMarkerPosition(float* x, float* y, float* z)
    {
        Vec3 marker{};
        if (!FindActiveMarker(marker)) return false;
        if (x) *x = marker.x;
        if (y) *y = marker.y;
        if (z) *z = marker.z;
        return true;
    }

    Teleport::MarkerStatus Teleport::TeleportToMarker(float fallbackHeight)
    {
        if (!g_markerReady)
            return MarkerStatus::NotReady;

        const uintptr_t moveOwner = g_playerMoveOwner.load(std::memory_order_acquire);
        const uintptr_t markerPlayer = g_markerPlayer.load(std::memory_order_acquire);
        if (moveOwner < kMinPointer && markerPlayer < kMinPointer)
            return MarkerStatus::NoPlayer;

        Vec3 marker{};
        if (!FindActiveMarker(marker))
            return MarkerStatus::NoMarker;

        Vec3 origin{};
        if (g_markerOriginAddress == 0 || !mem::ReadVec3(g_markerOriginAddress, &origin.x) || !FiniteCoordinate(origin))
            return MarkerStatus::InvalidCoordinates;

        const bool usesFallbackHeight = (marker.y == 0.0f);
        const float height = usesFallbackHeight ? (fallbackHeight > 0.0f ? fallbackHeight : 1200.0f) : marker.y;
        const Vec3 destination{
            marker.x - origin.x,
            height + kMarker_DestLift,
            marker.z - origin.z,
        };
        if (!FiniteCoordinate(destination))
            return MarkerStatus::InvalidCoordinates;

        if (usesFallbackHeight)
        {
            g_markerProtectFlag.store(1, std::memory_order_release);
            g_markerProtectDeadline.store(GetTickCount64() + 3500, std::memory_order_relaxed);
        }
        else
        {
            g_markerProtectFlag.store(0, std::memory_order_release);
            g_markerProtectDeadline.store(0, std::memory_order_relaxed);
        }

        bool wrote = false;
        __try
        {
            if (moveOwner >= kMinPointer)
            {
                *reinterpret_cast<Vec3*>(moveOwner + kOff_Player_Dest0) = destination;
                *reinterpret_cast<Vec3*>(moveOwner + kOff_Player_Dest1) = destination;
                // Zero out mid-air momentum and falling velocity to eliminate drift
                *reinterpret_cast<Vec3*>(moveOwner + 0xC0) = Vec3{ 0.0f, 0.0f, 0.0f };
                wrote = true;
            }
            if (markerPlayer >= kMinPointer && markerPlayer != moveOwner)
            {
                *reinterpret_cast<Vec3*>(markerPlayer + kOff_Player_Dest0) = destination;
                *reinterpret_cast<Vec3*>(markerPlayer + kOff_Player_Dest1) = destination;
                wrote = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            wrote = false;
        }

        if (!wrote)
        {
            g_markerProtectFlag.store(0, std::memory_order_release);
            g_markerProtectDeadline.store(0, std::memory_order_relaxed);
            return MarkerStatus::WriteFailed;
        }

        return MarkerStatus::Success;
    }
}
