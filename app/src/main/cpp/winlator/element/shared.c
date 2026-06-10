#include "../touch_processor_internal.h"

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
            int col = (int)((x - g_state.grid_min_x) / g_state.grid_cell_w);
            int row = (int)((y - g_state.grid_min_y) / g_state.grid_cell_h);

            if (col >= 0 && col < GRID_COLS && row >= 0 && row < GRID_ROWS) {
                int min_c = col > 0 ? col - 1 : 0;
                int max_c = col < GRID_COLS - 1 ? col + 1 : GRID_COLS - 1;
                int min_r = row > 0 ? row - 1 : 0;
                int max_r = row < GRID_ROWS - 1 ? row + 1 : GRID_ROWS - 1;

                for (int r = max_r; r >= min_r; r--) {
                    for (int c = max_c; c >= min_c; c--) {
                        int cell = r * GRID_COLS + c;
                        int cnt = g_state.spatial_grid_count[cell];
                        if (cnt > MAX_ELEMENTS) cnt = MAX_ELEMENTS;
                        for (int j = cnt - 1; j >= 0; j--) {
                            TouchElement* e = &g_state.elements[g_state.spatial_grid[cell][j]];
                            if (point_in_element(x, y, e))
                                return e;
                        }
                    }
                }
            }
        }
    }
    
    // Fallback: linear scan
    for (int i = g_state.element_count - 1; i >= 0; i--) {
        TouchElement* e = &g_state.elements[i];
        if (__builtin_expect(i - 4 >= 0, 1))
            __builtin_prefetch(&g_state.elements[i - 4], 0, 1);
        if (point_in_element(x, y, e))
            return e;
    }
    return NULL;
}

#define BEZIER_LUT_SIZE 256

static float g_bezier_lut[BEZIER_LUT_SIZE + 1];

void init_bezier_lut(void) {
    for (int i = 0; i <= BEZIER_LUT_SIZE; i++) {
        float x_target = (float)i / (float)BEZIER_LUT_SIZE;
        float lo = 0.0f, hi = 1.0f;
        for (int j = 0; j < 16; j++) {
            float t = (lo + hi) * 0.5f;
            float omt = 1.0f - t;
            float bx = 3.0f * omt * omt * t * 0.075f + 3.0f * omt * t * t * 0.45f + t * t * t;
            if (bx < x_target) lo = t;
            else hi = t;
        }
        float t = (lo + hi) * 0.5f;
        float omt = 1.0f - t;
        float by = 3.0f * omt * omt * t * 0.95f + 3.0f * omt * t * t * 0.95f + t * t * t;
        g_bezier_lut[i] = by;
    }
}

// Eager init: constructor runs before any touch events
__attribute__((constructor)) static void _auto_init_bezier(void) { init_bezier_lut(); }

float cubic_bezier_interpolate_trackpad(float x) {
    float abs_x = fabsf(x);
    if (abs_x <= 0.0001f) return 0.0f;
    if (abs_x >= 1.0f) return x > 0 ? 1.0f : -1.0f;
    float f = abs_x * (float)BEZIER_LUT_SIZE;
    int idx = (int)f;
    if (idx >= BEZIER_LUT_SIZE) return x > 0 ? 1.0f : -1.0f;
    float frac = f - (float)idx;
    float result = g_bezier_lut[idx] + frac * (g_bezier_lut[idx+1] - g_bezier_lut[idx]);
    return x > 0 ? result : -result;
}

int detect_swipe_dir(float dx, float dy, float threshold) {
    if (fabsf(dx) < threshold && fabsf(dy) < threshold) return -1;
    if (fabsf(dx) > fabsf(dy)) return dx > 0 ? 3 : 2;
    return dy > 0 ? 1 : 0;
}

