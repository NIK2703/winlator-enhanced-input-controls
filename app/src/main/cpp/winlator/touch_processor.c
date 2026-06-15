#include "touch_processor_internal.h"
#include "touch_processor_activation.h"
#include "gesture/types.h"

// spatial grid rebuild (defined in element/shared.c)
void init_bezier_lut(void);
#define DEFAULT_OPACITY 0.5f
#define DEFAULT_CORNER_RADIUS 0.8f
#define TICK_GAP_WARNING_MS 100
#define DEFAULT_CURSOR_ACCEL_THRESHOLD 6
#define DEFAULT_CURSOR_ACCEL_FACTOR 1.25f
#define DEFAULT_AUTO_REPEAT_INTERVAL_MS 100
#define HIGH_ACTION_COUNT_THRESHOLD 16
#define TICK_BUDGET_US 16000
static uint64_t tp_diag_tick_count = 0;
static uint64_t tp_diag_slow_ticks = 0;
static uint64_t tp_diag_gap_ticks = 0;
static uint64_t tp_diag_last_status_ms = 0;
static uint64_t tp_diag_last_tick_ms = 0;
static uint64_t tp_diag_total_actions = 0;
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
    g_state.gesture_toggled_count = 0;
    for (int i = 0; i < MAX_FINGERS; i++) g_state.hovered_element_per_ptr[i] = -1;
    if (g_state.cfg.cursor_acceleration_threshold <= 0) g_state.cfg.cursor_acceleration_threshold = DEFAULT_CURSOR_ACCEL_THRESHOLD;
    if (g_state.cfg.cursor_acceleration_factor <= 0.0f) g_state.cfg.cursor_acceleration_factor = DEFAULT_CURSOR_ACCEL_FACTOR;
    if (g_state.cfg.xform_scale_x <= 0.0f) g_state.cfg.xform_scale_x = 1.0f;
    if (g_state.cfg.xform_scale_y <= 0.0f) g_state.cfg.xform_scale_y = 1.0f;
    if (g_state.cfg.scroll_threshold_px <= 0) g_state.cfg.scroll_threshold_px = 50;
    if (g_state.cfg.scroll_hold_threshold_px <= 0) g_state.cfg.scroll_hold_threshold_px = 100;
    g_state.cfg.bindings_generation = 1;
    compute_gesture_caps(&g_state.cfg);
}

void touch_processor_update_config(const TouchProcessorConfig* config) {
    memcpy(&g_state.cfg, config, sizeof(TouchProcessorConfig));
    compute_gesture_caps(&g_state.cfg);

    // Log scroll config state
    {
        static const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};
        static const char* slot_names[] = {"Sd", "Ld", "Dd", "Sd2", "Dd2"};
        __android_log_print(ANDROID_LOG_WARN, "ScrollDbg",
            "CONFIG: is_ts=%d caps_scroll=%d thresh=%d",
            g_state.cfg.is_ts, g_state.cfg.caps_has_scroll_bindings, g_state.cfg.scroll_threshold_px);
        for (int s = 0; s < 5; s++) {
            for (int d = 0; d < 4; d++) {
                int cnt = g_state.cfg.scroll_bindings[s].dirs[d].count;
                if (cnt > 0) {
                    __android_log_print(ANDROID_LOG_WARN, "ScrollDbg",
                        "CONFIG %s/%s: %d bindings", slot_names[s], dir_names[d], cnt);
                }
            }
        }
    }

    g_state.cfg.bindings_generation++;

    // Clear stale gesture state from previous mode on switch
    g_state.gesture_post_double_tap_drag = false;
    g_state.gesture_second_active = false;
    g_state.gesture_second_ptr_id = -1;
    g_state.gesture_double_tap_waiting = false;
    g_state.gesture_double_tap_consumed = false;
    g_state.gesture_deferred_second_finger_tap = false;
    g_state.gesture_main_ptr_id = -1;
    g_state.second_double_tap_waiting = false;
    gesture_clear_deferred_tap();
    gesture_clear_pending_long_press();
    gesture_clear_second_finger_state();
    {
        TouchActionResult release_result = {0};
        release_held_actions(&release_result);
    }

    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active) continue;
        if (f->is_second_finger) {
            setup_second_finger_bindings(f);
        } else {
            FingerBindings* fb = &f->bindings;
            setup_main_finger_bindings(fb);
        }
        f->bindings_generation = g_state.cfg.bindings_generation;
        touch_finger_cache_bindings_state(f);
    }
}

