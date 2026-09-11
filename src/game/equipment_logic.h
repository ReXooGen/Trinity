#pragma once

#include <cstdint>

namespace trinity::game
{
    // A recognized equipment identity wins over runtime party ordering;
    // party position is only a fallback for an unidentified component.
    bool AcceptCharacterComponent(int selectedIndex, int identifiedIndex,
                                  int partyIndex);

    // Equipment components are owned by the outer gameplay-character object;
    // the inner actor is only a fallback for older captures.
    uintptr_t PreferEquipmentOwner(uintptr_t owner, uintptr_t actor);
}
