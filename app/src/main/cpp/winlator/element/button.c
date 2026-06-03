#include "../touch_processor_internal.h"

void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)x; (void)y; (void)time_ms;

    if (e->toggle_switch && e->selected) {
        if (e->bindings[0].type != BINDING_NONE)
            release_binding(result, &e->bindings[0]);
        // Java: toggle-selected on down releases then STOPS — no long-press, no double-tap, no press
        return;
    }

    e->long_press_arm = true;

    if (e->double_tap_waiting) {
        // Second tap confirmed — hold all double-tap bindings (Java pressBindings)
        e->double_tap_waiting = false;
        e->gesture_double_tap_triggered = true;
        e->gesture_swipe_triggered = false; // Java handleDoubleTapConfirmed: gestureTriggered = false
        e->long_press_arm = false;
        for (int k = 0; k < e->element_double_tap_count; k++)
            press_binding(result, &e->element_double_tap[k], true);
        return;
    }

    bool has_dt = e->element_double_tap_count > 0 && e->element_double_tap[0].type != BINDING_NONE;
    bool has_lp = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
    if (has_dt) {
        // Java: hasDoubleTapBinding() && hasLongPressBinding() && !toggleSwitch -> startLongPressTimer
        if (has_lp && !e->toggle_switch) e->long_press_arm = true;
        return;
    }

    // Java: hasLongPressBinding() && !toggleSwitch -> startLongPressTimer (no primary press)
    if (has_lp && !e->toggle_switch) {
        return;
    }

    // Java else: pressBindings(bindings.get(0))
    if (e->bindings[0].type != BINDING_NONE)
        press_binding(result, &e->bindings[0], true);
}

void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    float dx = x - e->down_x;
    float dy = y - e->down_y;

    // Java: cancelDoubleTap when finger leaves bounds during double-tap waiting
    // Java cancelDoubleTap fires primary binding as a tap (press + release)
    if (e->double_tap_waiting && !e->gesture_swipe_triggered
        && !e->gesture_long_press_triggered && !e->gesture_double_tap_triggered
        && !point_in_element(x, y, e)) {
        e->double_tap_waiting = false;
        e->long_press_arm = false; // Java cancelPendingLongPress
        if (e->bindings[0].type != BINDING_NONE) {
            press_binding(result, &e->bindings[0], false);
            release_binding(result, &e->bindings[0]);
        }
    }

    // Java: gesture binding check — hasGestureBinding() && !gestureTriggered && !longPressTriggered && !doubleTapTriggered
    if (!e->gesture_swipe_triggered && !e->gesture_long_press_triggered
        && !e->gesture_double_tap_triggered) {
        bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        if (has_gesture) {
            float gesture_threshold = g_state.cfg.gesture_threshold_px > 0 ?
                (float)g_state.cfg.gesture_threshold_px : 20.0f;
            float dx_sq = dx * dx, dy_sq = dy * dy;
            float dist_sq = dx_sq + dy_sq;
            if (dist_sq > gesture_threshold * gesture_threshold) {
                e->gesture_swipe_triggered = true;
                e->long_press_arm = false;
                e->gesture_long_press_triggered = false;
                e->double_tap_waiting = false;
                // Java: does NOT release primary binding on gesture trigger
                if (e->button_gesture_haptic > 0)
                    add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                for (int k = 0; k < e->element_gesture_count; k++)
                    press_binding(result, &e->element_gesture[k], true);
                return;
            }
        }
    }

    if (e->long_press_arm && !point_in_element(x, y, e)) {
        e->long_press_arm = false;
    }
}

void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y;
    // Java ControlElement.handleTouchUp: debounce for non-toggle L3/R3 bindings
    // isKeepButtonPressedAfterMinTime() = !toggleSwitch && (binding == GAMEPAD_BUTTON_L3 || GAMEPAD_BUTTON_R3)
    if (!e->toggle_switch) {
        int bt0 = e->bindings[0].type;
        if (bt0 == BINDING_GAMEPAD_BASE + 8 || bt0 == BINDING_GAMEPAD_BASE + 9)
            e->selected = (time_ms - e->down_time_ms) > BUTTON_MIN_KEEP_PRESSED_MS;
    }
    if (e->toggle_switch && e->bindings[0].type != BINDING_NONE) {
        e->selected = !e->selected;
        if (e->selected) {
            e->engaged = false;
            e->current_ptr_id = -1;
            return;
        }
        e->engaged = false;
        e->current_ptr_id = -1;
    }

    if (e->gesture_long_press_triggered) {
        // Java releaseHeldBindings: releases long-press bindings only (NOT primary)
        for (int k = e->element_long_press_count - 1; k >= 0; k--) {
            if (e->element_long_press[k].type != BINDING_NONE)
                release_binding(result, &e->element_long_press[k]);
        }
        e->gesture_long_press_triggered = false;
    } else if (e->gesture_double_tap_triggered) {
        // Java releaseHeldBindings: releases double-tap bindings only (NOT primary)
        for (int k = e->element_double_tap_count - 1; k >= 0; k--) {
            if (e->element_double_tap[k].type != BINDING_NONE)
                release_binding(result, &e->element_double_tap[k]);
        }
        e->gesture_double_tap_triggered = false;
    } else if (e->gesture_swipe_triggered) {
        if (e->element_gesture_count > 0) {
            for (int k = e->element_gesture_count - 1; k >= 0; k--) {
                if (e->element_gesture[k].type != BINDING_NONE)
                    release_binding(result, &e->element_gesture[k]);
            }
        }
        e->gesture_swipe_triggered = false;
    } else {
        bool has_dt = e->element_double_tap_count > 0 && e->element_double_tap[0].type != BINDING_NONE;

        if (has_dt && !e->double_tap_waiting) {
            // Java handleTouchUp: first tap-up with double-tap binding
            // cancelPendingLongPress() + startDoubleTapTimer()
            e->gesture_last_tap_time = time_ms;
            e->gesture_tap_up_x = x;
            e->gesture_tap_up_y = y;
            e->double_tap_waiting = true;
            e->long_press_arm = false; // Java cancelPendingLongPress
        } else {
            bool has_lp = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
            if (has_lp && !e->gesture_long_press_triggered) {
                if (e->bindings[0].type != BINDING_NONE) {
                    press_binding(result, &e->bindings[0], true);
                    release_binding(result, &e->bindings[0]);
                }
            } else {
                if (e->bindings[0].type != BINDING_NONE)
                    release_binding(result, &e->bindings[0]);
            }
        }
    }

    e->engaged = false;
    e->current_ptr_id = -1;
}
