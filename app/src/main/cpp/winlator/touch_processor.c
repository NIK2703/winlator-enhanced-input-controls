#include <android/log.h>
#define LOG_TAG "Winlator_TP"
#include "touch_processor_internal.h"
#include "touch_processor_activation.h"

// spatial grid rebuild (defined in element/shared.c)
TouchProcessorState g_state;

void touch_processor_init(const TouchProcessorConfig* config) {
    memset(&g_state, 0, sizeof(g_state));
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    g_state.main_ptr_id = -1;
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_second_ptr_id = -1;
    g_state.finger_pointer_left = -1;
    g_state.finger_pointer_right = -1;
    g_state.pointer_left_enabled = true;
    g_state.pointer_right_enabled = true;
    g_state.pending_left_release_ptr_id = -1;
    g_state.pending_right_release_ptr_id = -1;
    g_state.sim_click_ptr_id = -1;
    g_state.free_finger_hint = 0;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.hovered_element_per_ptr[i] = -1;
    if (g_state.cfg.cursor_acceleration_threshold <= 0) g_state.cfg.cursor_acceleration_threshold = 6;
    if (g_state.cfg.cursor_acceleration_factor <= 0.0f) g_state.cfg.cursor_acceleration_factor = 1.25f;
    if (g_state.cfg.xform_scale_x <= 0.0f) g_state.cfg.xform_scale_x = 1.0f;
    if (g_state.cfg.xform_scale_y <= 0.0f) g_state.cfg.xform_scale_y = 1.0f;
    g_state.cfg.bindings_generation = 1;
    compute_gesture_caps(&g_state.cfg);
    extern void init_bezier_lut(void);
    init_bezier_lut();
}

void touch_processor_update_config(const TouchProcessorConfig* config) {
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    compute_gesture_caps(&g_state.cfg);
    g_state.cfg.bindings_generation++;

    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active) continue;
        FingerBindings* fb = &f->bindings;
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            COPY_FINGER_BINDINGS(fb, &g_state.cfg, ts)
        } else {
            COPY_FINGER_BINDINGS(fb, &g_state.cfg, tp)
            fb->single_tap_drag_count = 0;
            fb->single_tap_drag_2nd_count = 0;
        }
        f->bindings_generation = g_state.cfg.bindings_generation;
        touch_finger_cache_bs(f);
    }
}

void touch_processor_set_elements(const TouchElement* elements, int count) {
    int n = count < MAX_ELEMENTS ? count : MAX_ELEMENTS;
    memcpy(g_state.elements, elements, n * sizeof(TouchElement));
    g_state.element_count = n;
    g_state.cfg.caps_has_track_hover_buttons = false;
    g_state.cfg.caps_has_toggle_switch = false;
    for (int i = 0; i < n; i++) {
        TouchElement* e = &g_state.elements[i];
        e->visual_x = (float)e->x;
        e->visual_y = (float)e->y;
        e->visual_active = false;
        e->engaged = false;
        e->current_ptr_id = -1;

        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)
            g_state.cfg.caps_has_track_hover_buttons = true;
        if (e->toggle_switch)
            g_state.cfg.caps_has_toggle_switch = true;
        float hs_snap = g_state.snapping_size > 0.0f ? g_state.snapping_size : 1.0f;
        switch (e->type) {
            case ELEM_DPAD: e->hw = hs_snap * 7.0f * e->scale; e->hh = hs_snap * 7.0f * e->scale; break;
            case ELEM_STICK:
            case ELEM_TRACKPAD: e->hw = hs_snap * 6.0f * e->scale; e->hh = hs_snap * 6.0f * e->scale; break;
            case ELEM_BUTTON:
                if (e->shape == SHAPE_CIRCLE) { e->hw = hs_snap * 3.0f * e->scale; e->hh = hs_snap * 3.0f * e->scale; }
                else { e->hw = e->w * hs_snap * 0.5f * e->scale; e->hh = e->h * hs_snap * 0.5f * e->scale; }
                break;
            case ELEM_RANGE_BUTTON:
                e->hw = hs_snap * ((e->range_binding_count * 4) / 2) * e->scale;
                e->hh = hs_snap * 2.0f * e->scale;
                if (e->range_orientation == 1) { float _t = e->hw; e->hw = e->hh; e->hh = _t; }
                break;
            default: e->hw = e->w * hs_snap * 0.5f * e->scale; e->hh = e->h * hs_snap * 0.5f * e->scale; break;
        }
    }
    g_state.button_count = 0;
    g_state.range_count = 0;
    for (int i = 0; i < n; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->type == ELEM_BUTTON && g_state.button_count < MAX_ELEMENTS)
            g_state.button_indices[g_state.button_count++] = i;
        else if (e->type == ELEM_RANGE_BUTTON && g_state.range_count < MAX_ELEMENTS)
            g_state.range_indices[g_state.range_count++] = i;
    }
    g_state.visual_state_dirty = true;
    build_spatial_grid();
}

