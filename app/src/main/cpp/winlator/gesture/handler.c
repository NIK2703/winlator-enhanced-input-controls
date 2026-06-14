#include "../touch_processor_internal.h"
#include "../activation_mode.h"
#include "branch.h"

#define DELAYED_RELEASE_MS 30
#define GRID_NEIGHBOR_OFFSET 1
#define CURSOR_SENSITIVITY_DIVISOR 100.0f

static inline int pointer_index(const TouchFinger* f) {
    return (int)((uint32_t)f->ptr_id % MAX_FINGERS);
}

static void gesture_repress_saved_bindings(TouchElement* element, bool had_gest_swipe, bool had_gest_lp, TouchActionResult* restrict result);

typedef struct {
    bool has_gesture;
    bool lp_arm;
    bool gest_swipe;
    bool gest_lp;
    bool gest_timer;
} HoverRestoreState;

typedef struct {
    bool gesture_swipe_triggered;
    bool gesture_long_press_triggered;
    bool gesture_toggled;
    bool lp_toggled;
    bool long_press_arm;
    bool gesture_timer_armed;
} ElementGestureState;

static ElementGestureState save_element_gesture_state(const TouchElement* element) {
    return (ElementGestureState){
        .gesture_swipe_triggered = element->gesture_swipe_triggered,
        .gesture_long_press_triggered = element->gesture_long_press_triggered,
        .gesture_toggled = element->gesture_toggled,
        .lp_toggled = element->lp_toggled,
        .long_press_arm = element->long_press_arm,
        .gesture_timer_armed = element->gesture_timer_armed,
    };
}

static void restore_element_gesture_state(TouchElement* element, const ElementGestureState* state) {
    element->gesture_swipe_triggered = state->gesture_swipe_triggered;
    element->gesture_long_press_triggered = state->gesture_long_press_triggered;
    element->long_press_arm = state->long_press_arm;
    element->gesture_timer_armed = state->gesture_timer_armed;
    element->gesture_toggled = state->gesture_toggled;
    element->lp_toggled = state->lp_toggled;
    if (element->type == ELEM_BUTTON) update_visual_layers(element);
}

static inline int element_index(const TouchElement* element) {
    return (int)(element - g_state.elements);
}

static void gesture_restore_first_button_state(TouchFinger* finger, int pointer_index, int saved_hovered_idx,
    const HoverRestoreState* state,
    TrackedButtons* tracked, TouchActionResult* restrict result)
{
    if (!state->has_gesture || saved_hovered_idx < 0
        || saved_hovered_idx >= g_state.element_count) return;
    int current_hovered = g_state.hovered_element_per_ptr[pointer_index];
    if (current_hovered == saved_hovered_idx) return;
    TouchElement* prev = &g_state.elements[saved_hovered_idx];
    if (prev != &g_state.elements[tracked->element_indices[0]]) return;
    prev->long_press_arm = state->lp_arm;
    prev->gesture_swipe_triggered = state->gest_swipe;
    prev->gesture_long_press_triggered = state->gest_lp;
    prev->gesture_timer_armed = state->gest_timer;
    if (state->gest_swipe || state->gest_lp) {
        gesture_repress_saved_bindings(prev, state->gest_swipe, state->gest_lp, result);
        prev->current_ptr_id = finger->ptr_id;
        prev->engaged = true;
    }
    if (prev->type == ELEM_BUTTON) update_visual_layers(prev);
}

__attribute__((hot))
static bool check_confirm_dt_waiting(TouchFinger* restrict finger, TouchActionResult* restrict result,
    TouchFinger* restrict main_finger, bool use_main_bindings)
{
    if (!g_state.gesture_double_tap_waiting) return false;
    if (!gesture_is_within_tap_distance(finger->x, finger->y)) {
        gesture_cancel_double_tap_wait(result);
        return false;
    }
    TouchFinger* dt_target = use_main_bindings && main_finger ? main_finger : finger;
    double_tap_confirm_internal(result, dt_target);
    TouchFinger* reset_target = use_main_bindings ? main_finger : finger;
    if (reset_target) {
        reset_target->state = GESTURE_STATE_TAP_WAITING;
        reset_finger_tap_state(reset_target);
        if (use_main_bindings) {
            reset_target->down_x = reset_target->x;
            reset_target->down_y = reset_target->y;
        }
    }
    return true;
}

static void save_pending_resume_action(TouchFinger* main_finger) {
    if (!main_finger || !main_finger->active) return;
    main_finger->pending_resume_action_count = 0;
    if (g_state.gesture_is_action_held) {
        int n = g_state.gesture_held_count < MAX_DEFERRED_BINDINGS ? g_state.gesture_held_count : MAX_DEFERRED_BINDINGS;
        memcpy(main_finger->pending_resume_action, g_state.gesture_held_actions, n * sizeof(TouchBinding));
        main_finger->pending_resume_action_count = n;
    }
    scroll_mode_save(main_finger);
}

static void cleanup_second_finger_up(TouchActionResult* restrict result) {
    TouchFinger* main = find_finger(g_state.gesture_main_ptr_id);
    if (main && main->pending_resume_action_count > 0) {
        execute_actions_hold(result, main->pending_resume_action, main->pending_resume_action_count);
        main->pending_resume_action_count = 0;
        main->state = GESTURE_STATE_DRAGGING;
    }
    if (main) scroll_mode_restore(main);
    g_state.gesture_second_ptr_id = INVALID_PTR_ID;
}

