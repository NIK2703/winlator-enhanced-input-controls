#include <android/log.h>
#define LOG_TAG "Winlator_Button"
#include "../touch_processor_internal.h"

#define TOGGLE_DEBOUNCE_MS 50
#define BINDING_GAMEPAD_L3 (BINDING_GAMEPAD_BASE + 8)
#define BINDING_GAMEPAD_R3 (BINDING_GAMEPAD_BASE + 9)
#define GESTURE_THRESHOLD_FALLBACK 20.0f
#define MOVE_DEADZONE_EPSILON 0.001f

static inline bool has_any_sticky_slot(uint32_t mask) {
    for (int i = 0; i < MAX_BINDINGS_PER_ELEMENT; i++)
        if (mask & (1u << i)) return true;
    return false;
}

static inline int element_index(const TouchElement* e) {
    return (int)(e - g_state.elements);
}

static inline void clear_gesture_defer_state(TouchElement* e, TouchActionResult* restrict result) {
    e->gesture_long_press_triggered = false;
    vis_clear(e, VF_LONG_TAP);
    if (!e->lp_toggled)
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
    update_visual_layers(e);
}

static inline void clear_gesture_and_release_lp(TouchElement* e, TouchActionResult* restrict result) {
    if (e->gesture_long_press_triggered) {
        clear_gesture_defer_state(e, result);
    } else if (e->gesture_swipe_triggered) {
        e->gesture_swipe_triggered = false;
        vis_clear(e, VF_GESTURE);
        if (!e->gesture_toggled)
            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        update_visual_layers(e);
    }
}

// --- DOWN: unified for toggle and non-toggle ---
// Toggle = regular action: press binding, set selected. Re-press = toggle OFF.
void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id; (void)x; (void)y;
    bool has_primary = element_has_primary(e);

    // Toggle ON: re-press = toggle OFF
    if (e->cached_has_toggle && e->selected) {
        if (e->cached_has_auto_repeat) {
            if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS)
                return;
            if (element_has_gesture_toggle(e)) {
                update_visual_layers(e);
                if (e->lp_toggled) e->long_press_arm = true;
                return;
            }
            release_toggled_alternate_bindings(e, result, true);
        } else if (e->cached_has_long_press) {
            e->long_press_arm = true;
            e->defer_primary = true;
            update_visual_layers(e);
            mark_element_dirty(e);
            return;
        } else if (e->activation_mode == ACTIVATION_HOVER) {
            if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS)
                return;
            if (has_primary) release_binding(result, &e->bindings[0]);
            e->selected = false;
            update_visual_layers(e);
            mark_element_dirty(e);
            return;
        } else if (e->activation_mode == ACTIVATION_TRACK) {
            e->auto_repeat_primary_pressed = true;
            return;
        }
        if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS)
            return;
        if (has_primary) release_binding(result, &e->bindings[0]);
        e->selected = false;
        e->auto_repeat_primary_pressed = false;
        e->auto_repeat_last_time = 0;
        update_visual_layers(e);
        mark_element_dirty(e);
        return;
    }

    // Auto-repeat only (no toggle)
    if (e->cached_has_auto_repeat && !e->cached_has_toggle) {
        if (has_primary) {
            press_binding(result, &e->bindings[0], true);
            e->auto_repeat_primary_pressed = true;
        }
        e->auto_repeat_last_time = time_ms;
        return;
    }

    // Toggle+AR first press: press all bindings
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++) {
            TouchBinding* tb = &e->bindings[k];
            if (tb->type == BINDING_NONE) continue;
            press_binding(result, tb, true);
        }
        e->toggle_debounce_last_time = time_ms;
        e->selected = true;
        e->auto_repeat_primary_pressed = true;
        e->auto_repeat_last_time = time_ms;
        update_visual_layers(e);
        return;
    }

    // Defer if competing action (LP/gesture) present
    e->long_press_arm = e->cached_has_long_press;
    e->defer_primary = (e->cached_has_long_press || e->cached_has_gesture);

    if (e->defer_primary) {
        if (!has_primary && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
            && !e->gesture_toggled && !e->lp_toggled) {
            update_visual_layers(e);
        }
        return;
    }

    // Press binding immediately
    if (has_primary) {
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++)
            if (e->bindings[k].type != BINDING_NONE && (e->primary_sticky_mask & (1u << k)))
                release_binding(result, &e->bindings[k]);

        press_binding(result, &e->bindings[0], true);
        // Toggle: mark selected (binding stays held)
        if (e->cached_has_toggle
            && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER || e->activation_mode == ACTIVATION_LOCK)) {
            e->auto_repeat_primary_pressed = true;
            if (e->activation_mode == ACTIVATION_HOVER || e->activation_mode == ACTIVATION_LOCK) {
                e->selected = true;
                e->toggle_debounce_last_time = time_ms;
            }
        }
        update_visual_layers(e);
    } else if (!e->cached_has_toggle) {
        update_visual_layers(e);
    }
}

