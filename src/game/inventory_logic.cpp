#include "inventory_logic.h"

namespace trinity::game
{
    bool CanTransitionPickupCapacityPatch(PickupCapacityPatchState from,
                                          PickupCapacityPatchState to,
                                          const uint8_t* current,
                                          const uint8_t* expected,
                                          size_t size)
    {
        return current && expected && size == 2 && from != to &&
               current[0] == expected[0] && current[1] == expected[1];
    }

    bool CanCommitAuthoritativeAdd(bool primitivesReady, bool haveDefinition,
                                   uintptr_t clientHolder, uintptr_t serverHolder)
    {
        return primitivesReady && haveDefinition && clientHolder != 0 &&
               serverHolder != 0 && serverHolder != clientHolder;
    }

    bool ShouldRetryAuthoritativeAdd(bool primitivesReady, bool haveDefinition,
                                     uintptr_t clientHolder, uintptr_t serverHolder,
                                     int attempts, int maxAttempts)
    {
        return primitivesReady && haveDefinition && clientHolder != 0 &&
               serverHolder == 0 && attempts >= 0 && attempts < maxAttempts;
    }

    bool IsAuthoritativeHolderCandidate(uintptr_t clientContainer,
                                        uintptr_t clientHolder,
                                        uintptr_t candidateContainer,
                                        uintptr_t candidateHolder,
                                        bool candidateIsLiveCharacter,
                                        uint32_t clientBucketCount,
                                        uint32_t candidateBucketCount)
    {
        return clientContainer != 0 && clientHolder != 0 &&
               candidateContainer != 0 && candidateHolder != 0 &&
               candidateContainer != clientContainer &&
               candidateHolder != clientHolder &&
               candidateIsLiveCharacter && clientBucketCount != 0 &&
               candidateBucketCount == clientBucketCount;
    }

    bool ShouldAttemptCatalogResolve(bool haveItemTableGlobal,
                                     uint64_t nowMs, uint64_t lastAttemptMs,
                                     uint64_t retryIntervalMs)
    {
        if (haveItemTableGlobal) return false;
        if (lastAttemptMs == 0) return true;
        if (nowMs < lastAttemptMs) return true;
        return nowMs - lastAttemptMs >= retryIntervalMs;
    }

    bool ShouldInspectPassiveHolderCandidate(uintptr_t cachedServerHolder,
                                             uintptr_t candidateContainer,
                                             uintptr_t candidateHolder,
                                             uintptr_t clientHolder,
                                             uint32_t clientBucketCount,
                                             uint32_t candidateBucketCount)
    {
        if (cachedServerHolder != 0) return false;
        if (candidateContainer == 0 || candidateHolder == 0) return false;
        if (clientHolder != 0 && candidateHolder == clientHolder) return false;
        if (clientBucketCount != 0 && candidateBucketCount != clientBucketCount) return false;
        return true;
    }
}
