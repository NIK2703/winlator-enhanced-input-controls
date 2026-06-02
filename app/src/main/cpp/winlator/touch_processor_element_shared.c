#include "touch_processor_internal.h"

bool point_in_element(float px, float py, const TouchElement* e) {
    float hs = g_state.snapping_size;
    float cx = e->x;
    float cy = e->y;
    float hw, hh;

    switch (e->type) {
        case ELEM_DPAD:
            hw = hs * 7.0f * e->scale;
            hh = hs * 7.0f * e->scale;
            break;
        case ELEM_STICK:
        case ELEM_TRACKPAD:
            hw = hs * 6.0f * e->scale;
            hh = hs * 6.0f * e->scale;
            break;
        case ELEM_BUTTON:
            if (e->shape == SHAPE_CIRCLE) {
                hw = hs * 3.0f * e->scale;
                hh = hs * 3.0f * e->scale;
            } else {
                hw = e->w * hs * 0.5f * e->scale;
                hh = e->h * hs * 0.5f * e->scale;
            }
            break;
        default:
            hw = e->w * hs * 0.5f * e->scale;
            hh = e->h * hs * 0.5f * e->scale;
            break;
    }

    bool hit = px >= cx - hw && px <= cx + hw && py >= cy - hh && py <= cy + hh;
    LOGD("point_in_element: (%.0f,%.0f) vs elem@(%d,%d type=%d c=%.0f,%.0f hw=%.0f hh=%.0f hs=%.0f) -> %s",
         px, py, e->x, e->y, e->type, cx, cy, hw, hh, hs, hit ? "HIT" : "miss");
    return hit;
}

TouchElement* hit_test_element(float x, float y) {
    for (int i = g_state.element_count - 1; i >= 0; i--)
        if (point_in_element(x, y, &g_state.elements[i]))
            return &g_state.elements[i];
    return NULL;
}

int detect_swipe_dir(float dx, float dy, float threshold) {
    if (fabsf(dx) < threshold && fabsf(dy) < threshold) return -1;
    if (fabsf(dx) > fabsf(dy)) return dx > 0 ? 3 : 2;
    return dy > 0 ? 1 : 0;
}

bool finger_has_engaged_element(int ptr_id) {
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].engaged && g_state.elements[i].current_ptr_id == ptr_id)
            return true;
    }
    return false;
}

void handle_element_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    e->current_ptr_id = ptr_id;
    e->down_x = x;
    e->down_y = y;
    e->down_time_ms = time_ms;
    e->engaged = true;
    e->gesture_swipe_triggered = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_direction = -1;
    e->long_press_arm = false;
    LOGD("handle_element_down: type=%d ptr=%d (%.0f,%.0f) bind[0].type=%d", e->type, ptr_id, x, y, e->bindings[0].type);

    switch (e->type) {
        case ELEM_BUTTON: element_button_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_down(e, ptr_id, x, y, time_ms, result); break;
        default: break;
    }
}

void handle_element_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    switch (e->type) {
        case ELEM_BUTTON: element_button_move(e, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_move(e, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_move(e, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_move(e, x, y, time_ms, result); break;
        default: break;
    }
}

void handle_element_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    switch (e->type) {
        case ELEM_BUTTON: element_button_up(e, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_up(e, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_up(e, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_up(e, x, y, time_ms, result); break;
        default: break;
    }
}
