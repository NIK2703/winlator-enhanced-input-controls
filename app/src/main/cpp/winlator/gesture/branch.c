#include <android/log.h>
#include <inttypes.h>
#include "../touch_processor_internal.h"
#include "types.h"
#include "branch.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Winlator_Gesture", __VA_ARGS__)

// =========================================================================
// INLINE HELPERS (branch.c local, need g_state)
// =========================================================================

static inline int finger_index(const TouchFinger* f) {
    return (int)(f - g_state.fingers);
}

static inline void reset_finger_ctx(GestureFingerCtx* ctx, bool is_second) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->is_second_finger = is_second;
}

static inline bool finger_can_go_idle(const TouchFinger* mf) {
    if (mf->state == GESTURE_STATE_DRAGGING) return false;
    if (mf->state == GESTURE_STATE_TAP_WAITING
        && g_ctx[finger_index(mf)].deferred_double_count > 0)
        return false;
    return true;
}

static inline void reset_single_tap_hold_timer(TouchFinger* f) {
    f->single_tap_hold_delay_ms = 0;
    f->single_tap_hold_timer = 0;
}

// =========================================================================
// RESOLVED BINDINGS — stack-allocated result of resolve_binding_slot
// =========================================================================

typedef struct {
    const TouchBinding* non_drag;
    int                  non_drag_count;
    const TouchBinding* drag;
    int                  drag_count;
} ResolvedBindings;

static inline ResolvedBindings resolve_slot(const TouchFinger* f,
                                            const GesturePairSlot* slot)
{
    ResolvedBindings rb = {NULL, 0, NULL, 0};
    resolve_binding_slot(f, slot, &rb.non_drag, &rb.non_drag_count,
                         &rb.drag, &rb.drag_count);
    return rb;
}

// =========================================================================
// FORWARD DECLARATIONS
// =========================================================================
static void handle_down(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy);
static void handle_move(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy);
static void handle_up(TouchFinger* f, GestureFingerCtx* ctx,
                      TouchActionResult* restrict result, uint64_t time_ms,
                      float dx, float dy);
static void handle_tick(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy);
static void up_handle_tp_early_exit(TouchFinger* f, GestureFingerCtx* ctx,
                                    TouchActionResult* restrict result, uint64_t time_ms,
                                    const TouchBinding* x, int x_cnt);

// =========================================================================
// DISPATCH TABLE
// =========================================================================
typedef void (*GestureEventHandler)(TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* restrict result, uint64_t time_ms,
    float dx, float dy);

static const GestureEventHandler handlers[GESTURE_EVENT_COUNT] = {
    [GESTURE_EVENT_DOWN] = handle_down,
    [GESTURE_EVENT_MOVE] = handle_move,
    [GESTURE_EVENT_UP]   = handle_up,
    [GESTURE_EVENT_TICK] = handle_tick,
};

// =========================================================================
// SHARED HELPERS (static, used by multiple handlers)
// =========================================================================

// Execute bindings only if no action is currently held.
static inline void execute_if_not_held(TouchActionResult* restrict result,
                                        const TouchBinding* bindings, int count)
{
    if (!g_state.gesture_is_action_held && count > 0)
        execute_actions(result, bindings, count);
}

static void cleanup_main_finger(TouchFinger* f, GestureFingerCtx* ctx) {
    LOGD("UP ptr=%d CLEANUP_MAIN", f->ptr_id);
    f->state = GESTURE_STATE_IDLE;
    ctx->post_double_tap_drag = false;
    ctx->pending_long_press_count = 0;
    ctx->deferred_double_count = 0;
    ctx->dt_consumed = false;
    gesture_clear_deferred_tap();
    g_state.gesture_pending_deferred_long_press_count = 0;
    g_state.gesture_double_tap_waiting = false;
    g_state.gesture_post_double_tap_drag = false;
    g_state.gesture_main_ptr_id = INVALID_PTR_ID;
    g_state.gesture_double_tap_consumed = false;
    if (!g_state.gesture_second_active)
        g_state.gesture_deferred_second_finger_tap = false;
    deactivate_finger(f);
}

typedef enum {
    TAP_PATH_HANDLED,
    TAP_PATH_ENTER_DT,
    TAP_PATH_SINGLE,
} TapPathResult;

// Mark DT consumed, go idle, return HANDLED. Used by execute_tap_path paths 1 & 2.
static inline TapPathResult mark_dt_consumed_idle(TouchFinger* f, GestureFingerCtx* ctx)
{
    ctx->post_double_tap_drag = false;
    ctx->dt_consumed = true;
    g_state.gesture_double_tap_consumed = true;
    f->state = GESTURE_STATE_IDLE;
    return TAP_PATH_HANDLED;
}

