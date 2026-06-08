#include <android/log.h>
#define LOG_TAG "Winlator_Button"
#include "../touch_processor_internal.h"

void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id; (void)x; (void)y; (void)time_ms;
    TouchProcessorState* s = &g_state;
    bool has_primary = e->bindings[0].type != BINDING_NONE;
    int idx = (int)(e - g_state.elements);
    int mode = e->activation_mode;
#ifndef NDEBUG
    __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] ptr=%d b0=%d tog=%d auto=%d sel=%d mode=%s",
        idx, ptr_id, e->bindings[0].type, e->cached_has_toggle, e->cached_has_auto_repeat,
        e->selected, mode == 0 ? "LOCK" : mode == 1 ? "TRACK" : "HOVER");
#endif

    // Toggle + Auto-repeat mode: burst mode — press+release all bindings at interval
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        if (e->selected) {
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> toggle_auto_deselect", idx);
#endif
            e->auto_repeat_primary_pressed = false;
            e->selected = false;
            e->visual_active = false;
        } else {
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> toggle_auto_select", idx);
#endif
            execute_actions(result, e->bindings, 4);
            e->selected = true;
            e->auto_repeat_last_time = time_ms;
        }
        return;
    }

    // Normal toggle switch (non-auto-repeat)
    if (e->cached_has_toggle && e->selected) {
        if (mode == ACTIVATION_TRACK || mode == ACTIVATION_HOVER) {
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> toggle_sel_track stay_visual", idx);
#endif
            e->auto_repeat_primary_pressed = true;
            return;
        }
#ifndef NDEBUG
        __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> toggle_sel_lock release", idx);
#endif
        if (has_primary)
            release_binding(result, &e->bindings[0]);
        return;
    }

    // Auto-repeat (non-toggle): press primary immediately
    if (e->cached_has_auto_repeat) {
#ifndef NDEBUG
        __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> auto_repeat has_p=%d", idx, has_primary);
#endif
        if (has_primary) {
            press_binding(result, &e->bindings[0], true);
            e->auto_repeat_primary_pressed = true;
        }
        e->auto_repeat_last_time = time_ms;
        return;
    }

    bool arm_lp = e->cached_has_long_press && !e->cached_has_toggle && !e->lp_toggled;
    bool defer_primary = (e->cached_has_long_press || e->cached_has_gesture) && !e->cached_has_toggle && !e->lp_toggled && !e->gesture_toggled;
    e->long_press_arm = arm_lp;
#ifndef NDEBUG
    __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] arm_lp=%d defer=%d has_lp=%d has_gest=%d tog=%d",
        idx, arm_lp, defer_primary, e->cached_has_long_press, e->cached_has_gesture, e->cached_has_toggle);
#endif

    if (defer_primary) {
        if (!has_primary && !e->gesture_toggled && !e->lp_toggled) {
#ifndef NDEBUG
            __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> defer_primary visual=0", idx);
#endif
            e->visual_active = false;
        }
        return;
    }

    if (has_primary) {
#ifndef NDEBUG
        __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> normal_press sticky=%d mode=%s", idx, e->primary_sticky_mask,
            mode == 0 ? "LOCK" : mode == 1 ? "TRACK" : "HOVER");
#endif
        for (int k = 0; k < 4; k++)
            if (e->bindings[k].type != BINDING_NONE && (e->primary_sticky_mask & (1 << k)))
                release_binding(result, &e->bindings[k]);

        press_binding(result, &e->bindings[0], true);
        if (e->cached_has_toggle && (mode == ACTIVATION_TRACK || mode == ACTIVATION_HOVER))
            e->auto_repeat_primary_pressed = true;
    } else if (!e->cached_has_toggle && !e->gesture_toggled && !e->lp_toggled) {
#ifndef NDEBUG
        __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "DOWN[%d] -> no_primary visual=0", idx);
#endif
        e->visual_active = false;
    }
}

void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)time_ms;
    TouchProcessorState* s = &g_state;
    bool has_primary = e->bindings[0].type != BINDING_NONE;
    int idx = (int)(e - g_state.elements);
    float dx = x - e->down_x;
    float dy = y - e->down_y;
    if (dx == 0.0f && dy == 0.0f) return;
    bool inside = point_in_element(x, y, e);
