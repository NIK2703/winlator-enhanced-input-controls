#ifndef TOUCH_PROCESSOR_INTERNAL_H
#define TOUCH_PROCESSOR_INTERNAL_H

// C++ lacks 'restrict' keyword — use compiler extension for compatibility
#ifdef __cplusplus
#define restrict __restrict__
#endif

#include "touch_processor.h"
#include "gesture/types.h"
#include <android/log.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define TP_LOG(prio, tag, fmt, ...) __android_log_print(prio, tag, fmt, ##__VA_ARGS__)
#ifndef LOG_TAG
#define LOG_TAG "Winlator_Touch"
#endif
#define MAX_TRACKED_PER_POINTER 8
#define CLICK_DELAY_MS 50
#define GESTURE_TIMER_MS 50
#define MAX_TAP_TRAVEL 10
#define RANGE_TAP_TIMEOUT_MS 200
#define TAP_MAX_TIME_MS 200
#define MOUSE_WHEEL_DELTA 120

// Scheduled actions (non-blocking replacement for delay_ms)
#define MAX_SCHEDULED_ACTIONS 32
#define MAX_HELD_ACTIONS 16

typedef struct {
    uint64_t scheduled_time_ms;
    TouchBinding binding;
    int action_type;
    bool active;
    bool needs_toggle_record;
} ScheduledAction;

typedef struct {
    int ptr_id;
    int element_indices[MAX_TRACKED_PER_POINTER];
    int count;
} TrackedButtons;

typedef struct {
    // === CORE (HOT): accessed on every single touch event ===
    TouchProcessorConfig cfg;
    TouchFinger* finger_by_ptr_id[256];
    TouchFinger fingers[MAX_FINGERS];
    int finger_count;
    int active_finger_count;
    int element_count;

    // Pointers (core state, every touch + move)
    float ptr_x, ptr_y;
    float scroll_accum_y;
    int main_ptr_id;

    // Gesture flags (checked in every tick and touch event)
    int gesture_main_ptr_id;
    int gesture_second_ptr_id;
    bool gesture_second_active;
    bool gesture_double_tap_waiting;
    bool second_double_tap_waiting;
    bool gesture_post_double_tap_drag;
    bool gesture_double_tap_consumed;
    bool gesture_is_down_event;
    float gesture_second_main_ref_x;
    float gesture_second_main_ref_y;
    bool passthrough_active;
    int free_finger_hint;

    // === HIT-TESTING: spatial grid + element layout (every touch DOWN) ===
    #define GRID_COLS 8
    #define GRID_ROWS 8
    float grid_cell_w;
    float grid_cell_h;
    float grid_min_x;
    float grid_min_y;
    float grid_max_x;
    float grid_max_y;
    float snapping_size;
    float resolution_scale;
    float grid_inv_cell_w;
    float grid_inv_cell_h;
    int spatial_grid_count[GRID_COLS * GRID_ROWS];
    int grid_cell_to_elems[MAX_ELEMENTS];
    int grid_cell_start[GRID_COLS * GRID_ROWS + 1];

    // === TICK PATH: button/range processing at 60fps ===
    int button_count;
    int range_count;
    int button_indices[MAX_ELEMENTS];
    int range_indices[MAX_ELEMENTS];

    // === ACTIVATION: updated on finger move ===
    ActivationMode activation_mode;
    TrackedButtons tracked[MAX_FINGERS];
    int hovered_element_per_ptr[MAX_FINGERS];
    uint32_t visual_dirty_mask[4];

    // === GESTURE STATE: double-tap waiting, second finger timing ===
    bool gesture_deferred_second_finger_tap;
    float gesture_last_tap_up_x, gesture_last_tap_up_y;
    uint64_t gesture_double_tap_start_time;
    int last_touch_x, last_touch_y;

    // Two-finger touchpad state
    bool scrolling;
    bool pointer_left_enabled;
    bool pointer_right_enabled;
    int finger_pointer_left;
    int finger_pointer_right;

    // === COLD: deferred actions, held actions, scheduled actions ===
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
    TouchBinding gesture_toggled_actions[16];
    int gesture_toggled_count;
    uint64_t gesture_auto_repeat_last_time[16];
    uint64_t gesture_auto_repeat_last_time_held[16];
    TouchBinding second_tap_fallback[8];
    int second_tap_fallback_count;
    uint64_t second_tap_fallback_time;
    TouchBinding pending_second_double[8];
    int pending_second_double_count;

    // Delayed release + sim
    uint64_t pending_left_release_time;
    int pending_left_release_ptr_id;
    uint64_t pending_right_release_time;
    int pending_right_release_ptr_id;
    bool sim_touch_screen;
    bool sim_continue_click;
    uint64_t sim_click_press_time;
    int sim_click_ptr_id;
    uint64_t sim_click_release_time;

    // Scheduled actions
    ScheduledAction scheduled_actions[MAX_SCHEDULED_ACTIONS];
    int scheduled_action_count;
    uint8_t scheduled_seq_counter;

    // Timestamp of last tick for timeout detection
    uint64_t last_tick_time;

    // Large arrays at the end (cold path)
    TouchElement elements[MAX_ELEMENTS];
    int spatial_grid[GRID_COLS * GRID_ROWS][MAX_ELEMENTS];
} TouchProcessorState;

