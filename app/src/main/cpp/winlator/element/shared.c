#include "../touch_processor_internal.h"
#include <float.h>

#define NEAR_ZERO_EPSILON 0.0001f
#define PREFETCH_DISTANCE 4
#define MAX_ENGAGED_ELEMENTS 16
// MUST match element_down_table/move_table/up_table sizes. Add assertion if adding new element types.
#define ELEM_TYPE_COUNT 5

static inline int get_grid_cell(float fx, float fy, float min_x, float min_y);

bool is_mouse_move_binding(const TouchBinding* b) {
    return b->type >= BINDING_MOUSE_MOVE_LEFT && b->type <= BINDING_MOUSE_MOVE_DOWN;
}

bool point_in_element(float px, float py, const TouchElement* e) {
    if (px < e->cached_left || px > e->cached_right) return false;
    if (py < e->cached_top || py > e->cached_bottom) return false;
    if (e->shape == SHAPE_CIRCLE) {
        float dx = px - e->x, dy = py - e->y;
        if (dx * dx + dy * dy > e->cached_hw_sq) return false;
    }
    return true;
}

TouchElement* hit_test_element(float x, float y) {
    // Use spatial grid if available
    if (g_state.grid_cell_w > 0.0f && g_state.grid_cell_h > 0.0f) {
        if (x >= g_state.grid_min_x && x <= g_state.grid_max_x &&
            y >= g_state.grid_min_y && y <= g_state.grid_max_y) {
            int cell = get_grid_cell(x, y, g_state.grid_min_x, g_state.grid_min_y);
            int col = cell % GRID_COLS;
            int row = cell / GRID_COLS;
            int min_c = col > 0 ? col - 1 : 0;
            int max_c = col < GRID_COLS - 1 ? col + 1 : GRID_COLS - 1;
            int min_r = row > 0 ? row - 1 : 0;
            int max_r = row < GRID_ROWS - 1 ? row + 1 : GRID_ROWS - 1;

            for (int r = max_r; r >= min_r; r--) {
                for (int c = max_c; c >= min_c; c--) {
                    int cell_idx = r * GRID_COLS + c;
                    int cnt = g_state.spatial_grid_count[cell_idx];
                    if (cnt > MAX_ELEMENTS) cnt = MAX_ELEMENTS;
                    for (int j = cnt - 1; j >= 0; j--) {
                        TouchElement* e = &g_state.elements[g_state.spatial_grid[cell_idx][j]];
                        if (point_in_element(x, y, e))
                            return e;
                    }
                }
            }
        }
    }
    
    // Fallback: linear scan
    for (int i = g_state.element_count - 1; i >= 0; i--) {
        TouchElement* e = &g_state.elements[i];
        if (i >= PREFETCH_DISTANCE)
            __builtin_prefetch(&g_state.elements[i - PREFETCH_DISTANCE], 0, 1);
        if (point_in_element(x, y, e))
            return e;
    }
    return NULL;
}

#define BEZIER_LUT_SIZE 256
#define BEZIER_SEARCH_ITERS 16
#define BEZIER_P0X 0.075f
#define BEZIER_P1X 0.45f
#define BEZIER_P1Y 0.95f
#define BEZIER_P2Y 0.95f

static float g_bezier_lut[BEZIER_LUT_SIZE + 1];

static inline int get_grid_cell(float fx, float fy, float min_x, float min_y) {
    int col, row;
    if (g_state.grid_max_x <= min_x) {
        col = 0;
    } else {
        col = (int)((fx - min_x) * g_state.grid_inv_cell_w);
        if (col < 0) col = 0; else if (col >= GRID_COLS) col = GRID_COLS - 1;
    }
    if (g_state.grid_max_y <= min_y) {
        row = 0;
    } else {
        row = (int)((fy - min_y) * g_state.grid_inv_cell_h);
        if (row < 0) row = 0; else if (row >= GRID_ROWS) row = GRID_ROWS - 1;
    }
    return row * GRID_COLS + col;
}

