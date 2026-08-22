#pragma once
#include <cstdint>
#include <cstddef>

namespace trinity::game
{
    inline constexpr uint32_t kKnownItemCount = 6573;

    // Resolves engine item key string for a given ItemInfoTable row index (typeId)
    const char* ResolveItemKeyForTypeId(uint16_t typeId);

    // Classifies item into category and tab, and returns group icon
    void GetItemCategoryInfo(const char* key, char* outGroup, size_t groupSize, char* outTab, size_t tabSize, char* outIcon = nullptr, size_t iconSize = 0);
}
