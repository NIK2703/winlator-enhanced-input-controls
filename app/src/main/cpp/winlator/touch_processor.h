#ifndef TOUCH_PROCESSOR_H
#define TOUCH_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FINGERS 8
#define MAX_ELEMENTS 128
#define MAX_PETALS 4
#define STICK_DEAD_ZONE 0.15f
#define DPAD_DEAD_ZONE 0.3f
#define STICK_SENSITIVITY 2.0f
#define TRACKPAD_MIN_SPEED 0.8f
#define TRACKPAD_MAX_SPEED 20.0f
#define BUTTON_MIN_KEEP_PRESSED_MS 300

// --- Binding types ---
typedef enum {
    BINDING_NONE = 0,
    // Mouse buttons
    BINDING_MOUSE_LEFT, BINDING_MOUSE_RIGHT, BINDING_MOUSE_MIDDLE,
    BINDING_MOUSE_BUTTON4, BINDING_MOUSE_BUTTON5,
    BINDING_MOUSE_SCROLL_UP, BINDING_MOUSE_SCROLL_DOWN,
    BINDING_MOUSE_MOVE_LEFT, BINDING_MOUSE_MOVE_RIGHT,
    BINDING_MOUSE_MOVE_UP, BINDING_MOUSE_MOVE_DOWN,
    // Keyboard — stored as raw X11 keycode range
    BINDING_KEYBOARD_FIRST = 0x100,
    BINDING_KEYBOARD_LAST = 0x400,
    // Gamepad — stored as ordinal offset from GAMEPAD_BUTTON_A (0x500 + index)
    BINDING_GAMEPAD_BASE = 0x500,
} BindingType;

typedef struct {
    BindingType type;
    int keycode;  // X11 keycode for keyboard bindings
    int modifiers; // modifier mask for keyboard bindings
} TouchBinding;

// --- Element types ---
typedef enum {
    ELEM_BUTTON, ELEM_DPAD, ELEM_RANGE_BUTTON, ELEM_STICK, ELEM_TRACKPAD
} ElementType;

typedef enum {
    SHAPE_CIRCLE, SHAPE_RECT
} ElementShape;

typedef enum {
    ACTIVATION_LOCK, ACTIVATION_TRACK, ACTIVATION_HOVER
} ActivationMode;

// --- Element state ---
typedef struct {
    ElementType type;
    ElementShape shape;
    int current_ptr_id;
    uint64_t down_time_ms;
    bool engaged;
    bool long_press_arm;
    bool gesture_long_press_triggered;
    int range_ordinal;
    int range_index;
    int element_long_press_count;
    TouchBinding element_long_press[8];
    TouchBinding bindings[4];

    int x, y;
    float w, h;
    float hw, hh;
    float scale;
    bool passthrough_touch;
    ActivationMode activation_mode;
    float down_x, down_y;
    bool petal_active[MAX_PETALS];
    float stick_value_x, stick_value_y;
    float trackpad_last_x, trackpad_last_y;
    float trackpad_vel_x, trackpad_vel_y;
    uint64_t trackpad_last_time;
    int range_max;
    int range_binding_count;
    int range_orientation;
    float range_scroll_offset;
    float range_current_offset;
    float range_last_position;
    bool range_scrolling;
    bool range_has_binding;
    bool range_hold_pressed;
    bool range_pending_tap_release;
    uint64_t range_tap_release_time;
    int range_initial_kc;
    TouchBinding element_gesture[8];  int element_gesture_count;

    bool toggle_switch;
    bool selected;
    int button_long_press_haptic;
    int button_gesture_haptic;
    int gesture_swipe_direction;
    bool gesture_swipe_triggered;
    bool gesture_timer_armed;
    bool gesture_suppressed;

    // Auto-repeat (toggle primary binding at configured rate while held)
    bool auto_repeat;
    int auto_repeat_interval_ms;
    uint64_t auto_repeat_last_time;
    bool auto_repeat_primary_pressed;

    bool visual_active;
    float visual_x;
    float visual_y;
} TouchElement;

// --- Gesture handler types ---
typedef enum {
    GESTURE_STATE_IDLE,
    GESTURE_STATE_TAP_WAITING,
    GESTURE_STATE_TOUCHING,
    GESTURE_STATE_DOUBLE_TAP_WAITING,
    GESTURE_STATE_LONG_PRESSING,
    GESTURE_STATE_DRAGGING
} GestureState;

