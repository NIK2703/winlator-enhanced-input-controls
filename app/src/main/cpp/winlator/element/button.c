#include <android/log.h>
#define LOG_TAG "Winlator_Button"
#include "../touch_processor_internal.h"

void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)x; (void)y; (void)time_ms;

    if (e->toggle_switch) {
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN toggle mode=%d sel=%d auto=%d refrain=%d",
            e->activation_mode, e->selected, e->auto_repeat, e->auto_repeat_primary_pressed);
    }

    // Toggle + Auto-repeat mode: toggle on/off (same for all modes — quick-toggle on DOWN)
    if (e->toggle_switch && e->auto_repeat) {
        if (e->selected) {
            if (e->auto_repeat_primary_pressed && e->bindings[0].type != BINDING_NONE)
                release_binding(result, &e->bindings[0]);
            e->auto_repeat_primary_pressed = false;
            e->selected = false;
            e->visual_active = false;
        } else {
            if (e->bindings[0].type != BINDING_NONE) {
                press_binding(result, &e->bindings[0], true);
                e->auto_repeat_primary_pressed = true;
            }
            e->selected = true;
            e->auto_repeat_last_time = 0;
        }
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN auto-repeat toggle: sel=%d vis=%d",
            e->selected, e->visual_active);
        return;
    }

    // Normal toggle switch (non-auto-repeat)
    if (e->toggle_switch && e->selected) {
        // TRACK/HOVER: keep toggle active on DOWN, let MOVE handler deactivate when finger leaves
        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER) {
            __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN TRACK/HOVER active toggle: keep active");
            return;
        }
        // LOCK mode: release binding and keep selected for UP flip
        if (e->bindings[0].type != BINDING_NONE)
            release_binding(result, &e->bindings[0]);
        return;
    }

    // Auto-repeat (non-toggle): press primary immediately
    if (e->auto_repeat) {
        if (e->bindings[0].type != BINDING_NONE) {
            press_binding(result, &e->bindings[0], true);
            e->auto_repeat_primary_pressed = true;
        }
        e->auto_repeat_last_time = 0;
        return;
    }

    bool has_lp = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;

    e->long_press_arm = has_lp && !e->toggle_switch;
    if (has_lp && !e->toggle_switch) {
        return;
    }

    if (e->bindings[0].type != BINDING_NONE) {
        press_binding(result, &e->bindings[0], true);
        if (e->toggle_switch && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)) {
            e->selected = true;
            e->auto_repeat_primary_pressed = true;
            __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN fresh activate toggle: sel=%d refrain=%d",
                e->selected, e->auto_repeat_primary_pressed);
        }
    } else if (!e->toggle_switch) {
        bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        if (has_gesture) {
            e->gesture_timer_armed = true;
        } else {
            e->visual_active = false;
        }
    }
}

void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    float dx = x - e->down_x;
    float dy = y - e->down_y;

    // Java: gesture binding check — hasGestureBinding() && !gestureTriggered && !longPressTriggered
    if (!e->gesture_swipe_triggered && !e->gesture_long_press_triggered) {
        bool has_gesture = e->element_gesture_count > 0 && e->element_gesture[0].type != BINDING_NONE;
        if (has_gesture) {
            float gesture_threshold = g_state.cfg.gesture_threshold_px > 0 ?
                (float)g_state.cfg.gesture_threshold_px : 20.0f;
            float dx_sq = dx * dx, dy_sq = dy * dy;
            float dist_sq = dx_sq + dy_sq;
            if (dist_sq > gesture_threshold * gesture_threshold) {
                e->gesture_swipe_triggered = true;
                e->long_press_arm = false;
                e->gesture_long_press_triggered = false;
                // Java: does NOT release primary binding on gesture trigger
                if (e->button_gesture_haptic > 0)
                    add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                // Press modifier bindings first (held for entire sequence)
                for (int k = 0; k < e->element_gesture_count; k++)
                    if (is_modifier_binding(&e->element_gesture[k]))
                        press_binding(result, &e->element_gesture[k], true);
                for (int k = 0; k < e->element_gesture_count; k++)
                    if (!is_modifier_binding(&e->element_gesture[k]))
                        press_binding(result, &e->element_gesture[k], true);
                return;
            }
        }
    }

    // Auto-repeat toggle: latched state (visual = selected)
    if (e->toggle_switch && e->auto_repeat) {
        e->visual_active = e->selected;
        return;
    }

    // Non-auto-repeat toggle in TRACK/HOVER mode: deactivate when finger leaves element.
    if (e->toggle_switch && !e->auto_repeat && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)) {
        bool inside = point_in_element(x, y, e);
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "MOVE toggle: sel=%d vis=%d in=%d refrain=%d",
            e->selected, e->visual_active, inside, e->auto_repeat_primary_pressed);
        if (e->selected) {
            if (!inside && !e->auto_repeat_primary_pressed) {
                if (e->bindings[0].type != BINDING_NONE)
                    release_binding(result, &e->bindings[0]);
                e->selected = false;
                e->visual_active = false;
            } else {
                e->visual_active = true;
            }
        } else {
            if (inside) {
                if (e->bindings[0].type != BINDING_NONE)
                    press_binding(result, &e->bindings[0], true);
                e->selected = true;
                e->visual_active = true;
            } else {
                e->visual_active = false;
            }
        }
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "MOVE toggle: result sel=%d vis=%d",
            e->selected, e->visual_active);
        return;
    }

    // Auto-repeat: release binding if finger leaves element, press again if re-enters
    // (Skip for toggle+auto-repeat mode — state is latched, not finger-dependent)
    if (e->auto_repeat && !e->toggle_switch) {
        bool inside = point_in_element(x, y, e);
        if (!inside && e->auto_repeat_primary_pressed) {
            if (e->bindings[0].type != BINDING_NONE)
                release_binding(result, &e->bindings[0]);
            e->auto_repeat_primary_pressed = false;
        } else if (inside && !e->auto_repeat_primary_pressed) {
            if (e->bindings[0].type != BINDING_NONE) {
                press_binding(result, &e->bindings[0], true);
                e->auto_repeat_primary_pressed = true;
            }
            e->auto_repeat_last_time = 0;
        }
        if (!inside) {
            e->visual_active = false;
        }
        return;
    }

    if (e->long_press_arm && !point_in_element(x, y, e)) {
        e->long_press_arm = false;
    }
    if (e->gesture_timer_armed && !point_in_element(x, y, e)) {
        e->gesture_timer_armed = false;
    }

    // Suppress visual for NONE-binding buttons (no single-tap action) when no gesture is active.
    // Matches element_button_down which sets visual_active = false for the same condition.
    if (e->bindings[0].type == BINDING_NONE
        && !e->toggle_switch
        && !e->gesture_swipe_triggered
        && !e->gesture_long_press_triggered
        && !e->gesture_timer_armed) {
        e->visual_active = false;
    }
}

