#pragma once

#include <cstdint>

namespace trinity::game
{
    // God Mode may only block damage after the damage target has been
    // positively identified as a tracked player target (or mount).
    bool ShouldBlockPlayerDamage(bool godMode, bool strictPlayerTarget,
                                 bool mountTarget);

    // TU 2.01 stores the stable ObjectType in the type-descriptor tag. The
    // owner+0x48 field now also contains state bits and cannot be compared as
    // a plain enum.
    bool IsMountTypeTag(uint8_t tag);

    // The character-manager vector interleaves protagonists with mounts and
    // pets that share the same outer class/vtable. Filter those entries before
    // applying the three-protagonist limit, or a horse can displace Oongka.
    bool IsTrackedProtagonistTypeTag(uint8_t tag);

    // Character-dependent menus may request the same manager refresh even
    // when stat cheats are disabled. The request expires so the scan does not
    // remain active after those menus stop consuming it.
    bool ShouldRefreshTrackedCharacters(bool statFeatureActive,
                                        uint64_t nowMs,
                                        uint64_t requestedUntilMs);
}