static void schedule_pending_release(uint64_t* time_field, int* ptr_field, int* id_field, int ptr_id, uint64_t time_ms) {
    if (*id_field == ptr_id) {
        *time_field = time_ms + DELAYED_RELEASE_MS;
        *ptr_field = ptr_id;
        *id_field = -1;
    }
}

static TouchFinger* find_other_dt_finger(TouchFinger* finger) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* other = &g_state.fingers[i];
        if (other->active && other->double_tap_original_id_set && other->ptr_id != finger->ptr_id)
            return other;
    }
    return NULL;
}

static void setup_second_finger_state(void) {
    TouchFinger* second_finger = find_finger(g_state.gesture_second_ptr_id);
    if (second_finger) {
        second_finger->is_second_finger = true;
        setup_second_finger_bindings(second_finger);
    }
    g_state.gesture_second_active = true;
}

static void handle_ts_main_finger_down(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result, bool was_second_deferred) {
    if (check_confirm_dt_waiting(finger, result, NULL, false)) {
        g_state.gesture_main_ptr_id = finger->ptr_id;

        if (finger->double_tap_original_id_set) {
            TouchFinger* other = find_other_dt_finger(finger);
            if (other) {
                g_state.gesture_second_active = false;
                g_state.gesture_main_ptr_id = other->ptr_id;
                update_ts_pointer(x, y, result);
                finger->state = GESTURE_STATE_TAP_WAITING;
                return;
            }
        }

        if (was_second_deferred && g_state.gesture_second_ptr_id >= 0)
            setup_second_finger_state();
        return;
    }

    if (!g_state.gesture_double_tap_waiting && was_second_deferred && g_state.gesture_second_ptr_id >= 0)
        setup_second_finger_state();

    finger->original_ptr_id = finger->ptr_id;
    finger->double_tap_original_id_set = true;
    update_ts_pointer(x, y, result);

    gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_DOWN, finger->x, finger->y);
}

static void handle_ts_second_finger_down(TouchFinger* finger, uint64_t time_ms, TouchActionResult* restrict result, TouchFinger* main_finger) {
    if (g_state.gesture_second_active) {
        finger->state = GESTURE_STATE_IDLE;
        return;
    }
    if (!(g_state.cfg.caps_ts_mask & GESTURE_SECOND_MASK)
        && !g_state.cfg.caps_has_scroll_bindings
        && !g_state.gesture_double_tap_waiting) {
        g_state.gesture_second_active = true;
        g_state.gesture_second_ptr_id = finger->ptr_id;
        finger->is_second_finger = true;
        finger->state = GESTURE_STATE_IDLE;
        return;
    }

    if (check_confirm_dt_waiting(finger, result, main_finger, true)) {
        g_state.gesture_second_active = false;
        return;
    }

    save_pending_resume_action(main_finger);
    release_held_actions(result);
    setup_second_finger_bindings(finger);
    gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_DOWN, finger->x, finger->y);
}

static void handle_sim_touch_down(TouchFinger* finger, float x, float y, uint64_t time_ms) {
    int finger_count = active_finger_count();
    if (finger_count == 1) {
        g_state.sim_continue_click = true;
        g_state.sim_click_press_time = time_ms + CLICK_DELAY_MS;
        g_state.sim_click_ptr_id = finger->ptr_id;
        g_state.last_touch_x = (int)x;
        g_state.last_touch_y = (int)y;
    } else if (finger_count == 2) {
        TouchFinger* first = NULL;
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (g_state.fingers[i].active && g_state.fingers[i].ptr_id != finger->ptr_id) {
                first = &g_state.fingers[i]; break;
            }
        }
        if (first && (time_ms - first->down_time_ms) >= CLICK_DELAY_MS)
            g_state.sim_continue_click = true;
        else {
            g_state.sim_continue_click = false;
            g_state.sim_click_press_time = 0;
        }
    }
}

static void handle_tp_finger_down(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result, TouchFinger* main_finger, bool is_first, bool is_second) {
    if (is_first) {
        g_state.scrolling = false;
        g_state.scroll_accum_y = 0;
    } else if (is_second) {
        save_pending_resume_action(main_finger);
        release_held_actions(result);
    }

    if (g_state.sim_touch_screen) {
        handle_sim_touch_down(finger, x, y, time_ms);
    }

    if (is_second)
        setup_second_finger_bindings(finger);
    gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_DOWN, finger->x, finger->y);
}

static void process_element_hit(TouchElement* element, TouchFinger* finger,
    float x, float y, uint64_t time_ms, TouchActionResult* restrict result,
    bool has_passthrough, bool* found_lock, bool* handled, TouchElement** btn)
{
    if (has_passthrough && element->passthrough_touch)
        g_state.passthrough_active = true;

    ActivationMode activation_mode = element->activation_mode;
    int eidx = (int)(element - g_state.elements);
    if (activation_mode == ACTIVATION_LOCK) {
        if (!*found_lock) {
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HIT_LOCK elem=%d type=%d", eidx, element->type);
            handle_element_down(element, finger->ptr_id, x, y, time_ms, result);
            *found_lock = true;
        }
    } else if (element->type != ELEM_BUTTON) {
        if (!*handled) {
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HIT_NONBTN elem=%d type=%d bind0=%d engaged=%d", eidx, element->type, element->bindings[0].type, element->engaged);
            handle_element_down(element, finger->ptr_id, x, y, time_ms, result);
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HIT_NONBTN_AFTER elem=%d engaged=%d current_ptr=%d", eidx, element->engaged, element->current_ptr_id);
            if (element->engaged && !element->passthrough_touch) *handled = true;
        }
    } else if (*btn == NULL) {
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "HIT_BTN elem=%d type=%d act=%d bind0=%d", eidx, element->type, activation_mode, element->bindings[0].type);
        *btn = element;
    }
}