void element_set_petals(TouchElement* e, float nx, float ny, float dead_zone, TouchActionResult* restrict result) {
    if (!e->cached_has_any_binding) return;
    bool raw_up = ny <= -dead_zone;
    bool raw_right = nx >= dead_zone;
    bool raw_down = ny >= dead_zone;
    bool raw_left = nx <= -dead_zone;
    bool states[4] = {raw_up, raw_right, raw_down, raw_left};
    bool mouse_move[4];
    for (int i = 0; i < 4; i++)
        mouse_move[i] = e->bindings[i].type != BINDING_NONE && is_mouse_move_binding(&e->bindings[i]);
    for (int i = 0; i < 4; i++) {
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

void handle_element_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    // Elements always take priority: cancel any pending double-tap gesture
    // so the touch engages the element instead of triggering gesture bindings.
    if (__builtin_expect(g_state.gesture_double_tap_waiting, 0)) {
        gesture_cancel_double_tap_wait(result);
    }
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "handle_element_down[%d] type=%d ptr=%d x=%.0f y=%.0f", (int)(e - g_state.elements), e->type, ptr_id, x, y);
    //LOG_SHARED("handle_element_down type=%d ptr=%d x=%.0f y=%.0f cur_ptr=%d engaged=%d b0=%d",
    //    e->type, ptr_id, x, y, e->current_ptr_id, e->engaged, e->bindings[0].type);
    // Java ControlElement.handleTouchDown: if (currentPointerId == -1 && containsPoint(x, y))
    // containsPoint is checked by the caller; guard already-engaged elements here.
    if (__builtin_expect(e->current_ptr_id >= 0, 0)) {
        //LOG_SHARED("  already engaged (ptr=%d), returning", e->current_ptr_id);
        //__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN skip: already engaged ptr=%d type=%d", ptr_id, e->type);
        return;
    }
    e->current_ptr_id = ptr_id;
    e->down_x = x;
    e->down_y = y;
    e->down_time_ms = time_ms;
    e->engaged = true;
    e->visual_active = true;
    e->visual_x = x;
    e->visual_y = y;
    mark_element_dirty(e);
    e->gesture_swipe_triggered = false;
    e->gesture_long_press_triggered = false;
    e->long_press_arm = false;
    e->gesture_timer_armed = false;
    e->gesture_suppressed = false;
    // Clear gesture/lp toggle flags on each new touch to prevent stale bindings
    // from previous touches persisting. Release any bindings that are still held.
    if (e->gesture_toggled) {
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        e->gesture_toggled = false;
    }
    if (e->lp_toggled) {
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        e->lp_toggled = false;
    }

    element_down_table[e->type](e, ptr_id, x, y, time_ms, result);

    int16_t elem_idx = (int16_t)(e - g_state.elements);
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(f != NULL, 1)) {
        uint8_t cnt = f->engaged_elem_count;
        if (__builtin_expect(cnt < 16, 1)) {
            f->engaged_elem_indices[cnt] = elem_idx;
            f->engaged_elem_count = cnt + 1;
        }
    }
}

void handle_element_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    e->visual_active = true;
    e->visual_x = x;
    e->visual_y = y;
    mark_element_dirty(e);
    element_move_table[e->type](e, x, y, time_ms, result);
    // HOVER mode: non-toggle buttons are only visually active when finger is inside
    // (unless gesture/LP is active — see concept: button without primary transfers
    // visual activation to gesture/LP firing).
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
    //__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "UP type=%d ptr=%d lp_trig=%d swipe=%d b0=%d",
    //    e->type, e->current_ptr_id, e->gesture_long_press_triggered, e->gesture_swipe_triggered, e->bindings[0].type);
    element_up_table[e->type](e, x, y, time_ms, result);
    e->engaged = false;
    e->current_ptr_id = -1;
    // Toggle buttons that still have any active toggle keep their visual activation
    if (!element_is_toggle_active(e))
        e->visual_active = false;
    mark_element_dirty(e);
    // Clean up leaked gesture flags: element-specific up may early-return
    // (e.g., toggle buttons that stay selected) without clearing these,
    // leaving gesture bindings pressed forever and breaking future gesture
    // detection on subsequent finger-downs.
    if (e->gesture_swipe_triggered) {
        if (!e->gesture_toggled)
            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        e->gesture_swipe_triggered = false;
    }
    if (e->gesture_long_press_triggered) {
        if (!e->lp_toggled)
            release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        e->gesture_long_press_triggered = false;
    }
    if (release_ptr_id >= 0) {
        TouchFinger* f = find_finger(release_ptr_id);
        if (__builtin_expect(f != NULL, 1)) {
            int16_t idx = (int16_t)(e - g_state.elements);
            uint8_t cnt = f->engaged_elem_count;
            for (uint8_t i = 0; i < cnt; i++) {
                if (f->engaged_elem_indices[i] == idx) {
                    f->engaged_elem_indices[i] = f->engaged_elem_indices[cnt - 1];
                    f->engaged_elem_count = cnt - 1;
                    break;
                }
            }
        }
    }
}

void suppress_element_gestures(TouchElement* e, TouchActionResult* restrict result) {
    if (e->long_press_arm && e->bindings[0].type != BINDING_NONE)
        press_binding(result, &e->bindings[0], true);
    if (e->lp_toggled) {
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        e->lp_toggled = false;
    } else if (e->gesture_long_press_triggered) {
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
    }
    if (e->gesture_toggled) {
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        e->gesture_toggled = false;
    } else if (e->gesture_swipe_triggered) {
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
    }
    e->long_press_arm = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_triggered = false;
    e->gesture_timer_armed = false;
    e->visual_long_press_active = false;
    e->gesture_suppressed = true;
}

