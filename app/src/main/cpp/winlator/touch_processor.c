#include "touch_processor_internal.h"
#include "touch_processor_activation.h"
#include <stddef.h>

TouchProcessorState g_state __attribute__((aligned(64)));

__attribute__((cold))
void touch_processor_init(const TouchProcessorConfig* config) {
    memset(&g_state, 0, sizeof(g_state));
    memset(g_state.finger_slot_by_ptr_id, 0xFF, sizeof(g_state.finger_slot_by_ptr_id));
    g_state.active_finger_count = 0;
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
    g_state.gesture_toggled_count = 0;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.hovered_element_per_ptr[i] = -1;
    if (g_state.cfg.cursor_acceleration_threshold <= 0) g_state.cfg.cursor_acceleration_threshold = 6;
    if (g_state.cfg.cursor_acceleration_factor <= 0.0f) g_state.cfg.cursor_acceleration_factor = 1.25f;
    if (g_state.cfg.xform_scale_x <= 0.0f) g_state.cfg.xform_scale_x = 1.0f;
    if (g_state.cfg.xform_scale_y <= 0.0f) g_state.cfg.xform_scale_y = 1.0f;
    g_state.cfg.bindings_generation = 1;
    compute_gesture_caps(&g_state.cfg);
    float th = g_state.cfg.gesture_threshold_px > 0 ? (float)g_state.cfg.gesture_threshold_px : 20.0f;
    g_state.gesture_threshold_sq = th * th;
}

void touch_processor_update_config(const TouchProcessorConfig* config) {
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    compute_gesture_caps(&g_state.cfg);
    float th = g_state.cfg.gesture_threshold_px > 0 ? (float)g_state.cfg.gesture_threshold_px : 20.0f;
    g_state.gesture_threshold_sq = th * th;
    g_state.cfg.bindings_generation++;

    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active) continue;
        FingerBindings* fb = &f->bindings;
        setup_main_finger_bindings(fb);
        f->bindings_generation = g_state.cfg.bindings_generation;
        touch_finger_cache_bs(f);
    }
}

