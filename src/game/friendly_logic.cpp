#include "friendly_logic.h"

namespace trinity::game
{
    bool SelectTrustBaseline(int64_t stored, bool hasStored,
                             int64_t cached, bool hasCached,
                             int64_t* outBaseline)
    {
        if (!outBaseline) return false;
        if (hasCached)
        {
            *outBaseline = cached;
            return true;
        }
        if (hasStored)
        {
            *outBaseline = stored;
            return true;
        }
        return false;
    }

    int64_t ScaleTrustValue(int64_t previous, bool hasPrevious,
                            int64_t incoming, float multiplier, bool enabled)
    {
        constexpr int64_t kTrustMax = 100;
        if (!enabled || multiplier <= 1.0f || incoming < 0 || incoming > kTrustMax)
            return incoming;

        const int64_t baseline = hasPrevious ? previous : 0;
        const int64_t gain = incoming - baseline;
        if (gain <= 0)
            return incoming;

        int64_t result = baseline + static_cast<int64_t>(
            static_cast<double>(gain) * static_cast<double>(multiplier));
        if (result > kTrustMax) result = kTrustMax;
        if (result < 0) result = 0;
        return result;
    }
}
