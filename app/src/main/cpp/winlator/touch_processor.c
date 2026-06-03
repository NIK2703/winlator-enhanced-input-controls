#include "touch_processor_internal.h"

TouchProcessorState g_state;

void touch_processor_init(const TouchProcessorConfig* config) {
    memset(&g_state, 0, sizeof(g_state));
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    g_state.main_ptr_id = -1;
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_second_ptr_id = -1;
    g_state.second_double_tap_ptr_id = -1;
    g_state.finger_pointer_left = -1;
    g_state.finger_pointer_right = -1;
    g_state.long_tap_mode = (config->second_finger_mode == SECOND_FINGER_LONG_TAP);
    g_state.pointer_left_enabled = true;
    g_state.pointer_right_enabled = true;
    g_state.pending_left_release_ptr_id = -1;
    g_state.pending_right_release_ptr_id = -1;
    g_state.sim_click_ptr_id = -1;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.hovered_element_per_ptr[i] = -1;
    if (g_state.cfg.cursor_acceleration_threshold <= 0) g_state.cfg.cursor_acceleration_threshold = 6;
    if (g_state.cfg.cursor_acceleration_factor <= 0.0f) g_state.cfg.cursor_acceleration_factor = 1.25f;
    if (g_state.cfg.xform_scale_x <= 0.0f) g_state.cfg.xform_scale_x = 1.0f;
    if (g_state.cfg.xform_scale_y <= 0.0f) g_state.cfg.xform_scale_y = 1.0f;
    
}

void touch_processor_update_config(const TouchProcessorConfig* config) {
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    g_state.long_tap_mode = (config->second_finger_mode == SECOND_FINGER_LONG_TAP);
}

void touch_processor_set_elements(const TouchElement* elements, int count) {
    int n = count < MAX_ELEMENTS ? count : MAX_ELEMENTS;
    memcpy(g_state.elements, elements, n * sizeof(TouchElement));
    g_state.element_count = n;
}

void touch_processor_set_snapping_size(float size) {
    g_state.snapping_size = size;
    
}

void touch_processor_set_resolution_scale(float scale) { g_state.resolution_scale = scale; }
void touch_processor_set_sim_touch_screen(bool enabled) { g_state.sim_touch_screen = enabled; }
void touch_processor_set_xform_scale(float scale_x, float scale_y) {
    g_state.cfg.xform_scale_x = scale_x > 0.0f ? scale_x : 1.0f;
    g_state.cfg.xform_scale_y = scale_y > 0.0f ? scale_y : 1.0f;
}

TouchActionResult touch_processor_on_finger_down(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result = {0};
    // Java does NOT release held actions on every finger-down — only in specific
    // paths (second-finger TouchscreenGestureHandler path or TouchpadGestureHandler
    // onFingerDown). Releasing here would cancel a long-press hold
    // (e.g. mouse-right held) when a second finger touches for scrolling.
    // release_held_actions(&result);

    TouchFinger* f = find_finger(ptr_id);
    if (!f) {
        f = find_free_finger();
        if (!f) return result;
        memset(f, 0, sizeof(TouchFinger));
        f->ptr_id = ptr_id;
        f->active = true;
    }
    f->x = x; f->y = y;
    f->down_x = x; f->down_y = y;
    f->last_x = x; f->last_y = y;
    f->down_time_ms = time_ms;
    f->travel_x = 0; f->travel_y = 0;
    f->is_tap = true;

    // Copy all 12 FingerBindings lists from config
    FingerBindings* fb = &f->bindings;
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN)
        COPY_FINGER_BINDINGS(fb, &g_state.cfg, ts)
    else
        COPY_FINGER_BINDINGS(fb, &g_state.cfg, tp)
    touch_finger_cache_bs(f);

    switch (g_state.cfg.touch_mode) {
        case TOUCH_MODE_TOUCHPAD: handle_touchpad_down(f, x, y, time_ms, &result); break;
        case TOUCH_MODE_TOUCHSCREEN: handle_touchscreen_down(f, x, y, time_ms, &result); break;
        default: break;
    }
    
    return result;
}

TouchActionResult touch_processor_on_finger_move(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result = {0};
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return result;
    f->travel_x += fabsf(x - f->x);
    f->travel_y += fabsf(y - f->y);
    if (f->travel_x > MAX_TAP_TRAVEL || f->travel_y > MAX_TAP_TRAVEL) f->is_tap = false;
    f->x = x; f->y = y;
    switch (g_state.cfg.touch_mode) {
        case TOUCH_MODE_TOUCHPAD: handle_touchpad_move(f, x, y, time_ms, &result); break;
        case TOUCH_MODE_TOUCHSCREEN: handle_touchscreen_move(f, x, y, time_ms, &result); break;
        default: break;
    }
    return result;
}

