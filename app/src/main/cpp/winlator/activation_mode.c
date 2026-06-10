#include "touch_processor_internal.h"
#include "activation_mode.h"

// ─── Mode constants ──────────────────────────────────────────────────────────

const ActivationModeParams ACTIVATION_MODE_LOCK = {
    .down_button = BTN_DOWN_ALL,
    .down_set_hovered = false,
    .down_skip_off_toggle = false,
    .move_nonbutton = NONBTN_MOVE_LOCK,
    .move_button = BTN_MOVE_LOCK,
    .move_gesture_move = true,
    .up_release = UP_RELEASE_ENGAGED,
    .up_clear_hovered = false,
    .up_reset_tracked = false,
    .skip_reentry_down = false,
};

const ActivationModeParams ACTIVATION_MODE_TRACK = {
    .down_button = BTN_DOWN_FIRST,
    .down_set_hovered = false,
    .down_skip_off_toggle = true,
    .move_nonbutton = NONBTN_MOVE_ACCUM,
    .move_button = BTN_MOVE_ACCUM,
    .move_gesture_move = false,
    .up_release = UP_RELEASE_TRACKED,
    .up_clear_hovered = false,
    .up_reset_tracked = false,
    .skip_reentry_down = false,
};

const ActivationModeParams ACTIVATION_MODE_HOVER = {
    .down_button = BTN_DOWN_FIRST,
    .down_set_hovered = true,
    .down_skip_off_toggle = true,
    .move_nonbutton = NONBTN_MOVE_HOVER,
    .move_button = BTN_MOVE_TRANS,
    .move_gesture_move = true,
    .up_release = UP_RELEASE_ENGAGED,
    .up_clear_hovered = true,
    .up_reset_tracked = true,
    .skip_reentry_down = false,
};

// ─── Static helpers (moved from touch_processor_activation.c) ─────────────────

static inline void toggle_slide_over(TouchElement* btn, TouchActionResult* restrict result, uint64_t time_ms) {
    if (btn->selected) {
        if (element_has_gesture_toggle(btn)) {
            btn->visual_active = true;
            TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=1 (blocked_other_toggle)", (int)(btn - g_state.elements));
            mark_element_dirty(btn);
            return;
        }
        release_toggled_alternate_bindings(btn, result, true);
        for (int k = 0; k < 4; k++) {
            TouchBinding* tb = &btn->bindings[k];
            if (tb->type == BINDING_NONE) continue;
            release_binding(result, tb);
            if (tb->toggle)
                gesture_remove_toggled_action(tb->type, tb->keycode);
            else if (tb->auto_repeat)
                gesture_remove_held_action(tb->type, tb->keycode);
        }
        if (g_state.gesture_held_count == 0)
            g_state.gesture_is_action_held = false;
        btn->selected = false;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=0 (deselect)", (int)(btn - g_state.elements));
        btn->visual_active = false;
    } else {
        for (int k = 0; k < 4; k++) {
            TouchBinding* tb = &btn->bindings[k];
            if (tb->type == BINDING_NONE) continue;
            if (tb->toggle || tb->auto_repeat)
                press_binding(result, tb, true);
            else
                press_binding(result, tb, false);
        }
        btn->selected = true;
        btn->auto_repeat_last_time = time_ms;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=1 (select)", (int)(btn - g_state.elements));
        btn->visual_active = true;
    }
    mark_element_dirty(btn);
}

