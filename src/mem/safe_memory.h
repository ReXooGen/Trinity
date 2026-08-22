#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstddef>

#include "../game/offsets.h"

namespace trinity::mem
{
    // Guarded (SEH) memory access for reading/writing game-process memory
    // whose validity we can't otherwise prove - a pointer chain through
    // engine objects that may be stale, mid-construction, or simply wrong.
    // Every function here rejects addresses below kMinPointer up front and
    // wraps the access in __try/__except so a bad read/write is dropped
    // instead of crashing the process. Locals must stay POD (no C++ objects
    // with destructors) for __try/__except to be legal in the same function.

    inline constexpr uintptr_t kMaxPointer = 0x00007FFFFFFFFFFFULL;

    inline bool IsValidUserPtr(uintptr_t addr)
    {
        return addr >= game::kMinPointer && addr <= kMaxPointer;
    }

    inline bool Read8(uintptr_t addr, uint8_t* out)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *out = *reinterpret_cast<volatile uint8_t*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool Read16(uintptr_t addr, uint16_t* out)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *out = *reinterpret_cast<volatile uint16_t*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool Read32(uintptr_t addr, uint32_t* out)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *out = *reinterpret_cast<volatile uint32_t*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool Read64(uintptr_t addr, uint64_t* out)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *out = *reinterpret_cast<volatile uint64_t*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    // Signed 64-bit alias - most game quantities (item counts, stat values)
    // are read as int64_t at the call site.
    inline bool Read64(uintptr_t addr, int64_t* out)
    {
        return Read64(addr, reinterpret_cast<uint64_t*>(out));
    }
    inline bool ReadPtr(uintptr_t addr, uintptr_t* out)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *out = *reinterpret_cast<volatile uintptr_t*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline bool Write8(uintptr_t addr, uint8_t val)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *reinterpret_cast<volatile uint8_t*>(addr) = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool Write16(uintptr_t addr, uint16_t val)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *reinterpret_cast<volatile uint16_t*>(addr) = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool Write32(uintptr_t addr, uint32_t val)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *reinterpret_cast<volatile uint32_t*>(addr) = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    // A single overload (rather than one per signedness) - two same-rank
    // overloads differing only in signedness make an int/int64_t literal
    // argument (e.g. `0`) an ambiguous call.
    inline bool Write64(uintptr_t addr, uint64_t val)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *reinterpret_cast<volatile uint64_t*>(addr) = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool WritePtr(uintptr_t addr, uintptr_t val)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *reinterpret_cast<volatile uintptr_t*>(addr) = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline bool ReadFloat(uintptr_t addr, float* out)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *out = *reinterpret_cast<volatile float*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool WriteFloat(uintptr_t addr, float val)
    {
        if (!IsValidUserPtr(addr)) return false;
        __try { *reinterpret_cast<volatile float*>(addr) = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Three packed floats (a Vec3) at addr+0/4/8.
    inline bool ReadFloat3(uintptr_t addr, float out[3])
    {
        if (!IsValidUserPtr(addr) || !IsValidUserPtr(addr + 8)) return false;
        __try
        {
            out[0] = *reinterpret_cast<volatile float*>(addr + 0);
            out[1] = *reinterpret_cast<volatile float*>(addr + 4);
            out[2] = *reinterpret_cast<volatile float*>(addr + 8);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline bool ReadVec3(uintptr_t addr, float* out)
    {
        return ReadFloat3(addr, out);
    }

    // Copies an ASCII C-string out of game memory, byte by byte and guarded.
    // Rejects non-printable bytes outright - callers use this to test "is
    // this actually a string" as much as to read one.
    inline bool ReadCString(uintptr_t addr, char* out, size_t n)
    {
        if (!IsValidUserPtr(addr) || n == 0) return false;
        __try
        {
            size_t i = 0;
            for (; i < n - 1; ++i)
            {
                if (!IsValidUserPtr(addr + i)) break;
                const char c = *reinterpret_cast<volatile char*>(addr + i);
                if (c == 0) break;
                if (static_cast<unsigned char>(c) < 0x20)
                    return false; // not a printable key/string - reject
                out[i] = c;
            }
            out[i] = 0;
            return i > 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Engine refcounted string: slot -> string object -> first qword = char*.
    inline bool ReadEngineString(uintptr_t slot, char* out, size_t n)
    {
        uintptr_t obj = 0, cstr = 0;
        if (!ReadPtr(slot, &obj) || obj < game::kMinPointer) return false;
        if (!ReadPtr(obj, &cstr) || cstr < game::kMinPointer) return false;
        return ReadCString(cstr, out, n);
    }

    // Safely unprotects, modifies, reprotects, and flushes instructions for a memory region
    inline bool PatchMemory(uintptr_t addr, const void* data, size_t size)
    {
        if (!IsValidUserPtr(addr) || !data || size == 0) return false;
        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        __try
        {
            memcpy(reinterpret_cast<void*>(addr), data, size);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            VirtualProtect(reinterpret_cast<void*>(addr), size, oldProtect, &oldProtect);
            return false;
        }
        VirtualProtect(reinterpret_cast<void*>(addr), size, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), size);
        return true;
    }
}
