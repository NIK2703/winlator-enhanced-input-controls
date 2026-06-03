#include "touch_processor_internal.h"

void touchpad_finger_down(TouchFinger* f, TouchActionResult* result) {
    LOGD("touchpad_finger_down: ptr=%d main_ptr=%d handler_active=%d", f->ptr_id, g_state.gesture_main_ptr_id, g_state.gesture_handler_active);
    if (g_state.gesture_main_ptr_id < 0) {
        f->is_second_finger = false;
        g_state.gesture_main_ptr_id = f->ptr_id;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_second_active = false;

        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;

        if (g_state.gesture_double_tap_waiting && g_state.gesture_deferred_tap_count > 0) {
            float dx = fabsf(f->x - g_state.gesture_last_tap_up_x);
            float dy = fabsf(f->y - g_state.gesture_last_tap_up_y);
            if (dx <= g_state.cfg.double_tap_distance_px && dy <= g_state.cfg.double_tap_distance_px) {
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                g_state.gesture_pending_deferred_double_count = 0;
                if (g_state.gesture_pending_double_count > 0) {
                    if (!has_active_double_tap_drag(&f->bindings)) {
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                return;
            } else {
                g_state.gesture_double_tap_waiting = false;
                if (g_state.gesture_deferred_tap_count > 0) {
                    execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
                    g_state.gesture_deferred_tap_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;
            }
        }

        if (has_long_press_timer(&f->bindings)) g_state.gesture_handler_active = true;
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && !f->is_second_finger
            && !g_state.gesture_is_action_held && !g_state.gesture_post_double_tap_drag) {
            if (has_active_single_tap(&f->bindings) && !has_active_single_tap_drag(&f->bindings)) {
                bool has_dt = has_active_double_tap(&f->bindings) || has_active_double_tap_drag(&f->bindings);
                if (has_dt) {
                    g_state.gesture_deferred_tap_count = 0;
                    for (int i = 0; i < f->bindings.single_tap_count && i < 8; i++)
                        g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = f->bindings.single_tap[i];
                    g_state.gesture_pending_double_count = 0;
                    for (int i = 0; i < f->bindings.double_tap_count && i < 8; i++)
                        g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = f->bindings.double_tap[i];
                } else if (g_state.cfg.single_tap_delay_ms > 0) {
                    f->single_tap_hold_delay_ms = g_state.cfg.single_tap_delay_ms;
                    f->single_tap_hold_timer = now_ms();
                } else {
                    hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                }
            }
        }
    } else if (f->ptr_id != g_state.gesture_main_ptr_id) {
        f->is_second_finger = true;
        g_state.gesture_second_active = true;
        g_state.gesture_handler_active = false;

        FingerBindings* fb2 = &f->bindings;
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            fb2->single_tap_count = g_state.cfg.ts_single_2nd_count;
            memcpy(fb2->single_tap, g_state.cfg.ts_single_2nd, sizeof(g_state.cfg.ts_single_2nd));
            fb2->long_press_count = g_state.cfg.ts_long_2nd_count;
            memcpy(fb2->long_press, g_state.cfg.ts_long_2nd, sizeof(g_state.cfg.ts_long_2nd));
            fb2->double_tap_count = g_state.cfg.ts_double_2nd_count;
            memcpy(fb2->double_tap, g_state.cfg.ts_double_2nd, sizeof(g_state.cfg.ts_double_2nd));
            fb2->single_tap_drag_count = g_state.cfg.ts_single_drag_2nd_count;
            memcpy(fb2->single_tap_drag, g_state.cfg.ts_single_drag_2nd, sizeof(g_state.cfg.ts_single_drag_2nd));
            fb2->long_press_drag_count = g_state.cfg.ts_long_drag_2nd_count;
            memcpy(fb2->long_press_drag, g_state.cfg.ts_long_drag_2nd, sizeof(g_state.cfg.ts_long_drag_2nd));
            fb2->double_tap_drag_count = g_state.cfg.ts_double_drag_2nd_count;
            memcpy(fb2->double_tap_drag, g_state.cfg.ts_double_drag_2nd, sizeof(g_state.cfg.ts_double_drag_2nd));
        } else {
            fb2->single_tap_count = g_state.cfg.tp_single_2nd_count;
            memcpy(fb2->single_tap, g_state.cfg.tp_single_2nd, sizeof(g_state.cfg.tp_single_2nd));
            fb2->long_press_count = g_state.cfg.tp_long_2nd_count;
            memcpy(fb2->long_press, g_state.cfg.tp_long_2nd, sizeof(g_state.cfg.tp_long_2nd));
            fb2->double_tap_count = g_state.cfg.tp_double_2nd_count;
            memcpy(fb2->double_tap, g_state.cfg.tp_double_2nd, sizeof(g_state.cfg.tp_double_2nd));
            fb2->single_tap_drag_count = g_state.cfg.tp_single_drag_2nd_count;
            memcpy(fb2->single_tap_drag, g_state.cfg.tp_single_drag_2nd, sizeof(g_state.cfg.tp_single_drag_2nd));
            fb2->long_press_drag_count = g_state.cfg.tp_long_drag_2nd_count;
            memcpy(fb2->long_press_drag, g_state.cfg.tp_long_drag_2nd, sizeof(g_state.cfg.tp_long_drag_2nd));
            fb2->double_tap_drag_count = g_state.cfg.tp_double_drag_2nd_count;
            memcpy(fb2->double_tap_drag, g_state.cfg.tp_double_drag_2nd, sizeof(g_state.cfg.tp_double_drag_2nd));
        }

        if (f->second_double_tap_waiting) {
            f->second_double_tap_waiting = false;
            f->second_tap_fallback_count = 0;
            if (has_active_double_tap(&f->bindings)) {
                if (has_active_double_tap_drag(&f->bindings)) {
                    f->pending_second_double_count = 0;
                    for (int _i = 0; _i < f->bindings.double_tap_count && _i < 8; _i++)
                        f->pending_second_double[_i] = f->bindings.double_tap[_i];
                    f->pending_second_double_count = f->bindings.double_tap_count;
                    g_state.gesture_post_double_tap_drag = true;
                } else {
                    hold_actions(result, f->bindings.double_tap, f->bindings.double_tap_count);
                }
            }
            if (has_long_press_timer(&f->bindings)) g_state.gesture_handler_active = true;
            f->state = GESTURE_STATE_TAP_WAITING;
            return;
        }

        release_held_actions(result);

        f->second_tap_fallback_count = 0;
        for (int i = 0; i < f->bindings.single_tap_count && i < 8; i++) {
            f->second_tap_fallback[i] = f->bindings.single_tap[i];
            f->second_tap_fallback_count++;
        }

        if (has_long_press_timer(&f->bindings)) g_state.gesture_handler_active = true;
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && !g_state.gesture_is_action_held
            && !g_state.gesture_post_double_tap_drag) {
            if (has_active_single_tap(&f->bindings) && !has_active_single_tap_drag(&f->bindings)) {
                bool has_dt = has_active_double_tap(&f->bindings) || has_active_double_tap_drag(&f->bindings);
                if (has_dt) {
                    g_state.gesture_deferred_tap_count = 0;
                    for (int i = 0; i < f->bindings.single_tap_count && i < 8; i++)
                        g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = f->bindings.single_tap[i];
                    g_state.gesture_pending_double_count = 0;
                    for (int i = 0; i < f->bindings.double_tap_count && i < 8; i++)
                        g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = f->bindings.double_tap[i];
                } else if (g_state.cfg.single_tap_delay_ms > 0) {
                    f->single_tap_hold_delay_ms = g_state.cfg.single_tap_delay_ms;
                    f->single_tap_hold_timer = now_ms();
                } else {
                    hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                }
            }
        }
    }
}
void touchpad_finger_up(TouchFinger* f, TouchActionResult* result) {
    if (f->is_second_finger) {
        if (g_state.gesture_is_action_held) {
            f->second_double_tap_waiting = false;
            f->second_tap_fallback_count = 0;
        } else if (f->second_tap_fallback_count > 0 && has_active_double_tap(&f->bindings)) {
            f->second_double_tap_waiting = true;
            f->second_tap_fallback_time = now_ms();
        } else if (f->second_tap_fallback_count > 0) {
            execute_actions(result, f->second_tap_fallback, f->second_tap_fallback_count);
            f->second_tap_fallback_count = 0;
        }

        if (f->pending_second_double_count > 0) {
            execute_actions(result, f->pending_second_double, f->pending_second_double_count);
            f->pending_second_double_count = 0;
        }

        release_held_actions(result);

        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 1, 0, 0);

        g_state.gesture_second_active = false;
        g_state.gesture_second_ptr_id = -1;

        if (f->state == GESTURE_STATE_LONG_PRESSING && has_active_long_press(&f->bindings) && !can_hold_long_press(&f->bindings)) {
            execute_actions(result, f->bindings.long_press, f->bindings.long_press_count);
        }

        f->active = false;
        return;
    }
    g_state.gesture_double_tap_waiting = false;

    f->tap_up_x = f->x;
    f->tap_up_y = f->y;

    switch (f->state) {
        case GESTURE_STATE_TAP_WAITING: touchpad_handle_tap_up(f, result); release_held_actions(result); g_state.gesture_main_ptr_id = -1; f->active = false; break;
        case GESTURE_STATE_LONG_PRESSING: release_held_actions(result); if (has_active_long_press(&f->bindings) && !can_hold_long_press(&f->bindings)) execute_actions(result, f->bindings.long_press, f->bindings.long_press_count); g_state.gesture_main_ptr_id = -1; f->active = false; break;
        case GESTURE_STATE_DRAGGING: release_held_actions(result); g_state.gesture_main_ptr_id = -1; f->active = false; break;
        case GESTURE_STATE_DOUBLE_TAP_WAITING: if (!has_active_single_tap_drag(&f->bindings)) { hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count); release_held_actions(result); } else execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count); g_state.gesture_main_ptr_id = -1; f->active = false; break;
        default: g_state.gesture_main_ptr_id = -1; f->active = false; break;
    }
}