void touch_processor_set_snapping_size(float size) {
    g_state.snapping_size = size;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        float hs = size;
        float hw, hh;
        switch (e->type) {
            case ELEM_DPAD:
                hw = hs * 7.0f * e->scale; hh = hs * 7.0f * e->scale; break;
            case ELEM_STICK:
            case ELEM_TRACKPAD:
                hw = hs * 6.0f * e->scale; hh = hs * 6.0f * e->scale; break;
            case ELEM_BUTTON:
                if (e->shape == SHAPE_CIRCLE) { hw = hs * 3.0f * e->scale; hh = hs * 3.0f * e->scale; }
                else { hw = e->w * hs * 0.5f * e->scale; hh = e->h * hs * 0.5f * e->scale; }
                break;
            case ELEM_RANGE_BUTTON:
                hw = hs * ((e->range_binding_count * 4) / 2) * e->scale; hh = hs * 2.0f * e->scale;
                if (e->range_orientation == 1) { float _t = hw; hw = hh; hh = _t; }
                break;
            default:
                hw = e->w * hs * 0.5f * e->scale; hh = e->h * hs * 0.5f * e->scale; break;
        }
        e->hw = hw; e->hh = hh;
    }
}

void touch_processor_set_resolution_scale(float scale) { g_state.resolution_scale = scale; }
void touch_processor_set_sim_touch_screen(bool enabled) { g_state.sim_touch_screen = enabled; }
void touch_processor_set_xform_scale(float scale_x, float scale_y) {
    g_state.cfg.xform_scale_x = scale_x > 0.0f ? scale_x : 1.0f;
    g_state.cfg.xform_scale_y = scale_y > 0.0f ? scale_y : 1.0f;
}

