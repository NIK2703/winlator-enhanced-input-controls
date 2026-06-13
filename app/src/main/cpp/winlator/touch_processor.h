#ifndef TOUCH_PROCESSOR_H
#define TOUCH_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FINGERS 8
#define MAX_DEFERRED_BINDINGS 8
#define MAX_ELEMENTS 128
#define MAX_PETALS 4
#define STICK_DEAD_ZONE 0.15f
#define DPAD_DEAD_ZONE 0.3f
#define STICK_SENSITIVITY 2.0f
#define TRACKPAD_MIN_SPEED 0.8f
#define TRACKPAD_MAX_SPEED 20.0f
#define BUTTON_MIN_KEEP_PRESSED_MS 300

// --- Visual layer bits (computed by update_visual_layers) ---
#define VISUAL_LAYER_FILL            (1u << 0)
#define VISUAL_LAYER_STROKE_TEXT     (1u << 1)
#define VISUAL_LAYER_GLOW            (1u << 2)
#define VISUAL_LAYER_OUTER_STROKE    (1u << 3)

// --- Per-action visual flags (3-flag system) ---
// Each flag tracks one independent visual activation source.
// Set/cleared on action activate/deactivate; persistence governed by toggle state.
#define VF_TAP       (1u << 0)  // Primary tap: finger on element OR tap-toggle ON
#define VF_LONG_TAP  (1u << 1)  // Long press: LP timer fired OR LP-toggle ON
#define VF_GESTURE   (1u << 2)  // Gesture: swipe/timer fired OR gesture-toggle ON

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
    int modifiers; // 1 = sticky (hold on press), 0 = tap (press+release)
    bool toggle;   // 1 = toggle switch (press on first activation, release on second)
    bool auto_repeat;
    int auto_repeat_interval_ms;
} TouchBinding;

typedef enum {
    GESTURE_SINGLE_TAP,
    GESTURE_LONG_PRESS,
    GESTURE_DOUBLE_TAP,
    GESTURE_SINGLE_TAP_DRAG,
    GESTURE_LONG_PRESS_DRAG,
    GESTURE_DOUBLE_TAP_DRAG,
    GESTURE_SINGLE_2ND,
    GESTURE_DOUBLE_2ND,
    GESTURE_SINGLE_DRAG_2ND,
    GESTURE_DOUBLE_DRAG_2ND,
    GESTURE_TYPE_COUNT
} GestureType;

typedef struct {
    TouchBinding arr[8];
    int count;
} GestureBindingSlot;

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
        // === HOT: accessed every element iteration or every event (first cache line) ===
        // Ints/enums (4 bytes each, 7 total = 28 bytes)
        int current_ptr_id;
        ElementType type;
        ElementShape shape;
        ActivationMode activation_mode;
        int x;
        int y;
        float visual_x;
        float visual_y;
        // Hit-test hot cache (5 floats = 20 bytes)
        float cached_left;
        float cached_right;
        float cached_top;
        float cached_bottom;
        float cached_hw_sq;
        // Bools (1 byte each, 9 total = 9 bytes)
        bool engaged;
        bool selected;
        bool passthrough_touch;
        bool gesture_suppressed;
        bool cached_has_toggle;
        bool cached_has_auto_repeat;
        bool cached_has_long_press;
        bool cached_has_gesture;
        bool cached_has_any_binding;
        // Bindings (pushed to end of HOT — 40 bytes, accessed on every element interaction)
        TouchBinding bindings[4];

        // === WARM: accessed per-tick but conditionally or via dispatch ===
        int cached_auto_repeat_interval;
        uint8_t visual_flags;     // VF_TAP | VF_LONG_TAP | VF_GESTURE (per-action visual state)
        uint32_t visual_layers;   // bitmask of VISUAL_LAYER_* (computed by update_visual_layers)
        uint32_t visual_alpha;    // packed 8-bit alpha per layer
        float down_x;
        float down_y;
        uint64_t down_time_ms;
        uint64_t auto_repeat_last_time;
        uint64_t toggle_debounce_last_time;
        int primary_sticky_mask;
        int button_long_press_haptic;
        int button_gesture_haptic;
        bool long_press_arm;
        bool gesture_swipe_triggered;
        bool gesture_long_press_triggered;
        bool gesture_timer_armed;
        bool lp_toggled;
        bool gesture_toggled;
        bool defer_primary;
        bool auto_repeat_primary_pressed;
        bool cached_lp_has_toggle;
        bool cached_gesture_has_toggle;
        bool cached_bind0_is_gamepad;
        bool cached_bind0_is_right_stick;
        int element_long_press_count;
        int element_gesture_count;
        TouchBinding element_long_press[8];
        TouchBinding element_gesture[8];

        // === COLD: init/setup only ===
        float hw;
        float hh;
        float w;
        float h;
        float scale;

        // --- Range button state ---
        int range_ordinal;
        int range_index;
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
        float cached_range_cw;
        float cached_range_ch;
        float cached_range_element_size;

        // --- Stick/trackpad state ---
        bool petal_active[MAX_PETALS];
        float stick_value_x;
        float stick_value_y;
        float trackpad_last_x;
        float trackpad_last_y;
        float trackpad_vel_x;
        float trackpad_vel_y;
        uint64_t trackpad_last_time;

        // --- Render state (cold, accessed only during visual sync) ---
        float opacity;
        float corner_radius;
        uint32_t color_primary;
        uint32_t color_secondary;
        float stroke_width;
        int fill_alpha_inactive;
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
    const TouchBinding* single_tap;       int single_tap_count;
    const TouchBinding* long_press;       int long_press_count;
    const TouchBinding* double_tap;       int double_tap_count;
    const TouchBinding* single_tap_drag;  int single_tap_drag_count;
    const TouchBinding* long_press_drag;  int long_press_drag_count;
    const TouchBinding* double_tap_drag;  int double_tap_drag_count;
    const TouchBinding* single_tap_2nd;   int single_tap_2nd_count;
    const TouchBinding* double_tap_2nd;   int double_tap_2nd_count;
    const TouchBinding* single_tap_drag_2nd; int single_tap_drag_2nd_count;
    const TouchBinding* double_tap_drag_2nd; int double_tap_drag_2nd_count;
} FingerBindings;

