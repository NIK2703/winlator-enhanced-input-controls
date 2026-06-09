#include "../touch_processor_internal.h"


// Forward declaration for deferred tap-up helper (defined after gesture_tick)
static void handle_tap_up_impl(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms);

// ---- helpers ----
// Unified drag binding resolution for ALL gesture types.
// Priority chain (first-match wins):
//   1. xd (drag variant — Sd/Dd/Ld)
//   2. x  (non-drag variant — S/D/L) — only if press_on_drag
//   3. fb (competition fallback — S)  — only if use_fallback
static inline bool resolve_drag_binding(
    const TouchBinding* xd, int xd_count,
    const TouchBinding* x,  int x_count,
    const TouchBinding* fb, int fb_count,
    bool press_on_drag,
    bool use_fallback,
    const TouchBinding** out_binding,
    int* out_count)
{
    if (xd_count > 0) { *out_binding = xd; *out_count = xd_count; return true; }
    if (press_on_drag && x_count > 0) { *out_binding = x; *out_count = x_count; return true; }
    if (use_fallback && fb_count > 0) { *out_binding = fb; *out_count = fb_count; return true; }
    return false;
}

// Minimal drag start — no binding change, just state transition.
static inline void start_drag_no_binding(TouchFinger* f) {
    on_drag_start(f);
    f->state = GESTURE_STATE_DRAGGING;
}

static inline void mark_second_finger_dragging(TouchFinger* exclude) {
    for (int _si = 0; _si < MAX_FINGERS; _si++) {
        TouchFinger* _sf = &g_state.fingers[_si];
        if (_sf->active && _sf->ptr_id == g_state.gesture_second_ptr_id && _sf != exclude) {
            start_drag_no_binding(_sf);
            break;
        }
    }
}

static inline GesturePairPlan resolve_long_press_pair(const TouchFinger* f) {
    return gesture_decide_branch(gesture_branch_params(
        f->cached_has_active_long_press,
        f->cached_has_active_long_press_drag,
        false, false,
        g_state.cfg.is_ts,
        f->is_second_finger,
        0, false
    ));
}

static bool bindings_equal(const TouchBinding* a, int a_count, const TouchBinding* b, int b_count) {
    if (a_count != b_count) return false;
    for (int i = 0; i < a_count; i++)
        if (a[i].type != b[i].type || a[i].keycode != b[i].keycode)
            return false;
    return true;
}

// Full drag start with binding: compare against currently-held actions,
// release old if different, hold new, then transition.
static inline void start_drag_with_binding(TouchFinger* f,
    TouchActionResult* restrict result,
    const TouchBinding* drag_binding, int drag_count)
{
    bool same_as_held = g_state.gesture_is_action_held;
    if (same_as_held && drag_binding && g_state.gesture_held_count > 0) {
        same_as_held = bindings_equal(drag_binding, drag_count,
                        g_state.gesture_held_actions, g_state.gesture_held_count);
    }
    if (!same_as_held) {
        // Save auto-repeat bindings before releasing held actions,
        // so their auto-repeat continues through the drag transition.
        TouchBinding saved_ar[MAX_HELD_ACTIONS];
        int saved_ar_count = 0;
        for (int i = 0; i < g_state.gesture_held_count; i++) {
            if (g_state.gesture_held_actions[i].auto_repeat) {
                saved_ar[saved_ar_count++] = g_state.gesture_held_actions[i];
            }
        }
        release_held_actions(result);
        // Restore auto-repeat bindings so tick continues bursting them
        for (int i = 0; i < saved_ar_count && g_state.gesture_held_count < MAX_HELD_ACTIONS; i++) {
            g_state.gesture_held_actions[g_state.gesture_held_count++] = saved_ar[i];
        }
        if (saved_ar_count > 0)
            g_state.gesture_is_action_held = true;
        execute_actions_hold(result, drag_binding, drag_count);
    }
    start_drag_no_binding(f);
}

void on_drag_start(TouchFinger* f) {
    f->single_tap_hold_delay_ms = 0;
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
    g_state.gesture_pending_deferred_double_count = 0;
    TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
    if (mf) mf->state = GESTURE_STATE_IDLE;
}

