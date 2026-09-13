#include "equipment_logic.h"

namespace trinity::game
{
    bool AcceptCharacterComponent(int selectedIndex, int identifiedIndex,
                                  int partyIndex)
    {
        if (selectedIndex < 0 || selectedIndex > 2) return false;
        // A party-array position is only a fallback. During companion swaps
        // it can still point at a stale component from another character.
        if (identifiedIndex >= 0)
            return identifiedIndex == selectedIndex;
        return partyIndex == selectedIndex;
    }

    uintptr_t PreferEquipmentOwner(uintptr_t owner, uintptr_t actor)
    {
        return owner ? owner : actor;
    }
}
