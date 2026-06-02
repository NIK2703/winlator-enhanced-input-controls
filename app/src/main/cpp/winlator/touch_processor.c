#include "touch_processor_internal.h"

TouchProcessorState g_state;

void touch_processor_init(const TouchProcessorConfig* config) {
    memset(&g_state, 0, sizeof(g_state));
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    g_state.main_ptr_id = -1;
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_second_ptr_id = -1;
    g_state.finger_pointer_left = -1;
    g_state.finger_pointer_right = -1;
    g_state.long_tap_mode = (config->second_finger_mode == SECOND_FINGER_LONG_TAP);
    g_state.pointer_left_enabled = true;
    g_state.pointer_right_enabled = true;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.hovered_element_per_ptr[i] = -1;
    if (g_state.cfg.cursor_acceleration_threshold <= 0) g_state.cfg.cursor_acceleration_threshold = 6;
    if (g_state.cfg.cursor_acceleration_factor <= 0.0f) g_state.cfg.cursor_acceleration_factor = 1.25f;
    LOGD("init: touch_mode=%d input_mode=%d screen=%dx%d", config->touch_mode, config->input_mode, config->screen_w, config->screen_h);
}

void touch_processor_update_config(const TouchProcessorConfig* config) {
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    g_state.long_tap_mode = (config->second_finger_mode == SECOND_FINGER_LONG_TAP);
}

void touch_processor_set_elements(const TouchElement* elements, int count) {
    int n = count < MAX_ELEMENTS ? count : MAX_ELEMENTS;
    memcpy(g_state.elements, elements, n * sizeof(TouchElement));
    g_state.element_count = n;
    LOGD("set_elements: count=%d n=%d snapping_size=%.1f", count, n, g_state.snapping_size);
    for (int i = 0; i < n && i < 5; i++) {
        LOGD("  elem[%d]: type=%d shape=%d x=%d y=%d w=%.1f h=%.1f scale=%.2f bind[0].type=%d",
             i, elements[i].type, elements[i].shape, elements[i].x, elements[i].y,
             elements[i].w, elements[i].h, elements[i].scale, elements[i].bindings[0].type);
    }
}

void touch_processor_set_snapping_size(float size) {
    g_state.snapping_size = size;
    LOGD("set_snapping_size: %.1f", size);
}

void touch_processor_set_resolution_scale(float scale) { g_state.resolution_scale = scale; }
void touch_processor_set_sim_touch_screen(bool enabled) { g_state.sim_touch_screen = enabled; }

