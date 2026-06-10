#include <android/log.h>
#define LOG_TAG "Winlator_Button"
#include "../touch_processor_internal.h"

#define TOGGLE_DEBOUNCE_MS 50

void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id; (void)x; (void)y;
    TouchProcessorState* s = &g_state;
    bool has_primary = e->bindings[0].type != BINDING_NONE;
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "DOWN ptr=%d b0=%d TOGGLE_SWITCH=%d auto=%d arm=%d lp_cnt=%d delay=%d",
        ptr_id, e->bindings[0].type, e->cached_has_toggle, e->cached_has_auto_repeat,
        e->long_press_arm, e->element_long_press_count, s->cfg.long_press_delay_ms);

    // Toggle + Auto-repeat mode: burst mode — press+release all bindings at interval
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "DOWN toggle+AR ptr=%d selected=%d b0.type=%d b0.tog=%d b0.ar=%d b0.int=%d",
            ptr_id, e->selected, e->bindings[0].type,
            e->bindings[0].toggle, e->bindings[0].auto_repeat, e->bindings[0].auto_repeat_interval_ms);
        if (e->selected) {
            // Debounce: prevent rapid toggle flip
            if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS) {
                return;
            }
            e->toggle_debounce_last_time = time_ms;
            // Release stale gesture/long-press toggles that are no longer active.
            // gesture_swipe_triggered/long_press_triggered are cleared on finger-up,
            // but gesture_toggled/lp_toggled survive because element_is_toggle_active
            // returns true (selected=true), preventing handle_element_down from clearing them.
            if (e->gesture_toggled && !e->gesture_swipe_triggered) {
                release_bindings_list(result, e->element_gesture, e->element_gesture_count);
                e->gesture_toggled = false;
            }
            if (e->lp_toggled && !e->gesture_long_press_triggered) {
                release_bindings_list(result, e->element_long_press, e->element_long_press_count);
                e->lp_toggled = false;
            }
            // Don't deselect if another toggle is still active
            if (e->lp_toggled || e->gesture_toggled) {
                e->visual_active = true;
                return;
            }
            // Release all bindings on deselect. Element-level toggle+AR never
            // uses gesture_toggled_actions/gesture_held_actions (select path
            // calls press_binding directly, bypassing execute_actions), so
            // no cleanup of those arrays is needed.
            for (int k = 0; k < 4; k++) {
                TouchBinding* tb = &e->bindings[k];
                if (tb->type == BINDING_NONE) continue;
                release_binding(result, tb);
            }
            e->auto_repeat_primary_pressed = false;
            e->selected = false;
            e->visual_active = false;
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] visual=0 (toggle_auto_deselect)", (int)(e - g_state.elements));
        } else {
            // execute_actions skips toggle bindings when gesture_is_down_event is false,
            // so press toggle bindings manually, handle non-toggle ones inline
            for (int k = 0; k < 4; k++) {
                TouchBinding* tb = &e->bindings[k];
                if (tb->type == BINDING_NONE) continue;
                if (tb->toggle || tb->auto_repeat)
                    press_binding(result, tb, true);
                else
                    press_binding(result, tb, false); // tap
            }
            e->toggle_debounce_last_time = time_ms;
            e->selected = true;
            e->auto_repeat_last_time = time_ms;
        }
        return;
    }

    // Normal toggle switch (non-auto-repeat)
    if (e->cached_has_toggle && e->selected) {
        // Don't deselect primary toggle if another toggle is still active
        if (e->lp_toggled || e->gesture_toggled) {
            if (e->lp_toggled)
                e->long_press_arm = true;
            e->visual_active = true;
            return;
        }
        if (e->activation_mode == ACTIVATION_HOVER) {
            // HOVER mode: toggle ON from previous touch — switch OFF on re-entry
            if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS) {
                return;
            }
            e->toggle_debounce_last_time = time_ms;
            if (has_primary)
                release_binding(result, &e->bindings[0]);
            e->selected = false;
            e->visual_active = false;
            return;
        }
        if (e->activation_mode == ACTIVATION_TRACK) {
            // TRACK mode: set auto_repeat flag so MOVE doesn't release
            e->auto_repeat_primary_pressed = true;
            return;
        }
        // LOCK mode: release binding and flip selected immediately on DOWN
        if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS) {
            return;
        }
        e->toggle_debounce_last_time = time_ms;
        e->selected = false;
        e->visual_active = false;
        if (has_primary)
            release_binding(result, &e->bindings[0]);
        return;
    }

    // Auto-repeat (non-toggle): press primary immediately
    if (e->cached_has_auto_repeat) {
        if (has_primary) {
            press_binding(result, &e->bindings[0], true);
            e->auto_repeat_primary_pressed = true;
        }
        e->auto_repeat_last_time = time_ms;
        return;
    }

    bool arm_lp = e->cached_has_long_press;
    e->defer_primary = (e->cached_has_long_press || e->cached_has_gesture) && !e->cached_has_toggle;
    e->long_press_arm = arm_lp;
    bool defer_primary = e->defer_primary;
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN arm_lp=%d has_lp=%d has_gesture=%d defer=%d tog=%d lp_cnt=%d lp0=%d",
        arm_lp, e->cached_has_long_press, e->cached_has_gesture, defer_primary, e->cached_has_toggle, e->element_long_press_count,
        e->element_long_press[0].type);

    if (defer_primary) {
        if (!has_primary && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
            && !e->gesture_toggled && !e->lp_toggled) {
            e->visual_active = false;
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] type=%d visual=0 (defer_primary)", (int)(e - g_state.elements), e->bindings[0].type);
        }
        return;
    }

    if (has_primary) {
        // Release previously sticky-held bindings before re-pressing
        for (int k = 0; k < 4; k++)
            if (e->bindings[k].type != BINDING_NONE && (e->primary_sticky_mask & (1 << k)))
                release_binding(result, &e->bindings[k]);

        press_binding(result, &e->bindings[0], true);
        if (e->cached_has_toggle && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER || e->activation_mode == ACTIVATION_LOCK)) {
            e->auto_repeat_primary_pressed = true;
            if (e->activation_mode == ACTIVATION_HOVER || e->activation_mode == ACTIVATION_LOCK) {
                e->selected = true;
                e->toggle_debounce_last_time = time_ms;
            }
        }
    } else if (!e->cached_has_toggle) {
        e->visual_active = false;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] type=%d visual=0 (no_primary)", (int)(e - g_state.elements), e->bindings[0].type);
    }
}

