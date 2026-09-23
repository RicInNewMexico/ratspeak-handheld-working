#pragma once
#include <lvgl.h>

// Sensitive renderer content must never be exposed by USB semantic snapshots.
// Mark the widget itself: descendants inherit the exclusion during traversal.
namespace LvPrivacy {
static constexpr lv_obj_flag_t Sensitive = LV_OBJ_FLAG_USER_1;
inline void markSensitive(lv_obj_t* object) { lv_obj_add_flag(object, Sensitive); }
inline bool isSensitive(lv_obj_t* object) { return lv_obj_has_flag(object, Sensitive); }
}