__attribute__((cold))
void touch_processor_set_elements(const TouchElement* elements, int count) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "touch_processor_set_elements: count=%d", count);
    int n = count < MAX_ELEMENTS ? count : MAX_ELEMENTS;
    memcpy(g_state.elements, elements, n * sizeof(TouchElement));
    g_state.element_count = n;
    g_state.button_count = 0;
    g_state.range_count = 0;
    g_state.cfg.caps_has_track_hover_buttons = false;
    g_state.cfg.caps_has_element_toggle = false;
    g_state.cfg.caps_has_any_element_long_press = false;
    g_state.cfg.caps_has_any_element_gesture = false;
    g_state.cfg.caps_has_auto_repeat_buttons = false;
    g_state.cfg.caps_has_mouse_left_element = false;
    g_state.cfg.caps_has_passthrough_elements = false;
    g_state.activation_mode = (n > 0) ? g_state.elements[0].activation_mode : ACTIVATION_LOCK;

    for (int i = 0; i < n; i++) {
        TouchElement* e = &g_state.elements[i];
        e->visual_x = (float)e->x;
        e->visual_y = (float)e->y;
        e->visual_active = false;
        e->engaged = false;
        e->current_ptr_id = -1;

        // Initialize render defaults from config
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "set_elements[%d] type=%d visual=0 (init)", i, e->type);
        if (e->opacity <= 0.0f) e->opacity = 0.5f;
        if (e->corner_radius <= 0.0f) e->corner_radius = 0.8f;
        if (e->color_primary == 0) e->color_primary = g_state.cfg.color_primary;
        if (e->color_secondary == 0) e->color_secondary = g_state.cfg.color_secondary;
        if (e->stroke_width <= 0.0f) e->stroke_width = g_state.cfg.stroke_width_default;
        if (e->fill_alpha_inactive <= 0) e->fill_alpha_inactive = g_state.cfg.fill_alpha_inactive_default;

        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)
            g_state.cfg.caps_has_track_hover_buttons = true;
        e->cached_has_toggle = element_has_toggle(e);
        if (e->cached_has_toggle)
            g_state.cfg.caps_has_element_toggle = true;
        if (e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE)
            g_state.cfg.caps_has_any_element_long_press = true;
        if (e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE)
            g_state.cfg.caps_has_any_element_gesture = true;
        if (e->cached_has_auto_repeat)
            g_state.cfg.caps_has_auto_repeat_buttons = true;
        if (e->bindings[0].type == BINDING_MOUSE_LEFT)
            g_state.cfg.caps_has_mouse_left_element = true;
        if (e->passthrough_touch)
            g_state.cfg.caps_has_passthrough_elements = true;
        float hs_snap = g_state.snapping_size > 0.0f ? g_state.snapping_size : 1.0f;
        element_compute_snapped_hwhh(e, hs_snap);

        e->cached_has_auto_repeat = element_has_auto_repeat(e);
        e->cached_has_long_press = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
        e->cached_has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        e->cached_has_any_binding = e->bindings[0].type != BINDING_NONE
                                 || e->bindings[1].type != BINDING_NONE
                                 || e->bindings[2].type != BINDING_NONE
                                 || e->bindings[3].type != BINDING_NONE;
        e->cached_auto_repeat_interval = e->bindings[0].auto_repeat_interval_ms > 0
            ? e->bindings[0].auto_repeat_interval_ms : 100;
        e->cached_lp_has_toggle = false;
        for (int k = 0; k < e->element_long_press_count; k++)
            if (e->element_long_press[k].toggle) { e->cached_lp_has_toggle = true; break; }
        e->cached_gesture_has_toggle = false;
        for (int k = 0; k < e->element_gesture_count; k++)
            if (e->element_gesture[k].toggle) { e->cached_gesture_has_toggle = true; break; }
        e->cached_bind0_is_gamepad = is_gamepad_binding(&e->bindings[0]);
        e->cached_bind0_is_right_stick = is_right_stick_binding(e);
        e->cached_left = e->x - e->hw;
        e->cached_right = e->x + e->hw;
        e->cached_top = e->y - e->hh;
        e->cached_bottom = e->y + e->hh;
        e->cached_hw_sq = e->hw * e->hw;

        if (e->type == ELEM_BUTTON && g_state.button_count < MAX_ELEMENTS)
            g_state.button_indices[g_state.button_count++] = i;
        else if (e->type == ELEM_RANGE_BUTTON && g_state.range_count < MAX_ELEMENTS)
            g_state.range_indices[g_state.range_count++] = i;

        if (e->type == ELEM_RANGE_BUTTON) {
            float hs = g_state.snapping_size > 0.0f ? g_state.snapping_size : 1.0f;
            e->cached_range_cw = hs * (e->range_binding_count * 2) * e->scale;
            e->cached_range_ch = hs * 2.0f * e->scale;
            if (e->range_orientation == 1) { float _t = e->cached_range_cw; e->cached_range_cw = e->cached_range_ch; e->cached_range_ch = _t; }
            int rbc = e->range_binding_count > 0 ? e->range_binding_count : 1;
            e->cached_range_element_size = (e->range_orientation == 0 ? e->cached_range_cw * 2.0f : e->cached_range_ch * 2.0f) / (float)rbc;
            e->cached_scroll_size = e->cached_range_element_size * (float)e->range_max;
            e->cached_inv_scroll_size = 1.0f / e->cached_scroll_size;
        }
    }
    mark_all_dirty();
    build_spatial_grid();
}

void touch_processor_set_snapping_size(float size) {
    g_state.snapping_size = size;
    for (int i = 0; i < g_state.element_count; i++) {
        element_compute_snapped_hwhh(&g_state.elements[i], size);
    }
}

void touch_processor_set_resolution_scale(float scale) { g_state.resolution_scale = scale; }
void touch_processor_set_sim_touch_screen(bool enabled) { g_state.sim_touch_screen = enabled; }
void touch_processor_set_xform_scale(float scale_x, float scale_y) {
    g_state.cfg.xform_scale_x = scale_x > 0.0f ? scale_x : 1.0f;
    g_state.cfg.xform_scale_y = scale_y > 0.0f ? scale_y : 1.0f;
}

static void finger_init(TouchFinger* f, int ptr_id) {
    memset(f, 0, sizeof(TouchFinger));
    f->ptr_id = ptr_id;
    f->active = true;
    f->is_tap = true;
    g_state.finger_slot_by_ptr_id[ptr_id] = (uint8_t)(f - g_state.fingers);
    g_state.active_finger_count++;
}

