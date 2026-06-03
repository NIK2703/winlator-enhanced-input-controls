#include "../touch_processor_internal.h"

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
        case ELEM_RANGE_BUTTON:
            hw = hs * ((e->range_binding_count * 4) / 2) * e->scale;
            hh = hs * 2.0f * e->scale;
            if (e->range_orientation == 1) { float t = hw; hw = hh; hh = t; }
            break;
        default:
            hw = e->w * hs * 0.5f * e->scale;
            hh = e->h * hs * 0.5f * e->scale;
            break;
    }

    bool hit = px >= cx - hw && px <= cx + hw && py >= cy - hh && py <= cy + hh;
    
    return hit;
}

TouchElement* hit_test_element(float x, float y) {
    for (int i = g_state.element_count - 1; i >= 0; i--)
        if (point_in_element(x, y, &g_state.elements[i]))
            return &g_state.elements[i];
    return NULL;
}

// Cubic bezier interpolation: given input x in [0,1] and control points (0,0), (cpx1,cpy1), (0.45,0.95), (1,1),
// find t where B_x(t) ≈ |x|, then return B_y(t) preserving sign.
// Control points match Java's CubicBezierInterpolator.set(0.075f, 0.95f, 0.45f, 0.95f).
float cubic_bezier_interpolate(float x, float cpx1, float cpy1) {
    float abs_x = fabsf(x);
    if (abs_x <= 0.0001f) return 0.0f;
    if (abs_x >= 1.0f) return x > 0 ? 1.0f : -1.0f;

    // Binary search for t where B_x(t) ≈ abs_x (16 iterations for ~6e-6 precision)
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 16; i++) {
        float mid = (lo + hi) * 0.5f;
        float omt = 1.0f - mid;
        float bx = 3.0f * omt * omt * mid * cpx1 + 3.0f * omt * mid * mid * 0.45f + mid * mid * mid;
        if (bx < abs_x) lo = mid;
        else hi = mid;
    }
    float t = (lo + hi) * 0.5f;
    float omt = 1.0f - t;
    float by = 3.0f * omt * omt * t * cpy1 + 3.0f * omt * t * t * 0.95f + t * t * t;
    return x > 0 ? by : -by;
}

int detect_swipe_dir(float dx, float dy, float threshold) {
    if (fabsf(dx) < threshold && fabsf(dy) < threshold) return -1;
    if (fabsf(dx) > fabsf(dy)) return dx > 0 ? 3 : 2;
    return dy > 0 ? 1 : 0;
}

bool is_mouse_move_binding(const TouchBinding* b) {
    return b->type >= BINDING_MOUSE_MOVE_LEFT && b->type <= BINDING_MOUSE_MOVE_DOWN;
}

bool finger_has_engaged_element(int ptr_id) {
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].engaged && g_state.elements[i].current_ptr_id == ptr_id)
            return true;
    }
    return false;
}

void handle_element_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java ControlElement.handleTouchDown: if (currentPointerId == -1 && containsPoint(x, y))
    // containsPoint is checked by the caller; guard already-engaged elements here.
    if (e->current_ptr_id >= 0) {
        
        return;
    }
    e->current_ptr_id = ptr_id;
    e->down_x = x;
    e->down_y = y;
    e->down_time_ms = time_ms;
    e->engaged = true;
    e->gesture_swipe_triggered = false;
    e->gesture_long_press_triggered = false;
    e->gesture_double_tap_triggered = false;
    e->gesture_swipe_direction = -1;
    e->long_press_arm = false;
    

    switch (e->type) {
        case ELEM_BUTTON: element_button_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_RANGE_BUTTON: element_range_button_down(e, ptr_id, x, y, time_ms, result); break;
        default: break;
    }
}

void handle_element_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    switch (e->type) {
        case ELEM_BUTTON: element_button_move(e, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_move(e, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_move(e, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_move(e, x, y, time_ms, result); break;
        case ELEM_RANGE_BUTTON: element_range_button_move(e, x, y, time_ms, result); break;
        default: break;
    }
}

void handle_element_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    switch (e->type) {
        case ELEM_BUTTON: element_button_up(e, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_up(e, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_up(e, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_up(e, x, y, time_ms, result); break;
        case ELEM_RANGE_BUTTON: element_range_button_up(e, x, y, time_ms, result); break;
        default: break;
    }
}

void release_element_bindings(TouchElement* e, TouchActionResult* result) {
    if (e->bindings[0].type != BINDING_NONE)
        release_binding(result, &e->bindings[0]);
    if (e->gesture_swipe_triggered) {
        for (int k = e->element_gesture_count - 1; k >= 0; k--)
            if (e->element_gesture[k].type != BINDING_NONE)
                release_binding(result, &e->element_gesture[k]);
    }
    if (e->gesture_long_press_triggered) {
        for (int k = e->element_long_press_count - 1; k >= 0; k--)
            if (e->element_long_press[k].type != BINDING_NONE)
                release_binding(result, &e->element_long_press[k]);
    }
    if (e->gesture_double_tap_triggered) {
        for (int k = e->element_double_tap_count - 1; k >= 0; k--)
            if (e->element_double_tap[k].type != BINDING_NONE)
                release_binding(result, &e->element_double_tap[k]);
    }
    e->long_press_arm = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_triggered = false;
    e->gesture_double_tap_triggered = false;
    e->double_tap_waiting = false;
    e->current_ptr_id = -1;
    e->engaged = false;
}

void touch_finger_cache_bs(TouchFinger* f) {
    GestureBindingSet bs = gesture_build_binding_set(&f->bindings);
    f->cached_has_active_single_tap = bs.has_active_single_tap;
    f->cached_has_active_double_tap = bs.has_active_double_tap;
    f->cached_has_active_long_press = bs.has_active_long_press;
    f->cached_has_active_single_tap_drag = bs.has_active_single_tap_drag;
    f->cached_has_active_long_press_drag = bs.has_active_long_press_drag;
    f->cached_has_active_double_tap_drag = bs.has_active_double_tap_drag;
    f->cached_can_hold_long_press = bs.can_hold_long_press;
    f->cached_has_long_press_timer = bs.has_long_press_timer;
}

