#include <android/log.h>
#define LOG_TAG "Winlator_Button"
#include "../touch_processor_internal.h"

#define TOGGLE_DEBOUNCE_MS 50
#define BINDING_GAMEPAD_L3 (BINDING_GAMEPAD_BASE + 8)
#define BINDING_GAMEPAD_R3 (BINDING_GAMEPAD_BASE + 9)
#define GESTURE_THRESHOLD_FALLBACK 20.0f
#define MOVE_DEADZONE_EPSILON 0.001f

// auto_repeat_primary_pressed mutation points:
//   handle_toggle_ar_down:          cleared (false)
//   handle_normal_toggle_down:      set (true)
//   handle_ar_down:                 set (true)
//   handle_default_down:            set (true)
//   handle_button_up_activation_mode: cleared (false)
//   element_button_up:              cleared (false)

static inline bool has_any_sticky_slot(uint32_t mask) {
    for (int i = 0; i < MAX_BINDINGS_PER_ELEMENT; i++)
        if (mask & (1u << i)) return true;
    return false;
}

static inline int element_index(const TouchElement* e) {
    return (int)(e - g_state.elements);
}

static inline void deselect_button(TouchElement* e) {
    e->selected = false;
    e->visual_active = false;
    mark_element_dirty(e);
}

static inline void release_and_deselect(TouchElement* e, bool has_primary, TouchActionResult* restrict result) {
    if (has_primary)
        release_binding(result, &e->bindings[0]);
    deselect_button(e);
}

static inline void clear_gesture_defer_state(TouchElement* e, TouchActionResult* restrict result) {
    e->visual_long_press_active = false;
    e->gesture_long_press_triggered = false;
    if (!e->lp_toggled)
        release_bindings_list(result, e->element_long_press, e->element_long_press_count);
}

static inline void clear_gesture_and_release_lp(TouchElement* e, TouchActionResult* restrict result) {
    if (e->gesture_long_press_triggered) {
        clear_gesture_defer_state(e, result);
    } else if (e->gesture_swipe_triggered) {
        e->gesture_swipe_triggered = false;
        if (!e->gesture_toggled)
            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
    }
}

static inline bool toggle_debounce_check(TouchElement* e, uint64_t time_ms) {
    if (e->toggle_debounce_last_time != 0 && time_ms - e->toggle_debounce_last_time < TOGGLE_DEBOUNCE_MS)
        return true;
    e->toggle_debounce_last_time = time_ms;
    return false;
}

static inline bool activate_and_block_deselect(TouchElement* e) {
    if (element_has_gesture_toggle(e)) {
        e->visual_active = true;
        return true;
    }
    return false;
}

static void handle_toggle_ar_down(TouchElement* e, int ptr_id, uint64_t time_ms, TouchActionResult* restrict result) {
    if (e->bindings[0].type != BINDING_NONE)
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "DOWN toggle+AR ptr=%d selected=%d b0.type=%d b0.tog=%d b0.ar=%d b0.int=%d",
            ptr_id, e->selected, e->bindings[0].type,
            e->bindings[0].toggle, e->bindings[0].auto_repeat, e->bindings[0].auto_repeat_interval_ms);
    if (e->selected) {
        if (toggle_debounce_check(e, time_ms)) return;
        // Don't deselect if a gesture/long-press toggle is still active.
        // Check BEFORE release_toggled_alternate_bindings — the gesture toggle
        // must not be cleared by a finger-down on the slot; only the user's
        // deliberate re-gesture can toggle it off.
        if (activate_and_block_deselect(e)) return;
        // Release stale gesture/long-press toggles that are no longer active.
        // gesture_swipe_triggered/long_press_triggered are cleared on finger-up,
        // but gesture_toggled/lp_toggled survive because element_is_toggle_active
        // returns true (selected=true), preventing handle_element_down from clearing them.
        release_toggled_alternate_bindings(e, result, true);
        // Release all bindings on deselect. Element-level toggle+AR never
        // uses gesture_toggled_actions/gesture_held_actions (select path
        // calls press_binding directly, bypassing execute_actions), so
        // no cleanup of those arrays is needed.
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++) {
            TouchBinding* tb = &e->bindings[k];
            if (tb->type == BINDING_NONE) continue;
            release_binding(result, tb);
        }
        e->auto_repeat_primary_pressed = false;
        deselect_button(e);
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] visual=0 (toggle_auto_deselect)", element_index(e));
    } else {
        // execute_actions skips toggle bindings when gesture_is_down_event is false,
        // so press toggle bindings manually, handle non-toggle ones inline
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++) {
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
}

