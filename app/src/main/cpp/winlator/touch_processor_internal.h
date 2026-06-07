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
#define GESTURE_TIMER_MS 50
#define MAX_TAP_TRAVEL 10
#define RANGE_TAP_TIMEOUT_MS 200
#define TAP_MAX_TIME_MS 200
#define MAX_SCROLL_FINGER_DIST 350
#define MOUSE_WHEEL_DELTA 120

// Scheduled actions (non-blocking replacement for delay_ms)
#define MAX_SCHEDULED_ACTIONS 32

typedef struct {
    uint64_t scheduled_time_ms;
    TouchBinding binding;
    int action_type;
    bool active;
} ScheduledAction;

typedef struct {
    int ptr_id;
    int element_indices[MAX_TRACKED_PER_POINTER];
    int count;
} TrackedButtons;

typedef struct {
    TouchProcessorConfig cfg;
    TouchFinger* finger_by_ptr_id[256];
    TouchFinger fingers[MAX_FINGERS];
    int finger_count;
    int active_finger_count;
    TouchElement elements[MAX_ELEMENTS];
    int element_count;
    int button_count;
    int range_count;

    float ptr_x, ptr_y;
    float scroll_accum_y;
    int main_ptr_id;
    bool passthrough_active;

    // Gesture handler state
    bool gesture_handler_active;
    int gesture_main_ptr_id;
    int gesture_second_ptr_id;
    bool gesture_second_active;
    float gesture_second_main_ref_x;
    float gesture_second_main_ref_y;
    bool gesture_post_double_tap_drag;
    bool gesture_double_tap_consumed;
    bool gesture_double_tap_waiting;
    TouchBinding gesture_deferred_tap[8];
    int gesture_deferred_tap_count;
    TouchBinding gesture_pending_double[8];
    int gesture_pending_double_count;
    TouchBinding gesture_pending_deferred_double[8];
    int gesture_pending_deferred_double_count;
    TouchBinding gesture_pending_deferred_long_press[8];
    int gesture_pending_deferred_long_press_count;
    TouchBinding gesture_held_actions[16];
    int gesture_held_count;
    bool gesture_is_action_held;

    // Second-finger double-tap state (global, survives finger deactivation between taps)
    // Mirrors Java TouchpadGestureHandler secondFingerDoubleTapWaiting, pendingSecondTapAction, etc.
    bool second_double_tap_waiting;
    TouchBinding second_tap_fallback[8];
    int second_tap_fallback_count;
    uint64_t second_tap_fallback_time;
    TouchBinding pending_second_double[8];
    int pending_second_double_count;

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
    // Spatial grid for element hit-testing
    #define GRID_COLS 8
    #define GRID_ROWS 8
    int spatial_grid[GRID_COLS * GRID_ROWS][MAX_ELEMENTS];
    int spatial_grid_count[GRID_COLS * GRID_ROWS];
    float grid_cell_w;
    float grid_cell_h;
    // Element type index lists for accelerated tick scanning
    int button_indices[MAX_ELEMENTS];
    int range_indices[MAX_ELEMENTS];
    float grid_min_x;
    float grid_min_y;

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

    // Scheduled actions queue (replaces blocking nanosleep)
    ScheduledAction scheduled_actions[MAX_SCHEDULED_ACTIONS];
    int scheduled_action_count;
    uint8_t scheduled_seq_counter;

    bool visual_state_dirty;

    int free_finger_hint;

} TouchProcessorState;

extern TouchProcessorState g_state;

// Forward declarations
void touch_finger_cache_bs(TouchFinger* f);

// --- Inline helpers ---

static inline uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// Transform raw touch coordinates to Wine screen coordinates using the configured
// view offset and scale (applied in TOUCHSCREEN mode).
static inline void touch_transform_coords(float x, float y, int* out_x, int* out_y) {
    *out_x = (int)((x - g_state.cfg.view_offset_x) * g_state.cfg.xform_scale_x);
    *out_y = (int)((y - g_state.cfg.view_offset_y) * g_state.cfg.xform_scale_y);
}

static inline TouchFinger* find_finger(int ptr_id) {
    if (ptr_id < 0 || ptr_id >= 256) return NULL;
    return g_state.finger_by_ptr_id[ptr_id];
}

