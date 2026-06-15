#include <inttypes.h>
#include <stdio.h>
#include "../touch_processor_internal.h"
#include "types.h"
#include "branch.h"

#define LOGD(...) do {} while(0)
#define LOGD_MOVE(...) do {} while(0)

// =========================================================================
// GESTURE LOGGING HELPERS
// =========================================================================

static const char* gesture_state_name(GestureState s) {
    switch (s) {
        case GESTURE_STATE_IDLE:                return "IDLE";
        case GESTURE_STATE_TAP_WAITING:         return "TAP_WAIT";
        case GESTURE_STATE_TOUCHING:            return "TOUCHING";
        case GESTURE_STATE_DOUBLE_TAP_WAITING:  return "DT_WAIT";
        case GESTURE_STATE_LONG_PRESSING:       return "LP";
        case GESTURE_STATE_DRAGGING:            return "DRAG";
        default:                                return "UNKNOWN";
    }
}

static const char* gesture_slot_name(const GesturePairSlot* slot) {
    if (slot == SLOT_S())  return "S";
    if (slot == SLOT_D())  return "D";
    if (slot == SLOT_L())  return "L";
    if (slot == SLOT_S2()) return "S2";
    if (slot == SLOT_D2()) return "D2";
    return "?";
}

static const char* binding_type_name(BindingType t) {
    switch (t) {
        case BINDING_NONE:              return "none";
        case BINDING_MOUSE_LEFT:        return "M_LEFT";
        case BINDING_MOUSE_RIGHT:       return "M_RIGHT";
        case BINDING_MOUSE_MIDDLE:      return "M_MID";
        case BINDING_MOUSE_BUTTON4:     return "M_B4";
        case BINDING_MOUSE_BUTTON5:     return "M_B5";
        case BINDING_MOUSE_SCROLL_UP:   return "SCROLL_U";
        case BINDING_MOUSE_SCROLL_DOWN: return "SCROLL_D";
        case BINDING_MOUSE_MOVE_LEFT:   return "MOVE_L";
        case BINDING_MOUSE_MOVE_RIGHT:  return "MOVE_R";
        case BINDING_MOUSE_MOVE_UP:     return "MOVE_U";
        case BINDING_MOUSE_MOVE_DOWN:   return "MOVE_D";
        default:
            if (t >= BINDING_KEYBOARD_FIRST && t <= BINDING_KEYBOARD_LAST)
                return "KEY";
            if (t >= BINDING_GAMEPAD_BASE) return "GP";
            return "?";
    }
}

static void log_bindings(const char* label, const TouchBinding* b, int count) {
    if (count == 0) return;
    char buf[256];
    int pos = 0;
    for (int i = 0; i < count && i < 4 && pos < (int)sizeof(buf) - 32; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        if (b[i].type >= BINDING_KEYBOARD_FIRST && b[i].type <= BINDING_KEYBOARD_LAST)
            pos += snprintf(buf + pos, sizeof(buf) - pos, "KEY(%d)", b[i].keycode);
        else if (b[i].type >= BINDING_GAMEPAD_BASE)
            pos += snprintf(buf + pos, sizeof(buf) - pos, "GP(%d)", b[i].type - BINDING_GAMEPAD_BASE);
        else
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
                binding_type_name(b[i].type), b[i].modifiers ? "[S]" : "");
    }
    if (count > 4) pos += snprintf(buf + pos, sizeof(buf) - pos, ",+%d", count - 4);
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "  %s[%d]: %s", label, count, buf);
}

// =========================================================================
// INLINE HELPERS (branch.c local, need g_state)
// =========================================================================

static inline int finger_index(const TouchFinger* f) {
    int idx = (int)(f - g_state.fingers);
    if (idx < 0 || idx >= MAX_FINGERS) return -1;
    return idx;
}

static inline void reset_finger_ctx(GestureFingerCtx* ctx, bool is_second) {
    *ctx = (GestureFingerCtx){0};
    ctx->is_second_finger = is_second;
}

static inline bool finger_can_go_idle(const TouchFinger* main_finger) {
    if (main_finger->state == GESTURE_STATE_DRAGGING) return false;
    int idx = finger_index(main_finger);
    if (main_finger->state == GESTURE_STATE_TAP_WAITING
        && idx >= 0 && g_ctx[idx].deferred_double.count > 0)
        return false;
    return true;
}

static inline void reset_single_tap_hold_timer(TouchFinger* f) {
    f->single_tap_hold_delay_ms = 0;
    f->single_tap_hold_timer = 0;
}

static inline void reset_common_gesture_state(void) {
    g_state.gesture_post_double_tap_drag = false;
}

static inline void reset_main_gesture_state(void) {
    g_state.gesture_double_tap_waiting = false;
    reset_common_gesture_state();
    g_state.gesture_main_ptr_id = INVALID_PTR_ID;
}

static inline void reset_second_gesture_state(void) {
    g_state.gesture_second_active = false;
    reset_common_gesture_state();
    g_state.second_double_tap_waiting = false;
    g_state.pending_second_double_count = 0;
    g_state.second_tap_fallback_count = 0;
}

static inline void reset_dt_gesture_state(void) {
    g_state.gesture_double_tap_waiting = false;
    gesture_clear_deferred_tap();
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
    ResolvedBindings resolved = {NULL, 0, NULL, 0};
    resolve_binding_slot(f, slot, &resolved.non_drag, &resolved.non_drag_count,
                         &resolved.drag, &resolved.drag_count);
    return resolved;
}

// =========================================================================
// FORWARD DECLARATIONS
// =========================================================================
static void handle_down(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms);
static void handle_move(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy);
static void handle_up(TouchFinger* f, GestureFingerCtx* ctx,
                      TouchActionResult* restrict result, uint64_t time_ms);
static void handle_tick(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms);
static void up_handle_tp_early_exit(TouchFinger* f, GestureFingerCtx* ctx,
                                    TouchActionResult* restrict result, uint64_t time_ms,
                                    const TouchBinding* non_drag, int non_drag_count);

// =========================================================================
// DISPATCHER (public entry point)
// =========================================================================
void gesture_branch(TouchFinger* f, GestureFingerCtx* ctx,
                    TouchActionResult* restrict result, uint64_t time_ms,
                    GestureProcessEvent event,
                    float dx, float dy)
{
    if (event >= GESTURE_EVENT_COUNT) return;
    switch (event) {
        case GESTURE_EVENT_DOWN: handle_down(f, ctx, result, time_ms); break;
        case GESTURE_EVENT_MOVE: handle_move(f, ctx, result, time_ms, dx, dy); break;
        case GESTURE_EVENT_UP:   handle_up(f, ctx, result, time_ms); break;
        case GESTURE_EVENT_TICK: handle_tick(f, ctx, result, time_ms); break;
        default: break;
    }
}

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

