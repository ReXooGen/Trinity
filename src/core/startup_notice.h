#pragma once

#include <array>

namespace trinity::core
{
    // Keep the useful post-startup guidance visually distinct from technical
    // hook diagnostics.  ASCII is deliberate: it renders consistently in the
    // Windows console, Trinity.log, and a redirected support log.
    inline constexpr std::array<const char*, 5> kStartupNoticeLines = {
        "+======================================================================+",
        "|   TRINITY  |  QUICK START & SUPPORT                                  |",
        "|   ADD ITEM: Open inventory or pick up an item first.                 |",
        "|   SUPPORT: https://mul0.com/trainer/crimson-desert-trinity-mod-menu/ |",
        "+======================================================================+",
    };

    inline constexpr const std::array<const char*, 5>& StartupNoticeLines()
    {
        return kStartupNoticeLines;
    }
}