static void hit_test_elements_down(TouchFinger* finger, float x, float y, uint64_t time_ms,
    TouchActionResult* restrict result, bool has_passthrough,
    bool* found_lock, bool* handled, TouchElement** btn)
{
    if (g_state.grid_cell_w > 0.0f && g_state.grid_cell_h > 0.0f) {
        int cx = (int)((x - g_state.grid_min_x) * g_state.grid_inv_cell_w);
        int cy = (int)((y - g_state.grid_min_y) * g_state.grid_inv_cell_h);
        if (cx >= 0 && cx < GRID_COLS && cy >= 0 && cy < GRID_ROWS) {
            int start_cx = cx - GRID_NEIGHBOR_OFFSET < 0 ? 0 : cx - GRID_NEIGHBOR_OFFSET;
            int end_cx = cx + GRID_NEIGHBOR_OFFSET >= GRID_COLS ? GRID_COLS - 1 : cx + GRID_NEIGHBOR_OFFSET;
            int start_cy = cy - GRID_NEIGHBOR_OFFSET < 0 ? 0 : cy - GRID_NEIGHBOR_OFFSET;
            int end_cy = cy + GRID_NEIGHBOR_OFFSET >= GRID_ROWS ? GRID_ROWS - 1 : cy + GRID_NEIGHBOR_OFFSET;
            for (int r = start_cy; r <= end_cy; r++) {
                for (int c = start_cx; c <= end_cx; c++) {
                    int cell_idx = r * GRID_COLS + c;
                    for (int j = g_state.grid_cell_start[cell_idx]; j < g_state.grid_cell_start[cell_idx + 1]; j++) {
                        TouchElement* entry = &g_state.elements[g_state.grid_cell_to_elems[j]];
                        if (!point_in_element(x, y, entry)) continue;

                        process_element_hit(entry, finger, x, y, time_ms, result,
                            has_passthrough, found_lock, handled, btn);
                    }
                }
            }
        }
    }

    if (!*found_lock && !*handled) {
        for (int i = 0; i < g_state.element_count; i++) {
            TouchElement* entry = &g_state.elements[i];
            if (!point_in_element(x, y, entry)) continue;

            process_element_hit(entry, finger, x, y, time_ms, result,
                has_passthrough, found_lock, handled, btn);
        }
    }
}

// ============================================================
// handle_gesture_down — element hit-test & delegation
// ============================================================
static bool handle_gesture_down_element(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    const bool has_mouse_left = g_state.cfg.caps_has_mouse_left_element;
    const bool has_passthrough = g_state.cfg.caps_has_passthrough_elements;
    bool found_lock = false;
    bool handled = false;
    TouchElement* btn = NULL;

    if (has_mouse_left && g_state.pointer_left_enabled) {
        for (int i = 0; i < g_state.element_count; i++) {
            if (g_state.elements[i].bindings[0].type == BINDING_MOUSE_LEFT) {
                g_state.pointer_left_enabled = false;
                break;
            }
        }
    }

    hit_test_elements_down(finger, x, y, time_ms, result, has_passthrough, &found_lock, &handled, &btn);
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "GDOWN_ELEMENT found_lock=%d handled=%d btn=%p", found_lock, handled, (void*)btn);
    if (found_lock) return true;

    // TRACK/HOVER BUTTON
    if (btn == NULL) btn = hit_test_element(x, y);
    if (btn && btn->type == ELEM_BUTTON) {
        if (btn->activation_mode == ACTIVATION_TRACK || btn->activation_mode == ACTIVATION_HOVER) {
            TrackedButtons* tracked = get_tracked_buttons(finger->ptr_id);
            bool already = is_tracked(tracked, element_index(btn));
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "GDOWN_TRACK_HOVER elem=%d already=%d", element_index(btn), already);
            if (!already) {
                int16_t prev_ptr = btn->current_ptr_id;
                handle_element_down(btn, finger->ptr_id, x, y, time_ms, result);
                if (btn->cached_has_toggle && btn->cached_has_auto_repeat && btn->activation_mode == ACTIVATION_TRACK) {
                    btn->gesture_timer_armed = true;
                } else if (prev_ptr != finger->ptr_id && btn->current_ptr_id == finger->ptr_id && tracked->count < MAX_TRACKED_PER_POINTER) {
                    tracked->element_indices[tracked->count++] = element_index(btn);
                }
            }
            if (btn->activation_mode == ACTIVATION_HOVER)
                g_state.hovered_element_per_ptr[pointer_index(finger)] = element_index(btn);
            if (!btn->passthrough_touch) handled = true;
        } else {
            handle_element_down(btn, finger->ptr_id, x, y, time_ms, result);
            if (!btn->passthrough_touch) handled = true;
        }
    }

    return handled;
}

