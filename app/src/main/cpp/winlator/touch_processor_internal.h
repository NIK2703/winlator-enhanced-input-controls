#ifndef TOUCH_PROCESSOR_INTERNAL_H
#define TOUCH_PROCESSOR_INTERNAL_H

#include "touch_processor.h"
#include "gesture/types.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#define MAX_TRACKED_PER_POINTER 8
#define TWO_FINGER_SCROLL_DIST 350
#define SCROLL_ACCUM_THRESHOLD 100
#define CLICK_DELAY_MS 50
#define MAX_TAP_TRAVEL 10
#define RANGE_TAP_TIMEOUT_MS 200
#define TAP_MAX_TIME_MS 200
#define MAX_SCROLL_FINGER_DIST 350
#define MOUSE_WHEEL_DELTA 120

typedef struct {
    int ptr_id;
    int element_indices[MAX_TRACKED_PER_POINTER];
    int count;
} TrackedButtons;

typedef struct {
    TouchProcessorConfig cfg;
    TouchFinger fingers[MAX_FINGERS];
    int finger_count;
    TouchElement elements[MAX_ELEMENTS];
    int element_count;

    float ptr_x, ptr_y;
    float scroll_accum_y;
    int main_ptr_id;
    bool passthrough_active;

    // Gesture handler state
    bool gesture_handler_active;
    int gesture_main_ptr_id;
    int gesture_second_ptr_id;
    bool gesture_second_active;
    bool gesture_post_double_tap_drag;
    bool gesture_double_tap_consumed;
    bool gesture_double_tap_waiting;
    TouchBinding gesture_deferred_tap[8];
    int gesture_deferred_tap_count;
    TouchBinding gesture_pending_double[8];
    int gesture_pending_double_count;
    TouchBinding gesture_pending_deferred_double[8];
    int gesture_pending_deferred_double_count;
    TouchBinding gesture_held_actions[16];
    int gesture_held_count;
    bool gesture_is_action_held;

    bool second_double_tap_waiting;
    int second_double_tap_ptr_id;
    TouchBinding second_tap_fallback[8];
    int second_tap_fallback_count;

    bool long_tap_mode;

    // TouchActivationMode state
    TrackedButtons tracked[MAX_FINGERS];
    int hovered_element_per_ptr[MAX_FINGERS];

    // simTouchScreen
    bool sim_touch_screen;
    int last_touch_x, last_touch_y;

    // Deferred second-finger tap flag (for touchscreen — survives finger deactivation, matches Java deferredSecondFingerTap)
    bool gesture_deferred_second_finger_tap;

    // Double-tap tracking (global, survives finger deactivation)
    float gesture_last_tap_up_x, gesture_last_tap_up_y;
    uint64_t gesture_double_tap_start_time;

    // Two-finger touchpad
    bool scrolling;
    bool pointer_left_enabled;
    bool pointer_right_enabled;
    int finger_pointer_left;
    int finger_pointer_right;

    // Snapping size for element layout
    float snapping_size;
    float resolution_scale;

    // Delayed action mechanism (matching Java postDelayed)
    // 30ms delayed pointer button release (TouchpadView.releasePointerButtonLeft/Right)
    uint64_t pending_left_release_time;
    int pending_left_release_ptr_id;
    uint64_t pending_right_release_time;
    int pending_right_release_ptr_id;

    // simTouchScreen delayed click (50ms CLICK_DELAYED_TIME)
    uint64_t sim_click_press_time;
    bool sim_continue_click;
    int sim_click_ptr_id;
    uint64_t sim_click_release_time;

    // Mouse move timer state
    int last_mouse_move_dx;
    int last_mouse_move_dy;
    int last_mouse_move_action; // 0=none, 1=started
} TouchProcessorState;

extern TouchProcessorState g_state;

// --- Inline helpers ---

static inline uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static inline TouchFinger* find_finger(int ptr_id) {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (g_state.fingers[i].active && g_state.fingers[i].ptr_id == ptr_id)
            return &g_state.fingers[i];
    return NULL;
}

static inline TouchFinger* find_free_finger(void) {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (!g_state.fingers[i].active) return &g_state.fingers[i];
    return NULL;
}

static inline int active_finger_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_FINGERS; i++) if (g_state.fingers[i].active) n++;
    return n;
}

// Macro to copy finger binding arrays from config (replaces 24 repetitive memcpy lines).
// fb: FingerBindings* pointer, cfg: TouchProcessorConfig*, pfx: config prefix (ts or tp).
#define COPY_SINGLE_BINDING(fb_, cfg_, fb_field_, cfg_field_) \
    memcpy((fb_)->fb_field_, (cfg_)->cfg_field_, sizeof((cfg_)->cfg_field_)); \
    (fb_)->fb_field_##_count = (cfg_)->cfg_field_##_count;

