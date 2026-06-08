#include <android/log.h>
#define LOG_TAG "Winlator_Button"
#include "../touch_processor_internal.h"

void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)x; (void)y; (void)time_ms;
    TouchProcessorState* s = &g_state;
    bool has_primary = e->bindings[0].type != BINDING_NONE;
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "DOWN ptr=%d b0=%d TOGGLE_SWITCH=%d auto=%d arm=%d lp_cnt=%d delay=%d",
        ptr_id, e->bindings[0].type, e->toggle_switch, e->auto_repeat,
        e->long_press_arm, e->element_long_press_count, s->cfg.long_press_delay_ms);

    // Toggle + Auto-repeat mode: burst mode — press+release all bindings at interval
    if (e->toggle_switch && e->auto_repeat) {
        if (e->selected) {
            e->auto_repeat_primary_pressed = false;
            e->selected = false;
            e->visual_active = false;
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] visual=0 (toggle_auto_deselect)", (int)(e - g_state.elements));
        } else {
            execute_actions(result, e->bindings, 4);
            e->selected = true;
            e->auto_repeat_last_time = time_ms;
        }
        return;
    }

    // Normal toggle switch (non-auto-repeat)
    if (e->toggle_switch && e->selected) {
        // TRACK/HOVER: keep toggle active on DOWN, let MOVE handler deactivate when finger leaves
        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER) {
            return;
        }
        // LOCK mode: release binding and keep selected for UP flip
        if (has_primary)
            release_binding(result, &e->bindings[0]);
        return;
    }

    // Auto-repeat (non-toggle): press primary immediately
    if (e->auto_repeat) {
        if (has_primary) {
            press_binding(result, &e->bindings[0], true);
            e->auto_repeat_primary_pressed = true;
        }
        e->auto_repeat_last_time = time_ms;
        return;
    }

    bool has_lp = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
    bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
    bool arm_lp = has_lp && !e->toggle_switch;
    bool defer_primary = (has_lp || has_gesture) && !e->toggle_switch;
    e->long_press_arm = arm_lp;
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN arm_lp=%d has_lp=%d has_gesture=%d defer=%d tog=%d lp_cnt=%d lp0=%d",
        arm_lp, has_lp, has_gesture, defer_primary, e->toggle_switch, e->element_long_press_count,
        e->element_long_press[0].type);

    if (defer_primary) {
        if (!has_primary) {
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
        if (e->toggle_switch && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)) {
            e->selected = true;
            e->auto_repeat_primary_pressed = true;
        }
    } else if (!e->toggle_switch) {
        e->visual_active = false;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] type=%d visual=0 (no_primary)", (int)(e - g_state.elements), e->bindings[0].type);
    }
}

void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
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
    if (!e->gesture_suppressed && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered) {
        bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        if (has_gesture) {
            float gesture_threshold = s->cfg.gesture_threshold_px > 0 ?
                (float)s->cfg.gesture_threshold_px : 20.0f;
            float dx_sq = dx * dx, dy_sq = dy * dy;
            float dist_sq = dx_sq + dy_sq;
            if (dist_sq > gesture_threshold * gesture_threshold) {
                e->gesture_swipe_triggered = true;
                e->long_press_arm = false;
                e->gesture_long_press_triggered = false;
                // Java: does NOT release primary binding on gesture trigger
                if (e->button_gesture_haptic > 0)
                    add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                press_bindings_list(result, e->element_gesture, e->element_gesture_count);
                return;
            }
        }
    }

    // Auto-repeat toggle: latched state (visual = selected)
    if (e->toggle_switch && e->auto_repeat) {
        e->visual_active = e->selected;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=%d (toggle_auto_latch)", (int)(e - g_state.elements), e->bindings[0].type, e->selected);
        return;
    }

    // Non-auto-repeat toggle in TRACK/HOVER mode: deactivate when finger leaves element.
    if (e->toggle_switch && !e->auto_repeat && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)) {
        if (e->selected) {
            if (!inside && !e->auto_repeat_primary_pressed) {
                if (has_primary)
                    release_binding(result, &e->bindings[0]);
                e->selected = false;
                e->visual_active = false;
                TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (toggle_track_left)", (int)(e - g_state.elements), e->bindings[0].type);
            } else {
                e->visual_active = true;
                TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_track_inside_sel)", (int)(e - g_state.elements), e->bindings[0].type);
            }
        } else {
            if (inside) {
                if (has_primary)
                    press_binding(result, &e->bindings[0], true);
                e->selected = true;
                e->visual_active = true;
                TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_reenter)", (int)(e - g_state.elements), e->bindings[0].type);
            } else {
                e->visual_active = false;
                TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (toggle_track_outside_notsel)", (int)(e - g_state.elements), e->bindings[0].type);
            }
        }
        return;
    }

    // Auto-repeat: release binding if finger leaves element, press again if re-enters
    // (Skip for toggle+auto-repeat mode — state is latched, not finger-dependent)
    if (e->auto_repeat && !e->toggle_switch) {
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
        && !e->toggle_switch
        && !e->gesture_swipe_triggered
        && !e->gesture_long_press_triggered
        && !e->gesture_timer_armed) {
        e->visual_active = false;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (none_primary)", (int)(e - g_state.elements), e->bindings[0].type);
    }

    // HOVER mode: non-toggle buttons are only visually active when finger is inside.
    // Without this, handle_element_move (shared.c:194) keeps visual_active=true for ALL
    // engaged elements, causing previously-hovered buttons to stay highlighted.
    // Gesture flags are NOT checked here — the first_btn_has_gesture save/restore
    // mechanism in handle_gesture_move can toggle them, and a gesture trigger from an
    // earlier move should not keep a visually non-hovered button illuminated.
    if (e->activation_mode == ACTIVATION_HOVER && !e->toggle_switch && !inside) {
        e->visual_active = false;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (hover_outside)", (int)(e - g_state.elements), e->bindings[0].type);
    }
}

