#pragma once

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace handheld::storage {

// Normal and exceptional legacy reads stream into one reserved arena.
// In-place growth avoids the old+new StringBuilder copy when parsing a large
// body. No input/output-sized String is retained beside this bounded DOM.
class JsonArena final : public ArduinoJson::Allocator {
public:
    JsonArena(void* data, size_t bytes,
              size_t failAfter = std::numeric_limits<size_t>::max())
        : _begin(static_cast<uint8_t*>(data)), _bytes(bytes), _failAfter(failAfter) {
        if (!_begin || uintptr_t(_begin) % alignof(Block) || bytes < sizeof(Block)) {
            _begin = nullptr; _bytes = 0; return;
        }
        _bytes -= _bytes % alignof(Block);
        new (_begin) Block{_bytes - sizeof(Block), true};
    }
    JsonArena(const JsonArena&) = delete;
    JsonArena& operator=(const JsonArena&) = delete;

    void* allocate(size_t bytes) override {
        if (!_begin || bytes > _bytes || _attempts++ >= _failAfter) return nullptr;
        bytes = aligned(bytes);
        for (Block* block = first(); block; block = next(block)) {
            if (!block->free || block->bytes < bytes) continue;
            split(block, bytes);
            block->free = false;
            _used += sizeof(Block) + block->bytes;
            if (_used > _peak) _peak = _used;
            return block + 1;
        }
        return nullptr;
    }

    void deallocate(void* pointer) override {
        if (!pointer) return;
        auto* block = static_cast<Block*>(pointer) - 1;
        _used -= sizeof(Block) + block->bytes;
        block->free = true;
        coalesce();
    }

private:
    void coalesce() {
        for (Block* at = first(); at;) {
            Block* after = next(at);
            if (at->free && after && after->free)
                at->bytes += sizeof(Block) + after->bytes;
            else at = after;
        }
    }

public:

    void* reallocate(void* pointer, size_t bytes) override {
        if (!pointer) return allocate(bytes);
        if (!bytes) { deallocate(pointer); return nullptr; }
        if (bytes > _bytes) return nullptr;
        bytes = aligned(bytes);
        auto* block = static_cast<Block*>(pointer) - 1;
        const size_t previous = block->bytes;
        Block* after = next(block);
        if (bytes > previous && after && after->free &&
            bytes <= previous + sizeof(Block) + after->bytes) {
            block->bytes += sizeof(Block) + after->bytes;
        }
        if (bytes <= block->bytes) {
            split(block, bytes);
            coalesce();
            _used = _used - previous + block->bytes;
            if (_used > _peak) _peak = _used;
            return pointer;
        }
        void* replacement = allocate(bytes);
        if (!replacement) return nullptr;
        std::memcpy(replacement, pointer, previous);
        deallocate(pointer);
        return replacement;
    }

    size_t used() const { return _used; }
    size_t peak() const { return _peak; }
    size_t capacity() const { return _bytes; }
    size_t attempts() const { return _attempts; }

private:
    struct alignas(std::max_align_t) Block { size_t bytes; bool free; };
    static size_t aligned(size_t bytes) {
        const size_t alignment = alignof(Block);
        return bytes ? (bytes + alignment - 1) / alignment * alignment : alignment;
    }
    Block* first() const { return reinterpret_cast<Block*>(_begin); }
    Block* next(Block* block) const {
        uint8_t* address = reinterpret_cast<uint8_t*>(block + 1) + block->bytes;
        return address < _begin + _bytes ? reinterpret_cast<Block*>(address) : nullptr;
    }
    static void split(Block* block, size_t bytes) {
        if (block->bytes - bytes < sizeof(Block) + alignof(Block)) return;
        new (reinterpret_cast<uint8_t*>(block + 1) + bytes)
            Block{block->bytes - bytes - sizeof(Block), true};
        block->bytes = bytes;
    }
    uint8_t* _begin;
    size_t _bytes;
    size_t _failAfter;
    size_t _attempts = 0;
    size_t _used = 0;
    size_t _peak = 0;
};

} // namespace handheld::storage