extern TouchProcessorState g_state;

static inline bool element_is_toggle_active(const TouchElement* e) {
    return (e->cached_has_toggle && e->selected) || e->lp_toggled || e->gesture_toggled;
}

static inline void mark_element_dirty(TouchElement* e) {
    int idx = (int)(e - g_state.elements);
    g_state.visual_dirty_mask[idx / 32] |= (1u << (idx % 32));
}

static inline void mark_all_dirty(void) {
    int n = g_state.element_count;
    for (int b = 0; b < 4; b++) {
        int base = b * 32;
        if (base >= n) {
            g_state.visual_dirty_mask[b] = 0;
        } else {
            int rem = n - base;
            g_state.visual_dirty_mask[b] = (rem >= 32) ? 0xFFFFFFFF : ((1u << rem) - 1);
        }
    }
}

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

// SINGLE place where TS vs TP binding selection happens.
// Returns pointer to precomputed cached_mode_bindings from compute_gesture_caps.
static inline const GestureModeBindings* get_mode_bindings(void) {
    return &g_state.cfg.cached_mode_bindings;
}

// Quick check: does current touch mode have ANY gesture bindings?
static inline bool current_mode_has_gestures(void) {
    return g_state.cfg.caps_mode_mask != 0;
}

// Quick check: does current touch mode have a specific gesture type?
static inline bool current_mode_has_gesture(GestureType t) {
    return (g_state.cfg.caps_mode_mask & GESTURE_MASK(t)) != 0;
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

// --- Element query helpers ---
static inline bool element_has_primary(const TouchElement* e) {
    return e->bindings[0].type != BINDING_NONE;
}
static inline bool element_has_any_gesture_activity(const TouchElement* e) {
    return e->gesture_swipe_triggered || e->gesture_long_press_triggered
        || e->gesture_toggled || e->lp_toggled;
}
static inline bool element_has_gesture_toggle(const TouchElement* e) {
    return e->lp_toggled || e->gesture_toggled;
}
static inline bool finger_gesture_active(const TouchElement* e) {
    if (e->current_ptr_id < 0) return false;
    TouchFinger* f = g_state.finger_by_ptr_id[e->current_ptr_id];
    return f != NULL && f->gesture_activated_in_touch;
}
static inline bool is_tracked(const TrackedButtons* tb, int idx) {
    for (int j = 0; j < tb->count; j++)
        if (tb->element_indices[j] == idx) return true;
    return false;
}
static inline void element_update_position_cache(TouchElement* e) {
    e->cached_left = e->x - e->hw;
    e->cached_right = e->x + e->hw;
    e->cached_top = e->y - e->hh;
    e->cached_bottom = e->y + e->hh;
    e->cached_hw_sq = e->hw * e->hw;
}
static inline bool gesture_processing_needed(void) {
    return __builtin_expect(g_state.cfg.caps_mode_mask != 0, 1)
        || g_state.gesture_double_tap_waiting
        || g_state.gesture_post_double_tap_drag
        || g_state.second_double_tap_waiting;
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
    GestureModeBindings mb = *get_mode_bindings();
    if (g_state.cfg.is_tp) {
        mb.single_tap_drag = NULL; mb.single_tap_drag_count = 0;
        mb.single_drag_2nd = NULL; mb.single_drag_2nd_count = 0;
    }
    COPY_FROM_MODE_BINDINGS(fb, mb);
}

// Set up second-finger bindings: 2nd variants map to primary FingerBindings slots.
static inline void setup_second_finger_bindings(TouchFinger* f) {
    FingerBindings* fb = &f->bindings;
    // If no second-finger bindings exist in current mode, skip setup entirely
    if (!(g_state.cfg.caps_mode_mask & g_state.cfg.caps_second_mask)) {
        memset(fb, 0, sizeof(*fb));
        touch_finger_cache_bs(f);
        return;
    }
    const GestureModeBindings* mb = get_mode_bindings();
    COPY_FROM_MODE_BINDINGS_2ND(fb, *mb);
    if (g_state.cfg.is_tp) {
        fb->single_tap_drag = NULL; fb->single_tap_drag_count = 0;
    }
    touch_finger_cache_bs(f);
}

// --- Element size computation (dedup set_elements / set_snapping_size) ---

static inline void element_compute_snapped_hwhh(TouchElement* e, float snap) {
    float hs = snap;
    switch (e->type) {
        case ELEM_DPAD: e->hw = hs * 7.0f * e->scale; e->hh = hs * 7.0f * e->scale; break;
        case ELEM_STICK:
        case ELEM_TRACKPAD: e->hw = hs * 6.0f * e->scale; e->hh = hs * 6.0f * e->scale; break;
        case ELEM_BUTTON:
            if (e->shape == SHAPE_CIRCLE) { e->hw = hs * 3.0f * e->scale; e->hh = hs * 3.0f * e->scale; }
            else { e->hw = e->w * hs * 0.5f * e->scale; e->hh = e->h * hs * 0.5f * e->scale; }
            break;
        case ELEM_RANGE_BUTTON:
            e->hw = hs * ((e->range_binding_count * 4) / 2) * e->scale;
            e->hh = hs * 2.0f * e->scale;
            if (e->range_orientation == 1) { float _t = e->hw; e->hw = e->hh; e->hh = _t; }
            break;
        default: e->hw = e->w * hs * 0.5f * e->scale; e->hh = e->h * hs * 0.5f * e->scale; break;
    }
}

// --- Finger deactivation (dedup on_finger_up / on_finger_cancel) ---

static inline void deactivate_finger(TouchFinger* f) {
    if (__builtin_expect(!f->active, 0)) return;
    g_state.finger_by_ptr_id[f->ptr_id] = NULL;
    g_state.active_finger_count--;
    f->active = false;
    f->gesture_activated_in_touch = false;
}

// --- Internal function declarations ---

// Actions
void add_action(TouchActionResult* restrict r, ActionType type, int a0, int a1, int a2);
void release_held_actions(TouchActionResult* restrict result);
bool is_modifier_binding(const TouchBinding* b);
void execute_actions(TouchActionResult* restrict result, const TouchBinding* actions, int count);
void execute_actions_hold(TouchActionResult* restrict result, const TouchBinding* actions, int count);
void release_binding(TouchActionResult* restrict result, const TouchBinding* b);
void press_binding(TouchActionResult* restrict result, const TouchBinding* b, bool hold);

// --- Binding comparison helper ---

static inline bool bindings_equal(const TouchBinding* a, int a_count, const TouchBinding* b, int b_count) {
    if (a_count != b_count) return false;
    for (int i = 0; i < a_count; i++)
        if (a[i].type != b[i].type || a[i].keycode != b[i].keycode
            || a[i].modifiers != b[i].modifiers || a[i].toggle != b[i].toggle
            || a[i].auto_repeat != b[i].auto_repeat)
            return false;
    return true;
}

// --- Cleanup helpers (reduce repetitive zeroing patterns) ---

static inline void gesture_clear_deferred_tap(void) {
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_pending_deferred_double_count = 0;
}

static inline void execute_deferred_double(TouchActionResult* restrict result) {
    if (g_state.gesture_pending_deferred_double_count > 0) {
        execute_actions(result, g_state.gesture_pending_deferred_double,
                        g_state.gesture_pending_deferred_double_count);
        g_state.gesture_pending_deferred_double_count = 0;
    }
}

static inline void gesture_clear_pending_long_press(void) {
    g_state.gesture_pending_deferred_long_press_count = 0;
}

static inline void tap_up_cleanup(TouchFinger* f, TouchActionResult* restrict result) {
    if (!g_state.gesture_second_active || !g_state.gesture_is_action_held
        || g_state.cfg.is_tp)
        release_held_actions(result);
    g_state.gesture_main_ptr_id = -1;
    deactivate_finger(f);
}

static inline void gesture_clear_second_finger_globals(void) {
    g_state.gesture_second_active = false;
    g_state.gesture_second_ptr_id = -1;
    g_state.gesture_deferred_second_finger_tap = false;
    g_state.gesture_post_double_tap_drag = false;
    g_state.gesture_second_main_ref_x = 0;
    g_state.gesture_second_main_ref_y = 0;
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
    return f->cached_has_active_single_tap
        || f->cached_has_active_long_press
        || f->cached_has_active_double_tap
        || f->cached_has_active_single_tap_drag
        || f->cached_has_active_long_press_drag
        || f->cached_has_active_double_tap_drag;
}

static inline bool element_has_toggle(const TouchElement* e) {
    return e->bindings[0].toggle || e->bindings[1].toggle || e->bindings[2].toggle || e->bindings[3].toggle;
}

static inline bool element_has_auto_repeat(const TouchElement* e) {
    return e->bindings[0].auto_repeat || e->bindings[1].auto_repeat || e->bindings[2].auto_repeat || e->bindings[3].auto_repeat;
}

// Cached fields accessed directly via f->cached_* — no wrapper overhead needed

#define BINDING_GAMEPAD_COUNT 24

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

// Element shared helpers
bool point_in_element(float px, float py, const TouchElement* e);
TouchElement* hit_test_element(float x, float y);
void touch_finger_cache_bs(TouchFinger* f);
int detect_swipe_dir(float dx, float dy, float threshold);
bool is_mouse_move_binding(const TouchBinding* b);
float cubic_bezier_interpolate_trackpad(float x);
void element_set_petals(TouchElement* e, float nx, float ny, float dead_zone, TouchActionResult* restrict result);
void release_element_bindings(TouchElement* e, TouchActionResult* restrict result, bool release_gestures);
void suppress_element_gestures(TouchElement* e, TouchActionResult* restrict result);
void release_non_toggle_gestures(TouchElement* e, TouchActionResult* restrict result);
void toggle_alternate_bindings(TouchElement* e, bool* toggled_flag, bool has_toggle_cache,
                               TouchBinding* bindings, int count, bool has_primary,
                               TouchActionResult* restrict result);

// Element dispatchers
void handle_element_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void handle_element_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void handle_element_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);

