#include "../touch_processor_internal.h"

// ---- DT confirm internal ----
void double_tap_confirm_internal(TouchActionResult* result, const FingerBindings* fb) {
    g_state.gesture_double_tap_waiting = false;
    g_state.gesture_deferred_tap_count = 0;

    if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
        g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
        for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
            g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
        g_state.gesture_pending_deferred_double_count = 0;
    }
    g_state.gesture_pending_deferred_double_count = 0;

    bool has_dt = g_state.gesture_pending_double_count > 0;
    bool has_dt_drag = fb->double_tap_drag_count > 0;

    if (has_dt) {
        GesturePairPlan d_plan = gesture_decide_branch(gesture_branch_params(
            has_dt, has_dt_drag,
            false, false,
            g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN,
            false, 0, false
        ));

        confirm_double_tap(result, d_plan,
            g_state.gesture_pending_double, g_state.gesture_pending_double_count,
            g_state.gesture_pending_deferred_double,
            &g_state.gesture_pending_deferred_double_count, 8,
            has_dt_drag,
            &g_state.gesture_post_double_tap_drag);

        g_state.gesture_pending_double_count = 0;
    } else {
        g_state.gesture_post_double_tap_drag = has_dt_drag;
    }
}

// ---- TS finger-down pre-hold ----
static void handle_ts_single_tap_hold(TouchFinger* f, TouchActionResult* result, uint64_t time_ms) {
    if (g_state.cfg.touch_mode != TOUCH_MODE_TOUCHSCREEN) return;
    if (f->is_second_finger) return;
    if (g_state.gesture_is_action_held) return;
    if (!f->cached_has_active_single_tap) return;

    GesturePairPlan plan = gesture_decide_branch(gesture_branch_params(
        f->cached_has_active_single_tap,
        f->cached_has_active_single_tap_drag,
        f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag,
        f->cached_has_long_press_timer,
        true,
        false,
        g_state.cfg.single_tap_delay_ms,
        true
    ));

    execute_tap_on_finger_down(f, result, time_ms, plan, true);
}

static inline void cleanup_main_finger(TouchFinger* f) {
    f->state = GESTURE_STATE_IDLE;
    // Do NOT clear gesture_second_active/second_ptr_id here — the second finger
    // may still be on the pad and needs those to process its finger-up properly.
    g_state.gesture_post_double_tap_drag = false;
    gesture_clear_deferred_tap();
    gesture_clear_pending_long_press();
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_double_tap_consumed = false;
    // Only clear deferred_second_finger_tap if no second finger is holding the context.
    // When main finger lifts during drag-pause (2nd touch held), the flag
    // must survive so a new main finger can restore second-finger bindings.
    if (!g_state.gesture_second_active)
        g_state.gesture_deferred_second_finger_tap = false;
}