static inline TouchFinger* find_free_finger(void) {
    if (g_state.free_finger_hint >= 0 && g_state.free_finger_hint < MAX_FINGERS 
        && !g_state.fingers[g_state.free_finger_hint].active)
        return &g_state.fingers[g_state.free_finger_hint];
    for (int i = 0; i < MAX_FINGERS; i++)
        if (!g_state.fingers[i].active) {
            g_state.free_finger_hint = i;
            return &g_state.fingers[i];
        }
    return NULL;
}

static inline int active_finger_count(void) {
    return g_state.active_finger_count;
}

// GestureModeBindings — mode-agnostic view of all binding arrays.
// This is the single point where TS/TP config branching is resolved.
typedef struct {
    const TouchBinding* single_tap;         int single_tap_count;
    const TouchBinding* long_press;         int long_press_count;
    const TouchBinding* double_tap;         int double_tap_count;
    const TouchBinding* single_tap_drag;    int single_tap_drag_count;
    const TouchBinding* long_press_drag;    int long_press_drag_count;
    const TouchBinding* double_tap_drag;    int double_tap_drag_count;
    const TouchBinding* single_2nd;         int single_2nd_count;
    const TouchBinding* double_2nd;         int double_2nd_count;
    const TouchBinding* single_drag_2nd;    int single_drag_2nd_count;
    const TouchBinding* double_drag_2nd;    int double_drag_2nd_count;
} GestureModeBindings;

// SINGLE place where TS vs TP binding selection happens.
static inline GestureModeBindings get_mode_bindings(void) {
    GestureModeBindings b;
    const TouchProcessorConfig* c = &g_state.cfg;
    if (c->touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        b.single_tap        = c->ts_single_tap;        b.single_tap_count        = c->ts_single_tap_count;
        b.long_press        = c->ts_long_press;        b.long_press_count        = c->ts_long_press_count;
        b.double_tap        = c->ts_double_tap;        b.double_tap_count        = c->ts_double_tap_count;
        b.single_tap_drag   = c->ts_single_tap_drag;   b.single_tap_drag_count   = c->ts_single_tap_drag_count;
        b.long_press_drag   = c->ts_long_press_drag;   b.long_press_drag_count   = c->ts_long_press_drag_count;
        b.double_tap_drag   = c->ts_double_tap_drag;   b.double_tap_drag_count   = c->ts_double_tap_drag_count;
        b.single_2nd        = c->ts_single_2nd;        b.single_2nd_count        = c->ts_single_2nd_count;
        b.double_2nd        = c->ts_double_2nd;        b.double_2nd_count        = c->ts_double_2nd_count;
        b.single_drag_2nd   = c->ts_single_drag_2nd;   b.single_drag_2nd_count   = c->ts_single_drag_2nd_count;
        b.double_drag_2nd   = c->ts_double_drag_2nd;   b.double_drag_2nd_count   = c->ts_double_drag_2nd_count;
    } else {
        b.single_tap        = c->tp_single_tap;        b.single_tap_count        = c->tp_single_tap_count;
        b.long_press        = c->tp_long_press;        b.long_press_count        = c->tp_long_press_count;
        b.double_tap        = c->tp_double_tap;        b.double_tap_count        = c->tp_double_tap_count;
        b.single_tap_drag   = c->tp_single_tap_drag;   b.single_tap_drag_count   = c->tp_single_tap_drag_count;
        b.long_press_drag   = c->tp_long_press_drag;   b.long_press_drag_count   = c->tp_long_press_drag_count;
        b.double_tap_drag   = c->tp_double_tap_drag;   b.double_tap_drag_count   = c->tp_double_tap_drag_count;
        b.single_2nd        = c->tp_single_2nd;        b.single_2nd_count        = c->tp_single_2nd_count;
        b.double_2nd        = c->tp_double_2nd;        b.double_2nd_count        = c->tp_double_2nd_count;
        b.single_drag_2nd   = c->tp_single_drag_2nd;   b.single_drag_2nd_count   = c->tp_single_drag_2nd_count;
        b.double_drag_2nd   = c->tp_double_drag_2nd;   b.double_drag_2nd_count   = c->tp_double_drag_2nd_count;
    }
    return b;
}

