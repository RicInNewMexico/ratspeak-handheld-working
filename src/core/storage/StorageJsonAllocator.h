#pragma once

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "AlignedStorageMemory.h"

namespace handheld::storage {

// Accounts every requested byte, including its aligned size header. Growing a
// block reserves the old and new blocks together, so realloc cannot hide a
// temporary second allocation. Failure leaves the original block untouched.
class JsonAllocator : public ArduinoJson::Allocator {
public:
    explicit JsonAllocator(size_t limit,
                           size_t failAfter = std::numeric_limits<size_t>::max())
        : _limit(limit), _failAfter(failAfter) {}
    JsonAllocator(const JsonAllocator&) = delete;
    JsonAllocator& operator=(const JsonAllocator&) = delete;

    void* allocate(size_t size) override {
        if (size > _limit || headerBytes() > _limit - size ||
            size + headerBytes() > _limit - _used || _attempts++ >= _failAfter)
            return nullptr;
        auto* header = static_cast<Header*>(allocateAlignedStorage(sizeof(Header) + size));
        if (!header) return nullptr;
        header->bytes = size;
        _used += headerBytes() + size;
        if (_used > _peak) _peak = _used;
        return header + 1;
    }

    void deallocate(void* pointer) override {
        if (!pointer) return;
        auto* header = static_cast<Header*>(pointer) - 1;
        _used -= headerBytes() + header->bytes;
        freeAlignedStorage(header);
    }

    void* reallocate(void* pointer, size_t size) override {
        if (!pointer) return allocate(size);
        if (!size) { deallocate(pointer); return nullptr; }
        auto* header = static_cast<Header*>(pointer) - 1;
        // Keeping a shrinking block avoids a hidden temporary allocation. The
        // original charge remains until release, even when fewer bytes are used.
        if (size <= header->bytes) return pointer;
        void* replacement = allocate(size);
        if (!replacement) return nullptr;
        std::memcpy(replacement, pointer, header->bytes);
        deallocate(pointer);
        return replacement;
    }

    size_t used() const { return _used; }
    size_t peak() const { return _peak; }
    size_t attempts() const { return _attempts; }
    static constexpr size_t headerBytes() { return sizeof(Header) + AlignedStorageOverhead; }

private:
    struct alignas(std::max_align_t) Header { size_t bytes; };
    size_t _limit;
    size_t _failAfter;
    size_t _used = 0;
    size_t _peak = 0;
    size_t _attempts = 0;
};

} // namespace handheld::storage
