#include "../touch_processor_internal.h"

void touchpad_finger_down(TouchFinger* f, TouchActionResult* result) {
    
    if (g_state.gesture_main_ptr_id < 0) {
        f->is_second_finger = false;
        g_state.gesture_main_ptr_id = f->ptr_id;
        // post_double_tap_drag is reset by the caller (handle_touchscreen_down / handle_touchpad_down)
        // to avoid overwriting the flag when a double-tap was just confirmed upstream.
        // NOTE: gesture_second_active is NOT cleared here — the caller manages it.
        // handle_touchpad_down clears it before calling us.
        // handle_touchscreen_down clears it early and may set it back for deferred second-finger.

        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;

        // Mirror Java GestureHandler / TouchpadGestureHandler: check state==DOUBLE_TAP_WAITING
        // (no deferred_tap_count guard — Java only checks state, which implies deferredTapAction)
        if (g_state.gesture_double_tap_waiting) {
            float dx = fabsf(f->x - g_state.gesture_last_tap_up_x);
            float dy = fabsf(f->y - g_state.gesture_last_tap_up_y);
            if (dx <= g_state.cfg.double_tap_distance_px && dy <= g_state.cfg.double_tap_distance_px) {
                // Confirm double-tap (mirrors handleDoubleTapConfirmed)
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;
                if (g_state.gesture_pending_double_count > 0) {
                    bool has_dt_drag = (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN)
                        ? (g_state.cfg.ts_double_tap_drag_count > 0)
                        : (g_state.cfg.tp_double_tap_drag_count > 0);
                    if (!has_dt_drag) {
                        for (int _pi = 0; _pi < g_state.gesture_pending_double_count; _pi++)
                            press_binding(result, &g_state.gesture_pending_double[_pi], false);
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                return;
            } else {
                // Cancel: mirror Java cancelDoubleTapWait
                g_state.gesture_double_tap_waiting = false;
                if (g_state.gesture_deferred_tap_count > 0) {
                    execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
                    g_state.gesture_deferred_tap_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;
            }
        }

        if (f->bindings.single_tap_count > 0 || f->bindings.long_press_count > 0 || f->bindings.double_tap_count > 0
            || f->bindings.single_tap_drag_count > 0 || f->bindings.long_press_drag_count > 0 || f->bindings.double_tap_drag_count > 0)
            g_state.gesture_handler_active = true;
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN
            && !g_state.gesture_is_action_held) {
            if (has_active_single_tap(&f->bindings) && !has_active_single_tap_drag(&f->bindings)) {
                bool has_dt = has_active_double_tap(&f->bindings) || has_active_double_tap_drag(&f->bindings);
                if (has_dt) {
                    
                    g_state.gesture_deferred_tap_count = 0;
                    for (int i = 0; i < f->bindings.single_tap_count && i < 8; i++)
                        g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = f->bindings.single_tap[i];
                    g_state.gesture_pending_double_count = 0;
                    for (int i = 0; i < f->bindings.double_tap_count && i < 8; i++)
                        g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = f->bindings.double_tap[i];
                    f->single_tap_hold_delay_ms = g_state.cfg.double_tap_timeout_ms;
                    f->single_tap_hold_timer = now_ms();
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

        // Java TouchpadGestureHandler.onFingerDown: check global double-tap waiting for second finger
        if (g_state.gesture_double_tap_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                // Java: handleDoubleTapConfirmed - confirm double-tap
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                // Java handleDoubleTapConfirmed: pendingDoubleTapAction = pendingDeferredDoubleAction
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;
                bool has_dt_drag = (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN)
                    ? (g_state.cfg.ts_double_tap_drag_count > 0)
                    : (g_state.cfg.tp_double_tap_drag_count > 0);
                if (g_state.gesture_pending_double_count > 0) {
                    if (!has_dt_drag) {
                        for (int _pi = 0; _pi < g_state.gesture_pending_double_count; _pi++)
                            press_binding(result, &g_state.gesture_pending_double[_pi], false);
                    } else {
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        // Java TouchpadGestureHandler.onFingerDown (touchpad-only): store single-tap as
        // pendingSecondTapAction. Guard with touch_mode because TouchscreenGestureHandler
        // has no equivalent fields — the C struct has them always but they're zero.
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD) {
            f->second_tap_fallback_count = 0;
            for (int _i = 0; _i < f->bindings.single_tap_count && _i < 8; _i++) {
                f->second_tap_fallback[_i] = f->bindings.single_tap[_i];
                f->second_tap_fallback_count++;
            }
        }

        // Java TouchpadGestureHandler.onFingerDown (touchpad-only): check secondFingerDoubleTapWaiting
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && f->second_double_tap_waiting) {
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
            // Java: resetLongPressTimer() — clear and repost long-press timer
            f->down_time_ms = now_ms();
            if (f->bindings.single_tap_count > 0 || f->bindings.long_press_count > 0 || f->bindings.double_tap_count > 0
                || f->bindings.single_tap_drag_count > 0 || f->bindings.long_press_drag_count > 0 || f->bindings.double_tap_drag_count > 0)
                g_state.gesture_handler_active = true;
        }
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && !g_state.gesture_is_action_held) {
            if (has_active_single_tap(&f->bindings) && !has_active_single_tap_drag(&f->bindings)) {
                bool has_dt = has_active_double_tap(&f->bindings) || has_active_double_tap_drag(&f->bindings);
                if (has_dt) {
                    // Second finger: use per-finger state, don't overwrite first-finger
                    // globals g_state.gesture_deferred_tap[] / g_state.gesture_pending_double[]
                    f->single_tap_hold_delay_ms = g_state.cfg.double_tap_timeout_ms;
                    f->single_tap_hold_timer = now_ms();
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
            f->pending_second_double_count = 0;
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
        g_state.gesture_post_double_tap_drag = false;

        if (f->state == GESTURE_STATE_LONG_PRESSING && has_active_long_press(&f->bindings) && !can_hold_long_press(&f->bindings)) {
            execute_actions(result, f->bindings.long_press, f->bindings.long_press_count);
        }

        // Java: postDoubleTapDrag = false; if (!secondFingerDoubleTapWaiting && state != DRAGGING) state = IDLE
        if (!f->second_double_tap_waiting && f->state != GESTURE_STATE_DRAGGING) {
            f->state = GESTURE_STATE_IDLE;
        }

        f->active = false;
        return;
    }
    f->tap_up_x = f->x;
    f->tap_up_y = f->y;

    switch (f->state) {
        case GESTURE_STATE_TAP_WAITING: {
            if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD
                && !(f->travel_x < MAX_TAP_TRAVEL && f->travel_y < MAX_TAP_TRAVEL && (now_ms() - f->down_time_ms) < TAP_MAX_TIME_MS)) {
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                g_state.gesture_pending_double_count = 0;
                g_state.gesture_pending_deferred_double_count = 0;
                g_state.gesture_main_ptr_id = -1;
                f->active = false;
                break;
            }
            handle_tap_up(f, result); release_held_actions(result); g_state.gesture_main_ptr_id = -1; f->active = false;
            break;
        }
        case GESTURE_STATE_LONG_PRESSING: release_held_actions(result); if (has_active_long_press(&f->bindings) && !can_hold_long_press(&f->bindings)) execute_actions(result, f->bindings.long_press, f->bindings.long_press_count); g_state.gesture_post_double_tap_drag = false; g_state.gesture_second_active = false; g_state.gesture_deferred_tap_count = 0; g_state.gesture_pending_double_count = 0; g_state.gesture_pending_deferred_double_count = 0; g_state.gesture_main_ptr_id = -1; f->active = false; break;
        case GESTURE_STATE_DRAGGING: release_held_actions(result); g_state.gesture_post_double_tap_drag = false; g_state.gesture_second_active = false; g_state.gesture_deferred_tap_count = 0; g_state.gesture_pending_double_count = 0; g_state.gesture_pending_deferred_double_count = 0; g_state.gesture_main_ptr_id = -1; f->active = false; break;
        case GESTURE_STATE_DOUBLE_TAP_WAITING: {
            if (!g_state.gesture_is_action_held) {
                if (!has_active_single_tap_drag(&f->bindings)) {
                    hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                    release_held_actions(result);
                } else {
                    execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                }
            }
            g_state.gesture_double_tap_waiting = false;
            g_state.gesture_deferred_tap_count = 0;
            g_state.gesture_pending_double_count = 0;
            g_state.gesture_pending_deferred_double_count = 0;
            g_state.gesture_post_double_tap_drag = false;
            g_state.gesture_second_active = false;
            g_state.gesture_main_ptr_id = -1;
            f->active = false;
            break;
        }
        default:
            
            if (g_state.gesture_post_double_tap_drag) {
                // Second tap up after double-tap confirm: release the held double-tap action
                // (touchpad_finger_down was skipped for this finger so state is IDLE)
                
                g_state.gesture_post_double_tap_drag = false;
                release_held_actions(result);
            }
            g_state.gesture_main_ptr_id = -1;
            f->active = false;
            break;
    }
}