void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)time_ms;
    TouchProcessorState* s = &g_state;
    bool has_primary = e->bindings[0].type != BINDING_NONE;
    float dx = x - e->down_x;
    float dy = y - e->down_y;
    if (dx == 0.0f && dy == 0.0f) return;
    bool inside = point_in_element(x, y, e);
    //TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "MOVE arm=%d lp_trig=%d inside=%d b0=%d",
    //    e->long_press_arm, e->gesture_long_press_triggered, inside, e->bindings[0].type);

    // Java: gesture binding check — hasGestureBinding() && !gestureTriggered && !longPressTriggered
    // Also skip if any gesture/long-press already fired on this finger.
    TouchFinger* _gf = find_finger(e->current_ptr_id);
    if (!e->gesture_suppressed && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
        && (_gf == NULL || !_gf->gesture_activated_in_touch))
    {
        bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        if (has_gesture) {
            float gesture_threshold = s->cfg.gesture_threshold_px > 0 ?
                (float)s->cfg.gesture_threshold_px : 20.0f;
            float dx_sq = dx * dx, dy_sq = dy * dy;
            float dist_sq = dx_sq + dy_sq;
            if (dist_sq > gesture_threshold * gesture_threshold) {
                e->gesture_swipe_triggered = true;
                if (_gf) _gf->gesture_activated_in_touch = true;
                e->long_press_arm = false;
                e->gesture_long_press_triggered = false;
                if (e->button_gesture_haptic > 0)
                    add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                if (e->gesture_toggled) {
                    release_bindings_list(result, e->element_gesture, e->element_gesture_count);
                    e->gesture_toggled = false;
                    if (e->defer_primary && has_primary)
                        release_binding(result, &e->bindings[0]);
                } else {
                    press_bindings_list(result, e->element_gesture, e->element_gesture_count);
                    for (int k = 0; k < e->element_gesture_count; k++)
                        if (e->element_gesture[k].toggle) { e->gesture_toggled = true; break; }
                    if (e->defer_primary && has_primary)
                        press_binding(result, &e->bindings[0], true);
                }
                return;
            }
        }
    }

    // Auto-repeat toggle: latched state (visual = selected)
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        e->visual_active = e->selected;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=%d (toggle_auto_latch)", (int)(e - g_state.elements), e->bindings[0].type, e->selected);
        return;
    }

    // Non-auto-repeat toggle in TRACK/HOVER mode
    if (e->cached_has_toggle && !e->cached_has_auto_repeat && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)) {
        if (e->auto_repeat_primary_pressed) {
            // Toggle was activated by finger DOWN — binding is held, visual always ON during press
            e->visual_active = true;
        } else {
            // Toggle was activated by MOVE entry (slide-over)
            if (inside) {
                if (!e->selected) {
                    if (has_primary)
                        press_binding(result, &e->bindings[0], true);
                    e->selected = true;
                    e->visual_active = true;
                    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_slideover_in)", (int)(e - g_state.elements), e->bindings[0].type);
                } else {
                    // Already selected — keep visual_active if another toggle is active
                    if (e->lp_toggled || e->gesture_toggled) {
                        e->visual_active = true;
                        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_already_other_active)", (int)(e - g_state.elements), e->bindings[0].type);
                    }
                }
            } else {
                if (e->selected) {
                    // Don't deselect via slide-over leave if another toggle is still active
                    if (e->lp_toggled || e->gesture_toggled) {
                        e->visual_active = true;
                        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_blocked_other_active)", (int)(e - g_state.elements), e->bindings[0].type);
                    } else {
                        if (has_primary)
                            release_binding(result, &e->bindings[0]);
                        e->selected = false;
                        e->visual_active = false;
                        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (toggle_slideover_leave)", (int)(e - g_state.elements), e->bindings[0].type);
                    }
                } else {
                    e->visual_active = false;
                    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (toggle_track_outside_notsel)", (int)(e - g_state.elements), e->bindings[0].type);
                }
            }
        }
        return;
    }

    // Auto-repeat: release binding if finger leaves element, press again if re-enters
    // (Skip for toggle+auto-repeat mode — state is latched, not finger-dependent)
    if (e->cached_has_auto_repeat && !e->cached_has_toggle) {
        if (!inside && e->auto_repeat_primary_pressed) {
            if (has_primary)
                release_binding(result, &e->bindings[0]);
            e->auto_repeat_primary_pressed = false;
        } else if (inside && !e->auto_repeat_primary_pressed) {
            if (has_primary) {
                press_binding(result, &e->bindings[0], true);
                e->auto_repeat_primary_pressed = true;
            }
            e->auto_repeat_last_time = time_ms;
        }
        if (!inside) {
            e->visual_active = false;
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (auto_repeat_left)", (int)(e - g_state.elements), e->bindings[0].type);
        }
        return;
    }

    if (e->long_press_arm && !inside) {
        e->long_press_arm = false;
    }
    if (e->gesture_timer_armed && !inside) {
        e->gesture_timer_armed = false;
    }

    // Suppress visual for NONE-binding buttons (no single-tap action) when no gesture is active.
    // Matches element_button_down which sets visual_active = false for the same condition.
    if (!has_primary
        && !e->cached_has_toggle
        && !e->gesture_swipe_triggered
        && !e->gesture_long_press_triggered
        && !e->gesture_toggled
        && !e->lp_toggled
        && !e->gesture_timer_armed) {
        e->visual_active = false;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (none_primary)", (int)(e - g_state.elements), e->bindings[0].type);
    }

    // HOVER mode: non-toggle buttons are only visually active when finger is inside
    // (unless gesture/LP is active — see concept: button without primary transfers
    // visual activation to gesture/LP firing).
    if (e->activation_mode == ACTIVATION_HOVER && !e->cached_has_toggle && !inside
        && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
        && !e->gesture_toggled && !e->lp_toggled) {
        e->visual_active = false;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (hover_outside)", (int)(e - g_state.elements), e->bindings[0].type);
    }
}

