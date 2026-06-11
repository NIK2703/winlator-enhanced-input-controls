#ifndef TOUCH_PROCESSOR_GESTURE_TYPES_H
#define TOUCH_PROCESSOR_GESTURE_TYPES_H

#include "../touch_processor.h"

// GestureBindingSet — precomputed boolean flags (mirrors Java GestureHandler.BindingSet)
typedef struct {
    bool has_active_single_tap;
    bool has_active_double_tap;
    bool has_active_long_press;
    bool has_active_single_tap_drag;
    bool has_active_long_press_drag;
    bool has_active_double_tap_drag;
    bool has_long_press_timer;

} GestureBindingSet;

// Build a GestureBindingSet from a FingerBindings struct (mirrors Java buildBindingSet())
static inline GestureBindingSet gesture_build_binding_set(const FingerBindings* fb) {
    GestureBindingSet s;
    s.has_active_single_tap = fb->single_tap_count > 0;
    s.has_active_double_tap = fb->double_tap_count > 0;
    s.has_active_long_press = fb->long_press_count > 0;
    s.has_active_single_tap_drag = fb->single_tap_drag_count > 0;
    s.has_active_long_press_drag = fb->long_press_drag_count > 0;
    s.has_active_double_tap_drag = fb->double_tap_drag_count > 0;
    s.has_long_press_timer = s.has_active_long_press || s.has_active_long_press_drag;
    return s;
}

// ---- Unified gesture pair branching ----

// Describes how a non-drag/drag gesture pair should behave.
// Hold vs tap is determined per-binding by TouchBinding.modifiers (sticky flag).
typedef struct {
    int  hold_delay_ms;     // > 0: start timer; on expiry: execute
    bool pulse_on_up;       // true: on finger-up execute the non-drag binding
    bool press_on_drag;     // true: on drag threshold crossing, press down non-drag (then release on up)
    bool drag_available;    // true: drag variant exists
} GesturePairPlan;

typedef struct {
    bool has_x;
    bool has_xd;
    bool has_competing_dt;
    bool has_competing_lp;
    bool is_ts;
    bool is_second;
    int  hold_delay_ms;
} GestureBranchParams;

// Build params for a specific gesture pair.
static inline GestureBranchParams gesture_branch_params(
    bool has_x, bool has_xd,
    bool has_competing_dt, bool has_competing_lp,
    bool is_ts, bool is_second,
    int hold_delay_ms
) {
    GestureBranchParams p;
    p.has_x = has_x; p.has_xd = has_xd;
    p.has_competing_dt = has_competing_dt; p.has_competing_lp = has_competing_lp;
    p.is_ts = is_ts; p.is_second = is_second;
    p.hold_delay_ms = hold_delay_ms;
    return p;
}

// Unified decision for ANY non-drag/drag gesture pair.
// When competing gestures exist for a pair, the non-drag action is deferred:
// fired on drag threshold crossing or on trigger-release timeout (pulse).
// Without competition, the action fires immediately (or after hold_delay_ms in TS).
// Hold vs tap is determined per-binding by TouchBinding.modifiers.
static inline GesturePairPlan gesture_decide_branch(GestureBranchParams bp) {
    GesturePairPlan p = {0};
    p.drag_available = bp.has_xd;

    if (!bp.has_x) return p;

    if (bp.has_x && !bp.has_xd) {
        if (bp.has_competing_dt || bp.has_competing_lp) {
            p.press_on_drag = true;
            p.pulse_on_up = true;
        } else if (bp.is_ts && bp.hold_delay_ms > 0 && !bp.is_second) {
            p.hold_delay_ms = bp.hold_delay_ms;
        } else {
            // S-only, no competition, no hold: execute immediately (caller will hold)
            // execute immediately
        }
        return p;
    }

    p.pulse_on_up = true;
    return p;
}

// Gesture handler tick (processes long-press, single-tap-hold, double-tap, second-finger timeouts)
void gesture_tick(uint64_t time_ms, TouchActionResult* restrict result);

// Gesture handler state machine
void on_drag_start(TouchFinger* f, TouchActionResult* restrict result);
bool gesture_is_within_tap_distance(float x, float y);
void gesture_cancel_double_tap_wait(TouchActionResult* restrict result);
void double_tap_confirm_internal(TouchActionResult* restrict result, TouchFinger* f);