typedef struct {
    int ptr_id;
    float x, y;
    float down_x, down_y;
    uint64_t down_time_ms;
    uint64_t last_move_time_ms;
    float last_x, last_y;
    float sub_pixel_x, sub_pixel_y;
    float travel_x, travel_y;
    bool active;
    // Cached binding flags (recomputed after bindings change, avoids rebuilding per call)
    bool cached_has_active_single_tap;
    bool cached_has_active_double_tap;
    bool cached_has_active_long_press;
    bool cached_has_active_single_tap_drag;
    bool cached_has_active_long_press_drag;
    bool cached_has_active_double_tap_drag;
    bool cached_has_long_press_timer;
    bool cached_has_moved_beyond_threshold;
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

    // Engaged element indices for optimized element scan in gesture handler
    int16_t engaged_elem_indices[16];
    uint8_t engaged_elem_count;

    // Second-finger resume (touchscreen)
    TouchBinding pending_resume_action[8];
    int pending_resume_action_count;
    int original_ptr_id;            // finger identity for double-tap continuity
    bool double_tap_original_id_set;

    // Single-tap hold timer (touchscreen finger-down hold)
    int single_tap_hold_delay_ms;
    uint64_t single_tap_hold_timer;

    // Deferred single-tap (replaces nanosleep)
    bool single_tap_deferred;
    uint64_t single_tap_deferred_time;

    // Set when any gesture or long-press fires during this touch.
    // Once set, all subsequent gesture/long-press triggers on ANY
    // button are blocked until the finger lifts.
    bool gesture_activated_in_touch;
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

    // Gesture bindings — indexed by GestureType
    GestureBindingSlot ts[GESTURE_TYPE_COUNT];
    GestureBindingSlot tp[GESTURE_TYPE_COUNT];
    uint32_t bindings_generation;

    // Render config (shared across all elements)
    uint32_t color_primary;       // default 0xFFFFFFFF (white)
    uint32_t color_secondary;     // default 0xFF0277BD (blue)
    float stroke_width_default;   // default 0.2f
    int fill_alpha_inactive_default; // default 50

    // Pre-computed gesture capability flags (set by compute_gesture_caps)
    bool caps_has_gesture_bindings;
    bool caps_has_drag_bindings;
    bool caps_has_double_tap;
    bool caps_has_long_press;
    bool caps_has_long_press_timer;
    bool caps_has_track_hover_buttons;  // true if any element has ACTIVATION_TRACK or ACTIVATION_HOVER
    uint32_t caps_mode_mask;       // touch_mode-specific mask: caps_ts_mask or caps_tp_mask (pre-resolved, avoids ternary)
    bool is_ts;    // true if touch_mode == TOUCH_MODE_TOUCHSCREEN
    bool is_tp;    // true if touch_mode == TOUCH_MODE_TOUCHPAD
    uint32_t caps_second_mask;     // second-finger gesture mask: SINGLE_2ND|DOUBLE_2ND|SINGLE_DRAG_2ND|DOUBLE_DRAG_2ND
    bool caps_has_element_toggle;   // true if any element has toggle bindings
    bool caps_has_any_element_long_press;  // true if any button has element_long_press bindings
    bool caps_has_any_element_gesture;     // true if any button has element_gesture bindings
    bool caps_has_auto_repeat_buttons;   // true if any button has auto_repeat binding
    bool caps_has_mouse_left_element;    // true if any element has BINDING_MOUSE_LEFT as first binding
    bool caps_has_passthrough_elements;  // true if any element has passthrough_touch

    // Per-mode gesture type presence bitmasks (bit i set iff GestureType i has bindings in that mode)
    // Enables O(1) tests like: if (caps_ts_mask & GESTURE_MASK(GESTURE_LONG_PRESS)) ...
    uint32_t caps_ts_mask;
    uint32_t caps_tp_mask;
    GestureModeBindings cached_mode_bindings;  // precomputed, avoids struct copy per call
} TouchProcessorConfig;