void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    TouchProcessorState* s = &g_state;
    bool has_primary = e->bindings[0].type != BINDING_NONE;
    // Java ControlElement.handleTouchUp: debounce for non-toggle L3/R3 bindings
    // isKeepButtonPressedAfterMinTime() = !toggleSwitch && (binding == GAMEPAD_BUTTON_L3 || GAMEPAD_BUTTON_R3)
    if (!e->cached_has_toggle) {
        int bt0 = e->bindings[0].type;
        if (bt0 == BINDING_GAMEPAD_BASE + 8 || bt0 == BINDING_GAMEPAD_BASE + 9)
            e->selected = (time_ms - e->down_time_ms) > BUTTON_MIN_KEEP_PRESSED_MS;
    }
    // Toggle + Auto-repeat: keep repeating after finger-up, don't flip selected
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        if (e->selected) {
            return;
        }
    }

    // Normal toggle switch (non-auto-repeat)
    if (e->cached_has_toggle && !e->cached_has_auto_repeat && has_primary) {
        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER) {
            if (e->auto_repeat_primary_pressed) {
                e->auto_repeat_primary_pressed = false;
                if (e->activation_mode != ACTIVATION_HOVER) {
                    // TRACK mode: flip toggle state on each UP
                    // Don't deselect if another toggle is still active
                    if (e->selected && (e->lp_toggled || e->gesture_toggled)) {
                        e->visual_active = true;
                        return;
                    }
                    // Debounce: prevent rapid toggle flip
                    if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS) {
                        return;
                    }
                    e->toggle_debounce_last_time = time_ms;
                    e->selected = !e->selected;
                    if (e->selected) {
                        if (e->gesture_swipe_triggered && !e->gesture_toggled)
                            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
                        if (e->gesture_long_press_triggered && !e->lp_toggled)
                            release_bindings_list(result, e->element_long_press, e->element_long_press_count);
                        e->gesture_swipe_triggered = false;
                        e->gesture_long_press_triggered = false;
                        e->gesture_timer_armed = false;
                        return;
                    }
                    if (has_primary)
                        release_binding(result, &e->bindings[0]);
                }
                // HOVER mode: no-op — toggle managed on DOWN/MOVE entry
            } else if (e->activation_mode != ACTIVATION_HOVER) {
                // Slide-over: release on UP (momentary, not persistent)
                if (e->selected) {
                    // Don't deselect if another toggle is still active
                    if (e->lp_toggled || e->gesture_toggled) {
                        e->visual_active = true;
                    } else {
                        if (has_primary)
                            release_binding(result, &e->bindings[0]);
                        e->selected = false;
                        e->visual_active = false;
                    }
                }
            }
            // HOVER slide-over: no-op — toggle managed on MOVE entry
            return;
        }
        // LOCK mode: toggle already flipped on DOWN, UP handles cleanup only
        if (e->selected) {
            // First UP after DOWN1: toggle ON, release stale gesture/LP
            if (e->gesture_swipe_triggered && !e->gesture_toggled)
                release_bindings_list(result, e->element_gesture, e->element_gesture_count);
            if (e->gesture_long_press_triggered && !e->lp_toggled)
                release_bindings_list(result, e->element_long_press, e->element_long_press_count);
            e->gesture_swipe_triggered = false;
            e->gesture_long_press_triggered = false;
            e->gesture_timer_armed = false;
            return;
        }
        // Second UP after DOWN2: toggle already OFF, no-op
        return;
    }

    // Auto-repeat (non-toggle): release primary binding, no long-press/gesture processing
    if (e->cached_has_auto_repeat) {
        if (e->auto_repeat_primary_pressed && has_primary)
            release_binding(result, &e->bindings[0]);
        e->auto_repeat_primary_pressed = false;
        goto cleanup;
    }

    //TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "UP lp_trig=%d swipe_trig=%d has_lp=%d b0=%d elapsed=%llu",
    //    e->gesture_long_press_triggered, e->gesture_swipe_triggered,
    //    e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE,
    //    e->bindings[0].type, (unsigned long long)(time_ms - e->down_time_ms));

    if (__builtin_expect(e->gesture_long_press_triggered, 0)) {
        e->visual_long_press_active = false;
        e->gesture_long_press_triggered = false;
        if (!e->lp_toggled)
            release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        if (e->defer_primary && has_primary && !(e->primary_sticky_mask & 1))
            release_binding(result, &e->bindings[0]);
    } else if (__builtin_expect(e->gesture_swipe_triggered, 0)) {
        e->gesture_swipe_triggered = false;
        if (!e->gesture_toggled)
            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        if (e->defer_primary && has_primary && !(e->primary_sticky_mask & 1))
            release_binding(result, &e->bindings[0]);
    } else {
        if (e->defer_primary) {
            // Primary was NOT pressed on DOWN — press+release as tap on UP
            if (has_primary && !(e->primary_sticky_mask & 1)) {
                press_binding(result, &e->bindings[0], true);
                release_binding(result, &e->bindings[0]);
            }
        } else {
            // Normal button: primary was pressed on DOWN, release on UP
            if (has_primary && !(e->primary_sticky_mask & 1))
                release_binding(result, &e->bindings[0]);
        }
    }

cleanup:
    e->gesture_timer_armed = false;
}
