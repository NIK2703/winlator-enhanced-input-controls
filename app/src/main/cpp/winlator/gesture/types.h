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

// Gesture handler tick (processes long-press, single-tap-hold, double-tap, second-finger timeouts)
void gesture_tick(uint64_t time_ms, TouchActionResult* result);

// Gesture handler state machine
void on_drag_start(TouchFinger* f);
bool gesture_is_within_tap_distance(float x, float y);
void gesture_cancel_double_tap_wait(TouchActionResult* result);
void check_start_drag(TouchFinger* f, float dx, float dy, TouchActionResult* result);
void handle_tap_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms);

#endif // TOUCH_PROCESSOR_GESTURE_TYPES_H
