#include <Windows.h>
#include <DbgHelp.h>
#include <cstdint>
#include <cstdio>
#include "core/mod.h"

#pragma comment(lib, "dbghelp.lib")

static HMODULE g_module = nullptr;

// --- Crash capture ---------------------------------------------------------
// Two layers:
//   1. SetUnhandledExceptionFilter - clean minidump when nothing claims the
//      exception. The game installs its own filter later, which replaces ours,
//      so this alone proved unreliable on TU 2.00.
//   2. A last-chance VECTORED exception handler that logs EVERY first-chance
//      access violation raised by code OUTSIDE this module (ours are the
//      guarded __try reads). The log appends and survives process death, so
//      the LAST line before a crash names the exact faulting instruction
//      (module+offset) - no guessing needed.
static uintptr_t g_modBase = 0;
static uintptr_t g_modEnd  = 0;
static volatile LONG s_dumpsWritten = 0;

static void AppendCrashLine(const char* line)
{
    char log[MAX_PATH];
    if (!GetModuleFileNameA(g_module, log, MAX_PATH)) return;
    char* slash = strrchr(log, '\\');
    if (!slash) return;
    *(slash + 1) = '\0';
    strcat_s(log, "Trinity_Crash.txt");
    HANDLE f = CreateFileA(log, FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SetFilePointer(f, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(f, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(f);
}

struct ModNameOff { char name[MAX_PATH]; uintptr_t off; };

static bool ModuleForRip(uintptr_t rip, ModNameOff* out)
{
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(rip), &mod))
        return false;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(mod, path, MAX_PATH)) return false;
    char* base = strrchr(path, '\\');
    lstrcpynA(out->name, base ? base + 1 : path, MAX_PATH);
    out->off = rip - reinterpret_cast<uintptr_t>(mod);
    return true;
}

static LONG WINAPI VectoredCrashLogger(PEXCEPTION_POINTERS ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;

    const uintptr_t rip = ep->ContextRecord->Rip;
    if (rip >= g_modBase && rip < g_modEnd)   // our own guarded read
        return EXCEPTION_CONTINUE_SEARCH;

    const uintptr_t addr = ep->ExceptionRecord->NumberParameters > 1
                               ? static_cast<uintptr_t>(
                                     ep->ExceptionRecord->ExceptionInformation[1])
                               : 0;
    ModNameOff mo{};
    char where[64];
    if (ModuleForRip(rip, &mo))
        snprintf(where, sizeof(where), "%s+0x%llX", mo.name,
                 static_cast<unsigned long long>(mo.off));
    else
        snprintf(where, sizeof(where), "rip=0x%llX",
                 static_cast<unsigned long long>(rip));

    char msg[224];
    snprintf(msg, sizeof(msg), "AV: code=0x%08lX rip=%s addr=0x%llX\n",
             static_cast<unsigned long>(code), where,
             static_cast<unsigned long long>(addr));
    AppendCrashLine(msg);

    // Keep one full dump of a foreign fault for post-mortem analysis.
    if (InterlockedCompareExchange(&s_dumpsWritten, 1, 0) == 0)
    {
        char dpath[MAX_PATH];
        if (GetModuleFileNameA(g_module, dpath, MAX_PATH))
        {
            char* slash2 = strrchr(dpath, '\\');
            if (slash2) *(slash2 + 1) = '\0';
            strcat_s(dpath, "Trinity_Crash.dmp");
            HANDLE f = CreateFileA(dpath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE)
            {
                MINIDUMP_EXCEPTION_INFORMATION mei{};
                mei.ThreadId          = GetCurrentThreadId();
                mei.ExceptionPointers = ep;
                mei.ClientPointers    = FALSE;
                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                                  MiniDumpNormal, &mei, nullptr, nullptr);
                CloseHandle(f);
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
    AppendCrashLine("UNHANDLED: filter reached\n");
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI MainThread(LPVOID)
{
    trinity::Mod::Get().Initialize(g_module);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_module = module;
        DisableThreadLibraryCalls(module);

        // Record our module span immediately so the VEH filters out our own guarded SEH reads
        if (module)
        {
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
            if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE)
            {
                auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(module) + dos->e_lfanew);
                if (nt && nt->Signature == IMAGE_NT_SIGNATURE)
                {
                    g_modBase = reinterpret_cast<uintptr_t>(module);
                    g_modEnd  = g_modBase + nt->OptionalHeader.SizeOfImage;
                }
            }
        }

        AddVectoredExceptionHandler(0, VectoredCrashLogger); // last-chance
        SetUnhandledExceptionFilter(CrashHandler);
        // Do real work off the loader lock.
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        trinity::Mod::Get().Shutdown();
        break;
    }
    return TRUE;
}
