#include "../touch_processor_internal.h"
#include "branch.h"

static inline int finger_index(const TouchFinger* f) {
    int idx = (int)(f - g_state.fingers);
    if (idx < 0 || idx >= MAX_FINGERS) return -1;
    return idx;
}

static bool validate_dt_confirm(void) {
    if (!g_state.gesture_double_tap_waiting) {
        return false;
    }
    g_state.gesture_double_tap_waiting = false;
    g_state.gesture_deferred_tap_count = 0;
    return true;
}

static void copy_deferred_double_to_all(const TouchBinding* src, int src_count, GestureFingerCtx* dt_context);

static void sync_dt_ctx(TouchFinger* f) {
    int idx = finger_index(f);
    if (idx < 0) return;
    GestureFingerCtx* dt_context = &g_ctx[idx];
    dt_context->dt_waiting = false;
    dt_context->pending_double.count = 0;
    dt_context->deferred_double.count = 0;
    dt_context->post_double_tap_drag = g_state.gesture_post_double_tap_drag;
}

void double_tap_confirm_internal(TouchActionResult* result, TouchFinger* f) {
    if (!validate_dt_confirm()) return;

    int idx = finger_index(f);
    if (idx < 0) return;
    copy_deferred_double_to_all(g_state.gesture_pending_deferred_double,
        g_state.gesture_pending_deferred_double_count, &g_ctx[idx]);

    bool has_dt = g_state.gesture_pending_double_count > 0;
    bool has_dt_drag = f->bindings.double_tap_drag_count > 0;

    if (has_dt) {
        GesturePairPlan double_plan = gesture_decide_branch(gesture_branch_params_simple(
            true, has_dt_drag, g_state.cfg.is_ts
        ));

        confirm_double_tap(result, double_plan,
            g_state.gesture_pending_double, g_state.gesture_pending_double_count,
            g_state.gesture_pending_deferred_double,
            &g_state.gesture_pending_deferred_double_count, FALLBACK_MAX,
            &g_state.gesture_post_double_tap_drag);

        g_state.gesture_pending_double_count = 0;
    } else {
        g_state.gesture_post_double_tap_drag = has_dt_drag;
    }

    sync_dt_ctx(f);
}

bool resolve_drag_binding(
    const DragResolveContext* ctx,
    const TouchBinding** out_binding,
    int* out_count)
{
    if (ctx->drag_count > 0) {
        *out_binding = ctx->drag;
        *out_count = ctx->drag_count;
        return true;
    }
    if (ctx->press_on_drag && ctx->non_drag_count > 0) {
        *out_binding = ctx->non_drag;
        *out_count = ctx->non_drag_count;
        return true;
    }
    if (ctx->is_d2_slot && ctx->fallback_count > 0) {
        *out_binding = ctx->fallback;
        *out_count = ctx->fallback_count;
        return true;
    }
    return false;
}

void on_drag_start(TouchFinger* f, TouchActionResult* result) {
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

void gesture_cancel_double_tap_wait(TouchActionResult* result) {
    if (!g_state.gesture_double_tap_waiting) return;
    g_state.gesture_double_tap_waiting = false;
    if (g_state.gesture_deferred_tap_count > 0) {
        execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
        g_state.gesture_deferred_tap_count = 0;
    }
    gesture_clear_deferred_tap();
    TouchFinger* main_finger = find_finger(g_state.gesture_main_ptr_id);
    if (main_finger) {
        main_finger->state = GESTURE_STATE_IDLE;
        int idx = finger_index(main_finger);
        if (idx >= 0) g_ctx[idx].dt_waiting = false;
    }
}

static void gesture_handle_sdtw_timeout(uint64_t time_ms, TouchActionResult* result) {
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
        TouchFinger* main_finger = find_finger(g_state.gesture_main_ptr_id);
        if (main_finger && main_finger->state != GESTURE_STATE_DRAGGING)
            main_finger->state = GESTURE_STATE_IDLE;
    }
}