// Per-type element handlers
void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);

void element_dpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_dpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_dpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);

void element_stick_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_stick_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_stick_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);

void element_trackpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_trackpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_trackpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);

void element_range_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_range_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void element_range_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
int range_keycode(int ordinal, int index);

// --- TS pointer update (dedup 4x touch_transform_coords + ACT_POINTER_MOVE) ---
static inline void update_ts_pointer(float x, float y, TouchActionResult* restrict result) {
    int tx, ty;
    touch_transform_coords(x, y, &tx, &ty);
    g_state.ptr_x = (float)tx;
    g_state.ptr_y = (float)ty;
    add_action(result, ACT_POINTER_MOVE, tx, ty, 0);
}

static inline void release_bindings_list(TouchActionResult* restrict result, TouchBinding* bindings, int count) {
    if (count == 0) return;
    for (int k = count - 1; k >= 0; k--) {
        if (bindings[k].type == BINDING_NONE) continue;
        if (!is_modifier_binding(&bindings[k]))
            release_binding(result, &bindings[k]);
    }
    for (int k = count - 1; k >= 0; k--) {
        if (bindings[k].type == BINDING_NONE) continue;
        if (is_modifier_binding(&bindings[k]))
            release_binding(result, &bindings[k]);
    }
}

static inline void press_bindings_list(TouchActionResult* restrict result, TouchBinding* bindings, int count) {
    if (count == 0) return;
    for (int k = 0; k < count; k++) {
        if (bindings[k].type == BINDING_NONE) continue;
        if (is_modifier_binding(&bindings[k]))
            press_binding(result, &bindings[k], true);
    }
    for (int k = 0; k < count; k++) {
        if (bindings[k].type == BINDING_NONE) continue;
        if (!is_modifier_binding(&bindings[k]))
            press_binding(result, &bindings[k], true);
    }
}
static inline void reset_finger_tap_state(TouchFinger* f) {
    f->cached_has_long_press_timer = false;
    f->single_tap_hold_delay_ms = 0;
    f->single_tap_hold_timer = 0;
    f->cached_has_moved_beyond_threshold = false;
}

