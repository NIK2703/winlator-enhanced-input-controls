#include "../touch_processor_internal.h"

// ---- DT confirm internal ----
void double_tap_confirm_internal(TouchActionResult* result) {
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
    bool has_dt_drag = (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN)
        ? (g_state.cfg.ts_double_tap_drag_count > 0)
        : (g_state.cfg.tp_double_tap_drag_count > 0);

    if (has_dt) {
        GesturePairPlan d_plan = gesture_decide_branch(gesture_branch_params(
            has_dt, has_dt_drag,
            false, false,
            g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN,
            false,
            0,
            false
        ));

        if (d_plan.hold_delay_ms > 0 || d_plan.pulse_on_up || !has_dt_drag) {
            g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
            for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
        } else {
            execute_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
        }
        g_state.gesture_pending_double_count = 0;
    }

    g_state.gesture_post_double_tap_drag = has_dt || has_dt_drag;
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

    if (plan.hold_delay_ms > 0) {
        f->single_tap_hold_delay_ms = plan.hold_delay_ms;
        f->single_tap_hold_timer = time_ms;
    } else if (plan.pulse_on_up || plan.press_on_drag) {
        // deferred to check_start_drag / handle_tap_up
    } else {
        execute_actions_hold(result, f->bindings.single_tap, f->bindings.single_tap_count);
    }
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
                double_tap_confirm_internal(result);
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

                if (d_plan.pulse_on_up) {
                    // Defer D to fire on second-finger up; Dd drives via check_start_drag
                    g_state.pending_second_double_count = 0;
                    for (int _i = 0; _i < f->bindings.double_tap_count && _i < 8; _i++)
                        g_state.pending_second_double[_i] = f->bindings.double_tap[_i];
                    g_state.pending_second_double_count = f->bindings.double_tap_count;
                    g_state.gesture_post_double_tap_drag = true;
                } else {
                    execute_actions(result, f->bindings.double_tap, f->bindings.double_tap_count);
                }
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
                double_tap_confirm_internal(result);
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
                g_state.gesture_second_active = false;
                g_state.gesture_second_ptr_id = -1;
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

            if (plan.hold_delay_ms > 0) {
                f->single_tap_hold_delay_ms = plan.hold_delay_ms;
                f->single_tap_hold_timer = time_ms;
            } else if (plan.pulse_on_up || plan.press_on_drag) {
                // deferred to check_start_drag / handle_tap_up
            } else {
                execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
            }
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
            // D/Dd present — always enter SDTW (analogous to first-finger
            // DT_WAITING on each tap-up). ST fallback fires on SDTW timeout;
            // DT fires via SDTW_handler on second tap within timeout.
            g_state.second_double_tap_waiting = true;
            g_state.second_tap_fallback_time = time_ms;
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

        g_state.gesture_second_active = false;
        g_state.gesture_second_ptr_id = -1;
        g_state.gesture_deferred_second_finger_tap = false;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_second_main_ref_x = 0;
        g_state.gesture_second_main_ref_y = 0;

        if (!g_state.second_double_tap_waiting && f->state != GESTURE_STATE_DRAGGING)
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
                    if (!g_state.gesture_is_action_held) {
                        if (!f->cached_has_active_single_tap_drag)
                            execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
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