void init_bezier_lut(void) {
    for (int i = 0; i <= BEZIER_LUT_SIZE; i++) {
        float x_target = (float)i / (float)BEZIER_LUT_SIZE;
        float lo = 0.0f, hi = 1.0f;
        for (int j = 0; j < BEZIER_SEARCH_ITERS; j++) {
            float t = (lo + hi) * 0.5f;
            float omt = 1.0f - t;
            float bx = 3.0f * omt * omt * t * BEZIER_P0X + 3.0f * omt * t * t * BEZIER_P1X + t * t * t;
            if (bx < x_target) lo = t;
            else hi = t;
        }
        float t = (lo + hi) * 0.5f;
        float omt = 1.0f - t;
        float by = 3.0f * omt * omt * t * BEZIER_P1Y + 3.0f * omt * t * t * BEZIER_P2Y + t * t * t;
        g_bezier_lut[i] = by;
    }
}

// Eager init: constructor runs before any touch events
__attribute__((constructor)) static void _auto_init_bezier(void) { init_bezier_lut(); }

float cubic_bezier_interpolate_trackpad(float x) {
    float abs_x = fabsf(x);
    if (abs_x <= NEAR_ZERO_EPSILON) return 0.0f;
    if (abs_x >= 1.0f) return x > 0 ? 1.0f : -1.0f;
    float f = abs_x * (float)BEZIER_LUT_SIZE;
    int idx = (int)f;
    if (idx >= BEZIER_LUT_SIZE) return x > 0 ? 1.0f : -1.0f;
    float frac = f - (float)idx;
    float result = g_bezier_lut[idx] + frac * (g_bezier_lut[idx+1] - g_bezier_lut[idx]);
    return x > 0 ? result : -result;
}

SwipeDirection detect_swipe_dir(float dx, float dy, float threshold) {
    if (fabsf(dx) < threshold && fabsf(dy) < threshold) return SWIPE_NONE;
    if (fabsf(dx) > fabsf(dy)) return dx > 0 ? SWIPE_RIGHT : SWIPE_LEFT;
    return dy > 0 ? SWIPE_DOWN : SWIPE_UP;
}

void element_set_petals(TouchElement* e, float nx, float ny, float dead_zone, TouchActionResult* restrict result) {
    if (!e->cached_has_any_binding) return;
    bool raw_up = ny <= -dead_zone;
    bool raw_right = nx >= dead_zone;
    bool raw_down = ny >= dead_zone;
    bool raw_left = nx <= -dead_zone;
    bool states[MAX_PETALS] = {raw_up, raw_right, raw_down, raw_left};
    for (int i = 0; i < MAX_PETALS; i++) {
        const TouchBinding* b = &e->bindings[i];
        if (b->type == BINDING_NONE) continue;
        bool active = states[i];
        if (active != e->petal_active[i]) {
            e->petal_active[i] = active;
            if (active) press_binding(result, b, true);
            else release_binding(result, b);
        }
    }
}

typedef void (*element_down_fn)(TouchElement*, int, float, float, uint64_t, TouchActionResult* restrict);
typedef void (*element_move_fn)(TouchElement*, float, float, uint64_t, TouchActionResult* restrict);
typedef void (*element_up_fn)(TouchElement*, float, float, uint64_t, TouchActionResult* restrict);

static const element_down_fn element_down_table[] = {
    element_button_down,
    element_dpad_down,
    element_range_button_down,
    element_stick_down,
    element_trackpad_down,
};

static const element_move_fn element_move_table[] = {
    element_button_move,
    element_dpad_move,
    element_range_button_move,
    element_stick_move,
    element_trackpad_move,
};

static const element_up_fn element_up_table[] = {
    element_button_up,
    element_dpad_up,
    element_range_button_up,
    element_stick_up,
    element_trackpad_up,
};

