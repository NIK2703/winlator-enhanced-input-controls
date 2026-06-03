#include "../touch_processor_internal.h"

void on_drag_start(TouchFinger* f) {
    f->single_tap_hold_delay_ms = 0;
    g_state.second_tap_fallback_count = 0;
    g_state.pending_second_double_count = 0;
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
        // Java GestureHandler.resolveDragAction for LONG_PRESSING:
        //   longPressDrag → null (first finger)
        // Java TouchscreenGestureHandler override: longPressDrag → longPress → null
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            if (f->cached_has_active_long_press_drag) {
                drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
            } else if (f->cached_has_active_long_press) {
                drag_binding = fb->long_press; drag_count = fb->long_press_count;
            } else {
                return;
            }
        } else {
            if (f->cached_has_active_long_press_drag) {
                drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
            } else {
                return;
            }
        }
    } else if (g_state.gesture_post_double_tap_drag) {
        // Use config-level bindings for post-double-tap-drag, not per-finger bindings,
        // because the second finger (that confirmed double-tap) may have a different
        // binding set (ts_single_2nd etc.) that doesn't include double_tap_drag.
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            // Java TouchscreenGestureHandler.resolveDragAction:
            //   postDoubleTapDrag → hasActiveDoubleTapDrag() ? activeDoubleTapDragAction() : fallback
            //   Uses cur (switched to secondFingerSet when gesture_second_active)
            if (g_state.gesture_second_active) {
                if (g_state.cfg.ts_double_drag_2nd_count > 0) {
                    drag_binding = g_state.cfg.ts_double_drag_2nd; drag_count = g_state.cfg.ts_double_drag_2nd_count;
                } else if (g_state.cfg.ts_double_2nd_count > 0) {
                    drag_binding = g_state.cfg.ts_double_2nd; drag_count = g_state.cfg.ts_double_2nd_count;
                } else if (g_state.cfg.ts_single_2nd_count > 0) {
                    drag_binding = g_state.cfg.ts_single_2nd; drag_count = g_state.cfg.ts_single_2nd_count;
                } else {
                    g_state.gesture_post_double_tap_drag = false;
                    return;
                }
            } else {
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
            }
        } else {
            // Java resolveDragAction: postDoubleTapDrag -> doubleTapDrag || null
            // When second finger is active, use second-finger bindings (matches Java cur switching)
            bool use_second = g_state.gesture_second_active;
            const TouchBinding* dt_drag = use_second ? g_state.cfg.tp_double_drag_2nd : g_state.cfg.tp_double_tap_drag;
            int dt_drag_count = use_second ? g_state.cfg.tp_double_drag_2nd_count : g_state.cfg.tp_double_tap_drag_count;
            if (dt_drag_count == 0) {
                g_state.gesture_post_double_tap_drag = false;
                return;
            }
            drag_binding = dt_drag; drag_count = dt_drag_count;
        }
        g_state.gesture_post_double_tap_drag = false;
    } else if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        // Java TouchscreenGestureHandler.resolveDragAction:
        //   hasActiveSingleTapDrag() ? activeSingleTapDragAction() : null
        //   Uses cur (switched to secondFingerSet when gesture_second_active)
        if (g_state.gesture_second_active) {
            if (g_state.cfg.ts_single_drag_2nd_count > 0) {
                drag_binding = g_state.cfg.ts_single_drag_2nd; drag_count = g_state.cfg.ts_single_drag_2nd_count;
            } else {
                return;
            }
        } else if (f->cached_has_active_single_tap_drag) {
            drag_binding = fb->single_tap_drag; drag_count = fb->single_tap_drag_count;
        } else {
            return;
        }
    } else if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && g_state.gesture_second_active) {
        // Java GestureHandler.resolveDragAction: secondFingerActive -> singleTapDrag -> doubleTapDrag -> null
        if (g_state.cfg.tp_single_drag_2nd_count > 0) {
            drag_binding = g_state.cfg.tp_single_drag_2nd; drag_count = g_state.cfg.tp_single_drag_2nd_count;
        } else if (g_state.cfg.tp_double_drag_2nd_count > 0) {
            drag_binding = g_state.cfg.tp_double_drag_2nd; drag_count = g_state.cfg.tp_double_drag_2nd_count;
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
            bool has_lp_timer = f->cached_has_long_press_timer;

            // Mirror Java: skip ALL long-press processing when no timer is active.
            if (has_lp_timer) {
                // Determine if long-press should be cancelled due to finger movement.
                bool cancel_lp = false;
                if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN
                    && f->ptr_id == g_state.gesture_main_ptr_id) {
                    cancel_lp = !f->cached_has_active_single_tap_drag && (f->travel_x > 0 || f->travel_y > 0);
                } else if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD
                           && (f->ptr_id == g_state.gesture_main_ptr_id || g_state.gesture_second_active)) {
                    // Mirror Java: checkStartDrag cancels long-press when net displacement > dragThreshold.
                    cancel_lp = fabsf(f->x - f->down_x) > g_state.cfg.drag_threshold_px
                        || fabsf(f->y - f->down_y) > g_state.cfg.drag_threshold_px;
                }

                if (!cancel_lp) {
                    if (time_ms - f->down_time_ms >= g_state.cfg.long_press_timeout_ms) {
                        g_state.gesture_handler_active = false;
                        g_state.second_tap_fallback_count = 0;
                        memset(g_state.second_tap_fallback, 0, sizeof(g_state.second_tap_fallback));
                        g_state.pending_second_double_count = 0;
                        memset(g_state.pending_second_double, 0, sizeof(g_state.pending_second_double));
                        if (f->cached_can_hold_long_press)
                            hold_actions(result, f->bindings.long_press, f->bindings.long_press_count);
                        f->state = GESTURE_STATE_LONG_PRESSING;
                        if (f->cached_has_long_press_timer && g_state.cfg.gesture_long_press_haptic > 0)
                            add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
                    }
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
    }

    // Second-finger double-tap timeout — mirrors TouchpadGestureHandler.onSecondFingerDoubleTapTimer()
    // MUST be outside the per-finger loop because it uses global state and setting f->state
    // on whichever finger the loop happens to be iterating is incorrect.
    if (g_state.second_double_tap_waiting
        && time_ms - g_state.second_tap_fallback_time >= g_state.cfg.double_tap_timeout_ms) {
        g_state.second_double_tap_waiting = false;
        if (g_state.second_tap_fallback_count > 0) {
            execute_actions(result, g_state.second_tap_fallback, g_state.second_tap_fallback_count);
            g_state.second_tap_fallback_count = 0;
        }
        // Mirror Java: if (state != State.DRAGGING) state = State.IDLE
        // Set main finger (not the second finger, which is already deactivated) to IDLE.
        if (g_state.gesture_main_ptr_id >= 0) {
            for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
                TouchFinger* mf = &g_state.fingers[_fi];
                if (mf->active && mf->ptr_id == g_state.gesture_main_ptr_id) {
                    if (mf->state != GESTURE_STATE_DRAGGING) mf->state = GESTURE_STATE_IDLE;
                    break;
                }
            }
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

void handle_tap_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms) {
    // Java GestureHandler.handleTapUp: sleep singleTapDelay before processing
    if (g_state.cfg.single_tap_delay_ms > 0) {
        struct timespec ts_delay;
        ts_delay.tv_sec = g_state.cfg.single_tap_delay_ms / 1000;
        ts_delay.tv_nsec = (g_state.cfg.single_tap_delay_ms % 1000) * 1000000L;
        nanosleep(&ts_delay, NULL);
    }
    const FingerBindings* fb = &f->bindings;

    // Java GestureHandler.handleTapUp: cur = secondFingerSet when secondFingerActive
    int active_single_count = fb->single_tap_count;
    const TouchBinding* active_single = fb->single_tap;
    int active_double_count = fb->double_tap_count;
    const TouchBinding* active_double = fb->double_tap;
    bool active_has_single_tap_drag = f->cached_has_active_single_tap_drag;
    bool active_has_double_tap = f->cached_has_active_double_tap;
    bool active_has_double_tap_drag = f->cached_has_active_double_tap_drag;
    if (g_state.gesture_second_active) {
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            active_single = g_state.cfg.ts_single_2nd;
            active_single_count = g_state.cfg.ts_single_2nd_count;
            active_double = g_state.cfg.ts_double_2nd;
            active_double_count = g_state.cfg.ts_double_2nd_count;
            active_has_single_tap_drag = g_state.cfg.ts_single_drag_2nd_count > 0;
            active_has_double_tap = g_state.cfg.ts_double_2nd_count > 0;
            active_has_double_tap_drag = g_state.cfg.ts_double_drag_2nd_count > 0;
        } else {
            active_single = g_state.cfg.tp_single_2nd;
            active_single_count = g_state.cfg.tp_single_2nd_count;
            active_double = g_state.cfg.tp_double_2nd;
            active_double_count = g_state.cfg.tp_double_2nd_count;
            active_has_single_tap_drag = g_state.cfg.tp_single_drag_2nd_count > 0;
            active_has_double_tap = g_state.cfg.tp_double_2nd_count > 0;
            active_has_double_tap_drag = g_state.cfg.tp_double_drag_2nd_count > 0;
        }
    }

    // Java handleTapUp: pendingDoubleTapAction != null → double-tap confirmed (drag case)
    // Java re-evaluates hasActiveDoubleTapDrag() at lift time (uses cur).
    // When cur = firstFingerSet (second finger already lifted), uses first-finger bindings.
    // When cur = secondFingerSet (second finger still down), uses second-finger bindings.
    if (g_state.gesture_pending_deferred_double_count > 0) {
        bool has_dt_drag = (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN)
            ? (g_state.gesture_second_active ? g_state.cfg.ts_double_drag_2nd_count > 0 : g_state.cfg.ts_double_tap_drag_count > 0)
            : (g_state.gesture_second_active ? g_state.cfg.tp_double_drag_2nd_count > 0 : g_state.cfg.tp_double_tap_drag_count > 0);
        if (has_dt_drag) {
            execute_actions(result, g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count);
        } else {
            hold_actions(result, g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count);
        }
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
            if (!active_has_single_tap_drag) hold_actions(result, active_single, active_single_count);
            else execute_actions(result, active_single, active_single_count);
        }
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Java handleTapUp normal path: first tap up — set up double-tap waiting
    bool has_dt = active_has_double_tap;
    if (has_dt) {
        
        g_state.gesture_deferred_tap_count = 0;
        for (int i = 0; i < active_single_count && i < 8; i++)
            g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = active_single[i];
        g_state.gesture_pending_double_count = 0;
        for (int i = 0; i < active_double_count && i < 8; i++)
            g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = active_double[i];
        // Also save in deferred field (Java: pendingDeferredDoubleAction)
        // This survives the gesture_pending_double_count = 0 reset at the top of
        // handle_touchscreen_down / handle_touchpad_down on the next finger-down,
        // matching Java's handleDoubleTapConfirmed recovery:
        //   pendingDoubleTapAction = pendingDeferredDoubleAction
        g_state.gesture_pending_deferred_double_count = 0;
        for (int i = 0; i < active_double_count && i < 8; i++)
            g_state.gesture_pending_deferred_double[g_state.gesture_pending_deferred_double_count++] = active_double[i];
        g_state.gesture_double_tap_waiting = true;
        g_state.gesture_double_tap_start_time = time_ms;
        g_state.gesture_last_tap_up_x = f->tap_up_x;
        g_state.gesture_last_tap_up_y = f->tap_up_y;
        f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
    } else {
        
        if (!g_state.gesture_is_action_held) {
            if (!active_has_single_tap_drag) hold_actions(result, active_single, active_single_count);
            else execute_actions(result, active_single, active_single_count);
        }
        if (active_has_double_tap_drag) {
            
            g_state.gesture_double_tap_waiting = true;
            g_state.gesture_double_tap_start_time = time_ms;
            f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
        } else {
            f->state = GESTURE_STATE_IDLE;
        }
    }
}