// ============================================================
// handle_gesture_down — passthrough logic
// ============================================================
static void handle_gesture_down_passthrough(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)x; (void)y; (void)time_ms;
    if (!g_state.passthrough_active) return;
    finger->state = GESTURE_STATE_IDLE;
    if (g_state.gesture_main_ptr_id < 0) {
        g_state.gesture_main_ptr_id = finger->ptr_id;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = false;
        g_state.gesture_second_active = false;
    }
}

// ============================================================
// handle_gesture_down
// ============================================================
__attribute__((hot))
void handle_gesture_down(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    finger->gesture_activated_in_touch = false;
    g_state.main_ptr_id = finger->ptr_id;

    if (handle_gesture_down_element(finger, x, y, time_ms, result))
        return;

    handle_gesture_down_passthrough(finger, x, y, time_ms, result);
    if (g_state.passthrough_active) return;

    TouchFinger* main_finger = NULL;
    if (g_state.gesture_main_ptr_id >= 0) {
        main_finger = find_finger(g_state.gesture_main_ptr_id);
        if (!main_finger || !main_finger->active) {
            g_state.gesture_main_ptr_id = INVALID_PTR_ID;
            g_state.gesture_second_active = false;
        }
    }
    bool is_first = (g_state.gesture_main_ptr_id < 0);
    bool is_second = !is_first && (finger->ptr_id != g_state.gesture_main_ptr_id);

    if (is_first) {
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = false;
        g_state.gesture_second_active = false;
        g_state.gesture_pending_double_count = 0;
    }

    if (g_state.cfg.is_ts && is_first) {
        bool was_second_deferred = g_state.gesture_deferred_second_finger_tap;
        g_state.gesture_deferred_second_finger_tap = false;
        handle_ts_main_finger_down(finger, x, y, time_ms, result, was_second_deferred);
        return;
    }

    if (g_state.cfg.is_ts && is_second) {
        handle_ts_second_finger_down(finger, time_ms, result, main_finger);
        return;
    }

    handle_tp_finger_down(finger, x, y, time_ms, result, main_finger, is_first, is_second);
}

static void release_gesture_alternate_bindings(TouchElement* element,
    const ElementGestureState* saved, TouchActionResult* restrict result)
{
    if (saved->gesture_swipe_triggered && !saved->gesture_toggled)
        release_bindings_list(result, element->element_gesture, element->element_gesture_count);
    if (saved->gesture_long_press_triggered && !saved->lp_toggled)
        release_bindings_list(result, element->element_long_press, element->element_long_press_count);
}

static void press_gesture_alternate_bindings(TouchElement* element,
    const ElementGestureState* saved, TouchActionResult* restrict result)
{
    if (saved->gesture_toggled)
        press_bindings_list(result, element->element_gesture, element->element_gesture_count);
    if (saved->lp_toggled)
        press_bindings_list(result, element->element_long_press, element->element_long_press_count);
    if (saved->gesture_swipe_triggered && !saved->gesture_toggled)
        press_bindings_list(result, element->element_gesture, element->element_gesture_count);
    if (saved->gesture_long_press_triggered && !saved->lp_toggled)
        press_bindings_list(result, element->element_long_press, element->element_long_press_count);
}

// ============================================================
// handle_toggle_entry — unified toggle entry logic for slide-over
// ============================================================
static void gesture_repress_saved_bindings(
    TouchElement* element,
    bool had_gest_swipe, bool had_gest_lp,
    TouchActionResult* restrict result)
{
    if (had_gest_swipe && !element->gesture_swipe_triggered && !element->gesture_toggled)
        press_bindings_list(result, element->element_gesture, element->element_gesture_count);
    if (had_gest_lp && !element->gesture_long_press_triggered && !element->lp_toggled)
        press_bindings_list(result, element->element_long_press, element->element_long_press_count);
}

// ============================================================
static void handle_toggle_entry(TouchElement* element, TouchActionResult* restrict result) {
    if (element_is_toggle_active(element)) {
        // Don't deselect via slide-over if a gesture/long-press toggle is still active
        if (element_has_gesture_toggle(element)) {
            if (element->type == ELEM_BUTTON) update_visual_layers(element);
            mark_element_dirty(element);
            return;
        }
        // Release stale gesture/long-press toggles that lingered after finger-up
        release_toggled_alternate_bindings(element, result, true);
        // Release all bindings across all slots
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++) {
            if (element->bindings[k].type == BINDING_NONE) continue;
            release_binding(result, &element->bindings[k]);
        }
        element->selected = false;
        if (element->type == ELEM_BUTTON) update_visual_layers(element);
        mark_element_dirty(element);
        return;
    }
    if (element_has_primary(element))
        press_binding(result, &element->bindings[0], true);
    element->selected = true;
    if (element->type == ELEM_BUTTON) update_visual_layers(element);
    element->gesture_timer_armed = true;
    mark_element_dirty(element);
}

static bool save_hover_state(TouchFinger* finger, HoverRestoreState* out_state, int* out_saved_hovered_idx) {
    out_state->has_gesture = false;
    out_state->lp_arm = false;
    out_state->gest_swipe = false;
    out_state->gest_lp = false;
    out_state->gest_timer = false;
    *out_saved_hovered_idx = -1;
    if (!g_state.cfg.caps_has_track_hover_buttons) return false;
    *out_saved_hovered_idx = g_state.hovered_element_per_ptr[pointer_index(finger)];
    TrackedButtons* tracked = get_tracked_buttons(finger->ptr_id);
    if (tracked->count > 0) {
        int first_idx = tracked->element_indices[0];
        if (first_idx >= 0 && first_idx < g_state.element_count) {
            TouchElement* first_btn = &g_state.elements[first_idx];
            if (first_btn->type == ELEM_BUTTON && first_btn->activation_mode == ACTIVATION_HOVER) {
                out_state->lp_arm = first_btn->long_press_arm;
                out_state->gest_swipe = first_btn->gesture_swipe_triggered;
                out_state->gest_lp = first_btn->gesture_long_press_triggered;
                out_state->gest_timer = first_btn->gesture_timer_armed;
                out_state->has_gesture = true;
            }
        }
    }
    return out_state->has_gesture;
}