_Static_assert(
    sizeof(element_down_table) / sizeof(element_down_table[0]) == ELEM_TYPE_COUNT
    && sizeof(element_move_table) / sizeof(element_move_table[0]) == ELEM_TYPE_COUNT
    && sizeof(element_up_table) / sizeof(element_up_table[0]) == ELEM_TYPE_COUNT,
    "ELEM_TYPE_COUNT must match dispatch table sizes");

void handle_element_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    // Elements always take priority: cancel any pending double-tap gesture
    // so the touch engages the element instead of triggering gesture bindings.
    if (__builtin_expect(g_state.gesture_double_tap_waiting, 0)) {
        gesture_cancel_double_tap_wait(result);
    }
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "handle_element_down[%d] type=%d ptr=%d x=%.0f y=%.0f", (int)(e - g_state.elements), e->type, ptr_id, x, y);
    if (__builtin_expect(e->current_ptr_id >= 0, 0)) {
        return;
    }
    if (e->type >= ELEM_TYPE_COUNT) return;
    e->current_ptr_id = ptr_id;
    e->down_x = x;
    e->down_y = y;
    e->down_time_ms = time_ms;
    e->engaged = true;
    e->visual_active = true;
    e->visual_x = x;
    e->visual_y = y;
    mark_element_dirty(e);
    clear_element_gesture_flags(e, false);
    e->gesture_suppressed = false;
    // NOTE: gesture_toggled/lp_toggled are NOT cleared here — toggle switches
    // persist across touches. Deactivation happens only on re-activation of
    // the same gesture (LP/swipe) via the timer/move handlers.

    element_down_table[e->type](e, ptr_id, x, y, time_ms, result);

    int16_t elem_idx = (int16_t)(e - g_state.elements);
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(f != NULL, 1)) {
        uint8_t cnt = f->engaged_elem_count;
        if (__builtin_expect(cnt < MAX_ENGAGED_ELEMENTS, 1)) {
            f->engaged_elem_indices[cnt] = elem_idx;
            f->engaged_elem_count = cnt + 1;
        }
    }
}

// WHEN: handle_element_up — clean up non-toggle gesture presses that shouldn't persist
void release_non_toggle_gestures(TouchElement* e, TouchActionResult* restrict result) {
    if (__builtin_expect(e->gesture_swipe_triggered, 0) && !e->gesture_toggled)
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
    if (__builtin_expect(e->gesture_long_press_triggered, 0) && !e->lp_toggled)
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
}

void toggle_alternate_bindings(TouchElement* e, bool* toggled_flag, bool supports_toggle,
                               TouchBinding* bindings, int count, bool has_primary,
                               TouchActionResult* restrict result)
{
    if (*toggled_flag) {
        release_bindings_list(result, bindings, count);
        *toggled_flag = false;
        if (e->defer_primary && has_primary && !e->cached_has_toggle)
            release_binding(result, &e->bindings[0]);
    } else {
        press_bindings_list(result, bindings, count);
        if (supports_toggle) *toggled_flag = true;
        if (e->defer_primary && has_primary && !e->cached_has_toggle)
            press_binding(result, &e->bindings[0], true);
    }
}

void clear_element_gesture_flags(TouchElement* e, bool set_suppressed) {
    e->long_press_arm = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_triggered = false;
    e->gesture_timer_armed = false;
    e->visual_long_press_active = false;
    if (set_suppressed) e->gesture_suppressed = true;
}

// WHEN: toggle-held bindings need release when trigger ends (e.g. double-tap toggle off)
void release_toggled_alternate_bindings(TouchElement* e, TouchActionResult* restrict result, bool check_triggered) {
    if ((!check_triggered || !e->gesture_swipe_triggered) && e->gesture_toggled) {
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        e->gesture_toggled = false;
    }
    if ((!check_triggered || !e->gesture_long_press_triggered) && e->lp_toggled) {
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        e->lp_toggled = false;
    }
}

