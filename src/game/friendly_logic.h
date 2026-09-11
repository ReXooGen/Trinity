#pragma once

#include <cstdint>

namespace trinity::game
{
    // Pure trust-delta policy kept separate from the live map traversal so the
    // first positive event and the cap remain regression-testable.
    int64_t ScaleTrustValue(int64_t previous, bool hasPrevious,
                            int64_t incoming, float multiplier, bool enabled);

    // Prefer the last value observed before the setter call. The setter's
    // source can alias its destination, making a live-map lookup already show
    // the incoming value and hide the positive delta.
    bool SelectTrustBaseline(int64_t stored, bool hasStored,
                             int64_t cached, bool hasCached,
                             int64_t* outBaseline);
}
