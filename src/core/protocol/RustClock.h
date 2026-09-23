#pragma once

#include <Arduino.h>
#include <time.h>
#include "hal/ClockConfidence.h"

// Monotonic 64-bit millisecond clock for the FFI. millis() wraps at
// ~49.7 days; the accumulator is wrap-safe as long as nowMs() is called more
// than once per wrap period (the backend ticks it every loop pass). All use is
// restricted to the protocol owner task — no atomics needed.
class RustClock {
public:
    uint64_t nowMs() {
        uint32_t m = millis();
        if (m < _lastMs) _high += (1ULL << 32);
        _lastMs = m;
        return _high | (uint64_t)m;
    }

    // Best-effort Unix seconds for message/display dates, including an
    // approximate saved epoch. This does not imply current-boot synchronization.
    static uint64_t epochSecs() {
        time_t t = time(nullptr);
        return (t > 1700000000) ? (uint64_t)t : 0;
    }

    // Protocol ordering/expiry needs current-boot GPS or NTP synchronization.
    // Zero selects the Rust protocol's existing clockless policies.
    static uint64_t synchronizedEpochSecs() {
        return handheld::ClockConfidence::synchronized() ? epochSecs() : 0;
    }

private:
    uint32_t _lastMs = 0;
    uint64_t _high = 0;
};
