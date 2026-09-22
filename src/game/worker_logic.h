#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace trinity::game
{
    enum class WorkerPatchState
    {
        Original,
        Patched
    };

    // PE 2850 supplied the original Auto Assembler patch; PE 2944 and PE 2949
    // keep the same unique grade-selector branch bytes, so the identical
    // reversible patch applies to all three revisions.
    inline bool WorkerPatchSupportedForRevision(int revision)
    {
        return revision == 2850 || revision == 2944 || revision == 2949;
    }

    // Validate the bytes that are currently at the injection point before a
    // controller writes. The expected buffer is selected by the caller for
    // the requested transition, so every unexpected state fails closed.
    inline bool CanTransitionWorkerPatch(WorkerPatchState from,
                                         WorkerPatchState to,
                                         const uint8_t* current,
                                         const uint8_t* expected,
                                         size_t size)
    {
        if (!current || !expected || size != 6)
            return false;

        if (from == to)
            return std::memcmp(current, expected, size) == 0;

        if (from == WorkerPatchState::Original && to == WorkerPatchState::Patched)
            return std::memcmp(current, expected, size) == 0;

        if (from == WorkerPatchState::Patched && to == WorkerPatchState::Original)
            return std::memcmp(current, expected, size) == 0;

        return false;
    }
}