TouchActionResult touch_processor_on_finger_down(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result = {0};
    LOGD("on_finger_down: ptr=%d (%.0f,%.0f) t=%llu elements=%d hs=%.1f",
         ptr_id, x, y, (unsigned long long)time_ms, g_state.element_count, g_state.snapping_size);

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

    // Copy all 12 FingerBindings lists from config (6 first-finger + 6 second-finger)
    FingerBindings* fb = &f->bindings;
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        memcpy(fb->single_tap, g_state.cfg.ts_single_tap, sizeof(g_state.cfg.ts_single_tap)); fb->single_tap_count = g_state.cfg.ts_single_tap_count;
        memcpy(fb->long_press, g_state.cfg.ts_long_press, sizeof(g_state.cfg.ts_long_press)); fb->long_press_count = g_state.cfg.ts_long_press_count;
        memcpy(fb->double_tap, g_state.cfg.ts_double_tap, sizeof(g_state.cfg.ts_double_tap)); fb->double_tap_count = g_state.cfg.ts_double_tap_count;
        memcpy(fb->single_tap_drag, g_state.cfg.ts_single_tap_drag, sizeof(g_state.cfg.ts_single_tap_drag)); fb->single_tap_drag_count = g_state.cfg.ts_single_tap_drag_count;
        memcpy(fb->long_press_drag, g_state.cfg.ts_long_press_drag, sizeof(g_state.cfg.ts_long_press_drag)); fb->long_press_drag_count = g_state.cfg.ts_long_press_drag_count;
        memcpy(fb->double_tap_drag, g_state.cfg.ts_double_tap_drag, sizeof(g_state.cfg.ts_double_tap_drag)); fb->double_tap_drag_count = g_state.cfg.ts_double_tap_drag_count;
        memcpy(fb->single_tap_2nd, g_state.cfg.ts_single_2nd, sizeof(g_state.cfg.ts_single_2nd)); fb->single_tap_2nd_count = g_state.cfg.ts_single_2nd_count;
        memcpy(fb->long_press_2nd, g_state.cfg.ts_long_2nd, sizeof(g_state.cfg.ts_long_2nd)); fb->long_press_2nd_count = g_state.cfg.ts_long_2nd_count;
        memcpy(fb->double_tap_2nd, g_state.cfg.ts_double_2nd, sizeof(g_state.cfg.ts_double_2nd)); fb->double_tap_2nd_count = g_state.cfg.ts_double_2nd_count;
        memcpy(fb->single_tap_drag_2nd, g_state.cfg.ts_single_drag_2nd, sizeof(g_state.cfg.ts_single_drag_2nd)); fb->single_tap_drag_2nd_count = g_state.cfg.ts_single_drag_2nd_count;
        memcpy(fb->long_press_drag_2nd, g_state.cfg.ts_long_drag_2nd, sizeof(g_state.cfg.ts_long_drag_2nd)); fb->long_press_drag_2nd_count = g_state.cfg.ts_long_drag_2nd_count;
        memcpy(fb->double_tap_drag_2nd, g_state.cfg.ts_double_drag_2nd, sizeof(g_state.cfg.ts_double_drag_2nd)); fb->double_tap_drag_2nd_count = g_state.cfg.ts_double_drag_2nd_count;
    } else {
        memcpy(fb->single_tap, g_state.cfg.tp_single_tap, sizeof(g_state.cfg.tp_single_tap)); fb->single_tap_count = g_state.cfg.tp_single_tap_count;
        memcpy(fb->long_press, g_state.cfg.tp_long_press, sizeof(g_state.cfg.tp_long_press)); fb->long_press_count = g_state.cfg.tp_long_press_count;
        memcpy(fb->double_tap, g_state.cfg.tp_double_tap, sizeof(g_state.cfg.tp_double_tap)); fb->double_tap_count = g_state.cfg.tp_double_tap_count;
        memcpy(fb->single_tap_drag, g_state.cfg.tp_single_tap_drag, sizeof(g_state.cfg.tp_single_tap_drag)); fb->single_tap_drag_count = g_state.cfg.tp_single_tap_drag_count;
        memcpy(fb->long_press_drag, g_state.cfg.tp_long_press_drag, sizeof(g_state.cfg.tp_long_press_drag)); fb->long_press_drag_count = g_state.cfg.tp_long_press_drag_count;
        memcpy(fb->double_tap_drag, g_state.cfg.tp_double_tap_drag, sizeof(g_state.cfg.tp_double_tap_drag)); fb->double_tap_drag_count = g_state.cfg.tp_double_tap_drag_count;
        memcpy(fb->single_tap_2nd, g_state.cfg.tp_single_2nd, sizeof(g_state.cfg.tp_single_2nd)); fb->single_tap_2nd_count = g_state.cfg.tp_single_2nd_count;
        memcpy(fb->long_press_2nd, g_state.cfg.tp_long_2nd, sizeof(g_state.cfg.tp_long_2nd)); fb->long_press_2nd_count = g_state.cfg.tp_long_2nd_count;
        memcpy(fb->double_tap_2nd, g_state.cfg.tp_double_2nd, sizeof(g_state.cfg.tp_double_2nd)); fb->double_tap_2nd_count = g_state.cfg.tp_double_2nd_count;
        memcpy(fb->single_tap_drag_2nd, g_state.cfg.tp_single_drag_2nd, sizeof(g_state.cfg.tp_single_drag_2nd)); fb->single_tap_drag_2nd_count = g_state.cfg.tp_single_drag_2nd_count;
        memcpy(fb->long_press_drag_2nd, g_state.cfg.tp_long_drag_2nd, sizeof(g_state.cfg.tp_long_drag_2nd)); fb->long_press_drag_2nd_count = g_state.cfg.tp_long_drag_2nd_count;
        memcpy(fb->double_tap_drag_2nd, g_state.cfg.tp_double_drag_2nd, sizeof(g_state.cfg.tp_double_drag_2nd)); fb->double_tap_drag_2nd_count = g_state.cfg.tp_double_drag_2nd_count;
    }

    // Check passthrough
    g_state.passthrough_active = false;
    TouchElement* elem = hit_test_element(x, y);
    if (elem && elem->passthrough_touch) g_state.passthrough_active = true;

    switch (g_state.cfg.touch_mode) {
        case TOUCH_MODE_TOUCHPAD: handle_touchpad_down(f, x, y, &result); break;
        case TOUCH_MODE_TOUCHSCREEN: handle_touchscreen_down(f, x, y, time_ms, &result); break;
        default: break;
    }
    LOGD("on_finger_down: result.count=%d", result.count);
    for (int _i = 0; _i < result.count && _i < 12; _i++) {
        int _t = result.actions[_i].type;
        int _a0 = 0, _a1 = 0;
        if (_t == ACT_KEY_PRESS || _t == ACT_KEY_RELEASE) {
            _a0 = result.actions[_i].key.keycode;
            _a1 = result.actions[_i].key.is_down;
        } else if (_t == ACT_POINTER_MOVE) {
            _a0 = result.actions[_i].pointer_move.x;
            _a1 = result.actions[_i].pointer_move.y;
        } else if (_t == ACT_POINTER_BUTTON_PRESS || _t == ACT_POINTER_BUTTON_RELEASE) {
            _a0 = result.actions[_i].pointer_button.button;
        } else if (_t == ACT_GAMEPAD_STATE) {
            _a0 = result.actions[_i].key.keycode;
            _a1 = result.actions[_i].key.is_down;
        } else if (_t == ACT_SCROLL) {
            _a0 = result.actions[_i].scroll.amount;
        } else if (_t == ACT_POINTER_MOVE_DELTA) {
            _a0 = result.actions[_i].pointer_delta.dx;
            _a1 = result.actions[_i].pointer_delta.dy;
        }
        LOGD("  action[%d]: type=%d a0=%d a1=%d", _i, _t, _a0, _a1);
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
        case TOUCH_MODE_TOUCHSCREEN: handle_touchscreen_up(f, x, y, &result); break;
        default: break;
    }
    f->active = false;
    return result;
}

