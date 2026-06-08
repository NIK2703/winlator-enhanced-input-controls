#include "touch_processor_internal.h"

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
    const bool has_toggle = g_state.cfg.caps_has_element_toggle;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        // Toggle buttons that are still selected keep their visual activation
        if (!has_toggle || !element_is_toggle_active(e))
            e->visual_active = false;
    }
    mark_all_dirty();
}

int activation_tracked_count(int ptr_id) {
    if (ptr_id < 0 || ptr_id >= MAX_FINGERS) return 0;
    if (g_state.tracked[ptr_id].ptr_id != ptr_id) return 0;
    return g_state.tracked[ptr_id].count;
}

int activation_hovered_for_ptr(int ptr_id) {
    if (ptr_id < 0 || ptr_id >= MAX_FINGERS) return -1;
    return g_state.hovered_element_per_ptr[ptr_id];
}

// --- Legacy Java-compatible handlers ---

static inline void toggle_slide_over(TouchElement* btn, TouchActionResult* restrict result) {
    if (btn->selected) {
        if (btn->bindings[0].type != BINDING_NONE)
            release_binding(result, &btn->bindings[0]);
        btn->selected = false;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=0 (deselect)", (int)(btn - g_state.elements));
        btn->visual_active = false;
    } else {
        if (btn->bindings[0].type != BINDING_NONE)
            press_binding(result, &btn->bindings[0], true);
        btn->selected = true;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=1 (select)", (int)(btn - g_state.elements));
        btn->visual_active = true;
    }
    mark_element_dirty(btn);
}

static inline void process_engaged_non_buttons(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(f == NULL, 0)) return;
    uint8_t cnt = f->engaged_elem_count;
    for (uint8_t i = 0; i < cnt; i++) {
        TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
        if (e->type != ELEM_BUTTON && e->current_ptr_id == ptr_id)
            handle_element_move(e, x, y, time_ms, result);
    }
}

static inline bool release_engaged_elements(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    bool handled = false;
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(f == NULL, 0)) return false;
    uint8_t cnt = f->engaged_elem_count;
    for (uint8_t i = 0; i < cnt; i++) {
        TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
        if (e->current_ptr_id == ptr_id) {
            handle_element_up(e, x, y, time_ms, result);
            handled = true;
        }
    }
    f->engaged_elem_count = 0;
    return handled;
}