// ---- check_start_drag ----
void check_start_drag(TouchFinger* f, float dx, float dy, TouchActionResult* restrict result) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "check_start_drag ptr=%d state=%d dx=%.1f dy=%.1f", f->ptr_id, f->state, dx, dy);
    if (__builtin_expect(f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING, 0)) {
        return;
    }
    if (fabsf(dx) <= g_state.cfg.drag_threshold_px && fabsf(dy) <= g_state.cfg.drag_threshold_px) return;

    __android_log_print(ANDROID_LOG_INFO, "Winlator_Gesture", "check_start_drag: post_dtd=%d defer_dbl=%d second=%d held=%d caps_drag=%d",
        g_state.gesture_post_double_tap_drag,
        g_state.gesture_pending_deferred_double_count,
        g_state.gesture_second_active,
        g_state.gesture_is_action_held,
        g_state.cfg.caps_has_drag_bindings);

    g_state.gesture_handler_active = false;
    const bool is_tp = g_state.cfg.is_tp;
    const bool is_ts = g_state.cfg.is_ts;
    const bool caps_has_drag_bindings = g_state.cfg.caps_has_drag_bindings;
    const FingerBindings* fb = &f->bindings;
    const TouchBinding* drag_binding = NULL;
    int drag_count = 0;

    if (f->state == GESTURE_STATE_LONG_PRESSING) {
        // Early exit: no L or Ld bindings in current mode — shouldn't reach here, but guard anyway
        if (!current_mode_has_gesture(GESTURE_LONG_PRESS) && !current_mode_has_gesture(GESTURE_LONG_PRESS_DRAG)) {
            start_drag_no_binding(f);
            return;
        }
        gesture_clear_pending_long_press();
        if (!resolve_drag_binding(
                fb->long_press_drag, fb->long_press_drag_count,
                NULL, 0, NULL, 0,
                false, false,
                &drag_binding, &drag_count)) {
            start_drag_no_binding(f);
            return;
        }
    } else if (g_state.gesture_post_double_tap_drag) {
        // TP mode two-finger: only the second finger initiates post-DT drag.
        // The main finger's movement is for cursor positioning and should
        // not consume gesture_post_double_tap_drag or resolve Dd2 for the
        // wrong finger. Single-finger: main finger is the only finger, allow it.
        if (g_state.cfg.is_tp && g_state.gesture_second_active && !f->is_second_finger)
            return;
        // Early exit: no D or Dd bindings (including second-finger variants)
        if (!current_mode_has_gesture(GESTURE_DOUBLE_TAP) && !current_mode_has_gesture(GESTURE_DOUBLE_TAP_DRAG)
            && !current_mode_has_gesture(GESTURE_DOUBLE_2ND) && !current_mode_has_gesture(GESTURE_DOUBLE_DRAG_2ND)) {
            g_state.gesture_post_double_tap_drag = false;
            return;
        }
        const FingerBindings* dt_fb = &f->bindings;
        if (g_state.gesture_second_active && !f->is_second_finger) {
            TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
            if (sf) dt_fb = &sf->bindings;
        }
        if (!resolve_drag_binding(
                dt_fb->double_tap_drag, dt_fb->double_tap_drag_count,
                dt_fb->double_tap, dt_fb->double_tap_count,
                dt_fb->single_tap, dt_fb->single_tap_count,
                true, true,
                &drag_binding, &drag_count)) {
            g_state.gesture_post_double_tap_drag = false;
            return;
        }
        if (drag_binding == dt_fb->double_tap)
            g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_post_double_tap_drag = false;
        if (g_state.gesture_second_active) {
            mark_second_finger_dragging(f);
        }
    } else if (g_state.gesture_pending_deferred_double_count > 0) {
        // DT confirmed without Dd — prevent Sd from firing on drag.
        // Don't call start_drag_no_binding (it clears pending_deferred_double_count
        // via on_drag_start → gesture_clear_deferred_tap). The deferred D must
        // survive so PATH 1 fires on finger-up.
        f->single_tap_hold_delay_ms = 0;
        f->state = GESTURE_STATE_DRAGGING;
        return;
    } else if (g_state.gesture_second_active && f->is_second_finger) {
        // Skip resolve_drag_binding if no second-finger drag binding at all
        if (!current_mode_has_gesture(GESTURE_SINGLE_DRAG_2ND) && !current_mode_has_gesture(GESTURE_DOUBLE_DRAG_2ND)
            && !current_mode_has_gesture(GESTURE_SINGLE_2ND) && !current_mode_has_gesture(GESTURE_DOUBLE_2ND)) {
            start_drag_no_binding(f);
            return;
        }
        // Unified second-finger drag resolution (TS/TP).
        // When S2 is held without D2, fall through to resolve Sd2 (TS).
        // If no single-tap-drag exists, resolve_drag_binding simply returns false.
        bool has_single_and_dt = f->cached_has_active_single_tap
            && (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag);
        // Held S2/D2: transition without re-fire if Sd2 unavailable or D2 present.
        if (g_state.gesture_is_action_held
            && (!fb->single_tap_drag_count || f->cached_has_active_double_tap)) {
            g_state.gesture_pending_double_count = 0;
            start_drag_no_binding(f);
            return;
        }
        // TP mode zeroes single_tap_drag on the finger; read from config directly.
        const TouchBinding* sd = f->bindings.single_tap_drag;
        int sd_count = f->bindings.single_tap_drag_count;
        if (is_tp) {
            sd = g_state.cfg.tp[GESTURE_SINGLE_DRAG_2ND].arr;
            sd_count = g_state.cfg.tp[GESTURE_SINGLE_DRAG_2ND].count;
        }
        if (!resolve_drag_binding(
                sd, sd_count,
                fb->single_tap, fb->single_tap_count, NULL, 0,
                has_single_and_dt, false,
                &drag_binding, &drag_count))
            return;
    } else if (!g_state.gesture_second_active) {
        // Early exit: nothing to resolve for main finger
        if (!current_mode_has_gesture(GESTURE_SINGLE_TAP) && !current_mode_has_gesture(GESTURE_SINGLE_TAP_DRAG)) {
            start_drag_no_binding(f);
            return;
        }
        // Main-finger drag (handles both TS and TP first-finger).
        if (is_tp && !caps_has_drag_bindings) return;
        bool press_on_drag = f->cached_has_active_single_tap
            && (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag
                || f->cached_has_long_press_timer)
            && !g_state.gesture_is_action_held;
        if (!resolve_drag_binding(
                fb->single_tap_drag, fb->single_tap_drag_count,
                fb->single_tap, fb->single_tap_count, NULL, 0,
                is_ts && press_on_drag,
                false,
                &drag_binding, &drag_count))
            return;
    } else {
        return;
    }

    start_drag_with_binding(f, result, drag_binding, drag_count);
}