static inline bool should_execute_deferred(
    const TouchBinding* bindings, int count,
    const TouchBinding* held, int held_count)
{
    return count > 0 && (!g_state.gesture_is_action_held
        || !bindings_equal(bindings, count, held, held_count));
}

static void cleanup_main_finger(TouchFinger* f, GestureFingerCtx* ctx) {
    f->state = GESTURE_STATE_IDLE;
    *ctx = (GestureFingerCtx){0};
    ctx->is_second_finger = f->is_second_finger;
    gesture_clear_deferred_tap();
    g_state.gesture_pending_deferred_long_press_count = 0;
    reset_main_gesture_state();
    g_state.gesture_double_tap_consumed = false;
    if (!g_state.gesture_second_active)
        g_state.gesture_deferred_second_finger_tap = false;
    deactivate_finger(f);
}

// Mark DT consumed, go idle, return HANDLED. Used by execute_tap_path paths 1 & 2.
static inline TapPathResult mark_dt_consumed_idle(TouchFinger* f, GestureFingerCtx* ctx)
{
    ctx->post_double_tap_drag = false;
    ctx->dt_consumed = true;
    g_state.gesture_double_tap_consumed = true;
    f->state = GESTURE_STATE_IDLE;
    return TAP_PATH_HANDLED;
}

// Path 1: deferred D from DT confirm
static TapPathResult tap_path_deferred_double(
    TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* restrict result)
{
    if (ctx->deferred_double.count > 0) {
        g_state.gesture_pending_deferred_double_count = 0;
        if (!g_state.gesture_is_action_held)
            execute_actions(result, ctx->deferred_double.items, ctx->deferred_double.count);
        else
            release_held_actions(result);
        return mark_dt_consumed_idle(f, ctx);
    }
    return TAP_PATH_SINGLE; // not handled
}

// Path 2: post-DT drag cleanup
static TapPathResult tap_path_post_dt_drag(
    TouchFinger* f, GestureFingerCtx* ctx)
{
    if (ctx->post_double_tap_drag)
        return mark_dt_consumed_idle(f, ctx);
    return TAP_PATH_SINGLE; // not handled
}

// Path 3: DT consumed (3+ tap) — fire S binding
static TapPathResult tap_path_dt_consumed(
    TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* restrict result,
    const TapPathParams* params)
{
    if (ctx->dt_consumed) {
        ctx->dt_consumed = false;
        g_state.gesture_double_tap_consumed = false;
        execute_if_not_held(result, params->non_drag, params->non_drag_count);
        f->state = GESTURE_STATE_IDLE;
        return TAP_PATH_HANDLED;
    }
    return TAP_PATH_SINGLE; // not handled
}

// Path 4: Normal first tap-up — enter DT or single tap
static TapPathResult tap_path_normal(
    TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* restrict result, uint64_t time_ms,
    const TapPathParams* params)
{
    if (f->cached_has_active_double_tap) {
        if (params->is_second_finger)
            enter_sdtw(f, time_ms);
        else
            enter_double_tap_waiting(f, time_ms,
                params->non_drag, params->non_drag_count,
                f->bindings.double_tap, f->bindings.double_tap_count);
        return TAP_PATH_ENTER_DT;
    }

    if (f->cached_has_active_double_tap_drag) {
        execute_if_not_held(result, params->non_drag, params->non_drag_count);
        if (params->is_second_finger)
            enter_sdtw(f, time_ms);
        else
            enter_double_tap_waiting(f, time_ms, NULL, 0, NULL, 0);
        return TAP_PATH_ENTER_DT;
    }

    // Single tap
    if (params->is_second_finger) {
        execute_if_not_held(result, params->fallback, params->fallback_count);
    } else {
        execute_if_not_held(result, params->non_drag, params->non_drag_count);
    }
    f->state = GESTURE_STATE_IDLE;
    return TAP_PATH_SINGLE;
}

static TapPathResult execute_tap_path(
    TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* restrict result, uint64_t time_ms,
    const TapPathParams* params)
{
    TapPathResult r;

    r = tap_path_deferred_double(f, ctx, result);
    if (r != TAP_PATH_SINGLE) return r;

    r = tap_path_post_dt_drag(f, ctx);
    if (r != TAP_PATH_SINGLE) return r;

    r = tap_path_dt_consumed(f, ctx, result, params);
    if (r != TAP_PATH_SINGLE) return r;

    return tap_path_normal(f, ctx, result, time_ms, params);
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
        || time_ms - f->down_time_ms < (uint64_t)g_state.cfg.long_press_timeout_ms) /* int→u64: prevent sign extension */
        return;

    gesture_clear_second_finger_state();
    g_state.second_tap_fallback_count = 0;

    const GesturePairSlot* long_press_slot = SLOT_L();
    ResolvedBindings resolved = resolve_slot(f, long_press_slot);
    GesturePairPlan long_press_plan = resolve_gesture_pair(f, long_press_slot);

    if (long_press_plan.pulse_on_up) {
        if (long_press_plan.drag_available) {
            copy_bindings_bounded(resolved.non_drag, resolved.non_drag_count,
                ctx->pending_long_press.items, &ctx->pending_long_press.count, FALLBACK_MAX);
            copy_bindings_bounded(resolved.non_drag, resolved.non_drag_count,
                g_state.gesture_pending_deferred_long_press,
                &g_state.gesture_pending_deferred_long_press_count, FALLBACK_MAX);
        } else {
            execute_actions(result, resolved.non_drag, resolved.non_drag_count);
            f->cached_has_active_single_tap = false;
        }
        f->cached_has_long_press_timer = false;
    } else {
        execute_actions_hold(result, resolved.non_drag, resolved.non_drag_count);
        f->cached_has_long_press_timer = false;
    }
    f->single_tap_hold_delay_ms = 0;
    f->state = GESTURE_STATE_LONG_PRESSING;
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "TICK_LP ptr=%d pulse=%d drag=%d held=%d",
        f->ptr_id, long_press_plan.pulse_on_up, long_press_plan.drag_available,
        g_state.gesture_is_action_held);
    log_bindings("TICK_LP", resolved.non_drag, resolved.non_drag_count);
    if (g_state.cfg.gesture_long_press_haptic > 0)
        add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
}

