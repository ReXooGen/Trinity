#pragma once

#include "map_marker.h"
#include "offsets.h"

#include <cstddef>
#include <cmath>
#include <cstdint>

namespace trinity::game
{
    using MarkerTeleportWrite = bool (*)(void*, uintptr_t, const MapMarkerPosition&);
    using MarkerTeleportRead = bool (*)(void*, uintptr_t, MapMarkerPosition&);

    // The current map UI exposes its active destination through a stable
    // pointer chain. Capture hooks are only a fallback for older builds; a
    // failed legacy detour must not disable a verified direct read path.
    inline bool MarkerTeleportCanUseDestination(bool hasDirectDestination,
                                                bool hasOrigin,
                                                size_t installedCaptureHooks)
    {
        return hasOrigin && (hasDirectDestination || installedCaptureHooks != 0);
    }

    inline bool ApplyMarkerTeleportDestination(uintptr_t moveOwner, uintptr_t markerPlayer,
                                               const MapMarkerPosition& destination, void* context,
                                               MarkerTeleportWrite writePosition,
                                               MarkerTeleportRead readPosition)
    {
        if (!moveOwner || !writePosition || !readPosition ||
            !std::isfinite(destination.x) || !std::isfinite(destination.y) ||
            !std::isfinite(destination.z))
            return false;

        const MapMarkerPosition zero{};
        if (!writePosition(context, moveOwner + kOff_Player_Dest0, destination) ||
            !writePosition(context, moveOwner + kOff_Player_Dest1, destination) ||
            !writePosition(context, moveOwner + kOff_MoveOwner_DesiredVel, zero) ||
            !writePosition(context, moveOwner + kOff_MoveOwner_Velocity, zero))
            return false;

        // The marker-specific player proxy is optional and can become stale
        // during character swaps. The live movement owner above is authoritative.
        if (markerPlayer && markerPlayer != moveOwner)
        {
            writePosition(context, markerPlayer + kOff_Player_Dest0, destination);
            writePosition(context, markerPlayer + kOff_Player_Dest1, destination);
        }

        MapMarkerPosition observed{};
        if (!readPosition(context, moveOwner + kOff_Player_Dest0, observed))
            return false;
        constexpr float kReadbackTolerance = 0.5f;
        return std::abs(observed.x - destination.x) <= kReadbackTolerance &&
               std::abs(observed.y - destination.y) <= kReadbackTolerance &&
               std::abs(observed.z - destination.z) <= kReadbackTolerance;
    }
}
