#pragma once

namespace trinity::game
{
    // Resolves an internal engine item key (e.g. "Bayur_Fabric_Armor") to its
    // official in-game localized display name (e.g. "The Faceless's Cloth Armor").
    // Returns nullptr if no custom mapping exists.
    const char* ResolveItemDisplayName(const char* key);
}