static void tick_s_hold_timer(TouchFinger* f, GestureFingerCtx* ctx,
                              TouchActionResult* restrict result, uint64_t time_ms,
                              const GesturePairSlot* slot)
{
    ResolvedBindings resolved = resolve_slot(f, slot);

    if (f->single_tap_hold_delay_ms <= 0
        || time_ms - f->single_tap_hold_timer < (uint64_t)f->single_tap_hold_delay_ms /* int→u64: prevent sign extension */
        || g_state.gesture_is_action_held
        || f->cached_has_moved_beyond_threshold
        || resolved.non_drag_count == 0)
        return;

    f->single_tap_hold_delay_ms = 0;
    if (slot->is_second_finger)
        execute_actions(result, resolved.non_drag, resolved.non_drag_count);
    else
        execute_actions_hold(result, resolved.non_drag, resolved.non_drag_count);
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "TICK_S_HOLD ptr=%d slot=%s held=%d",
        f->ptr_id, gesture_slot_name(slot), g_state.gesture_is_action_held);
    log_bindings("TICK_S_HOLD", resolved.non_drag, resolved.non_drag_count);
    ctx->pending_double.count = 0;
    gesture_clear_deferred_tap();
    if (slot->is_second_finger) {
        g_state.second_tap_fallback_count = 0;
        gesture_clear_second_finger_state();
    }
}

static void reset_dt_ctx(GestureFingerCtx* ctx) {
    ctx->dt_waiting = false;
    ctx->pending_double.count = 0;
    ctx->deferred_double.count = 0;
}

static inline void execute_and_cleanup_deferred(
    TouchActionResult* restrict result,
    const TouchBinding* bindings, int count,
    TouchFinger* f, GestureFingerCtx* ctx)
{
    (void)ctx;
    if (count > 0)
        execute_actions(result, bindings, count);
    /* When binding_delay > 0, execute_actions() schedules the PRESS via
     * schedule_action() instead of adding it immediately.  Flush those
     * scheduled presses NOW so they become real held-actions, then
     * release_held_actions() can cancel remaining schedules safely and
     * dispatch the matching RELEASE.  Without this flush the scheduled
     * PRESS would be cancelled before ever firing, producing only a
     * RELEASE (orphaned button state). */
    process_scheduled_actions(result, UINT64_MAX);
    release_held_actions(result);
    deactivate_finger(f);
}

static void tick_dt_timeout(TouchFinger* f, GestureFingerCtx* ctx,
                            TouchActionResult* restrict result, uint64_t time_ms,
                            const GesturePairSlot* slot)
{
    if (!ctx->dt_waiting
        || time_ms - ctx->dt_wait_start_time < (uint64_t)g_state.cfg.double_tap_timeout_ms) /* int→u64: prevent sign extension */
        return;

    reset_dt_ctx(ctx);

    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "TICK_DT_TIMEOUT ptr=%d is_2nd=%d deferred=%d",
        f->ptr_id, slot->is_second_finger, slot->is_second_finger ? ctx->fallback.count : g_state.gesture_deferred_tap_count);

    if (slot->is_second_finger) {
        execute_and_cleanup_deferred(result, ctx->fallback.items, ctx->fallback.count, f, ctx);
        ctx->fallback.count = 0;
        // Only reset second gesture state if this finger is still the registered
        // second finger — otherwise a new second finger may have been assigned
        // via handle_ts_second_finger_down or role transfer, and resetting would
        // corrupt its state (gesture_second_active, second_double_tap_waiting, etc.)
        if (g_state.gesture_second_ptr_id == f->ptr_id
            || g_state.gesture_second_ptr_id == INVALID_PTR_ID) {
            reset_second_gesture_state();
            TouchFinger* main_finger = find_finger(g_state.gesture_main_ptr_id);
            if (main_finger && finger_can_go_idle(main_finger))
                main_finger->state = GESTURE_STATE_IDLE;
        }
    } else {
        if (g_state.gesture_deferred_tap_count > 0) {
        }
        execute_and_cleanup_deferred(result, g_state.gesture_deferred_tap,
            g_state.gesture_deferred_tap_count, f, ctx);
        gesture_clear_deferred_tap();
        reset_main_gesture_state();
    }
}

static void tick_deferred_single_tap(TouchFinger* f, GestureFingerCtx* ctx,
                                     TouchActionResult* restrict result, uint64_t time_ms)
{
    if (!f->single_tap_deferred || time_ms < f->single_tap_deferred_time)
        return;

    f->single_tap_deferred = false;

    const GesturePairSlot* ds_slot = select_s_slot(f);
    ResolvedBindings resolved = resolve_slot(f, ds_slot);

    const TapPathParams tap_params = {
        .non_drag = resolved.non_drag,
        .non_drag_count = resolved.non_drag_count,
        .fallback = resolved.non_drag,
        .fallback_count = resolved.non_drag_count,
        .is_second_finger = ds_slot->is_second_finger,
    };
    TapPathResult path = execute_tap_path(f, ctx, result, time_ms, &tap_params);

    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "TAP_TICK path=%d result=%d held_before=%d is_held=%d",
        path, result->count, g_state.gesture_held_count, g_state.gesture_is_action_held);
    release_held_actions(result);
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "TAP_TICK after_release result=%d is_held=%d held=%d",
        result->count, g_state.gesture_is_action_held, g_state.gesture_held_count);

    if (path == TAP_PATH_ENTER_DT) return;

    f->state = GESTURE_STATE_IDLE;
    g_state.gesture_main_ptr_id = INVALID_PTR_ID;
    execute_and_cleanup_deferred(result, NULL, 0, f, ctx);
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
        < (uint64_t)g_state.cfg.double_tap_timeout_ms; /* int→u64: prevent sign extension */
    float sdtw_dx = f->x - g_state.second_sdtw_ref_x;
    float sdtw_dy = f->y - g_state.second_sdtw_ref_y;
    float sdtw_dist = sqrtf(sdtw_dx * sdtw_dx + sdtw_dy * sdtw_dy);
    bool sdtw_pos_valid = sdtw_dist <= g_state.cfg.double_tap_distance_px;

    if (sdtw_time_valid && sdtw_pos_valid) {
        gesture_clear_second_finger_state();
        const GesturePairSlot* double_tap_slot = select_dt_slot(f);
        GesturePairPlan double_tap_plan = resolve_gesture_pair(f, double_tap_slot);
        confirm_double_tap(result, double_tap_plan,
            f->bindings.double_tap, f->bindings.double_tap_count,
            g_state.pending_second_double,
            &g_state.pending_second_double_count, FALLBACK_MAX,
            &g_state.gesture_post_double_tap_drag);
        ctx->post_double_tap_drag = g_state.gesture_post_double_tap_drag;
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
            "SDTW_CONFIRMED ptr=%d slot=%s post_dtd=%d",
            f->ptr_id, gesture_slot_name(double_tap_slot), ctx->post_double_tap_drag);

        // Always populate ctx->deferred_double so D2 fires on second-tap UP
        // (via tap_path_deferred_double) or after drag (via up_handle_dragging),
        // matching single-finger D/Dd behavior where handle_dt_waiting_on_down
        // copies ctx->pending_double to ctx->deferred_double.
        // Clear pending_second_double to prevent on_drag_start() from re-firing
        // D2 when the user transitions from tap to drag.
        copy_bindings_bounded(f->bindings.double_tap, f->bindings.double_tap_count,
            ctx->deferred_double.items, &ctx->deferred_double.count, FALLBACK_MAX);
        g_state.pending_second_double_count = 0;

        f->cached_has_long_press_timer = false;
        f->down_time_ms = time_ms;
        f->state = GESTURE_STATE_TAP_WAITING;
        reset_single_tap_hold_timer(f);
        return true;
    }

    gesture_clear_second_finger_state();
    return false;
}