__attribute__((hot))
TouchActionResult touch_processor_on_finger_down(int ptr_id, float x, float y, uint64_t time_ms) {
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "on_finger_down ptr=%d x=%.0f y=%.0f time=%llu count=%d",
        ptr_id, x, y, (unsigned long long)time_ms, g_state.element_count);
    TouchActionResult result; result.count = 0;
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "gesture_main_ptr_id=%d second_active=%d touch_mode=%d gen=%d",
        g_state.gesture_main_ptr_id, g_state.gesture_second_active,
        g_state.cfg.touch_mode, g_state.cfg.bindings_generation);
    // Java does NOT release held actions on every finger-down — only in specific
    // paths (second-finger TouchscreenGestureHandler path or TouchpadGestureHandler
    // onFingerDown). Releasing here would cancel a long-press hold
    // (e.g. mouse-right held) when a second finger touches for scrolling.
    // release_held_actions(&result);

    TouchFinger* f = find_finger(ptr_id);
    if (!f) {
        f = find_free_finger();
        if (!f) {
            TP_LOG(ANDROID_LOG_ERROR, LOG_TAG, "on_finger_down: NO FREE FINGER SLOT!");
            return result;
        }
        finger_init(f, ptr_id);
    }
    f->x = x; f->y = y;
    f->down_x = x; f->down_y = y;
    f->last_x = x; f->last_y = y;
    f->down_time_ms = time_ms;
    f->travel_x = 0; f->travel_y = 0;
    f->is_tap = true;

    // Copy all 12 FingerBindings lists from config
    FingerBindings* fb = &f->bindings;
    uint32_t bindings_gen = g_state.cfg.bindings_generation;
    if (__builtin_expect(f->bindings_generation != bindings_gen, 0)) {
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "bindings copy: finger_gen=%d cfg_gen=%d",
            f->bindings_generation, bindings_gen);
        setup_main_finger_bindings(fb);
        f->bindings_generation = bindings_gen;
        touch_finger_cache_bs(f);
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "bindings result: S=%d L=%d D=%d Sd=%d Ld=%d Dd=%d",
            fb->single_tap_count, fb->long_press_count, fb->double_tap_count,
            fb->single_tap_drag_count, fb->long_press_drag_count, fb->double_tap_drag_count);
    }

    if (__builtin_expect(g_state.cfg.caps_mode_mask != 0, 1)
        || g_state.gesture_double_tap_waiting
        || g_state.gesture_post_double_tap_drag
        || g_state.second_double_tap_waiting)
        handle_gesture_down(f, x, y, time_ms, &result);
    return result;
}

__attribute__((hot))
TouchActionResult touch_processor_on_finger_move(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(!f, 0)) return result;
    if (f->is_tap) {
        f->travel_x += fabsf(x - f->x);
        f->travel_y += fabsf(y - f->y);
        if (__builtin_expect(f->travel_x > MAX_TAP_TRAVEL, 0) || __builtin_expect(f->travel_y > MAX_TAP_TRAVEL, 0))
            f->is_tap = false;
    }
    f->x = x; f->y = y;
    if (__builtin_expect(g_state.cfg.caps_mode_mask != 0, 1)
        || g_state.gesture_double_tap_waiting
        || g_state.gesture_post_double_tap_drag
        || g_state.second_double_tap_waiting)
        handle_gesture_move(f, x, y, time_ms, &result);
    return result;
}

__attribute__((hot))
TouchActionResult touch_processor_on_finger_up(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(!f, 0)) {
        TP_LOG(ANDROID_LOG_WARN, LOG_TAG, "on_finger_up: finger not found ptr=%d", ptr_id);
        return result;
    }
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "on_finger_up ptr=%d x=%.0f y=%.0f state=%d active=%d act_fingers=%d",
        ptr_id, x, y, f->state, f->active, g_state.active_finger_count);
    f->x = x; f->y = y;
    if (__builtin_expect(g_state.cfg.caps_mode_mask != 0, 1)
        || g_state.gesture_double_tap_waiting
        || g_state.gesture_post_double_tap_drag
        || g_state.second_double_tap_waiting)
        handle_gesture_up(f, x, y, time_ms, &result);
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "on_finger_up: after handle_gesture_up active=%d ptr_id=%d",
        f->active, f->ptr_id);
    deactivate_finger(f);
    return result;
}