static TapPathResult execute_tap_path(
    TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* restrict result, uint64_t time_ms,
    const TouchBinding* x, int x_cnt,
    const TouchBinding* x2, int x2_cnt,
    bool is_second_finger)
{
    // Path 1: deferred D from DT confirm
    if (ctx->deferred_double_count > 0) {
        if (!g_state.gesture_is_action_held)
            execute_actions(result, ctx->deferred_double, ctx->deferred_double_count);
        else
            release_held_actions(result);
        return mark_dt_consumed_idle(f, ctx);
    }

    // Path 2: post-DT drag cleanup
    if (ctx->post_double_tap_drag)
        return mark_dt_consumed_idle(f, ctx);

    // Path 3: DT consumed (3+ tap) — fire S binding
    if (ctx->dt_consumed) {
        ctx->dt_consumed = false;
        g_state.gesture_double_tap_consumed = false;
        execute_if_not_held(result, x, x_cnt);
        f->state = GESTURE_STATE_IDLE;
        return TAP_PATH_HANDLED;
    }

    // Path 4: Normal first tap-up
    if (f->cached_has_active_double_tap) {
        if (is_second_finger)
            enter_sdtw(f, time_ms);
        else
            enter_double_tap_waiting(f, time_ms,
                x, x_cnt,
                f->bindings.double_tap, f->bindings.double_tap_count);
        return TAP_PATH_ENTER_DT;
    }

    if (f->cached_has_active_double_tap_drag) {
        execute_if_not_held(result, x, x_cnt);
        if (is_second_finger)
            enter_sdtw(f, time_ms);
        else
            enter_double_tap_waiting(f, time_ms, NULL, 0, NULL, 0);
        return TAP_PATH_ENTER_DT;
    }

    // Path 4 else: single tap
    if (is_second_finger) {
        if (x2_cnt > 0)
            execute_actions(result, x2, x2_cnt);
    } else {
        execute_if_not_held(result, x, x_cnt);
    }
    f->state = GESTURE_STATE_IDLE;
    return TAP_PATH_SINGLE;
}

// =========================================================================
// TICK HELPERS
// =========================================================================

static void tick_lp_timer(TouchFinger* f, GestureFingerCtx* ctx,
                          TouchActionResult* restrict result, uint64_t time_ms)
{
    if (!f->cached_has_long_press_timer
        || f->cached_has_moved_beyond_threshold
        || g_state.gesture_is_action_held
        || f->gesture_activated_in_touch
        || time_ms - f->down_time_ms < (uint64_t)g_state.cfg.long_press_timeout_ms)
        return;

    LOGD("TICK ptr=%d LP_TIMER_FIRE elapsed=%" PRIu64,
        f->ptr_id, time_ms - f->down_time_ms);
    gesture_clear_second_finger_state();
    g_state.second_tap_fallback_count = 0;

    const GesturePairSlot* lp_slot = SLOT_L();
    ResolvedBindings rb = resolve_slot(f, lp_slot);
    GesturePairPlan lp_plan = resolve_gesture_pair(f, lp_slot);

    if (lp_plan.pulse_on_up) {
        if (lp_plan.drag_available) {
            LOGD("TICK ptr=%d LP_PULSE_ON_UP+DRAG_AVAIL -> pending lx_cnt=%d", f->ptr_id, rb.non_drag_count);
            copy_bindings_bounded(rb.non_drag, rb.non_drag_count,
                ctx->pending_long_press, &ctx->pending_long_press_count, FALLBACK_MAX);
            copy_bindings_bounded(rb.non_drag, rb.non_drag_count,
                g_state.gesture_pending_deferred_long_press,
                &g_state.gesture_pending_deferred_long_press_count, FALLBACK_MAX);
        } else {
            LOGD("TICK ptr=%d LP_PULSE_ON_UP+NO_DRAG -> exec lx_cnt=%d", f->ptr_id, rb.non_drag_count);
            execute_actions(result, rb.non_drag, rb.non_drag_count);
            f->cached_has_active_single_tap = false;
        }
        f->cached_has_long_press_timer = false;
    } else {
        LOGD("TICK ptr=%d LP_HOLD lx_cnt=%d", f->ptr_id, rb.non_drag_count);
        execute_actions_hold(result, rb.non_drag, rb.non_drag_count);
        f->cached_has_long_press_timer = false;
    }
    f->single_tap_hold_delay_ms = 0;
    f->state = GESTURE_STATE_LONG_PRESSING;
    if (g_state.cfg.gesture_long_press_haptic > 0)
        add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
}

