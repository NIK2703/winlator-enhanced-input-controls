#include "../touch_processor_internal.h"

void on_drag_start(TouchFinger* f) {
    f->single_tap_hold_delay_ms = 0;
    f->second_tap_fallback_count = 0;
    f->pending_second_double_count = 0;
    g_state.gesture_pending_deferred_double_count = 0;
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_deferred_second_finger_tap = false;
}

bool gesture_is_within_tap_distance(float x, float y) {
    return fabsf(x - g_state.gesture_last_tap_up_x) <= g_state.cfg.double_tap_distance_px
        && fabsf(y - g_state.gesture_last_tap_up_y) <= g_state.cfg.double_tap_distance_px;
}

void gesture_cancel_double_tap_wait(TouchActionResult* result) {
    g_state.gesture_double_tap_waiting = false;
    if (g_state.gesture_deferred_tap_count > 0) {
        // Mirror Java cancelDoubleTapWait: always executeActions (fire-and-forget).
        // Java calls actionExecutor.executeActions(deferredTapAction) unconditionally,
        // NOT executeActionsAndHold. The original C comment was wrong.
        execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
        g_state.gesture_deferred_tap_count = 0;
    }
    g_state.gesture_pending_deferred_double_count = 0;

    // Mirror Java cancelDoubleTapWait: transition main finger from
    // DOUBLE_TAP_WAITING to IDLE so it is not left in a zombie state.
    if (g_state.gesture_main_ptr_id >= 0) {
        for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
            TouchFinger* mf = &g_state.fingers[_fi];
            if (mf->active && mf->ptr_id == g_state.gesture_main_ptr_id) {
                mf->state = GESTURE_STATE_IDLE;
                break;
            }
        }
    }
}

void check_start_drag(TouchFinger* f, float dx, float dy, TouchActionResult* result) {
    if (f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING) return;
    if (fabsf(dx) <= g_state.cfg.drag_threshold_px && fabsf(dy) <= g_state.cfg.drag_threshold_px) return;

    g_state.gesture_handler_active = false;
    const FingerBindings* fb = &f->bindings;

    const TouchBinding* drag_binding = NULL;
    int drag_count = 0;

    if (f->state == GESTURE_STATE_LONG_PRESSING) {
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            // Java TouchscreenGestureHandler.resolveDragAction for LONG_PRESSING:
            //   longPressDrag → longPress → null (no drag)
            if (f->cached_has_active_long_press_drag) {
                drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
            } else if (f->cached_has_active_long_press) {
                drag_binding = fb->long_press; drag_count = fb->long_press_count;
            } else {
                return;
            }
        } else {
            // Java GestureHandler.resolveDragAction for touchpad LONG_PRESSING (non-second-finger):
            //   longPressDrag -> null (no drag), drag does NOT start
            if (f->cached_has_active_long_press_drag) {
                drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
            } else if (f->is_second_finger && f->cached_has_active_single_tap_drag) {
                drag_binding = fb->single_tap_drag; drag_count = fb->single_tap_drag_count;
            } else {
                return;
            }
        }
    } else if (g_state.gesture_post_double_tap_drag) {
        // Use config-level bindings for post-double-tap-drag, not per-finger bindings,
        // because the second finger (that confirmed double-tap) may have a different
        // binding set (ts_single_2nd etc.) that doesn't include double_tap_drag.
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            if (g_state.cfg.ts_double_tap_drag_count > 0) {
                drag_binding = g_state.cfg.ts_double_tap_drag; drag_count = g_state.cfg.ts_double_tap_drag_count;
            } else if (g_state.cfg.ts_double_tap_count > 0) {
                drag_binding = g_state.cfg.ts_double_tap; drag_count = g_state.cfg.ts_double_tap_count;
            } else if (g_state.cfg.ts_single_tap_count > 0) {
                drag_binding = g_state.cfg.ts_single_tap; drag_count = g_state.cfg.ts_single_tap_count;
            } else {
                g_state.gesture_post_double_tap_drag = false;
                return;
            }
        } else {
            // Java resolveDragAction: postDoubleTapDrag -> doubleTapDrag || null
            if (g_state.cfg.tp_double_tap_drag_count == 0) {
                g_state.gesture_post_double_tap_drag = false;
                return;
            }
            drag_binding = g_state.cfg.tp_double_tap_drag; drag_count = g_state.cfg.tp_double_tap_drag_count;
        }
        g_state.gesture_post_double_tap_drag = false;
    } else if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && f->cached_has_active_single_tap_drag) {
        // TouchscreenGestureHandler.resolveDragAction allows singleTapDrag for main finger
        drag_binding = fb->single_tap_drag; drag_count = fb->single_tap_drag_count;
    } else if (f->is_second_finger) {
        // Java GestureHandler.resolveDragAction: secondFingerActive -> singleTapDrag -> longPressDrag -> doubleTapDrag -> null
        if (f->cached_has_active_single_tap_drag) {
            drag_binding = fb->single_tap_drag; drag_count = fb->single_tap_drag_count;
        } else if (f->cached_has_active_long_press_drag) {
            drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
        } else if (f->cached_has_active_double_tap_drag) {
            drag_binding = fb->double_tap_drag; drag_count = fb->double_tap_drag_count;
        } else {
            return;
        }
    } else {
        // GestureHandler.resolveDragAction base: main finger -> null (no drag in TAP_WAITING)
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

    // Mirror Java checkStartDrag: clear pending double-tap action when drag starts
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_deferred_tap_count = 0;

    on_drag_start(f);
    f->state = GESTURE_STATE_DRAGGING;
    
}