typedef struct {
    // 12 binding lists as flat arrays (first binding is the action, then modifiers)
    TouchBinding single_tap[8];
    int single_tap_count;
    TouchBinding long_press[8];
    int long_press_count;
    TouchBinding double_tap[8];
    int double_tap_count;
    TouchBinding single_tap_drag[8];
    int single_tap_drag_count;
    TouchBinding long_press_drag[8];
    int long_press_drag_count;
    TouchBinding double_tap_drag[8];
    int double_tap_drag_count;
    TouchBinding single_tap_2nd[8];
    int single_tap_2nd_count;
    TouchBinding double_tap_2nd[8];
    int double_tap_2nd_count;
    TouchBinding single_tap_drag_2nd[8];
    int single_tap_drag_2nd_count;
    TouchBinding double_tap_drag_2nd[8];
    int double_tap_drag_2nd_count;
} FingerBindings;

typedef struct {
    int ptr_id;
    float x, y;
    float down_x, down_y;
    uint64_t down_time_ms;
    uint64_t last_move_time_ms;
    float last_x, last_y;
    float travel_x, travel_y;
    bool active;
    bool is_tap;
    GestureState state;
    FingerBindings bindings;
    // For second-finger gesture handling
    bool is_second_finger;
    // Held actions
    TouchBinding held_actions[16];
    int held_actions_count;
    // Double-tap state
    uint64_t last_tap_up_time;
    float tap_up_x, tap_up_y;
    bool double_tap_waiting;
    TouchBinding deferred_tap[8];
    int deferred_tap_count;
    TouchBinding pending_double[8];
    int pending_double_count;
    uint32_t bindings_generation;

    // Second-finger resume (touchscreen)
    TouchBinding pending_resume_action[8];
    int pending_resume_action_count;
    int original_ptr_id;            // finger identity for double-tap continuity
    bool double_tap_original_id_set;

    // Modifier tracking
    TouchBinding held_modifiers[8];
    int held_modifiers_count;

    // Single-tap hold timer (touchscreen finger-down hold)
    int single_tap_hold_delay_ms;
    uint64_t single_tap_hold_timer;

    // Deferred single-tap (replaces nanosleep)
    bool single_tap_deferred;
    uint64_t single_tap_deferred_time;

    // Cached GestureBindingSet fields (recomputed after bindings change, avoids rebuilding per call)
    bool cached_has_active_single_tap;
    bool cached_has_active_double_tap;
    bool cached_has_active_long_press;
    bool cached_has_active_single_tap_drag;
    bool cached_has_active_long_press_drag;
    bool cached_has_active_double_tap_drag;
    bool cached_can_hold_long_press;
    bool cached_has_long_press_timer;
    bool cached_has_moved_beyond_threshold;
} TouchFinger;

typedef enum {
    TOUCH_MODE_TOUCHPAD,        // relative cursor
    TOUCH_MODE_TOUCHSCREEN,     // absolute with gestures
    TOUCH_MODE_STYLUS
} TouchMode;

typedef enum {
    INPUT_RELATIVE, INPUT_ABSOLUTE
} InputMode;

