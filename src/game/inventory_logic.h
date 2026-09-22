#pragma once

#include <cstddef>
#include <cstdint>

namespace trinity::game
{
    enum class PickupCapacityPatchState
    {
        Original,
        Patched
    };

    bool CanTransitionPickupCapacityPatch(PickupCapacityPatchState from,
                                          PickupCapacityPatchState to,
                                          const uint8_t* current,
                                          const uint8_t* expected,
                                          size_t size);

    // A generated item is usable only when the operation can be committed to
    // a distinct authority holder before its client mirror is touched.
    bool CanCommitAuthoritativeAdd(bool primitivesReady, bool haveDefinition,
                                   uintptr_t clientHolder, uintptr_t serverHolder);

    bool ShouldRetryAuthoritativeAdd(bool primitivesReady, bool haveDefinition,
                                     uintptr_t clientHolder, uintptr_t serverHolder,
                                     int attempts, int maxAttempts);

    // A passive observer may see merchant, planner-copy, client, and server
    // inventories. Only a distinct, live player container with the same bucket
    // shape as the current client inventory can become server authority.
    bool IsAuthoritativeHolderCandidate(uintptr_t clientContainer,
                                        uintptr_t clientHolder,
                                        uintptr_t candidateContainer,
                                        uintptr_t candidateHolder,
                                        bool candidateIsLiveCharacter,
                                        uint32_t clientBucketCount,
                                        uint32_t candidateBucketCount);

    // Retry a missing catalog resolver at a bounded cadence. Once the table is
    // present the static catalog is built once and no further scans are needed.
    bool ShouldAttemptCatalogResolve(bool haveItemTableGlobal,
                                     uint64_t nowMs, uint64_t lastAttemptMs,
                                     uint64_t retryIntervalMs);

    // Fast lock-free pre-filter for the passive GetHolder hook:
    // If the server authority holder is already resolved, or if the candidate
    // is clearly the client or has a mismatched bucket count, skip taking any lock.
    bool ShouldInspectPassiveHolderCandidate(uintptr_t cachedServerHolder,
                                             uintptr_t candidateContainer,
                                             uintptr_t candidateHolder,
                                             uintptr_t clientHolder,
                                             uint32_t clientBucketCount,
                                             uint32_t candidateBucketCount);
}
