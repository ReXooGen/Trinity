#pragma once

#include <cstdint>

namespace trinity::game
{
    // PE 2944's final dispatcher is sub_1406550B0, reached after the map UI's
    // CommonModalMessage confirmation.  Its global client-actor attacher
    // deliberately ignores RCX; sceneId, nodeIndex and a zero R9D mode select
    // the ordinary fast-travel flow.
    struct NativeTravelCall
    {
        void* context = nullptr;
        int sceneId = -1;
        unsigned int nodeIndex = 0;
        int travelMode = 0;
    };

    inline bool PrepareNativeTravelCall(bool triggerResolved, int& sceneId,
                                        int nodeIndex, NativeTravelCall* out)
    {
        if (!triggerResolved || !out || sceneId < 0 || nodeIndex < 0)
            return false;

        out->context = nullptr;
        out->sceneId = sceneId;
        out->nodeIndex = static_cast<unsigned int>(nodeIndex);
        out->travelMode = 0;
        return true;
    }
}
