#include "../touch_processor_internal.h"

// ---- DT confirm internal (moved from entry.c) ----
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
        copy_bindings_bounded(g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count,
            g_state.gesture_pending_double, &g_state.gesture_pending_double_count, FALLBACK_MAX);
    }
    g_state.gesture_pending_deferred_double_count = 0;

    bool has_dt = g_state.gesture_pending_double_count > 0;
    bool has_dt_drag = f->bindings.double_tap_drag_count > 0;

    if (has_dt) {
        GesturePairPlan d_plan = gesture_decide_branch(gesture_branch_params(
            true, has_dt_drag, false, false,
            g_state.cfg.is_ts, false, 0
        ));

        confirm_double_tap(result, d_plan,
            g_state.gesture_pending_double, g_state.gesture_pending_double_count,
            g_state.gesture_pending_deferred_double,
            &g_state.gesture_pending_deferred_double_count, FALLBACK_MAX,
            &g_state.gesture_post_double_tap_drag);

        g_state.gesture_pending_double_count = 0;
    } else {
        g_state.gesture_post_double_tap_drag = has_dt_drag;
    }

    // Sync per-finger ctx for unified DOWN/UP/TICK paths
    GestureFingerCtx* dt_ctx = &g_ctx[(int)(f - g_state.fingers)];
    dt_ctx->dt_waiting = false;
    dt_ctx->pending_double_count = 0;
    dt_ctx->deferred_double_count = 0;
    if (g_state.gesture_pending_deferred_double_count > 0) {
        copy_bindings_bounded(g_state.gesture_pending_deferred_double,
            g_state.gesture_pending_deferred_double_count,
            dt_ctx->deferred_double, &dt_ctx->deferred_double_count, FALLBACK_MAX);
    }
    dt_ctx->post_double_tap_drag = g_state.gesture_post_double_tap_drag;
}

// ---- helpers ----
bool resolve_drag_binding(
    const TouchBinding* xd, int xd_count,
    const TouchBinding* x,  int x_count,
    const TouchBinding* fb, int fb_count,
    bool press_on_drag,
    bool use_fallback,
    const TouchBinding** out_binding,
    int* out_count)
{
    if (xd_count > 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "Winlator_Gesture",
            "RESOLVE_DRAG: tier1_xd xd_cnt=%d type0=%d", xd_count, xd->type);
        *out_binding = xd; *out_count = xd_count; return true;
    }
    if (press_on_drag && x_count > 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "Winlator_Gesture",
            "RESOLVE_DRAG: tier2_press_on_drag x_cnt=%d type0=%d", x_count, x->type);
        *out_binding = x; *out_count = x_count; return true;
    }
    if (use_fallback && fb_count > 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "Winlator_Gesture",
            "RESOLVE_DRAG: tier3_fallback fb_cnt=%d type0=%d", fb_count, fb->type);
        *out_binding = fb; *out_count = fb_count; return true;
    }
    __android_log_print(ANDROID_LOG_DEBUG, "Winlator_Gesture",
        "RESOLVE_DRAG: NONE xd_cnt=%d press=%d x_cnt=%d use_fb=%d fb_cnt=%d",
        xd_count, press_on_drag, x_count, use_fallback, fb_count);
    return false;
}

void on_drag_start(TouchFinger* f, TouchActionResult* restrict result) {
    f->single_tap_hold_delay_ms = 0;
    // Execute pending second-finger D2 before clearing, so D2 isn't silently
    // lost when the second finger transitions to DRAGGING (two-finger DTD).
    if (result && g_state.pending_second_double_count > 0) {
        execute_actions(result, g_state.pending_second_double, g_state.pending_second_double_count);
    }
    gesture_clear_second_finger_state();
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_deferred_second_finger_tap = false;
}

bool gesture_is_within_tap_distance(float x, float y) {
    return fabsf(x - g_state.gesture_last_tap_up_x) <= g_state.cfg.double_tap_distance_px
        && fabsf(y - g_state.gesture_last_tap_up_y) <= g_state.cfg.double_tap_distance_px;
}

void gesture_cancel_double_tap_wait(TouchActionResult* restrict result) {
    if (!__builtin_expect(g_state.gesture_double_tap_waiting, 0)) return;
    g_state.gesture_double_tap_waiting = false;
    if (g_state.gesture_deferred_tap_count > 0) {
        execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
        g_state.gesture_deferred_tap_count = 0;
    }
    gesture_clear_deferred_tap();
    TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
    if (mf) {
        mf->state = GESTURE_STATE_IDLE;
        g_ctx[(int)(mf - g_state.fingers)].dt_waiting = false;
    }
}

