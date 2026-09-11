#pragma once

#include <cstdint>
#include <functional>

namespace trinity::core
{
    enum class ReadinessProfile : uint8_t
    {
        LegacyComplete,
        Tu201KnownCompatible,
    };

    ReadinessProfile ReadinessProfileForRevision(uint16_t revision);

    using ReadinessProbe = std::function<bool()>;
    using ReadinessClock = std::function<uint64_t()>;
    using ReadinessPause = std::function<void(uint32_t)>;

    // Polls a late-materialising game-code probe without oversleeping the
    // timeout. The probe is evaluated once more at the exact timeout boundary.
    bool WaitForReadiness(const ReadinessProbe& probe,
                          const ReadinessClock& now,
                          const ReadinessPause& pause,
                          uint32_t timeoutMs,
                          uint32_t intervalMs);
}
