#pragma once

#include <cstdint>

namespace trinity::mem
{
    // A section named .debug may contain either stale non-code data or the
    // live executable image, depending on the game build. Only reject the
    // non-executable form.
    bool ShouldScanSection(const char* name, uint32_t characteristics);
}
