#pragma once
#include "../mem/safe_memory.h"

namespace trinity::game
{
    // TU 2.02: native 14171E696 reads root+60 count, 14171E6C4
    // reads WORD status IDs. Live protagonists/mount: 20 entries, not 64.
    struct StatView
    {
        uintptr_t owner = 0, actor = 0, marker = 0, root = 0, array = 0;
        uint32_t count = 0;
        bool Contains(uintptr_t entry) const
        {
            return count && entry >= array && (entry - array) % kSizeof_StatEntry == 0 &&
                (entry - array) / kSizeof_StatEntry < count;
        }
        bool Same(const StatView& other) const
        {
            return owner == other.owner && actor == other.actor && marker == other.marker &&
                root == other.root && array == other.array && count == other.count && count;
        }
    };

    inline StatView ReadStatView(uintptr_t owner)
    {
        StatView view{};
        view.owner = owner;
        uintptr_t back = 0;
        if (!mem::IsValidUserPtr(owner) ||
            !mem::ReadPtr(owner + kOff_Owner_Actor, &view.actor) || !mem::IsValidUserPtr(view.actor) ||
            !mem::ReadPtr(view.actor + kOff_Actor_StatusMarker, &view.marker) || !mem::IsValidUserPtr(view.marker) ||
            !mem::ReadPtr(view.marker + kOff_Marker_TargetOwner, &view.root) || !mem::IsValidUserPtr(view.root) ||
            !mem::ReadPtr(view.root, &back) || back != view.marker ||
            !mem::ReadPtr(view.root + kOff_Root_StatArray, &view.array) || !mem::IsValidUserPtr(view.array) ||
            !mem::Read32(view.root + 0x60, &view.count) || !view.count || view.count > 256 ||
            view.array > mem::kMaxPointer - uintptr_t{view.count} * kSizeof_StatEntry) return {};
        return view;
    }
}
