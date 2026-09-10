#pragma once

#include <Arduino.h>
#include "config/BoardConfig.h"
#include "storage/StorageContract.h"
#include "storage/StorageJsonAllocator.h"
#include <limits>

namespace handheld::config {

// Admission for configuration and identity-metadata allocations only. The pinned Card SDK
// adds 12 poison bytes, a 4-byte TLSF header, up to 3 alignment bytes and up to
// 12 unsplittable bytes. Round that <=31-byte cost up to 32 per allocation.
// This is not a reservation against concurrent network/SDK allocations.
struct Memory {
    static constexpr size_t AllocationOverhead = 32;
    static constexpr size_t charge(size_t bytes) {
        return !bytes ? 0 : bytes > SIZE_MAX - AllocationOverhead ? SIZE_MAX
            : bytes + AllocationOverhead;
    }
    // Pinned ESP32 WString: lengths below 14 fit SSO; other fresh buffers
    // allocate (length + 16) rounded down to a multiple of 16, including NUL.
    static constexpr size_t stringCharge(size_t length) {
        return length < 14 ? 0 : length > SIZE_MAX - 16 ? SIZE_MAX
            : charge((length + 16) & ~size_t(15));
    }
    static bool admits(size_t incrementalCharge) {
#if defined(STORAGE_ASYNC_WRITES) && STORAGE_ASYNC_WRITES
        using storage::Budget;
        const size_t free = ESP.getFreeHeap(), largest = ESP.getMaxAllocHeap();
        return free >= Budget::CardHeapFloor && largest >= Budget::CardLargestBlockFloor &&
            incrementalCharge <= free - Budget::CardHeapFloor &&
            incrementalCharge <= largest - Budget::CardLargestBlockFloor;
#else
        (void)incrementalCharge;
        return true;
#endif
    }
    // Call only for a fresh temporary. A refusal after reserve leaves its
    // capacity owned by that temporary until normal scope destruction.
    static bool reserveString(String& value, size_t length) {
        return admits(stringCharge(length)) && value.reserve(length) && admits(0);
    }
};

// No retained policy fields or global allocator replacement. The inherited
// growth path calls this override while the original block is still charged.
class JsonAllocator final : public storage::JsonAllocator {
public:
    using storage::JsonAllocator::JsonAllocator;
    void* allocate(size_t size) override {
        if (size > SIZE_MAX - headerBytes() ||
            !Memory::admits(Memory::charge(size + headerBytes()))) return nullptr;
        void* result = storage::JsonAllocator::allocate(size);
        if (result && !Memory::admits(0)) {
            storage::JsonAllocator::deallocate(result);
            return nullptr;
        }
        return result;
    }
};

} // namespace handheld::config
