#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace trinity::core::diag {

enum class BreadcrumbKind : std::uint8_t {
    FeatureState,
    HookState,
    PatchState,
    Identity,
    Mutation,
    Operation,
    SafetyFailure,
};

enum class Attribution : std::uint8_t {
    TrinityDirect,
    TrinitySuspected,
    GameOrDriver,
    Inconclusive,
};

struct BreadcrumbInput {
    BreadcrumbKind kind{};
    const char* label{};
    std::uint64_t tickMs{};
    std::uint32_t threadId{};
    std::uintptr_t address{};
    std::uint32_t size{};
    std::int64_t detail{};
    bool success{};
};

struct Breadcrumb {
    std::uint64_t sequence{};
    std::uint64_t tickMs{};
    std::uint32_t threadId{};
    BreadcrumbKind kind{};
    char label[32]{};
    std::uintptr_t address{};
    std::uint32_t size{};
    std::int64_t detail{};
    std::uint32_t repeatCount{};
    std::uint8_t success{};
};

class BreadcrumbRing final {
public:
    static constexpr std::size_t kCapacity = 512;

    bool Record(const BreadcrumbInput& input) noexcept;
    std::size_t Snapshot(Breadcrumb* output,
                         std::size_t outputCapacity,
                         std::uint64_t* dropped) const noexcept;

private:
    struct Slot {
        std::atomic<std::uint64_t> publishedSequence{0};
        std::atomic<std::uint64_t> tickMs{0};
        std::atomic<std::uint32_t> threadId{0};
        std::atomic<std::uint8_t> kind{0};
        std::array<std::atomic<std::uint64_t>, 4> labelWords{};
        std::atomic<std::uintptr_t> address{0};
        std::atomic<std::uint32_t> size{0};
        std::atomic<std::int64_t> detail{0};
        std::atomic<std::uint32_t> repeatCount{0};
        std::atomic<std::uint8_t> success{0};
    };

    std::array<Slot, kCapacity> slots_{};
    std::atomic_flag writer_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> nextSequence_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

struct AttributionInput {
    std::uintptr_t instructionPointer{};
    std::uintptr_t targetAddress{};
    std::uintptr_t trinityBase{};
    std::size_t trinitySize{};
    bool faultModuleIsGameOrDriver{};
    bool stackContainsTrinity{};
    std::uint64_t nowMs{};
    const Breadcrumb* breadcrumbs{};
    std::size_t breadcrumbCount{};
};

Attribution Classify(const AttributionInput& input) noexcept;
const char* AttributionName(Attribution value) noexcept;

}  // namespace trinity::core::diag