TouchActionResult touch_processor_on_finger_down(int ptr_id, float x, float y, uint64_t time_ms) {
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "on_finger_down ptr=%d x=%.0f y=%.0f time=%llu count=%d",
        ptr_id, x, y, (unsigned long long)time_ms, g_state.element_count);
    TouchActionResult result; result.count = 0;
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "gesture_main_ptr_id=%d second_active=%d",
        g_state.gesture_main_ptr_id, g_state.gesture_second_active);
    // Java does NOT release held actions on every finger-down — only in specific
    // paths (second-finger TouchscreenGestureHandler path or TouchpadGestureHandler
    // onFingerDown). Releasing here would cancel a long-press hold
    // (e.g. mouse-right held) when a second finger touches for scrolling.
    // release_held_actions(&result);

    TouchFinger* f = find_finger(ptr_id);
    if (!f) {
        f = find_free_finger();
        if (!f) return result;
        f->ptr_id = ptr_id;
        f->active = true;
        f->bindings_generation = 0;  // force binding copy
        f->state = 0;
        f->is_tap = true;
        f->double_tap_original_id_set = false;
        f->is_second_finger = false;
        f->cached_has_active_single_tap = false;
        f->cached_has_active_double_tap = false;
        f->cached_has_active_long_press = false;
        f->cached_has_active_single_tap_drag = false;
        f->cached_has_active_long_press_drag = false;
        f->cached_has_active_double_tap_drag = false;
        f->cached_has_long_press_timer = false;
        f->cached_has_moved_beyond_threshold = false;
        f->single_tap_deferred = false;
        f->single_tap_hold_delay_ms = 0;
        f->held_actions_count = 0;
        f->deferred_tap_count = 0;
        f->pending_double_count = 0;
        f->double_tap_waiting = false;
        f->pending_resume_action_count = 0;
        g_state.finger_by_ptr_id[ptr_id] = f;
        g_state.active_finger_count++;
    }
    f->x = x; f->y = y;
    f->down_x = x; f->down_y = y;
    f->last_x = x; f->last_y = y;
    f->down_time_ms = time_ms;
    f->travel_x = 0; f->travel_y = 0;
    f->is_tap = true;

    // Copy all 12 FingerBindings lists from config
    FingerBindings* fb = &f->bindings;
    if (f->bindings_generation != g_state.cfg.bindings_generation) {
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            COPY_FINGER_BINDINGS(fb, &g_state.cfg, ts)
        } else {
            COPY_FINGER_BINDINGS(fb, &g_state.cfg, tp)
            fb->single_tap_drag_count = 0;
            fb->single_tap_drag_2nd_count = 0;
        }
        f->bindings_generation = g_state.cfg.bindings_generation;
    }
    touch_finger_cache_bs(f);

    handle_gesture_down(f, x, y, time_ms, &result);
    return result;
}

TouchActionResult touch_processor_on_finger_move(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return result;
    if (f->is_tap) {
        f->travel_x += fabsf(x - f->x);
        f->travel_y += fabsf(y - f->y);
        if (f->travel_x > MAX_TAP_TRAVEL || f->travel_y > MAX_TAP_TRAVEL) f->is_tap = false;
    }
    f->x = x; f->y = y;
    handle_gesture_move(f, x, y, time_ms, &result);
    return result;
}

TouchActionResult touch_processor_on_finger_up(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return result;
    f->x = x; f->y = y;
    handle_gesture_up(f, x, y, time_ms, &result);
    g_state.active_finger_count--;
    g_state.finger_by_ptr_id[f->ptr_id] = NULL;
    f->active = false;
    g_state.free_finger_hint = (int)(f - g_state.fingers);
    return result;
}

void touch_processor_on_finger_cancel(int ptr_id) {
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return;
    TouchActionResult cancel_result = {0};
    release_held_actions(&cancel_result);
    if (f->active) g_state.active_finger_count--;
    g_state.finger_by_ptr_id[f->ptr_id] = NULL;
    f->active = false;
}

