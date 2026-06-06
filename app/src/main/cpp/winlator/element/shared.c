#include "../touch_processor_internal.h"

bool point_in_element(float px, float py, const TouchElement* e) {
    if (e->shape == SHAPE_CIRCLE) {
        float dx = px - (float)e->x;
        float dy = py - (float)e->y;
        return (dx * dx + dy * dy) <= (e->hw * e->hw);
    }
    return px >= e->x - e->hw && px <= e->x + e->hw &&
           py >= e->y - e->hh && py <= e->y + e->hh;
}

TouchElement* hit_test_element(float x, float y) {
    // Use spatial grid if available
    if (g_state.grid_cell_w > 0.0f && g_state.grid_cell_h > 0.0f) {
        // Early exit: point outside spatial grid bounds cannot hit any element
        if (x < g_state.grid_min_x || x > g_state.grid_min_x + GRID_COLS * g_state.grid_cell_w ||
            y < g_state.grid_min_y || y > g_state.grid_min_y + GRID_ROWS * g_state.grid_cell_h)
            return NULL;

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
    
    // Fallback: linear scan
    for (int i = g_state.element_count - 1; i >= 0; i--) {
        TouchElement* e = &g_state.elements[i];
        if (point_in_element(x, y, e))
            return e;
    }
    return NULL;
}

// Cubic bezier interpolation: given input x in [0,1] and control points (0,0), (cpx1,cpy1), (0.45,0.95), (1,1),
// find t where B_x(t) ≈ |x|, then return B_y(t) preserving sign.
// Control points match Java's CubicBezierInterpolator.set(0.075f, 0.95f, 0.45f, 0.95f).
float cubic_bezier_interpolate(float x, float cpx1, float cpy1) {
    float abs_x = fabsf(x);
    if (abs_x <= 0.0001f) return 0.0f;
    if (abs_x >= 1.0f) return x > 0 ? 1.0f : -1.0f;

    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 16; i++) {
        float mid = (lo + hi) * 0.5f;
        float omt = 1.0f - mid;
        float m2 = mid * mid;
        float m3 = m2 * mid;
        float omt2 = omt * omt;
        float bx = 3.0f * omt2 * mid * cpx1 + 3.0f * omt * m2 * 0.45f + m3;
        if (bx < abs_x) lo = mid;
        else hi = mid;
    }
    float t = (lo + hi) * 0.5f;
    float omt = 1.0f - t;
    float t2 = t * t;
    float t3 = t2 * t;
    float by = 3.0f * omt * omt * t * cpy1 + 3.0f * omt * t2 * 0.95f + t3;
    return x > 0 ? by : -by;
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

bool is_mouse_move_binding(const TouchBinding* b) {
    return b->type >= BINDING_MOUSE_MOVE_LEFT && b->type <= BINDING_MOUSE_MOVE_DOWN;
}

void element_set_petals(TouchElement* e, float nx, float ny, float dead_zone, TouchActionResult* result) {
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
        if (mouse_move[i])
            active = active || states[(i + 2) % 4];
        if (active != e->petal_active[i]) {
            e->petal_active[i] = active;
            if (active) press_binding(result, b, true);
            else release_binding(result, b);
        }
    }
}

bool finger_has_engaged_element(int ptr_id) {
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].engaged && g_state.elements[i].current_ptr_id == ptr_id)
            return true;
    }
    return false;
}

void handle_element_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    //LOG_SHARED("handle_element_down type=%d ptr=%d x=%.0f y=%.0f cur_ptr=%d engaged=%d b0=%d",
    //    e->type, ptr_id, x, y, e->current_ptr_id, e->engaged, e->bindings[0].type);
    // Java ControlElement.handleTouchDown: if (currentPointerId == -1 && containsPoint(x, y))
    // containsPoint is checked by the caller; guard already-engaged elements here.
    if (e->current_ptr_id >= 0) {
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
    g_state.visual_state_dirty = true;
    e->gesture_swipe_triggered = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_direction = -1;
    e->long_press_arm = false;
    e->gesture_timer_armed = false;
    e->gesture_suppressed = false;

    switch (e->type) {
        case ELEM_BUTTON: element_button_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_down(e, ptr_id, x, y, time_ms, result); break;
        case ELEM_RANGE_BUTTON: element_range_button_down(e, ptr_id, x, y, time_ms, result); break;
    }
    //LOG_SHARED("  after switch: result_count=%d", result->count);
}

void handle_element_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    e->visual_active = true;
    e->visual_x = x;
    e->visual_y = y;
    g_state.visual_state_dirty = true;
    switch (e->type) {
        case ELEM_BUTTON: element_button_move(e, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_move(e, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_move(e, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_move(e, x, y, time_ms, result); break;
        case ELEM_RANGE_BUTTON: element_range_button_move(e, x, y, time_ms, result); break;
    }
    // HOVER mode: non-toggle buttons are only visually active when finger is inside.
    // This runs after element_button_move (which may return early on gesture trigger)
    // and overrides any visual_active=true set above for non-hovered buttons.
    if (e->type == ELEM_BUTTON && e->activation_mode == ACTIVATION_HOVER
        && !e->toggle_switch && !point_in_element(x, y, e)) {
        e->visual_active = false;
    }
}

void handle_element_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    //__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "UP type=%d ptr=%d lp_trig=%d swipe=%d b0=%d",
    //    e->type, e->current_ptr_id, e->gesture_long_press_triggered, e->gesture_swipe_triggered, e->bindings[0].type);
    switch (e->type) {
        case ELEM_BUTTON: element_button_up(e, x, y, time_ms, result); break;
        case ELEM_DPAD: element_dpad_up(e, x, y, time_ms, result); break;
        case ELEM_STICK: element_stick_up(e, x, y, time_ms, result); break;
        case ELEM_TRACKPAD: element_trackpad_up(e, x, y, time_ms, result); break;
        case ELEM_RANGE_BUTTON: element_range_button_up(e, x, y, time_ms, result); break;
    }
    g_state.visual_state_dirty = true;
}

void suppress_element_gestures(TouchElement* e, TouchActionResult* result) {
    //LOG_SHARED("suppress_element_gestures type=%d b0=%d lp_arm=%d",
    //    e->type, e->bindings[0].type, e->long_press_arm);
    if (e->long_press_arm && e->bindings[0].type != BINDING_NONE)
        press_binding(result, &e->bindings[0], true);
    e->long_press_arm = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_triggered = false;
    e->gesture_timer_armed = false;
    e->gesture_suppressed = true;
}

void release_element_bindings(TouchElement* e, TouchActionResult* result) {
    if (e->bindings[0].type != BINDING_NONE && !(e->primary_sticky_mask & 1))
        release_binding(result, &e->bindings[0]);
    if (e->gesture_swipe_triggered)
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
    if (e->gesture_long_press_triggered)
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
    e->long_press_arm = false;
    e->gesture_long_press_triggered = false;
    e->gesture_swipe_triggered = false;
    e->gesture_timer_armed = false;
    e->current_ptr_id = -1;
    e->engaged = false;
    e->visual_active = false;
    g_state.visual_state_dirty = true;
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
    g_state.grid_cell_w = range_x / GRID_COLS;
    g_state.grid_cell_h = range_y / GRID_ROWS;
    
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
}