void touch_processor_on_finger_cancel(int ptr_id) {
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return;
    TouchActionResult cancel_result = {0};
    release_held_actions(&cancel_result);
    // Clear global gesture state if this was the main or second finger
    if (f->ptr_id == g_state.gesture_main_ptr_id) {
        g_state.gesture_main_ptr_id = -1;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = false;
        gesture_clear_deferred_tap();
        gesture_clear_pending_long_press();
        if (!g_state.gesture_second_active)
            g_state.gesture_deferred_second_finger_tap = false;
    } else if (f->ptr_id == g_state.gesture_second_ptr_id) {
        gesture_clear_second_finger_globals();
        gesture_clear_second_finger_state();
    }
    f->state = GESTURE_STATE_IDLE;
    if (f->active) deactivate_finger(f);
}

static inline void process_delayed_actions(TouchActionResult* restrict result, uint64_t time_ms) {
    if (g_state.pending_left_release_time == 0
        && g_state.pending_right_release_time == 0
        && g_state.sim_click_press_time == 0
        && g_state.sim_click_release_time == 0)
        return;
    if (g_state.pending_left_release_time > 0 && time_ms >= g_state.pending_left_release_time) {
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        g_state.pending_left_release_time = 0;
        g_state.pending_left_release_ptr_id = -1;
        g_state.pointer_left_enabled = true;
    }
    if (g_state.pending_right_release_time > 0 && time_ms >= g_state.pending_right_release_time) {
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 1, 0, 0);
        g_state.pending_right_release_time = 0;
        g_state.pending_right_release_ptr_id = -1;
        g_state.pointer_right_enabled = true;
    }

    if (g_state.sim_click_press_time > 0 && time_ms >= g_state.sim_click_press_time) {
        if (g_state.sim_continue_click) {
            TouchFinger* sf = find_finger(g_state.sim_click_ptr_id);
            if (sf) {
                add_action(result, ACT_POINTER_MOVE, (int)g_state.last_touch_x, (int)g_state.last_touch_y, 0);
                add_action(result, ACT_POINTER_BUTTON_PRESS, 0, 0, 0);
            }
        }
        g_state.sim_click_press_time = 0;
    }

    if (g_state.sim_click_release_time > 0 && time_ms >= g_state.sim_click_release_time) {
        if (g_state.sim_continue_click)
            add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        g_state.sim_click_release_time = 0;
        g_state.sim_continue_click = false;
    }
}

static inline void process_auto_repeat_burst(TouchActionResult* restrict result, TouchBinding* restrict actions, int count, uint64_t* restrict last_time, uint64_t time_ms) {
    if (count == 0) return;
    for (int i = 0; i < count; i++) {
        TouchBinding* b = &actions[i];
        if (!b->auto_repeat) continue;
        int interval_ms = b->auto_repeat_interval_ms;
        if (interval_ms <= 0) continue;

        uint64_t last = last_time[i];
        if (last == 0 || time_ms - last >= (uint64_t)interval_ms) {
            press_binding(result, b, false);
            last_time[i] = time_ms;
        }
    }
}