// ---- gesture_tick ----
void gesture_tick(uint64_t time_ms, TouchActionResult* restrict result) {
    if (!g_state.cfg.caps_has_gesture_bindings) return;

    TouchFinger* restrict fingers = g_state.fingers;
    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &fingers[i];
        if (__builtin_expect(!f->active, 1)) continue;
        if (f->state != GESTURE_STATE_TAP_WAITING) continue;

        // LP timer
        if (__builtin_expect(f->cached_has_long_press_timer, 0)) {
            // LP fires only if finger never moved beyond drag_threshold during
            // the entire timeout, AND no other action (e.g. DT) is currently held.
            bool cancel_lp = f->cached_has_moved_beyond_threshold
                          || g_state.gesture_is_action_held;

            if (!cancel_lp && time_ms - f->down_time_ms >= g_state.cfg.long_press_timeout_ms) {
                g_state.gesture_handler_active = false;
                gesture_clear_second_finger_state();

                GesturePairPlan lp_plan = resolve_long_press_pair(f);

                if (lp_plan.pulse_on_up) {
                    if (lp_plan.drag_available) {
                        // L with Ld: defer L for drag (fire on finger-up, Ld on drag)
                        g_state.gesture_pending_deferred_long_press_count = f->bindings.long_press_count;
                        for (int _li = 0; _li < f->bindings.long_press_count && _li < 8; _li++)
                            g_state.gesture_pending_deferred_long_press[_li] = f->bindings.long_press[_li];
                    } else {
                        // L without Ld: execute immediately (pulse on timer tick)
                        execute_actions(result, f->bindings.long_press, f->bindings.long_press_count);
                        if (f->cached_has_active_single_tap && !g_state.gesture_is_action_held)
                            execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                        f->cached_has_long_press_timer = false;
                        f->cached_has_active_long_press = false;
                    }
                    f->single_tap_hold_delay_ms = 0;
                    f->state = GESTURE_STATE_LONG_PRESSING;
                } else {
                    execute_actions_hold(result, f->bindings.long_press, f->bindings.long_press_count);
                    f->single_tap_hold_delay_ms = 0;
                    f->state = GESTURE_STATE_LONG_PRESSING;
                }
                if (g_state.cfg.gesture_long_press_haptic > 0)
                    add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
            }
        }

        // S hold timer (TS finger-down hold)
        if (__builtin_expect(f->single_tap_hold_delay_ms > 0, 0)
            && time_ms - f->single_tap_hold_timer >= f->single_tap_hold_delay_ms) {
            // Skip if no single-tap bindings in current mode
            if (!current_mode_has_gesture(GESTURE_SINGLE_TAP) && !current_mode_has_gesture(GESTURE_SINGLE_2ND)) {
                f->single_tap_hold_delay_ms = 0;
                continue;
            }
            f->single_tap_hold_delay_ms = 0;
            if (!g_state.gesture_is_action_held && !f->cached_has_moved_beyond_threshold)
                execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
            g_state.gesture_deferred_tap_count = 0;
            g_state.gesture_pending_double_count = 0;
            // R1 fix: do NOT clear pending_deferred_double_count here.
            // It must survive S hold timer to fire on finger-up.
            // When the delayed hold fires for a second finger, clear SDTW state
            if (f->is_second_finger) {
                g_state.second_tap_fallback_count = 0;
                g_state.second_double_tap_waiting = false;
            }
        }
    }

    // caps gate
    if (!g_state.cfg.caps_has_double_tap) {
        g_state.second_double_tap_waiting = false;
        g_state.gesture_double_tap_waiting = false;
        g_state.gesture_pending_deferred_double_count = 0;
    }

    // SDTW timeout — second-finger double-tap expired, fire ST fallback
    {
        uint32_t sdtw_mask = GESTURE_MASK(GESTURE_DOUBLE_2ND) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);
        if ((g_state.cfg.caps_mode_mask & sdtw_mask) && g_state.second_double_tap_waiting
            && time_ms - g_state.second_tap_fallback_time >= g_state.cfg.double_tap_timeout_ms) {
            g_state.second_double_tap_waiting = false;
            if (g_state.second_tap_fallback_count > 0) {
                execute_actions(result, g_state.second_tap_fallback, g_state.second_tap_fallback_count);
                g_state.second_tap_fallback_count = 0;
            }
            TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
            if (mf && mf->state != GESTURE_STATE_DRAGGING)
                mf->state = GESTURE_STATE_IDLE;
        }
    }

    // DT timeout — first-finger double-tap expired, fire deferred S or D
    {
        if (g_state.cfg.caps_has_double_tap && __builtin_expect(g_state.gesture_double_tap_waiting, 0)
            && time_ms - g_state.gesture_double_tap_start_time >= g_state.cfg.double_tap_timeout_ms) {
            __android_log_print(ANDROID_LOG_INFO, "Winlator_Gesture", "DT_TIMEOUT: firing deferred S count=%d", g_state.gesture_deferred_tap_count);
            g_state.gesture_double_tap_waiting = false;
            if (g_state.gesture_deferred_tap_count > 0)
                execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
            gesture_clear_deferred_tap();
            TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
            if (mf) mf->state = GESTURE_STATE_IDLE;
        }
    }

    // Deferred single-tap (replaces nanosleep)
    if (__builtin_expect(g_state.cfg.single_tap_delay_ms > 0, 0)) {
        for (int i = 0; i < MAX_FINGERS; i++) {
            TouchFinger* f = &fingers[i];
            if (!f->active || !f->single_tap_deferred) {
                if (f->single_tap_deferred && !f->active)
                    TP_LOG(ANDROID_LOG_WARN, LOG_TAG, "gesture_tick: deferred tap SKIPPED finger[%d] active=%d ptr=%d",
                        i, f->active, f->ptr_id);
                continue;
            }
            if (time_ms >= f->single_tap_deferred_time) {
                TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "gesture_tick: processing deferred tap finger[%d] ptr=%d",
                    i, f->ptr_id);
                f->single_tap_deferred = false;
                handle_tap_up_impl(f, result, time_ms);
                tap_up_cleanup(f, result);
            }
        }
    }
}

