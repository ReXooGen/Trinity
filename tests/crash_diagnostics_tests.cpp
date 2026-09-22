#include "core/crash_diagnostics_logic.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
    int failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (condition) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }

    using trinity::core::diag::Attribution;
    using trinity::core::diag::AttributionInput;
    using trinity::core::diag::Breadcrumb;
    using trinity::core::diag::BreadcrumbInput;
    using trinity::core::diag::BreadcrumbKind;
    using trinity::core::diag::BreadcrumbRing;
    using trinity::core::diag::Classify;

    BreadcrumbInput Event(std::uint64_t tick,
                          std::int64_t detail,
                          const char* label = "event")
    {
        BreadcrumbInput input{};
        input.kind = BreadcrumbKind::Operation;
        input.label = label;
        input.tickMs = tick;
        input.threadId = 7;
        input.detail = detail;
        input.success = true;
        return input;
    }

    void TestOrderingAndWrap()
    {
        BreadcrumbRing ring;
        for (std::uint64_t i = 1; i <= 520; ++i)
            Expect(ring.Record(Event(i, static_cast<std::int64_t>(i))),
                   "single-threaded ring writes must be accepted");

        Breadcrumb snapshot[BreadcrumbRing::kCapacity]{};
        std::uint64_t dropped = 0;
        const std::size_t count = ring.Snapshot(snapshot, BreadcrumbRing::kCapacity, &dropped);
        Expect(count == BreadcrumbRing::kCapacity, "wraparound must retain exactly 512 records");
        Expect(snapshot[0].detail == 9, "wraparound must discard the eight oldest records");
        Expect(snapshot[count - 1].detail == 520, "snapshot must end with the newest record");
        for (std::size_t i = 1; i < count; ++i)
            Expect(snapshot[i - 1].sequence < snapshot[i].sequence,
                   "snapshot records must be in chronological sequence order");
        Expect(dropped == 0, "single-threaded writes must not be reported as dropped");
    }

    void TestCoalescing()
    {
        BreadcrumbRing ring;
        Expect(ring.Record(Event(1000, 42, "same")), "first event must be recorded");
        Expect(ring.Record(Event(1100, 42, "same")), "identical adjacent event must coalesce");
        Expect(ring.Record(Event(1400, 42, "same")), "event outside coalesce window must append");

        Breadcrumb snapshot[4]{};
        const std::size_t count = ring.Snapshot(snapshot, 4, nullptr);
        Expect(count == 2, "only adjacent identical events within 250 ms may coalesce");
        Expect(snapshot[0].repeatCount == 2, "coalesced event must preserve its repeat count");
        Expect(snapshot[0].tickMs == 1100, "coalesced event must publish its latest timestamp");
        Expect(snapshot[1].repeatCount == 1, "new event must begin with one occurrence");
    }

    void TestConcurrentPublication()
    {
        BreadcrumbRing ring;
        constexpr int kThreads = 16;
        constexpr int kWrites = 4000;
        std::atomic<bool> start{false};
        std::atomic<bool> done{false};
        std::atomic<bool> ordered{true};

        std::thread reader([&] {
            Breadcrumb snapshot[BreadcrumbRing::kCapacity]{};
            while (!done.load(std::memory_order_acquire)) {
                const std::size_t count = ring.Snapshot(snapshot, BreadcrumbRing::kCapacity, nullptr);
                for (std::size_t i = 1; i < count; ++i) {
                    if (snapshot[i - 1].sequence >= snapshot[i].sequence)
                        ordered.store(false, std::memory_order_relaxed);
                }
            }
        });

        std::vector<std::thread> writers;
        writers.reserve(kThreads);
        for (int thread = 0; thread < kThreads; ++thread) {
            writers.emplace_back([&, thread] {
                while (!start.load(std::memory_order_acquire)) { }
                for (int write = 0; write < kWrites; ++write) {
                    BreadcrumbInput input = Event(
                        static_cast<std::uint64_t>(thread * kWrites + write),
                        static_cast<std::int64_t>(thread * kWrites + write));
                    input.threadId = static_cast<std::uint32_t>(thread + 1);
                    ring.Record(input);
                }
            });
        }

        start.store(true, std::memory_order_release);
        for (auto& writer : writers) writer.join();
        done.store(true, std::memory_order_release);
        reader.join();

        Breadcrumb snapshot[BreadcrumbRing::kCapacity]{};
        std::uint64_t dropped = 0;
        const std::size_t count = ring.Snapshot(snapshot, BreadcrumbRing::kCapacity, &dropped);
        Expect(ordered.load(std::memory_order_relaxed),
               "a concurrent snapshot must never expose duplicate or reversed sequences");
        Expect(count == BreadcrumbRing::kCapacity,
               "concurrent publication must leave a full readable ring");
        Expect(dropped > 0, "contended non-blocking writers must be counted as dropped");
    }

    void TestAttribution()
    {
        const std::uintptr_t base = 0x10000000;
        const std::size_t size = 0x10000;

        AttributionInput directRip{};
        directRip.instructionPointer = base + 0x123;
        directRip.trinityBase = base;
        directRip.trinitySize = size;
        Expect(Classify(directRip) == Attribution::TrinityDirect,
               "a Trinity-owned instruction pointer must classify as direct");

        AttributionInput trinityOnStack{};
        trinityOnStack.instructionPointer = 0x20000000;
        trinityOnStack.trinityBase = base;
        trinityOnStack.trinitySize = size;
        trinityOnStack.stackContainsTrinity = true;
        Expect(Classify(trinityOnStack) == Attribution::TrinitySuspected,
               "a Trinity frame on the faulting stack must classify as suspected");

        Breadcrumb mutation{};
        mutation.kind = BreadcrumbKind::Mutation;
        mutation.tickMs = 1000;
        mutation.address = 0x3000;
        mutation.size = 0x20;
        mutation.success = 1;
        AttributionInput recentOverlappingMutation{};
        recentOverlappingMutation.instructionPointer = 0x20000000;
        recentOverlappingMutation.targetAddress = 0x3010;
        recentOverlappingMutation.trinityBase = base;
        recentOverlappingMutation.trinitySize = size;
        recentOverlappingMutation.nowMs = 61000;
        recentOverlappingMutation.breadcrumbs = &mutation;
        recentOverlappingMutation.breadcrumbCount = 1;
        Expect(Classify(recentOverlappingMutation) == Attribution::TrinitySuspected,
               "a recent successful overlapping mutation must classify as suspected");

        Breadcrumb safety{};
        safety.kind = BreadcrumbKind::SafetyFailure;
        safety.tickMs = 9500;
        safety.success = 0;
        AttributionInput recentSafetyFailure{};
        recentSafetyFailure.instructionPointer = 0x20000000;
        recentSafetyFailure.trinityBase = base;
        recentSafetyFailure.trinitySize = size;
        recentSafetyFailure.nowMs = 14000;
        recentSafetyFailure.breadcrumbs = &safety;
        recentSafetyFailure.breadcrumbCount = 1;
        Expect(Classify(recentSafetyFailure) == Attribution::TrinitySuspected,
               "a recent failed safety gate must classify as suspected");

        AttributionInput cleanGameFault{};
        cleanGameFault.instructionPointer = 0x20000000;
        cleanGameFault.trinityBase = base;
        cleanGameFault.trinitySize = size;
        cleanGameFault.faultModuleIsGameOrDriver = true;
        Expect(Classify(cleanGameFault) == Attribution::GameOrDriver,
               "an external fault without Trinity evidence must classify as game or driver");

        AttributionInput noEvidence{};
        noEvidence.instructionPointer = 0x20000000;
        noEvidence.trinityBase = base;
        noEvidence.trinitySize = size;
        Expect(Classify(noEvidence) == Attribution::Inconclusive,
               "missing evidence must fail closed to inconclusive");

        mutation.tickMs = 999;
        recentOverlappingMutation.nowMs = 61000;
        recentOverlappingMutation.faultModuleIsGameOrDriver = true;
        Expect(Classify(recentOverlappingMutation) == Attribution::GameOrDriver,
               "a mutation older than 60 seconds must not imply Trinity suspicion");
    }
}

int main()
{
    TestOrderingAndWrap();
    TestCoalescing();
    TestConcurrentPublication();
    TestAttribution();
    if (failures == 0) std::puts("Crash diagnostics tests passed.");
    return failures == 0 ? 0 : 1;
}