static void handle_normal_toggle_down(TouchElement* e, bool has_primary, uint64_t time_ms, TouchActionResult* restrict result) {
    // Don't deselect on DOWN if long-press is present — defer toggle to UP
    if (e->cached_has_long_press) {
        e->long_press_arm = true;
        e->defer_primary = true;
        e->visual_active = true;
        mark_element_dirty(e);
        return;
    }
    // Don't deselect primary toggle if another toggle is still active
    if (activate_and_block_deselect(e)) {
        if (e->lp_toggled)
            e->long_press_arm = true;
        return;
    }
    if (e->activation_mode == ACTIVATION_HOVER) {
        if (toggle_debounce_check(e, time_ms)) return;
        if (has_primary)
            release_binding(result, &e->bindings[0]);
        deselect_button(e);
        return;
    }
    if (e->activation_mode == ACTIVATION_TRACK) {
        e->auto_repeat_primary_pressed = true;
        return;
    }
    if (toggle_debounce_check(e, time_ms)) return;
    release_and_deselect(e, has_primary, result);
}

static void handle_ar_down(TouchElement* e, bool has_primary, uint64_t time_ms, TouchActionResult* restrict result) {
    if (has_primary) {
        press_binding(result, &e->bindings[0], true);
        e->auto_repeat_primary_pressed = true;
    }
    e->auto_repeat_last_time = time_ms;
}

static void handle_default_down(TouchElement* e, bool has_primary, uint64_t time_ms, TouchActionResult* restrict result) {
    bool arm_lp = e->cached_has_long_press;
    // Defer primary if has LP/gesture, unless toggle-only (no LP)
    // defer_primary truth table:
    // hasLP | hasGesture | hasToggle | result
    //   1   |     X      |     X     |  true   (always defer with LP)
    //   0   |     1      |     0     |  true   (gesture only → defer)
    //   0   |     1      |     1     |  false  (gesture+toggle → don't defer)
    //   0   |     0      |     X     |  false  (no gesture → don't defer)
    e->defer_primary = (e->cached_has_long_press || e->cached_has_gesture)
        && (!e->cached_has_toggle || e->cached_has_long_press);
    e->long_press_arm = arm_lp;
    bool defer_primary = e->defer_primary;
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "DOWN arm_lp=%d has_lp=%d has_gesture=%d defer=%d tog=%d lp_cnt=%d lp0=%d",
        arm_lp, e->cached_has_long_press, e->cached_has_gesture, defer_primary, e->cached_has_toggle, e->element_long_press_count,
        e->element_long_press[0].type);

    if (defer_primary) {
        if (!has_primary && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
            && !e->gesture_toggled && !e->lp_toggled) {
            e->visual_active = false;
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] type=%d visual=0 (defer_primary)", element_index(e), e->bindings[0].type);
        }
        return;
    }

    if (has_primary) {
        // Release previously sticky-held bindings before re-pressing
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++)
            if (e->bindings[k].type != BINDING_NONE && (e->primary_sticky_mask & (1u << k)))
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
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_down[%d] type=%d visual=0 (no_primary)", element_index(e), e->bindings[0].type);
    }
}

void element_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id; (void)x; (void)y;
    bool has_primary = element_has_primary(e);
    if (e->cached_has_toggle && e->cached_has_auto_repeat) { handle_toggle_ar_down(e, ptr_id, time_ms, result); return; }
    if (e->cached_has_toggle && e->selected) { handle_normal_toggle_down(e, has_primary, time_ms, result); return; }
    if (e->cached_has_auto_repeat) { handle_ar_down(e, has_primary, time_ms, result); return; }
    handle_default_down(e, has_primary, time_ms, result);
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
                if (gf) gf->gesture_activated_in_touch = true;
                e->long_press_arm = false;
                e->gesture_long_press_triggered = false;
                if (e->button_gesture_haptic > 0)
                    add_action(result, ACT_HAPTIC, e->button_gesture_haptic, 0, 0);
                toggle_alternate_bindings(e, &e->gesture_toggled, e->cached_gesture_has_toggle,
                    e->element_gesture, e->element_gesture_count, has_primary, result);
                return true;
            }
        }
    }
    return false;
}

