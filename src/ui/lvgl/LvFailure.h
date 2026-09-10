#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Registered before lv_init; the callback must not allocate or enter LVGL.
void handheld_lvgl_failure_display(void (*show)(void*), void* display);
void handheld_lvgl_fail(void);

#ifdef __cplusplus
}
#endif