static bool down_handle_global_dt(TouchFinger* f, GestureFingerCtx* ctx,
                                  TouchActionResult* restrict result)
{
    if (!g_state.gesture_double_tap_waiting)
        return false;

    TouchFinger* main_dt_finger = find_finger(g_state.gesture_main_ptr_id);
    if (!main_dt_finger) {
        gesture_cancel_double_tap_wait(result);
        return false;
    }

    if (gesture_is_within_tap_distance(f->x, f->y)) {
        double_tap_confirm_internal(result, main_dt_finger);
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
            "GLOBAL_DT_CONFIRMED main_ptr=%d second_ptr=%d",
            main_dt_finger->ptr_id, f->ptr_id);
        main_dt_finger->state = GESTURE_STATE_TAP_WAITING;
        reset_finger_tap_state(main_dt_finger);
        main_dt_finger->down_x = main_dt_finger->x;
        main_dt_finger->down_y = main_dt_finger->y;
        reset_finger_ctx(ctx, true);
        f->state = GESTURE_STATE_TAP_WAITING;
        return true;
    }

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

static DownRole assign_down_role(TouchFinger* f) {
    if (g_state.gesture_main_ptr_id == INVALID_PTR_ID) {
        f->is_second_finger = false;
        g_state.gesture_main_ptr_id = f->ptr_id;
        return ROLE_MAIN;
    }
    if (f->ptr_id != g_state.gesture_main_ptr_id) {
        if (!g_state.gesture_second_active) {
            g_state.gesture_second_active = true;
            g_state.gesture_second_ptr_id = f->ptr_id;
            f->is_second_finger = true;
            TouchFinger* main_finger = find_finger(g_state.gesture_main_ptr_id);
            if (main_finger) {
                g_state.gesture_second_main_ref_x = main_finger->x;
                g_state.gesture_second_main_ref_y = main_finger->y;
                main_finger->cached_has_long_press_timer = false;
            }
            return ROLE_SECOND;
        }
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
        const GesturePairSlot* dt_slot = select_dt_slot(f);
        GesturePairPlan double_tap_plan = resolve_gesture_pair(f, dt_slot);
        confirm_double_tap(result, double_tap_plan,
            ctx->pending_double.items, ctx->pending_double.count,
            ctx->deferred_double.items, &ctx->deferred_double.count, FALLBACK_MAX,
            &ctx->post_double_tap_drag);
        ctx->dt_waiting = false;
        g_state.gesture_double_tap_waiting = false;
        ctx->pending_double.count = 0;
        reset_finger_tap_state(f);
        f->state = GESTURE_STATE_TAP_WAITING;
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
            "DT_DOWN_CONFIRMED ptr=%d slot=%s post_dtd=%d",
            f->ptr_id, gesture_slot_name(dt_slot), ctx->post_double_tap_drag);
        return true;
    }

    gesture_cancel_double_tap_wait(result);
    return false;
}

// =========================================================================
// UP HELPERS
// =========================================================================

static bool up_handle_tap_waiting(TouchFinger* f, GestureFingerCtx* ctx,
                                  TouchActionResult* restrict result, uint64_t time_ms,
                                  const GesturePairSlot* slot,
                                  const TouchBinding* non_drag, int non_drag_count) {

    if (slot == SLOT_S() && g_state.cfg.single_tap_delay_ms > 0) {
        f->single_tap_deferred = true;
        f->single_tap_deferred_time = time_ms + g_state.cfg.single_tap_delay_ms;
        return true;
    }

    const TapPathParams tap_params = {
        .non_drag = non_drag,
        .non_drag_count = non_drag_count,
        .fallback = ctx->fallback.items,
        .fallback_count = ctx->fallback.count,
        .is_second_finger = slot->is_second_finger,
    };
    TapPathResult path = execute_tap_path(f, ctx, result, time_ms, &tap_params);

    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "UP_TAP ptr=%d slot=%s path=%d result=%d",
        f->ptr_id, gesture_slot_name(slot), path, result->count);

    // TAP_PATH_HANDLED: DT consumed paths 1-3. State set to IDLE by execute_tap_path.
    if (path == TAP_PATH_HANDLED)
        return false;

    // TAP_PATH_ENTER_DT: entered DT/SDTW waiting.
    if (path == TAP_PATH_ENTER_DT) {
        release_held_actions(result);
        if (slot->is_second_finger)
            cleanup_second_finger(f, ctx, result);
        return true;
    }

    // TAP_PATH_SINGLE: single tap executed. State set to IDLE.
    if (slot->is_second_finger) {
        cleanup_second_finger(f, ctx, result);
        return true;
    }

    if (!f->single_tap_deferred) {
        release_held_actions(result);
    }
    return false;
}