static void handle_toggle_slide_over(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result, TouchElement** hit_element, bool* hit_element_valid) {
    if (!*hit_element_valid) { *hit_element = hit_test_element(x, y); *hit_element_valid = true; }
    TouchElement* toggle_btn = *hit_element;
    if (toggle_btn && toggle_btn->type == ELEM_BUTTON && toggle_btn->cached_has_toggle) {
        ActivationMode mode = g_state.activation_mode;
        if (mode == ACTIVATION_TRACK || mode == ACTIVATION_HOVER) {
            TrackedButtons* tracked = &g_state.tracked[pointer_index(finger)];
            bool already_tracked = is_tracked(tracked, element_index(toggle_btn));
            if (!already_tracked && !toggle_btn->gesture_timer_armed) {
                if (toggle_btn->cached_has_auto_repeat) {
                    if (mode == ACTIVATION_TRACK) {
                        if (toggle_btn->current_ptr_id >= 0) {
                            int16_t eidx = (int16_t)element_index(toggle_btn);
                            finger_remove_engaged(finger, eidx);
                            toggle_btn->current_ptr_id = INVALID_PTR_ID;
                            toggle_btn->engaged = false;
                        }
                        handle_element_down(toggle_btn, finger->ptr_id, x, y, time_ms, result);
                        toggle_btn->gesture_timer_armed = true;
                        mark_element_dirty(toggle_btn);
                    }
                } else if (mode != ACTIVATION_HOVER) {
                    handle_toggle_entry(toggle_btn, result);
                }
            }
        }
    } else {
        for (int i = 0; i < g_state.element_count; i++)
            if (g_state.elements[i].cached_has_toggle && g_state.elements[i].current_ptr_id == finger->ptr_id)
                g_state.elements[i].gesture_timer_armed = false;
    }
}

static void handle_track_hover_move(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result, HoverRestoreState* hover_state, int saved_hovered_idx, bool* had_element_move) {
    int ptr_idx = pointer_index(finger);
    TrackedButtons* tracked = get_tracked_buttons(finger->ptr_id);
    ActivationMode mode = g_state.activation_mode;

    if (mode == ACTIVATION_HOVER) {
        int prev_idx = g_state.hovered_element_per_ptr[ptr_idx];
        ActivationModeParams hover_params = ACTIVATION_MODE_HOVER;
        hover_params.skip_reentry_down = true;
        int curr_idx = activation_mode_move_buttons(finger->ptr_id, x, y, time_ms, result, &hover_params);

        bool was_transition = (curr_idx != prev_idx);

        if (was_transition && curr_idx >= 0 && tracked->count > 0
            && curr_idx == tracked->element_indices[0])
        {
            TouchElement* curr = &g_state.elements[curr_idx];
            bool already = is_tracked(tracked, curr_idx);
            if (already && !curr->gesture_timer_armed) {
                if (!curr->cached_has_toggle || element_is_toggle_active(curr)) {
                    ElementGestureState saved = save_element_gesture_state(curr);
                    release_gesture_alternate_bindings(curr, &saved, result);
                    curr->current_ptr_id = INVALID_PTR_ID;
                    curr->engaged = false;
                    handle_element_down(curr, finger->ptr_id, x, y, time_ms, result);
                    press_gesture_alternate_bindings(curr, &saved, result);
                    restore_element_gesture_state(curr, &saved);
                } else {
                    suppress_element_gestures(curr, result);
                }
            } else if (tracked->count > 1 || prev_idx < 0) {
                suppress_element_gestures(curr, result);
                if (element_is_toggle_active(curr) && curr->cached_has_toggle)
                    curr->gesture_timer_armed = true;
            }
        }

        if (was_transition && prev_idx >= 0 && prev_idx < g_state.element_count) {
            hover_state->gest_swipe = false;
            hover_state->gest_lp = false;
            hover_state->lp_arm = false;
            hover_state->gest_timer = false;
        }

        gesture_restore_first_button_state(finger, ptr_idx, saved_hovered_idx,
            hover_state, tracked, result);

        if (curr_idx >= 0 && tracked->count > 0 && curr_idx == tracked->element_indices[0]) {
            handle_element_move(&g_state.elements[curr_idx], x, y, time_ms, result);
            *had_element_move = true;
        }
    } else if (tracked->count > 0) {
        activation_mode_move_buttons(finger->ptr_id, x, y, time_ms, result, &ACTIVATION_MODE_TRACK);

        gesture_restore_first_button_state(finger, ptr_idx, saved_hovered_idx,
            hover_state, tracked, result);
    }
}