void gesture_tick(uint64_t time_ms, TouchActionResult* result) {
    // Single merged loop: long-press, single-tap-hold, second-finger double-tap
    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active) continue;

        if (f->state == GESTURE_STATE_TAP_WAITING) {
            // Long-press timer — mirrors GestureHandler.onLongPressTimer()
            // Java: in longTapMode, TouchscreenGestureHandler posts timer unconditionally
            bool is_ts_longtap_main = (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN
                && g_state.long_tap_mode && f->ptr_id == g_state.gesture_main_ptr_id);
            if ((f->cached_has_long_press_timer || is_ts_longtap_main)
                && !(g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN
                     && f->ptr_id == g_state.gesture_main_ptr_id
                     && (!g_state.long_tap_mode
                         || (!f->cached_has_active_single_tap_drag && (f->travel_x > 0 || f->travel_y > 0))))
            ) {
                if (time_ms - f->down_time_ms >= g_state.cfg.long_press_timeout_ms) {
                    g_state.gesture_handler_active = false;
                    f->second_tap_fallback_count = 0;
                    memset(f->second_tap_fallback, 0, sizeof(f->second_tap_fallback));
                    f->pending_second_double_count = 0;
                    memset(f->pending_second_double, 0, sizeof(f->pending_second_double));
                    if (f->cached_can_hold_long_press)
                        hold_actions(result, f->bindings.long_press, f->bindings.long_press_count);
                    f->state = GESTURE_STATE_LONG_PRESSING;
                    if (f->cached_has_long_press_timer && g_state.cfg.gesture_long_press_haptic > 0)
                        add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
                }
            }

            // Single-tap hold timer — mirrors GestureHandler.singleTapHoldRunnable
            if (f->single_tap_hold_delay_ms > 0
                && time_ms - f->single_tap_hold_timer >= f->single_tap_hold_delay_ms) {
                f->single_tap_hold_delay_ms = 0;
                if (!g_state.gesture_is_action_held) {
                    hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                }
            }
        }

        // Second-finger double-tap timeout — mirrors TouchpadGestureHandler.onSecondFingerDoubleTapTimer()
        if (f->second_double_tap_waiting
            && time_ms - f->second_tap_fallback_time >= g_state.cfg.double_tap_timeout_ms) {
            f->second_double_tap_waiting = false;
            if (f->second_tap_fallback_count > 0) {
                execute_actions(result, f->second_tap_fallback, f->second_tap_fallback_count);
                f->second_tap_fallback_count = 0;
            }
            if (f->state != GESTURE_STATE_DRAGGING) f->state = GESTURE_STATE_IDLE;
        }
    }

    // Gesture double-tap timeout (global, not per-finger) — mirrors GestureHandler.onDoubleTapTimer()
    if (g_state.gesture_double_tap_waiting &&
        time_ms - g_state.gesture_double_tap_start_time >= g_state.cfg.double_tap_timeout_ms) {
        g_state.gesture_double_tap_waiting = false;
        if (g_state.gesture_deferred_tap_count > 0) {
            execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
            g_state.gesture_deferred_tap_count = 0;
        }
        g_state.gesture_pending_deferred_double_count = 0;

        if (g_state.gesture_main_ptr_id >= 0) {
            for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
                TouchFinger* mf = &g_state.fingers[_fi];
                if (mf->active && mf->ptr_id == g_state.gesture_main_ptr_id) {
                    mf->state = GESTURE_STATE_IDLE;
                    break;
                }
            }
        }
    }
}