void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
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
            e->engaged = false;
            e->current_ptr_id = -1;
            return;
        }
    }

    // Normal toggle switch (non-auto-repeat)
    if (e->toggle_switch && !e->auto_repeat && e->bindings[0].type != BINDING_NONE) {
        if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER) {
            if (e->selected) {
                bool inside = point_in_element(x, y, e);
                bool was_fresh = e->auto_repeat_primary_pressed;
                __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "UP toggle: sel=%d vis=%d in=%d fresh=%d",
                    e->selected, e->visual_active, inside, was_fresh);
                e->auto_repeat_primary_pressed = false;
                if (inside && !was_fresh) {
                    if (e->bindings[0].type != BINDING_NONE)
                        release_binding(result, &e->bindings[0]);
                    e->selected = false;
                    e->visual_active = false;
                    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "UP toggle: DEACTIVATE (tap off)");
                }
            } else {
                e->auto_repeat_primary_pressed = false;
            }
            e->engaged = false;
            e->current_ptr_id = -1;
            return;
        }
        e->selected = !e->selected;
        if (e->selected) {
            e->engaged = false;
            e->current_ptr_id = -1;
            return;
        }
    }

    // Auto-repeat (non-toggle): release primary binding, no long-press/gesture processing
    if (e->auto_repeat) {
        if (e->auto_repeat_primary_pressed && e->bindings[0].type != BINDING_NONE)
            release_binding(result, &e->bindings[0]);
        e->auto_repeat_primary_pressed = false;
        goto cleanup;
    }

    if (e->gesture_long_press_triggered) {
        // Java releaseHeldBindings: releases long-press bindings only (NOT primary)
        // Release non-modifiers first, then modifiers last
        for (int k = e->element_long_press_count - 1; k >= 0; k--) {
            if (e->element_long_press[k].type != BINDING_NONE && !is_modifier_binding(&e->element_long_press[k]))
                release_binding(result, &e->element_long_press[k]);
        }
        for (int k = e->element_long_press_count - 1; k >= 0; k--) {
            if (e->element_long_press[k].type != BINDING_NONE && is_modifier_binding(&e->element_long_press[k]))
                release_binding(result, &e->element_long_press[k]);
        }
        e->gesture_long_press_triggered = false;
    } else if (e->gesture_swipe_triggered) {
        if (e->element_gesture_count > 0) {
            // Release non-modifiers first, then modifiers last
            for (int k = e->element_gesture_count - 1; k >= 0; k--) {
                if (e->element_gesture[k].type != BINDING_NONE && !is_modifier_binding(&e->element_gesture[k]))
                    release_binding(result, &e->element_gesture[k]);
            }
            for (int k = e->element_gesture_count - 1; k >= 0; k--) {
                if (e->element_gesture[k].type != BINDING_NONE && is_modifier_binding(&e->element_gesture[k]))
                    release_binding(result, &e->element_gesture[k]);
            }
        }
        e->gesture_swipe_triggered = false;
    } else {
        bool has_lp = e->element_long_press_count > 0 && e->element_long_press[0].type != BINDING_NONE;
        if (has_lp && !e->gesture_long_press_triggered) {
            if (e->bindings[0].type != BINDING_NONE) {
                press_binding(result, &e->bindings[0], true);
                release_binding(result, &e->bindings[0]);
            }
        } else {
            if (e->bindings[0].type != BINDING_NONE)
                release_binding(result, &e->bindings[0]);
        }
    }

cleanup:
    e->engaged = false;
    e->current_ptr_id = -1;
    e->visual_active = false;
    e->gesture_timer_armed = false;
}