static bool handle_gesture_swipe(TouchElement* e, float dx, float dy, bool has_primary, TouchActionResult* restrict result) {
    TouchFinger* gf = find_finger(e->current_ptr_id);
    if (!e->gesture_suppressed && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
        && !finger_gesture_active(e))
    {
        bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        if (has_gesture) {
            TouchProcessorState* s = &g_state;
            float gesture_threshold = s->cfg.gesture_threshold_px > 0 ?
                (float)s->cfg.gesture_threshold_px : GESTURE_THRESHOLD_FALLBACK;
            float dx_sq = dx * dx, dy_sq = dy * dy;
            float dist_sq = dx_sq + dy_sq;
            if (dist_sq > gesture_threshold * gesture_threshold) {
                e->gesture_swipe_triggered = true;
                vis_set(e, VF_GESTURE);
                if (gf) gf->gesture_activated_in_touch = true;
                e->long_press_arm = false;
                e->gesture_long_press_triggered = false;
                if (e->button_gesture_haptic > 0)
                    add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                toggle_alternate_bindings(e, &e->gesture_toggled, e->cached_gesture_has_toggle,
                    e->element_gesture, e->element_gesture_count, has_primary, result);
                update_visual_layers(e);
                return true;
            }
        }
    }
    return false;
}

// --- MOVE: unified ---
void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)time_ms;
    bool has_primary = element_has_primary(e);
    float dx = x - e->down_x;
    float dy = y - e->down_y;
    bool inside = point_in_element(x, y, e);
    if (fabsf(dx) < MOVE_DEADZONE_EPSILON && fabsf(dy) < MOVE_DEADZONE_EPSILON) return;

    if (handle_gesture_swipe(e, dx, dy, has_primary, result)) return;

    // Toggle+AR: latch visual to selected state
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        if (e->selected) vis_set(e, VF_TAP); else vis_clear(e, VF_TAP);
        update_visual_layers(e);
        return;
    }

    // Toggle in TRACK/HOVER: slide-over in/out manages press/release
    if (e->cached_has_toggle && !e->cached_has_auto_repeat
        && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)) {
        if (e->auto_repeat_primary_pressed) {
            update_visual_layers(e);
            return;
        }
        if (inside) {
            if (!e->selected) {
                if (!e->defer_primary) {
                    if (has_primary) press_binding(result, &e->bindings[0], true);
                    e->selected = true;
                    update_visual_layers(e);
                    e->gesture_timer_armed = true;
                    mark_element_dirty(e);
                } else {
                    update_visual_layers(e);
                }
                return;
            }
            if (element_has_gesture_toggle(e)) update_visual_layers(e);
            return;
        }
        if (e->selected) {
            if (!element_has_gesture_toggle(e)) {
                if (has_primary) release_binding(result, &e->bindings[0]);
                e->selected = false;
                update_visual_layers(e);
                mark_element_dirty(e);
            }
            return;
        }
        update_visual_layers(e);
        return;
    }

    // LOCK-mode toggle: no finger tracking, just visual
    if (e->cached_has_toggle && !e->cached_has_auto_repeat) {
        if (!element_is_toggle_active(e) && !e->long_press_arm && !e->gesture_timer_armed)
            update_visual_layers(e);
        return;
    }

    // Auto-repeat (non-toggle): release/re-press on boundary
    if (e->cached_has_auto_repeat && !e->cached_has_toggle) {
        button_auto_repeat_move(e, inside, time_ms, result);
        if (!inside) update_visual_layers(e);
        return;
    }

    if (e->long_press_arm && !inside) e->long_press_arm = false;
    if (e->gesture_timer_armed && !inside) e->gesture_timer_armed = false;

    if (!has_primary && !e->cached_has_toggle
        && !element_has_any_gesture_activity(e)
        && !e->gesture_timer_armed) {
        update_visual_layers(e);
    }
}

