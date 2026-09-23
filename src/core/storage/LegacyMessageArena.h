#pragma once

#include "StorageContract.h"
#include "AlignedStorageMemory.h"
#include <Arduino.h>
#include <atomic>
#include <cstdlib>

namespace handheld::storage {

// Cardputer reserves this block during setup, before the worker and networking
// start. Its lifetime is the whole boot. Display pixels stay independently
// owned so slow legacy reads/writes never prevent the UI from drawing.
class LegacyMessageArena {
public:
    bool reserve() {
        if (_memory) return true;
        if (!Budget::canReserveLegacy(ESP.getFreeHeap(), ESP.getMaxAllocHeap())) return false;
        void* memory = allocateAlignedStorage(Budget::LegacyScratch);
        if (!memory) return false;
        if (ESP.getFreeHeap() < Budget::CardHeapFloor ||
            ESP.getMaxAllocHeap() < Budget::CardLargestBlockFloor) {
            freeAlignedStorage(memory); return false;
        }
        _memory = memory;
        return true;
    }
    bool available() const { return _memory != nullptr; }
    void* borrow() {
        // The storage executor is sequential. Refuse a nested live document
        // instead of overwriting its bytes or waiting for itself to finish.
        if (!_memory || _borrowed.test_and_set(std::memory_order_acquire)) return nullptr;
        return _memory;
    }
    void release() { _borrowed.clear(std::memory_order_release); }

private:
    void* _memory = nullptr;
    std::atomic_flag _borrowed = ATOMIC_FLAG_INIT;
};

inline LegacyMessageArena& legacyMessageArena() {
    static LegacyMessageArena arena;
    return arena;
}

} // namespace handheld::storage