void gesture_tick(uint64_t time_ms, TouchActionResult* result) {
    // caps gate: must run before the gesture-bindings early return so that
    // stale DT/SDTW flags are cleaned up even when switching to a no-gesture mode.
    if (!g_state.cfg.caps_has_double_tap) {
        gesture_clear_second_finger_state();
        g_state.gesture_double_tap_waiting = false;
        g_state.gesture_pending_deferred_double_count = 0;
    }

    if (!g_state.cfg.caps_has_gesture_bindings) return;

    TouchFinger* fingers = g_state.fingers;
    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &fingers[i];
        if (!f->active) continue;
        if (f->engaged_elem_count > 0) continue;
        gesture_branch(f, &g_ctx[i], result, time_ms, GESTURE_EVENT_TICK, 0 /*x*/, 0 /*y*/);
    }

    gesture_handle_sdtw_timeout(time_ms, result);
}


// Execute S on finger-down: start hold timer, defer to drag/up, or execute now.
// Pass resolved bindings (x = non-drag, x_cnt) instead of hardcoded single_tap.
void execute_tap_on_finger_down(TouchFinger* f, TouchActionResult* result,
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

static void copy_deferred_double_to_all(const TouchBinding* src, int src_count, GestureFingerCtx* dt_context) {
    copy_bindings_bounded(src, src_count,
        g_state.gesture_pending_double, &g_state.gesture_pending_double_count, FALLBACK_MAX);
    copy_bindings_bounded(src, src_count,
        g_state.gesture_pending_deferred_double, &g_state.gesture_pending_deferred_double_count, FALLBACK_MAX);
    copy_bindings_bounded(src, src_count,
        dt_context->pending_double.items, &dt_context->pending_double.count, FALLBACK_MAX);
    copy_bindings_bounded(src, src_count,
        dt_context->deferred_double.items, &dt_context->deferred_double.count, FALLBACK_MAX);
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
    int idx = finger_index(f);
    if (idx < 0) return;
    GestureFingerCtx* dt_context = &g_ctx[idx];
    if (deferred_double && deferred_double_count > 0) {
        copy_deferred_double_to_all(deferred_double, deferred_double_count, dt_context);
    }
    g_state.gesture_double_tap_waiting = true;
    g_state.gesture_double_tap_start_time = time_ms;
    g_state.gesture_last_tap_up_x = f->tap_up_x;
    g_state.gesture_last_tap_up_y = f->tap_up_y;
    dt_context->dt_waiting = true;
    dt_context->dt_wait_start_time = time_ms;
    f->cached_has_long_press_timer = false;
    f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
}

// Confirm double-tap: execute D now or defer to finger-up.
//
// Returns:
//   true  — was_executed_now: D actions were fired via execute_actions_hold.
//   false — deferred: D actions were NOT fired; caller must execute them on finger-up.
//
// out_post_dtd:
//   Set to true when post-double-tap-drag is enabled after this confirm.
//   When was_executed_now=true  → true means drag is available for the held D.
//   When was_executed_now=false → always true (deferred path always enables drag).
//
// dst/dst_count/dst_max:
//   D bindings are always copied here regardless of the branch, so that
//   check_start_drag can skip redundant Sd and PATH 1 fires on finger-up.
bool confirm_double_tap(TouchActionResult* result,
    GesturePairPlan d_plan,
    const TouchBinding* src, int src_count,
    TouchBinding* dst, int* dst_count, int dst_max,
    bool* out_post_dtd)
{
    copy_bindings_bounded(src, src_count, dst, dst_count, dst_max);

    if (d_plan.pulse_on_up) {
        *out_post_dtd = true;
        return false; // deferred
    } else {
        execute_actions_hold(result, src, src_count);
        *out_post_dtd = d_plan.drag_available;
        return true; // executed now
    }
}