__attribute__((hot))
TouchActionResult touch_processor_tick(uint64_t time_ms) {
    TouchActionResult result; result.count = 0;

    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "tick: time=%llu active=%d dt_wait=%d sdtw=%d caps_gesture=%d caps_dt=%d",
        (unsigned long long)time_ms, g_state.active_finger_count,
        g_state.gesture_double_tap_waiting, g_state.second_double_tap_waiting,
        g_state.cfg.caps_has_gesture_bindings, g_state.cfg.caps_has_double_tap);

    process_scheduled_actions(&result, time_ms);

    // Gesture-level timeouts (long-press, double-tap, single-tap-hold, second-finger double-tap)
    if (current_mode_has_gestures() || g_state.gesture_double_tap_waiting || g_state.second_double_tap_waiting)
        gesture_tick(time_ms, &result);

    // Auto-repeat + long-press + gesture timer: button elements only
    int button_count = g_state.button_count;
    TouchElement* elements = g_state.elements;
    int* button_indices = g_state.button_indices;
    uint32_t lp_delay_ms = g_state.cfg.long_press_delay_ms;
    bool has_auto_repeat_buttons = g_state.cfg.caps_has_auto_repeat_buttons;
    bool has_any_element_long_press = g_state.cfg.caps_has_any_element_long_press;
    bool has_any_element_gesture = g_state.cfg.caps_has_any_element_gesture;
    for (int _bi = 0; _bi < button_count; _bi++) {
        TouchElement* e = &elements[button_indices[_bi]];
        if (__builtin_expect(_bi + 4 < button_count, 1))
            __builtin_prefetch(&elements[button_indices[_bi + 4]], 0, 1);

        if (__builtin_expect(!e->engaged && !e->selected && e->current_ptr_id < 0
            && !(has_auto_repeat_buttons && e->cached_has_auto_repeat)
            && !(has_any_element_long_press && e->cached_has_long_press)
            && !(has_any_element_gesture && e->cached_has_gesture), 1)) {
            e->visual_x = e->x; e->visual_y = e->y;
            continue;
        }

        if (__builtin_expect(e->engaged && e->current_ptr_id < 0, 0)) {
            e->engaged = false;
            e->visual_active = false;
            mark_element_dirty(e);
        }

        if (has_auto_repeat_buttons) {
        // Auto-repeat: burst all bindings (toggle) or alternate primary (non-toggle)
        if (e->cached_has_auto_repeat && (e->cached_has_toggle ? e->selected : (e->current_ptr_id >= 0 && e->engaged))) {
            if (e->cached_has_toggle && e->selected) {
                // Burst mode: press+release all bindings at interval with binding delay
                int interval_ms = e->cached_auto_repeat_interval;
                if (e->auto_repeat_last_time == 0 || time_ms - e->auto_repeat_last_time >= (uint64_t)interval_ms) {
                    execute_actions(&result, e->bindings, 4);
                    e->auto_repeat_last_time = time_ms;
                }
            } else {
                // Non-toggle auto-repeat: alternate press/release of primary binding
                bool inside = point_in_element(e->visual_x, e->visual_y, e);
                if (!inside) {
                    if (e->auto_repeat_primary_pressed && e->bindings[0].type != BINDING_NONE) {
                        release_binding(&result, &e->bindings[0]);
                        e->auto_repeat_primary_pressed = false;
                    }
                } else {
                    int interval_ms = e->cached_auto_repeat_interval;
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
        }
        }

        // Element long-press timeout
        if (has_any_element_long_press) {
            if (__builtin_expect(e->current_ptr_id >= 0, 0) && e->long_press_arm && !e->gesture_long_press_triggered && !e->lp_toggled
                && e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE)
            {
                uint64_t elapsed = time_ms - e->down_time_ms;
                TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "TICK LP elapsed=%llu delay=%d arm=%d",
                    (unsigned long long)elapsed, lp_delay_ms, e->long_press_arm);
                if (elapsed >= lp_delay_ms) {
                    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "tick[%d] type=%d visual=1 (long_press_fire elapsed=%llu)", (int)(e - g_state.elements), e->type, (unsigned long long)elapsed);
                    e->gesture_long_press_triggered = true;
                    e->visual_active = true;
                    e->visual_long_press_active = true;
                    mark_element_dirty(e);
                    e->long_press_arm = false;
                    if (e->lp_toggled) {
                        release_bindings_list(&result, e->element_long_press, e->element_long_press_count);
                        e->lp_toggled = false;
                    } else {
                        press_bindings_list(&result, e->element_long_press, e->element_long_press_count);
                        if (e->cached_lp_has_toggle) e->lp_toggled = true;
                    }
                    if (e->button_long_press_haptic > 0)
                        add_action(&result, ACT_HAPTIC, e->button_long_press_haptic, 0, 0);
                }
            }
        }

        // Element gesture timer (50ms for gesture-only buttons)
        if (has_any_element_gesture) {
            if (__builtin_expect(e->current_ptr_id >= 0, 0) && e->gesture_timer_armed && !e->gesture_swipe_triggered
                && !e->gesture_long_press_triggered && !e->gesture_toggled
                && e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE)
            {
                if (time_ms - e->down_time_ms >= GESTURE_TIMER_MS) {
                    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "tick[%d] type=%d visual=1 (gesture_timer_fire elapsed=%llu)", (int)(e - g_state.elements), e->type, (unsigned long long)(time_ms - e->down_time_ms));
                    e->gesture_swipe_triggered = true;
                    e->visual_active = true;
                    mark_element_dirty(e);
                    e->gesture_timer_armed = false;
                    if (e->gesture_toggled) {
                        release_bindings_list(&result, e->element_gesture, e->element_gesture_count);
                        e->gesture_toggled = false;
                    } else {
                        press_bindings_list(&result, e->element_gesture, e->element_gesture_count);
                        if (e->cached_gesture_has_toggle) e->gesture_toggled = true;
                    }
                    if (e->button_gesture_haptic > 0)
                        add_action(&result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                }
            }
        }
    }

    // Gesture auto-repeat: burst any binding with auto_repeat flag.
    // This is purely binding-driven, not gesture-type-aware.
    process_auto_repeat_burst(&result, g_state.gesture_toggled_actions, g_state.gesture_toggled_count, g_state.gesture_auto_repeat_last_time, time_ms);
    if (__builtin_expect(g_state.gesture_is_action_held, 0))
        process_auto_repeat_burst(&result, g_state.gesture_held_actions, g_state.gesture_held_count, g_state.gesture_auto_repeat_last_time_held, time_ms);

    // Range button: hold timer + deferred release
    int range_count = g_state.range_count;
    int* range_indices = g_state.range_indices;
    for (int _ri = 0; _ri < range_count; _ri++) {
        if (_ri + 4 < range_count)
            __builtin_prefetch(&elements[range_indices[_ri + 4]], 0, 1);
        TouchElement* e = &elements[range_indices[_ri]];
        int _ridx = range_indices[_ri];

        // Range button hold timer
        if (__builtin_expect(e->current_ptr_id >= 0, 0) && !e->range_hold_pressed && !e->range_scrolling && e->range_has_binding) {
            if (time_ms - e->down_time_ms >= RANGE_TAP_TIMEOUT_MS) {
                int kc = range_keycode(e->range_ordinal, e->range_index);
                TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "tick[%d] hold_timeout elapsed=%llu kc=%d",
                    _ridx, (unsigned long long)(time_ms - e->down_time_ms), kc);
                if (kc > 0) {
                    add_action(&result, ACT_KEY_PRESS, kc, 1, 0);
                    e->range_hold_pressed = true;
                }
            }
        }

        // Range button deferred tap release
        if (e->range_pending_tap_release && time_ms >= e->range_tap_release_time) {
            int kc = e->range_initial_kc;
            TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "tick[%d] tap_release kc=%d", _ridx, kc);
            if (kc > 0)
                add_action(&result, ACT_KEY_RELEASE, kc, 0, 0);
            e->range_pending_tap_release = false;
            e->range_scrolling = false;
            e->range_has_binding = false;
            e->range_hold_pressed = false;
        }
    }

    process_delayed_actions(&result, time_ms);

    return result;
}