// --- Configuration ---
typedef struct {
    TouchMode touch_mode;
    InputMode input_mode;

    // Gesture timing
    int long_press_timeout_ms;
    int double_tap_timeout_ms;
    int single_tap_delay_ms;
    int drag_threshold_px;
    int gesture_threshold_px;      // separate threshold for element swipe detection
    int double_tap_distance_px;
    int binding_delay_ms;
    int long_press_delay_ms;
    int cursor_speed;
    int cursor_acceleration_threshold;  // delta above which acceleration applies
    float cursor_acceleration_factor;   // multiplier (e.g. 1.25f)

    // For touchpad absolute mode
    int screen_w, screen_h;

    // Xform scale + view offset (maps view-pixels to Wine-screen-pixels, for touchscreen absolute)
    float xform_scale_x;
    float xform_scale_y;
    float view_offset_x;
    float view_offset_y;

    // Haptic
    int gesture_long_press_haptic;
    bool haptic_enabled;

    // Gesture bindings — touchscreen
    TouchBinding ts_single_tap[8];       int ts_single_tap_count;
    TouchBinding ts_long_press[8];       int ts_long_press_count;
    TouchBinding ts_double_tap[8];       int ts_double_tap_count;
    TouchBinding ts_single_tap_drag[8];  int ts_single_tap_drag_count;
    TouchBinding ts_long_press_drag[8];  int ts_long_press_drag_count;
    TouchBinding ts_double_tap_drag[8];  int ts_double_tap_drag_count;
    // Second finger
    TouchBinding ts_single_2nd[8];       int ts_single_2nd_count;
    TouchBinding ts_double_2nd[8];       int ts_double_2nd_count;
    TouchBinding ts_single_drag_2nd[8];  int ts_single_drag_2nd_count;
    TouchBinding ts_double_drag_2nd[8];  int ts_double_drag_2nd_count;

    // Gesture bindings — touchpad
    TouchBinding tp_single_tap[8];       int tp_single_tap_count;
    TouchBinding tp_long_press[8];       int tp_long_press_count;
    TouchBinding tp_double_tap[8];       int tp_double_tap_count;
    TouchBinding tp_single_tap_drag[8];  int tp_single_tap_drag_count;
    TouchBinding tp_long_press_drag[8];  int tp_long_press_drag_count;
    TouchBinding tp_double_tap_drag[8];  int tp_double_tap_drag_count;
    TouchBinding tp_single_2nd[8];       int tp_single_2nd_count;
    TouchBinding tp_double_2nd[8];       int tp_double_2nd_count;
    TouchBinding tp_single_drag_2nd[8];  int tp_single_drag_2nd_count;
    TouchBinding tp_double_drag_2nd[8];  int tp_double_drag_2nd_count;
    uint32_t bindings_generation;

    // Pre-computed gesture capability flags (set by compute_gesture_caps)
    bool caps_has_gesture_bindings;
    bool caps_has_drag_bindings;
    bool caps_has_double_tap;
    bool caps_has_long_press;
    bool caps_has_long_press_timer;
    bool caps_has_track_hover_buttons;  // true if any element has ACTIVATION_TRACK or ACTIVATION_HOVER
    bool caps_has_toggle_switch;         // true if any element has toggle_switch
} TouchProcessorConfig;

// Compute gesture capability flags from a TouchProcessorConfig
static inline void compute_gesture_caps(TouchProcessorConfig* cfg) {
    bool has_ts = cfg->ts_single_tap_count > 0 || cfg->ts_long_press_count > 0 ||
                  cfg->ts_double_tap_count > 0 || cfg->ts_single_tap_drag_count > 0 ||
                  cfg->ts_long_press_drag_count > 0 || cfg->ts_double_tap_drag_count > 0 ||
                  cfg->ts_single_2nd_count > 0 || cfg->ts_double_2nd_count > 0 ||
                  cfg->ts_single_drag_2nd_count > 0 || cfg->ts_double_drag_2nd_count > 0;
    bool has_tp = cfg->tp_single_tap_count > 0 || cfg->tp_long_press_count > 0 ||
                  cfg->tp_double_tap_count > 0 || cfg->tp_single_tap_drag_count > 0 ||
                  cfg->tp_long_press_drag_count > 0 || cfg->tp_double_tap_drag_count > 0 ||
                  cfg->tp_single_2nd_count > 0 || cfg->tp_double_2nd_count > 0 ||
                  cfg->tp_single_drag_2nd_count > 0 || cfg->tp_double_drag_2nd_count > 0;
    cfg->caps_has_gesture_bindings = has_ts || has_tp;
    cfg->caps_has_drag_bindings = cfg->ts_single_tap_drag_count > 0 ||
                                  cfg->ts_long_press_drag_count > 0 ||
                                  cfg->ts_double_tap_drag_count > 0 ||
                                  cfg->ts_single_drag_2nd_count > 0 ||
                                  cfg->ts_double_drag_2nd_count > 0 ||
                                  cfg->tp_single_tap_drag_count > 0 ||
                                  cfg->tp_long_press_drag_count > 0 ||
                                  cfg->tp_double_tap_drag_count > 0 ||
                                  cfg->tp_single_drag_2nd_count > 0 ||
                                  cfg->tp_double_drag_2nd_count > 0;
    cfg->caps_has_double_tap = cfg->ts_double_tap_count > 0 ||
                               cfg->ts_double_2nd_count > 0 ||
                               cfg->tp_double_tap_count > 0 ||
                               cfg->tp_double_2nd_count > 0 ||
                               cfg->ts_double_tap_drag_count > 0 ||
                               cfg->ts_double_drag_2nd_count > 0 ||
                               cfg->tp_double_tap_drag_count > 0 ||
                               cfg->tp_double_drag_2nd_count > 0;
    cfg->caps_has_long_press = cfg->ts_long_press_count > 0 ||
                               cfg->tp_long_press_count > 0;
    cfg->caps_has_long_press_timer = cfg->caps_has_long_press ||
                                     cfg->ts_long_press_drag_count > 0 ||
                                     cfg->tp_long_press_drag_count > 0;
}

