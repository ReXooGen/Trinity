#include "crash_diagnostics.h"

#include "state.h"

#include <algorithm>
#include <atomic>
#include <limits>

namespace trinity::core::CrashDiagnostics {
namespace {

diag::BreadcrumbRing g_breadcrumbs;
HMODULE g_module = nullptr;
std::atomic<std::uint64_t> g_featureRevision{0};

struct AtomicFeatureSnapshot {
    std::atomic<std::uint64_t> revision{0};
    std::atomic<std::uint64_t> enabledBits{0};
    std::atomic<std::int32_t> walkSpeedMilli{0};
    std::atomic<std::int32_t> sprintSpeedMilli{0};
    std::atomic<std::int32_t> jumpHeightMilli{0};
    std::atomic<std::int32_t> slotSize{0};
};

AtomicFeatureSnapshot g_featureSnapshots[2];
std::atomic<unsigned> g_activeFeatureSnapshot{0};
thread_local MutationScope* g_activeMutationScope = nullptr;

diag::FeatureSnapshot LoadFeatureSnapshot(unsigned index) noexcept
{
    const AtomicFeatureSnapshot& source = g_featureSnapshots[index & 1u];
    diag::FeatureSnapshot snapshot{};
    snapshot.revision = source.revision.load(std::memory_order_acquire);
    snapshot.enabledBits = source.enabledBits.load(std::memory_order_relaxed);
    snapshot.walkSpeedMilli = source.walkSpeedMilli.load(std::memory_order_relaxed);
    snapshot.sprintSpeedMilli = source.sprintSpeedMilli.load(std::memory_order_relaxed);
    snapshot.jumpHeightMilli = source.jumpHeightMilli.load(std::memory_order_relaxed);
    snapshot.slotSize = source.slotSize.load(std::memory_order_relaxed);
    return snapshot;
}

void StoreFeatureSnapshot(unsigned index, const diag::FeatureSnapshot& snapshot) noexcept
{
    AtomicFeatureSnapshot& destination = g_featureSnapshots[index & 1u];
    destination.enabledBits.store(snapshot.enabledBits, std::memory_order_relaxed);
    destination.walkSpeedMilli.store(snapshot.walkSpeedMilli, std::memory_order_relaxed);
    destination.sprintSpeedMilli.store(snapshot.sprintSpeedMilli, std::memory_order_relaxed);
    destination.jumpHeightMilli.store(snapshot.jumpHeightMilli, std::memory_order_relaxed);
    destination.slotSize.store(snapshot.slotSize, std::memory_order_relaxed);
    destination.revision.store(snapshot.revision, std::memory_order_release);
}

}  // namespace

void InstallUnhandledFilter(HMODULE module) noexcept
{
    g_module = module;
}

bool InitializeSession(HMODULE module, const wchar_t*) noexcept
{
    g_module = module;
    return module != nullptr;
}

void Shutdown() noexcept
{
}

void Record(diag::BreadcrumbKind kind,
            const char* label,
            std::uintptr_t address,
            std::uint32_t size,
            std::int64_t detail,
            bool success) noexcept
{
    diag::BreadcrumbInput input{};
    input.kind = kind;
    input.label = label;
    input.tickMs = GetTickCount64();
    input.threadId = GetCurrentThreadId();
    input.address = address;
    input.size = size;
    input.detail = detail;
    input.success = success;
    g_breadcrumbs.Record(input);
}

void PublishFeatureSnapshot(const State& state) noexcept
{
    const std::uint64_t revision = g_featureRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    const diag::FeatureSnapshot next = diag::BuildFeatureSnapshot(state, revision);
    const unsigned active = g_activeFeatureSnapshot.load(std::memory_order_acquire) & 1u;
    const diag::FeatureSnapshot current = LoadFeatureSnapshot(active);
    if (current.revision != 0 && diag::FeatureSnapshotsEqualIgnoringRevision(current, next)) return;

    const unsigned inactive = active ^ 1u;
    StoreFeatureSnapshot(inactive, next);
    g_activeFeatureSnapshot.store(inactive, std::memory_order_release);
    Record(diag::BreadcrumbKind::FeatureState,
           "feature.snapshot",
           0,
           0,
           static_cast<std::int64_t>(next.enabledBits),
           true);
}

MutationScope::MutationScope(const char* label) noexcept
    : accumulator_(label), previous_(g_activeMutationScope)
{
    g_activeMutationScope = this;
}

MutationScope::~MutationScope() noexcept
{
    if (g_activeMutationScope == this) g_activeMutationScope = previous_;
    const diag::MutationSummary summary = accumulator_.Finish();
    if (summary.writes == 0) return;

    const std::uintptr_t extent = summary.lastAddress >= summary.firstAddress
        ? summary.lastAddress - summary.firstAddress
        : 0;
    const std::uint32_t size = extent > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(extent);
    const std::int64_t detail =
        (static_cast<std::int64_t>(summary.writes) << 32) | summary.failures;
    Record(diag::BreadcrumbKind::Mutation,
           summary.label,
           summary.firstAddress,
           size,
           detail,
           summary.failures == 0);
}

void NoteMemoryWrite(std::uintptr_t address, std::uint32_t size, bool success) noexcept
{
    if (g_activeMutationScope) {
        g_activeMutationScope->accumulator_.Note(address, size, success);
    } else if (!success) {
        Record(diag::BreadcrumbKind::SafetyFailure,
               "memory.write",
               address,
               size,
               0,
               false);
    }
}

}  // namespace trinity::core::CrashDiagnostics