static void up_handle_long_pressing(TouchFinger* f, GestureFingerCtx* ctx,
                                    TouchActionResult* restrict result) {
    if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag)
        execute_deferred_double(result);
    if (ctx->pending_long_press.count > 0) {
        execute_actions(result, ctx->pending_long_press.items,
            ctx->pending_long_press.count);
        ctx->pending_long_press.count = 0;
    } else if (!g_state.gesture_is_action_held
        && f->cached_has_active_long_press)
        execute_actions(result, f->bindings.long_press,
            f->bindings.long_press_count);
    release_held_actions(result);
}

static void up_handle_dragging(TouchFinger* f, GestureFingerCtx* ctx,
                               TouchActionResult* restrict result) {
    if (should_execute_deferred(ctx->deferred_double.items, ctx->deferred_double.count,
            g_state.gesture_held_actions, g_state.gesture_held_count))
        execute_actions(result, ctx->deferred_double.items, ctx->deferred_double.count);
    ctx->pending_long_press.count = 0;
    release_held_actions(result);
}

static void up_handle_dt_waiting(TouchFinger* f, GestureFingerCtx* ctx,
                                 TouchActionResult* restrict result,
                                 const GesturePairSlot* slot,
                                 const TouchBinding* non_drag, int non_drag_count) {
    if (non_drag_count > 0)
        execute_actions(result, non_drag, non_drag_count);
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
        return;
    }
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

// =========================================================================
// SCROLL MODE — processes finger movement when in scroll mode
// =========================================================================

// Determine which direction has dominant accumulated movement and fire its binding
static void scroll_mode_process(TouchFinger* f, TouchActionResult* restrict result) {
    int si = f->scroll_gesture_index;
    if (si < 0 || si >= 5) return;

    float dx = f->x - f->scroll_origin_x;
    float dy = f->y - f->scroll_origin_y;
    float hdx = f->x - f->scroll_hold_origin_x;
    float hdy = f->y - f->scroll_hold_origin_y;
    static const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};

    // --- Hold mode: zone-based relative to hold origin (never reset) ---
    // Vertical hold
    if (g_state.cfg.scroll_hold_v[si]) {
        float threshold = (float)g_state.cfg.scroll_hold_threshold_px;
        int new_state;
        if (fabsf(hdy) < threshold) new_state = 0;
        else new_state = (hdy < 0) ? 1 : 2;

        if (new_state != f->scroll_hold_v_state) {
            if (g_state.cfg.scroll_hold_haptic > 0)
                add_action(result, ACT_HAPTIC, g_state.cfg.scroll_hold_haptic, 0, 0);

            // Release only the previously held binding
            if (f->scroll_hold_v_state != 0) {
                int old_dir = (f->scroll_hold_v_state == 1) ? SCROLL_DIR_UP : SCROLL_DIR_DOWN;
                const GestureBindingSlot* old_slot = &g_state.cfg.scroll_bindings[si].dirs[old_dir];
                for (int i = 0; i < old_slot->count; i++)
                    release_binding(result, &old_slot->arr[i]);
            }

            if (new_state != 0) {
                int new_dir = (new_state == 1) ? SCROLL_DIR_UP : SCROLL_DIR_DOWN;
                const GestureBindingSlot* new_slot = &g_state.cfg.scroll_bindings[si].dirs[new_dir];
                for (int i = 0; i < new_slot->count; i++)
                    press_binding(result, &new_slot->arr[i], true);
                __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HOLD_V slot=%d dir=%s",
                    si, dir_names[new_dir]);
            } else {
                __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HOLD_V slot=%d NONE", si);
            }
            f->scroll_hold_v_state = new_state;
        }
    }

    // Horizontal hold
    if (g_state.cfg.scroll_hold_h[si]) {
        float threshold = (float)g_state.cfg.scroll_hold_threshold_px;
        int new_state;
        if (fabsf(hdx) < threshold) new_state = 0;
        else new_state = (hdx < 0) ? 1 : 2;

        if (new_state != f->scroll_hold_h_state) {
            if (g_state.cfg.scroll_hold_haptic > 0)
                add_action(result, ACT_HAPTIC, g_state.cfg.scroll_hold_haptic, 0, 0);

            // Release only the previously held binding
            if (f->scroll_hold_h_state != 0) {
                int old_dir = (f->scroll_hold_h_state == 1) ? SCROLL_DIR_LEFT : SCROLL_DIR_RIGHT;
                const GestureBindingSlot* old_slot = &g_state.cfg.scroll_bindings[si].dirs[old_dir];
                for (int i = 0; i < old_slot->count; i++)
                    release_binding(result, &old_slot->arr[i]);
            }

            if (new_state != 0) {
                int new_dir = (new_state == 1) ? SCROLL_DIR_LEFT : SCROLL_DIR_RIGHT;
                const GestureBindingSlot* new_slot = &g_state.cfg.scroll_bindings[si].dirs[new_dir];
                for (int i = 0; i < new_slot->count; i++)
                    press_binding(result, &new_slot->arr[i], true);
                __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HOLD_H slot=%d dir=%s",
                    si, dir_names[new_dir]);
            } else {
                __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HOLD_H slot=%d NONE", si);
            }
            f->scroll_hold_h_state = new_state;
        }
    }

    // --- Normal scroll mode: fire on threshold ---
    int dir = -1;
    float dominant = 0.0f;
    float adx = fabsf(dx);
    float ady = fabsf(dy);
    if (ady > adx && ady > 0.0f) {
        dir = (dy < 0.0f) ? SCROLL_DIR_UP : SCROLL_DIR_DOWN;
        dominant = ady;
    } else if (adx > 0.0f) {
        dir = (dx < 0.0f) ? SCROLL_DIR_LEFT : SCROLL_DIR_RIGHT;
        dominant = adx;
    }
    if (dir < 0) return;

    bool is_vert = (dir == SCROLL_DIR_UP || dir == SCROLL_DIR_DOWN);
    if (is_vert ? g_state.cfg.scroll_hold_v[si] : g_state.cfg.scroll_hold_h[si])
        return; // hold mode handles this axis

    f->scroll_accum[dir] += dominant;
    f->scroll_origin_x = f->x;
    f->scroll_origin_y = f->y;

    float threshold = (float)g_state.cfg.scroll_threshold_px;
    if (f->scroll_accum[dir] < threshold) return;
    f->scroll_accum[dir] -= threshold;

    const GestureBindingSlot* slot = &g_state.cfg.scroll_bindings[si].dirs[dir];
    if (slot->count == 0) return;

    if (g_state.cfg.scroll_bind_haptic > 0)
        add_action(result, ACT_HAPTIC, g_state.cfg.scroll_bind_haptic, 0, 0);
    execute_actions(result, slot->arr, slot->count);
}