static void handle_toggle_move(TouchElement* e, bool has_primary, bool inside, TouchActionResult* restrict result) {
    // Toggle was activated by finger DOWN — binding is held, visual always ON during press
    if (e->auto_repeat_primary_pressed) {
        e->visual_active = true;
        return;
    }
    // Toggle was activated by MOVE entry (slide-over)
    if (inside) {
        if (!e->selected) {
            if (has_primary)
                press_binding(result, &e->bindings[0], true);
            e->selected = true;
            e->visual_active = true;
            e->gesture_timer_armed = true;
            mark_element_dirty(e);
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_slideover_in)", element_index(e), e->bindings[0].type);
            return;
        }
        // Already selected — keep visual_active if another toggle is active
        if (activate_and_block_deselect(e)) {
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_already_other_active)", element_index(e), e->bindings[0].type);
        }
        return;
    }
    if (e->selected) {
        // Don't deselect via slide-over leave if another toggle is still active
        if (activate_and_block_deselect(e)) {
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=1 (toggle_blocked_other_active)", element_index(e), e->bindings[0].type);
            return;
        }
        release_and_deselect(e, has_primary, result);
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (toggle_slideover_leave)", element_index(e), e->bindings[0].type);
        return;
    }
    e->visual_active = false;
    TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (toggle_track_outside_notsel)", element_index(e), e->bindings[0].type);
}