// --- UP: unified ---
// Toggle: skip release (binding stays held). Non-toggle: release binding.
void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    bool has_primary = element_has_primary(e);

    // Toggle+AR: keep repeating if selected
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        if (e->selected) {
            e->defer_primary = false;
            update_visual_layers(e);
            mark_element_dirty(e);
            return;
        }
    }

    // L3/R3 debounce (non-toggle)
    if (!e->cached_has_toggle) {
        int bt0 = e->bindings[0].type;
        if (bt0 == BINDING_GAMEPAD_L3 || bt0 == BINDING_GAMEPAD_R3) {
            bool was = e->selected;
            e->selected = (time_ms - e->down_time_ms) > BUTTON_MIN_KEEP_PRESSED_MS;
            if (was != e->selected) update_visual_layers(e);
        }
    }

    // Deferred path (LP/gesture present, binding was NOT pressed on DOWN)
    if (e->defer_primary) {
        if (e->gesture_long_press_triggered || e->gesture_swipe_triggered) {
            clear_gesture_and_release_lp(e, result);
            e->defer_primary = false;
            e->gesture_timer_armed = false;
            goto cleanup;
        }

        // Deferred toggle: flip on qualifying tap
        if (e->cached_has_toggle && !e->cached_has_auto_repeat) {
            bool qualifies = true;
            if (e->cached_has_long_press) {
                uint64_t elapsed = time_ms - e->down_time_ms;
                int lp_delay = g_state.cfg.long_press_delay_ms;
                if (lp_delay > 0 && elapsed >= (uint64_t)lp_delay) qualifies = false;
            }
            if (qualifies && e->cached_has_gesture) {
                float ddx = x - e->down_x, ddy = y - e->down_y;
                float th = g_state.cfg.gesture_threshold_px > 0
                    ? (float)g_state.cfg.gesture_threshold_px : GESTURE_THRESHOLD_FALLBACK;
                if (ddx * ddx + ddy * ddy > th * th) qualifies = false;
            }
            if (qualifies) {
                if (has_primary) e->selected = !e->selected;
                if (e->selected) {
                    if (has_primary) press_binding(result, &e->bindings[0], true);
                } else {
                    if (has_primary) release_binding(result, &e->bindings[0]);
                }
            }
            e->defer_primary = false;
            mark_element_dirty(e);
            goto cleanup;
        }

        // Deferred non-toggle: tap (press+release)
        if (has_primary && !has_any_sticky_slot(e->primary_sticky_mask)) {
            press_binding(result, &e->bindings[0], true);
            release_binding(result, &e->bindings[0]);
        }
        e->defer_primary = false;
        goto cleanup;
    }

    // Non-deferred toggle UP: activation mode
    if (e->cached_has_toggle && !e->cached_has_auto_repeat && has_primary) {
        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER) {
            if (e->auto_repeat_primary_pressed) {
                e->auto_repeat_primary_pressed = false;
                if (e->activation_mode != ACTIVATION_HOVER) {
                    if (e->selected && element_has_gesture_toggle(e)) goto cleanup;
                    if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS)
                        goto cleanup;
                    e->selected = !e->selected;
                    if (e->selected) {
                        suppress_element_gestures(e, result);
                        update_visual_layers(e);
                    } else {
                        if (has_primary) release_binding(result, &e->bindings[0]);
                        e->selected = false;
                        update_visual_layers(e);
                        mark_element_dirty(e);
                    }
                }
            } else if (e->activation_mode != ACTIVATION_HOVER) {
                if (e->selected && !element_has_gesture_toggle(e)) {
                    if (has_primary) release_binding(result, &e->bindings[0]);
                    e->selected = false;
                    update_visual_layers(e);
                    mark_element_dirty(e);
                }
            }
        } else {
            if (e->selected) suppress_element_gestures(e, result);
        }
        e->defer_primary = false;
        goto cleanup;
    }

    // Auto-repeat non-toggle: release
    if (e->cached_has_auto_repeat) {
        if (e->auto_repeat_primary_pressed && has_primary)
            release_binding(result, &e->bindings[0]);
        e->auto_repeat_primary_pressed = false;
        e->defer_primary = false;
        e->gesture_timer_armed = false;
        goto cleanup;
    }

    // Default: release binding (skip for toggle — binding stays held)
    if (e->gesture_long_press_triggered || e->gesture_swipe_triggered) {
        clear_gesture_and_release_lp(e, result);
    } else {
        if (has_primary && !has_any_sticky_slot(e->primary_sticky_mask))
            release_binding(result, &e->bindings[0]);
    }

cleanup:
    e->gesture_timer_armed = false;
    e->auto_repeat_primary_pressed = false;
}
