#include <android/log.h>
#include <inttypes.h>
#include "../touch_processor_internal.h"
#include "types.h"
#include "branch.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Winlator_Gesture", __VA_ARGS__)

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
        LOGD("DOWN ptr=%d state=%d", f->ptr_id, f->state);

        // ---- Role assignment (main vs second finger) ----
        if (g_state.gesture_main_ptr_id < 0) {
            f->is_second_finger = false;
            g_state.gesture_main_ptr_id = f->ptr_id;
            LOGD("DOWN ptr=%d ROLE_MAIN", f->ptr_id);
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
            LOGD("DOWN ptr=%d ROLE_SECOND main_ptr=%d", f->ptr_id, g_state.gesture_main_ptr_id);
        } else if (f->ptr_id != g_state.gesture_main_ptr_id) {
            // Third+ finger: ignore (original IDLE behavior)
            LOGD("DOWN ptr=%d IGNORE_3RD_PLUS", f->ptr_id);
            f->state = GESTURE_STATE_IDLE;
            return;
        }

        // --- DT_WAITING check (uses D/Dd or D2/Dd2 slot for plan) ---
        // For the main finger, ctx->dt_waiting was set by enter_double_tap_waiting.
        // For the second finger, ctx->dt_waiting is never set (enter_double_tap_waiting
        // sets it on the main finger's ctx only), so second-finger DT/SDTW is checked
        // separately using globals below.
        if (ctx->dt_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                LOGD("DOWN ptr=%d DT_CONFIRM", f->ptr_id);
                const GesturePairSlot* dt_slot = f->is_second_finger
                    ? &GESTURE_SLOTS[4] : &GESTURE_SLOTS[1];
                GesturePairPlan d_plan = resolve_gesture_pair(f, dt_slot);
                confirm_double_tap(result, d_plan,
                    ctx->pending_double, ctx->pending_double_count,
                    ctx->deferred_double, &ctx->deferred_double_count, 8,
                    &ctx->post_double_tap_drag);
                ctx->dt_waiting = false;
                g_state.gesture_double_tap_waiting = false;
                ctx->pending_double_count = 0;
                reset_finger_tap_state(f);
                f->state = GESTURE_STATE_TAP_WAITING;
                return;
            } else {
                LOGD("DOWN ptr=%d DT_CANCEL (out of range)", f->ptr_id);
                gesture_cancel_double_tap_wait(result);
            }
        }

        // ---- Second-finger DT/SDTW checks (original confirm_second_double_tap
        //      + confirm_second_finger_global_dt) ----
        if (f->is_second_finger && g_state.gesture_second_active) {
            // SDTW check
            if (g_state.second_double_tap_waiting
                && (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag))
            {
                LOGD("DOWN ptr=%d SDTW_CONFIRM", f->ptr_id);
                gesture_clear_second_finger_state();
                GesturePairPlan d_plan = resolve_gesture_pair(f, &GESTURE_SLOTS[4]);
                confirm_double_tap(result, d_plan,
                    f->bindings.double_tap, f->bindings.double_tap_count,
                    g_state.pending_second_double,
                    &g_state.pending_second_double_count, 8,
                    &g_state.gesture_post_double_tap_drag);
                f->cached_has_long_press_timer = false;
                f->down_time_ms = time_ms;
                f->state = GESTURE_STATE_TAP_WAITING;
                f->single_tap_hold_delay_ms = 0;
                f->single_tap_hold_timer = 0;
                return;
            }
            // Global DT_WAITING check (second finger confirms main-finger DT)
            if (g_state.gesture_double_tap_waiting) {
                if (gesture_is_within_tap_distance(f->x, f->y)) {
                    LOGD("DOWN ptr=%d GLOBAL_DT_CONFIRM (second finger)", f->ptr_id);
                    TouchFinger* _main_for_dt = find_finger(g_state.gesture_main_ptr_id);
                    double_tap_confirm_internal(result, _main_for_dt ? _main_for_dt : f);
                    TouchFinger* _mf = find_finger(g_state.gesture_main_ptr_id);
                    if (_mf) {
                        _mf->state = GESTURE_STATE_TAP_WAITING;
                        reset_finger_tap_state(_mf);
                        _mf->down_x = _mf->x;
                        _mf->down_y = _mf->y;
                    }
                    gesture_clear_second_finger_globals();
                    f->state = GESTURE_STATE_IDLE;
                    return;
                } else {
                    LOGD("DOWN ptr=%d GLOBAL_DT_CANCEL (second finger)", f->ptr_id);
                    gesture_cancel_double_tap_wait(result);
                }
            }
            // Clear stale SDTW state (original: gesture_clear_second_finger_state)
            gesture_clear_second_finger_state();
        }

        // --- Clean per-finger ctx for fresh finger-down (after DT checks) ---
        memset(ctx, 0, sizeof(*ctx));

        // ---- No-gestures mode early exit (original touchpad_finger_down behavior) ----
        if (!current_mode_has_gestures() && !g_state.gesture_double_tap_waiting) {
            LOGD("DOWN ptr=%d NO_GESTURES -> IDLE", f->ptr_id);
            f->state = GESTURE_STATE_IDLE;
            return;
        }

        // ---- Slot selection (ALWAYS S/Sd or S2/Sd2 for normal DOWN) ----
        const GesturePairSlot* slot = f->is_second_finger
            ? &GESTURE_SLOTS[3] : &GESTURE_SLOTS[0];

        // ---- Binding resolution ----
        const TouchBinding* x  = NULL; int x_cnt  = 0;
        const TouchBinding* xd = NULL; int xd_cnt = 0;
        resolve_binding_slot(f, slot, &x, &x_cnt, &xd, &xd_cnt);

        // --- Fallback save (data-driven via slot flag) ---
        if (slot->down_save_fallback && x_cnt > 0 && ctx->fallback_count == 0) {
            // Original guard: skip saving S2 as fallback when only Dd2 is bound (no D2)
            if (slot->bindings_non_drag != GESTURE_SINGLE_2ND
                || f->cached_has_active_double_tap
                || !f->cached_has_active_double_tap_drag) {
                copy_bindings_bounded(x, x_cnt, ctx->fallback, &ctx->fallback_count, 8);
            }
        }

        // ---- Normal finger-down ----
        f->state = GESTURE_STATE_TAP_WAITING;
        f->single_tap_hold_delay_ms = 0;
        f->single_tap_hold_timer = 0;
        ctx->dt_consumed = false;

        GesturePairPlan plan = resolve_gesture_pair(f, slot);
        // Second finger (down_hold_always): always hold S2 immediately on DOWN
        // regardless of plan competition (original behavior).
        if (slot->down_hold_always) {
            if (x_cnt > 0) {
                LOGD("DOWN ptr=%d HOLD_ALWAYS x_cnt=%d", f->ptr_id, x_cnt);
                execute_actions_hold(result, x, x_cnt);
            }
        } else if (g_state.cfg.is_ts && !g_state.gesture_is_action_held) {
            LOGD("DOWN ptr=%d TS_TAP_DOWN x_cnt=%d pulse_on_up=%d hold=%d drag=%d",
                f->ptr_id, x_cnt, plan.pulse_on_up, plan.hold_delay_ms, plan.drag_available);
            // TS main finger: plan-based execution (may start hold timer, force_hold=true)
            execute_tap_on_finger_down(f, result, time_ms, plan, true, x, x_cnt);
        } else {
            LOGD("DOWN ptr=%d TP_NO_DOWN_ACTION x_cnt=%d", f->ptr_id, x_cnt);
        }
        // TP main finger: S fires on finger-up (original behavior) — no DOWN action.
        return;
    }

    // =====================================================================
    // GESTURE_EVENT_MOVE
    // =====================================================================
    if (event == GESTURE_EVENT_MOVE) {
        LOGD("MOVE ptr=%d state=%d", f->ptr_id, f->state);
        if (f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING) {
            LOGD("MOVE ptr=%d SKIP (state=%d)", f->ptr_id, f->state);
            return;
        }

        // ---- Cancel DT waiting on significant movement (original behavior) ----
        if (ctx->dt_waiting) {
            LOGD("MOVE ptr=%d CANCEL_DT_WAITING", f->ptr_id);
            gesture_cancel_double_tap_wait(result);
        }

        // ---- Slot selection — original check_start_drag always resolves from
        //      S/Sd (main) / S2/Sd2 (second), except when LONG_PRESSING where
        //      L/Ld bindings must be used for the drag binding. ----
        const GesturePairSlot* slot;
        if (f->state == GESTURE_STATE_LONG_PRESSING) {
            slot = f->is_second_finger ? NULL : &GESTURE_SLOTS[2];
        } else {
            slot = f->is_second_finger ? &GESTURE_SLOTS[3] : &GESTURE_SLOTS[0];
        }
        if (!slot) {
            LOGD("MOVE ptr=%d NO_SLOT (second LP)", f->ptr_id);
            return;
        }

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
        if (fabsf(dx) <= g_state.cfg.drag_threshold_px && fabsf(dy) <= g_state.cfg.drag_threshold_px) {
            LOGD("MOVE ptr=%d BELOW_THRESHOLD dx=%.1f dy=%.1f", f->ptr_id, dx, dy);
            return;
        }

        // ---- Long press drag ----
        if (f->state == GESTURE_STATE_LONG_PRESSING) {
            LOGD("MOVE ptr=%d LP_DRAG xd_cnt=%d", f->ptr_id, xd_cnt);
            if (!current_mode_has_gesture(slot->bindings_non_drag)
                && !current_mode_has_gesture(slot->bindings_drag)) {
                LOGD("MOVE ptr=%d LP_DRAG_NO_BINDING", f->ptr_id);
                start_drag_no_binding(f, result);
                return;
            }
            gesture_clear_pending_long_press();
            ctx->pending_long_press_count = 0;
            if (xd_cnt > 0) {
                LOGD("MOVE ptr=%d LP_DRAG_WITH_BINDING xd_cnt=%d", f->ptr_id, xd_cnt);
                start_drag_with_binding(f, result, xd, xd_cnt);
            } else {
                LOGD("MOVE ptr=%d LP_DRAG_NO_BINDING (no drag bindings)", f->ptr_id);
                start_drag_no_binding(f, result);
            }
            return;
        }

        // ---- Post-double-tap drag ----
        if (ctx->post_double_tap_drag) {
            LOGD("MOVE ptr=%d POST_DT_DRAG", f->ptr_id);
            if (g_state.cfg.is_tp && g_state.gesture_second_active && !f->is_second_finger) {
                LOGD("MOVE ptr=%d POST_DT_DRAG_TP_GATE (second active)", f->ptr_id);
                return;
            }
            if (!current_mode_has_gesture(slot->bindings_non_drag)
                && !current_mode_has_gesture(slot->bindings_drag)) {
                LOGD("MOVE ptr=%d POST_DT_DRAG_NO_BINDINGS -> cancel", f->ptr_id);
                ctx->post_double_tap_drag = false;
                return;
            }
            // Use D/Dd slot bindings (not S/Sd) for post-DT drag:
            // Dd > D > S (via fallback) priority
            const GesturePairSlot* dt_slot = f->is_second_finger
                ? &GESTURE_SLOTS[4] : &GESTURE_SLOTS[1];
            const TouchBinding* dx  = NULL; int dx_cnt  = 0;
            const TouchBinding* dxd = NULL; int dxd_cnt = 0;
            resolve_binding_slot(f, dt_slot, &dx, &dx_cnt, &dxd, &dxd_cnt);
            const TouchBinding* eff_x  = dx;  int eff_x_cnt  = dx_cnt;
            const TouchBinding* eff_xd = dxd; int eff_xd_cnt = dxd_cnt;
            if (g_state.gesture_second_active && !f->is_second_finger) {
                TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
                if (sf)
                    resolve_binding_slot(sf, dt_slot, &eff_x, &eff_x_cnt, &eff_xd, &eff_xd_cnt);
            }
            // Original post-DT fallback: use S bindings (dt_fb->single_tap), not eff_x
            const TouchFinger* eff_fb_f = f;
            if (g_state.gesture_second_active && !f->is_second_finger) {
                TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
                if (sf) eff_fb_f = sf;
            }
            const TouchBinding* eff_fb = eff_fb_f->bindings.single_tap;
            int eff_fb_cnt = eff_fb_f->bindings.single_tap_count;
            const TouchBinding* drag_binding = NULL; int drag_count = 0;
            if (resolve_drag_binding(eff_xd, eff_xd_cnt, eff_x, eff_x_cnt,
                eff_fb, eff_fb_cnt, true, true, &drag_binding, &drag_count)) {
                if (drag_binding == eff_x)
                    ctx->pending_double_count = 0;
                ctx->post_double_tap_drag = false;
                if (g_state.gesture_second_active)
                    mark_second_finger_dragging(f, result);
                LOGD("MOVE ptr=%d POST_DT_DRAG_START drag_cnt=%d", f->ptr_id, drag_count);
                start_drag_with_binding(f, result, drag_binding, drag_count);
            } else {
                LOGD("MOVE ptr=%d POST_DT_DRAG_RESOLVE_FAIL -> cancel", f->ptr_id);
                ctx->post_double_tap_drag = false;
            }
            return;
        }

        // ---- Deferred D pending — drag without binding change ----
        if (ctx->deferred_double_count > 0) {
            LOGD("MOVE ptr=%d DEFERRED_D_PENDING -> DRAGGING", f->ptr_id);
            f->single_tap_hold_delay_ms = 0;
            f->state = GESTURE_STATE_DRAGGING;
            return;
        }

        // ---- Drag threshold crossed — resolve drag binding ----
        if (!current_mode_has_gesture(slot->bindings_non_drag)
            && !current_mode_has_gesture(slot->bindings_drag)) {
            LOGD("MOVE ptr=%d NO_GESTURE -> DRAG_NO_BINDING", f->ptr_id);
            start_drag_no_binding(f, result);
            return;
        }

        // --- Held-action check (second finger) ---
        if (slot->move_hold_check && g_state.gesture_is_action_held) {
            int eff_sd = g_state.cfg.is_tp
                ? g_state.cfg.tp[slot->tp_drag].count : xd_cnt;
            int eff_dd = slot->comp_dt_drag
                ? (g_state.cfg.is_tp
                    ? g_state.cfg.tp[slot->comp_dt_drag].count
                    : f->bindings.double_tap_drag_count)
                : 0;
            if (!eff_sd && !eff_dd) {
                LOGD("MOVE ptr=%d HELD_NO_DRAG_BINDINGS -> DRAG_NO_BINDING", f->ptr_id);
                ctx->pending_double_count = 0;
                start_drag_no_binding(f, result);
                return;
            }
        }

        // --- TP gate (main finger only) ---
        if (slot->move_gate_tp_nodrag && g_state.cfg.is_tp && !g_state.cfg.caps_has_drag_bindings) {
            LOGD("MOVE ptr=%d TP_GATE (no drag bindings)", f->ptr_id);
            return;
        }

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

        const TouchBinding* fb = NULL; int fb_cnt = 0;
        if (slot->move_resolve_fb) {
            // Original fallback for S2/Sd2 is Dd2; for D2/Dd2 use ctx->fallback
            if (slot->bindings_non_drag == GESTURE_SINGLE_2ND
                && f->cached_has_active_double_tap_drag)
            {
                fb = f->bindings.double_tap_drag;
                fb_cnt = f->bindings.double_tap_drag_count;
            } else {
                fb = ctx->fallback;
                fb_cnt = ctx->fallback_count;
            }
        }
        const TouchBinding* drag_binding = NULL; int drag_count = 0;
        bool _drag_ok = resolve_drag_binding(xd, xd_cnt, x, x_cnt, fb, fb_cnt,
            slot->move_press_always ? press : (g_state.cfg.is_ts && press),
            slot->move_resolve_fb, &drag_binding, &drag_count);
        if (f->is_second_finger) {
            LOGD("MOVE ptr=%d DRAG_RESOLVE xd_cnt=%d x_cnt=%d press=%d fb_cnt=%d slot_nd=%d ok=%d drag_type=%d drag_cnt=%d",
                f->ptr_id, xd_cnt, x_cnt,
                slot->move_press_always ? press : (g_state.cfg.is_ts && press),
                fb_cnt, slot->bindings_non_drag, _drag_ok,
                drag_binding ? drag_binding->type : 0, drag_count);
        }
        if (!_drag_ok) return;

        // --- Post-resolution held check (second finger only) ---
        if (slot->move_hold_check && g_state.gesture_is_action_held) {
            LOGD("MOVE ptr=%d POST_RESOLVE_HELD drag_cnt=%d", f->ptr_id, drag_count);
            execute_actions_hold(result, drag_binding, drag_count);
            start_drag_no_binding(f, result);
            return;
        }

        LOGD("MOVE ptr=%d DRAG_START drag_cnt=%d press=%d", f->ptr_id, drag_count, press);
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
        LOGD("UP ptr=%d state=%d sec=%d", f->ptr_id, f->state, f->is_second_finger);

        // ---- Slot selection (S/Sd for TAP_WAITING, L/Ld for LONG_PRESSING) ----
        const GesturePairSlot* slot;
        if (f->state == GESTURE_STATE_LONG_PRESSING) {
            slot = &GESTURE_SLOTS[2]; // L/Ld for LP drag
        } else {
            slot = f->is_second_finger ? &GESTURE_SLOTS[3] : &GESTURE_SLOTS[0];
        }

        const TouchBinding* x  = NULL; int x_cnt  = 0;
        const TouchBinding* xd = NULL; int xd_cnt = 0;
        resolve_binding_slot(f, slot, &x, &x_cnt, &xd, &xd_cnt);
        LOGD("UP ptr=%d slot=%p x_cnt=%d xd_cnt=%d", f->ptr_id, (void*)slot, x_cnt, xd_cnt);

        // ---- Held-S2 prelude (second finger only) ----
        if (slot->is_second_finger) {
            if (!g_state.gesture_second_active) {
                LOGD("UP ptr=%d SECOND_NOT_ACTIVE -> return", f->ptr_id);
                return;
            }

            if (g_state.gesture_is_action_held) {
                bool has_dt_for_sdtw = f->cached_has_active_double_tap
                    && f->state != GESTURE_STATE_DRAGGING;
                if (has_dt_for_sdtw) {
                    LOGD("UP ptr=%d HELD_S2_DT_SDTW -> release+clear", f->ptr_id);
                    release_held_actions(result);
                    gesture_clear_second_finger_state();
                } else {
                    LOGD("UP ptr=%d HELD_S2_CLEAR", f->ptr_id);
                    gesture_clear_second_finger_state();
                }
            }
        }

        // ---- Main finger TP early exits ----
        if (!slot->is_second_finger) {
            if (g_state.cfg.is_tp
                && f->state == GESTURE_STATE_TAP_WAITING
                && !current_mode_has_gestures()) {
                LOGD("UP ptr=%d TP_NO_GESTURES -> cleanup_main", f->ptr_id);
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                goto cleanup_main;
            }

            if (g_state.cfg.is_tp
                && f->state == GESTURE_STATE_TAP_WAITING
                && (f->cached_has_moved_beyond_threshold
                    || (time_ms - f->down_time_ms) >= 200)) {
                LOGD("UP ptr=%d TP_EARLY_EXIT moved=%d dt=%" PRIu64,
                    f->ptr_id, f->cached_has_moved_beyond_threshold, time_ms - f->down_time_ms);
                if (g_state.gesture_double_tap_consumed) {
                    g_state.gesture_double_tap_consumed = false;
                    if (!g_state.gesture_is_action_held) {
                        LOGD("UP ptr=%d TP_EARLY_SINGLE_TAP x_cnt=%d", f->ptr_id, x_cnt);
                        execute_actions(result, x, x_cnt);
                    }
                }
                release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                goto cleanup_main;
            }
        }

        // ---- Unified state machine (both main and second finger) ----
        switch (f->state) {
            case GESTURE_STATE_TAP_WAITING: {
                LOGD("UP ptr=%d TAP_WAITING deferred=%d dt_consumed=%d post_dt=%d",
                    f->ptr_id, f->single_tap_deferred, ctx->dt_consumed, ctx->post_double_tap_drag);

                // Single-tap delay: main finger only (S-only feature)
                if (!slot->is_second_finger && g_state.cfg.single_tap_delay_ms > 0) {
                    LOGD("UP ptr=%d DEFER_SINGLE_TAP delay=%d", f->ptr_id, g_state.cfg.single_tap_delay_ms);
                    f->single_tap_deferred = true;
                    f->single_tap_deferred_time = time_ms + g_state.cfg.single_tap_delay_ms;
                    return;
                }

                // Path 1: deferred D from DT confirm
                if (ctx->deferred_double_count > 0) {
                    LOGD("UP ptr=%d PATH1_DEFERRED_D cnt=%d", f->ptr_id, ctx->deferred_double_count);
                    if (!g_state.gesture_is_action_held)
                        execute_actions(result, ctx->deferred_double, ctx->deferred_double_count);
                    if (g_state.gesture_is_action_held)
                        release_held_actions(result);
                    ctx->post_double_tap_drag = false;
                    ctx->dt_consumed = true;
                    g_state.gesture_double_tap_consumed = true;
                    f->state = GESTURE_STATE_IDLE;
                    goto cleanup;
                }

                // Path 2: post-DT drag cleanup
                if (ctx->post_double_tap_drag) {
                    LOGD("UP ptr=%d PATH2_POST_DT_DRAG_CLEANUP", f->ptr_id);
                    ctx->post_double_tap_drag = false;
                    ctx->dt_consumed = true;
                    g_state.gesture_double_tap_consumed = true;
                    f->state = GESTURE_STATE_IDLE;
                    goto cleanup;
                }

                // Path 3: DT consumed (3+ tap)
                if (ctx->dt_consumed) {
                    ctx->dt_consumed = false;
                    LOGD("UP ptr=%d PATH3_DT_CONSUMED x_cnt=%d", f->ptr_id, x_cnt);
                    if (!g_state.gesture_is_action_held && x_cnt > 0)
                        execute_actions(result, x, x_cnt);
                    f->state = GESTURE_STATE_IDLE;
                    goto cleanup;
                }

                // Path 4: Normal first tap-up
                if (f->cached_has_active_double_tap) {
                    LOGD("UP ptr=%d PATH4_DT_ENTRY sec=%d", f->ptr_id, slot->is_second_finger);
                    if (slot->is_second_finger) {
                        enter_sdtw(f, time_ms);
                    } else {
                        enter_double_tap_waiting(f, time_ms,
                            x, x_cnt,
                            f->bindings.double_tap, f->bindings.double_tap_count);
                    }
                } else if (f->cached_has_active_double_tap_drag) {
                    LOGD("UP ptr=%d PATH4_DTD_ENTRY sec=%d", f->ptr_id, slot->is_second_finger);
                    if (!g_state.gesture_is_action_held && x_cnt > 0)
                        execute_actions(result, x, x_cnt);
                    if (slot->is_second_finger) {
                        enter_sdtw(f, time_ms);
                    } else {
                        enter_double_tap_waiting(f, time_ms, NULL, 0, NULL, 0);
                    }
                } else {
                    LOGD("UP ptr=%d PATH4_SINGLE_TAP sec=%d x_cnt=%d is_held=%d",
                        f->ptr_id, slot->is_second_finger, x_cnt, g_state.gesture_is_action_held);
                    if (slot->is_second_finger) {
                        // Second finger: use fallback (S2) if no DT
                        if (ctx->fallback_count > 0) {
                            LOGD("UP ptr=%d PATH4_S2_FALLBACK cnt=%d", f->ptr_id, ctx->fallback_count);
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
                    // Execute pending D2 from SDTW confirm before releasing state
                    if (g_state.pending_second_double_count > 0
                        && !g_state.gesture_is_action_held) {
                        LOGD("UP ptr=%d EXECUTE_PENDING_D2 cnt=%d",
                            f->ptr_id, g_state.pending_second_double_count);
                        execute_actions(result, g_state.pending_second_double,
                            g_state.pending_second_double_count);
                    }
                    g_state.pending_second_double_count = 0;

                    // SDTW was just entered — preserve state for next second DOWN
                    if (ctx->dt_waiting) {
                        LOGD("UP ptr=%d SDTW_JUST_ENTERED preserve dt_waiting", f->ptr_id);
                        release_held_actions(result);
                        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
                        add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
                        f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
                        return;
                    }

                    g_state.second_double_tap_waiting = false;
                    if (!(ctx->post_double_tap_drag && g_state.gesture_is_action_held)) {
                        release_held_actions(result);
                        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
                        add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
                    }
                    gesture_clear_second_finger_globals();
                    f->state = GESTURE_STATE_IDLE;
                    deactivate_finger(f);
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
                LOGD("UP ptr=%d LONG_PRESSING pending_lp=%d active_lp=%d",
                    f->ptr_id, ctx->pending_long_press_count, f->cached_has_active_long_press);
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
                LOGD("UP ptr=%d DRAGGING deferred_d=%d", f->ptr_id, ctx->deferred_double_count);
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
                LOGD("UP ptr=%d DT_WAITING x_cnt=%d", f->ptr_id, x_cnt);
                if (x_cnt > 0)
                    execute_actions(result, x, x_cnt);
                if (!f->cached_has_active_single_tap_drag)
                    release_held_actions(result);
                g_state.gesture_double_tap_waiting = false;
                goto cleanup;
            }

            default: {
                LOGD("UP ptr=%d DEFAULT state=%d post_dt_drag=%d",
                    f->ptr_id, f->state, ctx->post_double_tap_drag);
                if (ctx->post_double_tap_drag) {
                    ctx->post_double_tap_drag = false;
                    release_held_actions(result);
                }
                goto cleanup;
            }
        }

    cleanup:
        LOGD("UP ptr=%d CLEANUP sec=%d", f->ptr_id, slot->is_second_finger);
        // ---- Second-finger post-UP cleanup ----
        if (slot->is_second_finger) {
            // Execute pending D2 from SDTW confirm before releasing state
            if (g_state.pending_second_double_count > 0
                && !g_state.gesture_is_action_held) {
                LOGD("UP ptr=%d CLEANUP_PENDING_D2 cnt=%d",
                    f->ptr_id, g_state.pending_second_double_count);
                execute_actions(result, g_state.pending_second_double,
                    g_state.pending_second_double_count);
            }
            g_state.pending_second_double_count = 0;
            if (!(ctx->post_double_tap_drag && g_state.gesture_is_action_held)) {
                release_held_actions(result);
                add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
                add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
            }
            gesture_clear_second_finger_globals();
            f->state = GESTURE_STATE_IDLE;
            deactivate_finger(f);
            return;
        }

    cleanup_main:
        LOGD("UP ptr=%d CLEANUP_MAIN", f->ptr_id);
        f->state = GESTURE_STATE_IDLE;
        ctx->post_double_tap_drag = false;
        g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_pending_double_count = 0;
        g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_pending_deferred_long_press_count = 0;
        g_state.gesture_double_tap_waiting = false;
        ctx->pending_long_press_count = 0;
        ctx->deferred_double_count = 0;
        g_state.gesture_main_ptr_id = -1;
        g_state.gesture_double_tap_consumed = false;
        ctx->dt_consumed = false;
        if (!g_state.gesture_second_active)
            g_state.gesture_deferred_second_finger_tap = false;
        deactivate_finger(f);
        return;
    }

    // =====================================================================
    // GESTURE_EVENT_TICK
    // =====================================================================
    if (event == GESTURE_EVENT_TICK) {
        LOGD("TICK ptr=%d state=%d lp_timer=%d held=%d moved=%d",
            f->ptr_id, f->state,
            f->cached_has_long_press_timer,
            g_state.gesture_is_action_held,
            f->cached_has_moved_beyond_threshold);

        // ---- Slot selection + binding ----
        const GesturePairSlot* slot = select_gesture_slot(f);
        if (!slot) {
            LOGD("TICK ptr=%d NO_SLOT", f->ptr_id);
            return;
        }

        const TouchBinding* x  = NULL; int x_cnt  = 0;
        const TouchBinding* xd = NULL; int xd_cnt = 0;
        resolve_binding_slot(f, slot, &x, &x_cnt, &xd, &xd_cnt);
        LOGD("TICK ptr=%d slot_nd=%d x_cnt=%d xd_cnt=%d",
            f->ptr_id, slot->bindings_non_drag, x_cnt, xd_cnt);

        // ---- LP timer cancellation (finger moved or action held) ----
        if (f->cached_has_long_press_timer
            && (f->cached_has_moved_beyond_threshold || g_state.gesture_is_action_held)) {
            LOGD("TICK ptr=%d LP_TIMER_CANCEL moved=%d held=%d",
                f->ptr_id, f->cached_has_moved_beyond_threshold, g_state.gesture_is_action_held);
            f->cached_has_long_press_timer = false;
        }

        // ---- LP timer (uses explicit L/Ld slot, not select_gesture_slot()) ----
        if (f->cached_has_long_press_timer
            && !f->cached_has_moved_beyond_threshold
            && !g_state.gesture_is_action_held
            && !f->gesture_activated_in_touch
            && time_ms - f->down_time_ms >= (uint64_t)g_state.cfg.long_press_timeout_ms)
        {
            LOGD("TICK ptr=%d LP_TIMER_FIRE elapsed=%" PRIu64,
                f->ptr_id, time_ms - f->down_time_ms);
            gesture_clear_second_finger_state();
            g_state.second_tap_fallback_count = 0;

            const GesturePairSlot* lp_slot = &GESTURE_SLOTS[2];
            const TouchBinding* lx  = NULL; int lx_cnt  = 0;
            const TouchBinding* lxd = NULL; int lxd_cnt = 0;
            resolve_binding_slot(f, lp_slot, &lx, &lx_cnt, &lxd, &lxd_cnt);

            GesturePairPlan lp_plan = resolve_gesture_pair(f, lp_slot);

            if (lp_plan.pulse_on_up) {
                if (lp_plan.drag_available) {
                    LOGD("TICK ptr=%d LP_PULSE_ON_UP+DRAG_AVAIL -> pending lx_cnt=%d", f->ptr_id, lx_cnt);
                    copy_bindings_bounded(lx, lx_cnt,
                        ctx->pending_long_press, &ctx->pending_long_press_count, 8);
                    copy_bindings_bounded(lx, lx_cnt,
                        g_state.gesture_pending_deferred_long_press,
                        &g_state.gesture_pending_deferred_long_press_count, 8);
                    f->cached_has_long_press_timer = false;
                } else {
                    LOGD("TICK ptr=%d LP_PULSE_ON_UP+NO_DRAG -> exec lx_cnt=%d", f->ptr_id, lx_cnt);
                    execute_actions(result, lx, lx_cnt);
                    f->cached_has_active_single_tap = false;
                    f->cached_has_long_press_timer = false;
                    f->cached_has_active_long_press = false;
                }
                f->single_tap_hold_delay_ms = 0;
                f->state = GESTURE_STATE_LONG_PRESSING;
            } else {
                LOGD("TICK ptr=%d LP_HOLD lx_cnt=%d", f->ptr_id, lx_cnt);
                execute_actions_hold(result, lx, lx_cnt);
                f->single_tap_hold_delay_ms = 0;
                f->state = GESTURE_STATE_LONG_PRESSING;
            }
            if (g_state.cfg.gesture_long_press_haptic > 0)
                add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
        }

        // ---- S hold timer (execute vs hold via S-specific slot) ----
        // Must resolve S/Sd bindings explicitly — select_gesture_slot may return
        // D/Dd when DT is bound, but the hold timer was set for S bindings.
        {
            const GesturePairSlot* s_slot = f->is_second_finger
                ? &GESTURE_SLOTS[3] : &GESTURE_SLOTS[0];
            const TouchBinding* sx  = NULL; int sx_cnt  = 0;
            const TouchBinding* sxd = NULL; int sxd_cnt = 0;
            resolve_binding_slot(f, s_slot, &sx, &sx_cnt, &sxd, &sxd_cnt);

            if (f->single_tap_hold_delay_ms > 0
                && time_ms - f->single_tap_hold_timer >= (uint64_t)f->single_tap_hold_delay_ms
                && !g_state.gesture_is_action_held
                && !f->cached_has_moved_beyond_threshold
                && sx_cnt > 0)
            {
                LOGD("TICK ptr=%d S_HOLD_FIRE delay=%d exec=%d sx_cnt=%d",
                    f->ptr_id, f->single_tap_hold_delay_ms, s_slot->tick_s_execute, sx_cnt);
                f->single_tap_hold_delay_ms = 0;
                if (s_slot->tick_s_execute)
                    execute_actions(result, sx, sx_cnt);
                else
                    execute_actions_hold(result, sx, sx_cnt);
                ctx->pending_double_count = 0;
                g_state.gesture_deferred_tap_count = 0;
                g_state.gesture_pending_double_count = 0;
                if (s_slot->is_second_finger) {
                    g_state.second_tap_fallback_count = 0;
                    gesture_clear_second_finger_state();
                }
            }
        }

        // ---- DT waiting timeout (unified: fallback vs pending_double via slot flag) ----
        if (ctx->dt_waiting
            && time_ms - ctx->dt_wait_start_time >= (uint64_t)g_state.cfg.double_tap_timeout_ms)
        {
            LOGD("TICK ptr=%d DT_TIMEOUT uses_fb=%d fallback_cnt=%d deferred_cnt=%d",
                f->ptr_id, slot->tick_dt_uses_fb, ctx->fallback_count,
                g_state.gesture_deferred_tap_count);
            ctx->dt_waiting = false;
            if (slot->tick_dt_uses_fb) {
                // SDTW style: use ctx->fallback, clear second-finger state
                if (ctx->fallback_count > 0) {
                    LOGD("TICK ptr=%d DT_TIMEOUT_SDTW_FALLBACK cnt=%d", f->ptr_id, ctx->fallback_count);
                    execute_actions(result, ctx->fallback, ctx->fallback_count);
                }
                ctx->fallback_count = 0;
                g_state.second_double_tap_waiting = false;
                g_state.pending_second_double_count = 0;
                g_state.second_tap_fallback_count = 0;
                TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
                if (mf && mf->state != GESTURE_STATE_DRAGGING)
                    mf->state = GESTURE_STATE_IDLE;
                deactivate_finger(f);
            } else {
                // DT_TIMEOUT style: fire S fallback (deferred single-tap)
                if (g_state.gesture_deferred_tap_count > 0) {
                    LOGD("TICK ptr=%d DT_TIMEOUT_S_FALLBACK cnt=%d",
                        f->ptr_id, g_state.gesture_deferred_tap_count);
                    execute_actions(result, g_state.gesture_deferred_tap,
                        g_state.gesture_deferred_tap_count);
                }
                g_state.gesture_deferred_tap_count = 0;
                g_state.gesture_pending_double_count = 0;
                g_state.gesture_pending_deferred_double_count = 0;
                g_state.gesture_double_tap_waiting = false;
                ctx->pending_double_count = 0;
                ctx->deferred_double_count = 0;
                g_state.gesture_main_ptr_id = -1;
                deactivate_finger(f);
            }
        }

        // ---- Deferred single-tap (full Path 1-4 + cleanup, matching original handle_tap_up_impl) ----
        // Must use S/Sd bindings (not select_gesture_slot which may return D/Dd)
        if (f->single_tap_deferred && time_ms >= f->single_tap_deferred_time) {
            LOGD("TICK ptr=%d DEFERRED_TAP_FIRE deferred_d=%d post_dt=%d dt_consumed=%d",
                f->ptr_id, ctx->deferred_double_count, ctx->post_double_tap_drag, ctx->dt_consumed);
            f->single_tap_deferred = false;

            // Resolve S/Sd bindings for Path 3/4 (deferred D in Path 1 is from ctx)
            const GesturePairSlot* ds_slot = f->is_second_finger
                ? &GESTURE_SLOTS[3] : &GESTURE_SLOTS[0];
            const TouchBinding* dsx  = NULL; int dsx_cnt  = 0;
            const TouchBinding* dsxd = NULL; int dsxd_cnt = 0;
            resolve_binding_slot(f, ds_slot, &dsx, &dsx_cnt, &dsxd, &dsxd_cnt);

            // Path 1: deferred D from DT confirm
            if (ctx->deferred_double_count > 0) {
                LOGD("TICK ptr=%d DEFERRED_PATH1_D cnt=%d", f->ptr_id, ctx->deferred_double_count);
                if (!g_state.gesture_is_action_held)
                    execute_actions(result, ctx->deferred_double, ctx->deferred_double_count);
                if (g_state.gesture_is_action_held)
                    release_held_actions(result);
                ctx->post_double_tap_drag = false;
                ctx->dt_consumed = true;
                g_state.gesture_double_tap_consumed = true;
                f->state = GESTURE_STATE_IDLE;
                goto deferred_tap_done;
            }

            // Path 2: post-DT drag cleanup
            if (ctx->post_double_tap_drag) {
                LOGD("TICK ptr=%d DEFERRED_PATH2_POST_DT", f->ptr_id);
                ctx->post_double_tap_drag = false;
                ctx->dt_consumed = true;
                g_state.gesture_double_tap_consumed = true;
                f->state = GESTURE_STATE_IDLE;
                goto deferred_tap_done;
            }

            // Path 3: DT consumed (3+ tap) — fire S binding
            if (ctx->dt_consumed) {
                ctx->dt_consumed = false;
                g_state.gesture_double_tap_consumed = false;
                LOGD("TICK ptr=%d DEFERRED_PATH3_DT_CONSUMED dsx_cnt=%d", f->ptr_id, dsx_cnt);
                if (!g_state.gesture_is_action_held && dsx_cnt > 0)
                    execute_actions(result, dsx, dsx_cnt);
                f->state = GESTURE_STATE_IDLE;
                goto deferred_tap_done;
            }

            // Path 4: Normal first tap-up
            if (f->cached_has_active_double_tap) {
                LOGD("TICK ptr=%d DEFERRED_PATH4_DT_ENTRY", f->ptr_id);
                enter_double_tap_waiting(f, time_ms,
                    dsx, dsx_cnt, f->bindings.double_tap, f->bindings.double_tap_count);
                return;
            } else if (f->cached_has_active_double_tap_drag) {
                LOGD("TICK ptr=%d DEFERRED_PATH4_DTD_ENTRY", f->ptr_id);
                if (!g_state.gesture_is_action_held && dsx_cnt > 0)
                    execute_actions(result, dsx, dsx_cnt);
                enter_double_tap_waiting(f, time_ms, NULL, 0, NULL, 0);
                return;
            } else {
                LOGD("TICK ptr=%d DEFERRED_PATH4_SINGLE dsx_cnt=%d", f->ptr_id, dsx_cnt);
                if (!g_state.gesture_is_action_held && dsx_cnt > 0)
                    execute_actions(result, dsx, dsx_cnt);
                f->state = GESTURE_STATE_IDLE;
            }

        deferred_tap_done:
            LOGD("TICK ptr=%d DEFERRED_DONE", f->ptr_id);
            release_held_actions(result);
            f->state = GESTURE_STATE_IDLE;
            g_state.gesture_main_ptr_id = -1;
            deactivate_finger(f);
        }
    }
}
