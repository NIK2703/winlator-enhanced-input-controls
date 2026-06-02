#include "touch_processor_internal.h"

// Fire primary binding as a single tap (press + immediate release)
static void fire_primary_tap(TouchElement* e, TouchActionResult* result) {
    if (e->bindings[0].type != BINDING_NONE) {
        press_binding(result, &e->bindings[0], false);
        release_binding(result, &e->bindings[0]);
    }
}

void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id;
    e->long_press_arm = true;

    if (e->double_tap_waiting) {
        float dx = fabsf(x - e->gesture_tap_up_x);
        float dy = fabsf(y - e->gesture_tap_up_y);
        if (dx <= g_state.cfg.double_tap_distance_px && dy <= g_state.cfg.double_tap_distance_px) {
            // Second tap confirmed — fire double-tap bindings, do NOT press primary
            e->double_tap_waiting = false;
            e->gesture_double_tap_first_up = false;
            for (int k = 0; k < e->element_double_tap_count; k++)
                press_binding(result, &e->element_double_tap[k], k == 0);
            LOGD("element_btn_down: double-tap confirmed, firing double-tap bindings");
            return;
        } else {
            // Second tap too far — cancel waiting, treat as new touch
            e->double_tap_waiting = false;
            e->gesture_double_tap_first_up = false;
        }
    }

    // First tap with double-tap bindings: do NOT press primary (matches Java)
    bool has_dt = e->element_double_tap_count > 0 && e->element_double_tap[0].type != BINDING_NONE;
    if (has_dt) {
        e->gesture_double_tap_first_up = false;
        return;
    }

    if (e->toggle_switch && e->selected) {
        e->selected = false;
        if (e->bindings[0].type != BINDING_NONE)
            release_binding(result, &e->bindings[0]);
        return;
    }

    // Normal button: press primary with hold
    if (e->bindings[0].type != BINDING_NONE)
        press_binding(result, &e->bindings[0], true);
}

void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    float dx = x - e->down_x;
    float dy = y - e->down_y;
    float threshold = g_state.cfg.drag_threshold_px > 0 ? g_state.cfg.drag_threshold_px : 20;

    // Java: cancel double-tap when finger leaves element bounds
    if (e->double_tap_waiting && !point_in_element(x, y, e)) {
        e->double_tap_waiting = false;
        e->gesture_double_tap_first_up = false;
        fire_primary_tap(e, result);
        LOGD("element_btn_move: finger left bounds, double-tap cancelled, firing primary tap");
    }

    if (!e->gesture_swipe_triggered) {
        float swipe_threshold = (g_state.cfg.gesture_threshold_px > 0) ?
            (float)g_state.cfg.gesture_threshold_px : threshold;
        int dir = detect_swipe_dir(dx, dy, swipe_threshold);
        if (dir >= 0 && dir < 3 && (dir + 1) < 4 && e->bindings[dir + 1].type != BINDING_NONE) {
            e->gesture_swipe_triggered = true;
            e->gesture_swipe_direction = dir;
            e->long_press_arm = false;
            e->gesture_long_press_triggered = false;
            e->double_tap_waiting = false;

            if (e->bindings[0].type != BINDING_NONE)
                release_binding(result, &e->bindings[0]);

            if (e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE) {
                for (int k = 0; k < e->element_gesture_count; k++)
                    press_binding(result, &e->element_gesture[k], k == 0);
            } else {
                press_binding(result, &e->bindings[dir + 1], true);
            }
            return;
        }
    }

    if (e->long_press_arm && !point_in_element(x, y, e)) {
        e->long_press_arm = false;
    }
}

void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y;
    if (e->gesture_long_press_triggered) {
        for (int k = e->element_long_press_count - 1; k >= 0; k--) {
            if (e->element_long_press[k].type != BINDING_NONE)
                release_binding(result, &e->element_long_press[k]);
        }
        if (e->bindings[0].type != BINDING_NONE)
            release_binding(result, &e->bindings[0]);
        e->gesture_long_press_triggered = false;
    } else if (e->gesture_swipe_triggered) {
        if (e->element_gesture_count > 0) {
            for (int k = e->element_gesture_count - 1; k >= 0; k--) {
                if (e->element_gesture[k].type != BINDING_NONE)
                    release_binding(result, &e->element_gesture[k]);
            }
        } else {
            int dir = e->gesture_swipe_direction;
            int slot = dir + 1;
            if (slot >= 0 && slot < 4 && e->bindings[slot].type != BINDING_NONE)
                release_binding(result, &e->bindings[slot]);
        }
        e->gesture_swipe_triggered = false;
    } else if (e->toggle_switch && e->bindings[0].type != BINDING_NONE) {
        e->selected = true;
    } else {
        bool has_dt = e->element_double_tap_count > 0 && e->element_double_tap[0].type != BINDING_NONE;

        if (e->gesture_double_tap_first_up) {
            // Second tap-up: check timing
            uint64_t elapsed = time_ms - e->gesture_last_tap_time;
            int dt_timeout = g_state.cfg.double_tap_timeout_ms > 0 ? g_state.cfg.double_tap_timeout_ms : 150;
            if (elapsed < (uint64_t)dt_timeout) {
                // Fast enough: fire double-tap bindings (press + release)
                for (int k = 0; k < e->element_double_tap_count; k++)
                    press_binding(result, &e->element_double_tap[k], false);
                LOGD("element_btn_up: double-tap confirmed on second up, firing double-tap");
            }
            // In both cases: release primary if held (e.g. from previous timeout-less lift)
            if (e->bindings[0].type != BINDING_NONE)
                release_binding(result, &e->bindings[0]);
            e->gesture_double_tap_first_up = false;
            e->double_tap_waiting = false;
        } else if (has_dt && !e->double_tap_waiting) {
            // First tap-up: start double-tap waiting, do NOT press primary (matches Java)
            e->gesture_double_tap_first_up = true;
            e->gesture_last_tap_time = time_ms;
            e->gesture_tap_up_x = x;
            e->gesture_tap_up_y = y;
            e->double_tap_waiting = true;
            LOGD("element_btn_up: first tap-up, waiting for second tap");
        } else {
            // No double-tap or timeout already fired: release primary
            if (e->bindings[0].type != BINDING_NONE)
                release_binding(result, &e->bindings[0]);
        }
    }

    e->engaged = false;
    e->current_ptr_id = -1;
}
