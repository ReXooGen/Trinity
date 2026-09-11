#include "player_logic.h"

namespace trinity::game
{
    bool ShouldBlockPlayerDamage(bool godMode, bool strictPlayerTarget,
                                 bool mountTarget)
    {
        return godMode && (strictPlayerTarget || mountTarget);
    }

    bool IsMountTypeTag(uint8_t tag)
    {
        return tag == 5 || tag == 6;
    }

    bool IsTrackedProtagonistTypeTag(uint8_t tag)
    {
        return tag == 1 || tag == 4 || tag == 9;
    }

    bool ShouldRefreshTrackedCharacters(bool statFeatureActive,
                                        uint64_t nowMs,
                                        uint64_t requestedUntilMs)
    {
        return statFeatureActive ||
               (requestedUntilMs != 0 && nowMs <= requestedUntilMs);
    }
}