__attribute__((hot))
bool activation_handle_down(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    if (__builtin_expect(g_state.element_count == 0, 0)) return false;

    bool handled = false;
    ActivationMode mode = g_state.activation_mode;

    // Fast path: all elements are ACTIVATION_LOCK — skip switch overhead
    if (!g_state.cfg.caps_has_track_hover_buttons)
        goto lock_handler;

    switch (mode) {
        case ACTIVATION_LOCK:
lock_handler: {
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
                                if (point_in_element(x, y, e)) {
                                    handle_element_down(e, ptr_id, x, y, time_ms, result);
                                    handled = true;
                                }
                            }
                        }
                    }
                }
            } else {
                for (int i = 0; i < g_state.element_count; i++) {
                    if (point_in_element(x, y, &g_state.elements[i])) {
                        handle_element_down(&g_state.elements[i], ptr_id, x, y, time_ms, result);
                        handled = true;
                    }
                }
            }
            break;
        }
        case ACTIVATION_TRACK:
        case ACTIVATION_HOVER: {
            TouchElement* btn = NULL;
            int hit_count = 0;
            int gcol = -1, grow = -1;
            bool used_grid = (g_state.grid_cell_w > 0.0f && g_state.grid_cell_h > 0.0f);
            if (used_grid) {
                if (x >= g_state.grid_min_x && x <= g_state.grid_max_x &&
                    y >= g_state.grid_min_y && y <= g_state.grid_max_y) {
                    gcol = (int)((x - g_state.grid_min_x) * g_state.grid_inv_cell_w);
                    grow = (int)((y - g_state.grid_min_y) * g_state.grid_inv_cell_h);
                    int min_c = gcol > 0 ? gcol - 1 : 0;
                    int max_c = gcol < GRID_COLS - 1 ? gcol + 1 : GRID_COLS - 1;
                    int min_r = grow > 0 ? grow - 1 : 0;
                    int max_r = grow < GRID_ROWS - 1 ? grow + 1 : GRID_ROWS - 1;
                    for (int r = min_r; r <= max_r; r++) {
                        for (int c = min_c; c <= max_c; c++) {
                            int cell = r * GRID_COLS + c;
                            for (int j = g_state.grid_cell_start[cell]; j < g_state.grid_cell_start[cell + 1]; j++) {
                                TouchElement* e = &g_state.elements[g_state.grid_cell_to_elems[j]];
                                if (!point_in_element(x, y, e)) continue;
                                hit_count++;
                                if (e->type != ELEM_BUTTON) {
                                    handle_element_down(e, ptr_id, x, y, time_ms, result);
                                    handled = true;
                                } else if (!btn) {
                                    btn = e;
                                }
                            }
                        }
                    }
                }
            } else {
                for (int i = 0; i < g_state.element_count; i++) {
                    TouchElement* e = &g_state.elements[i];
                    if (!point_in_element(x, y, e)) continue;
                    hit_count++;
                    if (e->type != ELEM_BUTTON) {
                        handle_element_down(e, ptr_id, x, y, time_ms, result);
                        handled = true;
                    } else if (!btn) {
                        btn = e;
                    }
                }
            }
            if (__builtin_expect(hit_count == 0 && used_grid, 0)) {
                __android_log_print(ANDROID_LOG_WARN, "Winlator_DBG", "DOWN_TRACK_HOVER: 0 hits at (%.0f,%.0f) cell(%d,%d) grid(%.0f-%.0f,%.0f-%.0f) cw=%.1f ch=%.1f ec=%d",
                    x, y, gcol, grow, g_state.grid_min_x, g_state.grid_max_x, g_state.grid_min_y, g_state.grid_max_y,
                    g_state.grid_cell_w, g_state.grid_cell_h, g_state.element_count);
                for (int i = 0; i < g_state.element_count; i++) {
                    TouchElement* e = &g_state.elements[i];
                    if (point_in_element(x, y, e)) {
                        int ecol = (int)(((float)e->x - g_state.grid_min_x) * g_state.grid_inv_cell_w);
                        int erow = (int)(((float)e->y - g_state.grid_min_y) * g_state.grid_inv_cell_h);
                        __android_log_print(ANDROID_LOG_WARN, "Winlator_DBG", "  MISSED[%d] type=%d pos(%d,%d) hw=%.0f hh=%.0f grid_cell(%d,%d)",
                            i, e->type, e->x, e->y, e->hw, e->hh, ecol, erow);
                    }
                }
                // Linear fallback when grid misses
                for (int i = 0; i < g_state.element_count; i++) {
                    TouchElement* e = &g_state.elements[i];
                    if (!point_in_element(x, y, e)) continue;
                    hit_count++;
                    if (e->type != ELEM_BUTTON) {
                        handle_element_down(e, ptr_id, x, y, time_ms, result);
                        handled = true;
                    } else if (!btn) {
                        btn = e;
                    }
                }
                __android_log_print(ANDROID_LOG_WARN, "Winlator_DBG", "  FALLBACK found=%d en=%d", hit_count, handled);
            }
            // Button at point: handle with tracking
            if (btn && btn->type == ELEM_BUTTON) {
                int btn_idx = (int)(btn - g_state.elements);
                // In track/hover mode, skip OFF toggle switches to prevent accidental activation
                bool skip_toggle = btn->cached_has_toggle && !btn->selected && !btn->lp_toggled && !btn->gesture_toggled;
#ifndef NDEBUG
                __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "DOWN_HOVER[%d] skip_tog=%d sel=%d lp_tog=%d gest_tog=%d mode=%s",
                    btn_idx, skip_toggle, btn->selected, btn->lp_toggled, btn->gesture_toggled,
                    mode == 0 ? "LOCK" : mode == 1 ? "TRACK" : "HOVER");
#endif

                TrackedButtons* tb = &g_state.tracked[(uint32_t)ptr_id % MAX_FINGERS];
                if (tb->ptr_id != ptr_id) {
                    tb->ptr_id = ptr_id;
                    tb->count = 0;
                }
                if (!skip_toggle) {
                    bool already = false;
                    for (int j = 0; j < tb->count; j++) {
                        if (tb->element_indices[j] == btn_idx) {
                            already = true;
                            break;
                        }
                    }
                    if (!already && tb->count < MAX_TRACKED_PER_POINTER) {
#ifndef NDEBUG
                        __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "DOWN_HOVER[%d] tracking slot=%d", btn_idx, tb->count);
#endif
                        tb->element_indices[tb->count++] = btn_idx;
                        handle_element_down(btn, ptr_id, x, y, time_ms, result);
                    } else {
#ifndef NDEBUG
                        __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "DOWN_HOVER[%d] already tracked count=%d", btn_idx, tb->count);
#endif
                    }
                } else {
#ifndef NDEBUG
                    __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "DOWN_HOVER[%d] SKIPPED (off toggle)", btn_idx);