// Copy main-finger bindings from mode bindings (zeroes second-finger slots).
#define COPY_FROM_MODE_BINDINGS(fb_, mb_) { \
    (fb_)->single_tap = (mb_).single_tap;            (fb_)->single_tap_count = (mb_).single_tap_count; \
    (fb_)->long_press = (mb_).long_press;            (fb_)->long_press_count = (mb_).long_press_count; \
    (fb_)->double_tap = (mb_).double_tap;            (fb_)->double_tap_count = (mb_).double_tap_count; \
    (fb_)->single_tap_drag = (mb_).single_tap_drag;  (fb_)->single_tap_drag_count = (mb_).single_tap_drag_count; \
    (fb_)->long_press_drag = (mb_).long_press_drag;  (fb_)->long_press_drag_count = (mb_).long_press_drag_count; \
    (fb_)->double_tap_drag = (mb_).double_tap_drag;  (fb_)->double_tap_drag_count = (mb_).double_tap_drag_count; \
    (fb_)->single_tap_2nd = NULL;  (fb_)->single_tap_2nd_count = 0; \
    (fb_)->double_tap_2nd = NULL;  (fb_)->double_tap_2nd_count = 0; \
    (fb_)->single_tap_drag_2nd = NULL; (fb_)->single_tap_drag_2nd_count = 0; \
    (fb_)->double_tap_drag_2nd = NULL; (fb_)->double_tap_drag_2nd_count = 0; \
}

// Copy second-finger bindings from mode bindings (2nd variants map to primary slots).
#define COPY_FROM_MODE_BINDINGS_2ND(fb_, mb_) { \
    (fb_)->single_tap = (mb_).single_2nd;          (fb_)->single_tap_count = (mb_).single_2nd_count; \
    (fb_)->long_press = NULL;                      (fb_)->long_press_count = 0; \
    (fb_)->double_tap = (mb_).double_2nd;          (fb_)->double_tap_count = (mb_).double_2nd_count; \
    (fb_)->single_tap_drag = (mb_).single_drag_2nd; (fb_)->single_tap_drag_count = (mb_).single_drag_2nd_count; \
    (fb_)->long_press_drag = NULL;                 (fb_)->long_press_drag_count = 0; \
    (fb_)->double_tap_drag = (mb_).double_drag_2nd; (fb_)->double_tap_drag_count = (mb_).double_drag_2nd_count; \
    (fb_)->single_tap_2nd = NULL; (fb_)->single_tap_2nd_count = 0; \
    (fb_)->double_tap_2nd = NULL; (fb_)->double_tap_2nd_count = 0; \
    (fb_)->single_tap_drag_2nd = NULL; (fb_)->single_tap_drag_2nd_count = 0; \
    (fb_)->double_tap_drag_2nd = NULL; (fb_)->double_tap_drag_2nd_count = 0; \
}

// Set up bindings for a main finger from the active mode, zeroing single-tap-drag for TP.
static inline void setup_main_finger_bindings(FingerBindings* fb) {
    GestureModeBindings mb = get_mode_bindings();
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD) {
        mb.single_tap_drag = NULL; mb.single_tap_drag_count = 0;
        mb.single_drag_2nd = NULL; mb.single_drag_2nd_count = 0;
    }
    COPY_FROM_MODE_BINDINGS(fb, mb);
}

// Set up second-finger bindings: 2nd variants map to primary FingerBindings slots.
static inline void setup_second_finger_bindings(TouchFinger* f) {
    FingerBindings* fb = &f->bindings;
    GestureModeBindings mb = get_mode_bindings();
    COPY_FROM_MODE_BINDINGS_2ND(fb, mb);
    // TP second finger has no independent single-tap-drag.
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD) {
        fb->single_tap_drag = NULL; fb->single_tap_drag_count = 0;
    }
    touch_finger_cache_bs(f);
}

// --- Cleanup helpers (reduce repetitive zeroing patterns) ---

static inline void gesture_clear_deferred_tap(void) {
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_pending_deferred_double_count = 0;
}

static inline void gesture_clear_pending_long_press(void) {
    g_state.gesture_pending_deferred_long_press_count = 0;
}

static inline void gesture_clear_second_finger_state(void) {
    g_state.second_tap_fallback_count = 0;
    g_state.second_double_tap_waiting = false;
    g_state.pending_second_double_count = 0;
}

