#ifndef TOUCH_PROCESSOR_GESTURE_TYPES_H
#define TOUCH_PROCESSOR_GESTURE_TYPES_H

#include "../touch_processor.h"

// ---- Unified gesture pair branching ----

// Describes how a non-drag/drag gesture pair should behave.
// Hold vs tap is determined per-binding by TouchBinding.modifiers (sticky flag).
typedef struct {
    uint16_t hold_delay_ms;  // > 0: start timer; on expiry: execute
    bool pulse_on_up;        // true: on finger-up execute the non-drag binding
    bool press_on_drag;      // true: on drag threshold crossing, press down non-drag
    bool drag_available;     // true: drag variant exists
} GesturePairPlan;

typedef struct {
    bool has_x;
    bool has_xd;
    bool has_competing_dt;
    bool has_competing_lp;
    bool is_ts;
    bool is_second;
    uint16_t hold_delay_ms;
} GestureBranchParams;

// Build params for a specific gesture pair.
static inline GestureBranchParams gesture_branch_params(
    bool has_x, bool has_xd,
    bool has_competing_dt, bool has_competing_lp,
    bool is_ts, bool is_second,
    uint16_t hold_delay_ms
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

    if (!bp.has_xd) {
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

// ---- D/Dd and LP/LPd fast path (no competing gestures, no hold) ----
static inline GesturePairPlan resolve_pair_no_compete(bool has_x, bool has_xd) {
    GesturePairPlan p = {0};
    p.drag_available = has_xd;
    if (has_x && has_xd)
        p.pulse_on_up = true;
    return p;
}

// Gesture handler tick (processes long-press, single-tap-hold, double-tap, second-finger timeouts)
void gesture_tick(uint64_t time_ms, TouchActionResult* restrict result);

// Gesture handler state machine
void on_drag_start(TouchFinger* f);
bool gesture_is_within_tap_distance(float x, float y);
void gesture_cancel_double_tap_wait(TouchActionResult* restrict result);
void check_start_drag(TouchFinger* f, float dx, float dy, TouchActionResult* restrict result);
void handle_tap_up(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms);
void double_tap_confirm_internal(TouchActionResult* restrict result, TouchFinger* f);

// Unified tap execution helpers (shared across base.c, entry.c, handler.c)
void execute_tap_on_finger_down(TouchFinger* f, TouchActionResult* restrict result,
    uint64_t time_ms, GesturePairPlan plan, bool force_hold);
bool confirm_double_tap(TouchActionResult* restrict result,
    GesturePairPlan d_plan,
    const TouchBinding* src, int src_count,
    TouchBinding* dst, int* dst_count, int dst_max,
    bool has_dt_drag, bool* out_post_dtd);

#endif // TOUCH_PROCESSOR_GESTURE_TYPES_H
