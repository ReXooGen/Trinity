#include "crash_diagnostics_logic.h"

#include <algorithm>
#include <cstring>

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

}  // namespace

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
