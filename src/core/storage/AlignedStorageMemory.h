#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace handheld::storage {

// ESP32's ordinary heap can return four-byte alignment, while JSON arena
// headers require max_align_t (eight bytes on Xtensa). Keep malloc's existing
// internal/PSRAM selection and retain its original pointer for the paired free.
inline constexpr size_t StorageMemoryAlignment = alignof(std::max_align_t);
inline constexpr size_t AlignedStorageOverhead = sizeof(void*) + StorageMemoryAlignment - 1;
static_assert((StorageMemoryAlignment & (StorageMemoryAlignment - 1)) == 0);

inline void* allocateAlignedStorage(size_t bytes) {
    if (!bytes || bytes > std::numeric_limits<size_t>::max() - AlignedStorageOverhead)
        return nullptr;
    void* original = std::malloc(bytes + AlignedStorageOverhead);
    if (!original) return nullptr;
    const uintptr_t start = reinterpret_cast<uintptr_t>(original) + sizeof(void*);
    auto* aligned = reinterpret_cast<uint8_t*>(
        (start + StorageMemoryAlignment - 1) & ~(uintptr_t(StorageMemoryAlignment) - 1));
    std::memcpy(aligned - sizeof(void*), &original, sizeof(original));
    return aligned;
}

inline void freeAlignedStorage(void* memory) {
    if (!memory) return;
    void* original;
    std::memcpy(&original, static_cast<uint8_t*>(memory) - sizeof(void*), sizeof(original));
    std::free(original);
}

} // namespace handheld::storage