void release_element_petals(TouchElement* e, TouchActionResult* restrict result) {
    for (int i = 0; i < MAX_PETALS; i++) {
        if (__builtin_expect(e->petal_active[i], 0)) {
            e->petal_active[i] = false;
            if (e->bindings[i].type != BINDING_NONE && !(e->primary_sticky_mask & (1 << i)))
                release_binding(result, &e->bindings[i]);
        }
    }
}

void button_auto_repeat_move(TouchElement* e, bool inside, uint64_t time_ms, TouchActionResult* restrict result) {
    if (!inside && e->auto_repeat_primary_pressed) {
        if (e->bindings[0].type != BINDING_NONE)
            release_binding(result, &e->bindings[0]);
        e->auto_repeat_primary_pressed = false;
    } else if (inside && !e->auto_repeat_primary_pressed) {
        if (e->bindings[0].type != BINDING_NONE) {
            press_binding(result, &e->bindings[0], true);
            e->auto_repeat_primary_pressed = true;
        }
        e->auto_repeat_last_time = time_ms;
    }
}

// Unlike release_toggled_alternate_bindings, this unconditionally releases ALL bindings and clears selected state.
// WHEN: unconditional full deselect — another element grabs exclusive focus
void force_release_element_toggles(TouchElement* e, TouchActionResult* restrict result) {
    if (e->selected) {
        for (int k = 0; k < MAX_PETALS; k++) {
            if (e->bindings[k].type != BINDING_NONE)
                release_binding(result, &e->bindings[k]);
        }
        e->selected = false;
        e->visual_active = false;
        mark_element_dirty(e);
    }
    if (e->gesture_toggled) {
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        e->gesture_toggled = false;
    }
    if (e->lp_toggled) {
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        e->lp_toggled = false;
    }
}

bool finger_remove_engaged(TouchFinger* f, int16_t elem_idx) {
    for (uint8_t i = 0; i < f->engaged_elem_count; i++) {
        if (f->engaged_elem_indices[i] == elem_idx) {
            f->engaged_elem_indices[i] = f->engaged_elem_indices[--f->engaged_elem_count];
            return true;
        }
    }
    return false;
}

TrackedButtons* get_tracked_buttons(int ptr_id) {
    // NOTE: hash collision silently overwrites — acceptable for single-touch primary finger tracking
    TrackedButtons* tb = &g_state.tracked[(uint32_t)ptr_id % MAX_FINGERS];
    if (__builtin_expect(tb->ptr_id != ptr_id, 0)) {
        tb->count = 0;
        tb->ptr_id = ptr_id;
    }
    return tb;
}

void handle_element_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    e->visual_active = true;
    e->visual_x = x;
    e->visual_y = y;
    mark_element_dirty(e);
    if (e->type >= ELEM_TYPE_COUNT) return;
    element_move_table[e->type](e, x, y, time_ms, result);
    // HOVER mode: non-toggle buttons are only visually active when finger is inside
    // (unless gesture/LP is active — see concept: button without primary transfers
    // visual activation to gesture/LP firing).
    // NOTE: Button is checked directly here (not via dispatch table) because this
    // is a post-dispatch visual-only rule that applies exclusively to buttons.
    // Other element types (dpad, stick, etc.) always keep visual_active during move.
    if (e->type == ELEM_BUTTON && e->activation_mode == ACTIVATION_HOVER
        && !e->cached_has_toggle && !point_in_element(x, y, e)
        && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
        && !e->gesture_toggled && !e->lp_toggled) {
        e->visual_active = false;
    }
}