#ifndef NDEBUG
    __android_log_print(ANDROID_LOG_WARN, "Winlator_Gesture", "MOVE[%d] sup=%d swipe=%d tog=%d inside=%d",
        idx, e->gesture_suppressed, e->gesture_swipe_triggered, e->gesture_toggled, inside);
#endif

    // Java: gesture binding check — hasGestureBinding() && !gestureTriggered && !longPressTriggered
    if (!e->gesture_suppressed && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered) {
        const bool has_gesture = e->cached_has_gesture;
        if (has_gesture) {
            float dx_sq = dx * dx, dy_sq = dy * dy;
            float dist_sq = dx_sq + dy_sq;
            if (dist_sq > g_state.gesture_threshold_sq) {
                e->gesture_swipe_triggered = true;
                e->long_press_arm = false;
                e->gesture_long_press_triggered = false;
                if (e->button_gesture_haptic > 0)
                    add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                if (e->gesture_toggled) {
#ifndef NDEBUG
                    __android_log_print(ANDROID_LOG_WARN, "Winlator_Gesture", "MOVE[%d] TOGGLE_OFF FIRING", idx);
#endif
                    release_bindings_list(result, e->element_gesture, e->element_gesture_count);
                    e->gesture_toggled = false;
                    if (!element_is_toggle_active(e)) {
                        e->visual_active = false;
#ifndef NDEBUG
                        __android_log_print(ANDROID_LOG_WARN, "Winlator_Gesture", "MOVE[%d] TOGGLE_OFF visual=0", idx);
#endif
                    }
                } else {
#ifndef NDEBUG
                    __android_log_print(ANDROID_LOG_WARN, "Winlator_Gesture", "MOVE[%d] TOGGLE_ON FIRING", idx);
#endif
                    press_bindings_list(result, e->element_gesture, e->element_gesture_count);
                    // Set gesture_toggled when any gesture binding has toggle=true.
                    // This keeps gesture bindings held after finger UP — only the
                    // next gesture swipe toggles them OFF. Works even when the element
                    // itself is not a toggle switch (cached_has_toggle==false).
                    for (int k = 0; k < e->element_gesture_count; k++)
                        if (e->element_gesture[k].toggle) { e->gesture_toggled = true; break; }
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
                }
            } else {
                if (e->selected) {
                    if (has_primary)
                        release_binding(result, &e->bindings[0]);
                    e->selected = false;
                    e->visual_active = false;
                    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (toggle_slideover_leave)", (int)(e - g_state.elements), e->bindings[0].type);
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
        && !e->gesture_toggled
        && !e->gesture_long_press_triggered
        && !e->gesture_timer_armed) {
        e->visual_active = false;
#ifndef NDEBUG
        __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "MOVE[%d] -> none_primary visual=0", idx);
#endif
    }

    // HOVER mode: non-toggle buttons are only visually active when finger is inside.
    // Keep visual active when a gesture swipe is in-flight — otherwise the
    // gesture handler's restore mechanism fights against this override every frame,
    // causing a visual flicker for gesture-only buttons.
    if (e->activation_mode == ACTIVATION_HOVER && !element_is_toggle_active(e)
        && !e->gesture_swipe_triggered && !inside) {
#ifndef NDEBUG
        __android_log_print(ANDROID_LOG_WARN, "Winlator_Button", "MOVE[%d] -> hover_outside visual=0", idx);
#endif
        e->visual_active = false;
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
                // Direct press: flip toggle state on each UP
                e->auto_repeat_primary_pressed = false;
                e->selected = !e->selected;
                if (e->selected) {
                    return;
                }
                if (has_primary)
                    release_binding(result, &e->bindings[0]);
            } else {
                // Slide-over: release on UP (momentary, not persistent)
                if (e->selected) {
                    if (has_primary)
                        release_binding(result, &e->bindings[0]);
                    e->selected = false;
                    e->visual_active = false;
                }
            }
            return;
        }
        e->selected = !e->selected;
        if (e->selected) {
            return;
        }
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
    } else if (__builtin_expect(e->gesture_swipe_triggered, 0)) {
        e->gesture_swipe_triggered = false;
        if (!e->gesture_toggled)
            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
    } else {
        bool defer_primary = (e->cached_has_long_press || e->cached_has_gesture) && !e->cached_has_toggle && !e->lp_toggled && !e->gesture_toggled;

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
    e->gesture_swipe_triggered = false;
    e->gesture_long_press_triggered = false;
    e->gesture_timer_armed = false;
}