// --- Output actions ---
typedef enum {
    ACT_NONE,
    ACT_POINTER_MOVE, ACT_POINTER_MOVE_DELTA,
    ACT_POINTER_BUTTON_PRESS, ACT_POINTER_BUTTON_RELEASE,
    ACT_KEY_PRESS, ACT_KEY_RELEASE,
    ACT_MOUSE_EVENT,
    ACT_SCROLL,
    ACT_HAPTIC,
    ACT_SET_CURSOR_SPEED,
    ACT_START_MOUSE_MOVE,
    ACT_STOP_MOUSE_MOVE,
    ACT_GAMEPAD_STATE,       // a0=button_index, a1=is_down
    ACT_GAMEPAD_RELEASE,     // scheduled-action-only: gamepad button release
    ACT_GAMEPAD_AXIS         // a0=is_left (0=left,1=right), a1=axis_x, a2=axis_y
} ActionType;

typedef struct {
    ActionType type;
    union {
        struct { int x, y; } pointer_move;
        struct { int dx, dy; } pointer_delta;
        struct { int button; } pointer_button;
        struct { int keycode; bool is_down; } key;
        struct { int flags, dx, dy, wheel; } mouse_event;
        struct { int amount; } scroll;
        struct { int effect; } haptic;
        struct { int speed; } cursor_speed;
        struct { int dx, dy; int hold; } mouse_move;
        struct { int is_left; int axis_x; int axis_y; } gamepad_axis;
    };
} TouchAction;

typedef struct {
    TouchAction actions[32];
    int count;
} TouchActionResult;

// --- Public API ---

// Initialize processor with configuration
void touch_processor_init(const TouchProcessorConfig* config);

// Update configuration at runtime
void touch_processor_update_config(const TouchProcessorConfig* config);

// Set control elements (buttons, dpads, sticks, etc.)
void touch_processor_set_elements(const TouchElement* elements, int count);

// Set snapping size for element layout
void touch_processor_set_snapping_size(float size);

// Set resolution scale for coordinate transforms
void touch_processor_set_resolution_scale(float scale);

// Enable/disable simulated touch screen mode
void touch_processor_set_sim_touch_screen(bool enabled);

// Set xform scale for trackpad delta computation (view-pixels to Wine-screen-pixels)
void touch_processor_set_xform_scale(float scale_x, float scale_y);

// --- Touch event input ---
// All events return a result struct with actions to execute.

// Finger down
TouchActionResult touch_processor_on_finger_down(int ptr_id, float x, float y, uint64_t time_ms);

// Finger move
TouchActionResult touch_processor_on_finger_move(int ptr_id, float x, float y, uint64_t time_ms);

// Finger up
TouchActionResult touch_processor_on_finger_up(int ptr_id, float x, float y, uint64_t time_ms);

// Finger cancel
void touch_processor_on_finger_cancel(int ptr_id);

// Timer tick — called periodically for long-press, double-tap timeouts
TouchActionResult touch_processor_tick(uint64_t time_ms);

// Reset all state
void touch_processor_reset(void);

// --- Query state ---
bool touch_processor_is_passthrough_active(void);

// Get pointer position for touchscreen mode
void touch_processor_get_pointer_pos(int* x, int* y);

// Get element runtime state for visual feedback (stick_value_x/y, engaged, ptr_id)
// Returns false if elemIndex is out of range
bool touch_processor_get_element_state(int elemIndex, float* out_stick_x, float* out_stick_y, bool* out_engaged, int* out_ptr_id);

// --- Activation API ---

// Query element visual state (x, y, active flag)
bool touch_processor_get_element_visual(int elemIndex, float* out_x, float* out_y, bool* out_active);

// Batch sync visual states: returns number of elements written
int touch_processor_sync_visual_states(float* out_positions, uint8_t* out_active, int max_count);

// Activate all elements at a given point (visual feedback)
void touch_processor_activate_at(float x, float y);

// Deactivate all elements (visual feedback)
void touch_processor_deactivate_all(void);

// Handle touch by activation mode (Java-compatible fallback path)
bool touch_processor_handle_down_by_mode(int ptr_id, float x, float y, uint64_t time_ms);
bool touch_processor_handle_up_by_mode(int ptr_id, float x, float y, uint64_t time_ms);
void touch_processor_handle_move_by_mode(int ptr_id, float x, float y, uint64_t time_ms);

// Get tracked button count for pointer
int touch_processor_tracked_count(int ptr_id);

// Get hovered element index for pointer
int touch_processor_hovered_index(int ptr_id);

// Cleanup
void touch_processor_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_PROCESSOR_H