// Gesture — entry points (touch_processor_gesture.c)
void touchpad_finger_down(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms);
void touchpad_finger_up(TouchFinger* f, TouchActionResult* restrict result, uint64_t time_ms);

// Unified gesture handler (replaces both touchscreen.c and touchpad.c)
// The only difference between TOUCHPAD and TOUCHSCREEN modes is cursor behavior:
//   TOUCHSCREEN - cursor attached absolutely to first finger
//   TOUCHPAD    - relative cursor movement with delta accumulation
void handle_gesture_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void handle_gesture_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void handle_gesture_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);

// --- Element shared helpers (defined in element/shared.c) ---
void clear_element_gesture_flags(TouchElement* e, bool set_suppressed);
void release_toggled_alternate_bindings(TouchElement* e, TouchActionResult* restrict result, bool check_triggered);
void release_element_petals(TouchElement* e, TouchActionResult* restrict result);
void button_auto_repeat_move(TouchElement* e, bool inside, uint64_t time_ms, TouchActionResult* restrict result);
void force_release_element_toggles(TouchElement* e, TouchActionResult* restrict result);
bool finger_remove_engaged(TouchFinger* f, int16_t elem_idx);
TrackedButtons* get_tracked_buttons(int ptr_id);

static inline void reset_tracked_slot(TrackedButtons* tb, int pid_slot) {
    tb->count = 0;
    tb->ptr_id = -1;
    g_state.hovered_element_per_ptr[pid_slot] = -1;
}