void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    TouchProcessorState* s = &g_state;
    bool has_primary = e->bindings[0].type != BINDING_NONE;
    // Java ControlElement.handleTouchUp: debounce for non-toggle L3/R3 bindings
    // isKeepButtonPressedAfterMinTime() = !toggleSwitch && (binding == GAMEPAD_BUTTON_L3 || GAMEPAD_BUTTON_R3)
    if (!e->toggle_switch) {
        int bt0 = e->bindings[0].type;
        if (bt0 == BINDING_GAMEPAD_BASE + 8 || bt0 == BINDING_GAMEPAD_BASE + 9)
            e->selected = (time_ms - e->down_time_ms) > BUTTON_MIN_KEEP_PRESSED_MS;
    }
    // Toggle + Auto-repeat: keep repeating after finger-up, don't flip selected
    if (e->toggle_switch && e->auto_repeat) {
        if (e->selected) {
            return;
        }
    }

    // Normal toggle switch (non-auto-repeat)
    if (e->toggle_switch && !e->auto_repeat && has_primary) {
        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER) {
            if (e->selected) {
                bool inside = point_in_element(x, y, e);
                bool was_fresh = e->auto_repeat_primary_pressed;
                e->auto_repeat_primary_pressed = false;
                if (has_primary) {
                    release_binding(result, &e->bindings[0]);
                }
                e->selected = false;
                e->visual_active = false;
                TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_up[%d] type=%d visual=0 (toggle_deselect)", (int)(e - g_state.elements), e->bindings[0].type);
            } else {
                e->auto_repeat_primary_pressed = false;
            }
            return;
        }
        e->selected = !e->selected;
        if (e->selected) {
            return;
        }
    }

    // Auto-repeat (non-toggle): release primary binding, no long-press/gesture processing
    if (e->auto_repeat) {
        if (e->auto_repeat_primary_pressed && has_primary)
            release_binding(result, &e->bindings[0]);
        e->auto_repeat_primary_pressed = false;
        goto cleanup;
    }

    //TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "UP lp_trig=%d swipe_trig=%d has_lp=%d b0=%d elapsed=%llu",
    //    e->gesture_long_press_triggered, e->gesture_swipe_triggered,
    //    e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE,
    //    e->bindings[0].type, (unsigned long long)(time_ms - e->down_time_ms));

    if (e->gesture_long_press_triggered) {
        e->visual_long_press_active = false;
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
        e->gesture_long_press_triggered = false;
    } else if (e->gesture_swipe_triggered) {
        release_bindings_list(result, e->element_gesture, e->element_gesture_count);
        e->gesture_swipe_triggered = false;
    } else {
        bool has_lp = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
        bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        bool defer_primary = (has_lp || has_gesture) && !e->toggle_switch;

        if (defer_primary) {
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