static void tick_s_hold_timer(TouchFinger* f, GestureFingerCtx* ctx,
                              TouchActionResult* restrict result, uint64_t time_ms,
                              const GesturePairSlot* slot)
{
    ResolvedBindings rb = resolve_slot(f, slot);

    if (f->single_tap_hold_delay_ms <= 0
        || time_ms - f->single_tap_hold_timer < (uint64_t)f->single_tap_hold_delay_ms
        || g_state.gesture_is_action_held
        || f->cached_has_moved_beyond_threshold
        || rb.non_drag_count == 0)
        return;

    LOGD("TICK ptr=%d S_HOLD_FIRE delay=%d exec=%d sx_cnt=%d",
        f->ptr_id, f->single_tap_hold_delay_ms, slot->is_second_finger, rb.non_drag_count);
    f->single_tap_hold_delay_ms = 0;
    if (slot->is_second_finger)
        execute_actions(result, rb.non_drag, rb.non_drag_count);
    else
        execute_actions_hold(result, rb.non_drag, rb.non_drag_count);
    ctx->pending_double_count = 0;
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    if (slot->is_second_finger) {
        g_state.second_tap_fallback_count = 0;
        gesture_clear_second_finger_state();
    }
}

static void tick_dt_timeout(TouchFinger* f, GestureFingerCtx* ctx,
                            TouchActionResult* restrict result, uint64_t time_ms,
                            const GesturePairSlot* slot)
{
    if (!ctx->dt_waiting
        || time_ms - ctx->dt_wait_start_time < (uint64_t)g_state.cfg.double_tap_timeout_ms)
        return;

    LOGD("TICK ptr=%d DT_TIMEOUT uses_fb=%d fallback_cnt=%d deferred_cnt=%d",
        f->ptr_id, slot->is_second_finger, ctx->fallback_count,
        g_state.gesture_deferred_tap_count);
    ctx->dt_waiting = false;

    if (slot->is_second_finger) {
        LOGD("TICK ptr=%d DT_TIMEOUT_SDTW fallback_cnt=%d", f->ptr_id, ctx->fallback_count);
        if (ctx->fallback_count > 0)
            execute_actions(result, ctx->fallback, ctx->fallback_count);
        ctx->fallback_count = 0;
        g_state.gesture_second_active = false;
        g_state.gesture_post_double_tap_drag = false;
        g_state.second_double_tap_waiting = false;
        g_state.pending_second_double_count = 0;
        g_state.second_tap_fallback_count = 0;
        TouchFinger* mf = find_finger(g_state.gesture_main_ptr_id);
        if (mf && finger_can_go_idle(mf))
            mf->state = GESTURE_STATE_IDLE;
        deactivate_finger(f);
    } else {
        if (g_state.gesture_deferred_tap_count > 0) {
            LOGD("TICK ptr=%d DT_TIMEOUT_S_FALLBACK cnt=%d",
                f->ptr_id, g_state.gesture_deferred_tap_count);
            execute_actions(result, g_state.gesture_deferred_tap,
                g_state.gesture_deferred_tap_count);
        }
        gesture_clear_deferred_tap();
        g_state.gesture_double_tap_waiting = false;
        ctx->pending_double_count = 0;
        ctx->deferred_double_count = 0;
        g_state.gesture_main_ptr_id = INVALID_PTR_ID;
        deactivate_finger(f);
    }
}

static void tick_deferred_single_tap(TouchFinger* f, GestureFingerCtx* ctx,
                                     TouchActionResult* restrict result, uint64_t time_ms)
{
    if (!f->single_tap_deferred || time_ms < f->single_tap_deferred_time)
        return;

    LOGD("TICK ptr=%d DEFERRED_TAP_FIRE deferred_d=%d post_dt=%d dt_consumed=%d",
        f->ptr_id, ctx->deferred_double_count, ctx->post_double_tap_drag, ctx->dt_consumed);
    f->single_tap_deferred = false;

    const GesturePairSlot* ds_slot = select_s_slot(f);
    ResolvedBindings rb = resolve_slot(f, ds_slot);

    TapPathResult path = execute_tap_path(f, ctx, result, time_ms,
        rb.non_drag, rb.non_drag_count, rb.non_drag, rb.non_drag_count,
        ds_slot->is_second_finger);

    if (path == TAP_PATH_ENTER_DT) return;

    LOGD("TICK ptr=%d DEFERRED_DONE", f->ptr_id);
    release_held_actions(result);
    f->state = GESTURE_STATE_IDLE;
    g_state.gesture_main_ptr_id = INVALID_PTR_ID;
    deactivate_finger(f);
}

// =========================================================================
// DOWN HELPERS
// =========================================================================