// Unified tap execution helpers (shared across base.c, entry.c, handler.c)
void execute_tap_on_finger_down(TouchFinger* f, TouchActionResult* restrict result,
    uint64_t time_ms, GesturePairPlan plan, bool force_hold,
    const TouchBinding* x, int x_cnt);
bool confirm_double_tap(TouchActionResult* restrict result,
    GesturePairPlan d_plan,
    const TouchBinding* src, int src_count,
    TouchBinding* dst, int* dst_count, int dst_max,
    bool* out_post_dtd);
bool resolve_drag_binding(
    const TouchBinding* xd, int xd_count,
    const TouchBinding* x,  int x_count,
    const TouchBinding* fb, int fb_count,
    bool press_on_drag,
    bool use_fallback,
    const TouchBinding** out_binding,
    int* out_count);
void enter_double_tap_waiting(TouchFinger* f, uint64_t time_ms,
    const TouchBinding* deferred_single, int deferred_single_count,
    const TouchBinding* deferred_double, int deferred_double_count);

// ============================================================
// Unified per-finger gesture context
// Replaces all per-finger global arrays (main vs second finger)
// ============================================================
typedef struct {
    int  hold_delay_ms;
    uint64_t hold_timer;
    bool single_tap_deferred;
    uint64_t single_tap_deferred_time;
    bool post_double_tap_drag;
    bool dt_consumed;
    bool dt_waiting;
    uint64_t dt_wait_start_time;
    float last_tap_up_x, last_tap_up_y;
    bool deferred_second_finger_tap;

    TouchBinding fallback[8];
    int fallback_count;

    TouchBinding pending_double[8];
    int pending_double_count;

    TouchBinding deferred_double[8];
    int deferred_double_count;

    TouchBinding pending_long_press[8];
    int pending_long_press_count;

    int original_ptr_id;
    bool double_tap_original_id_set;
    bool is_second_finger;
} GestureFingerCtx;

// ============================================================
// Gesture event type for unified process function
// ============================================================
typedef enum {
    GESTURE_EVENT_DOWN,
    GESTURE_EVENT_MOVE,
    GESTURE_EVENT_UP,
    GESTURE_EVENT_TICK
} GestureProcessEvent;

// ============================================================
// Gesture pair slot — encodes ALL differences between gesture
// pairs (S/Sd, D/Dd, L/Ld, S2/Sd2, D2/Dd2) as data.
// ============================================================
typedef struct {
    GestureType bindings_non_drag;
    GestureType bindings_drag;
    GestureType tp_non_drag;
    GestureType tp_drag;
    GestureType comp_dt;
    GestureType comp_lp;
    bool is_second_finger;
    bool has_comp_dt;
    bool has_comp_lp;

    // Unified behavioral flags — ELIMINATE is_second_finger branches
    bool down_save_fallback;     // DOWN: save non-drag as fallback
    bool move_agg_tp_delta;      // MOVE: aggregate TP main-finger delta
    bool move_hold_check;        // MOVE: check held-action w/ SD+Dd before drag
    bool move_resolve_fb;        // MOVE: use ctx->fallback in drag resolution
    bool move_press_always;      // MOVE: press_on_drag unconditional
    bool move_gate_tp_nodrag;    // MOVE: TP gate when no drag bindings
    bool down_hold_always;       // DOWN: always hold non-drag (bypass plan, second finger S2)
    bool tick_s_execute;         // TICK: S hold -> execute_actions (not hold)
    bool tick_dt_uses_fb;        // TICK: DT timeout uses fallback
} GesturePairSlot;

// Per-finger context array (defined in unified.c)
extern GestureFingerCtx g_ctx[MAX_FINGERS];

// Init per-finger context
void g_ctx_init(GestureFingerCtx* ctx, bool is_second);

// ---- Unified entry point (defined in unified.c) ----
void gesture_process_finger(
    TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* result, uint64_t time_ms,
    GestureProcessEvent event,
    float x, float y);

// ---- Unified slot / binding / plan helpers (defined in unified.c) ----
const GesturePairSlot* select_gesture_slot(const TouchFinger* f);
void resolve_binding_slot(
    const TouchFinger* f,
    const GesturePairSlot* slot,
    const TouchBinding** non_drag, int* non_drag_count,
    const TouchBinding** drag, int* drag_count);
GesturePairPlan resolve_gesture_pair(
    const TouchFinger* f,
    const GesturePairSlot* slot);

#endif // TOUCH_PROCESSOR_GESTURE_TYPES_H