static void handle_move_scroll_mode(TouchFinger* f, GestureFingerCtx* ctx,
                                     TouchActionResult* restrict result, uint64_t time_ms) {
    scroll_mode_process(f, result);
}

// =========================================================================
// MOVE HELPERS
// =========================================================================

static void resolve_fallback_bindings(
    const GesturePairSlot* slot,
    TouchFinger* f, GestureFingerCtx* ctx,
    const TouchBinding** fallback_bindings, int* fallback_count)
{
    *fallback_bindings = NULL; *fallback_count = 0;
    if (slot == SLOT_D2()) {
        if (slot->bindings_non_drag == GESTURE_DOUBLE_2ND
            && f->cached_has_active_double_tap_drag) {
            *fallback_bindings = f->bindings.double_tap_drag;
            *fallback_count = f->bindings.double_tap_drag_count;
        } else {
            *fallback_bindings = ctx->fallback.items;
            *fallback_count = ctx->fallback.count;
        }
    }
    if (slot == SLOT_D() && g_state.gesture_second_active) {
        TouchFinger* second_finger = find_finger(g_state.gesture_second_ptr_id);
        if (second_finger) {
            *fallback_bindings = second_finger->bindings.single_tap;
            *fallback_count = second_finger->bindings.single_tap_count;
        }
    }
}

static bool move_resolve_drag(
    TouchFinger* f, GestureFingerCtx* ctx,
    const GesturePairSlot* slot,
    const DragResolveParams* params,
    const TouchBinding** out_binding, int* out_count)
{
    bool is_d_slot = (slot == SLOT_D() || slot == SLOT_D2());

    bool press;
    if (is_d_slot) {
        press = params->non_drag_count > 0 && f->cached_has_active_double_tap
            && !f->cached_has_active_double_tap_drag;
    } else {
        bool has_competing = f->cached_has_active_double_tap
            || f->cached_has_active_double_tap_drag
            || f->cached_has_long_press_timer;
        press = params->non_drag_count > 0 && has_competing && !g_state.gesture_is_action_held;
    }

    const TouchBinding* fallback_bindings = NULL; int fallback_count = 0;
    resolve_fallback_bindings(slot, f, ctx, &fallback_bindings, &fallback_count);

    const TouchBinding* drag_binding = NULL; int drag_count = 0;
    const DragResolveContext resolve_ctx = {
        .drag = params->drag,
        .drag_count = params->drag_count,
        .non_drag = params->non_drag,
        .non_drag_count = params->non_drag_count,
        .fallback = fallback_bindings,
        .fallback_count = fallback_count,
        .press_on_drag = is_d_slot ? press : (g_state.cfg.is_ts && press),
        .is_d2_slot = slot == SLOT_D2(),
    };
    bool drag_ok = resolve_drag_binding(&resolve_ctx, &drag_binding, &drag_count);

    if (!drag_ok && !g_state.cfg.is_tp && params->non_drag_count > 0) {
        drag_binding = params->non_drag;
        drag_count = params->non_drag_count;
        drag_ok = true;
    }


    *out_binding = drag_binding;
    *out_count   = drag_count;
    return drag_ok;
}

static void move_resolve_binding(TouchFinger* f, GestureFingerCtx* ctx,
                                 const GesturePairSlot* slot, bool is_d_slot,
                                 ResolvedBindings* resolved) {
    *resolved = resolve_slot(f, slot);

    if (is_d_slot && g_state.gesture_second_active) {
        TouchFinger* second_finger = find_finger(g_state.gesture_second_ptr_id);
        if (second_finger)
            resolve_binding_slot(second_finger, slot, &resolved->non_drag, &resolved->non_drag_count,
                                 &resolved->drag, &resolved->drag_count);
    }
    if (slot->is_second_finger) {
    }
}

static bool move_handle_gates(TouchFinger* f, GestureFingerCtx* ctx,
                              const GesturePairSlot* slot, bool is_d_slot,
                              int xd_cnt,
                              TouchActionResult* restrict result) {
    if (slot->is_second_finger && g_state.gesture_is_action_held) {
        int effective_second_drag_count = g_state.cfg.is_tp
            ? g_state.cfg.tp[slot->tp_drag].count : xd_cnt;
        bool has_scroll = g_state.cfg.caps_has_scroll_bindings
            && gesture_has_scroll_bindings(slot->bindings_drag);
        if (!effective_second_drag_count && !has_scroll) {
            ctx->pending_double.count = 0;
            start_drag_no_binding(f, result);
            return false;
        }
    }

    if (is_d_slot && g_state.cfg.is_tp && g_state.gesture_second_active) {
        ctx->post_double_tap_drag = false;
        return false;
    }
    if (!slot->is_second_finger && g_state.cfg.is_tp && !g_state.cfg.caps_has_drag_bindings) {
        return false;
    }
    return true;
}

// =========================================================================
// TP EARLY EXIT (used by handle_up)
// =========================================================================
static void up_handle_tp_early_exit(TouchFinger* f, GestureFingerCtx* ctx,
                                    TouchActionResult* restrict result, uint64_t time_ms,
                                    const TouchBinding* non_drag, int non_drag_count)
{
    if (!g_state.cfg.is_tp
        || f->state != GESTURE_STATE_TAP_WAITING)
        return;

    if (!current_mode_has_gestures()) {
        release_held_actions(result);
        reset_main_gesture_state();
        cleanup_main_finger(f, ctx);
        return;
    }

    if (f->cached_has_moved_beyond_threshold
        || (time_ms - f->down_time_ms) >= TP_EARLY_EXIT_MS) {
        if (g_state.gesture_double_tap_consumed) {
            g_state.gesture_double_tap_consumed = false;
            execute_if_not_held(result, non_drag, non_drag_count);
        }
        release_held_actions(result);
        reset_main_gesture_state();
        cleanup_main_finger(f, ctx);
    }
}