void touch_processor_reset(void) {
    // Save fields that are zeroed by targeted memset but need to be preserved
    int saved_element_count = g_state.element_count;
    float saved_snapping_size = g_state.snapping_size;
    float saved_resolution_scale = g_state.resolution_scale;
    int saved_button_count = g_state.button_count;
    int saved_range_count = g_state.range_count;
    float saved_ptr_x = g_state.ptr_x;
    float saved_ptr_y = g_state.ptr_y;

    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "reset: cfg.touch_mode=%d gen=%d caps_gesture=%d caps_dt=%d caps_drag=%d",
        g_state.cfg.touch_mode, g_state.cfg.bindings_generation,
        g_state.cfg.caps_has_gesture_bindings, g_state.cfg.caps_has_double_tap, g_state.cfg.caps_has_drag_bindings);

    // Zero all runtime fields between cfg end and elements start
    ptrdiff_t hot_start = offsetof(TouchProcessorState, finger_slot_by_ptr_id);
    ptrdiff_t hot_end = offsetof(TouchProcessorState, elements);
    memset((char*)&g_state + hot_start, 0, hot_end - hot_start);

    // Restore preserved fields
    g_state.element_count = saved_element_count;
    g_state.snapping_size = saved_snapping_size;
    g_state.resolution_scale = saved_resolution_scale;
    g_state.button_count = saved_button_count;
    g_state.range_count = saved_range_count;
    g_state.ptr_x = saved_ptr_x;
    g_state.ptr_y = saved_ptr_y;

    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "reset: after restore cfg.touch_mode=%d gen=%d caps_gesture=%d",
        g_state.cfg.touch_mode, g_state.cfg.bindings_generation, g_state.cfg.caps_has_gesture_bindings);

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
    memset(g_state.finger_slot_by_ptr_id, 0xFF, sizeof(g_state.finger_slot_by_ptr_id));
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
    g_state.gesture_toggled_count = 0;

    float th = g_state.cfg.gesture_threshold_px > 0 ? (float)g_state.cfg.gesture_threshold_px : 20.0f;
    g_state.gesture_threshold_sq = th * th;

    for (int i = 0; i < g_state.element_count; i++)
        element_reset_runtime(&g_state.elements[i]);
    mark_all_dirty();
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