// ---- touchpad_finger_down ----
void touchpad_finger_down(TouchFinger* f, TouchActionResult* result, uint64_t time_ms) {
    if (!g_state.cfg.caps_has_gesture_bindings && !g_state.gesture_double_tap_waiting) {
        if (g_state.gesture_main_ptr_id >= 0 && f->ptr_id != g_state.gesture_main_ptr_id) {
            g_state.gesture_second_active = true;
            g_state.gesture_second_ptr_id = f->ptr_id;
            f->is_second_finger = true;
            // Keep gesture_handler_active as-is (passthrough main set it true).
            // This prevents the TP fallback tap and two-finger tap left PRESS.
        } else {
            if (g_state.gesture_main_ptr_id < 0)
                g_state.gesture_main_ptr_id = f->ptr_id;
            g_state.gesture_handler_active = false;
        }
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    if (g_state.gesture_main_ptr_id < 0) {
        // === MAIN FINGER ===
        f->is_second_finger = false;
        g_state.gesture_main_ptr_id = f->ptr_id;
        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;

        if (g_state.gesture_double_tap_waiting) {
            float dx = fabsf(f->x - g_state.gesture_last_tap_up_x);
            float dy = fabsf(f->y - g_state.gesture_last_tap_up_y);
            if (dx <= g_state.cfg.double_tap_distance_px && dy <= g_state.cfg.double_tap_distance_px) {
                double_tap_confirm_internal(result, &f->bindings);
                f->cached_has_long_press_timer = false;
                f->single_tap_hold_delay_ms = 0;
                f->single_tap_hold_timer = 0;
                f->cached_has_moved_beyond_threshold = false;
                f->state = GESTURE_STATE_TAP_WAITING;
                return;
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        g_state.gesture_handler_active = finger_has_gesture(f);
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        handle_ts_single_tap_hold(f, result, time_ms);

    } else if (f->ptr_id != g_state.gesture_main_ptr_id) {
        // === SECOND FINGER ===
        // Guard against 3rd+ finger overwriting second-finger state.
        // Only the first non-main finger is treated as the gesture second finger.
        if (g_state.gesture_second_active) {
            // Disable gesture processing for 3rd+ fingers: keep IDLE so no
            // gesture actions fire, but the finger remains active for element
            // processing (buttons, sticks, etc.).
            f->state = GESTURE_STATE_IDLE;
            return;
        }
        f->is_second_finger = true;
        g_state.gesture_second_active = true;
        g_state.gesture_second_ptr_id = f->ptr_id;
        g_state.gesture_handler_active = false;

        TouchFinger* _mf = find_finger(g_state.gesture_main_ptr_id);
        if (_mf) {
            g_state.gesture_second_main_ref_x = _mf->x;
            g_state.gesture_second_main_ref_y = _mf->y;
            _mf->cached_has_long_press_timer = false;
        }

        setup_second_finger_bindings(f);

        // SDTW check (second-finger D/Dd — uses its own bindings, just like
        // first-finger D/Dd uses gesture_decide_branch for the non-drag/drag pair)
        if (g_state.second_double_tap_waiting) {
            g_state.second_double_tap_waiting = false;
            g_state.second_tap_fallback_count = 0;
            if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag) {
                GesturePairPlan d_plan = gesture_decide_branch(gesture_branch_params(
                    f->cached_has_active_double_tap,
                    f->cached_has_active_double_tap_drag,
                    false, false,
                    g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN,
                    f->is_second_finger,
                    0,
                    false
                ));

                confirm_double_tap(result, d_plan,
                    f->bindings.double_tap, f->bindings.double_tap_count,
                    g_state.pending_second_double,
                    &g_state.pending_second_double_count, 8,
                    f->cached_has_active_double_tap_drag,
                    &g_state.gesture_post_double_tap_drag);
                // SDTW confirmed DT/Dd — clear the ST fallback so it does not
                // fire alongside DT on lift.
                g_state.second_tap_fallback_count = 0;
                f->cached_has_long_press_timer = false;
                f->down_time_ms = time_ms;
                g_state.gesture_handler_active = finger_has_gesture(f);
                f->state = GESTURE_STATE_TAP_WAITING;
                f->single_tap_hold_delay_ms = 0;
                f->single_tap_hold_timer = 0;
                return;  // DT/Dd: done, skip normal second-finger processing
            }
            // ST-only with stale SDTW: fall through to normal ST hold/fallback
        }

        // Global DT_WAITING check
        if (g_state.gesture_double_tap_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                TouchFinger* _main_for_dt = find_finger(g_state.gesture_main_ptr_id);
                double_tap_confirm_internal(result, _main_for_dt ? &_main_for_dt->bindings : &f->bindings);
                for (int _mi = 0; _mi < MAX_FINGERS; _mi++) {
                    TouchFinger* _mf = &g_state.fingers[_mi];
                    if (_mf->active && _mf->ptr_id == g_state.gesture_main_ptr_id) {
                        _mf->state = GESTURE_STATE_TAP_WAITING;
                        _mf->cached_has_long_press_timer = false;
                        _mf->single_tap_hold_delay_ms = 0;
                        _mf->single_tap_hold_timer = 0;
                        _mf->cached_has_moved_beyond_threshold = false;
                        _mf->down_x = _mf->x;
                        _mf->down_y = _mf->y;
                        break;
                    }
                }
                gesture_clear_second_finger_globals();
                f->state = GESTURE_STATE_IDLE;
                g_state.gesture_handler_active = true;
                return;
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        release_held_actions(result);

        g_state.second_tap_fallback_count = 0;
        for (int _i = 0; _i < f->bindings.single_tap_count && _i < 8; _i++) {
            g_state.second_tap_fallback[_i] = f->bindings.single_tap[_i];
            g_state.second_tap_fallback_count++;
        }

        f->down_time_ms = time_ms;
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        g_state.gesture_handler_active = finger_has_gesture(f);

        // ST handling for second finger using unified branch plan
        // (matches one-finger logic via gesture_decide_branch)
        if (f->cached_has_active_single_tap) {
            GesturePairPlan plan = gesture_decide_branch(gesture_branch_params(
                f->cached_has_active_single_tap,
                f->cached_has_active_single_tap_drag,
                f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag,
                false,
                g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN,
                true,
                g_state.cfg.single_tap_delay_ms,
                true
            ));

            // When first-finger drag was saved to pending_resume_action,
            // force_hold so S2 press persists until second-finger up
            // (instead of press+release tap).
            TouchFinger* _exec_main = find_finger(g_state.gesture_main_ptr_id);
            bool _drag_paused = _exec_main && _exec_main->pending_resume_action_count > 0;
            execute_tap_on_finger_down(f, result, time_ms, plan, _drag_paused);
        }
    }
}

// ---- touchpad_finger_up ----
void touchpad_finger_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms) {
    if (f->is_second_finger) {
        if (!g_state.gesture_second_active) {
            return;
        }

        if (g_state.gesture_is_action_held) {
            gesture_clear_second_finger_state();
        } else if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag) {
            enter_sdtw(time_ms);
        } else if (g_state.second_tap_fallback_count > 0) {
            execute_actions(result, g_state.second_tap_fallback, g_state.second_tap_fallback_count);
            g_state.second_tap_fallback_count = 0;
        }

        if (g_state.pending_second_double_count > 0) {
            execute_actions(result, g_state.pending_second_double, g_state.pending_second_double_count);
            gesture_clear_second_finger_state();
        }

        if (!(g_state.gesture_post_double_tap_drag && g_state.gesture_is_action_held)) {
            release_held_actions(result);
            add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
            add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
        }

        gesture_clear_second_finger_globals();

        f->state = GESTURE_STATE_IDLE;
        f->active = false;
        return;
    }

    // === MAIN FINGER UP ===
    f->tap_up_x = f->x;
    f->tap_up_y = f->y;

    switch (f->state) {
        case GESTURE_STATE_TAP_WAITING: {
            if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD
                && (f->cached_has_moved_beyond_threshold
                    || (time_ms - f->down_time_ms) >= TAP_MAX_TIME_MS)) {
                if (g_state.gesture_double_tap_consumed) {
                    g_state.gesture_double_tap_consumed = false;
                    if (!g_state.gesture_is_action_held)
                        execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                }
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                cleanup_main_finger(f);
                f->active = false;
                break;
            }
            handle_tap_up(f, result, time_ms);
            if (f->single_tap_deferred) {
                break; // gesture_tick will complete the deferred tap
            }
            if (!g_state.gesture_second_active || !g_state.gesture_is_action_held
                || g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD)
                release_held_actions(result);
            g_state.gesture_main_ptr_id = -1;
            f->active = false;
            break;
        }

        case GESTURE_STATE_LONG_PRESSING: {
            if (g_state.gesture_pending_deferred_double_count > 0) {
                execute_actions(result, g_state.gesture_pending_deferred_double,
                                g_state.gesture_pending_deferred_double_count);
                g_state.gesture_pending_deferred_double_count = 0;
            }
            if (g_state.gesture_pending_deferred_long_press_count > 0) {
                execute_actions(result, g_state.gesture_pending_deferred_long_press,
                                g_state.gesture_pending_deferred_long_press_count);
                gesture_clear_pending_long_press();
            } else if (!g_state.gesture_is_action_held) {
                if (f->cached_has_active_long_press)
                    execute_actions(result, f->bindings.long_press, f->bindings.long_press_count);
            }
            release_held_actions(result);
            cleanup_main_finger(f);
            f->active = false;
            break;
        }

        case GESTURE_STATE_DRAGGING: {
            if (g_state.gesture_pending_deferred_double_count > 0) {
                execute_actions(result, g_state.gesture_pending_deferred_double,
                                g_state.gesture_pending_deferred_double_count);
                g_state.gesture_pending_deferred_double_count = 0;
            }
            gesture_clear_pending_long_press();
            release_held_actions(result);
            cleanup_main_finger(f);
            f->active = false;
            break;
        }

        case GESTURE_STATE_DOUBLE_TAP_WAITING: {
            if (!f->cached_has_active_single_tap_drag) {
                execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                release_held_actions(result);
            } else {
                execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
            }
            g_state.gesture_double_tap_waiting = false;
            cleanup_main_finger(f);
            f->active = false;
            break;
        }

        default: {
            if (g_state.gesture_post_double_tap_drag) {
                g_state.gesture_post_double_tap_drag = false;
                release_held_actions(result);
            }
            cleanup_main_finger(f);
            f->active = false;
            break;
        }
    }
}