static void init_element_visuals(TouchElement* e) {
    e->visual_x = (float)e->x;
    e->visual_y = (float)e->y;
    e->visual_flags = 0;
    e->visual_layers = 0;
    e->visual_alpha = 0;
    e->engaged = false;
    e->current_ptr_id = -1;
    int idx = (int)(e - g_state.elements);
    if (e->opacity <= 0.0f) e->opacity = DEFAULT_OPACITY;
    if (e->corner_radius <= 0.0f) e->corner_radius = DEFAULT_CORNER_RADIUS;
    if (e->color_primary == 0) e->color_primary = g_state.cfg.color_primary;
    if (e->color_secondary == 0) e->color_secondary = g_state.cfg.color_secondary;
    if (e->stroke_width <= 0.0f) e->stroke_width = g_state.cfg.stroke_width_default;
    if (e->fill_alpha_inactive <= 0) e->fill_alpha_inactive = g_state.cfg.fill_alpha_inactive_default;
}

static void compute_element_caps(TouchElement* e, int index) {
    if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)
        g_state.cfg.caps_has_track_hover_buttons = true;
    e->cached_has_toggle = element_has_toggle(e);
    if (e->cached_has_toggle)
        g_state.cfg.caps_has_element_toggle = true;
    if (e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE)
        g_state.cfg.caps_has_any_element_long_press = true;
    if (e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE)
        g_state.cfg.caps_has_any_element_gesture = true;
    e->cached_has_auto_repeat = element_has_auto_repeat(e);
    if (e->cached_has_auto_repeat)
        g_state.cfg.caps_has_auto_repeat_buttons = true;
    if (e->bindings[0].type == BINDING_MOUSE_LEFT)
        g_state.cfg.caps_has_mouse_left_element = true;
    if (e->passthrough_touch)
        g_state.cfg.caps_has_passthrough_elements = true;
}

static void cache_element_flags(TouchElement* e) {
    e->cached_has_long_press = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
    e->cached_has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
    e->cached_petal_mask = 0;
    for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++)
        if (e->bindings[k].type != BINDING_NONE)
            e->cached_petal_mask |= (1 << k);
    e->cached_has_any_binding = e->cached_petal_mask != 0;
    e->cached_auto_repeat_interval = DEFAULT_AUTO_REPEAT_INTERVAL_MS;
    for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++) {
        if ((e->bindings[k].toggle || e->bindings[k].auto_repeat) && e->bindings[k].auto_repeat_interval_ms > 0) {
            e->cached_auto_repeat_interval = e->bindings[k].auto_repeat_interval_ms;
            break;
        }
    }
    if (e->element_long_press_count > 0) {
        e->cached_lp_has_toggle = false;
        for (int k = 0; k < e->element_long_press_count; k++)
            if (e->element_long_press[k].toggle) { e->cached_lp_has_toggle = true; break; }
    } else {
        e->cached_lp_has_toggle = false;
    }
    if (e->element_gesture_count > 0) {
        e->cached_gesture_has_toggle = false;
        for (int k = 0; k < e->element_gesture_count; k++)
            if (e->element_gesture[k].toggle) { e->cached_gesture_has_toggle = true; break; }
    } else {
        e->cached_gesture_has_toggle = false;
    }
    e->cached_bind0_is_gamepad = is_gamepad_binding(&e->bindings[0]);
    e->cached_bind0_is_right_stick = is_right_stick_binding(e);
}

