#pragma once

#include "diagnostics/RemoteUi.h"
#include "LvPrivacy.h"
#include <lvgl.h>

namespace handheld::diagnostics {
struct LvglUiState {
    const char* title = "";
    int tab = 0;
    bool asleep = false;
    bool boot = false;
    const char* overlay = "none";
    lv_obj_t* focus = nullptr;
};

// Called only by the LVGL owner. It borrows widgets during one bounded walk;
// no pointers or renderer text survive in the Service mailbox.
class LvglUiSnapshot {
public:
    static constexpr unsigned MaxNodes = 512, MaxDepth = 24, MaxLabels = 8;
    static size_t encode(char* output, size_t capacity, const RemoteUiRequest& request,
                         const LvglUiState& state, bool wokeOnly,
                         lv_obj_t* active, lv_obj_t* top, lv_obj_t* system) {
        LvglUiSnapshot snapshot(output, capacity, request.offset, state.focus);
        auto& json = snapshot._json;
        json.append("[UICTRL] {\"id\":"); json.number(static_cast<int32_t>(request.id));
        json.append(",\"ok\":true,\"woke_only\":"); json.append(wokeOnly ? "true" : "false");
        json.append(",\"title\":"); json.string(state.title);
        json.append(",\"tab\":"); json.number(state.tab);
        json.append(",\"asleep\":"); json.append(state.asleep ? "true" : "false");
        json.append(",\"boot\":"); json.append(state.boot ? "true" : "false");
        json.append(",\"overlay\":"); json.string(state.overlay);
        json.append(",\"focus\":");
        if (state.focus && lv_obj_is_visible(state.focus)) {
            json.append("{"); coordinates(json, state.focus); json.append("}");
        } else json.append("null");
        json.append(",\"offset\":"); json.number(request.offset);
        json.append(",\"labels\":[");
        snapshot.walk(active);
        snapshot.walk(top);
        snapshot.walk(system);
        json.append("],\"next\":");
        if (snapshot._more) json.number(request.offset + snapshot._emitted);
        else json.append("null");
        json.append(",\"scan_truncated\":"); json.append(snapshot._scanTruncated ? "true" : "false");
        json.append("}\n");
        return json.size();
    }
private:
    LvglUiSnapshot(char* output, size_t capacity, unsigned offset, lv_obj_t* focus)
        : _json(output, capacity), _offset(offset), _focus(focus) {}
    static void coordinates(RemoteUiJson& json, lv_obj_t* object) {
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        json.append("\"x\":"); json.number(area.x1);
        json.append(",\"y\":"); json.number(area.y1);
        json.append(",\"w\":"); json.number(lv_area_get_width(&area));
        json.append(",\"h\":"); json.number(lv_area_get_height(&area));
    }
    bool focused(lv_obj_t* object) const {
        // A group often focuses a row while its text is a child label.
        for (unsigned n = 0; object && n < MaxDepth; ++n, object = lv_obj_get_parent(object))
            if (object == _focus) return true;
        return false;
    }
    void record(lv_obj_t* object, const char* text, const char* kind) {
        if (_seen++ < _offset) return;
        if (_emitted >= MaxLabels) { _more = true; return; }
        char record[768];
        RemoteUiJson item(record, sizeof(record));
        item.append("{\"text\":"); const bool truncated = item.string(text);
        item.append(",\"kind\":"); item.string(kind);
        item.append(","); coordinates(item, object);
        item.append(",\"focused\":"); item.append(focused(object) ? "true" : "false");
        item.append(",\"truncated\":"); item.append(truncated ? "true" : "false");
        item.append("}");
        // Leave enough room for comma, pagination, truncation and line ending.
        if (item.size() + 96 >= _json.remaining()) { _more = true; return; }
        if (_emitted) _json.append(",");
        _json.append(record);
        ++_emitted;
    }
    bool visit(lv_obj_t* object) {
        if (!lv_obj_is_visible(object)) return false;
        if (LvPrivacy::isSensitive(object)) {
            record(object, "[redacted]", "private");
            return false;
        }
#if LV_USE_TEXTAREA
        if (lv_obj_check_type(object, &lv_textarea_class)) {
            record(object, lv_textarea_get_password_mode(object) ? "[redacted]" : lv_textarea_get_text(object), "textarea");
            return false; // Its internal label can briefly reveal a password.
        }
#endif
#if LV_USE_LABEL
        if (lv_obj_check_type(object, &lv_label_class)) record(object, lv_label_get_text(object), "label");
#endif
#if LV_USE_DROPDOWN
        if (lv_obj_check_type(object, &lv_dropdown_class)) {
            char selected[100];
            lv_dropdown_get_selected_str(object, selected, sizeof(selected));
            record(object, selected, "dropdown");
        }
#endif
#if LV_USE_SWITCH
        if (lv_obj_check_type(object, &lv_switch_class))
            record(object, lv_obj_has_state(object, LV_STATE_CHECKED) ? "on" : "off", "switch");
#endif
        return true;
    }
    void walk(lv_obj_t* root) {
        if (!root || _more) return;
        struct Frame { lv_obj_t* object; uint32_t child; } stack[MaxDepth];
        unsigned depth = 0;
        stack[0] = {root, 0};
        for (;;) {
            auto& frame = stack[depth];
            if (frame.child == 0) {
                if (_visited++ >= MaxNodes) { _scanTruncated = true; return; }
                if (!visit(frame.object) || _more) {
                    if (_more || !depth) return;
                    --depth;
                    continue;
                }
            }
            const uint32_t children = lv_obj_get_child_cnt(frame.object);
            if (frame.child < children) {
                lv_obj_t* child = lv_obj_get_child(frame.object, static_cast<int32_t>(frame.child++));
                if (depth + 1 == MaxDepth) { _scanTruncated = true; continue; }
                stack[++depth] = {child, 0};
            } else {
                if (!depth) return;
                --depth;
            }
        }
    }
    RemoteUiJson _json;
    unsigned _offset = 0, _seen = 0, _emitted = 0, _visited = 0;
    lv_obj_t* _focus = nullptr;
    bool _more = false, _scanTruncated = false;
};
} // namespace handheld::diagnostics