// ---- Unified tap execution helpers ----

// Execute S on finger-down: start hold timer, defer to drag/up, or execute now.
void execute_tap_on_finger_down(TouchFinger* f, TouchActionResult* restrict result,
    uint64_t time_ms, GesturePairPlan plan, bool force_hold)
{
    if (plan.hold_delay_ms > 0) {
        f->single_tap_hold_delay_ms = plan.hold_delay_ms;
        f->single_tap_hold_timer = time_ms;
    } else if (plan.pulse_on_up || plan.press_on_drag) {
        // deferred to check_start_drag / handle_tap_up
    } else {
        if (force_hold)
            execute_actions_hold(result, f->bindings.single_tap, f->bindings.single_tap_count);
        else
            execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
    }
}

// Fire S on finger-up if no action is held and S bindings exist.
static inline void fire_single_tap_on_up(TouchFinger* f, TouchActionResult* restrict result) {
    if (!g_state.gesture_is_action_held && f->bindings.single_tap_count > 0)
        execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
}

static inline void fire_single_and_idle(TouchFinger* f, TouchActionResult* restrict result) {
    fire_single_tap_on_up(f, result);
    f->state = GESTURE_STATE_IDLE;
}

// Enter DT_WAITING state, optionally saving deferred bindings.
static inline void enter_double_tap_waiting(TouchFinger* f, uint64_t time_ms,
    const TouchBinding* deferred_single, int deferred_single_count,
    const TouchBinding* deferred_double, int deferred_double_count)
{
    gesture_clear_deferred_tap();
    if (deferred_single && deferred_single_count > 0) {
        for (int _i = 0; _i < deferred_single_count && _i < 8; _i++)
            g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = deferred_single[_i];
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
    f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
}

// Confirm double-tap: execute D now or defer to finger-up.
// Returns true if D was executed now, false if deferred.
bool confirm_double_tap(TouchActionResult* restrict result,
    GesturePairPlan d_plan,
    const TouchBinding* src, int src_count,
    TouchBinding* dst, int* dst_count, int dst_max,
    bool has_dt_drag,
    bool* out_post_dtd)
{
    if (d_plan.pulse_on_up) {
        *dst_count = 0;
        for (int _i = 0; _i < src_count && _i < dst_max; _i++)
            dst[_i] = src[_i];
        *dst_count = src_count;
        *out_post_dtd = true;
        return false; // deferred
    } else {
        execute_actions_hold(result, src, src_count);
        // Still store D bindings in deferred so PATH 1 fires on finger-up
        // (prevents re-entering DT waiting and allows check_start_drag to skip Sd).
        *dst_count = 0;
        for (int _i = 0; _i < src_count && _i < dst_max; _i++)
            dst[_i] = src[_i];
        *dst_count = src_count;
        *out_post_dtd = has_dt_drag;
        return true; // executed now
    }
}

// ---- handle_tap_up_impl (non-deferred body) ----
static void handle_tap_up_impl(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms) {
    const FingerBindings* fb = &f->bindings;

    // Finger bindings already set by setup_second_finger_bindings (second finger)
    // or setup_main_finger_bindings (main finger) — no TS/TP switch needed.
    int active_single_count = fb->single_tap_count;
    const TouchBinding* active_single = fb->single_tap;
    int active_double_count = fb->double_tap_count;
    const TouchBinding* active_double = fb->double_tap;
    bool active_has_double_tap = f->cached_has_active_double_tap;
    bool active_has_double_tap_drag = f->cached_has_active_double_tap_drag;

    // Path 1: deferred D from DT confirm.
    // When D was executed on finger-down (pulse_on_up=false, no Dd), it's already
    // held — skip re-executing so the action isn't duplicated.
    // Must clear the count either way so stale values don't trigger PATH 1
    // on the next cycle's first tap-up.
    if (g_state.gesture_pending_deferred_double_count > 0) {
        __android_log_print(ANDROID_LOG_INFO, "Winlator_Gesture", "tap_up_impl PATH1: defer_dbl=%d held=%d",
            g_state.gesture_pending_deferred_double_count, g_state.gesture_is_action_held);
        if (!g_state.gesture_is_action_held)
            execute_deferred_double(result);
        else
            g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true;
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Path 2: post_double_tap_drag cleanup
    if (g_state.gesture_post_double_tap_drag) {
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true;
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Path 3: double_tap_consumed (3+ tap)
    if (g_state.gesture_double_tap_consumed) {
        g_state.gesture_double_tap_consumed = false;
        fire_single_and_idle(f, result);
        return;
    }

    // Path 4: Normal first tap-up
    bool has_dt = active_has_double_tap;

    __android_log_print(ANDROID_LOG_INFO, "Winlator_Gesture", "tap_up_impl PATH4: has_dt=%d has_dtd=%d S=%d D=%d state=%d consumed=%d",
        has_dt, active_has_double_tap_drag,
        active_single_count, active_double_count,
        f->state, g_state.gesture_double_tap_consumed);

    if (has_dt) {
        enter_double_tap_waiting(f, time_ms,
            active_single, active_single_count,
            active_double, active_double_count);
    } else if (active_has_double_tap_drag) {
        fire_single_tap_on_up(f, result);
        enter_double_tap_waiting(f, time_ms, NULL, 0, NULL, 0);
    } else {
        fire_single_and_idle(f, result);
    }
}

// ---- handle_tap_up ----
void handle_tap_up(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms) {
    if (!current_mode_has_gestures() && !__builtin_expect(g_state.gesture_double_tap_waiting, 0) && !g_state.gesture_post_double_tap_drag) {
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    if (g_state.cfg.single_tap_delay_ms > 0) {
        f->single_tap_deferred = true;
        f->single_tap_deferred_time = time_ms + g_state.cfg.single_tap_delay_ms;
        return;
    }

    handle_tap_up_impl(f, result, time_ms);
}