#define GESTURE_MASK(t) (1u << (t))

// Compute gesture capability flags from a TouchProcessorConfig
static inline void compute_gesture_caps(TouchProcessorConfig* cfg) {
    cfg->caps_ts_mask = 0;
    cfg->caps_tp_mask = 0;

    for (int g = 0; g < GESTURE_TYPE_COUNT; g++) {
        if (cfg->ts[g].count > 0) cfg->caps_ts_mask |= GESTURE_MASK(g);
        if (cfg->tp[g].count > 0) cfg->caps_tp_mask |= GESTURE_MASK(g);
    }

    uint32_t all = cfg->caps_ts_mask | cfg->caps_tp_mask;

    cfg->caps_has_gesture_bindings = all != 0;

    uint32_t drag_mask =
        GESTURE_MASK(GESTURE_SINGLE_TAP_DRAG) | GESTURE_MASK(GESTURE_LONG_PRESS_DRAG) |
        GESTURE_MASK(GESTURE_DOUBLE_TAP_DRAG) | GESTURE_MASK(GESTURE_SINGLE_DRAG_2ND) |
        GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);
    cfg->caps_has_drag_bindings = (all & drag_mask) != 0;

    uint32_t dt_mask =
        GESTURE_MASK(GESTURE_DOUBLE_TAP) | GESTURE_MASK(GESTURE_DOUBLE_2ND) |
        GESTURE_MASK(GESTURE_DOUBLE_TAP_DRAG) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);
    cfg->caps_has_double_tap = (all & dt_mask) != 0;

    cfg->caps_has_long_press = (all & GESTURE_MASK(GESTURE_LONG_PRESS)) != 0;

    cfg->caps_has_long_press_timer =
        (all & (GESTURE_MASK(GESTURE_LONG_PRESS) | GESTURE_MASK(GESTURE_LONG_PRESS_DRAG))) != 0;

    cfg->caps_mode_mask = (cfg->touch_mode == TOUCH_MODE_TOUCHSCREEN) ? cfg->caps_ts_mask : cfg->caps_tp_mask;
    cfg->caps_second_mask =
        GESTURE_MASK(GESTURE_SINGLE_2ND) | GESTURE_MASK(GESTURE_DOUBLE_2ND) |
        GESTURE_MASK(GESTURE_SINGLE_DRAG_2ND) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);

    cfg->is_ts = (cfg->touch_mode == TOUCH_MODE_TOUCHSCREEN);
    cfg->is_tp = (cfg->touch_mode == TOUCH_MODE_TOUCHPAD);

    {
        const GestureBindingSlot* slots = cfg->is_ts ? cfg->ts : cfg->tp;
        cfg->cached_mode_bindings.single_tap        = slots[GESTURE_SINGLE_TAP].arr;        cfg->cached_mode_bindings.single_tap_count        = slots[GESTURE_SINGLE_TAP].count;
        cfg->cached_mode_bindings.long_press        = slots[GESTURE_LONG_PRESS].arr;        cfg->cached_mode_bindings.long_press_count        = slots[GESTURE_LONG_PRESS].count;
        cfg->cached_mode_bindings.double_tap        = slots[GESTURE_DOUBLE_TAP].arr;        cfg->cached_mode_bindings.double_tap_count        = slots[GESTURE_DOUBLE_TAP].count;
        cfg->cached_mode_bindings.single_tap_drag   = slots[GESTURE_SINGLE_TAP_DRAG].arr;   cfg->cached_mode_bindings.single_tap_drag_count   = slots[GESTURE_SINGLE_TAP_DRAG].count;
        cfg->cached_mode_bindings.long_press_drag   = slots[GESTURE_LONG_PRESS_DRAG].arr;   cfg->cached_mode_bindings.long_press_drag_count   = slots[GESTURE_LONG_PRESS_DRAG].count;
        cfg->cached_mode_bindings.double_tap_drag   = slots[GESTURE_DOUBLE_TAP_DRAG].arr;   cfg->cached_mode_bindings.double_tap_drag_count   = slots[GESTURE_DOUBLE_TAP_DRAG].count;
        cfg->cached_mode_bindings.single_2nd        = slots[GESTURE_SINGLE_2ND].arr;        cfg->cached_mode_bindings.single_2nd_count        = slots[GESTURE_SINGLE_2ND].count;
        cfg->cached_mode_bindings.double_2nd        = slots[GESTURE_DOUBLE_2ND].arr;        cfg->cached_mode_bindings.double_2nd_count        = slots[GESTURE_DOUBLE_2ND].count;
        cfg->cached_mode_bindings.single_drag_2nd   = slots[GESTURE_SINGLE_DRAG_2ND].arr;   cfg->cached_mode_bindings.single_drag_2nd_count   = slots[GESTURE_SINGLE_DRAG_2ND].count;
        cfg->cached_mode_bindings.double_drag_2nd   = slots[GESTURE_DOUBLE_DRAG_2ND].arr;   cfg->cached_mode_bindings.double_drag_2nd_count   = slots[GESTURE_DOUBLE_DRAG_2ND].count;
    }
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
    TouchAction actions[64];
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