void release_element_bindings(TouchElement* e, TouchActionResult* restrict result, bool release_gestures) {
    if (!e->cached_has_any_binding && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered)
        return;
    if (e->bindings[0].type != BINDING_NONE && !(e->primary_sticky_mask & 1)
        && !((e->bindings[0].toggle || e->cached_has_toggle) && e->selected))
        release_binding(result, &e->bindings[0]);
    if (release_gestures) {
        if (__builtin_expect(e->gesture_swipe_triggered, 0) && !e->gesture_toggled)
            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        if (__builtin_expect(e->gesture_long_press_triggered, 0) && !e->lp_toggled)
            release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        e->long_press_arm = false;
        e->gesture_long_press_triggered = false;
        e->gesture_swipe_triggered = false;
        e->gesture_timer_armed = false;
        e->visual_long_press_active = false;
    } else {
        e->long_press_arm = false;
        e->gesture_timer_armed = false;
        e->visual_long_press_active = false;
    }
    e->current_ptr_id = -1;
    e->engaged = false;
    if (!element_is_toggle_active(e))
        e->visual_active = false;
    mark_element_dirty(e);
}

void touch_finger_cache_bs(TouchFinger* f) {
    GestureBindingSet bs = gesture_build_binding_set(&f->bindings);
    f->cached_has_active_single_tap = bs.has_active_single_tap;
    f->cached_has_active_double_tap = bs.has_active_double_tap;
    f->cached_has_active_long_press = bs.has_active_long_press;
    f->cached_has_active_single_tap_drag = bs.has_active_single_tap_drag;
    f->cached_has_active_long_press_drag = bs.has_active_long_press_drag;
    f->cached_has_active_double_tap_drag = bs.has_active_double_tap_drag;
    f->cached_has_long_press_timer = bs.has_long_press_timer;
}

void build_spatial_grid(void) {
    if (g_state.element_count == 0) {
        g_state.grid_cell_w = 0.0f;
        return;
    }
    
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
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
        int col = (int)(((float)e->x - min_x) / g_state.grid_cell_w);
        int row = (int)(((float)e->y - min_y) / g_state.grid_cell_h);
        if (col < 0) col = 0; if (col >= GRID_COLS) col = GRID_COLS - 1;
        if (row < 0) row = 0; if (row >= GRID_ROWS) row = GRID_ROWS - 1;
        int cell = row * GRID_COLS + col;
        int cnt = g_state.spatial_grid_count[cell];
        if (cnt < MAX_ELEMENTS) {
            g_state.spatial_grid[cell][cnt] = i;
            g_state.spatial_grid_count[cell] = cnt + 1;
        }
    }

    int offset = 0;
    for (int cell = 0; cell < GRID_COLS * GRID_ROWS; cell++) {
        g_state.grid_cell_start[cell] = offset;
        offset += g_state.spatial_grid_count[cell];
    }
    g_state.grid_cell_start[GRID_COLS * GRID_ROWS] = offset;

    uint8_t temp_count[GRID_COLS * GRID_ROWS];
    memset(temp_count, 0, sizeof(temp_count));
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        int col = (int)(((float)e->x - min_x) / g_state.grid_cell_w);
        int row = (int)(((float)e->y - min_y) / g_state.grid_cell_h);
        if (col < 0) col = 0; if (col >= GRID_COLS) col = GRID_COLS - 1;
        if (row < 0) row = 0; if (row >= GRID_ROWS) row = GRID_ROWS - 1;
        int cell = row * GRID_COLS + col;
        g_state.grid_cell_to_elems[g_state.grid_cell_start[cell] + temp_count[cell]++] = i;
    }
}

void element_reset_runtime(TouchElement* e) {
    e->current_ptr_id = -1;
    e->engaged = false;
    e->gesture_suppressed = false;
    e->visual_active = false;
    e->visual_long_press_active = false;
    e->selected = false;
    e->long_press_arm = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_triggered = false;
    e->gesture_toggled = false;
    e->lp_toggled = false;
    e->gesture_timer_armed = false;
    e->auto_repeat_primary_pressed = false;
    e->auto_repeat_last_time = 0;
    e->toggle_debounce_last_time = 0;
    e->stick_value_x = 0.0f;
    e->stick_value_y = 0.0f;
    e->trackpad_vel_x = 0.0f;
    e->trackpad_vel_y = 0.0f;
    e->range_scrolling = false;
    e->range_hold_pressed = false;
    e->range_pending_tap_release = false;
    for (int p = 0; p < MAX_PETALS; p++) e->petal_active[p] = false;
}