static void handle_cursor_move(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    int active_count = active_finger_count();
    if (!g_state.cfg.is_tp || g_state.scrolling || active_count > 2) return;
    if (finger->scroll_mode) return; // cursor frozen in scroll mode
    if (g_state.gesture_second_active && finger->ptr_id != g_state.gesture_main_ptr_id) return;
    if (g_state.sim_touch_screen) {
        if (finger->travel_x > MAX_TAP_TRAVEL || finger->travel_y > MAX_TAP_TRAVEL)
            g_state.sim_continue_click = false;
        uint64_t elapsed = time_ms - finger->down_time_ms;
        if (elapsed > CLICK_DELAY_MS)
            add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
    } else {
        float dx = x - finger->last_x;
        float dy = y - finger->last_y;
        float sx = g_state.cfg.xform_scale_x;
        float sy = g_state.cfg.xform_scale_y;
        float sens = g_state.cfg.cursor_speed / CURSOR_SENSITIVITY_DIVISOR;
        float accel_factor = g_state.cfg.cursor_acceleration_factor;
        float accel_threshold = (float)g_state.cfg.cursor_acceleration_threshold;
        dx *= sx;
        dy *= sy;
        dx *= sens;
        dy *= sens;
        if (accel_factor > 0.0f && accel_threshold > 0.0f) {
            float adx = fabsf(dx);
            if (adx > accel_threshold)
                dx = adx * accel_factor * (dx > 0 ? 1.0f : -1.0f);
            float ady = fabsf(dy);
            if (ady > accel_threshold)
                dy = ady * accel_factor * (dy > 0 ? 1.0f : -1.0f);
        }
        dx += finger->sub_pixel_x;
        dy += finger->sub_pixel_y;
        int id = lrintf(dx);
        int jd = lrintf(dy);
        finger->sub_pixel_x = dx - id;
        finger->sub_pixel_y = dy - jd;
        if (id != 0 || jd != 0) {
            if (g_state.cfg.input_mode == INPUT_RELATIVE)
                add_action(result, ACT_MOUSE_EVENT, 0, id, jd);
            else
                add_action(result, ACT_POINTER_MOVE_DELTA, id, jd, 0);
        }
    }
}