void handle_element_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    int release_ptr_id = e->current_ptr_id;
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "handle_element_up[%d] type=%d ptr=%d", (int)(e - g_state.elements), e->type, e->current_ptr_id);
    if (e->type >= ELEM_TYPE_COUNT) { e->engaged = false; e->current_ptr_id = -1; return; }
    element_up_table[e->type](e, x, y, time_ms, result);
    e->engaged = false;
    e->current_ptr_id = -1;
    // NOTE: Cannot delegate to release_element_bindings() because:
    // 1. Element-specific up handlers (element_button_up etc.) already release
    //    primary bindings internally — calling release_element_bindings would double-release.
    // 2. This path must always release non-toggle gestures (release_non_toggle_gestures)
    //    without clearing gesture state flags, whereas release_element_bindings with
    //    release_gestures=true would also clear the flags (gesture_swipe_triggered etc.).
    // 3. release_element_bindings has its own early-return for no-binding elements,
    //    but this path must still clean up engaged state and visual state regardless.
    // Toggle buttons that still have any active toggle keep their visual activation
    if (!element_is_toggle_active(e))
        e->visual_active = false;
    mark_element_dirty(e);
    // Clean up leaked gesture flags: element-specific up may early-return
    // (e.g., toggle buttons that stay selected) without clearing these,
    // leaving gesture bindings pressed forever and breaking future gesture
    // detection on subsequent finger-downs.
    release_non_toggle_gestures(e, result);
    if (release_ptr_id >= 0) {
        TouchFinger* f = find_finger(release_ptr_id);
        if (__builtin_expect(f != NULL, 1))
            finger_remove_engaged(f, (int16_t)(e - g_state.elements));
    }
}

// WHEN: finger slides off element during gesture detection — preserves toggle state
void suppress_element_gestures(TouchElement* e, TouchActionResult* restrict result) {
    if (e->long_press_arm && e->bindings[0].type != BINDING_NONE)
        press_binding(result, &e->bindings[0], true);
    // Toggle state (lp_toggled/gesture_toggled) persists across touches and
    // slide-overs. Do NOT release toggle bindings or clear these flags here —
    // they are only cleared by the toggle's own LP/gesture alternation or
    // element_button_down's explicit deselect path.
    if (!e->lp_toggled && e->gesture_long_press_triggered) {
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
    }
    if (!e->gesture_toggled && e->gesture_swipe_triggered) {
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
    }
    clear_element_gesture_flags(e, true);
}

// WHEN: general teardown — releases primary binding (if not sticky/toggle-held) and
// optionally clears gesture state. Used by element-specific cleanup paths.
void release_element_bindings(TouchElement* e, TouchActionResult* restrict result, bool release_gestures) {
    // Always clean up engaged state, even if no bindings/gestures to release
    int release_ptr_id = e->current_ptr_id;
    e->current_ptr_id = -1;
    e->engaged = false;
    if (release_ptr_id >= 0) {
        TouchFinger* f = find_finger(release_ptr_id);
        if (__builtin_expect(f != NULL, 1))
            finger_remove_engaged(f, (int16_t)(e - g_state.elements));
    }

    if (!e->cached_has_any_binding && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered)
        return;
    if (e->bindings[0].type != BINDING_NONE && !(e->primary_sticky_mask & 1)
        && !((e->bindings[0].toggle || e->cached_has_toggle) && e->selected))
        release_binding(result, &e->bindings[0]);
    if (release_gestures) {
        release_non_toggle_gestures(e, result);
        clear_element_gesture_flags(e, false);
    } else {
        e->long_press_arm = false;
        e->gesture_timer_armed = false;
        e->visual_long_press_active = false;
    }
    if (!element_is_toggle_active(e))
        e->visual_active = false;
    mark_element_dirty(e);
}

void touch_finger_cache_bindings_state(TouchFinger* f) {
    const FingerBindings* b = &f->bindings;
    f->cached_has_active_single_tap = b->single_tap_count > 0;
    f->cached_has_active_double_tap = b->double_tap_count > 0;
    f->cached_has_active_long_press = b->long_press_count > 0;
    f->cached_has_active_single_tap_drag = b->single_tap_drag_count > 0;
    f->cached_has_active_long_press_drag = b->long_press_drag_count > 0;
    f->cached_has_active_double_tap_drag = b->double_tap_drag_count > 0;
    f->cached_has_long_press_timer = b->long_press_count > 0 || b->long_press_drag_count > 0;
}