// =========================================================================
// GESTURE_EVENT_DOWN
// =========================================================================
static void handle_down(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms)
{
    g_state.gesture_is_down_event = true;

    DownRole role = assign_down_role(f);
    if (role == ROLE_IGNORED)
        return;

    if (handle_dt_waiting_on_down(f, ctx, result))
        return;

    if (down_handle_sdtw_and_global_dt(f, ctx, result, time_ms))
        return;

    reset_finger_ctx(ctx, f->is_second_finger);

    if (!current_mode_has_gestures() && !g_state.gesture_double_tap_waiting
        && !g_state.cfg.caps_has_scroll_bindings) {
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    const GesturePairSlot* slot = select_s_slot(f);
    ResolvedBindings resolved = resolve_slot(f, slot);

    if (slot->is_second_finger && resolved.non_drag_count > 0 && ctx->fallback.count == 0) {
        if (slot->bindings_non_drag != GESTURE_SINGLE_2ND
            || f->cached_has_active_double_tap
            || !f->cached_has_active_double_tap_drag) {
            copy_bindings_bounded(resolved.non_drag, resolved.non_drag_count,
                ctx->fallback.items, &ctx->fallback.count, FALLBACK_MAX);
        }
    }

    f->state = GESTURE_STATE_TAP_WAITING;
    reset_single_tap_hold_timer(f);
    ctx->dt_consumed = false;

    GesturePairPlan plan = resolve_gesture_pair(f, slot);
    const GesturePairSlot* gesture_slot = select_gesture_slot(f);

    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "DOWN ptr=%d role=%s slot=%s gesture=%s pulse=%d press_drag=%d drag_avail=%d hold_delay=%d nd=%d",
        f->ptr_id, f->is_second_finger ? "2ND" : "MAIN",
        gesture_slot_name(slot), gesture_slot ? gesture_slot_name(gesture_slot) : "?",
        plan.pulse_on_up, plan.press_on_drag,
        plan.drag_available, plan.hold_delay_ms, resolved.non_drag_count);
    log_bindings("DOWN nd", resolved.non_drag, resolved.non_drag_count);
    log_bindings("DOWN drag", resolved.drag, resolved.drag_count);

    if (g_state.cfg.is_ts && !g_state.gesture_is_action_held) {
        execute_tap_on_finger_down(f, result, time_ms, plan, true, resolved.non_drag, resolved.non_drag_count);
    } else if (g_state.cfg.is_tp && slot->is_second_finger
               && resolved.non_drag_count > 0
               && !plan.pulse_on_up && !plan.press_on_drag
               && !g_state.gesture_is_action_held) {
        execute_actions_hold(result, resolved.non_drag, resolved.non_drag_count);
    } else {
    }
}

// =========================================================================
// GESTURE_EVENT_MOVE
// =========================================================================

typedef struct {
    const GesturePairSlot* slot;
    bool is_d_slot;
    ResolvedBindings resolved;
} MoveContext;

// Slot resolution, binding resolution, TP adjustment, threshold check.
// Returns true if move should continue, false to skip.
static bool handle_move_resolve(TouchFinger* f, GestureFingerCtx* ctx,
                                TouchActionResult* restrict result,
                                float* dx, float* dy,
                                MoveContext* mc)
{
    if (f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING) {
        return false;
    }

    if (ctx->dt_waiting) {
        gesture_cancel_double_tap_wait(result);
    }

    mc->slot = select_slot_common(f, ctx, false);
    if (!mc->slot) {
        return false;
    }
    mc->is_d_slot = (mc->slot == SLOT_D());

    move_resolve_binding(f, ctx, mc->slot, mc->is_d_slot, &mc->resolved);

    TouchFinger* main_finger_tp = NULL;
    if (mc->slot->is_second_finger && g_state.cfg.is_tp) {
        main_finger_tp = find_finger(g_state.gesture_main_ptr_id);
        if (main_finger_tp) {
            *dx += main_finger_tp->x - g_state.gesture_second_main_ref_x;
            *dy += main_finger_tp->y - g_state.gesture_second_main_ref_y;
        }
    }
    float check_dx = main_finger_tp ? main_finger_tp->x - g_state.gesture_second_main_ref_x : *dx;
    float check_dy = main_finger_tp ? main_finger_tp->y - g_state.gesture_second_main_ref_y : *dy;
    if (fabsf(check_dx) <= g_state.cfg.drag_threshold_px && fabsf(check_dy) <= g_state.cfg.drag_threshold_px) {
        return false;
    }
    return true;
}

// Second finger with held action: execute hold, start no-binding drag.
// Returns true if handled (caller should return).
static bool handle_move_second_finger(TouchFinger* f, GestureFingerCtx* ctx,
                                      TouchActionResult* restrict result,
                                      const MoveContext* mc,
                                      const TouchBinding* drag_binding, int drag_count)
{
    if (mc->slot->is_second_finger && g_state.gesture_is_action_held) {
        execute_actions_hold(result, drag_binding, drag_count);
        start_drag_no_binding(f, result);
        return true;
    }
    return false;
}

// Apply resolved drag binding: D-slot cleanup + drag start.
static void handle_move_apply(TouchFinger* f, GestureFingerCtx* ctx,
                              TouchActionResult* restrict result,
                              const MoveContext* mc,
                              const TouchBinding* drag_binding, int drag_count)
{
    if (mc->is_d_slot && drag_binding == mc->resolved.non_drag)
        ctx->pending_double.count = 0;
    if (mc->is_d_slot) ctx->post_double_tap_drag = false;

    start_drag_with_binding(f, result, drag_binding, drag_count);
    if (mc->is_d_slot && g_state.gesture_second_active)
        mark_second_finger_dragging(f, result);
}