static bool down_handle_sdtw(TouchFinger* f, GestureFingerCtx* ctx,
                             TouchActionResult* restrict result, uint64_t time_ms)
{
    if (!g_state.gesture_second_active || !g_state.second_double_tap_waiting)
        return false;
    if (!f->cached_has_active_double_tap && !f->cached_has_active_double_tap_drag)
        return false;

    bool sdtw_time_valid = (time_ms - g_state.second_tap_fallback_time)
        < (uint64_t)g_state.cfg.double_tap_timeout_ms;
    float sdtw_dx = f->x - g_state.second_sdtw_ref_x;
    float sdtw_dy = f->y - g_state.second_sdtw_ref_y;
    float sdtw_dist = sqrtf(sdtw_dx * sdtw_dx + sdtw_dy * sdtw_dy);
    bool sdtw_pos_valid = sdtw_dist <= g_state.cfg.double_tap_distance_px;

    if (sdtw_time_valid && sdtw_pos_valid) {
        LOGD("DOWN ptr=%d SDTW_CONFIRM dist=%.1f", f->ptr_id, sdtw_dist);
        gesture_clear_second_finger_state();
        const GesturePairSlot* d2_slot = select_dt_slot(f);
        GesturePairPlan d_plan = resolve_gesture_pair(f, d2_slot);
        confirm_double_tap(result, d_plan,
            f->bindings.double_tap, f->bindings.double_tap_count,
            g_state.pending_second_double,
            &g_state.pending_second_double_count, FALLBACK_MAX,
            &g_state.gesture_post_double_tap_drag);
        ctx->post_double_tap_drag = g_state.gesture_post_double_tap_drag;
        f->cached_has_long_press_timer = false;
        f->down_time_ms = time_ms;
        f->state = GESTURE_STATE_TAP_WAITING;
        reset_single_tap_hold_timer(f);
        return true;
    }

    LOGD("DOWN ptr=%d SDTW_CANCEL time_ok=%d pos_ok=%d dist=%.1f",
        f->ptr_id, sdtw_time_valid, sdtw_pos_valid, sdtw_dist);
    gesture_clear_second_finger_state();
    return false;
}

static bool down_handle_global_dt(TouchFinger* f, GestureFingerCtx* ctx,
                                  TouchActionResult* restrict result)
{
    if (!g_state.gesture_double_tap_waiting)
        return false;

    TouchFinger* _main_for_dt = find_finger(g_state.gesture_main_ptr_id);
    if (!_main_for_dt) {
        LOGD("DOWN ptr=%d GLOBAL_DT_CANCEL (main finger gone)", f->ptr_id);
        gesture_cancel_double_tap_wait(result);
        return false;
    }

    if (gesture_is_within_tap_distance(f->x, f->y)) {
        LOGD("DOWN ptr=%d GLOBAL_DT_CONFIRM (second finger)", f->ptr_id);
        double_tap_confirm_internal(result, _main_for_dt);
        _main_for_dt->state = GESTURE_STATE_TAP_WAITING;
        reset_finger_tap_state(_main_for_dt);
        _main_for_dt->down_x = _main_for_dt->x;
        _main_for_dt->down_y = _main_for_dt->y;
        reset_finger_ctx(ctx, true);
        f->state = GESTURE_STATE_TAP_WAITING;
        return true;
    }

    LOGD("DOWN ptr=%d GLOBAL_DT_CANCEL (second finger)", f->ptr_id);
    gesture_cancel_double_tap_wait(result);
    return false;
}

static bool down_handle_sdtw_and_global_dt(TouchFinger* f, GestureFingerCtx* ctx,
                                           TouchActionResult* restrict result, uint64_t time_ms)
{
    const GesturePairSlot* down_slot = select_s_slot(f);

    if (down_slot->is_second_finger) {
        if (g_state.gesture_second_active && down_handle_sdtw(f, ctx, result, time_ms))
            return true;
        if (down_handle_global_dt(f, ctx, result))
            return true;
        gesture_clear_second_finger_state();
    }

    return false;
}

typedef enum { ROLE_MAIN, ROLE_SECOND, ROLE_IGNORED } DownRole;

static DownRole assign_down_role(TouchFinger* f) {
    if (g_state.gesture_main_ptr_id < 0) {
        f->is_second_finger = false;
        g_state.gesture_main_ptr_id = f->ptr_id;
        LOGD("DOWN ptr=%d ROLE_MAIN", f->ptr_id);
        return ROLE_MAIN;
    }
    if (f->ptr_id != g_state.gesture_main_ptr_id) {
        if (!g_state.gesture_second_active) {
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
            return ROLE_SECOND;
        }
        LOGD("DOWN ptr=%d IGNORE_3RD_PLUS", f->ptr_id);
        f->state = GESTURE_STATE_IDLE;
        return ROLE_IGNORED;
    }
    return ROLE_MAIN;
}

static bool handle_dt_waiting_on_down(TouchFinger* f, GestureFingerCtx* ctx,
                                      TouchActionResult* restrict result) {
    if (!ctx->dt_waiting)
        return false;

    if (gesture_is_within_tap_distance(f->x, f->y)) {
        LOGD("DOWN ptr=%d DT_CONFIRM", f->ptr_id);
        const GesturePairSlot* dt_slot = select_dt_slot(f);
        GesturePairPlan d_plan = resolve_gesture_pair(f, dt_slot);
        confirm_double_tap(result, d_plan,
            ctx->pending_double, ctx->pending_double_count,
            ctx->deferred_double, &ctx->deferred_double_count, FALLBACK_MAX,
            &ctx->post_double_tap_drag);
        ctx->dt_waiting = false;
        g_state.gesture_double_tap_waiting = false;
        ctx->pending_double_count = 0;
        reset_finger_tap_state(f);
        f->state = GESTURE_STATE_TAP_WAITING;
        return true;
    }

    LOGD("DOWN ptr=%d DT_CANCEL (out of range)", f->ptr_id);
    gesture_cancel_double_tap_wait(result);
    return false;
}