#endif
                }
                if (mode == ACTIVATION_HOVER && !skip_toggle) {
                    g_state.hovered_element_per_ptr[(uint32_t)ptr_id % MAX_FINGERS] = btn_idx;
#ifndef NDEBUG
                    __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "DOWN_HOVER[%d] set hovered ptr_slot=%d", btn_idx, (uint32_t)ptr_id % MAX_FINGERS);
#endif
                }
                handled = true;
            }
            break;
        }
    }
    return handled;
}

__attribute__((hot))
void activation_handle_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    if (__builtin_expect(g_state.element_count == 0, 0)) return;

    ActivationMode mode = g_state.activation_mode;

    // Fast path: all elements are ACTIVATION_LOCK — skip switch overhead
    if (!g_state.cfg.caps_has_track_hover_buttons)
        goto lock_handler_move;

    switch (mode) {
        case ACTIVATION_LOCK:
lock_handler_move: {
            TouchFinger* f = find_finger(ptr_id);
            if (__builtin_expect(f != NULL, 1)) {
                uint8_t cnt = f->engaged_elem_count;
                for (uint8_t i = 0; i < cnt; i++) {
                    TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
                    if (e->current_ptr_id == ptr_id)
                        handle_element_move(e, x, y, time_ms, result);
                }
            }
            break;
        }
        case ACTIVATION_TRACK: {
            process_engaged_non_buttons(ptr_id, x, y, time_ms, result);
            // Tracked buttons: check for new button at position
            TrackedButtons* tb = &g_state.tracked[(uint32_t)ptr_id % MAX_FINGERS];
            if (tb->ptr_id == ptr_id && tb->count > 0) {
                TouchElement* btn = hit_test_element(x, y);
                if (btn && btn->type == ELEM_BUTTON) {
                    if (btn->cached_has_toggle) {
                        toggle_slide_over(btn, result);
                    } else {
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
                            if (tb->count > 1) {
                                suppress_element_gestures(btn, result);
                            }
                            if (tb->count == 2) {
                                TouchElement* first = &g_state.elements[tb->element_indices[0]];
                                first->long_press_arm = false;
                                first->gesture_long_press_triggered = false;
                            }
                        }
                    }
                }
            }
            break;
        }
        case ACTIVATION_HOVER: {
            process_engaged_non_buttons(ptr_id, x, y, time_ms, result);
            // Hover transitions: prev.deactivate(), curr.activate()
            int pid_slot = (uint32_t)ptr_id % MAX_FINGERS;
            int prev_idx = g_state.hovered_element_per_ptr[pid_slot];
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER ptr=%d slot=%d prev=%d", ptr_id, pid_slot, prev_idx);
#endif
            TrackedButtons* tb = &g_state.tracked[pid_slot];
            if (tb->ptr_id == ptr_id && tb->count > 0) {
                TouchElement* btn = hit_test_element(x, y);
                int curr_idx = btn ? (int)(btn - g_state.elements) : -1;
#ifndef NDEBUG
                __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER curr=%d tracked_count=%d", curr_idx, tb->count);
#endif
                if (curr_idx != prev_idx) {
                    // Deactivate previous
                    if (prev_idx >= 0 && prev_idx < g_state.element_count) {
#ifndef NDEBUG
                        __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER deactivate prev[%d]", prev_idx);
#endif
                        release_element_bindings(&g_state.elements[prev_idx], result);
                    }
                    // Activate current
                    if (btn && btn->type == ELEM_BUTTON) {
                        if (btn->cached_has_toggle) {
#ifndef NDEBUG
                            __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER curr[%d] toggle_slide_over", curr_idx);
#endif
                            toggle_slide_over(btn, result);
                            g_state.hovered_element_per_ptr[pid_slot] = -1;
                        } else {
                            bool already = false;
                            for (int j = 0; j < tb->count; j++) {
                                if (tb->element_indices[j] == curr_idx) {
                                    already = true;
                                    break;
                                }
                            }
                            if (!already && tb->count < MAX_TRACKED_PER_POINTER) {
#ifndef NDEBUG
                                __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER curr[%d] track slot=%d", curr_idx, tb->count);
#endif
                                tb->element_indices[tb->count++] = curr_idx;
                                if (tb->count == 2) {
                                    TouchElement* first = &g_state.elements[tb->element_indices[0]];
                                    first->long_press_arm = false;
                                    first->gesture_long_press_triggered = false;
#ifndef NDEBUG
                                    __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER second_btn cleared first[%d] lp_arm/gest_lp", tb->element_indices[0]);
#endif
                                }
                            } else {
#ifndef NDEBUG
                                __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER curr[%d] already tracked (re-activate)", curr_idx);
#endif
                            }
                            handle_element_down(btn, ptr_id, x, y, time_ms, result);
                            if (tb->count > 1 || already) {
#ifndef NDEBUG
                                __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER curr[%d] suppress_gestures (cnt=%d already=%d)", curr_idx, tb->count, already);
#endif
                                suppress_element_gestures(btn, result);
                            }
                            g_state.hovered_element_per_ptr[pid_slot] = curr_idx;
                        }
                    } else {
#ifndef NDEBUG
                        __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER no_button_at_curr curr=%d", curr_idx);
#endif
                        g_state.hovered_element_per_ptr[pid_slot] = -1;
                    }
                } else {
#ifndef NDEBUG
                    __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "MOVE_HOVER same_element curr=prev=%d (no transition)", curr_idx);
#endif
                }
            }
            break;
        }
    }
}

