#pragma once

#include "SX1262Timing.h"

namespace handheld::radio_timing {

// A bounded 1.5x frame allowance plus two seconds for owner/driver service.
// This is a local operation deadline, not an end-to-end delivery guarantee.
constexpr uint32_t transmitTimeoutMs(uint32_t frameAirtimeMs) {
    const uint64_t budget = (uint64_t{frameAirtimeMs} * 3 + 1) / 2 + 2000;
    return budget < INT32_MAX ? static_cast<uint32_t>(budget) : INT32_MAX;
}

constexpr uint32_t MAX_SPLIT_RX_TIMEOUT_MS =
    ((transmitTimeoutMs(sx1262_timing::MAX_SUPPORTED_FRAME_AIRTIME_MS) + 499) / 500) * 500;
static_assert(MAX_SPLIT_RX_TIMEOUT_MS == 384000, "update supported timing envelope");

constexpr uint32_t splitReceiveTimeoutMs(uint32_t frameAirtimeMs) {
    const uint64_t rounded = ((uint64_t{transmitTimeoutMs(frameAirtimeMs)} + 499) / 500) * 500;
    return rounded < 5000 ? 5000 : rounded > MAX_SPLIT_RX_TIMEOUT_MS ?
        MAX_SPLIT_RX_TIMEOUT_MS : static_cast<uint32_t>(rounded);
}

} // namespace handheld::radio_timing