static void handle_move(TouchFinger* f, GestureFingerCtx* ctx,
                        TouchActionResult* restrict result, uint64_t time_ms,
                        float dx, float dy)
{
    // If already in scroll mode, just process scroll movement
    if (f->scroll_mode) {
        handle_move_scroll_mode(f, ctx, result, time_ms);
        return;
    }

    MoveContext mc;
    if (!handle_move_resolve(f, ctx, result, &dx, &dy, &mc))
        return;

    if (f->state == GESTURE_STATE_LONG_PRESSING) {
        gesture_clear_pending_long_press();
        ctx->pending_long_press.count = 0;
    }
    if (mc.is_d_slot) f->single_tap_hold_delay_ms = 0;

    // Scroll mode: a variant of the drag gesture.
    // Goes through the SAME mode-binding checks and exclusion gates as normal drag.
    // Only diverges at execution: scroll_mode_enter instead of execute_actions_hold(drag).
    // When Dd2-only is triggered via double-tap and Dd2 drag bindings exist,
    // skip scroll mode so the user's configured drag bindings fire.
    bool post_dtd_with_drag = ctx->post_double_tap_drag
        && current_mode_has_gesture(mc.slot->bindings_drag);
    bool scroll_for_slot = g_state.cfg.is_ts && g_state.cfg.caps_has_scroll_bindings
        && gesture_has_scroll_bindings(mc.slot->bindings_drag)
        && !post_dtd_with_drag;

    if (!current_mode_has_gesture(mc.slot->bindings_non_drag)
        && !current_mode_has_gesture(mc.slot->bindings_drag)
        && !scroll_for_slot) {
        start_drag_no_binding(f, result);
        return;
    }

    if (!move_handle_gates(f, ctx, mc.slot, mc.is_d_slot, mc.resolved.drag_count, result))
        return;

    // SCROLL MODE: enters after the same gates as normal drag.
    // For two-finger gestures, scroll tracking is on the MAIN finger (the one
    // already down), not the second finger (the trigger).
    if (scroll_for_slot) {
        TouchFinger* scroll_finger = f;
        if (mc.slot->is_second_finger) {
            TouchFinger* main = find_finger(g_state.gesture_main_ptr_id);
            if (main && main->active) scroll_finger = main;
        }
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
            "MOVE_SCROLL ptr=%d scroll_ptr=%d slot=%s dx=%.1f dy=%.1f cursor=(%.0f,%.0f)",
            f->ptr_id, scroll_finger->ptr_id, gesture_slot_name(mc.slot), dx, dy,
            g_state.ptr_x, g_state.ptr_y);
        release_held_actions(result);
        scroll_mode_enter(scroll_finger, mc.slot->bindings_drag);
        start_drag_no_binding(f, result);
        return;
    }

    const TouchBinding* drag_binding = NULL; int drag_count = 0;
    const DragResolveParams drag_params = {
        .non_drag = mc.resolved.non_drag,
        .non_drag_count = mc.resolved.non_drag_count,
        .drag = mc.resolved.drag,
        .drag_count = mc.resolved.drag_count,
    };
    if (!move_resolve_drag(f, ctx, mc.slot, &drag_params, &drag_binding, &drag_count))
        return;

    if (handle_move_second_finger(f, ctx, result, &mc, drag_binding, drag_count))
        return;

    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "MOVE_DRAG ptr=%d state=%s->DRAG slot=%s dx=%.1f dy=%.1f cursor=(%.0f,%.0f)",
        f->ptr_id, gesture_state_name(f->state), gesture_slot_name(mc.slot),
        dx, dy, f->travel_x, f->travel_y);
    handle_move_apply(f, ctx, result, &mc, drag_binding, drag_count);
}

// =========================================================================
// GESTURE_EVENT_UP
// =========================================================================
static void handle_up(TouchFinger* f, GestureFingerCtx* ctx,
                      TouchActionResult* restrict result, uint64_t time_ms)
{
    g_state.gesture_is_down_event = false;

    // Exit scroll mode if active on this finger
    if (f->scroll_mode) {
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
            "UP_SCROLL ptr=%d pos=(%.0f,%.0f) travel=(%.1f,%.1f) cursor=(%.0f,%.0f)",
            f->ptr_id, f->x, f->y, f->travel_x, f->travel_y, g_state.ptr_x, g_state.ptr_y);
        scroll_mode_exit(f, result);
        release_held_actions(result);
        f->state = GESTURE_STATE_IDLE;
        cleanup_main_finger(f, ctx);
        return;
    }

    // For second-finger gestures, scroll mode is on the MAIN finger.
    // When the second finger goes up, exit scroll mode on the main finger.
    if (f->is_second_finger && g_state.gesture_main_ptr_id >= 0) {
        TouchFinger* main = find_finger(g_state.gesture_main_ptr_id);
        if (main && main->scroll_mode) {
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
                "UP_2ND_SCROLL_EXIT main=%d pos=(%.0f,%.0f) cursor=(%.0f,%.0f)",
                main->ptr_id, main->x, main->y, g_state.ptr_x, g_state.ptr_y);
            scroll_mode_exit(main, result);
            release_held_actions(result);
        }
    }

    f->tap_up_x = f->x;
    f->tap_up_y = f->y;

    const GesturePairSlot* slot = select_slot_common(f, ctx, true);
    const GesturePairSlot* final_gesture_slot = select_gesture_slot(f);
    ResolvedBindings resolved = resolve_slot(f, slot);

    up_prelude_second_finger(f, result, slot);

    if (!slot->is_second_finger)
        up_handle_tp_early_exit(f, ctx, result, time_ms, resolved.non_drag, resolved.non_drag_count);

    if (f->state == GESTURE_STATE_IDLE)
        return;

    switch (f->state) {
        case GESTURE_STATE_TAP_WAITING:
            if (up_handle_tap_waiting(f, ctx, result, time_ms, slot,
                resolved.non_drag, resolved.non_drag_count))
                return;
            break;
        case GESTURE_STATE_LONG_PRESSING:
            up_handle_long_pressing(f, ctx, result);
            break;
        case GESTURE_STATE_DRAGGING:
            up_handle_dragging(f, ctx, result);
            break;
        case GESTURE_STATE_DOUBLE_TAP_WAITING:
            up_handle_dt_waiting(f, ctx, result, slot,
                resolved.non_drag, resolved.non_drag_count);
            break;
        case GESTURE_STATE_IDLE:
            break;
        default:
            up_handle_default(f, ctx, result);
            break;
    }

    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "UP ptr=%d slot=%s gesture=%s final_state=%s actions=%d pos=(%.0f,%.0f) travel=(%.1f,%.1f) cursor=(%.0f,%.0f)",
        f->ptr_id, gesture_slot_name(slot),
        final_gesture_slot ? gesture_slot_name(final_gesture_slot) : gesture_slot_name(slot),
        gesture_state_name(f->state),
        result->count, f->x, f->y, f->travel_x, f->travel_y, g_state.ptr_x, g_state.ptr_y);

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
                        TouchActionResult* restrict result, uint64_t time_ms)
{

    const GesturePairSlot* slot = select_gesture_slot(f);
    if (!slot) {
        return;
    }


    if (f->cached_has_long_press_timer
        && (f->cached_has_moved_beyond_threshold || g_state.gesture_is_action_held)) {
        f->cached_has_long_press_timer = false;
    }

    tick_lp_timer(f, ctx, result, time_ms);
    tick_s_hold_timer(f, ctx, result, time_ms, select_s_slot(f));
    tick_dt_timeout(f, ctx, result, time_ms, slot);
    tick_deferred_single_tap(f, ctx, result, time_ms);
}