static void handle_main_finger_propagate_to_second(float x, float y, uint64_t time_ms,
    TouchActionResult* restrict result, bool is_ts, bool is_tp)
{
    if (!g_state.gesture_second_active) return;
    TouchFinger* second_finger = find_finger(g_state.gesture_second_ptr_id);
    if (!second_finger) return;
    if (is_ts) {
        float second_dx = x - g_state.gesture_second_main_ref_x;
        float second_dy = y - g_state.gesture_second_main_ref_y;
        gesture_branch(second_finger, &g_ctx[(int)(second_finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, second_dx, second_dy);
    } else if (is_tp && second_finger->state >= GESTURE_STATE_TAP_WAITING
        && second_finger->state != GESTURE_STATE_DRAGGING) {
        gesture_branch(second_finger, &g_ctx[(int)(second_finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, 0.0f, 0.0f);
    }
}

static void handle_second_finger_move(TouchFinger* finger, float x, float y, uint64_t time_ms,
    TouchActionResult* restrict result, bool is_ts, bool is_tp)
{
    if (!g_state.gesture_second_active || finger->ptr_id != g_state.gesture_second_ptr_id)
        return;
    if (finger->state < GESTURE_STATE_TAP_WAITING)
        return;

    float dx = x - finger->down_x;
    float dy = y - finger->down_y;

    if (is_ts) {
        uint32_t ts_second = GESTURE_SECOND_MASK;
        if (g_state.cfg.caps_ts_mask & ts_second)
            gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, dx, dy);
    } else if (is_tp && finger->state != GESTURE_STATE_DRAGGING) {
        uint32_t tp_second = GESTURE_SECOND_MASK;
        if (g_state.cfg.caps_tp_mask & tp_second)
            gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, dx, dy);
    }
}

static void handle_gesture_move_processing(TouchFinger* finger, float x, float y, uint64_t time_ms,
    TouchActionResult* restrict result, bool is_ts, bool is_tp)
{
    if (finger->ptr_id == g_state.gesture_main_ptr_id) {
        if (is_ts
            && !g_state.gesture_second_active
            && !finger->cached_has_active_single_tap_drag
            && !finger->cached_has_active_long_press_drag
            && finger->cached_has_active_single_tap
            && !finger->cached_has_active_double_tap
            && !finger->cached_has_active_double_tap_drag
            && !finger->cached_has_long_press_timer
            && !g_state.gesture_post_double_tap_drag
            && finger->state != GESTURE_STATE_LONG_PRESSING
            && !finger->scroll_mode
            && !g_state.cfg.caps_has_scroll_bindings) {
            update_ts_pointer(x, y, result);
            finger->last_x = x;
            finger->last_y = y;
            return;
        }

        float dx = x - finger->down_x;
        float dy = y - finger->down_y;
        gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, dx, dy);

        handle_main_finger_propagate_to_second(x, y, time_ms, result, is_ts, is_tp);
    }

    handle_second_finger_move(finger, x, y, time_ms, result, is_ts, is_tp);

    if (is_ts) {
        if ((g_state.gesture_main_ptr_id < 0 || finger->ptr_id == g_state.gesture_main_ptr_id)
            && !finger->scroll_mode)
            update_ts_pointer(x, y, result);
        finger->last_x = x;
        finger->last_y = y;
        return;
    }
}

static bool handle_engaged_element_move(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    uint8_t cnt = finger->engaged_elem_count;
    bool skip_buttons = (g_state.activation_mode == ACTIVATION_TRACK || g_state.activation_mode == ACTIVATION_HOVER);
    bool had_element_move = false;
    for (uint8_t i = 0; i < cnt; i++) {
        TouchElement* element = &g_state.elements[finger->engaged_elem_indices[i]];
        if (element->current_ptr_id == finger->ptr_id && element->passthrough_touch)
            continue;
        if (element->current_ptr_id == finger->ptr_id) {
            if (skip_buttons && element->type == ELEM_BUTTON)
                continue;
            handle_element_move(element, x, y, time_ms, result);
            had_element_move = true;
        }
    }
    return had_element_move;
}

static bool check_passthrough_engagement(TouchFinger* finger) {
    if (g_state.cfg.caps_has_passthrough_elements) {
        uint8_t count = finger->engaged_elem_count;
        for (uint8_t i = 0; i < count; i++) {
            if (g_state.elements[finger->engaged_elem_indices[i]].passthrough_touch) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================
// handle_gesture_move — element interaction phase
// ============================================================
static bool handle_move_button_interactions(
    TouchFinger* finger, float x, float y, uint64_t time_ms,
    TouchActionResult* restrict result,
    bool has_track_hover, bool has_element_toggle,
    HoverRestoreState* hover_state, int saved_hovered_idx,
    TouchElement** hit_element, bool* hit_element_valid)
{
    bool had_element_move = handle_engaged_element_move(finger, x, y, time_ms, result);
    if (had_element_move) return true;

    bool is_gesture_zone_finger = finger->state > GESTURE_STATE_IDLE;
    bool has_passthrough_engagement = check_passthrough_engagement(finger);
    bool skip_element_activation = is_gesture_zone_finger || has_passthrough_engagement;

    if (has_element_toggle && !skip_element_activation)
        handle_toggle_slide_over(finger, x, y, time_ms, result, hit_element, hit_element_valid);

    if (has_track_hover && !skip_element_activation)
        handle_track_hover_move(finger, x, y, time_ms, result, hover_state, saved_hovered_idx, &had_element_move);

    if (had_element_move) return true;

    if (has_track_hover) {
        TrackedButtons* tracked = &g_state.tracked[pointer_index(finger)];
        if (tracked->count > 0) {
            int first_idx = tracked->element_indices[0];
            if (first_idx >= 0 && first_idx < g_state.element_count) {
                TouchElement* first = &g_state.elements[first_idx];
                if (first->type == ELEM_BUTTON && !first->passthrough_touch)
                    return true;
            }
        }
    }

    if (!*hit_element_valid) { *hit_element = hit_test_element(x, y); *hit_element_valid = true; }
    TouchElement* elem = *hit_element;
    if (elem && elem->current_ptr_id == finger->ptr_id && !elem->passthrough_touch) {
        handle_element_move(elem, x, y, time_ms, result);
        return true;
    }

    return false;
}

// ============================================================
// handle_gesture_move — gesture + cursor phase
// ============================================================
static void handle_move_gesture_and_cursor(
    TouchFinger* finger, float x, float y, uint64_t time_ms,
    TouchActionResult* restrict result, bool is_ts, bool is_tp)
{
    handle_gesture_move_processing(finger, x, y, time_ms, result, is_ts, is_tp);
    handle_cursor_move(finger, x, y, time_ms, result);
}

// ============================================================
// handle_gesture_move
// ============================================================
__attribute__((hot))
void handle_gesture_move(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {

    const bool is_ts = g_state.cfg.is_ts;
    const bool is_tp = g_state.cfg.is_tp;
    const bool has_track_hover = g_state.cfg.caps_has_track_hover_buttons;
    const bool has_element_toggle = g_state.cfg.caps_has_element_toggle;

    TouchElement* hit_element = NULL;
    bool hit_element_valid = false;

    if (fabsf(finger->x - finger->down_x) > g_state.cfg.drag_threshold_px || fabsf(finger->y - finger->down_y) > g_state.cfg.drag_threshold_px)
        finger->cached_has_moved_beyond_threshold = true;

    HoverRestoreState hover_state;
    int saved_hovered_idx = -1;
    if (has_track_hover)
        save_hover_state(finger, &hover_state, &saved_hovered_idx);

    if (handle_move_button_interactions(finger, x, y, time_ms, result,
            has_track_hover, has_element_toggle, &hover_state, saved_hovered_idx,
            &hit_element, &hit_element_valid))
    {
        finger->last_x = x;
        finger->last_y = y;
        return;
    }

    handle_move_gesture_and_cursor(finger, x, y, time_ms, result, is_ts, is_tp);

    finger->last_x = x; finger->last_y = y;
}

static void ts_up_main_finger(TouchFinger* finger, uint64_t time_ms, TouchActionResult* restrict result) {
    g_state.gesture_deferred_second_finger_tap = g_state.gesture_second_active;
    finger->pending_resume_action_count = 0;
    finger->double_tap_original_id_set = false;
    gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, finger->x, finger->y);
}

static void deactivate_finger_to_idle(TouchFinger* finger) {
    finger->state = GESTURE_STATE_IDLE;
    deactivate_finger(finger);
}

static void ts_up_second_double_tap_waiting(TouchFinger* finger, TouchActionResult* restrict result) {
    gesture_clear_second_finger_state();
    deactivate_finger_to_idle(finger);
}

static void ts_up_original_id_set(TouchFinger* finger, TouchActionResult* restrict result) {
    if (!g_state.gesture_second_active) {
        release_held_actions(result);
        finger->pending_resume_action_count = 0;
        finger->double_tap_original_id_set = false;
        g_state.gesture_main_ptr_id = INVALID_PTR_ID;
        g_state.gesture_second_active = false;
    } else {
        finger->double_tap_original_id_set = false;
    }
    if (g_state.gesture_post_double_tap_drag) {
        g_state.gesture_post_double_tap_drag = false;
        release_held_actions(result);
    }
    deactivate_finger_to_idle(finger);
}

static void ts_up_second_finger(TouchFinger* finger, uint64_t time_ms, TouchActionResult* restrict result) {
    gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, finger->x, finger->y);
    cleanup_second_finger_up(result);
}

static inline void ts_up_idle_double_tap(TouchFinger* finger, uint64_t time_ms, TouchActionResult* restrict result) {
    gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, finger->x, finger->y);
    deactivate_finger_to_idle(finger);
}

static void handle_ts_up(TouchFinger* finger, uint64_t time_ms, TouchActionResult* restrict result) {
    if (finger->ptr_id == g_state.gesture_main_ptr_id) {
        ts_up_main_finger(finger, time_ms, result);
    } else if (g_state.second_double_tap_waiting) {
        ts_up_second_double_tap_waiting(finger, result);
    } else if (finger->double_tap_original_id_set && g_state.gesture_main_ptr_id >= 0
               && finger->ptr_id == finger->original_ptr_id) {
        ts_up_original_id_set(finger, result);
    } else if (g_state.gesture_second_active && finger->ptr_id != g_state.gesture_main_ptr_id) {
        ts_up_second_finger(finger, time_ms, result);
    } else if (finger->state == GESTURE_STATE_IDLE
               && (finger->cached_has_active_double_tap || finger->cached_has_active_double_tap_drag)) {
        ts_up_idle_double_tap(finger, time_ms, result);
    } else {
        deactivate_finger(finger);
    }
}

static void handle_tp_up(TouchFinger* finger, uint64_t time_ms, TouchActionResult* restrict result) {
    if (finger->ptr_id == g_state.gesture_main_ptr_id) {
        finger->pending_resume_action_count = 0;
    }
    gesture_branch(finger, &g_ctx[(int)(finger - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, finger->x, finger->y);

    if (finger->is_second_finger) {
        cleanup_second_finger_up(result);
    }

    schedule_pending_release(&g_state.pending_left_release_time, &g_state.pending_left_release_ptr_id, &g_state.finger_pointer_left, finger->ptr_id, time_ms);
    schedule_pending_release(&g_state.pending_right_release_time, &g_state.pending_right_release_ptr_id, &g_state.finger_pointer_right, finger->ptr_id, time_ms);
}

// ============================================================
// handle_gesture_up
// ============================================================
__attribute__((hot))
void handle_gesture_up(TouchFinger* finger, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    int ptr_idx = pointer_index(finger);
    TrackedButtons* tracked = &g_state.tracked[ptr_idx];
    bool had_tracked = false;
    if (g_state.cfg.caps_has_track_hover_buttons && tracked->ptr_id == finger->ptr_id && tracked->count > 0) {
        had_tracked = activation_mode_up(finger->ptr_id, x, y, time_ms, result,
            g_state.activation_mode == ACTIVATION_TRACK
                ? &ACTIVATION_MODE_TRACK
                : &ACTIVATION_MODE_HOVER);
    }
    g_state.hovered_element_per_ptr[ptr_idx] = INVALID_PTR_ID;

    const int ptr_id = finger->ptr_id;
    bool had_element = false;
    TouchFinger* finger_up = find_finger(ptr_id);
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "GUP ptr=%d finger_up=%p engaged_count=%d", ptr_id, (void*)finger_up, finger_up ? finger_up->engaged_elem_count : -1);
    if (finger_up != NULL) {
        uint8_t count = finger_up->engaged_elem_count;
        for (uint8_t i = 0; i < count; i++) {
            TouchElement* element = &g_state.elements[finger_up->engaged_elem_indices[i]];
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "GUP engaged[%d] elem=%d current_ptr=%d match=%d", i, finger_up->engaged_elem_indices[i], element->current_ptr_id, element->current_ptr_id == ptr_id);
            if (element->current_ptr_id == ptr_id) {
                handle_element_up(element, x, y, time_ms, result);
                had_element = true;
            }
        }
        finger_up->engaged_elem_count = 0;
    }
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "GUP had_tracked=%d had_element=%d result_count=%d", had_tracked, had_element, result->count);
    if (had_tracked || had_element) {
        if (g_state.gesture_main_ptr_id == ptr_id) {
            g_state.gesture_main_ptr_id = INVALID_PTR_ID;
            gesture_clear_second_finger_globals();
        }
        g_state.main_ptr_id = INVALID_PTR_ID;
        deactivate_finger(finger);
        return;
    }

    if (!current_mode_has_gestures() && !g_state.gesture_double_tap_waiting
        && !g_state.gesture_post_double_tap_drag
        && !g_state.second_double_tap_waiting) {
        deactivate_finger(finger);
        g_state.main_ptr_id = INVALID_PTR_ID;
        return;
    }

    if (g_state.cfg.is_ts) {
        handle_ts_up(finger, time_ms, result);
    } else {
        handle_tp_up(finger, time_ms, result);
    }

    g_state.main_ptr_id = INVALID_PTR_ID;
}