static void compute_element_geometry(TouchElement* e) {
    float hs_snap = g_state.snapping_size > 0.0f ? g_state.snapping_size : 1.0f;
    element_compute_snapped_hwhh(e, hs_snap);
    e->cached_left = e->x - e->hw;
    e->cached_right = e->x + e->hw;
    e->cached_top = e->y - e->hh;
    e->cached_bottom = e->y + e->hh;
    e->cached_hw_sq = e->hw * e->hw;
    int idx = (int)(e - g_state.elements);
    if (e->type == ELEM_BUTTON && g_state.button_count < MAX_ELEMENTS)
        g_state.button_indices[g_state.button_count++] = idx;
    else if (e->type == ELEM_RANGE_BUTTON && g_state.range_count < MAX_ELEMENTS)
        g_state.range_indices[g_state.range_count++] = idx;
    if (e->type == ELEM_RANGE_BUTTON) {
        float hs = g_state.snapping_size > 0.0f ? g_state.snapping_size : 1.0f;
        e->cached_range_cw = hs * (e->range_binding_count * 2) * e->scale;
        e->cached_range_ch = hs * RANGE_HEIGHT_MULTIPLIER * e->scale;
        if (e->range_orientation == 1) { float _t = e->cached_range_cw; e->cached_range_cw = e->cached_range_ch; e->cached_range_ch = _t; }
        int rbc = e->range_binding_count > 0 ? e->range_binding_count : 1;
        e->cached_range_element_size = (e->range_orientation == 0 ? e->cached_range_cw * 2.0f : e->cached_range_ch * 2.0f) / (float)rbc;
    }
}

typedef struct {
    bool selected[MAX_ELEMENTS];
    bool gesture_toggled[MAX_ELEMENTS];
    bool lp_toggled[MAX_ELEMENTS];
} ToggleState;

static void save_toggle_state(ToggleState* state, int count) {
    for (int i = 0; i < count; i++) {
        state->selected[i] = g_state.elements[i].selected;
        state->gesture_toggled[i] = g_state.elements[i].gesture_toggled;
        state->lp_toggled[i] = g_state.elements[i].lp_toggled;
    }
}

static void restore_toggle_state(const ToggleState* state, int count) {
    for (int i = 0; i < count; i++) {
        g_state.elements[i].selected = state->selected[i];
        g_state.elements[i].gesture_toggled = state->gesture_toggled[i];
        g_state.elements[i].lp_toggled = state->lp_toggled[i];
        update_visual_layers(&g_state.elements[i]);
    }
}

void touch_processor_set_elements(const TouchElement* elements, int count) {
    int n = count < MAX_ELEMENTS ? count : MAX_ELEMENTS;

    int old_count = g_state.element_count;
    int save_n = old_count < n ? old_count : n;
    ToggleState saved_toggle;
    save_toggle_state(&saved_toggle, save_n);

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
    bool has_track = false, has_hover = false;
    for (int i = 0; i < n; i++) {
        if (g_state.elements[i].activation_mode == ACTIVATION_TRACK) has_track = true;
        if (g_state.elements[i].activation_mode == ACTIVATION_HOVER) has_hover = true;
    }
    if (has_hover) g_state.activation_mode = ACTIVATION_HOVER;
    else if (has_track) g_state.activation_mode = ACTIVATION_TRACK;
    else g_state.activation_mode = ACTIVATION_LOCK;

    for (int i = 0; i < n; i++) {
        TouchElement* e = &g_state.elements[i];
        init_element_visuals(e);
        compute_element_caps(e, i);
        cache_element_flags(e);
        compute_element_geometry(e);
    }
    // Restore toggle state for elements that existed before the update
    restore_toggle_state(&saved_toggle, save_n);
    mark_all_dirty();
    build_spatial_grid();

    for (int i = 0; i < n; i++) {
        TouchElement* e = &g_state.elements[i];
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
            "ELEM[%d] type=%d x=%d y=%d w=%.2f h=%.2f sc=%.2f hw=%.1f hh=%.1f L=%.1f R=%.1f T=%.1f B=%.1f b0=%d",
            i, e->type, e->x, e->y, e->w, e->h, e->scale,
            e->hw, e->hh, e->cached_left, e->cached_right, e->cached_top, e->cached_bottom,
            e->bindings[0].type);
    }
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace",
        "GRID x=[%.1f..%.1f] y=[%.1f..%.1f] cw=%.1f ch=%.1f snap=%.1f",
        g_state.grid_min_x, g_state.grid_max_x, g_state.grid_min_y, g_state.grid_max_y,
        g_state.grid_cell_w, g_state.grid_cell_h, g_state.snapping_size);
}