void build_spatial_grid(void) {
    if (g_state.element_count == 0) {
        g_state.grid_cell_w = 0.0f;
        return;
    }
    
    float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        float hw = e->hw;
        float hh = e->hh;
        
        float x1 = (float)e->x - hw, y1 = (float)e->y - hh;
        float x2 = (float)e->x + hw, y2 = (float)e->y + hh;
        if (x1 < min_x) min_x = x1; if (y1 < min_y) min_y = y1;
        if (x2 > max_x) max_x = x2; if (y2 > max_y) max_y = y2;
    }
    
    float range_x = max_x - min_x;
    float range_y = max_y - min_y;
    if (range_x < 1.0f) range_x = 1.0f;
    if (range_y < 1.0f) range_y = 1.0f;
    
    g_state.grid_min_x = min_x;
    g_state.grid_min_y = min_y;
    g_state.grid_max_x = max_x;
    g_state.grid_max_y = max_y;
    g_state.grid_cell_w = range_x / GRID_COLS;
    g_state.grid_cell_h = range_y / GRID_ROWS;
    g_state.grid_inv_cell_w = 1.0f / g_state.grid_cell_w;
    g_state.grid_inv_cell_h = 1.0f / g_state.grid_cell_h;
    
    memset(g_state.spatial_grid_count, 0, sizeof(g_state.spatial_grid_count));
    
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        int cell = get_grid_cell((float)e->x, (float)e->y, min_x, min_y);
        int cnt = g_state.spatial_grid_count[cell];
        if (cnt < MAX_ELEMENTS) {
            g_state.spatial_grid[cell][cnt] = i;
            g_state.spatial_grid_count[cell] = cnt + 1;
        }
    }

    // Build flat array for O(1) cell iteration (consumed by handler.c, activation_mode.c)
    int offset = 0;
    for (int cell = 0; cell < GRID_COLS * GRID_ROWS; cell++) {
        g_state.grid_cell_start[cell] = offset;
        offset += g_state.spatial_grid_count[cell];
    }
    // grid_cell_start must have GRID_COLS * GRID_ROWS + 1 elements (sentinel at end)
    g_state.grid_cell_start[GRID_COLS * GRID_ROWS] = offset;

    uint8_t temp_count[GRID_COLS * GRID_ROWS];
    memset(temp_count, 0, sizeof(temp_count));
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        int cell = get_grid_cell((float)e->x, (float)e->y, min_x, min_y);
        if (temp_count[cell] < MAX_ELEMENTS)
            g_state.grid_cell_to_elems[g_state.grid_cell_start[cell] + temp_count[cell]++] = i;
    }
}

void element_reset_runtime(TouchElement* e) {
    e->current_ptr_id = -1;
    e->engaged = false;
    e->selected = false;
    e->gesture_suppressed = false;
    e->visual_active = false;

    // Bulk-zero gesture flags: long_press_arm, gesture_swipe_triggered,
    // gesture_long_press_triggered, gesture_timer_armed, lp_toggled,
    // gesture_toggled, auto_repeat_primary_pressed,
    // visual_long_press_active (8 bools contiguous from long_press_arm)
    // NOTE: defer_primary is intentionally NOT zeroed here.
    // WARNING: This assumes long_press_arm through visual_long_press_active are contiguous bools.
    // If struct layout changes, this must be updated.
    e->long_press_arm = false;
    e->gesture_swipe_triggered = false;
    e->gesture_long_press_triggered = false;
    e->gesture_timer_armed = false;
    e->lp_toggled = false;
    e->gesture_toggled = false;
    e->auto_repeat_primary_pressed = false;
    e->visual_long_press_active = false;

    e->auto_repeat_last_time = 0;
    e->toggle_debounce_last_time = 0;
    e->stick_value_x = 0.0f;
    e->stick_value_y = 0.0f;
    e->trackpad_vel_x = 0.0f;
    e->trackpad_vel_y = 0.0f;
    e->range_scrolling = false;
    e->range_hold_pressed = false;
    e->range_pending_tap_release = false;
    memset(e->petal_active, 0, sizeof(e->petal_active));
}

