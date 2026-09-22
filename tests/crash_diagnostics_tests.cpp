#include "core/crash_diagnostics.h"
#include "core/state.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
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
    using trinity::core::diag::BuildFeatureSnapshot;
    using trinity::core::diag::FeatureSnapshot;
    using trinity::core::diag::FeatureSnapshotsEqualIgnoringRevision;
    using trinity::core::diag::MutationAccumulator;
    using trinity::core::diag::CrashBundleFile;
    using trinity::core::diag::CrashTimestamp;
    using trinity::core::diag::FormatCrashStem;
    using trinity::core::diag::SelectCrashFilesToPrune;

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

    void TestFeatureSnapshot()
    {
        trinity::State state{};
        state.godMode = true;
        state.noClip = true;
        state.invSlotSize = true;
        state.workerMaxLevelAndSkills = true;
        state.noClipSpeed = 12.345f;
        state.superRunMult = 3.25f;
        state.superJumpMult = 4.5f;
        state.invSlotSizeVal = 700;

        const FeatureSnapshot first = BuildFeatureSnapshot(state, 9);
        Expect(first.revision == 9, "feature snapshot must preserve its publication revision");
        Expect((first.enabledBits & (std::uint64_t{1} << 0)) != 0,
               "feature bit zero must identify God Mode");
        Expect((first.enabledBits & (std::uint64_t{1} << 11)) != 0,
               "feature bit eleven must identify No Clip");
        Expect((first.enabledBits & (std::uint64_t{1} << 21)) != 0,
               "feature bit twenty-one must identify Slot Size");
        Expect((first.enabledBits & (std::uint64_t{1} << 23)) != 0,
               "feature bit twenty-three must identify the worker patch");
        Expect(first.walkSpeedMilli == 12345, "No Clip speed must use stable milli-units");
        Expect(first.sprintSpeedMilli == 3250, "Super Run multiplier must use stable milli-units");
        Expect(first.jumpHeightMilli == 4500, "Super Jump multiplier must use stable milli-units");
        Expect(first.slotSize == 700, "slot-size snapshot must preserve the configured value");

        FeatureSnapshot second = BuildFeatureSnapshot(state, 10);
        Expect(FeatureSnapshotsEqualIgnoringRevision(first, second),
               "a revision-only change must be deduplicated");
        state.noClipSpeed = 12.346f;
        second = BuildFeatureSnapshot(state, 11);
        Expect(!FeatureSnapshotsEqualIgnoringRevision(first, second),
               "a diagnostic value change must publish a new snapshot");
    }

    void TestMutationAggregation()
    {
        MutationAccumulator outer("inventory.quantity");
        outer.Note(0x1200, 4, true);
        outer.Note(0x1100, 8, false);
        outer.Note(0x1300, 16, true);

        MutationAccumulator inner("equipment.modify");
        inner.Note(0x9000, 2, true);
        const auto innerSummary = inner.Finish();
        const auto outerSummary = outer.Finish();

        Expect(std::strcmp(outerSummary.label, "inventory.quantity") == 0,
               "mutation summary must retain its stable scope label");
        Expect(outerSummary.firstAddress == 0x1100,
               "mutation summary must expand to the lowest write address");
        Expect(outerSummary.lastAddress == 0x1310,
               "mutation summary must expand through the final written byte");
        Expect(outerSummary.writes == 3 && outerSummary.failures == 1,
               "mutation summary must count attempts and failures separately");
        Expect(innerSummary.firstAddress == 0x9000 && innerSummary.lastAddress == 0x9002,
               "a nested accumulator must retain its independent address range");
        Expect(innerSummary.writes == 1 && innerSummary.failures == 0,
               "a nested accumulator must not inherit outer writes");

        MutationAccumulator empty("empty");
        const auto emptySummary = empty.Finish();
        Expect(emptySummary.firstAddress == 0 && emptySummary.lastAddress == 0,
               "an empty mutation scope must not invent an address range");
    }

    void TestDumpPolicyAndNaming()
    {
        using namespace trinity::core::CrashDiagnostics;
        const unsigned flags = static_cast<unsigned>(kDiagnosticDumpType);
        Expect((flags & MiniDumpWithThreadInfo) != 0,
               "diagnostic dump must retain per-thread information");
        Expect((flags & MiniDumpWithUnloadedModules) != 0,
               "diagnostic dump must retain unloaded-module information");
        Expect((flags & MiniDumpWithIndirectlyReferencedMemory) != 0,
               "diagnostic dump must retain indirectly referenced memory");
        Expect((flags & MiniDumpWithFullMemory) == 0,
               "diagnostic dump must never become a full-memory dump");
        Expect((flags & MiniDumpWithPrivateReadWriteMemory) == 0,
               "diagnostic dump must never include all private read/write memory");

        const CrashTimestamp timestamp{2026, 9, 22, 7, 8, 9};
        wchar_t stem[80]{};
        Expect(FormatCrashStem(timestamp, 4321, stem, 80),
               "valid timestamp and PID must format a crash stem");
        Expect(std::wcscmp(stem, L"Trinity_Crash_20260922-070809_4321") == 0,
               "crash stem must be deterministic and sortable");
        wchar_t tooSmall[8]{};
        Expect(!FormatCrashStem(timestamp, 4321, tooSmall, 8),
               "filename formatting must fail closed on a short buffer");
    }

    void TestRetentionSelection()
    {
        const CrashBundleFile files[] = {
            {"Trinity_Crash_20260922-070000_1", true},
            {"Trinity_Crash_20260922-070000_1", false},
            {"Trinity_Crash_20260922-080000_1", true},
            {"Trinity_Crash_20260922-090000_1", false},
            {"Trinity_Crash_20260922-100000_1", true},
            {"Trinity_Crash_20260922-100000_1", false},
        };
        bool prune[6]{};
        SelectCrashFilesToPrune(files, 6, 3, prune);
        Expect(prune[0] && prune[1], "both files in the oldest complete bundle must be pruned");
        Expect(!prune[2], "a retained text-only partial bundle must remain intact");
        Expect(!prune[3], "a retained dump-only partial bundle must remain intact");
        Expect(!prune[4] && !prune[5], "both files in a retained complete bundle must remain");

        bool keepAll[6]{true, true, true, true, true, true};
        SelectCrashFilesToPrune(files, 6, 4, keepAll);
        for (bool value : keepAll)
            Expect(!value, "retention must keep every file when only four bundle stems exist");
    }
}

int main()
{
    TestOrderingAndWrap();
    TestCoalescing();
    TestConcurrentPublication();
    TestAttribution();
    TestFeatureSnapshot();
    TestMutationAggregation();
    TestDumpPolicyAndNaming();
    TestRetentionSelection();
    if (failures == 0) std::puts("Crash diagnostics tests passed.");
    return failures == 0 ? 0 : 1;
}