void touch_processor_set_snapping_size(float size) {
    g_state.snapping_size = size;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        element_compute_snapped_hwhh(e, size);
        element_update_position_cache(e);
    }
    mark_all_dirty();
    build_spatial_grid();
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
    f->bindings_generation = 0;
    f->is_tap = true;
    g_state.finger_by_ptr_id[ptr_id] = f;
    g_state.active_finger_count++;
}

TouchActionResult touch_processor_on_finger_down(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    TouchFinger* f = find_finger(ptr_id);
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "TP_DOWN ptr=%d found_finger=%d active_fingers=%d elements=%d", ptr_id, f != NULL, g_state.active_finger_count, g_state.element_count);
    if (!f) {
        f = find_free_finger();
        if (!f) {
            __android_log_print(ANDROID_LOG_WARN, "SigTrace", "TP_DOWN NO FREE FINGER! ptr=%d", ptr_id);
            return result;
        }
        finger_init(f, ptr_id);
    }
    f->x = x; f->y = y;
    f->down_x = x; f->down_y = y;
    f->last_x = x; f->last_y = y;
    f->sub_pixel_x = 0; f->sub_pixel_y = 0;
    f->down_time_ms = time_ms;
    f->travel_x = 0; f->travel_y = 0;
    f->is_tap = true;

    // Copy all 12 FingerBindings lists from config (only when generation changes)
    FingerBindings* fb = &f->bindings;
    uint32_t bindings_gen = g_state.cfg.bindings_generation;
    if (__builtin_expect(f->bindings_generation != bindings_gen, 0)) {
        setup_main_finger_bindings(fb);
        f->bindings_generation = bindings_gen;
    }
    // Always refresh cached flags — they may have been cleared between DOWNs
    // (e.g. by enter_double_tap_waiting, LP timer paths).
    touch_finger_cache_bindings_state(f);

    if (gesture_processing_needed())
        handle_gesture_down(f, x, y, time_ms, &result);
    return result;
}

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
    if (gesture_processing_needed())
        handle_gesture_move(f, x, y, time_ms, &result);
    return result;
}

TouchActionResult touch_processor_on_finger_up(int ptr_id, float x, float y, uint64_t time_ms) {
    TouchActionResult result; result.count = 0;
    TouchFinger* f = find_finger(ptr_id);
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "TP_UP ptr=%d found_finger=%d single_tap_deferred=%d state=%d engaged=%d sched=%d held=%d",
        ptr_id, f != NULL, f ? f->single_tap_deferred : -1, f ? f->state : -1,
        f ? f->engaged_elem_count : -1,
        g_state.scheduled_action_count, g_state.gesture_held_count);
    if (__builtin_expect(!f, 0)) {
        __android_log_print(ANDROID_LOG_WARN, "SigTrace", "TP_UP FINGER NOT FOUND! ptr=%d", ptr_id);
        return result;
    }
    f->x = x; f->y = y;
    if (gesture_processing_needed())
        handle_gesture_up(f, x, y, time_ms, &result);

    // Guarantee: any scheduled press from gesture path MUST be cancelled on finger UP.
    // release_held_actions may not run in all code paths (e.g. early returns in
    // handle_gesture_up), so we cancel here as a safety net to prevent orphaned presses.
    if (g_state.scheduled_action_count > 0) {
        int cancelled = 0;
        for (int i = 0; i < g_state.scheduled_action_count; i++) {
            if (g_state.scheduled_actions[i].active) {
                g_state.scheduled_actions[i].active = false;
                cancelled++;
            }
        }
        if (cancelled > 0)
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "TP_UP cancelled_sched=%d", cancelled);
    }
    if (result.count == 0) {
    }
    if (__builtin_expect(f->single_tap_deferred, 0)) {
        return result; // gesture_tick will handle cleanup via deferred tap
    }
    // Keep finger alive for gesture_tick to process DT_TIMEOUT
    if (__builtin_expect(f->state == GESTURE_STATE_DOUBLE_TAP_WAITING, 0)) {
        return result; // gesture_tick will fire DT_TIMEOUT and deactivate
    }
    deactivate_finger(f);
    g_state.free_finger_hint = (int)(f - g_state.fingers);
    return result;
}

