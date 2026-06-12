#include "touch_processor_internal.h"
#include "activation_mode.h"

int activation_get_visual_states(float* positions, uint8_t* active, int count) {
    int limit = count < g_state.element_count ? count : g_state.element_count;
    for (int i = 0; i < limit; i++) {
        positions[i * 2] = g_state.elements[i].visual_x;
        positions[i * 2 + 1] = g_state.elements[i].visual_y;
        active[i] = g_state.elements[i].visual_active ? 1 : 0;
    }
    return limit;
}

bool activation_get_element_visual(int elem_index, float* out_x, float* out_y, bool* out_active) {
    if (elem_index < 0 || elem_index >= g_state.element_count) return false;
    if (out_x) *out_x = g_state.elements[elem_index].visual_x;
    if (out_y) *out_y = g_state.elements[elem_index].visual_y;
    if (out_active) *out_active = g_state.elements[elem_index].visual_active;
    return true;
}

void activation_activate_at(float x, float y) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "activation_activate_at(%.0f,%.0f)", x, y);
    if (g_state.grid_cell_w > 0.0f && g_state.grid_cell_h > 0.0f) {
        if (x >= g_state.grid_min_x && x <= g_state.grid_max_x &&
            y >= g_state.grid_min_y && y <= g_state.grid_max_y) {
            int col = (int)((x - g_state.grid_min_x) * g_state.grid_inv_cell_w);
            int row = (int)((y - g_state.grid_min_y) * g_state.grid_inv_cell_h);
            int min_c = col > 0 ? col - 1 : 0;
            int max_c = col < GRID_COLS - 1 ? col + 1 : GRID_COLS - 1;
            int min_r = row > 0 ? row - 1 : 0;
            int max_r = row < GRID_ROWS - 1 ? row + 1 : GRID_ROWS - 1;
            for (int r = min_r; r <= max_r; r++) {
                for (int c = min_c; c <= max_c; c++) {
                    int cell = r * GRID_COLS + c;
                    for (int j = g_state.grid_cell_start[cell]; j < g_state.grid_cell_start[cell + 1]; j++) {
                        TouchElement* e = &g_state.elements[g_state.grid_cell_to_elems[j]];
                        bool hit = point_in_element(x, y, e);
                        e->visual_active = hit;
                        if (hit) {
                            TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "activation_activate_at[%d] type=%d visual=1 (hit)", (int)(e - g_state.elements), e->type);
                            e->visual_x = x;
                            e->visual_y = y;
                        }
                    }
                }
            }
        }
    } else {
        for (int i = 0; i < g_state.element_count; i++) {
            bool hit = point_in_element(x, y, &g_state.elements[i]);
            g_state.elements[i].visual_active = hit;
            if (hit) {
                TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "activation_activate_at[%d] type=%d visual=1 (hit)", i, g_state.elements[i].type);
                g_state.elements[i].visual_x = x;
                g_state.elements[i].visual_y = y;
            }
        }
    }
    mark_all_dirty();
}

void activation_deactivate_all(void) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "activation_deactivate_all count=%d", g_state.element_count);
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (!element_is_toggle_active(e)) {
            e->visual_active = false;
            e->visual_gesture_active = false;
        }
    }
    mark_all_dirty();
}

int activation_tracked_count(int ptr_id) {
    if (ptr_id < 0 || ptr_id >= MAX_FINGERS) return 0;
    TrackedButtons* tb = get_tracked_buttons(ptr_id);
    return tb->ptr_id == ptr_id ? tb->count : 0;
}

int activation_hovered_for_ptr(int ptr_id) {
    if (ptr_id < 0 || ptr_id >= MAX_FINGERS) return -1;
    return g_state.hovered_element_per_ptr[ptr_id];
}

static const ActivationModeParams* get_activation_params(void) {
    switch (g_state.activation_mode) {
        case ACTIVATION_LOCK:  return &ACTIVATION_MODE_LOCK;
        case ACTIVATION_TRACK: return &ACTIVATION_MODE_TRACK;
        case ACTIVATION_HOVER: return &ACTIVATION_MODE_HOVER;
        default:               return &ACTIVATION_MODE_LOCK;
    }
}

// ─── Legacy entry points — now thin wrappers over the unified API ────────────

__attribute__((hot))
bool activation_handle_down(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    return activation_mode_down(ptr_id, x, y, time_ms, result, get_activation_params());
}

__attribute__((hot))
void activation_handle_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    const ActivationModeParams* params = get_activation_params();
    activation_mode_move_nonbuttons(ptr_id, x, y, time_ms, result, params);
    activation_mode_move_buttons(ptr_id, x, y, time_ms, result, params);
}

bool activation_handle_up(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    return activation_mode_up(ptr_id, x, y, time_ms, result, get_activation_params());
}

void activation_reset(void) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "activation_reset count=%d", g_state.element_count);
    for (int i = 0; i < MAX_FINGERS; i++) {
        g_state.tracked[i].ptr_id = -1;
        g_state.tracked[i].count = 0;
        g_state.hovered_element_per_ptr[i] = -1;
    }
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (!element_is_toggle_active(e)) {
            e->visual_active = false;
            e->visual_gesture_active = false;
        }
    }
    mark_all_dirty();
}
