#include "crash_diagnostics_logic.h"
#include "state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <cwchar>

namespace trinity::core::diag {

namespace {

void PackLabel(const char* label, std::uint64_t (&words)[4]) noexcept
{
    char text[32]{};
    if (label) {
        std::size_t length = 0;
        while (length < sizeof(text) - 1 && label[length] != '\0') ++length;
        std::memcpy(text, label, length);
    }
    std::memcpy(words, text, sizeof(text));
}

void UnpackLabel(const std::uint64_t (&words)[4], char (&label)[32]) noexcept
{
    std::memcpy(label, words, sizeof(label));
    label[sizeof(label) - 1] = '\0';
}

bool InRange(std::uintptr_t value, std::uintptr_t base, std::size_t size) noexcept
{
    return size != 0 && value >= base && value - base < size;
}

bool Recent(std::uint64_t now, std::uint64_t then, std::uint64_t window) noexcept
{
    return now >= then && now - then <= window;
}

std::int32_t ToMilli(float value) noexcept
{
    if (!std::isfinite(value)) return 0;
    const double scaled = static_cast<double>(value) * 1000.0;
    const double low = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const double high = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    if (scaled <= low) return std::numeric_limits<std::int32_t>::min();
    if (scaled >= high) return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(std::llround(scaled));
}

void SetBit(std::uint64_t& bits, unsigned index, bool enabled) noexcept
{
    if (enabled) bits |= std::uint64_t{1} << index;
}

}  // namespace

FeatureSnapshot BuildFeatureSnapshot(const State& state, std::uint64_t revision) noexcept
{
    FeatureSnapshot snapshot{};
    snapshot.revision = revision;
    SetBit(snapshot.enabledBits, 0, state.godMode);
    SetBit(snapshot.enabledBits, 1, state.oneHitKill);
    SetBit(snapshot.enabledBits, 2, state.infDurability);
    SetBit(snapshot.enabledBits, 3, state.noFallDamage);
    SetBit(snapshot.enabledBits, 4, state.infStamina);
    SetBit(snapshot.enabledBits, 5, state.infMountStamina);
    SetBit(snapshot.enabledBits, 6, state.infSpirit);
    SetBit(snapshot.enabledBits, 7, state.noBounty);
    SetBit(snapshot.enabledBits, 8, state.superRun);
    SetBit(snapshot.enabledBits, 9, state.superJump);
    SetBit(snapshot.enabledBits, 10, state.freeFlight);
    SetBit(snapshot.enabledBits, 11, state.noClip);
    SetBit(snapshot.enabledBits, 12, state.trustMult);
    SetBit(snapshot.enabledBits, 13, state.gameSpeed);
    SetBit(snapshot.enabledBits, 14, state.timeFrozen);
    SetBit(snapshot.enabledBits, 15, state.forceClearSky);
    SetBit(snapshot.enabledBits, 16, state.noWind);
    SetBit(snapshot.enabledBits, 17, state.clearDistantFog);
    SetBit(snapshot.enabledBits, 18, state.showFps);
    SetBit(snapshot.enabledBits, 19, state.showConsole);
    SetBit(snapshot.enabledBits, 20, state.invStackSize);
    SetBit(snapshot.enabledBits, 21, state.invSlotSize);
    SetBit(snapshot.enabledBits, 22, state.playstationIcons);
    SetBit(snapshot.enabledBits, 23, state.workerMaxLevelAndSkills);
    SetBit(snapshot.enabledBits, 24, state.fileLogging);
    SetBit(snapshot.enabledBits, 25, state.autoSave);
    SetBit(snapshot.enabledBits, 26, state.useCustomFont);
    SetBit(snapshot.enabledBits, 27, state.showItemTooltip);
    snapshot.walkSpeedMilli = ToMilli(state.noClipSpeed);
    snapshot.sprintSpeedMilli = ToMilli(state.superRunMult);
    snapshot.jumpHeightMilli = ToMilli(state.superJumpMult);
    snapshot.slotSize = state.invSlotSizeVal;
    return snapshot;
}

bool FeatureSnapshotsEqualIgnoringRevision(const FeatureSnapshot& left,
                                           const FeatureSnapshot& right) noexcept
{
    return left.enabledBits == right.enabledBits &&
           left.walkSpeedMilli == right.walkSpeedMilli &&
           left.sprintSpeedMilli == right.sprintSpeedMilli &&
           left.jumpHeightMilli == right.jumpHeightMilli &&
           left.slotSize == right.slotSize;
}

MutationAccumulator::MutationAccumulator(const char* label) noexcept
{
    if (!label) return;
    std::size_t length = 0;
    while (length < sizeof(summary_.label) - 1 && label[length] != '\0') ++length;
    std::memcpy(summary_.label, label, length);
}

void MutationAccumulator::Note(std::uintptr_t address,
                               std::uint32_t size,
                               bool success) noexcept
{
    const std::uintptr_t maxAddress = std::numeric_limits<std::uintptr_t>::max();
    const std::uintptr_t end = size > maxAddress - address ? maxAddress : address + size;
    if (summary_.writes == 0) {
        summary_.firstAddress = address;
        summary_.lastAddress = end;
    } else {
        summary_.firstAddress = std::min(summary_.firstAddress, address);
        summary_.lastAddress = std::max(summary_.lastAddress, end);
    }
    ++summary_.writes;
    if (!success) ++summary_.failures;
}

MutationSummary MutationAccumulator::Finish() noexcept
{
    return summary_;
}

bool FormatCrashStem(const CrashTimestamp& timestamp,
                     std::uint32_t processId,
                     wchar_t* output,
                     std::size_t outputCapacity) noexcept
{
    if (!output || outputCapacity == 0 ||
        timestamp.month < 1 || timestamp.month > 12 ||
        timestamp.day < 1 || timestamp.day > 31 ||
        timestamp.hour > 23 || timestamp.minute > 59 || timestamp.second > 59) {
        return false;
    }
    const int written = std::swprintf(output,
                                     outputCapacity,
                                     L"Trinity_Crash_%04u%02u%02u-%02u%02u%02u_%u",
                                     timestamp.year,
                                     timestamp.month,
                                     timestamp.day,
                                     timestamp.hour,
                                     timestamp.minute,
                                     timestamp.second,
                                     processId);
    if (written < 0 || static_cast<std::size_t>(written) >= outputCapacity) {
        output[0] = L'\0';
        return false;
    }
    return true;
}

void SelectCrashFilesToPrune(const CrashBundleFile* files,
                             std::size_t count,
                             std::size_t bundlesToKeep,
                             bool* prune) noexcept
{
    if (!prune) return;
    for (std::size_t i = 0; i < count; ++i) prune[i] = false;
    if (!files) return;

    for (std::size_t i = 0; i < count; ++i) {
        std::size_t newerUniqueStems = 0;
        for (std::size_t j = 0; j < count; ++j) {
            if (std::strcmp(files[j].stem, files[i].stem) <= 0) continue;
            bool alreadyCounted = false;
            for (std::size_t k = 0; k < j; ++k) {
                if (std::strcmp(files[k].stem, files[j].stem) == 0 &&
                    std::strcmp(files[k].stem, files[i].stem) > 0) {
                    alreadyCounted = true;
                    break;
                }
            }
            if (!alreadyCounted) ++newerUniqueStems;
        }
        prune[i] = newerUniqueStems >= bundlesToKeep;
    }
}

bool BreadcrumbRing::Record(const BreadcrumbInput& input) noexcept
{
    if (writer_.test_and_set(std::memory_order_acquire)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    struct WriterRelease {
        std::atomic_flag& flag;
        ~WriterRelease() { flag.clear(std::memory_order_release); }
    } release{writer_};

    std::uint64_t labelWords[4]{};
    PackLabel(input.label, labelWords);

    const std::uint64_t previousSequence = nextSequence_.load(std::memory_order_relaxed);
    if (previousSequence != 0) {
        Slot& previous = slots_[(previousSequence - 1) % kCapacity];
        if (previous.publishedSequence.load(std::memory_order_acquire) == previousSequence) {
            bool identical =
                previous.kind.load(std::memory_order_relaxed) == static_cast<std::uint8_t>(input.kind) &&
                previous.threadId.load(std::memory_order_relaxed) == input.threadId &&
                previous.address.load(std::memory_order_relaxed) == input.address &&
                previous.size.load(std::memory_order_relaxed) == input.size &&
                previous.detail.load(std::memory_order_relaxed) == input.detail &&
                previous.success.load(std::memory_order_relaxed) == static_cast<std::uint8_t>(input.success);
            for (std::size_t i = 0; identical && i < 4; ++i)
                identical = previous.labelWords[i].load(std::memory_order_relaxed) == labelWords[i];

            const std::uint64_t priorTick = previous.tickMs.load(std::memory_order_relaxed);
            if (identical && input.tickMs >= priorTick && input.tickMs - priorTick <= 250) {
                previous.tickMs.store(input.tickMs, std::memory_order_relaxed);
                previous.repeatCount.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
    }

    const std::uint64_t sequence = previousSequence + 1;
    Slot& slot = slots_[(sequence - 1) % kCapacity];
    slot.publishedSequence.store(0, std::memory_order_release);
    slot.tickMs.store(input.tickMs, std::memory_order_relaxed);
    slot.threadId.store(input.threadId, std::memory_order_relaxed);
    slot.kind.store(static_cast<std::uint8_t>(input.kind), std::memory_order_relaxed);
    for (std::size_t i = 0; i < 4; ++i)
        slot.labelWords[i].store(labelWords[i], std::memory_order_relaxed);
    slot.address.store(input.address, std::memory_order_relaxed);
    slot.size.store(input.size, std::memory_order_relaxed);
    slot.detail.store(input.detail, std::memory_order_relaxed);
    slot.repeatCount.store(1, std::memory_order_relaxed);
    slot.success.store(static_cast<std::uint8_t>(input.success), std::memory_order_relaxed);
    slot.publishedSequence.store(sequence, std::memory_order_release);
    nextSequence_.store(sequence, std::memory_order_release);
    return true;
}

std::size_t BreadcrumbRing::Snapshot(Breadcrumb* output,
                                     std::size_t outputCapacity,
                                     std::uint64_t* dropped) const noexcept
{
    if (dropped) *dropped = dropped_.load(std::memory_order_relaxed);
    if (!output || outputCapacity == 0) return 0;

    std::size_t count = 0;
    for (const Slot& slot : slots_) {
        const std::uint64_t sequenceBefore =
            slot.publishedSequence.load(std::memory_order_acquire);
        if (sequenceBefore == 0) continue;

        Breadcrumb candidate{};
        candidate.sequence = sequenceBefore;
        candidate.tickMs = slot.tickMs.load(std::memory_order_relaxed);
        candidate.threadId = slot.threadId.load(std::memory_order_relaxed);
        candidate.kind = static_cast<BreadcrumbKind>(slot.kind.load(std::memory_order_relaxed));
        std::uint64_t labelWords[4]{};
        for (std::size_t i = 0; i < 4; ++i)
            labelWords[i] = slot.labelWords[i].load(std::memory_order_relaxed);
        UnpackLabel(labelWords, candidate.label);
        candidate.address = slot.address.load(std::memory_order_relaxed);
        candidate.size = slot.size.load(std::memory_order_relaxed);
        candidate.detail = slot.detail.load(std::memory_order_relaxed);
        candidate.repeatCount = slot.repeatCount.load(std::memory_order_relaxed);
        candidate.success = slot.success.load(std::memory_order_relaxed);

        const std::uint64_t sequenceAfter =
            slot.publishedSequence.load(std::memory_order_acquire);
        if (sequenceAfter != sequenceBefore) continue;

        if (count < outputCapacity) {
            output[count++] = candidate;
        } else {
            std::size_t oldest = 0;
            for (std::size_t i = 1; i < count; ++i) {
                if (output[i].sequence < output[oldest].sequence) oldest = i;
            }
            if (candidate.sequence > output[oldest].sequence) output[oldest] = candidate;
        }
    }

    std::sort(output, output + count, [](const Breadcrumb& left, const Breadcrumb& right) {
        return left.sequence < right.sequence;
    });
    return count;
}

Attribution Classify(const AttributionInput& input) noexcept
{
    if (InRange(input.instructionPointer, input.trinityBase, input.trinitySize))
        return Attribution::TrinityDirect;
    if (input.stackContainsTrinity)
        return Attribution::TrinitySuspected;

    for (std::size_t i = 0; input.breadcrumbs && i < input.breadcrumbCount; ++i) {
        const Breadcrumb& breadcrumb = input.breadcrumbs[i];
        if (breadcrumb.kind == BreadcrumbKind::Mutation &&
            breadcrumb.success != 0 &&
            Recent(input.nowMs, breadcrumb.tickMs, 60000) &&
            InRange(input.targetAddress, breadcrumb.address, breadcrumb.size)) {
            return Attribution::TrinitySuspected;
        }
        if (breadcrumb.kind == BreadcrumbKind::SafetyFailure &&
            Recent(input.nowMs, breadcrumb.tickMs, 5000)) {
            return Attribution::TrinitySuspected;
        }
    }

    if (input.faultModuleIsGameOrDriver)
        return Attribution::GameOrDriver;
    return Attribution::Inconclusive;
}

const char* AttributionName(Attribution value) noexcept
{
    switch (value) {
    case Attribution::TrinityDirect: return "TRINITY_DIRECT";
    case Attribution::TrinitySuspected: return "TRINITY_SUSPECTED";
    case Attribution::GameOrDriver: return "GAME_OR_DRIVER";
    case Attribution::Inconclusive: return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}

}  // namespace trinity::core::diag