void touch_processor_on_finger_cancel(int ptr_id) {
    TouchFinger* f = find_finger(ptr_id);
    if (!f) return;
    TouchActionResult cancel_result = {0};
    release_held_actions(&cancel_result);

    // Release element bindings and clean up tracked/hovered state
    int pi = (uint32_t)ptr_id % MAX_FINGERS;
    TrackedButtons* tb = &g_state.tracked[pi];
    if (tb->ptr_id == ptr_id) {
        for (int j = 0; j < tb->count; j++) {
            int idx = tb->element_indices[j];
            if (idx >= 0 && idx < g_state.element_count) {
                TouchElement* e = &g_state.elements[idx];
                release_element_bindings(e, &cancel_result, true);
                // release_element_bindings preserves toggle bindings when selected=true.
                // On cancel we must force-release everything to avoid stuck keys.
                force_release_element_toggles(e, &cancel_result);
            }
        }
        for (int j = 0; j < tb->count; j++) {
            int idx = tb->element_indices[j];
            if (idx >= 0 && idx < g_state.element_count)
                g_state.elements[idx].current_ptr_id = -1;
        }
        reset_tracked_slot(tb, pi);
    }
    g_state.hovered_element_per_ptr[pi] = -1;

    // Release any engaged elements not in tracked list
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].current_ptr_id == ptr_id) {
            release_element_bindings(&g_state.elements[i], &cancel_result, true);
            force_release_element_toggles(&g_state.elements[i], &cancel_result);
        }
    }

    // Release and clear global gesture toggle state
    for (int t = 0; t < g_state.gesture_toggled_count; t++)
        release_binding(&cancel_result, &g_state.gesture_toggled_actions[t]);
    g_state.gesture_toggled_count = 0;

    // Clear global gesture state if this was the main or second finger
    if (f->ptr_id == g_state.gesture_main_ptr_id) {
        g_state.gesture_main_ptr_id = -1;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = false;
        g_state.gesture_double_tap_waiting = false;
        gesture_clear_deferred_tap();
        gesture_clear_pending_long_press();
        if (!g_state.gesture_second_active)
            g_state.gesture_deferred_second_finger_tap = false;
    } else if (f->ptr_id == g_state.gesture_second_ptr_id) {
        gesture_clear_second_finger_globals();
        gesture_clear_second_finger_state();
    }
    // Clear scroll mode if active
    scroll_mode_exit(f, &cancel_result);
    reset_unused_toggle_gesture_timers();
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
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
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
                g_state.sim_click_release_time = time_ms + CLICK_DELAY_MS;
            }
        }
        g_state.sim_click_press_time = 0;
        g_state.sim_click_ptr_id = -1;
    }

    if (g_state.sim_click_release_time > 0 && time_ms >= g_state.sim_click_release_time) {
        if (g_state.sim_continue_click)
            add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        g_state.sim_click_release_time = 0;
        g_state.sim_continue_click = false;
        g_state.sim_click_ptr_id = -1;
    }
}

