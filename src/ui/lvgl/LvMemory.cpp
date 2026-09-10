#include "LvMemory.h"
#include "runtime/ResourceBudget.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>

namespace {
using Budget = handheld::ResourceBudget;
handheld_lvgl_memory_t usage{};
void refused() { if (usage.refused != UINT32_MAX) ++usage.refused; }
}

void* handheld_lvgl_alloc(size_t size) {
    if (!size) return nullptr; // LVGL owns its separate zero-byte sentinel.
    const size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (size > Budget::LvglAllocations - usage.retained ||
        free < Budget::PsramFree || size > free - Budget::PsramFree) {
        refused(); return nullptr;
    }
    void* pointer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!pointer) { refused(); return nullptr; }
    // Charge the actual SDK block size, including allocator rounding. Check
    // again before publication because other PSRAM consumers share this heap.
    const size_t allocated = heap_caps_get_allocated_size(pointer);
    if (allocated > Budget::LvglAllocations - usage.retained ||
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < Budget::PsramFree) {
        heap_caps_free(pointer); refused(); return nullptr;
    }
    usage.retained += allocated;
    usage.peak = std::max(usage.peak, usage.retained);
    if (usage.allocations != UINT32_MAX) ++usage.allocations;
    return pointer;
}

void handheld_lvgl_free(void* pointer) {
    if (!pointer) return;
    usage.retained -= heap_caps_get_allocated_size(pointer);
    heap_caps_free(pointer);
}

void* handheld_lvgl_realloc(void* pointer, size_t size) {
    if (!pointer) return handheld_lvgl_alloc(size);
    if (!size) { handheld_lvgl_free(pointer); return nullptr; }
    const size_t previous = heap_caps_get_allocated_size(pointer);
    if (previous == size) return pointer;
    // Retain the old block until replacement is admitted. A moving SDK realloc
    // cannot be rolled back if its rounded block would exceed the shared cap.
    // This also accounts for both blocks during replacement, rather than hiding
    // the temporary overlap or losing the old value on allocation failure.
    void* next = handheld_lvgl_alloc(size);
    if (!next) return nullptr;
    std::memcpy(next, pointer, std::min(previous, size));
    handheld_lvgl_free(pointer);
    return next;
}

handheld_lvgl_memory_t handheld_lvgl_memory_stats(void) { return usage; }
