#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace handheld {
// One owner samples these values. A sampled minimum can miss an allocation
// between polls; allocatorMinimum is the SDK's separate historical statistic.
struct HeapObservation {
    uint32_t free = 0, largest = 0, allocatorMinimum = 0;
    uint32_t sampledMinimumFree = UINT32_MAX, sampledMinimumLargest = UINT32_MAX;
    void add(uint32_t freeBytes, uint32_t largestBlock, uint32_t minimum) {
        free = freeBytes; largest = largestBlock; allocatorMinimum = minimum;
        if (free < sampledMinimumFree) sampledMinimumFree = free;
        if (largest < sampledMinimumLargest) sampledMinimumLargest = largest;
    }
};
struct HeapObservations {
    HeapObservation internal, psram;
    uint32_t samples = 0, lastSample = 0, lastReport = 0;
    bool due(uint32_t now) const { return !samples || now - lastSample >= 100; }
    void sampled(uint32_t now) {
        lastSample = now;
        if (samples != UINT32_MAX) ++samples;
    }
    bool reportDue(uint32_t now) {
        if (now - lastReport < 5000) return false;
        lastReport = now;
        return true;
    }
};
struct LatencyHistogram {
    static constexpr std::array<uint32_t, 8> limits{{16, 33, 50, 100, 200, 500, 1000, UINT32_MAX}};
    std::array<uint32_t, 8> bins{};
    uint32_t samples = 0, maximum = 0;
    void add(uint32_t ms) {
        ++samples;
        if (ms > maximum) maximum = ms;
        for (unsigned i = 0; i < limits.size(); ++i) if (ms <= limits[i]) { ++bins[i]; break; }
    }
    uint32_t percentile(unsigned percent) const {
        if (!samples) return 0;
        const uint64_t rank = (uint64_t(samples) * percent + 99) / 100;
        uint64_t count = 0;
        for (unsigned i = 0; i < bins.size(); ++i) { count += bins[i]; if (count >= rank) return limits[i]; }
        return maximum;
    }
};
// UI owner only: input detection to completion of the next physical flush.
// This is a proxy, not an interrupt timestamp or proof the input changed pixels.
inline LatencyHistogram inputToFlush;
inline uint32_t pendingInputAt = 0;
inline bool inputPending = false;
inline void inputObserved(uint32_t now) {
    if (!inputPending) { pendingInputAt = now; inputPending = true; }
}
inline void displayFlushed(uint32_t now) {
    if (inputPending) { inputToFlush.add(now - pendingInputAt); inputPending = false; }
}
inline std::atomic<uint32_t> maximumFlashWriteMs{0};
inline void recordFlashWrite(uint32_t duration) {
    auto old = maximumFlashWriteMs.load(std::memory_order_relaxed);
    while (duration > old && !maximumFlashWriteMs.compare_exchange_weak(old, duration, std::memory_order_relaxed)) {}
}
}
