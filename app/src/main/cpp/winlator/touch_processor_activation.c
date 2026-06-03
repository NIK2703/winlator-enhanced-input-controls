#include "touch_processor_internal.h"

ActivationMode activation_get_mode(int elem_index) {
    if (elem_index < 0 || elem_index >= g_state.element_count) return ACTIVATION_LOCK;
    return g_state.elements[elem_index].activation_mode;
}

void activation_set_mode_all(ActivationMode mode) {
    for (int i = 0; i < g_state.element_count; i++) {
        g_state.elements[i].activation_mode = mode;
    }
}

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
    for (int i = 0; i < g_state.element_count; i++) {
        bool hit = point_in_element(x, y, &g_state.elements[i]);
        g_state.elements[i].visual_active = hit;
        if (hit) {
            g_state.elements[i].visual_x = x;
            g_state.elements[i].visual_y = y;
        }
    }
}

void activation_deactivate_all(void) {
    for (int i = 0; i < g_state.element_count; i++) {
        g_state.elements[i].visual_active = false;
    }
}

int activation_tracked_count(int ptr_id) {
    if (ptr_id < 0 || ptr_id >= MAX_FINGERS) return 0;
    if (g_state.tracked[ptr_id].ptr_id != ptr_id) return 0;
    return g_state.tracked[ptr_id].count;
}

int activation_tracked_at(int ptr_id, int index) {
    if (ptr_id < 0 || ptr_id >= MAX_FINGERS) return -1;
    if (g_state.tracked[ptr_id].ptr_id != ptr_id) return -1;
    if (index < 0 || index >= g_state.tracked[ptr_id].count) return -1;
    return g_state.tracked[ptr_id].element_indices[index];
}

int activation_hovered_for_ptr(int ptr_id) {
    if (ptr_id < 0 || ptr_id >= MAX_FINGERS) return -1;
    return g_state.hovered_element_per_ptr[ptr_id];
}

// --- Legacy Java-compatible handlers ---

bool activation_handle_down(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    bool handled = false;
    ActivationMode mode = g_state.element_count > 0 ?
        g_state.elements[0].activation_mode : ACTIVATION_LOCK;

    switch (mode) {
        case ACTIVATION_LOCK: {
            for (int i = 0; i < g_state.element_count; i++) {
                if (point_in_element(x, y, &g_state.elements[i])) {
                    handle_element_down(&g_state.elements[i], ptr_id, x, y, time_ms, result);
                    handled = true;
                }
            }
            break;
        }
        case ACTIVATION_TRACK:
        case ACTIVATION_HOVER: {
            // Non-button elements: handle directly
            for (int i = 0; i < g_state.element_count; i++) {
                TouchElement* e = &g_state.elements[i];
                if (e->type != ELEM_BUTTON && point_in_element(x, y, e)) {
                    handle_element_down(e, ptr_id, x, y, time_ms, result);
                    handled = true;
                }
            }
            // Button at point: handle with tracking
            TouchElement* btn = hit_test_element(x, y);
            if (btn && btn->type == ELEM_BUTTON) {
                if (!btn->passthrough_touch) {
                    TrackedButtons* tb = &g_state.tracked[ptr_id % MAX_FINGERS];
                    if (tb->ptr_id != ptr_id) {
                        tb->ptr_id = ptr_id;
                        tb->count = 0;
                    }
                    bool already = false;
                    for (int j = 0; j < tb->count; j++) {
                        if (tb->element_indices[j] == (int)(btn - g_state.elements)) {
                            already = true;
                            break;
                        }
                    }
                    if (!already && tb->count < MAX_TRACKED_PER_POINTER) {
                        tb->element_indices[tb->count++] = (int)(btn - g_state.elements);
                        handle_element_down(btn, ptr_id, x, y, time_ms, result);
                    }
                } else {
                    handle_element_down(btn, ptr_id, x, y, time_ms, result);
                }
                if (mode == ACTIVATION_HOVER) {
                    g_state.hovered_element_per_ptr[ptr_id % MAX_FINGERS] = (int)(btn - g_state.elements);
                }
                if (!btn->passthrough_touch) {
                    handled = true;
                }
            }
            break;
        }
    }
    return handled;
}