TouchActionResult touch_processor_tick(uint64_t time_ms) {
    TouchActionResult result; result.count = 0;

    process_scheduled_actions(&result, time_ms);

    // Gesture-level timeouts (long-press, double-tap, single-tap-hold, second-finger double-tap)
    gesture_tick(time_ms, &result);

    // Auto-repeat + long-press + gesture timer: button elements only
    for (int _bi = 0; _bi < g_state.button_count; _bi++) {
        TouchElement* e = &g_state.elements[g_state.button_indices[_bi]];

        // Auto-repeat: toggle primary binding at the configured rate while finger is held
        if (e->auto_repeat && (e->toggle_switch ? e->selected : (e->current_ptr_id >= 0 && e->engaged))) {
            bool skip_inside = e->toggle_switch && e->selected;
            bool inside = skip_inside || point_in_element(e->visual_x, e->visual_y, e);
            if (!inside) {
                if (e->auto_repeat_primary_pressed && e->bindings[0].type != BINDING_NONE) {
                    release_binding(&result, &e->bindings[0]);
                    e->auto_repeat_primary_pressed = false;
                }
            } else {
                int interval_ms = e->auto_repeat_interval_ms > 0 ? e->auto_repeat_interval_ms : 100;
                if (e->auto_repeat_last_time == 0 || time_ms - e->auto_repeat_last_time >= (uint64_t)interval_ms) {
                    e->auto_repeat_primary_pressed = !e->auto_repeat_primary_pressed;
                    if (e->bindings[0].type != BINDING_NONE) {
                        if (e->auto_repeat_primary_pressed)
                            press_binding(&result, &e->bindings[0], true);
                        else
                            release_binding(&result, &e->bindings[0]);
                    }
                    e->auto_repeat_last_time = time_ms;
                }
            }
        }

        // Element long-press timeout
        if (e->current_ptr_id >= 0 && e->long_press_arm && !e->gesture_long_press_triggered
            && e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE)
        {
            uint64_t elapsed = time_ms - e->down_time_ms;
            __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "TICK LP elapsed=%llu delay=%d arm=%d",
                (unsigned long long)elapsed, g_state.cfg.long_press_delay_ms, e->long_press_arm);
            if (elapsed >= g_state.cfg.long_press_delay_ms) {
                e->gesture_long_press_triggered = true;
                e->visual_active = true;
                g_state.visual_state_dirty = true;
                e->long_press_arm = false;
                for (int k = 0; k < e->element_long_press_count; k++)
                    if (is_modifier_binding(&e->element_long_press[k]))
                        press_binding(&result, &e->element_long_press[k], true);
                for (int k = 0; k < e->element_long_press_count; k++)
                    if (!is_modifier_binding(&e->element_long_press[k]))
                        press_binding(&result, &e->element_long_press[k], true);
                if (e->button_long_press_haptic > 0)
                    add_action(&result, ACT_HAPTIC, e->button_long_press_haptic, 0, 0);
            }
        }

        // Element gesture timer (50ms for gesture-only buttons)
        if (e->current_ptr_id >= 0 && e->gesture_timer_armed && !e->gesture_swipe_triggered
            && !e->gesture_long_press_triggered
            && e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE)
        {
            if (time_ms - e->down_time_ms >= GESTURE_TIMER_MS) {
                e->gesture_swipe_triggered = true;
                e->visual_active = true;
                g_state.visual_state_dirty = true;
                e->gesture_timer_armed = false;
                for (int k = 0; k < e->element_gesture_count; k++)
                    if (is_modifier_binding(&e->element_gesture[k]))
                        press_binding(&result, &e->element_gesture[k], true);
                for (int k = 0; k < e->element_gesture_count; k++)
                    if (!is_modifier_binding(&e->element_gesture[k]))
                        press_binding(&result, &e->element_gesture[k], true);
                if (e->button_gesture_haptic > 0)
                    add_action(&result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
            }
        }
    }

    // Range button: hold timer + deferred release
    for (int _ri = 0; _ri < g_state.range_count; _ri++) {
        TouchElement* e = &g_state.elements[g_state.range_indices[_ri]];
        int kc = range_keycode(e->range_ordinal, e->range_index);

        // Range button hold timer
        if (e->current_ptr_id >= 0 && !e->range_hold_pressed && !e->range_scrolling && e->range_has_binding) {
            if (time_ms - e->down_time_ms >= RANGE_TAP_TIMEOUT_MS) {
                if (kc > 0) {
                    add_action(&result, ACT_KEY_PRESS, kc, 1, 0);
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
    // Save config/geometry before memset
    int saved_element_count = g_state.element_count;
    float saved_snapping_size = g_state.snapping_size;
    float saved_resolution_scale = g_state.resolution_scale;
    int saved_button_count = g_state.button_count;
    int saved_range_count = g_state.range_count;
    TouchElement saved_elements[MAX_ELEMENTS];
    memcpy(saved_elements, g_state.elements, sizeof(g_state.elements));

    memset(&g_state, 0, sizeof(g_state));

    // Restore config/geometry
    g_state.element_count = saved_element_count;
    g_state.snapping_size = saved_snapping_size;
    g_state.resolution_scale = saved_resolution_scale;
    g_state.button_count = saved_button_count;
    g_state.range_count = saved_range_count;
    memcpy(g_state.elements, saved_elements, sizeof(g_state.elements));

    // Reset runtime state only
    g_state.main_ptr_id = -1;
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_second_ptr_id = -1;
    g_state.finger_pointer_left = -1;
    g_state.finger_pointer_right = -1;
    g_state.pending_left_release_ptr_id = -1;
    g_state.pending_right_release_ptr_id = -1;
    g_state.sim_click_ptr_id = -1;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.hovered_element_per_ptr[i] = -1;
    g_state.free_finger_hint = 0;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.fingers[i].active = false;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.finger_by_ptr_id[i] = NULL;
    g_state.active_finger_count = 0;
    g_state.passthrough_active = false;
    g_state.sim_continue_click = false;
    g_state.sim_click_press_time = 0;
    g_state.sim_click_release_time = 0;
    g_state.pending_left_release_time = 0;
    g_state.pending_right_release_time = 0;
    g_state.pointer_left_enabled = true;
    g_state.pointer_right_enabled = true;
    g_state.scroll_accum_y = 0;

    for (int i = 0; i < g_state.element_count; i++) {
        g_state.elements[i].current_ptr_id = -1;
        g_state.elements[i].engaged = false;
        g_state.elements[i].visual_active = false;
        g_state.elements[i].visual_x = g_state.elements[i].x;
        g_state.elements[i].visual_y = g_state.elements[i].y;
        g_state.elements[i].down_time_ms = 0;
        g_state.elements[i].long_press_arm = false;
        g_state.elements[i].gesture_long_press_triggered = false;
        g_state.elements[i].gesture_swipe_triggered = false;
        g_state.elements[i].gesture_swipe_direction = -1;
        g_state.elements[i].gesture_timer_armed = false;
        g_state.elements[i].gesture_suppressed = false;
        g_state.elements[i].auto_repeat_last_time = 0;
        g_state.elements[i].auto_repeat_primary_pressed = false;
        g_state.elements[i].range_scrolling = false;
        g_state.elements[i].range_has_binding = false;
        g_state.elements[i].range_hold_pressed = false;
        g_state.elements[i].range_pending_tap_release = false;
        g_state.elements[i].range_tap_release_time = 0;
        g_state.elements[i].range_current_offset = 0;
        g_state.elements[i].range_scroll_offset = 0;
        g_state.elements[i].range_last_position = 0;
        g_state.elements[i].down_x = 0;
        g_state.elements[i].down_y = 0;
        g_state.elements[i].stick_value_x = 0;
        g_state.elements[i].stick_value_y = 0;
        g_state.elements[i].trackpad_last_x = 0;
        g_state.elements[i].trackpad_last_y = 0;
        g_state.elements[i].trackpad_vel_x = 0;
        g_state.elements[i].trackpad_vel_y = 0;
        g_state.elements[i].trackpad_last_time = 0;
        g_state.elements[i].selected = false;
        for (int p = 0; p < MAX_PETALS; p++)
            g_state.elements[i].petal_active[p] = false;
    }
    g_state.visual_state_dirty = true;
    activation_reset();
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

bool touch_processor_get_element_visual(int elemIndex, float* out_x, float* out_y, bool* out_active) {
    return activation_get_element_visual(elemIndex, out_x, out_y, out_active);
}

int touch_processor_sync_visual_states(float* out_positions, uint8_t* out_active, int max_count) {
    return activation_get_visual_states(out_positions, out_active, max_count);
}

void touch_processor_activate_at(float x, float y) {
    activation_activate_at(x, y);
}

void touch_processor_deactivate_all(void) {
    activation_deactivate_all();
}

bool touch_processor_handle_down_by_mode(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    bool handled = activation_handle_down(ptr_id, x, y, time_ms, &result);
    return handled;
}

bool touch_processor_handle_up_by_mode(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    return activation_handle_up(ptr_id, x, y, time_ms, &result);
}

void touch_processor_handle_move_by_mode(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    activation_handle_move(ptr_id, x, y, time_ms, &result);
}

int touch_processor_tracked_count(int ptr_id) {
    return activation_tracked_count(ptr_id);
}

int touch_processor_hovered_index(int ptr_id) {
    return activation_hovered_for_ptr(ptr_id);
}
