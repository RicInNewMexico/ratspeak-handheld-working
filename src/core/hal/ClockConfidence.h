#pragma once

#include <atomic>
#include <cstdint>
#include <time.h>

namespace handheld {

// A restored epoch is useful for display/message dates, but it is not evidence
// that time advanced while the device was off. Confidence lasts only this boot.
class ClockConfidence {
public:
    static bool synchronized() { return (_state.load(std::memory_order_acquire) & Synced) != 0; }
    static void markSynchronized(time_t epoch) {
        if (epoch > 1700000000) _state.fetch_or(Synced, std::memory_order_release);
    }
    static bool canSeedApproximate() { return _state.load(std::memory_order_acquire) == 0; }
    static void networkSyncStarted() {
        // Called by the device owner before SNTP starts. Once its asynchronous
        // clock writer is active, approximate GPS/NVS writes must not race it.
        _state.fetch_or(NetworkStarted, std::memory_order_release);
    }

private:
    enum : uint8_t { Synced = 1, NetworkStarted = 2 };
    inline static std::atomic<uint8_t> _state{0};
};

} // namespace handheld
