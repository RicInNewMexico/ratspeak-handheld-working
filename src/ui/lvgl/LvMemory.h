#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// LVGL and these counters belong to the UI task, including startup.
typedef struct {
    size_t retained;
    size_t peak;
    uint32_t allocations;
    uint32_t refused;
} handheld_lvgl_memory_t;

void* handheld_lvgl_alloc(size_t size);
void* handheld_lvgl_realloc(void* pointer, size_t size);
void handheld_lvgl_free(void* pointer);
handheld_lvgl_memory_t handheld_lvgl_memory_stats(void);

#ifdef __cplusplus
}
#endif
