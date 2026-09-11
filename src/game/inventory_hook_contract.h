#pragma once

#include <cstdint>

namespace trinity::game
{
    // TU 2.01.00 transaction commit. The fourth register argument is consumed
    // as a word; the four remaining arguments live in the Win64 stack slots.
    using InventoryCommit201_t = void*(__fastcall*)(void* holder, void* outError,
                                                     void* placements, uint16_t mode,
                                                     void* outEvents, uint8_t notify,
                                                     uint8_t reconcile, uint8_t replicate);
}