void activation_handle_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    ActivationMode mode = g_state.element_count > 0 ?
        g_state.elements[0].activation_mode : ACTIVATION_LOCK;

    switch (mode) {
        case ACTIVATION_LOCK: {
            // Process all engaged elements touch move
            for (int i = 0; i < g_state.element_count; i++) {
                if (g_state.elements[i].current_ptr_id == ptr_id) {
                    handle_element_move(&g_state.elements[i], x, y, time_ms, result);
                }
            }
            break;
        }
        case ACTIVATION_TRACK: {
            // Process non-button engaged elements
            for (int i = 0; i < g_state.element_count; i++) {
                TouchElement* e = &g_state.elements[i];
                if (e->current_ptr_id == ptr_id && e->type != ELEM_BUTTON) {
                    handle_element_move(e, x, y, time_ms, result);
                }
            }
            // Tracked buttons: check for new button at position
            TrackedButtons* tb = &g_state.tracked[ptr_id % MAX_FINGERS];
            if (tb->ptr_id == ptr_id && tb->count > 0) {
                TouchElement* btn = hit_test_element(x, y);
                if (btn && btn->type == ELEM_BUTTON && !btn->passthrough_touch) {
                    bool already = false;
                    for (int j = 0; j < tb->count; j++) {
                        if (tb->element_indices[j] == (int)(btn - g_state.elements)) {
                            already = true;
                            break;
                        }
                    }
                    if (!already && tb->count < MAX_TRACKED_PER_POINTER) {
                        tb->element_indices[tb->count++] = (int)(btn - g_state.elements);
                        handle_element_down(btn, ptr_id, x, y, time_ms, result);
                        if (tb->count == 2) {
                            TouchElement* first = &g_state.elements[tb->element_indices[0]];
                            first->long_press_arm = false;
                            first->gesture_long_press_triggered = false;
                        }
                    }
                }
            }
            break;
        }
        case ACTIVATION_HOVER: {
            // Process non-button engaged elements
            for (int i = 0; i < g_state.element_count; i++) {
                TouchElement* e = &g_state.elements[i];
                if (e->current_ptr_id == ptr_id && e->type != ELEM_BUTTON) {
                    handle_element_move(e, x, y, time_ms, result);
                }
            }
            // Hover transitions: prev.deactivate(), curr.activate()
            int pid_slot = ptr_id % MAX_FINGERS;
            int prev_idx = g_state.hovered_element_per_ptr[pid_slot];
            TrackedButtons* tb = &g_state.tracked[pid_slot];
            if (tb->ptr_id == ptr_id && tb->count > 0) {
                TouchElement* btn = hit_test_element(x, y);
                int curr_idx = btn ? (int)(btn - g_state.elements) : -1;
                if (curr_idx != prev_idx) {
                    // Deactivate previous
                    if (prev_idx >= 0 && prev_idx < g_state.element_count) {
                        release_element_bindings(&g_state.elements[prev_idx], result);
                    }
                    // Activate current
                    if (btn && btn->type == ELEM_BUTTON) {
                        if (!btn->passthrough_touch) {
                            bool already = false;
                            for (int j = 0; j < tb->count; j++) {
                                if (tb->element_indices[j] == curr_idx) {
                                    already = true;
                                    break;
                                }
                            }
                            if (!already && tb->count < MAX_TRACKED_PER_POINTER) {
                                tb->element_indices[tb->count++] = curr_idx;
                                if (tb->count == 2) {
                                    TouchElement* first = &g_state.elements[tb->element_indices[0]];
                                    first->long_press_arm = false;
                                    first->gesture_long_press_triggered = false;
                                }
                            }
                        }
                        handle_element_down(btn, ptr_id, x, y, time_ms, result);
                        g_state.hovered_element_per_ptr[pid_slot] = curr_idx;
                    } else {
                        g_state.hovered_element_per_ptr[pid_slot] = -1;
                    }
                }
            }
            break;
        }
    }
}

bool activation_handle_up(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    bool handled = false;
    ActivationMode mode = g_state.element_count > 0 ?
        g_state.elements[0].activation_mode : ACTIVATION_LOCK;

    switch (mode) {
        case ACTIVATION_LOCK:
        case ACTIVATION_HOVER: {
            // Release element at pointer
            for (int i = 0; i < g_state.element_count; i++) {
                if (g_state.elements[i].current_ptr_id == ptr_id) {
                    handle_element_up(&g_state.elements[i], x, y, time_ms, result);
                    handled = true;
                }
            }
            break;
        }
        case ACTIVATION_TRACK: {
            // Deactivate all tracked buttons for this pointer
            int pid_slot = ptr_id % MAX_FINGERS;
            TrackedButtons* tb = &g_state.tracked[pid_slot];
            if (tb->ptr_id == ptr_id) {
                for (int j = 0; j < tb->count; j++) {
                    int idx = tb->element_indices[j];
                    if (idx >= 0 && idx < g_state.element_count) {
                        handle_element_up(&g_state.elements[idx], x, y, time_ms, result);
                    }
                }
                tb->count = 0;
                tb->ptr_id = -1;
                handled = true;
                // Clear hovered for this pointer in HOVER mode
                if (mode == ACTIVATION_HOVER) {
                    g_state.hovered_element_per_ptr[pid_slot] = -1;
                }
            }
            break;
        }
    }
    return handled;
}

void activation_reset(void) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        g_state.tracked[i].ptr_id = -1;
        g_state.tracked[i].count = 0;
        g_state.hovered_element_per_ptr[i] = -1;
    }
    for (int i = 0; i < g_state.element_count; i++) {
        g_state.elements[i].visual_active = false;
    }
}
