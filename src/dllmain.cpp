#include <Windows.h>
#include <DbgHelp.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include "core/mod.h"
#include "core/state.h"
#include "core/version.h"

#pragma comment(lib, "dbghelp.lib")

static HMODULE g_module = nullptr;
static uintptr_t g_modBase = 0;
static uintptr_t g_modEnd  = 0;
static volatile LONG s_dumpsWritten = 0;

static void AppendCrashReport(const char* report)
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
    WriteFile(f, report, static_cast<DWORD>(strlen(report)), &written, nullptr);
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

static const char* GetExceptionCodeName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
    default:                                 return "UNKNOWN_EXCEPTION";
    }
}

static void LogCallStack(CONTEXT ctx, char* buf, size_t bufSize)
{
    STACKFRAME64 frame{};
    frame.AddrPC.Offset    = ctx.Rip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode   = AddrModeFlat;

    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    int count = 0;
    while (count < 16 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread,
                                     &frame, &ctx, nullptr,
                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
    {
        if (frame.AddrPC.Offset == 0) break;

        ModNameOff mo{};
        char frameDesc[128];
        if (ModuleForRip(frame.AddrPC.Offset, &mo))
            snprintf(frameDesc, sizeof(frameDesc), "  [%02d] %s+0x%llX\n", count, mo.name, static_cast<unsigned long long>(mo.off));
        else
            snprintf(frameDesc, sizeof(frameDesc), "  [%02d] 0x%016llX\n", count, static_cast<unsigned long long>(frame.AddrPC.Offset));

        strcat_s(buf, bufSize, frameDesc);
        ++count;
    }
}

static LONG WINAPI VectoredCrashLogger(PEXCEPTION_POINTERS ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;

    const uintptr_t rip = ep->ContextRecord->Rip;
    if (g_modBase != 0 && rip >= g_modBase && rip < g_modEnd)
        return EXCEPTION_CONTINUE_SEARCH;

    HMODULE faultMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(rip), &faultMod))
    {
        if (faultMod == g_module)
            return EXCEPTION_CONTINUE_SEARCH;
    }

    const ULONG_PTR accessType = ep->ExceptionRecord->NumberParameters > 0
                                     ? ep->ExceptionRecord->ExceptionInformation[0]
                                     : 0;
    const uintptr_t targetAddr = ep->ExceptionRecord->NumberParameters > 1
                                     ? static_cast<uintptr_t>(
                                           ep->ExceptionRecord->ExceptionInformation[1])
                                     : 0;

    ModNameOff mo{};
    char where[128];
    if (ModuleForRip(rip, &mo))
        snprintf(where, sizeof(where), "%s+0x%llX", mo.name, static_cast<unsigned long long>(mo.off));
    else
        snprintf(where, sizeof(where), "rip=0x%016llX", static_cast<unsigned long long>(rip));

    // Timestamp
    time_t rawtime;
    struct tm timeinfo;
    time(&rawtime);
    localtime_s(&timeinfo, &rawtime);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

    // Human-readable access type
    const char* opDesc = "Unknown Operation";
    if (code == EXCEPTION_ACCESS_VIOLATION)
    {
        if (accessType == 0)      opDesc = "Read Access Violation (Reading invalid address)";
        else if (accessType == 1) opDesc = "Write Access Violation (Writing to invalid address)";
        else if (accessType == 8) opDesc = "DEP Violation (Data Execution Prevention violation)";
    }

    // Diagnostic Cause Analysis
    char diag[512];
    if (code == EXCEPTION_ACCESS_VIOLATION)
    {
        if (targetAddr == 0 || targetAddr < 0x10000)
        {
            snprintf(diag, sizeof(diag),
                     "Cause    : Null Pointer Dereference (attempted to access address 0x%016llX).\n"
                     "Analysis : Object or pointer is null/uninitialized when accessed by %s.",
                     static_cast<unsigned long long>(targetAddr), where);
        }
        else if (targetAddr > 0x00007FFFFFFFFFFFULL || targetAddr == 0xFFFFFFFFFFFFFFFFULL)
        {
            snprintf(diag, sizeof(diag),
                     "Cause    : Wild / Invalid Pointer Access (attempted to access invalid address 0x%016llX).\n"
                     "Analysis : Memory overflow, corrupted register, or negative array index (-1) in %s.",
                     static_cast<unsigned long long>(targetAddr), where);
        }
        else if (mo.name[0] && _stricmp(mo.name, "CrimsonDesert.exe") == 0)
        {
            snprintf(diag, sizeof(diag),
                     "Cause    : Game Engine Exception in %s while accessing memory 0x%016llX.\n"
                     "Analysis : Game engine thread attempted to access unconstructed, freed, or out-of-bounds entity data.",
                     where, static_cast<unsigned long long>(targetAddr));
        }
        else
        {
            snprintf(diag, sizeof(diag),
                     "Cause    : Module Fault in %s while accessing address 0x%016llX.\n"
                     "Analysis : Instruction execution in %s raised an Access Violation.",
                     where, static_cast<unsigned long long>(targetAddr), where);
        }
    }
    else if (code == EXCEPTION_ILLEGAL_INSTRUCTION)
    {
        snprintf(diag, sizeof(diag),
                 "Cause    : Illegal Instruction in %s.\n"
                 "Analysis : CPU attempted to execute invalid or corrupted machine opcode.", where);
    }
    else if (code == EXCEPTION_STACK_OVERFLOW)
    {
        snprintf(diag, sizeof(diag),
                 "Cause    : Stack Overflow in %s.\n"
                 "Analysis : Thread stack quota exhausted due to deep recursion or excessive local frame allocation.", where);
    }
    else
    {
        snprintf(diag, sizeof(diag), "Cause    : Unhandled Exception Code 0x%08lX in %s.", code, where);
    }

    const CONTEXT* cx = ep->ContextRecord;
    const trinity::State& st = trinity::State::Get();

    char report[4096];
    snprintf(report, sizeof(report),
        "\n================================================================================\n"
        "CRIMSON DESERT - TRINITY CRASH REPORT\n"
        "================================================================================\n"
        "Timestamp   : %s\n"
        "Mod Version : Trinity v%s\n"
        "Process ID  : %lu\n"
        "Thread ID   : %lu\n\n"
        "--- EXCEPTION DETAILS ---\n"
        "Exception Code : 0x%08lX (%s)\n"
        "Fault Address  : %s (0x%016llX)\n"
        "Target Address : 0x%016llX\n"
        "Access Type    : %s\n\n"
        "--- DIAGNOSTIC SUMMARY ---\n"
        "%s\n\n"
        "--- CPU REGISTERS (x64) ---\n"
        "  RAX: 0x%016llX  RBX: 0x%016llX  RCX: 0x%016llX\n"
        "  RDX: 0x%016llX  RSI: 0x%016llX  RDI: 0x%016llX\n"
        "  R8 : 0x%016llX  R9 : 0x%016llX  R10: 0x%016llX\n"
        "  R11: 0x%016llX  R12: 0x%016llX  R13: 0x%016llX\n"
        "  R14: 0x%016llX  R15: 0x%016llX\n"
        "  RSP: 0x%016llX  RBP: 0x%016llX  RIP: 0x%016llX\n"
        "  EFLAGS: 0x%08lX\n\n"
        "--- CALL STACK BACKTRACE ---\n",
        timeStr,
        TRINITY_VERSION,
        GetCurrentProcessId(),
        GetCurrentThreadId(),
        code, GetExceptionCodeName(code),
        where, static_cast<unsigned long long>(rip),
        static_cast<unsigned long long>(targetAddr),
        opDesc,
        diag,
        static_cast<unsigned long long>(cx->Rax), static_cast<unsigned long long>(cx->Rbx), static_cast<unsigned long long>(cx->Rcx),
        static_cast<unsigned long long>(cx->Rdx), static_cast<unsigned long long>(cx->Rsi), static_cast<unsigned long long>(cx->Rdi),
        static_cast<unsigned long long>(cx->R8),  static_cast<unsigned long long>(cx->R9),  static_cast<unsigned long long>(cx->R10),
        static_cast<unsigned long long>(cx->R11), static_cast<unsigned long long>(cx->R12), static_cast<unsigned long long>(cx->R13),
        static_cast<unsigned long long>(cx->R14), static_cast<unsigned long long>(cx->R15),
        static_cast<unsigned long long>(cx->Rsp), static_cast<unsigned long long>(cx->Rbp), static_cast<unsigned long long>(cx->Rip),
        cx->EFlags
    );

    // Call stack
    CONTEXT ctxCopy = *cx;
    LogCallStack(ctxCopy, report, sizeof(report));

    char stateBuf[512];
    snprintf(stateBuf, sizeof(stateBuf),
        "\n--- ACTIVE MOD FEATURES ---\n"
        "  GodMode: %s | InfStamina: %s | InfMountStamina: %s | InfSpirit: %s\n"
        "  OneHitKill: %s | EasyParry: %s | NoBounty: %s | NoFallDamage: %s\n"
        "  Damage Mult Out: %.1fx | Damage Mult In: %.1fx\n"
        "================================================================================\n\n",
        st.godMode ? "ON" : "OFF",
        st.infStamina ? "ON" : "OFF",
        st.infMountStamina ? "ON" : "OFF",
        st.infSpirit ? "ON" : "OFF",
        st.oneHitKill ? "ON" : "OFF",
        st.easyParry ? "ON" : "OFF",
        st.noBounty ? "ON" : "OFF",
        st.noFallDamage ? "ON" : "OFF",
        st.dmgOutMult,
        st.dmgInMult
    );
    strcat_s(report, sizeof(report), stateBuf);

    AppendCrashReport(report);

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
    AppendCrashReport("UNHANDLED EXCEPTION: Windows Top-Level Filter Reached.\n");
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
