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
    bool can_hold_long_press;
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
    s.can_hold_long_press = false;
    if (s.has_active_long_press) {
        int t = fb->long_press[0].type;
        bool has_non_holdable = (t == BINDING_MOUSE_SCROLL_UP || t == BINDING_MOUSE_SCROLL_DOWN
                              || (t >= BINDING_MOUSE_MOVE_LEFT && t <= BINDING_MOUSE_MOVE_DOWN));
        s.can_hold_long_press = !has_non_holdable;
    }
    s.has_long_press_timer = s.has_active_long_press || s.has_active_long_press_drag;
    return s;
}

// ---- Unified gesture pair branching ----

// Describes how a non-drag/drag gesture pair should behave.
typedef struct {
    int  hold_delay_ms;     // > 0: start timer; on expiry: hold (press)
    bool hold_now;          // true: hold_actions immediately (finger-down/trigger)
    bool pulse_on_up;       // true: on finger-up execute (down+up) the non-drag binding
    bool press_on_drag;     // true: on drag threshold crossing, press down non-drag (then release on up)
    bool drag_available;    // true: drag variant exists
} GesturePairPlan;

// Unified decision for ANY non-drag/drag gesture pair.
// Used for S/Sd, D/Dd, L/Ld — both one-finger and two-finger variants.
//
// Parameters:
//   has_x              - non-drag variant exists (single_tap / double_tap / long_press)
//   has_xd             - drag variant exists
//   has_competing_dt   - D or Dd is configured (affects S branching decision)
//   has_competing_lp   - L or Ld is configured (affects S branching decision)
//   can_hold_x         - whether non-drag can be held as press+hold
//                        (true for S/D; for L: false for scroll/mouse-move)
//   is_ts              - touchscreen mode
//   is_second          - second finger
//   hold_delay_ms      - delay before hold (S: single_tap_delay_ms; D/L: 0)
//   is_single_tap_pair - true for S/Sd; false for D/Dd, L/Ld.
//                        In TP mode, single-tap pairs pulse on up (click),
//                        while D/L pairs hold (double-tap-drag / long-press-drag).
//
// When competing gestures exist for a pair (only relevant for S/Sd),
// the non-drag action is deferred: fired on drag threshold crossing
// (press down, release on up) or on trigger-release timeout (pulse).
// Without competition, non-holdable actions pulse on trigger;
// holdable actions hold immediately (or after a delay in TS mode).
// When both non-drag and drag variants exist, non-drag falls back
// to pulse on trigger-up, while drag drives on threshold.
static inline GesturePairPlan gesture_decide_branch(
    bool has_x, bool has_xd,
    bool has_competing_dt,
    bool has_competing_lp,
    bool can_hold_x,
    bool is_ts,
    bool is_second,
    int hold_delay_ms,
    bool is_single_tap_pair
) {
    GesturePairPlan p = {0};
    p.drag_available = has_xd;

    if (!has_x && !has_xd) return p;
    if (!has_x && has_xd) return p;

    // Only non-drag variant
    if (has_x && !has_xd) {
        if (has_competing_dt || has_competing_lp) {
            // Competing gestures — defer all action.
            // Non-drag fires on:
            //   1) DT timeout after finger-up (via DT_WAITING)
            //   2) Drag threshold crossing (press, then release on up)
            //   3) Finger-up before timeout (pulse)
            p.press_on_drag = true;
            p.pulse_on_up = true;
        } else if (can_hold_x) {
            // No competition, holdable
            if (is_ts) {
                if (hold_delay_ms > 0 && !is_second) {
                    p.hold_delay_ms = hold_delay_ms;
                } else {
                    p.hold_now = true;
                }
            } else if (is_single_tap_pair && !is_second) {
                // TP S (first finger): pulse on trigger-up (single click)
                p.pulse_on_up = true;
            } else if (is_single_tap_pair) {
                // TP S2 (second finger): hold (enable drag, matches TS behavior)
                p.hold_now = true;
            } else {
                // TP D/L: hold (double-tap-hold / long-press-hold)
                p.hold_now = true;
            }
        } else {
            // Non-holdable (scroll, mouse-move) — execute on trigger
            p.pulse_on_up = true;
        }
        return p;
    }

    // Both non-drag and drag exist
    p.pulse_on_up = true;  // non-drag falls back to pulse on trigger-up
    return p;
}

// Gesture handler tick (processes long-press, single-tap-hold, double-tap, second-finger timeouts)
void gesture_tick(uint64_t time_ms, TouchActionResult* result);

// Gesture handler state machine
void on_drag_start(TouchFinger* f);
bool gesture_is_within_tap_distance(float x, float y);
void gesture_cancel_double_tap_wait(TouchActionResult* result);
void check_start_drag(TouchFinger* f, float dx, float dy, TouchActionResult* result);
void handle_tap_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms);

#endif // TOUCH_PROCESSOR_GESTURE_TYPES_H