#define COPY_FINGER_BINDINGS(fb_, cfg_, pfx_) { \
    COPY_SINGLE_BINDING(fb_, cfg_, single_tap, pfx_##_single_tap); \
    COPY_SINGLE_BINDING(fb_, cfg_, long_press, pfx_##_long_press); \
    COPY_SINGLE_BINDING(fb_, cfg_, double_tap, pfx_##_double_tap); \
    COPY_SINGLE_BINDING(fb_, cfg_, single_tap_drag, pfx_##_single_tap_drag); \
    COPY_SINGLE_BINDING(fb_, cfg_, long_press_drag, pfx_##_long_press_drag); \
    COPY_SINGLE_BINDING(fb_, cfg_, double_tap_drag, pfx_##_double_tap_drag); \
    COPY_SINGLE_BINDING(fb_, cfg_, single_tap_2nd, pfx_##_single_2nd); \
    COPY_SINGLE_BINDING(fb_, cfg_, long_press_2nd, pfx_##_long_2nd); \
    COPY_SINGLE_BINDING(fb_, cfg_, double_tap_2nd, pfx_##_double_2nd); \
    COPY_SINGLE_BINDING(fb_, cfg_, single_tap_drag_2nd, pfx_##_single_drag_2nd); \
    COPY_SINGLE_BINDING(fb_, cfg_, long_press_drag_2nd, pfx_##_long_drag_2nd); \
    COPY_SINGLE_BINDING(fb_, cfg_, double_tap_drag_2nd, pfx_##_double_drag_2nd); \
}

// Legacy convenience wrappers (kept for backward compat — delegate to GestureBindingSet)
static inline bool has_active_double_tap(const FingerBindings* fb) { return gesture_build_binding_set(fb).has_active_double_tap; }
static inline bool has_active_long_press(const FingerBindings* fb) { return gesture_build_binding_set(fb).has_active_long_press; }
static inline bool has_active_single_tap(const FingerBindings* fb) { return gesture_build_binding_set(fb).has_active_single_tap; }
static inline bool has_active_single_tap_drag(const FingerBindings* fb) { return gesture_build_binding_set(fb).has_active_single_tap_drag; }
static inline bool has_active_long_press_drag(const FingerBindings* fb) { return gesture_build_binding_set(fb).has_active_long_press_drag; }
static inline bool has_active_double_tap_drag(const FingerBindings* fb) { return gesture_build_binding_set(fb).has_active_double_tap_drag; }
static inline bool can_hold_long_press(const FingerBindings* fb) { return gesture_build_binding_set(fb).can_hold_long_press; }
static inline bool has_long_press_timer(const FingerBindings* fb) { return gesture_build_binding_set(fb).has_long_press_timer; }

#define BINDING_GAMEPAD_COUNT 24
static inline bool is_keyboard_binding(const TouchBinding* b) {
    return b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST;
}
static inline bool is_gamepad_binding(const TouchBinding* b) {
    return b->type >= BINDING_GAMEPAD_BASE && b->type < BINDING_GAMEPAD_BASE + BINDING_GAMEPAD_COUNT;
}

// --- Internal function declarations ---

// Actions
void add_action(TouchActionResult* r, ActionType type, int a0, int a1, int a2);
void release_held_actions(TouchActionResult* result);
bool is_modifier_binding(const TouchBinding* b);
void hold_actions(TouchActionResult* result, const TouchBinding* actions, int count);
void execute_actions(TouchActionResult* result, const TouchBinding* actions, int count);
void release_binding(TouchActionResult* result, const TouchBinding* b);
void press_binding(TouchActionResult* result, const TouchBinding* b, bool hold);

// Element shared helpers
bool point_in_element(float px, float py, const TouchElement* e);
TouchElement* hit_test_element(float x, float y);
void touch_finger_cache_bs(TouchFinger* f);
int detect_swipe_dir(float dx, float dy, float threshold);
bool is_mouse_move_binding(const TouchBinding* b);
float cubic_bezier_interpolate(float x, float cpx1, float cpy1);
void release_element_bindings(TouchElement* e, TouchActionResult* result);
bool finger_has_engaged_element(int ptr_id);

// Element dispatchers
void handle_element_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void handle_element_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);
void handle_element_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);

// Per-type element handlers
void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);

void element_dpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_dpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_dpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);

void element_stick_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_stick_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_stick_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);

void element_trackpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_trackpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_trackpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);

void element_range_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_range_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);
void element_range_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result);
int range_keycode(int ordinal, int index);

// Gesture — base (maps to GestureHandler.java)
void on_drag_start(TouchFinger* f);
bool gesture_is_within_tap_distance(float x, float y);
void gesture_cancel_double_tap_wait(TouchActionResult* result);
void check_start_drag(TouchFinger* f, float dx, float dy, uint64_t time_ms, TouchActionResult* result);
void handle_tap_up(TouchFinger* f, TouchActionResult* result);

// Gesture — entry points (touch_processor_gesture.c)
void touchpad_finger_down(TouchFinger* f, TouchActionResult* result);
void touchpad_finger_up(TouchFinger* f, TouchActionResult* result);

// Touchscreen mode handler (maps to TouchscreenGestureHandler.java)
void handle_touchscreen_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);
void handle_touchscreen_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);
void handle_touchscreen_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);

// Touchpad mode handler (maps to TouchpadGestureHandler.java)
void handle_touchpad_down(TouchFinger* f, float x, float y, TouchActionResult* result);
void handle_touchpad_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);
void handle_touchpad_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);

#endif // TOUCH_PROCESSOR_INTERNAL_H