// =========================================================================
// UP HELPERS
// =========================================================================

static void up_handle_tap_waiting(TouchFinger* f, GestureFingerCtx* ctx,
                                  TouchActionResult* restrict result, uint64_t time_ms,
                                  const GesturePairSlot* slot,
                                  const TouchBinding* x, int x_cnt) {
    LOGD("UP ptr=%d TAP_WAITING deferred=%d dt_consumed=%d post_dt=%d",
        f->ptr_id, f->single_tap_deferred, ctx->dt_consumed, ctx->post_double_tap_drag);

    if (slot == SLOT_S() && g_state.cfg.single_tap_delay_ms > 0) {
        LOGD("UP ptr=%d DEFER_SINGLE_TAP delay=%d", f->ptr_id, g_state.cfg.single_tap_delay_ms);
        f->single_tap_deferred = true;
        f->single_tap_deferred_time = time_ms + g_state.cfg.single_tap_delay_ms;
        return;
    }

    TapPathResult path = execute_tap_path(f, ctx, result, time_ms,
        x, x_cnt, ctx->fallback, ctx->fallback_count, slot->is_second_finger);

    if (path == TAP_PATH_HANDLED || path == TAP_PATH_ENTER_DT)
        return;

    if (slot->is_second_finger) {
        cleanup_second_finger(f, ctx, result);
        return;
    }

    if (!f->single_tap_deferred) {
        if (!g_state.gesture_second_active || !g_state.gesture_is_action_held
            || g_state.cfg.is_tp)
            release_held_actions(result);
    }
}

static void up_handle_long_pressing(TouchFinger* f, GestureFingerCtx* ctx,
                                    TouchActionResult* restrict result) {
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
}

static void up_handle_dragging(TouchFinger* f, GestureFingerCtx* ctx,
                               TouchActionResult* restrict result) {
    LOGD("UP ptr=%d DRAGGING deferred_d=%d", f->ptr_id, ctx->deferred_double_count);
    if (ctx->deferred_double_count > 0
        && (g_state.gesture_is_action_held
            ? !bindings_equal(ctx->deferred_double, ctx->deferred_double_count,
                g_state.gesture_held_actions, g_state.gesture_held_count)
            : true))
        execute_actions(result, ctx->deferred_double, ctx->deferred_double_count);
    ctx->pending_long_press_count = 0;
    release_held_actions(result);
}

static void up_handle_dt_waiting(TouchFinger* f, GestureFingerCtx* ctx,
                                 TouchActionResult* restrict result,
                                 const GesturePairSlot* slot,
                                 const TouchBinding* x, int x_cnt) {
    LOGD("UP ptr=%d DT_WAITING x_cnt=%d sec=%d", f->ptr_id, x_cnt, slot->is_second_finger);
    if (x_cnt > 0)
        execute_actions(result, x, x_cnt);
    if (!f->cached_has_active_single_tap_drag)
        release_held_actions(result);
    ctx->dt_waiting = false;
    if (slot->is_second_finger)
        g_state.gesture_second_active = false;
    else
        g_state.gesture_double_tap_waiting = false;
}

static void up_handle_default(TouchFinger* f, GestureFingerCtx* ctx,
                               TouchActionResult* restrict result) {
    LOGD("UP ptr=%d DEFAULT state=%d post_dt_drag=%d",
        f->ptr_id, f->state, ctx->post_double_tap_drag);
    if (ctx->post_double_tap_drag) {
        ctx->post_double_tap_drag = false;
        release_held_actions(result);
    }
}

