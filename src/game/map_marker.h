#pragma once

#include <cmath>
#include <cstdint>

namespace trinity::game
{
    struct MapMarkerPosition
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    using MarkerReadPointer = bool (*)(uintptr_t, uintptr_t*);
    using MarkerReadPosition = bool (*)(uintptr_t, float*);

    inline bool ReadCurrentMapMarker(uintptr_t uiGlobal, MarkerReadPointer readPointer,
                                     MarkerReadPosition readPosition, MapMarkerPosition& out)
    {
        uintptr_t uiState = 0;
        uintptr_t destination = 0;
        float values[3]{};
        if (!uiGlobal || !readPointer || !readPosition ||
            !readPointer(uiGlobal, &uiState) || !uiState ||
            !readPointer(uiState + 0xA8, &destination) || !destination ||
            !readPosition(destination + 0x20, values))
            return false;
        if (!std::isfinite(values[0]) || !std::isfinite(values[1]) || !std::isfinite(values[2]) ||
            std::abs(values[0]) > 1.0e9f || std::abs(values[1]) > 1.0e9f ||
            std::abs(values[2]) > 1.0e9f ||
            (values[0] == 0.0f && values[1] == 0.0f && values[2] == 0.0f))
            return false;
        out = {values[0], values[1], values[2]};
        return true;
    }
}