void element_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    bool has_primary = element_has_primary(e);
    float dx = x - e->down_x;
    float dy = y - e->down_y;
    bool inside = point_in_element(x, y, e);
    if (fabsf(dx) < MOVE_DEADZONE_EPSILON && fabsf(dy) < MOVE_DEADZONE_EPSILON) return;

    // Java: gesture binding check — hasGestureBinding() && !gestureTriggered && !longPressTriggered
    // Also skip if any gesture/long-press already fired on this finger.
    if (handle_gesture_swipe(e, dx, dy, has_primary, result)) return;

    // Auto-repeat toggle: latched state (visual = selected)
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        e->visual_active = e->selected;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=%d (toggle_auto_latch)", element_index(e), e->bindings[0].type, e->selected);
        return;
    }

    // Non-auto-repeat toggle in TRACK/HOVER mode
    if (e->cached_has_toggle && !e->cached_has_auto_repeat && (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER)) {
        handle_toggle_move(e, has_primary, inside, result);
        return;
    }

    // LOCK-mode toggle: on move visual_active follows toggle active state,
    // but preserve visual feedback when LP/gesture timer is ticking
    if (e->cached_has_toggle && !e->cached_has_auto_repeat
        && e->activation_mode != ACTIVATION_TRACK && e->activation_mode != ACTIVATION_HOVER) {
        if (!element_is_toggle_active(e) && !e->long_press_arm && !e->gesture_timer_armed) {
            e->visual_active = false;
        }
        return;
    }

    // Auto-repeat: release binding if finger leaves element, press again if re-enters
    // (Skip for toggle+auto-repeat mode — state is latched, not finger-dependent)
    if (e->cached_has_auto_repeat && !e->cached_has_toggle) {
        button_auto_repeat_move(e, inside, time_ms, result);
        if (!inside) {
            e->visual_active = false;
            TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (auto_repeat_left)", element_index(e), e->bindings[0].type);
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
    if (!has_primary && !e->cached_has_toggle
        && !element_has_any_gesture_activity(e)
        && !e->gesture_timer_armed) {
        e->visual_active = false;
        TP_LOG(ANDROID_LOG_DEBUG, LOG_TAG, "button_move[%d] type=%d visual=0 (none_primary)", element_index(e), e->bindings[0].type);
    }
}

static bool handle_button_up_toggle_release(TouchElement* e, TouchActionResult* restrict result) {
    if (!e->defer_primary || !e->cached_has_toggle || e->cached_has_auto_repeat)
        return false;
    bool has_primary = element_has_primary(e);
    if (e->gesture_long_press_triggered || e->gesture_swipe_triggered) {
        clear_gesture_and_release_lp(e, result);
        e->defer_primary = false;
        e->gesture_timer_armed = false;
        return true;
    }
    // Short tap: flip toggle on UP
    if (has_primary)
        e->selected = !e->selected;
    if (e->selected) {
        if (has_primary)
            press_binding(result, &e->bindings[0], true);
        e->visual_active = true;
    } else {
        if (has_primary)
            release_binding(result, &e->bindings[0]);
        e->visual_active = false;
    }
    e->defer_primary = false;
    mark_element_dirty(e);
    return true;
}

static void handle_button_up_primary_release(TouchElement* e, TouchActionResult* restrict result) {
    bool has_primary = element_has_primary(e);
    if (__builtin_expect(e->gesture_long_press_triggered || e->gesture_swipe_triggered, 0)) {
        clear_gesture_and_release_lp(e, result);
        if (e->defer_primary && has_primary && !has_any_sticky_slot(e->primary_sticky_mask))
            release_binding(result, &e->bindings[0]);
    } else {
        if (e->defer_primary) {
            // Primary was NOT pressed on DOWN — press+release as tap on UP
            if (has_primary && !has_any_sticky_slot(e->primary_sticky_mask)) {
                press_binding(result, &e->bindings[0], true);
                release_binding(result, &e->bindings[0]);
            }
        } else {
            // Normal button: primary was pressed on DOWN, release on UP
            if (has_primary && !has_any_sticky_slot(e->primary_sticky_mask))
                release_binding(result, &e->bindings[0]);
        }
    }
}

static void handle_button_up_activation_mode(TouchElement* e, bool has_primary, uint64_t time_ms, TouchActionResult* restrict result) {
    if (e->activation_mode == ACTIVATION_TRACK || e->activation_mode == ACTIVATION_HOVER) {
        if (e->auto_repeat_primary_pressed) {
            e->auto_repeat_primary_pressed = false;
            if (e->activation_mode != ACTIVATION_HOVER) {
                // TRACK mode: flip toggle state on each UP
                // Don't deselect if another toggle is still active
                if (e->selected && activate_and_block_deselect(e)) {
                    return;
                }
                if (toggle_debounce_check(e, time_ms)) return;
                e->selected = !e->selected;
                release_and_deselect(e, has_primary, result);
            }
            // HOVER mode: no-op — toggle managed on DOWN/MOVE entry
        } else if (e->activation_mode != ACTIVATION_HOVER) {
            // Slide-over: release on UP (momentary, not persistent)
            if (e->selected) {
                // Don't deselect if another toggle is still active
                if (!activate_and_block_deselect(e)) {
                    release_and_deselect(e, has_primary, result);
                }
            }
        }
        // HOVER slide-over: no-op — toggle managed on MOVE entry
        return;
    }
    // LOCK mode: toggle already flipped on DOWN, UP handles cleanup only
    if (e->selected) {
        suppress_element_gestures(e, result);
        return;
    }
    // Second UP after DOWN2: toggle already OFF, no-op
}

void element_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    bool has_primary = element_has_primary(e);
    // Toggle + Auto-repeat: keep repeating after finger-up, don't flip selected
    if (e->cached_has_toggle && e->cached_has_auto_repeat) {
        if (e->selected) {
            e->defer_primary = false;
            e->visual_active = true;
            mark_element_dirty(e);
            return;
        }
    }
    // Java ControlElement.handleTouchUp: debounce for non-toggle L3/R3 bindings
    // isKeepButtonPressedAfterMinTime() = !toggleSwitch && (binding == GAMEPAD_BUTTON_L3 || GAMEPAD_BUTTON_R3)
    if (!e->cached_has_toggle) {
        int bt0 = e->bindings[0].type;
        if (bt0 == BINDING_GAMEPAD_L3 || bt0 == BINDING_GAMEPAD_R3)
            e->selected = (time_ms - e->down_time_ms) > BUTTON_MIN_KEEP_PRESSED_MS;
    }

    // Deferred toggle: flip on short tap, skip if LP/gesture fired in this session
    if (handle_button_up_toggle_release(e, result)) return;

    // Normal toggle switch (non-auto-repeat)
    if (e->cached_has_toggle && !e->cached_has_auto_repeat && has_primary) {
        handle_button_up_activation_mode(e, has_primary, time_ms, result);
        e->defer_primary = false;
        return;
    }

    // Auto-repeat (non-toggle): release primary binding, no long-press/gesture processing
    if (e->cached_has_auto_repeat) {
        if (e->auto_repeat_primary_pressed && has_primary)
            release_binding(result, &e->bindings[0]);
        e->auto_repeat_primary_pressed = false;
        e->defer_primary = false;
        e->gesture_timer_armed = false;
        return;
    }

    handle_button_up_primary_release(e, result);

    e->gesture_timer_armed = false;
}