static void up_prelude_second_finger(TouchFinger* f,
                                     TouchActionResult* restrict result,
                                     const GesturePairSlot* slot) {
    if (!slot->is_second_finger)
        return;
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

// =========================================================================
// MOVE HELPERS
// =========================================================================

static bool move_resolve_drag(
    TouchFinger* f, GestureFingerCtx* ctx,
    const GesturePairSlot* slot,
    const TouchBinding* x, int x_cnt,
    const TouchBinding* xd, int xd_cnt,
    const TouchBinding** out_binding, int* out_count)
{
    bool is_d_slot = (slot == SLOT_D() || slot == SLOT_D2());

    bool press;
    if (is_d_slot) {
        press = x_cnt > 0 && f->cached_has_active_double_tap
            && !f->cached_has_active_double_tap_drag;
    } else {
        bool has_competing = f->cached_has_active_double_tap
            || f->cached_has_active_double_tap_drag
            || f->cached_has_long_press_timer;
        press = x_cnt > 0 && has_competing && !g_state.gesture_is_action_held;
    }

    const TouchBinding* fb = NULL; int fb_cnt = 0;
    if (slot == SLOT_D2()) {
        if (slot->bindings_non_drag == GESTURE_DOUBLE_2ND
            && f->cached_has_active_double_tap_drag) {
            fb = f->bindings.double_tap_drag;
            fb_cnt = f->bindings.double_tap_drag_count;
        } else {
            fb = ctx->fallback;
            fb_cnt = ctx->fallback_count;
        }
    }
    if (slot == SLOT_D() && g_state.gesture_second_active) {
        TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
        if (sf) {
            fb = sf->bindings.single_tap;
            fb_cnt = sf->bindings.single_tap_count;
        }
    }

    const TouchBinding* drag_binding = NULL; int drag_count = 0;
    bool drag_ok = resolve_drag_binding(xd, xd_cnt, x, x_cnt, fb, fb_cnt,
        is_d_slot ? press : (g_state.cfg.is_ts && press),
        slot == SLOT_D2(), &drag_binding, &drag_count);

    if (!drag_ok && x_cnt > 0) {
        drag_binding = x;
        drag_count = x_cnt;
        drag_ok = true;
        LOGD("MOVE ptr=%d POST_TIER_FALLBACK x_cnt=%d -> use non-drag as drag",
            f->ptr_id, x_cnt);
    }

    LOGD("MOVE ptr=%d DRAG_RESOLVE ok=%d slot_nd=%d drag_cnt=%d is_d2=%d",
        f->ptr_id, drag_ok, slot->bindings_non_drag, drag_count, slot == SLOT_D2());

    *out_binding = drag_binding;
    *out_count   = drag_count;
    return drag_ok;
}

static void move_resolve_binding(TouchFinger* f, GestureFingerCtx* ctx,
                                 const GesturePairSlot* slot, bool is_d,
                                 ResolvedBindings* rb) {
    *rb = resolve_slot(f, slot);

    if (is_d && g_state.gesture_second_active) {
        TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
        if (sf)
            resolve_binding_slot(sf, slot, &rb->non_drag, &rb->non_drag_count,
                                 &rb->drag, &rb->drag_count);
    }
    if (slot->is_second_finger) {
        LOGD("MOVE ptr=%d SLOT3_RESOLVE nd=%d x_cnt=%d xd_cnt=%d "
            "x_type0=%d xd_type0=%d held=%d "
            "hdt=%d hdd=%d hst=%d hsdd=%d "
            "fb_cnt=%d",
            f->ptr_id, slot->bindings_non_drag, rb->non_drag_count, rb->drag_count,
            rb->non_drag ? rb->non_drag->type : -1, rb->drag ? rb->drag->type : -1,
            g_state.gesture_is_action_held,
            f->cached_has_active_double_tap,
            f->cached_has_active_double_tap_drag,
            f->cached_has_active_single_tap,
            f->cached_has_active_single_tap_drag,
            ctx->fallback_count);
    }
}

static bool move_handle_gates(TouchFinger* f, GestureFingerCtx* ctx,
                              const GesturePairSlot* slot, bool is_d,
                              int xd_cnt) {
    if (slot->is_second_finger && g_state.gesture_is_action_held) {
        int eff_sd = g_state.cfg.is_tp
            ? g_state.cfg.tp[slot->tp_drag].count : xd_cnt;
        if (!eff_sd) {
            LOGD("MOVE ptr=%d HELD_NO_DRAG_BINDINGS -> DRAG_NO_BINDING", f->ptr_id);
            ctx->pending_double_count = 0;
            start_drag_no_binding(f, NULL);
            return false;
        }
    }

    if (is_d && g_state.cfg.is_tp && g_state.gesture_second_active) {
        LOGD("MOVE ptr=%d TP_GATE (second active)", f->ptr_id);
        ctx->post_double_tap_drag = false;
        return false;
    }
    if (!slot->is_second_finger && g_state.cfg.is_tp && !g_state.cfg.caps_has_drag_bindings) {
        LOGD("MOVE ptr=%d TP_GATE (no drag bindings)", f->ptr_id);
        return false;
    }
    return true;
}

// =========================================================================
// TP EARLY EXIT (used by handle_up)
// =========================================================================
static void up_handle_tp_early_exit(TouchFinger* f, GestureFingerCtx* ctx,
                                    TouchActionResult* restrict result, uint64_t time_ms,
                                    const TouchBinding* x, int x_cnt)
{
    if (!g_state.cfg.is_tp
        || f->state != GESTURE_STATE_TAP_WAITING)
        return;

    if (!current_mode_has_gestures()) {
        LOGD("UP ptr=%d TP_NO_GESTURES -> cleanup_main", f->ptr_id);
        release_held_actions(result);
        g_state.gesture_double_tap_waiting = false;
        cleanup_main_finger(f, ctx);
        return;
    }

    if (f->cached_has_moved_beyond_threshold
        || (time_ms - f->down_time_ms) >= TP_EARLY_EXIT_MS) {
        LOGD("UP ptr=%d TP_EARLY_EXIT moved=%d dt=%" PRIu64,
            f->ptr_id, f->cached_has_moved_beyond_threshold, time_ms - f->down_time_ms);
        if (g_state.gesture_double_tap_consumed) {
            g_state.gesture_double_tap_consumed = false;
            execute_if_not_held(result, x, x_cnt);
        }
        release_held_actions(result);
        g_state.gesture_double_tap_waiting = false;
        cleanup_main_finger(f, ctx);
    }
}

// =========================================================================
// DISPATCHER (public entry point)
// =========================================================================
void gesture_branch(TouchFinger* f, GestureFingerCtx* ctx,
                    TouchActionResult* restrict result, uint64_t time_ms,
                    GestureProcessEvent event,
                    float dx, float dy)
{
    if (event >= GESTURE_EVENT_COUNT) return;
    handlers[event](f, ctx, result, time_ms, dx, dy);
}

// =========================================================================
// GESTURE_EVENT_DOWN
// =========================================================================
static void handle_down(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy)
{
    (void)dx; (void)dy;
    g_state.gesture_is_down_event = true;
    LOGD("DOWN ptr=%d state=%d", f->ptr_id, f->state);

    DownRole role = assign_down_role(f);
    if (role == ROLE_IGNORED)
        return;

    if (handle_dt_waiting_on_down(f, ctx, result))
        return;

    if (down_handle_sdtw_and_global_dt(f, ctx, result, time_ms))
        return;

    reset_finger_ctx(ctx, f->is_second_finger);

    if (!current_mode_has_gestures() && !g_state.gesture_double_tap_waiting) {
        LOGD("DOWN ptr=%d NO_GESTURES -> IDLE", f->ptr_id);
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    const GesturePairSlot* slot = select_s_slot(f);
    ResolvedBindings rb = resolve_slot(f, slot);

    if (slot->is_second_finger && rb.non_drag_count > 0 && ctx->fallback_count == 0) {
        if (slot->bindings_non_drag != GESTURE_SINGLE_2ND
            || f->cached_has_active_double_tap
            || !f->cached_has_active_double_tap_drag) {
            copy_bindings_bounded(rb.non_drag, rb.non_drag_count,
                ctx->fallback, &ctx->fallback_count, FALLBACK_MAX);
        }
    }

    f->state = GESTURE_STATE_TAP_WAITING;
    reset_single_tap_hold_timer(f);
    ctx->dt_consumed = false;

    GesturePairPlan plan = resolve_gesture_pair(f, slot);
    if (g_state.cfg.is_ts && !g_state.gesture_is_action_held) {
        LOGD("DOWN ptr=%d TS_TAP_DOWN x_cnt=%d pulse_on_up=%d hold=%d drag=%d",
            f->ptr_id, rb.non_drag_count, plan.pulse_on_up, plan.hold_delay_ms, plan.drag_available);
        execute_tap_on_finger_down(f, result, time_ms, plan, true, rb.non_drag, rb.non_drag_count);
    } else {
        LOGD("DOWN ptr=%d TP_NO_DOWN_ACTION x_cnt=%d", f->ptr_id, rb.non_drag_count);
    }
}

// =========================================================================
// GESTURE_EVENT_MOVE
// =========================================================================
static void handle_move(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy)
{
    (void)time_ms;
    LOGD("MOVE ptr=%d state=%d", f->ptr_id, f->state);
    if (f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING) {
        LOGD("MOVE ptr=%d SKIP (state=%d)", f->ptr_id, f->state);
        return;
    }

    if (ctx->dt_waiting) {
        LOGD("MOVE ptr=%d CANCEL_DT_WAITING", f->ptr_id);
        gesture_cancel_double_tap_wait(result);
    }

    const GesturePairSlot* slot = select_slot_for_move(f, ctx);
    if (!slot) {
        LOGD("MOVE ptr=%d NO_SLOT (second LP)", f->ptr_id);
        return;
    }
    bool is_d = (slot == SLOT_D());

    ResolvedBindings rb;
    move_resolve_binding(f, ctx, slot, is_d, &rb);

    if (slot->is_second_finger && g_state.cfg.is_tp) {
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

    if (f->state == GESTURE_STATE_LONG_PRESSING) {
        gesture_clear_pending_long_press();
        ctx->pending_long_press_count = 0;
    }
    if (is_d) f->single_tap_hold_delay_ms = 0;

    if (!current_mode_has_gesture(slot->bindings_non_drag)
        && !current_mode_has_gesture(slot->bindings_drag)) {
        LOGD("MOVE ptr=%d NO_GESTURE -> DRAG_NO_BINDING", f->ptr_id);
        start_drag_no_binding(f, result);
        return;
    }

    if (!move_handle_gates(f, ctx, slot, is_d, rb.drag_count))
        return;

    const TouchBinding* drag_binding = NULL; int drag_count = 0;
    if (!move_resolve_drag(f, ctx, slot, rb.non_drag, rb.non_drag_count,
                           rb.drag, rb.drag_count, &drag_binding, &drag_count))
        return;

    if (slot->is_second_finger && g_state.gesture_is_action_held) {
        LOGD("MOVE ptr=%d POST_RESOLVE_HELD drag_cnt=%d", f->ptr_id, drag_count);
        execute_actions_hold(result, drag_binding, drag_count);
        start_drag_no_binding(f, result);
        return;
    }

    if (is_d && drag_binding == rb.non_drag)
        ctx->pending_double_count = 0;
    if (is_d) ctx->post_double_tap_drag = false;

    LOGD("MOVE ptr=%d DRAG_START drag_cnt=%d", f->ptr_id, drag_count);
    start_drag_with_binding(f, result, drag_binding, drag_count);
    if (is_d && g_state.gesture_second_active)
        mark_second_finger_dragging(f, result);
}

// =========================================================================
// GESTURE_EVENT_UP
// =========================================================================
static void handle_up(TouchFinger* f, GestureFingerCtx* ctx,
                      TouchActionResult* restrict result, uint64_t time_ms,
                      float dx, float dy)
{
    (void)dx; (void)dy;
    g_state.gesture_is_down_event = false;
    f->tap_up_x = f->x;
    f->tap_up_y = f->y;
    LOGD("UP ptr=%d state=%d sec=%d", f->ptr_id, f->state, f->is_second_finger);

    const GesturePairSlot* slot = select_slot_for_up(f, ctx);
    ResolvedBindings rb = resolve_slot(f, slot);
    LOGD("UP ptr=%d slot=%p x_cnt=%d xd_cnt=%d", f->ptr_id, (void*)slot,
        rb.non_drag_count, rb.drag_count);

    up_prelude_second_finger(f, result, slot);

    if (!slot->is_second_finger)
        up_handle_tp_early_exit(f, ctx, result, time_ms, rb.non_drag, rb.non_drag_count);

    if (f->state == GESTURE_STATE_IDLE)
        return;

    switch (f->state) {
        case GESTURE_STATE_TAP_WAITING:
            up_handle_tap_waiting(f, ctx, result, time_ms, slot,
                rb.non_drag, rb.non_drag_count);
            break;
        case GESTURE_STATE_LONG_PRESSING:
            up_handle_long_pressing(f, ctx, result);
            break;
        case GESTURE_STATE_DRAGGING:
            up_handle_dragging(f, ctx, result);
            break;
        case GESTURE_STATE_DOUBLE_TAP_WAITING:
            up_handle_dt_waiting(f, ctx, result, slot,
                rb.non_drag, rb.non_drag_count);
            break;
        default:
            up_handle_default(f, ctx, result);
            break;
    }

    LOGD("UP ptr=%d CLEANUP sec=%d", f->ptr_id, slot->is_second_finger);
    if (slot->is_second_finger) {
        cleanup_second_finger(f, ctx, result);
        return;
    }
    cleanup_main_finger(f, ctx);
}

// =========================================================================
// GESTURE_EVENT_TICK
// =========================================================================
static void handle_tick(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy)
{
    (void)dx; (void)dy;
    LOGD("TICK ptr=%d state=%d lp_timer=%d held=%d moved=%d",
        f->ptr_id, f->state,
        f->cached_has_long_press_timer,
        g_state.gesture_is_action_held,
        f->cached_has_moved_beyond_threshold);

    const GesturePairSlot* slot = select_gesture_slot(f);
    if (!slot) {
        LOGD("TICK ptr=%d NO_SLOT", f->ptr_id);
        return;
    }

    LOGD("TICK ptr=%d slot_nd=%d", f->ptr_id, slot->bindings_non_drag);

    if (f->cached_has_long_press_timer
        && (f->cached_has_moved_beyond_threshold || g_state.gesture_is_action_held)) {
        LOGD("TICK ptr=%d LP_TIMER_CANCEL moved=%d held=%d",
            f->ptr_id, f->cached_has_moved_beyond_threshold, g_state.gesture_is_action_held);
        f->cached_has_long_press_timer = false;
    }

    tick_lp_timer(f, ctx, result, time_ms);
    tick_s_hold_timer(f, ctx, result, time_ms, select_s_slot(f));
    tick_dt_timeout(f, ctx, result, time_ms, slot);
    tick_deferred_single_tap(f, ctx, result, time_ms);
}
