#pragma once
#include <Windows.h>
#include <cstdint>
#include <string_view>

#include <vector>

namespace trinity::mem
{
    // A contiguous mapped module image (base + SizeOfImage).
    struct ModuleRegion
    {
        uintptr_t base = 0;
        size_t    size = 0;
        explicit operator bool() const { return base != 0 && size != 0; }
    };

    // The main game module (the running .exe). Resolved once and cached.
    // Every signature is expressed relative to this module so the mod keeps
    // working across game patches: we never bake in absolute addresses, we scan
    // for the surrounding byte pattern at load time.
    const ModuleRegion& GameModule();

    // Scan a module for an IDA-style byte pattern, e.g.
    //   "48 8B 05 ?? ?? ?? ?? 48 85 C0"
    // '?' or '??' are single-byte wildcards. Returns the address of the first
    // match, or 0 if not present.
    //
    // This game is packed, so we do not trust PE section flags: the scan walks
    // the module's actually-committed, readable pages (via VirtualQuery) rather
    // than only sections marked executable. Robust against odd section layouts.
    uintptr_t FindPattern(std::string_view pattern, const ModuleRegion& mod);
    inline uintptr_t FindPattern(std::string_view pattern) { return FindPattern(pattern, GameModule()); }

    // Scan a module for all occurrences of an IDA-style byte pattern (up to maxCount).
    std::vector<uintptr_t> FindAllMatches(std::string_view pattern, const ModuleRegion& mod, size_t maxCount = 64);
    inline std::vector<uintptr_t> FindAllMatches(std::string_view pattern, size_t maxCount = 64) { return FindAllMatches(pattern, GameModule(), maxCount); }

    // Count matches (up to `maxCount`) for a pattern - used to confirm a
    // signature is unique in the live build before trusting it.
    size_t CountMatches(std::string_view pattern, const ModuleRegion& mod, size_t maxCount = 8);
    inline size_t CountMatches(std::string_view pattern, size_t maxCount = 8) { return CountMatches(pattern, GameModule(), maxCount); }

    // Streaming scan: call `visit(match, ctx)` for every occurrence of the
    // pattern, in address order, until it returns true. Returns the accepted
    // match, or 0 if no match was accepted. Lets a caller filter a very common
    // byte sequence (e.g. a `lea rdx, [rip+..]` opcode) by a semantic test -
    // such as "does this instruction reference *that* string" - without
    // materialising every hit.
    uintptr_t FindPatternIf(std::string_view pattern, const ModuleRegion& mod,
                            bool (*visit)(uintptr_t match, void* ctx), void* ctx);
    inline uintptr_t FindPatternIf(std::string_view pattern,
                                   bool (*visit)(uintptr_t, void*), void* ctx)
    {
        return FindPatternIf(pattern, GameModule(), visit, ctx);
    }

    // Log a one-line summary of the module's committed regions (base, size,
    // number of readable/executable pages). Diagnostic aid.
    void LogModuleLayout();

    // Resolve a 32-bit RIP-relative reference. `dispAddr` points at the 4-byte
    // displacement field; `instrEnd` is the address of the next instruction.
    inline uintptr_t ResolveRip(uintptr_t dispAddr, uintptr_t instrEnd)
    {
        return instrEnd + *reinterpret_cast<const int32_t*>(dispAddr);
    }

    inline uintptr_t ResolveRipAt(uintptr_t instr, int instrLen)
    {
        const uintptr_t disp = instr + instrLen - 4;
        return ResolveRip(disp, instr + instrLen);
    }

    inline uintptr_t ResolveCall(uintptr_t callInstr)
    {
        return callInstr + 5 + *reinterpret_cast<const int32_t*>(callInstr + 1);
    }
}