bool activation_handle_up(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    if (__builtin_expect(g_state.element_count == 0, 0)) return false;

    // Fast path: all elements are ACTIVATION_LOCK — skip switch overhead
    if (!g_state.cfg.caps_has_track_hover_buttons) {
        return release_engaged_elements(ptr_id, x, y, time_ms, result);
    }

    bool handled = false;
    int pid_slot = (uint32_t)ptr_id % MAX_FINGERS;
    ActivationMode mode = g_state.activation_mode;

    switch (mode) {
        case ACTIVATION_LOCK:
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "UP_LOCK ptr=%d slot=%d", ptr_id, pid_slot);
#endif
            goto up_release;
        case ACTIVATION_HOVER:
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "UP_HOVER ptr=%d slot=%d", ptr_id, pid_slot);
#endif
up_release:
            handled = release_engaged_elements(ptr_id, x, y, time_ms, result);
            break;
        case ACTIVATION_TRACK: {
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "UP_TRACK ptr=%d slot=%d tracked_count=%d", ptr_id, pid_slot, g_state.tracked[pid_slot].count);
#endif
            // Deactivate all tracked buttons for this pointer
            TrackedButtons* tb = &g_state.tracked[pid_slot];
            if (tb->ptr_id == ptr_id) {
                for (int j = 0; j < tb->count; j++) {
                    int idx = tb->element_indices[j];
                    if (idx >= 0 && idx < g_state.element_count) {
#ifndef NDEBUG
                        __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "UP_TRACK release[%d]", idx);
#endif
                        handle_element_up(&g_state.elements[idx], x, y, time_ms, result);
                    }
                }
                tb->count = 0;
                tb->ptr_id = -1;
                handled = true;
            }
            break;
        }
    }
    g_state.hovered_element_per_ptr[pid_slot] = -1;
#ifndef NDEBUG
    __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "UP ptr=%d slot=%d hovered=-1", ptr_id, pid_slot);
#endif
    return handled;
}

void activation_reset(void) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "activation_reset count=%d", g_state.element_count);
    for (int i = 0; i < MAX_FINGERS; i++) {
        g_state.tracked[i].ptr_id = -1;
        g_state.tracked[i].count = 0;
        g_state.hovered_element_per_ptr[i] = -1;
    }
    const bool has_toggle = g_state.cfg.caps_has_element_toggle;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        // Toggle buttons that are still selected keep their visual activation
        if (!has_toggle || !element_is_toggle_active(e))
            e->visual_active = false;
    }
    mark_all_dirty();
}