// ---- gesture_tick ----
void gesture_tick(uint64_t time_ms, TouchActionResult* restrict result) {
    // caps gate: must run before the gesture-bindings early return so that
    // stale DT/SDTW flags are cleaned up even when switching to a no-gesture mode.
    if (!g_state.cfg.caps_has_double_tap) {
        gesture_clear_second_finger_state();
        g_state.gesture_double_tap_waiting = false;
        g_state.gesture_pending_deferred_double_count = 0;
    }

    if (!g_state.cfg.caps_has_gesture_bindings) return;

    TouchFinger* restrict fingers = g_state.fingers;
    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &fingers[i];
        if (__builtin_expect(!f->active, 1)) continue;
        if (__builtin_expect(f->engaged_elem_count > 0, 0)) continue;
        gesture_process_finger(f, &g_ctx[i], result, time_ms, GESTURE_EVENT_TICK, 0, 0);
    }

    // ---- SDTW timeout (top-level, survives finger deactivation) ----
    {
        uint32_t sdtw_mask = GESTURE_MASK(GESTURE_DOUBLE_2ND) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);
        if ((g_state.cfg.caps_mode_mask & sdtw_mask) && g_state.second_double_tap_waiting
            && time_ms - g_state.second_tap_fallback_time >= (uint64_t)g_state.cfg.double_tap_timeout_ms)
        {
            int fallback_count = g_state.second_tap_fallback_count;
            gesture_clear_second_finger_state();
            if (fallback_count > 0) {
                execute_actions(result, g_state.second_tap_fallback, fallback_count);
                g_state.second_tap_fallback_count = 0;
            }
            TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
            if (mf && mf->state != GESTURE_STATE_DRAGGING)
                mf->state = GESTURE_STATE_IDLE;
        }
    }
}

// ---- Unified tap execution helpers ----

// Execute S on finger-down: start hold timer, defer to drag/up, or execute now.
// Pass resolved bindings (x = non-drag, x_cnt) instead of hardcoded single_tap.
void execute_tap_on_finger_down(TouchFinger* f, TouchActionResult* restrict result,
    uint64_t time_ms, GesturePairPlan plan, bool force_hold,
    const TouchBinding* x, int x_cnt)
{
    if (plan.hold_delay_ms > 0) {
        f->single_tap_hold_delay_ms = plan.hold_delay_ms;
        f->single_tap_hold_timer = time_ms;
    } else if (plan.pulse_on_up || plan.press_on_drag) {
        // deferred to check_start_drag / handle_tap_up
    } else {
        if (force_hold)
            execute_actions_hold(result, x, x_cnt);
        else
            execute_actions(result, x, x_cnt);
    }
}

// Enter DT_WAITING state, optionally saving deferred bindings.
void enter_double_tap_waiting(TouchFinger* f, uint64_t time_ms,
    const TouchBinding* deferred_single, int deferred_single_count,
    const TouchBinding* deferred_double, int deferred_double_count)
{
    gesture_clear_deferred_tap();
    if (deferred_single && deferred_single_count > 0) {
        copy_bindings_bounded(deferred_single, deferred_single_count,
            g_state.gesture_deferred_tap, &g_state.gesture_deferred_tap_count, FALLBACK_MAX);
    }
    if (deferred_double && deferred_double_count > 0) {
        for (int _i = 0; _i < deferred_double_count && _i < 8; _i++) {
            g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = deferred_double[_i];
            g_state.gesture_pending_deferred_double[g_state.gesture_pending_deferred_double_count++] = deferred_double[_i];
        }
    }
    g_state.gesture_double_tap_waiting = true;
    g_state.gesture_double_tap_start_time = time_ms;
    g_state.gesture_last_tap_up_x = f->tap_up_x;
    g_state.gesture_last_tap_up_y = f->tap_up_y;
    // Sync per-finger ctx for unified tick/down/up paths
    GestureFingerCtx* dt_ctx = &g_ctx[(int)(f - g_state.fingers)];
    dt_ctx->dt_waiting = true;
    dt_ctx->dt_wait_start_time = time_ms;
    if (deferred_double && deferred_double_count > 0) {
        copy_bindings_bounded(deferred_double, deferred_double_count,
            dt_ctx->pending_double, &dt_ctx->pending_double_count, FALLBACK_MAX);
        copy_bindings_bounded(deferred_double, deferred_double_count,
            dt_ctx->deferred_double, &dt_ctx->deferred_double_count, FALLBACK_MAX);
    }
    f->cached_has_long_press_timer = false;
    f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
}

// Confirm double-tap: execute D now or defer to finger-up.
// Returns true if D was executed now, false if deferred.
bool confirm_double_tap(TouchActionResult* restrict result,
    GesturePairPlan d_plan,
    const TouchBinding* src, int src_count,
    TouchBinding* dst, int* dst_count, int dst_max,
    bool* out_post_dtd)
{
    if (d_plan.pulse_on_up) {
        copy_bindings_bounded(src, src_count, dst, dst_count, dst_max);
        *out_post_dtd = true;
        return false; // deferred
    } else {
        execute_actions_hold(result, src, src_count);
        // Still store D bindings in deferred so PATH 1 fires on finger-up
        // (prevents re-entering DT waiting and allows check_start_drag to skip Sd).
        copy_bindings_bounded(src, src_count, dst, dst_count, dst_max);
        *out_post_dtd = d_plan.drag_available;
        return true; // executed now
    }
}