void touch_processor_on_finger_cancel(int ptr_id) {
    TouchFinger* f = find_finger(ptr_id);
    if (f) f->active = false;
}

TouchActionResult touch_processor_tick(uint64_t time_ms) {
    TouchActionResult result = {0};

    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active || f->state != GESTURE_STATE_TAP_WAITING) continue;
        if (!has_long_press_timer(&f->bindings)) continue;

        if (time_ms - f->down_time_ms >= (uint64_t)g_state.cfg.long_press_timeout_ms) {
            g_state.gesture_handler_active = false;
            f->second_tap_fallback_count = 0;
            f->pending_second_double_count = 0;
            if (can_hold_long_press(&f->bindings))
                hold_actions(&result, f->bindings.long_press, f->bindings.long_press_count);
            f->state = GESTURE_STATE_LONG_PRESSING;
            if (has_long_press_timer(&f->bindings) && g_state.cfg.gesture_long_press_haptic > 0)
                add_action(&result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
        }
    }

    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active || f->state != GESTURE_STATE_TAP_WAITING) continue;
        if (f->single_tap_hold_delay_ms <= 0) continue;
        if (time_ms - f->single_tap_hold_timer >= (uint64_t)f->single_tap_hold_delay_ms) {
            f->single_tap_hold_delay_ms = 0;
            if (!g_state.gesture_is_action_held) {
                hold_actions(&result, f->bindings.single_tap, f->bindings.single_tap_count);
            }
        }
    }

    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->type != ELEM_BUTTON || e->current_ptr_id < 0) continue;
        if (!e->long_press_arm || e->gesture_long_press_triggered) continue;
        bool has_lp = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
        if (!has_lp) continue;

        if (time_ms - e->down_time_ms >= (uint64_t)g_state.cfg.long_press_timeout_ms) {
            e->gesture_long_press_triggered = true;
            e->long_press_arm = false;
            for (int k = 0; k < e->element_long_press_count; k++)
                press_binding(&result, &e->element_long_press[k], true);
        }
    }

    if (g_state.gesture_double_tap_waiting &&
        time_ms - g_state.gesture_double_tap_start_time >= (uint64_t)g_state.cfg.double_tap_timeout_ms) {
        g_state.gesture_double_tap_waiting = false;
        if (g_state.gesture_deferred_tap_count > 0) {
            execute_actions(&result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
            g_state.gesture_deferred_tap_count = 0;
        }
        g_state.gesture_pending_deferred_double_count = 0;
    }

    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active || !f->second_double_tap_waiting) continue;
        if (time_ms - f->second_tap_fallback_time >= (uint64_t)g_state.cfg.double_tap_timeout_ms) {
            f->second_double_tap_waiting = false;
            if (f->second_tap_fallback_count > 0) {
                execute_actions(&result, f->second_tap_fallback, f->second_tap_fallback_count);
                f->second_tap_fallback_count = 0;
            }
            if (f->state != GESTURE_STATE_DRAGGING) f->state = GESTURE_STATE_IDLE;
        }
    }

    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->type != ELEM_BUTTON || e->current_ptr_id < 0) continue;
        if (!e->double_tap_waiting) continue;
        if (time_ms - e->gesture_last_tap_time >= (uint64_t)g_state.cfg.double_tap_timeout_ms) {
            e->double_tap_waiting = false;
            e->gesture_double_tap_first_up = false;
            // Java: on double-tap timeout, fire primary as single tap (press + release)
            if (e->bindings[0].type != BINDING_NONE) {
                press_binding(&result, &e->bindings[0], false);
                release_binding(&result, &e->bindings[0]);
            }
        }
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
        g_state.elements[i].gesture_swipe_triggered = false;
        g_state.elements[i].double_tap_waiting = false;
        g_state.elements[i].gesture_double_tap_first_up = false;
        g_state.elements[i].long_press_arm = false;
    }

    g_state.last_mouse_move_action = 0;
}

bool touch_processor_is_passthrough_active(void) { return g_state.passthrough_active; }
void touch_processor_get_pointer_pos(int* x, int* y) { *x = (int)g_state.ptr_x; *y = (int)g_state.ptr_y; }

void touch_processor_destroy(void) {
    memset(&g_state, 0, sizeof(g_state));
}
