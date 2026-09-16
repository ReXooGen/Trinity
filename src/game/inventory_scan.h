#pragma once
#include "../mem/safe_memory.h"

namespace trinity::game
{
    // A resumable read cursor. The caller supplies a current holder and mutation
    // epoch each visit. No partial traversal may be published after churn.
    struct InventoryScan
    {
        uintptr_t holder = 0, buckets = 0, stride = 0;
        uint64_t epoch = 0;
        uint32_t count = 0, bucketIndex = 0, row = 0;
        uintptr_t bucket = 0, slots = 0;
        uint16_t slotCount = 0;
        bool started = false;

        bool Bind(uintptr_t currentHolder, uintptr_t currentStride, uint64_t currentEpoch)
        {
            if (currentStride != 0xC8 && currentStride != 0xD0) return false;
            uintptr_t array = 0;
            uint32_t n = 0;
            if (!mem::IsValidUserPtr(currentHolder) || !mem::ReadPtr(currentHolder + kOff_InvHolder_Buckets, &array) ||
                !mem::IsValidUserPtr(array) || !mem::Read32(currentHolder + kOff_InvHolder_Count, &n) || !n || n > 4096)
                return false;
            if (started) return currentHolder == holder && array == buckets && n == count &&
                currentStride == stride && currentEpoch == epoch;
            holder = currentHolder; buckets = array; count = n; stride = currentStride; epoch = currentEpoch;
            started = true;
            return true;
        }

        bool BucketCurrent() const
        {
            uintptr_t current = 0;
            uint16_t n = 0;
            return bucket && mem::ReadPtr(buckets + uintptr_t{bucketIndex} * 8, &current) && current == bucket &&
                mem::ReadPtr(bucket + kOff_InvBucket_Slots, &current) && current == slots &&
                mem::Read16(bucket + kOff_InvBucket_Count, &n) && n == slotCount;
        }

        enum class Result { Pending, Complete, Invalid };
        template<class Visit, class EndBucket>
        Result Slice(Visit visit, EndBucket endBucket, unsigned slotBudget = 256, unsigned bucketBudget = 8)
        {
            return SliceWhile(visit, endBucket, [] { return true; }, slotBudget, bucketBudget);
        }

        // A time-budget yield is not a failed read: retain the cursor and do not
        // publish a partial bucket. Existing callers keep their count-only cap.
        template<class Visit, class EndBucket, class KeepGoing>
        Result SliceWhile(Visit visit, EndBucket endBucket, KeepGoing keepGoing,
                          unsigned slotBudget = 256, unsigned bucketBudget = 8)
        {
            unsigned rows = 0, heads = 0;
            while (bucketIndex < count && rows < slotBudget && heads < bucketBudget)
            {
                if (!keepGoing()) return Result::Pending;
                if (!bucket)
                {
                    ++heads;
                    if (!mem::ReadPtr(buckets + uintptr_t{bucketIndex} * 8, &bucket)) return Result::Invalid;
                    if (!bucket) { ++bucketIndex; continue; }
                    if (!mem::IsValidUserPtr(bucket) || !mem::ReadPtr(bucket + kOff_InvBucket_Slots, &slots) ||
                        !mem::Read16(bucket + kOff_InvBucket_Count, &slotCount) || slotCount > 8192 ||
                        (slotCount && (!mem::IsValidUserPtr(slots) ||
                            uintptr_t{slotCount} * stride > mem::kMaxPointer - slots))) return Result::Invalid;
                }
                if (!BucketCurrent()) return Result::Invalid;
                while (row < slotCount && rows < slotBudget)
                {
                    if (!keepGoing()) return Result::Pending;
                    ++rows;
                    if (!visit(slots + uintptr_t{row} * stride)) return Result::Invalid;
                    ++row;
                }
                if (row == slotCount)
                {
                    if (!BucketCurrent() || !endBucket(*this)) return Result::Invalid;
                    ++bucketIndex; row = 0; bucket = slots = 0; slotCount = 0;
                }
            }
            return bucketIndex == count ? Result::Complete : Result::Pending;
        }
    };
}