static inline bool gesture_remove_toggled_action(int type, int keycode) {
    for (int t = 0; t < g_state.gesture_toggled_count; t++) {
        if (g_state.gesture_toggled_actions[t].type == type &&
            g_state.gesture_toggled_actions[t].keycode == keycode) {
            g_state.gesture_toggled_actions[t] = g_state.gesture_toggled_actions[--g_state.gesture_toggled_count];
            g_state.gesture_auto_repeat_last_time[t] = g_state.gesture_auto_repeat_last_time[g_state.gesture_toggled_count];
            return true;
        }
    }
    return false;
}

static inline bool gesture_remove_held_action(int type, int keycode) {
    for (int t = 0; t < g_state.gesture_held_count; t++) {
        if (g_state.gesture_held_actions[t].type == type &&
            g_state.gesture_held_actions[t].keycode == keycode) {
            g_state.gesture_held_actions[t] = g_state.gesture_held_actions[--g_state.gesture_held_count];
            g_state.gesture_auto_repeat_last_time_held[t] = g_state.gesture_auto_repeat_last_time_held[g_state.gesture_held_count];
            return true;
        }
    }
    return false;
}

static inline void reset_first_tracked_long_press(const TrackedButtons* tb) {
    if (tb->count >= 2) {
        TouchElement* first = &g_state.elements[tb->element_indices[0]];
        first->long_press_arm = false;
        first->gesture_long_press_triggered = false;
    }
}

static inline void reset_unused_toggle_gesture_timers(void) {
    for (int i = 0; i < g_state.element_count; i++) {
        if (!g_state.elements[i].cached_has_toggle) continue;
        if (g_state.elements[i].current_ptr_id >= 0) continue;
        g_state.elements[i].gesture_timer_armed = false;
    }
}

static inline void copy_bindings_bounded(const TouchBinding* src, int src_count, TouchBinding* dst, int* dst_count, int dst_max) {
    *dst_count = 0;
    for (int _i = 0; _i < src_count && _i < dst_max; _i++)
        dst[_i] = src[_i];
    *dst_count = src_count > dst_max ? dst_max : src_count;
}

// --- Activation internal helpers ---
bool activation_handle_down(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void activation_handle_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
bool activation_handle_up(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result);
void activation_reset(void);

// Scheduled actions
void process_scheduled_actions(TouchActionResult* restrict result, uint64_t time_ms);

// Spatial grid
void build_spatial_grid(void);

// Element runtime reset (defined in element/shared.c)
void element_reset_runtime(TouchElement* e);

#endif // TOUCH_PROCESSOR_INTERNAL_H