static inline void process_auto_repeat_burst(TouchActionResult* restrict result, TouchBinding* actions, int count, uint64_t* last_time, uint64_t time_ms) {
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

static void process_non_toggle_press_release(TouchElement* e, bool inside, uint64_t time_ms, TouchActionResult* restrict result) {
    if (!inside) {
        if (e->auto_repeat_primary_pressed && e->bindings[0].type != BINDING_NONE) {
            release_binding(result, &e->bindings[0]);
            e->auto_repeat_primary_pressed = false;
        }
    } else {
        int interval_ms = e->cached_auto_repeat_interval;
        if (e->auto_repeat_last_time == 0 || time_ms - e->auto_repeat_last_time >= (uint64_t)interval_ms) {
            e->auto_repeat_primary_pressed = !e->auto_repeat_primary_pressed;
            if (e->bindings[0].type != BINDING_NONE) {
                if (e->auto_repeat_primary_pressed)
                    press_binding(result, &e->bindings[0], true);
                else
                    release_binding(result, &e->bindings[0]);
            }
            e->auto_repeat_last_time = time_ms;
        }
    }
}

static void process_button_auto_repeat(TouchElement* e, int finger_idx, uint64_t time_ms, TouchActionResult* restrict result) {
    if (!(g_state.cfg.caps_has_auto_repeat_buttons || e->cached_has_auto_repeat)) return;
    bool tog_active = element_is_toggle_active(e);
    bool guard = e->cached_has_auto_repeat && (e->cached_has_toggle ? tog_active : (e->current_ptr_id >= 0 && e->engaged));
    if (!guard) return;
    if (e->cached_has_toggle && tog_active) {
        int interval_ms = e->cached_auto_repeat_interval;
        if (e->auto_repeat_last_time == 0 || time_ms - e->auto_repeat_last_time >= (uint64_t)interval_ms) {
            e->auto_repeat_primary_pressed = !e->auto_repeat_primary_pressed;
            for (int k = 0; k < 4; k++) {
                TouchBinding* tb = &e->bindings[k];
                if (tb->type == BINDING_NONE) continue;
                if (!tb->auto_repeat) continue;
                if (e->auto_repeat_primary_pressed)
                    press_binding(result, tb, true);
                else
                    release_binding(result, tb);
            }
            e->auto_repeat_last_time = time_ms;
        }
    } else {
        bool inside = point_in_element(e->visual_x, e->visual_y, e);
        process_non_toggle_press_release(e, inside, time_ms, result);
    }
}

static void process_button_long_press(TouchElement* e, int finger_idx, uint64_t time_ms, TouchActionResult* restrict result) {
    if (!g_state.cfg.caps_has_any_element_long_press) return;
    TouchFinger* _lp_f = e->current_ptr_id >= 0 ? find_finger(e->current_ptr_id) : NULL;
    if (__builtin_expect(e->current_ptr_id >= 0, 0) && e->long_press_arm && !e->gesture_long_press_triggered
        && e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE
        && !finger_gesture_active(e))
    {
        uint64_t elapsed = time_ms - e->down_time_ms;
        if (elapsed >= g_state.cfg.long_press_delay_ms) {
            e->gesture_long_press_triggered = true;
            if (_lp_f) _lp_f->gesture_activated_in_touch = true;
            vis_set(e, VF_LONG_TAP);
            update_visual_layers(e);
            mark_element_dirty(e);
            e->long_press_arm = false;
            toggle_alternate_bindings(e, &e->lp_toggled, e->cached_lp_has_toggle,
                e->element_long_press, e->element_long_press_count,
                e->bindings[0].type != BINDING_NONE, result);
            if (e->button_long_press_haptic > 0 && e->element_long_press_count > 0)
                add_action(result, ACT_HAPTIC, e->button_long_press_haptic, 0, 0);
        }
    }
}

static void process_button_gesture_timer(TouchElement* e, int finger_idx, uint64_t time_ms, TouchActionResult* restrict result) {
    if (!g_state.cfg.caps_has_any_element_gesture) return;
    TouchFinger* _gt_f = e->current_ptr_id >= 0 ? find_finger(e->current_ptr_id) : NULL;
    if (__builtin_expect(e->current_ptr_id >= 0, 0) && e->gesture_timer_armed && !e->gesture_swipe_triggered
        && !e->gesture_long_press_triggered
        && e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE
        && !finger_gesture_active(e))
    {
        if (time_ms - e->down_time_ms >= GESTURE_TIMER_MS) {
            e->gesture_swipe_triggered = true;
            vis_set(e, VF_GESTURE);
            if (_gt_f) _gt_f->gesture_activated_in_touch = true;
            update_visual_layers(e);
            mark_element_dirty(e);
            e->gesture_timer_armed = false;
            toggle_alternate_bindings(e, &e->gesture_toggled, e->cached_gesture_has_toggle,
                e->element_gesture, e->element_gesture_count,
                e->bindings[0].type != BINDING_NONE, result);
            if (e->button_gesture_haptic > 0)
                add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
        }
    }
}

static void tick_begin(struct timespec* ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static void tick_end(struct timespec* start, int action_count) {
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t elapsed = (end.tv_sec - start->tv_sec) * 1000000
                     + (end.tv_nsec - start->tv_nsec) / 1000;
    if (__builtin_expect(elapsed > TICK_BUDGET_US, 0)) {
        tp_diag_slow_ticks++;
        if (elapsed > 100000) {
        }
    }
    tp_diag_total_actions += action_count;
    if (action_count > HIGH_ACTION_COUNT_THRESHOLD) {
    }
}

static void process_range_button_tick(uint64_t time_ms, TouchActionResult* restrict result) {
    for (int _ri = 0; _ri < g_state.range_count; _ri++) {
        TouchElement* e = &g_state.elements[g_state.range_indices[_ri]];
        int kc = range_keycode(e->range_ordinal, e->range_index);
        int _ridx = (int)(e - g_state.elements);

        if (__builtin_expect(e->current_ptr_id >= 0, 0) && !e->range_hold_pressed && !e->range_scrolling && e->range_has_binding) {
            if (time_ms - e->down_time_ms >= RANGE_TAP_TIMEOUT_MS) {
                if (kc > 0) {
                    add_action(result, ACT_KEY_PRESS, kc, 1, 0);
                    e->range_hold_pressed = true;
                }
            }
        }

        if (e->range_pending_tap_release && time_ms >= e->range_tap_release_time) {
            if (kc > 0)
                add_action(result, ACT_KEY_RELEASE, kc, 0, 0);
            e->range_pending_tap_release = false;
            e->range_scrolling = false;
            e->range_has_binding = false;
            e->range_hold_pressed = false;
        }
    }
}

TouchActionResult touch_processor_tick(uint64_t time_ms) {
    TouchActionResult result; result.count = 0;

    tp_diag_tick_count++;
    if (tp_diag_last_tick_ms != 0 && time_ms - tp_diag_last_tick_ms > TICK_GAP_WARNING_MS) {
        tp_diag_gap_ticks++;
    }
    tp_diag_last_tick_ms = time_ms;

    if (tp_diag_last_status_ms == 0) tp_diag_last_status_ms = time_ms;
    if (time_ms - tp_diag_last_status_ms >= 5000) {
        tp_diag_last_status_ms = time_ms;
    }

    if (__builtin_expect(g_state.last_tick_time != 0 && time_ms - g_state.last_tick_time > TICK_GAP_WARNING_MS, 0)) {
        }
    g_state.last_tick_time = time_ms;

    struct timespec tick_start;
    tick_begin(&tick_start);

    process_scheduled_actions(&result, time_ms);

    // Gesture-level timeouts (long-press, double-tap, single-tap-hold, second-finger double-tap)
    if (current_mode_has_gestures() || g_state.gesture_double_tap_waiting || g_state.second_double_tap_waiting)
        gesture_tick(time_ms, &result);

    // Auto-repeat + long-press + gesture timer: button elements only
    int button_count = g_state.button_count;
    TouchElement* elements = g_state.elements;
    int* button_indices = g_state.button_indices;
    for (int _bi = 0; _bi < button_count; _bi++) {
        TouchElement* e = &elements[button_indices[_bi]];

        if (__builtin_expect(e->engaged && e->current_ptr_id < 0, 0)) {
            e->engaged = false;
            mark_element_dirty(e);
        }

        process_button_auto_repeat(e, _bi, time_ms, &result);
        process_button_long_press(e, _bi, time_ms, &result);
        process_button_gesture_timer(e, _bi, time_ms, &result);

        update_visual_layers(e);
    }

    // Gesture auto-repeat: burst any binding with auto_repeat flag.
    // This is purely binding-driven, not gesture-type-aware.
    process_auto_repeat_burst(&result, g_state.gesture_toggled_actions, g_state.gesture_toggled_count, g_state.gesture_auto_repeat_last_time, time_ms);
    if (__builtin_expect(g_state.gesture_is_action_held, 0))
        process_auto_repeat_burst(&result, g_state.gesture_held_actions, g_state.gesture_held_count, g_state.gesture_auto_repeat_last_time_held, time_ms);

    // Range button: hold timer + deferred release
    process_range_button_tick(time_ms, &result);

    process_delayed_actions(&result, time_ms);

    tick_end(&tick_start, result.count);

    if (result.count > 0) {
        }
    return result;
}

// Full cancel: releases ALL held keys, element bindings, and clears all state.
// Dispatches release actions so keys/buttons are properly released in Wine/XServer.
TouchActionResult touch_processor_cancel_all(void) {
    TouchActionResult cancel_result = {0};

    release_held_actions(&cancel_result);

    for (int i = 0; i < g_state.gesture_toggled_count; i++)
        release_binding(&cancel_result, &g_state.gesture_toggled_actions[i]);
    g_state.gesture_toggled_count = 0;
    g_state.gesture_is_action_held = false;
    g_state.gesture_held_count = 0;

    // Release ALL element bindings (engaged, tracked, or toggled elements)
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->current_ptr_id >= 0 || e->engaged
            || e->selected || e->gesture_toggled || e->lp_toggled) {
            release_element_bindings(e, &cancel_result, true);
            force_release_element_toggles(e, &cancel_result);
        }
    }
    // Full reset of ALL element runtime state (visual_flags, gesture_suppressed,
    // stick/trackpad/range/petal, auto_repeat, etc.) so elements start clean on next touch.
    for (int i = 0; i < g_state.element_count; i++) {
        element_reset_runtime(&g_state.elements[i]);
    }

    for (int i = 0; i < MAX_FINGERS; i++) {
        if (g_state.fingers[i].active) {
            scroll_mode_exit(&g_state.fingers[i], &cancel_result);
            deactivate_finger(&g_state.fingers[i]);
        }
    }
    g_state.active_finger_count = 0;
    g_state.free_finger_hint = 0;

    g_state.main_ptr_id = -1;
    g_state.gesture_main_ptr_id = -1;
    g_state.gesture_second_ptr_id = INVALID_PTR_ID;
    g_state.finger_pointer_left = -1;
    g_state.finger_pointer_right = -1;
    g_state.pending_left_release_ptr_id = -1;
    g_state.pending_right_release_ptr_id = -1;
    g_state.pending_left_release_time = 0;
    g_state.pending_right_release_time = 0;
    g_state.sim_click_ptr_id = -1;
    g_state.sim_click_press_time = 0;
    g_state.sim_click_release_time = 0;
    g_state.sim_continue_click = false;
    g_state.passthrough_active = false;
    g_state.scroll_accum_y = 0;
    g_state.pointer_left_enabled = true;
    g_state.pointer_right_enabled = true;

    // Cancel any scheduled actions that haven't fired yet
    for (int i = 0; i < g_state.scheduled_action_count; i++)
        g_state.scheduled_actions[i].active = false;
    g_state.scheduled_action_count = 0;

    gesture_clear_deferred_tap();
    gesture_clear_pending_long_press();
    gesture_clear_second_finger_globals();
    gesture_clear_second_finger_state();

    memset(g_ctx, 0, sizeof(g_ctx));

    for (int i = 0; i < MAX_FINGERS; i++)
        g_state.hovered_element_per_ptr[i] = -1;

    activation_reset();

    return cancel_result;
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
