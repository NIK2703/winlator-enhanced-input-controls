#include "../touch_processor_internal.h"
#include <android/log.h>

void touchpad_finger_down(TouchFinger* f, TouchActionResult* result, uint64_t time_ms) {
    // Fast path: no gesture bindings — skip all gesture state machine
    if (!g_state.cfg.caps_has_gesture_bindings && !g_state.gesture_double_tap_waiting) {
        g_state.gesture_handler_active = false;
        if (g_state.gesture_main_ptr_id < 0) {
            g_state.gesture_main_ptr_id = f->ptr_id;
        }
        f->state = GESTURE_STATE_IDLE;
        return;
    }

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
            __android_log_print(ANDROID_LOG_INFO, "Gesture",
                "TP_down DT_waiting: finger(%.1f,%.1f) last_up(%.1f,%.1f) dx=%.1f dy=%.1f dbl_dist=%d %s",
                f->x, f->y,
                g_state.gesture_last_tap_up_x, g_state.gesture_last_tap_up_y,
                dx, dy, g_state.cfg.double_tap_distance_px,
                (dx <= g_state.cfg.double_tap_distance_px && dy <= g_state.cfg.double_tap_distance_px) ? "PASS" : "FAIL");
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
                        // Mirror Java handleDoubleTapConfirmed:
                        // executeActionsAndHold(pendingDoubleTapAction) — press + hold
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    } else {
                        // Mirror Java: save as pendingDeferredDoubleAction for fire-and-forget
                        // on finger-up via handleTapUp (both drag and non-drag cases)
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                f->state = GESTURE_STATE_TAP_WAITING;
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

        // Java TouchpadGestureHandler.onFingerDown does NOT hold single-tap on finger-down;
        // only TouchscreenGestureHandler does. Guard to match Java behavior.
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && !g_state.gesture_is_action_held) {
            if (f->cached_has_active_single_tap && !f->cached_has_active_single_tap_drag) {
                bool has_dt = f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag;
                if (has_dt) {
                    
                    g_state.gesture_deferred_tap_count = 0;
                    for (int i = 0; i < f->bindings.single_tap_count && i < 8; i++)
                        g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = f->bindings.single_tap[i];
                    g_state.gesture_pending_double_count = 0;
                    for (int i = 0; i < f->bindings.double_tap_count && i < 8; i++)
                        g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = f->bindings.double_tap[i];
                    f->single_tap_hold_delay_ms = g_state.cfg.double_tap_timeout_ms;
                    f->single_tap_hold_timer = time_ms;
                } else if (g_state.cfg.single_tap_delay_ms > 0) {
                    f->single_tap_hold_delay_ms = g_state.cfg.single_tap_delay_ms;
                    f->single_tap_hold_timer = time_ms;
                } else {
                    hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                }
            }
        }
    } else if (f->ptr_id != g_state.gesture_main_ptr_id) {
        f->is_second_finger = true;
        g_state.gesture_second_active = true;
        g_state.gesture_second_ptr_id = f->ptr_id;
        g_state.gesture_handler_active = false;

        // Mirror Java TouchpadGestureHandler.onFingerDown: anchor main finger's drag threshold
        // at the moment the second finger appears (fingerDownX = mainFingerX, fingerDownY = mainFingerY)
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD) {
            for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
                TouchFinger* _mf = &g_state.fingers[_fi];
                if (_mf->active && _mf->ptr_id == g_state.gesture_main_ptr_id) {
                    _mf->down_x = _mf->x;
                    _mf->down_y = _mf->y;
                    break;
                }
            }
        }

        // Java TouchpadGestureHandler.onFingerDown / TouchscreenGestureHandler.handlePointerDown:
        //   removeCallbacks(longPressRunnable) — permanently cancel main finger's long-press
        //   (Java does NOT re-post the timer for the main finger after second finger appears)
        for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
            TouchFinger* _mf = &g_state.fingers[_fi];
            if (_mf->active && _mf->ptr_id == g_state.gesture_main_ptr_id) {
                _mf->cached_has_long_press_timer = false;
                break;
            }
        }

        FingerBindings* fb2 = &f->bindings;
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            fb2->single_tap_count = g_state.cfg.ts_single_2nd_count;
            memcpy(fb2->single_tap, g_state.cfg.ts_single_2nd, sizeof(g_state.cfg.ts_single_2nd));
            fb2->long_press_count = 0;
            fb2->double_tap_count = g_state.cfg.ts_double_2nd_count;
            memcpy(fb2->double_tap, g_state.cfg.ts_double_2nd, sizeof(g_state.cfg.ts_double_2nd));
            fb2->single_tap_drag_count = g_state.cfg.ts_single_drag_2nd_count;
            memcpy(fb2->single_tap_drag, g_state.cfg.ts_single_drag_2nd, sizeof(g_state.cfg.ts_single_drag_2nd));
            fb2->long_press_drag_count = 0;
            fb2->double_tap_drag_count = g_state.cfg.ts_double_drag_2nd_count;
            memcpy(fb2->double_tap_drag, g_state.cfg.ts_double_drag_2nd, sizeof(g_state.cfg.ts_double_drag_2nd));
        } else {
            fb2->single_tap_count = g_state.cfg.tp_single_2nd_count;
            memcpy(fb2->single_tap, g_state.cfg.tp_single_2nd, sizeof(g_state.cfg.tp_single_2nd));
            fb2->long_press_count = 0;
            fb2->double_tap_count = g_state.cfg.tp_double_2nd_count;
            memcpy(fb2->double_tap, g_state.cfg.tp_double_2nd, sizeof(g_state.cfg.tp_double_2nd));
            fb2->single_tap_drag_count = g_state.cfg.tp_single_drag_2nd_count;
            memcpy(fb2->single_tap_drag, g_state.cfg.tp_single_drag_2nd, sizeof(g_state.cfg.tp_single_drag_2nd));
            fb2->long_press_drag_count = 0;
            fb2->double_tap_drag_count = g_state.cfg.tp_double_drag_2nd_count;
            memcpy(fb2->double_tap_drag, g_state.cfg.tp_double_drag_2nd, sizeof(g_state.cfg.tp_double_drag_2nd));
        }
        touch_finger_cache_bs(f);

        // Java TouchscreenGestureHandler.handlePointerDown second-finger path:
        //   if (hasLongPressTimer()) postDelayed(longPressRunnable, longPressTimeout);
        //   hasLongPressTimer() checks cur.hasLongPressTimer where cur = secondFingerSet.
        //   So the timer IS posted when second-finger bindings have long-press/long-press-drag.
        //   We do NOT cancel it here — touch_finger_cache_bs at call site already set the
        //   correct flags from second-finger bindings.

        // second-finger double-tap waiting check (before global DOUBLE_TAP_WAITING)
        if (g_state.second_double_tap_waiting) {
            g_state.second_double_tap_waiting = false;
            g_state.second_tap_fallback_count = 0;
            if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag) {
                if (f->cached_has_active_double_tap_drag) {
                    g_state.pending_second_double_count = 0;
                    for (int _i = 0; _i < f->bindings.double_tap_count && _i < 8; _i++)
                        g_state.pending_second_double[_i] = f->bindings.double_tap[_i];
                    g_state.pending_second_double_count = f->bindings.double_tap_count;
                    g_state.gesture_post_double_tap_drag = true;
                } else {
                    hold_actions(result, f->bindings.double_tap, f->bindings.double_tap_count);
                }
            }
            // Java: resetLongPressTimer() — clear and repost long-press timer
            f->down_time_ms = time_ms;
            if (f->bindings.single_tap_count > 0 || f->bindings.long_press_count > 0 || f->bindings.double_tap_count > 0
                || f->bindings.single_tap_drag_count > 0 || f->bindings.long_press_drag_count > 0 || f->bindings.double_tap_drag_count > 0)
                g_state.gesture_handler_active = true;
            // Java returns early from secondFingerDoubleTapWaiting path — set state and return
            f->state = GESTURE_STATE_TAP_WAITING;
            f->single_tap_hold_delay_ms = 0;
            f->single_tap_hold_timer = 0;
            return;
        }

        // Java TouchpadGestureHandler.onFingerDown: check global double-tap waiting for second finger
        // (checked AFTER secondFingerDoubleTapWaiting, matching Java order)
        if (g_state.gesture_double_tap_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                // Java: handleDoubleTapConfirmed - confirm double-tap
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;
                // Java: handleDoubleTapConfirmed() checks hasActiveDoubleTapDrag() via cur.
                // At this point cur = secondFingerSet (setSecondFingerActive(true) at
                // TouchpadGestureHandler line 136 runs BEFORE the double-tap check at line 138).
                // Use per-finger cached flags which reflect second-finger bindings.
                bool has_dt_drag = f->cached_has_active_double_tap_drag;
                if (g_state.gesture_pending_double_count > 0) {
                    if (!has_dt_drag) {
                        // Mirror Java handleDoubleTapConfirmed:
                        // executeActionsAndHold(pendingDoubleTapAction) — press + hold
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    } else {
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                // Java TouchpadGestureHandler.onFingerDown second-finger path:
                //   handleDoubleTapConfirmed() → setSecondFingerActive(false) inside
                //   then setSecondFingerActive(true) → override back to true
                //   then releaseHeldAction() → release the held double-tap action
                // So gesture_second_active stays true (set at line 94), not false.
                g_state.gesture_post_double_tap_drag = true;
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        // Java TouchpadGestureHandler.onFingerDown: releaseHeldAction() after double-tap check
        // (unconditional, runs for ALL second-finger down events, not just double-tap waiting)
        release_held_actions(result);

        g_state.second_tap_fallback_count = 0;
        for (int _i = 0; _i < f->bindings.single_tap_count && _i < 8; _i++) {
            g_state.second_tap_fallback[_i] = f->bindings.single_tap[_i];
            g_state.second_tap_fallback_count++;
        }
        // Java: resetLongPressTimer() for normal second-finger path
        f->down_time_ms = time_ms;
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && !g_state.gesture_is_action_held) {
            if (f->cached_has_active_single_tap && !f->cached_has_active_single_tap_drag) {
                bool has_dt = f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag;
                if (has_dt) {
                    // Second finger: use per-finger state, don't overwrite first-finger
                    // globals g_state.gesture_deferred_tap[] / g_state.gesture_pending_double[]
                    f->single_tap_hold_delay_ms = g_state.cfg.double_tap_timeout_ms;
                    f->single_tap_hold_timer = time_ms;
                } else if (g_state.cfg.single_tap_delay_ms > 0) {
                    f->single_tap_hold_delay_ms = g_state.cfg.single_tap_delay_ms;
                    f->single_tap_hold_timer = time_ms;
                } else {
                    hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                }
            }
        }
    }
}
static inline void cleanup_main_finger(TouchFinger* f) {
    f->state = GESTURE_STATE_IDLE;
    g_state.gesture_post_double_tap_drag = false;
    g_state.gesture_second_active = false;
    g_state.gesture_second_ptr_id = -1;
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_pending_deferred_double_count = 0;
    g_state.pending_second_double_count = 0;
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_double_tap_consumed = false;
}
void touchpad_finger_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms) {
    if (f->is_second_finger) {
        // Java TouchpadGestureHandler.onFingerUp: second-finger processing gated on
        // secondFingerActive. When false (e.g. main finger lifted first and cleared
        // the flag via cleanupMainPointer -> setSecondFingerActive(false)), skip.
        if (!g_state.gesture_second_active) {
            return;
        }
        if (g_state.gesture_is_action_held) {
            g_state.second_double_tap_waiting = false;
            g_state.second_tap_fallback_count = 0;
        } else if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag) {
            g_state.second_double_tap_waiting = true;
            g_state.second_tap_fallback_time = time_ms;
        } else if (g_state.second_tap_fallback_count > 0) {
            execute_actions(result, g_state.second_tap_fallback, g_state.second_tap_fallback_count);
            g_state.second_tap_fallback_count = 0;
        }

        if (g_state.pending_second_double_count > 0) {
            execute_actions(result, g_state.pending_second_double, g_state.pending_second_double_count);
            g_state.pending_second_double_count = 0;
        }

        release_held_actions(result);

        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);

        g_state.gesture_second_active = false;
        g_state.gesture_second_ptr_id = -1;
        g_state.gesture_post_double_tap_drag = false;

        // Java: postDoubleTapDrag = false; if (!secondFingerDoubleTapWaiting && state != DRAGGING) state = IDLE
        if (!g_state.second_double_tap_waiting && f->state != GESTURE_STATE_DRAGGING) {
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
                && !(f->travel_x < MAX_TAP_TRAVEL && f->travel_y < MAX_TAP_TRAVEL && (time_ms - f->down_time_ms) < TAP_MAX_TIME_MS)) {
                if (g_state.gesture_double_tap_consumed) {
                    g_state.gesture_double_tap_consumed = false;
                    if (!g_state.gesture_is_action_held) {
                        if (!f->cached_has_active_single_tap_drag)
                            hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                        else
                            execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                    }
                }
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                cleanup_main_finger(f);
                f->active = false;
                break;
            }
            handle_tap_up(f, result, time_ms);
            release_held_actions(result);
            g_state.gesture_main_ptr_id = -1;
            g_state.gesture_second_active = false;
            f->active = false;
            break;
        }
        case GESTURE_STATE_LONG_PRESSING:
            // Execute pending deferred double (from DT confirm) before cleanup
            if (g_state.gesture_pending_deferred_double_count > 0) {
                execute_actions(result, g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count);
                g_state.gesture_pending_deferred_double_count = 0;
            }
            // Java TouchpadGestureHandler: if isActionHeld, releaseHeldAction;
            // else if hasActiveLongPress, executeActions (fire-and-forget)
            // When can_hold_long_press is false, the LP timer already fire-and-forgot L
            // via execute_actions at gesture_tick:216. Re-firing here would duplicate
            // the action — skip it.
            if (!g_state.gesture_is_action_held) {
                if (f->cached_has_active_long_press && f->cached_can_hold_long_press)
                    execute_actions(result, f->bindings.long_press, f->bindings.long_press_count);
            }
            release_held_actions(result);
            cleanup_main_finger(f);
            f->active = false;
            break;
        case GESTURE_STATE_DRAGGING:
            if (g_state.gesture_pending_deferred_double_count > 0) {
                execute_actions(result, g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count);
                g_state.gesture_pending_deferred_double_count = 0;
            }
            release_held_actions(result);
            cleanup_main_finger(f);
            f->active = false;
            break;
        case GESTURE_STATE_DOUBLE_TAP_WAITING: {
            // Java TouchpadGestureHandler: no isActionHeld guard — always execute single-tap
            if (!f->cached_has_active_single_tap_drag) {
                hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                release_held_actions(result);
            } else {
                execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
            }
            g_state.gesture_double_tap_waiting = false;
            cleanup_main_finger(f);
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
            cleanup_main_finger(f);
            f->active = false;
            break;
    }
}
