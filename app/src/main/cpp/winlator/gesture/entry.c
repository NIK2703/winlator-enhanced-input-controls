#include "../touch_processor_internal.h"

// ---- Unified S/Sd pair resolution ----
static inline GesturePairPlan resolve_single_tap_pair(const TouchFinger* f) {
    return gesture_decide_branch(gesture_branch_params(
        f->cached_has_active_single_tap,
        f->cached_has_active_single_tap_drag,
        f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag,
        f->cached_has_long_press_timer,
        g_state.cfg.is_ts,
        f->is_second_finger,
        g_state.cfg.single_tap_delay_ms,
        true
    ));
}

// ---- Unified D/Dd pair resolution ----
static inline GesturePairPlan resolve_double_tap_pair(const TouchFinger* f) {
    return gesture_decide_branch(gesture_branch_params(
        f->cached_has_active_double_tap,
        f->cached_has_active_double_tap_drag,
        false, false,
        g_state.cfg.is_ts,
        f->is_second_finger,
        0, false
    ));
}

// ---- DT confirm internal ----
void double_tap_confirm_internal(TouchActionResult* restrict result, TouchFinger* f) {
    if (__builtin_expect(!g_state.gesture_double_tap_waiting, 0)) {
        __android_log_print(ANDROID_LOG_WARN, "Winlator_Gesture", "DT_CONFIRM: skipped - not waiting");
        return;
    }
    __android_log_print(ANDROID_LOG_WARN, "Winlator_Gesture", "DT_CONFIRM: pending_dbl=%d pending_deferred_dbl=%d",
        g_state.gesture_pending_double_count,
        g_state.gesture_pending_deferred_double_count);
    g_state.gesture_double_tap_waiting = false;
    g_state.gesture_deferred_tap_count = 0;

    if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
        g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
        for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
            g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
    }
    g_state.gesture_pending_deferred_double_count = 0;

    bool has_dt = g_state.gesture_pending_double_count > 0;
    bool has_dt_drag = f->bindings.double_tap_drag_count > 0;

    if (has_dt) {
        GesturePairPlan d_plan = gesture_decide_branch(gesture_branch_params(
            true, has_dt_drag, false, false,
            g_state.cfg.is_ts, false, 0, false
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
static void handle_ts_single_tap_hold(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms) {
    if (!g_state.cfg.is_ts) return;
    if (f->is_second_finger) return;
    if (g_state.gesture_is_action_held) return;
    if (!f->cached_has_active_single_tap) return;

    GesturePairPlan plan = resolve_single_tap_pair(f);
    execute_tap_on_finger_down(f, result, time_ms, plan, true);
}

static inline void cleanup_main_finger(TouchFinger* f) {
    f->state = GESTURE_STATE_IDLE;
    g_state.gesture_post_double_tap_drag = false;
    gesture_clear_deferred_tap();
    gesture_clear_pending_long_press();
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_double_tap_consumed = false;
    if (!g_state.gesture_second_active)
        g_state.gesture_deferred_second_finger_tap = false;
    deactivate_finger(f);
}

// ---- Second-finger SDTW confirm (extracted from touchpad_finger_down) ----
static inline bool confirm_second_double_tap(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms) {
    if (!g_state.second_double_tap_waiting) return false;
    if (!f->cached_has_active_double_tap && !f->cached_has_active_double_tap_drag)
        return false;  // stale SDTW — don't consume, let timeout handle it
    g_state.second_double_tap_waiting = false;
    g_state.second_tap_fallback_count = 0;
    GesturePairPlan d_plan = resolve_double_tap_pair(f);
    confirm_double_tap(result, d_plan,
        f->bindings.double_tap, f->bindings.double_tap_count,
        g_state.pending_second_double,
        &g_state.pending_second_double_count, 8,
        f->cached_has_active_double_tap_drag,
        &g_state.gesture_post_double_tap_drag);
    f->cached_has_long_press_timer = false;
    f->down_time_ms = time_ms;
    g_state.gesture_handler_active = finger_has_gesture(f);
    f->state = GESTURE_STATE_TAP_WAITING;
    f->single_tap_hold_delay_ms = 0;
    f->single_tap_hold_timer = 0;
    return true;  // DT confirmed, caller should return to skip normal processing
}

// ---- Second-finger global DT_WAITING confirm (extracted from touchpad_finger_down) ----
static inline bool confirm_second_finger_global_dt(TouchFinger* f, TouchActionResult* restrict result) {
    if (!g_state.gesture_double_tap_waiting) return false;
    if (gesture_is_within_tap_distance(f->x, f->y)) {
        TouchFinger* _main_for_dt = find_finger(g_state.gesture_main_ptr_id);
        double_tap_confirm_internal(result, _main_for_dt ? _main_for_dt : f);
        for (int _mi = 0; _mi < MAX_FINGERS; _mi++) {
            TouchFinger* _mf = &g_state.fingers[_mi];
            if (_mf->active && _mf->ptr_id == g_state.gesture_main_ptr_id) {
                _mf->state = GESTURE_STATE_TAP_WAITING;
                reset_finger_tap_state(_mf);
                _mf->down_x = _mf->x;
                _mf->down_y = _mf->y;
                break;
            }
        }
        gesture_clear_second_finger_globals();
        f->state = GESTURE_STATE_IDLE;
        g_state.gesture_handler_active = true;
        return true;
    } else {
        gesture_cancel_double_tap_wait(result);
        return false;
    }
}

// ---- touchpad_finger_down ----
void touchpad_finger_down(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms) {
    g_state.gesture_is_down_event = true;
    if (__builtin_expect(!current_mode_has_gestures(), 0) && !g_state.gesture_double_tap_waiting) {
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

    if (__builtin_expect(g_state.gesture_main_ptr_id < 0, 1)) {
        // === MAIN FINGER ===
        f->is_second_finger = false;
        g_state.gesture_main_ptr_id = f->ptr_id;
        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;

        if (__builtin_expect(g_state.gesture_double_tap_waiting, 0)) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                double_tap_confirm_internal(result, f);
                reset_finger_tap_state(f);
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

        if (__builtin_expect(g_state.cfg.is_ts, 0))
            handle_ts_single_tap_hold(f, result, time_ms);

    } else if (f->ptr_id != g_state.gesture_main_ptr_id) {
        // === SECOND FINGER ===
        // Guard against 3rd+ finger overwriting second-finger state.
        // Only the first non-main finger is treated as the gesture second finger.
        if (__builtin_expect(g_state.gesture_second_active, 0)) {
            // Disable gesture processing for 3rd+ fingers: keep IDLE so no
            // gesture actions fire, but the finger remains active for element
            // processing (buttons, sticks, etc.).
            f->state = GESTURE_STATE_IDLE;
            return;
        }
        f->is_second_finger = true;
        // Early exit: no second-finger bindings in this mode and no pending DT
        if (!current_mode_has_gesture(GESTURE_SINGLE_2ND) && !current_mode_has_gesture(GESTURE_DOUBLE_2ND)
            && !current_mode_has_gesture(GESTURE_SINGLE_DRAG_2ND) && !current_mode_has_gesture(GESTURE_DOUBLE_DRAG_2ND)
            && !g_state.second_double_tap_waiting && !g_state.gesture_double_tap_waiting) {
            f->state = GESTURE_STATE_IDLE;
            return;
        }
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
        if (confirm_second_double_tap(f, result, time_ms)) return;

        // Global DT_WAITING check
        if (confirm_second_finger_global_dt(f, result)) return;

        // Don't release held actions during stale SDTW (confirm_second_double_tap
        // returned false without consuming SDTW) — let timeout handle it.
        if (!g_state.second_double_tap_waiting)
            release_held_actions(result);

        g_state.second_tap_fallback_count = 0;
        // Save S2 as SDTW fallback unless only Dd2 is present (no D2).
        // When only Dd2 is bound, a single two-finger tap should produce no action.
        if (!f->cached_has_active_double_tap_drag || f->cached_has_active_double_tap) {
            for (int _i = 0; _i < f->bindings.single_tap_count && _i < 8; _i++) {
                g_state.second_tap_fallback[_i] = f->bindings.single_tap[_i];
                g_state.second_tap_fallback_count++;
            }
        }

        f->down_time_ms = time_ms;
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        g_state.gesture_handler_active = finger_has_gesture(f);

        // ST handling for second finger using unified branch plan
        if (f->cached_has_active_single_tap) {
            GesturePairPlan plan = resolve_single_tap_pair(f);

            // When first-finger drag was saved to pending_resume_action,
            // force_hold so S2 press persists until second-finger up
            // (instead of press+release tap).
            bool _drag_paused = _mf && _mf->pending_resume_action_count > 0;
            execute_tap_on_finger_down(f, result, time_ms, plan, _drag_paused);
        }
    }
}

// ---- touchpad_finger_up ----
void touchpad_finger_up(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms) {
    g_state.gesture_is_down_event = false;
    if (__builtin_expect(f->is_second_finger, 0)) {
        if (__builtin_expect(!g_state.gesture_second_active, 0)) {
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
        g_state.active_finger_count--;
        g_state.finger_by_ptr_id[f->ptr_id] = NULL;
        f->active = false;
        return;
    }

    // === MAIN FINGER UP ===
    f->tap_up_x = f->x;
    f->tap_up_y = f->y;

    switch (f->state) {
        case GESTURE_STATE_TAP_WAITING: {
            // TP mode with no gesture bindings: release and cleanup immediately
            if (__builtin_expect(g_state.cfg.is_tp && !current_mode_has_gestures(), 0)) {
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                cleanup_main_finger(f);
                return;
            }
            if (g_state.cfg.is_tp
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
                return;
            }
            handle_tap_up(f, result, time_ms);
            if (__builtin_expect(f->single_tap_deferred, 0)) {
                return; // gesture_tick will complete the deferred tap
            }
            tap_up_cleanup(f, result);
            return;
        }

        case GESTURE_STATE_LONG_PRESSING: {
            if (current_mode_has_gesture(GESTURE_DOUBLE_TAP) || current_mode_has_gesture(GESTURE_DOUBLE_TAP_DRAG))
                execute_deferred_double(result);
            if (g_state.gesture_pending_deferred_long_press_count > 0) {
                execute_actions(result, g_state.gesture_pending_deferred_long_press,
                                g_state.gesture_pending_deferred_long_press_count);
                gesture_clear_pending_long_press();
            } else if (!g_state.gesture_is_action_held) {
                if (f->cached_has_active_long_press)
                    execute_actions(result, f->bindings.long_press, f->bindings.long_press_count);
            }
            release_held_actions(result);
            goto L_CLEANUP;
        }

        case GESTURE_STATE_DRAGGING: {
            if (g_state.gesture_pending_deferred_double_count > 0
                && (g_state.gesture_is_action_held
                    ? !bindings_equal(
                        g_state.gesture_pending_deferred_double,
                        g_state.gesture_pending_deferred_double_count,
                        g_state.gesture_held_actions,
                        g_state.gesture_held_count)
                    : true))
                execute_deferred_double(result);
            gesture_clear_pending_long_press();
            release_held_actions(result);
            goto L_CLEANUP;
        }

        case GESTURE_STATE_DOUBLE_TAP_WAITING: {
            execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
            if (!f->cached_has_active_single_tap_drag) {
                release_held_actions(result);
            }
            g_state.gesture_double_tap_waiting = false;
            goto L_CLEANUP;
        }

        default: {
            if (g_state.gesture_post_double_tap_drag) {
                g_state.gesture_post_double_tap_drag = false;
                release_held_actions(result);
            }
            goto L_CLEANUP;
        }
    }

L_CLEANUP:
    cleanup_main_finger(f);
}