void handle_tap_up(TouchFinger* f, TouchActionResult* result) {
    // Java GestureHandler.handleTapUp: sleep singleTapDelay before processing
    if (g_state.cfg.single_tap_delay_ms > 0) {
        struct timespec ts_delay;
        ts_delay.tv_sec = g_state.cfg.single_tap_delay_ms / 1000;
        ts_delay.tv_nsec = (g_state.cfg.single_tap_delay_ms % 1000) * 1000000L;
        nanosleep(&ts_delay, NULL);
    }
    const FingerBindings* fb = &f->bindings;

    

    // Java handleTapUp: pendingDoubleTapAction != null → double-tap confirmed (drag case)
    // gesture_pending_deferred_double_count > 0 means second finger confirmed
    // a double-tap-with-drag: pendingDoubleTapAction was saved, fire fire-and-forget
    if (g_state.gesture_pending_deferred_double_count > 0) {
        
        execute_actions(result, g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count);
        g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true;
        f->state = GESTURE_STATE_IDLE;
        
        return;
    }

    // Java handleTapUp: postDoubleTapDrag → second finger up after non-drag double-tap
    // The action was already held by handleDoubleTapConfirmed, just clean up state
    if (g_state.gesture_post_double_tap_drag) {
        
        g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true;
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Java handleTapUp: doubleTapConsumed → third+ finger up, fire single-tap
    if (g_state.gesture_double_tap_consumed) {
        
        g_state.gesture_double_tap_consumed = false;
        if (!g_state.gesture_is_action_held) {
            if (!f->cached_has_active_single_tap_drag) hold_actions(result, fb->single_tap, fb->single_tap_count);
            else execute_actions(result, fb->single_tap, fb->single_tap_count);
        }
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Java handleTapUp normal path: first tap up — set up double-tap waiting
    bool has_dt = f->cached_has_active_double_tap;
    if (has_dt) {
        
        g_state.gesture_deferred_tap_count = 0;
        for (int i = 0; i < fb->single_tap_count && i < 8; i++)
            g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = fb->single_tap[i];
        g_state.gesture_pending_double_count = 0;
        for (int i = 0; i < fb->double_tap_count && i < 8; i++)
            g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = fb->double_tap[i];
        // Also save in deferred field (Java: pendingDeferredDoubleAction)
        // This survives the gesture_pending_double_count = 0 reset at the top of
        // handle_touchscreen_down / handle_touchpad_down on the next finger-down,
        // matching Java's handleDoubleTapConfirmed recovery:
        //   pendingDoubleTapAction = pendingDeferredDoubleAction
        g_state.gesture_pending_deferred_double_count = 0;
        for (int i = 0; i < fb->double_tap_count && i < 8; i++)
            g_state.gesture_pending_deferred_double[g_state.gesture_pending_deferred_double_count++] = fb->double_tap[i];
        g_state.gesture_double_tap_waiting = true;
        g_state.gesture_double_tap_start_time = now_ms();
        g_state.gesture_last_tap_up_x = f->tap_up_x;
        g_state.gesture_last_tap_up_y = f->tap_up_y;
        f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
    } else {
        
        if (!g_state.gesture_is_action_held) {
            if (!f->cached_has_active_single_tap_drag) hold_actions(result, fb->single_tap, fb->single_tap_count);
            else execute_actions(result, fb->single_tap, fb->single_tap_count);
        }
        if (f->cached_has_active_double_tap_drag) {
            
            g_state.gesture_double_tap_waiting = true;
            g_state.gesture_double_tap_start_time = now_ms();
            f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
        } else {
            f->state = GESTURE_STATE_IDLE;
        }
    }
}