TouchActionResult touch_processor_on_finger_up(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result = {0};
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return result;
    f->x = x; f->y = y;
    switch (g_state.cfg.touch_mode) {
        case TOUCH_MODE_TOUCHPAD: handle_touchpad_up(f, x, y, time_ms, &result); break;
        case TOUCH_MODE_TOUCHSCREEN: handle_touchscreen_up(f, x, y, time_ms, &result); break;
        default: break;
    }
    f->active = false;
    return result;
}

void touch_processor_on_finger_cancel(int ptr_id) {
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return;
    // Emit release for any held actions (matching Java GestureHandler.reset -> releaseHeldAction)
    TouchActionResult cancel_result = {0};
    release_held_actions(&cancel_result);
    f->active = false;
    // Clear all per-finger gesture state
    f->state = GESTURE_STATE_IDLE;
    f->held_actions_count = 0;
    f->deferred_tap_count = 0;
    f->pending_double_count = 0;
    f->drag_binding_count = 0;
    f->dragging = false;
    f->double_tap_waiting = false;
    f->second_double_tap_waiting = false;
    f->pending_resume_action_count = 0;
    f->pending_second_double_count = 0;
    f->second_tap_fallback_count = 0;
    f->single_tap_hold_delay_ms = 0;
}

TouchActionResult touch_processor_tick(uint64_t time_ms) {
    TouchActionResult result = {0};

    // Gesture-level timeouts (long-press, double-tap, single-tap-hold, second-finger double-tap)
    gesture_tick(time_ms, &result);

    // Single merged element loop: long-press, range hold, range deferred release, double-tap timeout
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];

        // Element long-press timeout (requires finger down, ELEM_BUTTON only)
        if (e->type == ELEM_BUTTON && e->current_ptr_id >= 0
            && e->long_press_arm && !e->gesture_long_press_triggered
            && e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE)
        {
            if (time_ms - e->down_time_ms >= g_state.cfg.long_press_delay_ms) {
                e->gesture_long_press_triggered = true;
                e->long_press_arm = false;
                e->double_tap_waiting = false;
                for (int k = 0; k < e->element_long_press_count; k++)
                    press_binding(&result, &e->element_long_press[k], true);
                if (e->button_long_press_haptic > 0)
                    add_action(&result, ACT_HAPTIC, e->button_long_press_haptic, 0, 0);
            }
        }

        // Element double-tap timeout (works even after finger lift)
        if (e->type == ELEM_BUTTON && e->double_tap_waiting) {
            int dt_ms = g_state.cfg.button_double_tap_timeout_ms > 0
                ? g_state.cfg.button_double_tap_timeout_ms
                : g_state.cfg.double_tap_timeout_ms;
            if (time_ms - e->gesture_last_tap_time >= dt_ms) {
                e->double_tap_waiting = false;
                if (e->bindings[0].type != BINDING_NONE) {
                    press_binding(&result, &e->bindings[0], false);
                    release_binding(&result, &e->bindings[0]);
                }
            }
        }

        // Range button (both hold timer and deferred release in one pass)
        if (e->type != ELEM_RANGE_BUTTON) continue;
        int kc = range_keycode(e->range_ordinal, e->range_index);

        // Range button hold timer
        if (e->current_ptr_id >= 0 && !e->range_hold_pressed && !e->range_scrolling && e->range_has_binding) {
            if (time_ms - e->down_time_ms >= RANGE_TAP_TIMEOUT_MS) {
                if (kc > 0) {
                    add_action(&result, ACT_KEY_PRESS, kc, 0, 0);
                    e->range_hold_pressed = true;
                }
            }
        }

        // Range button deferred tap release
        if (e->range_pending_tap_release && time_ms >= e->range_tap_release_time) {
            if (kc > 0)
                add_action(&result, ACT_KEY_RELEASE, kc, 0, 0);
            e->range_pending_tap_release = false;
            e->range_scrolling = false;
            e->range_has_binding = false;
            e->range_hold_pressed = false;
        }
    }

    // Process delayed pointer button releases (matching Java TouchpadView 30ms postDelayed)
    if (g_state.pending_left_release_time > 0 && time_ms >= g_state.pending_left_release_time) {
        add_action(&result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        g_state.pending_left_release_time = 0;
        g_state.pending_left_release_ptr_id = -1;
        g_state.pointer_left_enabled = true;
    }
    if (g_state.pending_right_release_time > 0 && time_ms >= g_state.pending_right_release_time) {
        add_action(&result, ACT_POINTER_BUTTON_RELEASE, 1, 0, 0);
        g_state.pending_right_release_time = 0;
        g_state.pending_right_release_ptr_id = -1;
        g_state.pointer_right_enabled = true;
    }

    // Process simTouchScreen delayed press (matching Java clickDelay Runnable, 50ms CLICK_DELAYED_TIME)
    if (g_state.sim_click_press_time > 0 && time_ms >= g_state.sim_click_press_time) {
        if (g_state.sim_continue_click) {
            TouchFinger* sf = find_finger(g_state.sim_click_ptr_id);
            if (sf) {
                add_action(&result, ACT_POINTER_MOVE, (int)g_state.last_touch_x, (int)g_state.last_touch_y, 0);
                add_action(&result, ACT_POINTER_BUTTON_PRESS, 0, 0, 0);
            }
        }
        g_state.sim_click_press_time = 0;
    }

    // Process simTouchScreen delayed release (50ms after finger up)
    if (g_state.sim_click_release_time > 0 && time_ms >= g_state.sim_click_release_time) {
        if (g_state.sim_continue_click) {
            add_action(&result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        }
        g_state.sim_click_release_time = 0;
        g_state.sim_continue_click = false;
    }

    return result;
}

void touch_processor_reset(void) {
    memset(g_state.fingers, 0, sizeof(g_state.fingers));
    g_state.finger_count = 0;
    g_state.main_ptr_id = -1;
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_second_ptr_id = -1;
    g_state.gesture_handler_active = false;
    g_state.gesture_second_active = false;
    g_state.gesture_is_action_held = false;
    g_state.gesture_held_count = 0;
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_pending_deferred_double_count = 0;
    g_state.gesture_post_double_tap_drag = false;
    g_state.gesture_double_tap_consumed = false;
    g_state.gesture_double_tap_waiting = false;
    g_state.gesture_double_tap_start_time = 0;
    g_state.gesture_deferred_second_finger_tap = false;
    g_state.second_double_tap_ptr_id = -1;
    g_state.scroll_accum_y = 0;
    g_state.passthrough_active = false;
    g_state.finger_pointer_left = -1;
    g_state.finger_pointer_right = -1;
    memset(g_state.tracked, 0, sizeof(g_state.tracked));
    for (int i = 0; i < MAX_FINGERS; i++) g_state.hovered_element_per_ptr[i] = -1;

    for (int i = 0; i < g_state.element_count; i++) {
        g_state.elements[i].current_ptr_id = -1;
        g_state.elements[i].engaged = false;
        g_state.elements[i].gesture_long_press_triggered = false;
        g_state.elements[i].gesture_double_tap_triggered = false;
        g_state.elements[i].gesture_swipe_triggered = false;
        g_state.elements[i].double_tap_waiting = false;
        g_state.elements[i].long_press_arm = false;
        g_state.elements[i].range_hold_pressed = false;
        g_state.elements[i].range_pending_tap_release = false;
        g_state.elements[i].range_tap_release_time = 0;
    }

    g_state.last_mouse_move_action = 0;

    g_state.pending_left_release_time = 0;
    g_state.pending_left_release_ptr_id = -1;
    g_state.pending_right_release_time = 0;
    g_state.pending_right_release_ptr_id = -1;
    g_state.sim_click_press_time = 0;
    g_state.sim_continue_click = false;
    g_state.sim_click_ptr_id = -1;
    g_state.sim_click_release_time = 0;
}

bool touch_processor_is_passthrough_active(void) { return g_state.passthrough_active; }
void touch_processor_get_pointer_pos(int* x, int* y) { *x = (int)g_state.ptr_x; *y = (int)g_state.ptr_y; }

bool touch_processor_get_element_state(int elemIndex, float* out_stick_x, float* out_stick_y, bool* out_engaged, int* out_ptr_id) {
    if (elemIndex < 0 || elemIndex >= g_state.element_count) return false;
    TouchElement* e = &g_state.elements[elemIndex];
    if (out_stick_x) *out_stick_x = e->stick_value_x;
    if (out_stick_y) *out_stick_y = e->stick_value_y;
    if (out_engaged) *out_engaged = e->engaged;
    if (out_ptr_id) *out_ptr_id = e->current_ptr_id;
    return true;
}

void touch_processor_destroy(void) {
    memset(&g_state, 0, sizeof(g_state));
}
