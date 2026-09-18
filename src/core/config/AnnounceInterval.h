#pragma once

#include <cstdint>

namespace handheld::announce {
constexpr uint16_t Off = 0;
constexpr uint16_t MinimumMinutes = 30;
constexpr uint16_t MaximumMinutes = 360;
constexpr uint16_t StepMinutes = 5;

// Zero is an explicit opt-out, not an invalid interval to replace with 30m.
constexpr uint16_t normalizeMinutes(int32_t minutes) {
    return minutes == Off ? Off : minutes < MinimumMinutes ? MinimumMinutes :
        minutes > MaximumMinutes ? MaximumMinutes : uint16_t(minutes);
}
} // namespace handheld::announce
