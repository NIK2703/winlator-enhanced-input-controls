#include "touch_processor_internal.h"

void on_drag_start(TouchFinger* f) {
    f->single_tap_hold_delay_ms = 0;
    f->second_tap_fallback_count = 0;
    f->pending_second_double_count = 0;
}

bool gesture_is_within_tap_distance(float x, float y) {
    return fabsf(x - g_state.gesture_last_tap_up_x) <= g_state.cfg.double_tap_distance_px
        && fabsf(y - g_state.gesture_last_tap_up_y) <= g_state.cfg.double_tap_distance_px;
}

void gesture_cancel_double_tap_wait(TouchActionResult* result) {
    g_state.gesture_double_tap_waiting = false;
    if (g_state.gesture_deferred_tap_count > 0) {
        execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
        g_state.gesture_deferred_tap_count = 0;
    }
    g_state.gesture_pending_deferred_double_count = 0;
}

void check_start_drag(TouchFinger* f, float dx, float dy, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    if (f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING) return;
    if (fabsf(dx) <= g_state.cfg.drag_threshold_px && fabsf(dy) <= g_state.cfg.drag_threshold_px) return;

    g_state.gesture_handler_active = false;
    const FingerBindings* fb = &f->bindings;

    const TouchBinding* drag_binding = NULL;
    int drag_count = 0;

    if (f->state == GESTURE_STATE_LONG_PRESSING) {
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            if (has_active_long_press_drag(fb)) {
                drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
            } else if (has_active_long_press(fb)) {
                drag_binding = fb->long_press; drag_count = fb->long_press_count;
            } else {
                on_drag_start(f);
                f->state = GESTURE_STATE_DRAGGING;
                return;
            }
        } else {
            if (has_active_long_press_drag(fb)) {
                drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
            } else {
                on_drag_start(f);
                f->state = GESTURE_STATE_DRAGGING;
                return;
            }
        }
    } else if (g_state.gesture_post_double_tap_drag) {
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            if (has_active_double_tap_drag(fb)) {
                drag_binding = fb->double_tap_drag; drag_count = fb->double_tap_drag_count;
            } else if (has_active_double_tap(fb)) {
                drag_binding = fb->double_tap; drag_count = fb->double_tap_count;
            } else if (has_active_single_tap(fb)) {
                drag_binding = fb->single_tap; drag_count = fb->single_tap_count;
            } else {
                on_drag_start(f);
                f->state = GESTURE_STATE_DRAGGING;
                return;
            }
        } else {
            g_state.gesture_post_double_tap_drag = false;
            if (has_active_double_tap_drag(fb)) {
                drag_binding = fb->double_tap_drag; drag_count = fb->double_tap_drag_count;
            } else {
                on_drag_start(f);
                f->state = GESTURE_STATE_DRAGGING;
                return;
            }
        }
        g_state.gesture_post_double_tap_drag = false;
    } else if (has_active_single_tap_drag(fb)) {
        drag_binding = fb->single_tap_drag; drag_count = fb->single_tap_drag_count;
    } else if (f->is_second_finger && g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD) {
        if (has_active_long_press_drag(fb)) {
            drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
        } else if (has_active_double_tap_drag(fb)) {
            drag_binding = fb->double_tap_drag; drag_count = fb->double_tap_drag_count;
        } else {
            return;
        }
    } else {
        return;
    }

    bool same_as_held = g_state.gesture_is_action_held;
    if (same_as_held && drag_binding && g_state.gesture_held_count > 0) {
        same_as_held = (drag_count == g_state.gesture_held_count);
        if (same_as_held) {
            for (int _i = 0; _i < drag_count && _i < g_state.gesture_held_count; _i++) {
                if (drag_binding[_i].type != g_state.gesture_held_actions[_i].type ||
                    drag_binding[_i].keycode != g_state.gesture_held_actions[_i].keycode) {
                    same_as_held = false; break;
                }
            }
        }
    }

    if (!same_as_held) {
        release_held_actions(result);
        hold_actions(result, drag_binding, drag_count);
    }

    on_drag_start(f);
    f->state = GESTURE_STATE_DRAGGING;
}

void touchpad_handle_tap_up(TouchFinger* f, TouchActionResult* result) {
    const FingerBindings* fb = &f->bindings;
    if (g_state.gesture_pending_double_count > 0) {
        if (!has_active_double_tap_drag(fb)) hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
        else execute_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
        g_state.gesture_pending_double_count = 0; g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_pending_deferred_double_count = 0; g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true; f->state = GESTURE_STATE_IDLE; return;
    }
    if (g_state.gesture_post_double_tap_drag) {
        if (g_state.gesture_pending_deferred_double_count > 0) {
            if (has_active_double_tap_drag(fb)) {
                execute_actions(result, g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count);
            }
            g_state.gesture_pending_deferred_double_count = 0;
        }
        g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true;
        f->state = GESTURE_STATE_IDLE; return;
    }
    if (g_state.gesture_double_tap_consumed) {
        g_state.gesture_double_tap_consumed = false;
        if (!g_state.gesture_is_action_held) {
            if (!has_active_single_tap_drag(fb)) hold_actions(result, fb->single_tap, fb->single_tap_count);
            else execute_actions(result, fb->single_tap, fb->single_tap_count);
        }
        f->state = GESTURE_STATE_IDLE; return;
    }
    bool has_dt = has_active_double_tap(fb);
    if (has_dt) {
        g_state.gesture_deferred_tap_count = 0;
        for (int i = 0; i < fb->single_tap_count && i < 8; i++) g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = fb->single_tap[i];
        g_state.gesture_pending_double_count = 0;
        for (int i = 0; i < fb->double_tap_count && i < 8; i++) g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = fb->double_tap[i];
        g_state.gesture_double_tap_waiting = true;
        g_state.gesture_double_tap_start_time = now_ms();
        g_state.gesture_last_tap_up_x = f->tap_up_x;
        g_state.gesture_last_tap_up_y = f->tap_up_y;
        f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
    } else {
        if (!g_state.gesture_is_action_held) {
            if (!has_active_single_tap_drag(fb)) hold_actions(result, fb->single_tap, fb->single_tap_count);
            else execute_actions(result, fb->single_tap, fb->single_tap_count);
        }
        f->state = has_active_double_tap_drag(fb) ? GESTURE_STATE_DOUBLE_TAP_WAITING : GESTURE_STATE_IDLE;
    }
}