static inline void process_engaged_non_buttons(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result, bool hover_release) {
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(f == NULL, 0)) return;
    uint8_t cnt = f->engaged_elem_count;
    uint8_t i = 0;
    while (i < cnt) {
        TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
        if (e->type != ELEM_BUTTON && e->current_ptr_id == ptr_id) {
            if (hover_release && !point_in_element(x, y, e)) {
                handle_element_up(e, x, y, time_ms, result);
                finger_remove_engaged(f, f->engaged_elem_indices[i]);
                cnt = f->engaged_elem_count;
                continue;
            }
            handle_element_move(e, x, y, time_ms, result);
        }
        i++;
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

// ─── DOWN: unified hit-test + element engagement ────────────────────────────

static bool grid_engage_all(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    bool handled = false;
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
    return handled;
}

static bool scan_first_button(int ptr_id, float x, float y, uint64_t time_ms,
                              TouchActionResult* result, bool skip_off_toggle, bool set_hovered)
{
    bool handled = false;
    TouchElement* btn = NULL;
    bool used_grid = (g_state.grid_cell_w > 0.0f && g_state.grid_cell_h > 0.0f);

    if (used_grid) {
        if (x >= g_state.grid_min_x && x <= g_state.grid_max_x &&
            y >= g_state.grid_min_y && y <= g_state.grid_max_y) {
            int col = (int)((x - g_state.grid_min_x) * g_state.grid_inv_cell_w);
            int row = (int)((y - g_state.grid_min_y) * g_state.grid_inv_cell_h);
            int min_c = col > 0 ? col - 1 : 0;
            int max_c = col < GRID_COLS - 1 ? col + 1 : GRID_COLS - 1;
            int min_r = row > 0 ? row - 1 : 0;
            int max_r = row < GRID_ROWS - 1 ? row + 1 : GRID_ROWS - 1;
            int hit_count = 0;
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
            // Linear fallback when grid misses
            if (hit_count == 0) {
                for (int i = 0; i < g_state.element_count; i++) {
                    TouchElement* e = &g_state.elements[i];
                    if (!point_in_element(x, y, e)) continue;
                    if (e->type != ELEM_BUTTON) {
                        handle_element_down(e, ptr_id, x, y, time_ms, result);
                        handled = true;
                    } else if (!btn) {
                        btn = e;
                    }
                }
            }
        }
    } else {
        for (int i = 0; i < g_state.element_count; i++) {
            TouchElement* e = &g_state.elements[i];
            if (!point_in_element(x, y, e)) continue;
            if (e->type != ELEM_BUTTON) {
                handle_element_down(e, ptr_id, x, y, time_ms, result);
                handled = true;
            } else if (!btn) {
                btn = e;
            }
        }
    }

    // Process the found button — track and engage
    if (btn && btn->type == ELEM_BUTTON) {
        int btn_idx = (int)(btn - g_state.elements);
        bool skip = skip_off_toggle && btn->cached_has_toggle
                    && !btn->selected && !btn->lp_toggled && !btn->gesture_toggled;

        if (!skip) {
            TrackedButtons* tb = get_tracked_buttons(ptr_id);
            if (!is_tracked(tb, btn_idx) && tb->count < MAX_TRACKED_PER_POINTER) {
                tb->element_indices[tb->count++] = btn_idx;
                handle_element_down(btn, ptr_id, x, y, time_ms, result);
                if (btn->cached_has_toggle && btn->cached_has_auto_repeat)
                    btn->gesture_timer_armed = true;
            }
            if (set_hovered)
                g_state.hovered_element_per_ptr[(uint32_t)ptr_id % MAX_FINGERS] = btn_idx;
        }
        handled = true;
    }

    return handled;
}

bool activation_mode_down(int ptr_id, float x, float y, uint64_t time_ms,
                          TouchActionResult* result, const ActivationModeParams* params)
{
    if (__builtin_expect(g_state.element_count == 0, 0)) return false;

    if (params->down_button == BTN_DOWN_ALL)
        return grid_engage_all(ptr_id, x, y, time_ms, result);

    return scan_first_button(ptr_id, x, y, time_ms, result,
                             params->down_skip_off_toggle, params->down_set_hovered);
}

// ─── MOVE (non-buttons): process engaged + optionally activate new ───────────

void activation_mode_move_nonbuttons(int ptr_id, float x, float y, uint64_t time_ms,
                                     TouchActionResult* result, const ActivationModeParams* params)
{
    if (params->move_nonbutton == NONBTN_MOVE_LOCK) {
        // LOCK: move ALL engaged elements (including buttons) — matches original
        TouchFinger* f = find_finger(ptr_id);
        if (__builtin_expect(f != NULL, 1)) {
            uint8_t cnt = f->engaged_elem_count;
            for (uint8_t i = 0; i < cnt; i++) {
                TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
                if (e->current_ptr_id == ptr_id)
                    handle_element_move(e, x, y, time_ms, result);
            }
        }
    } else {
        bool hover_release = (params->move_nonbutton == NONBTN_MOVE_HOVER);
        process_engaged_non_buttons(ptr_id, x, y, time_ms, result, hover_release);

        TouchElement* hit = hit_test_element(x, y);
        if (hit && hit->type != ELEM_BUTTON && hit->current_ptr_id < 0)
            handle_element_down(hit, ptr_id, x, y, time_ms, result);
    }
}

// ─── MOVE (buttons): tracking / accumulation / transition ───────────────────

// Returns: current hovered element index, or -1 if none.
// The caller can use this for gesture state save/restore around the call.
int activation_mode_move_buttons(int ptr_id, float x, float y, uint64_t time_ms,
                                 TouchActionResult* result, const ActivationModeParams* params)
{
    TrackedButtons* tb = get_tracked_buttons(ptr_id);
    if (tb->count == 0) return -1;

    TouchElement* btn = hit_test_element(x, y);
    int curr_idx = btn && btn->type == ELEM_BUTTON ? (int)(btn - g_state.elements) : -1;

    switch (params->move_button) {
    case BTN_MOVE_LOCK:
        break;

    case BTN_MOVE_ACCUM: {
        if (curr_idx < 0) break;
        // Original handler TRACK had no toggle activation on move (empty block).
        // Original activation.c TRACK had toggle_slide_over but it was buggy
        // (deselected finger-down toggles on first move). Match handler behavior.
        if (!btn->cached_has_toggle) {
            if (!is_tracked(tb, curr_idx) && tb->count < MAX_TRACKED_PER_POINTER) {
                tb->element_indices[tb->count++] = curr_idx;
                handle_element_down(btn, ptr_id, x, y, time_ms, result);
                if (tb->count > 1)
                    suppress_element_gestures(btn, result);
                reset_first_tracked_long_press(tb);
            }
        }
        break;
    }

    case BTN_MOVE_TRANS: {
        // Matches original activation.c HOVER move exactly.
        int pid_slot = (uint32_t)ptr_id % MAX_FINGERS;
        int prev_idx = g_state.hovered_element_per_ptr[pid_slot];

        if (curr_idx >= 0 && curr_idx == prev_idx)
            return curr_idx;

        // Deactivate previous element — release all bindings (element + gesture)
        // and clear gesture flags to prevent stale bindings after finger slides away.
        // Always runs when curr_idx != prev_idx, even when curr_idx < 0 (empty space).
        if (prev_idx >= 0 && prev_idx < g_state.element_count) {
            TouchElement* prev = &g_state.elements[prev_idx];
            release_element_bindings(prev, result, false);
            release_non_toggle_gestures(prev, result);
            clear_element_gesture_flags(prev, false);
            prev->current_ptr_id = -1;
            prev->engaged = false;
            TouchFinger* f = find_finger(ptr_id);
            if (f) finger_remove_engaged(f, prev_idx);
        }

        // Activate current element or clear hovered on empty space
        if (curr_idx >= 0) {
            if (btn->cached_has_toggle && !btn->gesture_timer_armed) {
                toggle_slide_over(btn, result, time_ms);
                btn->gesture_timer_armed = true;
            } else {
                // Non-toggle OR toggle with timer already armed
                bool already_tracked = is_tracked(tb, curr_idx);
                if (!already_tracked && tb->count < MAX_TRACKED_PER_POINTER) {
                    tb->element_indices[tb->count++] = curr_idx;
                    reset_first_tracked_long_press(tb);
                }
                // Skip handle_element_down on re-entry of first tracked button
                // when the caller (handler re-entry block) will handle it.
                bool reentry_first = (already_tracked && tb->count > 0
                    && curr_idx == tb->element_indices[0]);
                if (!(params->skip_reentry_down && reentry_first))
                    handle_element_down(btn, ptr_id, x, y, time_ms, result);
                if (tb->count > 1 || already_tracked)
                    suppress_element_gestures(btn, result);
            }
        }
        g_state.hovered_element_per_ptr[pid_slot] = curr_idx >= 0 ? curr_idx : -1;
        break;
    }
    }

    return curr_idx;
}

// ─── UP: release elements per mode ──────────────────────────────────────────

bool activation_mode_up(int ptr_id, float x, float y, uint64_t time_ms,
                        TouchActionResult* result, const ActivationModeParams* params)
{
    if (__builtin_expect(g_state.element_count == 0, 0)) return false;

    int pid_slot = (uint32_t)ptr_id % MAX_FINGERS;
    bool handled = false;

    if (params->up_release == UP_RELEASE_TRACKED) {
        TrackedButtons* tb = &g_state.tracked[pid_slot];
        if (tb->ptr_id == ptr_id) {
            for (int j = 0; j < tb->count; j++) {
                int idx = tb->element_indices[j];
                if (idx >= 0 && idx < g_state.element_count) {
                    handle_element_up(&g_state.elements[idx], x, y, time_ms, result);
                    if (!element_is_toggle_active(&g_state.elements[idx]))
                        g_state.elements[idx].visual_active = false;
                }
            }
            reset_tracked_slot(tb, pid_slot);
            handled = true;
        }
    } else {
        if (params->up_reset_tracked)
            reset_tracked_slot(&g_state.tracked[pid_slot], pid_slot);
        handled = release_engaged_elements(ptr_id, x, y, time_ms, result);
    }

    reset_unused_toggle_gesture_timers();

    if (params->up_clear_hovered)
        g_state.hovered_element_per_ptr[pid_slot] = -1;

    return handled;
}
