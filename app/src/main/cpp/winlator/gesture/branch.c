#include "../touch_processor_internal.h"
#include "types.h"
#include "branch.h"

// =========================================================================
// SINGLE UNIFIED GESTURE BRANCHING FUNCTION
// Handles ALL 5 gesture pairs (S/Sd, D/Dd, L/Ld, S2/Sd2, D2/Dd2)
// for ALL 4 events (DOWN, MOVE, UP, TICK).
//
// Behavioral differences between finger roles are encoded in the
// GesturePairSlot flags — not via `is_second_finger` branches.
// =========================================================================

void gesture_branch(TouchFinger* f, GestureFingerCtx* ctx,
                    TouchActionResult* restrict result, uint64_t time_ms,
                    GestureProcessEvent event,
                    float dx, float dy)
{
    // =====================================================================
    // GESTURE_EVENT_DOWN
    // =====================================================================
    if (event == GESTURE_EVENT_DOWN) {
        g_state.gesture_is_down_event = true;

        // ---- Role assignment (main vs second finger) ----
        if (g_state.gesture_main_ptr_id < 0) {
            f->is_second_finger = false;
            g_state.gesture_main_ptr_id = f->ptr_id;
        } else if (f->ptr_id != g_state.gesture_main_ptr_id && !g_state.gesture_second_active) {
            g_state.gesture_second_active = true;
            g_state.gesture_second_ptr_id = f->ptr_id;
            f->is_second_finger = true;
            TouchFinger* _mf = find_finger(g_state.gesture_main_ptr_id);
            if (_mf) {
                g_state.gesture_second_main_ref_x = _mf->x;
                g_state.gesture_second_main_ref_y = _mf->y;
                _mf->cached_has_long_press_timer = false;
            }
        }

        // ---- Slot selection ----
        const GesturePairSlot* slot = select_gesture_slot(f);
        if (!slot) {
            f->state = GESTURE_STATE_IDLE;
            return;
        }

        // ---- Binding resolution ----
        const TouchBinding* x  = NULL; int x_cnt  = 0;
        const TouchBinding* xd = NULL; int xd_cnt = 0;
        resolve_binding_slot(f, slot, &x, &x_cnt, &xd, &xd_cnt);

        // --- Fallback save (data-driven via slot flag) ---
        if (slot->down_save_fallback && x_cnt > 0 && ctx->fallback_count == 0) {
            copy_bindings_bounded(x, x_cnt, ctx->fallback, &ctx->fallback_count, 8);
        }

        // --- DT_WAITING check (unified: ctx->pending_double always populated) ---
        if (ctx->dt_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                GesturePairPlan d_plan = resolve_gesture_pair(f, slot);
                confirm_double_tap(result, d_plan,
                    ctx->pending_double, ctx->pending_double_count,
                    ctx->deferred_double, &ctx->deferred_double_count, 8,
                    &ctx->post_double_tap_drag);
                ctx->dt_waiting = false;
                ctx->pending_double_count = 0;
                reset_finger_tap_state(f);
                f->state = GESTURE_STATE_TAP_WAITING;
                return;
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        // ---- Normal finger-down ----
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;

        GesturePairPlan plan = resolve_gesture_pair(f, slot);
        execute_tap_on_finger_down(f, result, time_ms, plan, true);
        return;
    }

    // =====================================================================
    // GESTURE_EVENT_MOVE
    // =====================================================================
    if (event == GESTURE_EVENT_MOVE) {
        if (f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING)
            return;

        // ---- Slot selection + binding ----
        const GesturePairSlot* slot = select_gesture_slot(f);
        if (!slot) return;

        const TouchBinding* x  = NULL; int x_cnt  = 0;
        const TouchBinding* xd = NULL; int xd_cnt = 0;
        resolve_binding_slot(f, slot, &x, &x_cnt, &xd, &xd_cnt);

        // ---- Delta aggregation (TP: second-finger adds main-finger movement) ----
        if (slot->move_agg_tp_delta && g_state.cfg.is_tp) {
            TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
            if (mf) {
                dx += mf->x - g_state.gesture_second_main_ref_x;
                dy += mf->y - g_state.gesture_second_main_ref_y;
            }
        }
        if (fabsf(dx) <= g_state.cfg.drag_threshold_px && fabsf(dy) <= g_state.cfg.drag_threshold_px)
            return;

        // ---- Long press drag ----
        if (f->state == GESTURE_STATE_LONG_PRESSING) {
            if (!current_mode_has_gesture(slot->bindings_non_drag)
                && !current_mode_has_gesture(slot->bindings_drag)) {
                start_drag_no_binding(f, result);
                return;
            }
            gesture_clear_pending_long_press();
            if (xd_cnt > 0)
                start_drag_with_binding(f, result, xd, xd_cnt);
            else
                start_drag_no_binding(f, result);
            return;
        }

        // ---- Post-double-tap drag ----
        if (ctx->post_double_tap_drag) {
            if (g_state.cfg.is_tp && g_state.gesture_second_active && !f->is_second_finger)
                return;
            if (!current_mode_has_gesture(slot->bindings_non_drag)
                && !current_mode_has_gesture(slot->bindings_drag)) {
                ctx->post_double_tap_drag = false;
                return;
            }
            const TouchBinding* eff_x  = x;  int eff_x_cnt  = x_cnt;
            const TouchBinding* eff_xd = xd; int eff_xd_cnt = xd_cnt;
            if (g_state.gesture_second_active && !f->is_second_finger) {
                TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
                if (sf)
                    resolve_binding_slot(sf, slot, &eff_x, &eff_x_cnt, &eff_xd, &eff_xd_cnt);
            }
            const TouchBinding* eff_fb = eff_x; int eff_fb_cnt = eff_x_cnt;
            const TouchBinding* drag_binding = NULL; int drag_count = 0;
            if (resolve_drag_binding(eff_xd, eff_xd_cnt, eff_x, eff_x_cnt,
                eff_fb, eff_fb_cnt, true, true, &drag_binding, &drag_count)) {
                if (drag_binding == eff_x)
                    ctx->pending_double_count = 0;
                ctx->post_double_tap_drag = false;
                if (g_state.gesture_second_active)
                    mark_second_finger_dragging(f, result);
                start_drag_with_binding(f, result, drag_binding, drag_count);
            } else {
                ctx->post_double_tap_drag = false;
            }
            return;
        }

        // ---- Deferred D pending — drag without binding change ----
        if (ctx->pending_double_count > 0) {
            f->single_tap_hold_delay_ms = 0;
            f->state = GESTURE_STATE_DRAGGING;
            return;
        }

        // ---- Drag threshold crossed — resolve drag binding ----
        if (!current_mode_has_gesture(slot->bindings_non_drag)
            && !current_mode_has_gesture(slot->bindings_drag)) {
            start_drag_no_binding(f, result);
            return;
        }

        // --- Held-action check (second finger) ---
        if (slot->move_hold_check && g_state.gesture_is_action_held) {
            int eff_sd = g_state.cfg.is_tp
                ? g_state.cfg.tp[slot->tp_drag].count : xd_cnt;
            int eff_dd = g_state.cfg.is_tp
                ? (slot->comp_dt != 0 ? g_state.cfg.tp[slot->comp_dt].count : 0) : 0;
            if (!eff_sd && !eff_dd) {
                ctx->pending_double_count = 0;
                start_drag_no_binding(f, result);
                return;
            }
        }

        // --- TP gate (main finger only) ---
        if (slot->move_gate_tp_nodrag && g_state.cfg.is_tp && !g_state.cfg.caps_has_drag_bindings)
            return;

        // --- Determine press_on_drag for this drag slot ---
        bool press;
        if (slot->move_press_always) {
            press = x_cnt > 0 && f->cached_has_active_double_tap
                && !f->cached_has_active_double_tap_drag;
        } else {
            press = x_cnt > 0
                && (f->cached_has_active_double_tap
                    || f->cached_has_active_double_tap_drag
                    || f->cached_has_long_press_timer)
                && !g_state.gesture_is_action_held;
        }

        const TouchBinding* fb = slot->move_resolve_fb ? ctx->fallback : NULL;
        int fb_cnt = slot->move_resolve_fb ? ctx->fallback_count : 0;
        const TouchBinding* drag_binding = NULL; int drag_count = 0;
        if (!resolve_drag_binding(xd, xd_cnt, x, x_cnt, fb, fb_cnt,
            slot->move_press_always ? press : (g_state.cfg.is_ts && press),
            slot->move_resolve_fb, &drag_binding, &drag_count))
            return;

        // --- Post-resolution held check (second finger only) ---
        if (slot->move_hold_check && g_state.gesture_is_action_held) {
            execute_actions_hold(result, drag_binding, drag_count);
            start_drag_no_binding(f, result);
            return;
        }

        start_drag_with_binding(f, result, drag_binding, drag_count);
        return;
    }

    // =====================================================================
    // GESTURE_EVENT_UP
    // =====================================================================
    if (event == GESTURE_EVENT_UP) {
        g_state.gesture_is_down_event = false;
        f->tap_up_x = f->x;
        f->tap_up_y = f->y;

        // ---- Slot selection + binding ----
        const GesturePairSlot* slot = select_gesture_slot(f);
        if (!slot) { f->state = GESTURE_STATE_IDLE; return; }

        const TouchBinding* x  = NULL; int x_cnt  = 0;
        const TouchBinding* xd = NULL; int xd_cnt = 0;
        resolve_binding_slot(f, slot, &x, &x_cnt, &xd, &xd_cnt);

        // ---- Held-S2 prelude (second finger only) ----
        if (slot->is_second_finger) {
            if (!g_state.gesture_second_active) return;

            if (g_state.gesture_is_action_held) {
                bool has_dt_for_sdtw = f->cached_has_active_double_tap
                    && f->state != GESTURE_STATE_DRAGGING;
                if (has_dt_for_sdtw) {
                    release_held_actions(result);
                    gesture_clear_second_finger_state();
                } else {
                    gesture_clear_second_finger_state();
                }
            }
        }

        // ---- Main finger TP early exits ----
        if (!slot->is_second_finger) {
            if (g_state.cfg.is_tp
                && f->state == GESTURE_STATE_TAP_WAITING
                && !current_mode_has_gestures()) {
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                goto cleanup_main;
            }

            if (g_state.cfg.is_tp
                && f->state == GESTURE_STATE_TAP_WAITING
                && (f->cached_has_moved_beyond_threshold
                    || (time_ms - f->down_time_ms) >= 200)) {
                if (g_state.gesture_double_tap_consumed) {
                    g_state.gesture_double_tap_consumed = false;
                    if (!g_state.gesture_is_action_held)
                        execute_actions(result, x, x_cnt);
                }
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                goto cleanup_main;
            }
        }

        // ---- Unified state machine (both main and second finger) ----
        switch (f->state) {
            case GESTURE_STATE_TAP_WAITING: {
                // Single-tap delay: main finger only (S-only feature)
                if (!slot->is_second_finger && g_state.cfg.single_tap_delay_ms > 0) {
                    f->single_tap_deferred = true;
                    f->single_tap_deferred_time = time_ms + g_state.cfg.single_tap_delay_ms;
                    return;
                }

                // Path 1: deferred D from DT confirm
                if (ctx->deferred_double_count > 0) {
                    if (!g_state.gesture_is_action_held)
                        execute_actions(result, ctx->deferred_double, ctx->deferred_double_count);
                    ctx->post_double_tap_drag = false;
                    ctx->dt_consumed = true;
                    f->state = GESTURE_STATE_IDLE;
                    goto cleanup;
                }

                // Path 2: post-DT drag cleanup
                if (ctx->post_double_tap_drag) {
                    ctx->post_double_tap_drag = false;
                    ctx->dt_consumed = true;
                    f->state = GESTURE_STATE_IDLE;
                    goto cleanup;
                }

                // Path 3: DT consumed (3+ tap)
                if (ctx->dt_consumed) {
                    ctx->dt_consumed = false;
                    if (!g_state.gesture_is_action_held && x_cnt > 0)
                        execute_actions(result, x, x_cnt);
                    f->state = GESTURE_STATE_IDLE;
                    goto cleanup;
                }

                // Path 4: Normal first tap-up
                if (f->cached_has_active_double_tap) {
                    if (slot->is_second_finger) {
                        enter_sdtw(f, time_ms);
                    } else {
                        enter_double_tap_waiting(f, time_ms,
                            x, x_cnt,
                            f->bindings.double_tap, f->bindings.double_tap_count);
                    }
                } else if (f->cached_has_active_double_tap_drag) {
                    if (!g_state.gesture_is_action_held && x_cnt > 0)
                        execute_actions(result, x, x_cnt);
                    if (slot->is_second_finger) {
                        enter_sdtw(f, time_ms);
                    } else {
                        enter_double_tap_waiting(f, time_ms, NULL, 0, NULL, 0);
                    }
                } else {
                    if (slot->is_second_finger) {
                        // Second finger: use fallback (S2) if no DT
                        if (ctx->fallback_count > 0) {
                            execute_actions(result, ctx->fallback, ctx->fallback_count);
                            ctx->fallback_count = 0;
                        }
                    } else {
                        if (!g_state.gesture_is_action_held && x_cnt > 0)
                            execute_actions(result, x, x_cnt);
                    }
                    f->state = GESTURE_STATE_IDLE;
                }

                // Second finger after Path 4: always cleanup + return
                if (slot->is_second_finger) {
                    if (!(ctx->post_double_tap_drag && g_state.gesture_is_action_held)) {
                        release_held_actions(result);
                        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
                        add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
                    }
                    gesture_clear_second_finger_globals();
                    f->state = GESTURE_STATE_IDLE;
                    return;
                }

                // Main finger: release held if not deferred
                if (!f->single_tap_deferred) {
                    if (!g_state.gesture_second_active || !g_state.gesture_is_action_held
                        || g_state.cfg.is_tp)
                        release_held_actions(result);
                }
                return;
            }

            case GESTURE_STATE_LONG_PRESSING: {
                if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag)
                    execute_deferred_double(result);
                if (ctx->pending_long_press_count > 0) {
                    execute_actions(result, ctx->pending_long_press,
                        ctx->pending_long_press_count);
                    ctx->pending_long_press_count = 0;
                } else if (!g_state.gesture_is_action_held
                    && f->cached_has_active_long_press)
                    execute_actions(result, f->bindings.long_press,
                        f->bindings.long_press_count);
                release_held_actions(result);
                goto cleanup;
            }

            case GESTURE_STATE_DRAGGING: {
                if (ctx->deferred_double_count > 0
                    && (g_state.gesture_is_action_held
                        ? !bindings_equal(ctx->deferred_double, ctx->deferred_double_count,
                            g_state.gesture_held_actions, g_state.gesture_held_count)
                        : true))
                    execute_actions(result, ctx->deferred_double, ctx->deferred_double_count);
                ctx->pending_long_press_count = 0;
                release_held_actions(result);
                goto cleanup;
            }

            case GESTURE_STATE_DOUBLE_TAP_WAITING: {
                if (x_cnt > 0)
                    execute_actions(result, x, x_cnt);
                if (!f->cached_has_active_single_tap_drag)
                    release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                goto cleanup;
            }

            default: {
                if (ctx->post_double_tap_drag) {
                    ctx->post_double_tap_drag = false;
                    release_held_actions(result);
                }
                goto cleanup;
            }
        }

    cleanup:
        // ---- Second-finger post-UP cleanup ----
        if (slot->is_second_finger) {
            if (!(ctx->post_double_tap_drag && g_state.gesture_is_action_held)) {
                release_held_actions(result);
                add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
                add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
            }
            gesture_clear_second_finger_globals();
            f->state = GESTURE_STATE_IDLE;
            return;
        }

    cleanup_main:
        f->state = GESTURE_STATE_IDLE;
        ctx->post_double_tap_drag = false;
        g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_pending_double_count = 0;
        g_state.gesture_pending_deferred_double_count = 0;
        ctx->pending_long_press_count = 0;
        g_state.gesture_main_ptr_id = -1;
        ctx->dt_consumed = false;
        if (!g_state.gesture_second_active)
            g_state.gesture_deferred_second_finger_tap = false;
        return;
    }

    // =====================================================================
    // GESTURE_EVENT_TICK
    // =====================================================================
    if (event == GESTURE_EVENT_TICK) {
        // ---- Slot selection + binding ----
        const GesturePairSlot* slot = select_gesture_slot(f);
        if (!slot) return;

        const TouchBinding* x  = NULL; int x_cnt  = 0;
        const TouchBinding* xd = NULL; int xd_cnt = 0;
        resolve_binding_slot(f, slot, &x, &x_cnt, &xd, &xd_cnt);

        // ---- LP timer ----
        if (f->cached_has_long_press_timer
            && !f->cached_has_moved_beyond_threshold
            && !g_state.gesture_is_action_held
            && time_ms - f->down_time_ms >= (uint64_t)g_state.cfg.long_press_timeout_ms)
        {
            g_state.second_double_tap_waiting = false;
            g_state.pending_second_double_count = 0;

            GesturePairPlan lp_plan = resolve_gesture_pair(f, slot);

            if (lp_plan.pulse_on_up) {
                if (lp_plan.drag_available) {
                    copy_bindings_bounded(x, x_cnt,
                        ctx->pending_long_press, &ctx->pending_long_press_count, 8);
                    copy_bindings_bounded(x, x_cnt,
                        g_state.gesture_pending_deferred_long_press,
                        &g_state.gesture_pending_deferred_long_press_count, 8);
                } else {
                    execute_actions_hold(result, x, x_cnt);
                    f->cached_has_active_single_tap = false;
                    f->cached_has_long_press_timer = false;
                }
                f->single_tap_hold_delay_ms = 0;
                f->state = GESTURE_STATE_LONG_PRESSING;
            } else {
                execute_actions_hold(result, x, x_cnt);
                f->single_tap_hold_delay_ms = 0;
                f->state = GESTURE_STATE_LONG_PRESSING;
            }
            if (g_state.cfg.gesture_long_press_haptic > 0)
                add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
        }

        // ---- S hold timer (unified: execute vs hold via slot flag) ----
        if (f->single_tap_hold_delay_ms > 0
            && time_ms - f->single_tap_hold_timer >= (uint64_t)f->single_tap_hold_delay_ms
            && !g_state.gesture_is_action_held
            && !f->cached_has_moved_beyond_threshold
            && x_cnt > 0)
        {
            f->single_tap_hold_delay_ms = 0;
            if (slot->tick_s_execute)
                execute_actions(result, x, x_cnt);
            else
                execute_actions_hold(result, x, x_cnt);
            ctx->pending_double_count = 0;
        }

        // ---- DT waiting timeout (unified: fallback vs pending_double via slot flag) ----
        if (ctx->dt_waiting
            && time_ms - ctx->dt_wait_start_time >= (uint64_t)g_state.cfg.double_tap_timeout_ms)
        {
            ctx->dt_waiting = false;
            if (slot->tick_dt_uses_fb) {
                // SDTW style: use ctx->fallback, clear second-finger state
                if (ctx->fallback_count > 0)
                    execute_actions(result, ctx->fallback, ctx->fallback_count);
                ctx->fallback_count = 0;
                g_state.second_double_tap_waiting = false;
                g_state.pending_second_double_count = 0;
                TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
                if (mf && mf->state != GESTURE_STATE_DRAGGING)
                    mf->state = GESTURE_STATE_IDLE;
            } else {
                // DT_TIMEOUT style: use ctx->pending_double
                if (ctx->pending_double_count > 0)
                    execute_actions(result, ctx->pending_double, ctx->pending_double_count);
                ctx->pending_double_count = 0;
                ctx->deferred_double_count = 0;
                TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
                if (mf) mf->state = GESTURE_STATE_IDLE;
            }
        }

        // ---- Deferred single-tap ----
        if (ctx->single_tap_deferred && time_ms >= ctx->single_tap_deferred_time) {
            ctx->single_tap_deferred = false;
            gesture_branch(f, ctx, result, time_ms, GESTURE_EVENT_UP, 0, 0);
        }
    }
}