// Enter second-finger double-tap waiting (SDTW) — analogous to first-finger DT_WAITING.
static inline void enter_sdtw(uint64_t time_ms) {
    g_state.second_double_tap_waiting = true;
    g_state.second_tap_fallback_time = time_ms;
}

static inline bool finger_has_gesture(const TouchFinger* f) {
    return f->bindings.single_tap_count > 0
        || f->bindings.long_press_count > 0
        || f->bindings.double_tap_count > 0
        || f->bindings.single_tap_drag_count > 0
        || f->bindings.long_press_drag_count > 0
        || f->bindings.double_tap_drag_count > 0;
}

// Cached fields accessed directly via f->cached_* — no wrapper overhead needed

#define BINDING_GAMEPAD_COUNT 24

#define SWAP_F(a, b) do { float _t_ = (a); (a) = (b); (b) = _t_; } while(0)

static inline bool is_keyboard_binding(const TouchBinding* b) {
    return b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST;
}
static inline bool is_gamepad_binding(const TouchBinding* b) {
    return b->type >= BINDING_GAMEPAD_BASE && b->type < BINDING_GAMEPAD_BASE + BINDING_GAMEPAD_COUNT;
}
static inline bool is_right_stick_binding(const TouchElement* e) {
    int bt = e->bindings[0].type - BINDING_GAMEPAD_BASE;
    return bt >= 16 && bt <= 19;
}

// --- Internal function declarations ---

// Actions
void add_action(TouchActionResult* r, ActionType type, int a0, int a1, int a2);
void release_held_actions(TouchActionResult* result);
bool is_modifier_binding(const TouchBinding* b);
void execute_actions(TouchActionResult* result, const TouchBinding* actions, int count);
void execute_actions_hold(TouchActionResult* result, const TouchBinding* actions, int count);
void release_binding(TouchActionResult* result, const TouchBinding* b);
void press_binding(TouchActionResult* result, const TouchBinding* b, bool hold);

// Element shared helpers
bool point_in_element(float px, float py, const TouchElement* e);
TouchElement* hit_test_element(float x, float y);
void touch_finger_cache_bs(TouchFinger* f);
int detect_swipe_dir(float dx, float dy, float threshold);
bool is_mouse_move_binding(const TouchBinding* b);
float cubic_bezier_interpolate(float x, float cpx1, float cpy1);
float cubic_bezier_interpolate_trackpad(float x);
void element_set_petals(TouchElement* e, float nx, float ny, float dead_zone, TouchActionResult* result);
void release_element_bindings(TouchElement* e, TouchActionResult* result);
void suppress_element_gestures(TouchElement* e, TouchActionResult* result);
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
void double_tap_confirm_internal(TouchActionResult* result, const FingerBindings* fb);

static inline void release_bindings_list(TouchActionResult* result, TouchBinding* bindings, int count) {
    for (int k = count - 1; k >= 0; k--)
        if (bindings[k].type != BINDING_NONE && !is_modifier_binding(&bindings[k]))
            release_binding(result, &bindings[k]);
    for (int k = count - 1; k >= 0; k--)
        if (bindings[k].type != BINDING_NONE && is_modifier_binding(&bindings[k]))
            release_binding(result, &bindings[k]);
}
bool gesture_is_within_tap_distance(float x, float y);
void gesture_cancel_double_tap_wait(TouchActionResult* result);
void check_start_drag(TouchFinger* f, float dx, float dy, TouchActionResult* result);
void handle_tap_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms);

// Gesture — entry points (touch_processor_gesture.c)
void touchpad_finger_down(TouchFinger* f, TouchActionResult* result, uint64_t time_ms);
void touchpad_finger_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms);

// Unified gesture handler (replaces both touchscreen.c and touchpad.c)
// The only difference between TOUCHPAD and TOUCHSCREEN modes is cursor behavior:
//   TOUCHSCREEN - cursor attached absolutely to first finger
//   TOUCHPAD    - relative cursor movement with delta accumulation
void handle_gesture_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);
void handle_gesture_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);
void handle_gesture_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result);

// --- Activation internal helpers ---
bool activation_handle_down(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void activation_handle_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
bool activation_handle_up(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);
void activation_reset(void);

// Scheduled actions
void process_scheduled_actions(TouchActionResult* result, uint64_t time_ms);

// Spatial grid
void build_spatial_grid(void);

#endif // TOUCH_PROCESSOR_INTERNAL_H
