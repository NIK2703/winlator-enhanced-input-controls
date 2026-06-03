#include "touch_processor_internal.h"

void handle_touchscreen_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    TouchElement* elem = hit_test_element(x, y);
    if (elem) {
        LOGD("touchscreen_down: HIT element type=%d x=%d y=%d passthrough=%d bind[0].type=%d",
             elem->type, elem->x, elem->y, elem->passthrough_touch, elem->bindings[0].type);
        handle_element_down(elem, f->ptr_id, x, y, time_ms, result);
        if (!elem->passthrough_touch) {
            LOGD("touchscreen_down: element consumed touch, skipping gesture system");
            g_state.gesture_main_ptr_id = f->ptr_id;
            return;
        }
    } else {
        LOGD("touchscreen_down: NO element at (%.0f,%.0f), routing to gesture system", x, y);
    }

    bool was_second_deferred = false;
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (!g_state.fingers[i].active && g_state.fingers[i].deferred_second_finger_tap) {
            was_second_deferred = true;
            g_state.fingers[i].deferred_second_finger_tap = false;
            break;
        }
    }

    if (g_state.gesture_main_ptr_id < 0) {
        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;
        if (was_second_deferred) {
            g_state.gesture_second_active = true;
            f->is_second_finger = true;
            FingerBindings* fb = &f->bindings;
            fb->single_tap_count = g_state.cfg.ts_single_2nd_count;
            memcpy(fb->single_tap, g_state.cfg.ts_single_2nd, sizeof(g_state.cfg.ts_single_2nd));
            fb->long_press_count = g_state.cfg.ts_long_2nd_count;
            memcpy(fb->long_press, g_state.cfg.ts_long_2nd, sizeof(g_state.cfg.ts_long_2nd));
            fb->double_tap_count = g_state.cfg.ts_double_2nd_count;
            memcpy(fb->double_tap, g_state.cfg.ts_double_2nd, sizeof(g_state.cfg.ts_double_2nd));
            fb->single_tap_drag_count = g_state.cfg.ts_single_drag_2nd_count;
            memcpy(fb->single_tap_drag, g_state.cfg.ts_single_drag_2nd, sizeof(g_state.cfg.ts_single_drag_2nd));
            fb->long_press_drag_count = g_state.cfg.ts_long_drag_2nd_count;
            memcpy(fb->long_press_drag, g_state.cfg.ts_long_drag_2nd, sizeof(g_state.cfg.ts_long_drag_2nd));
            fb->double_tap_drag_count = g_state.cfg.ts_double_drag_2nd_count;
            memcpy(fb->double_tap_drag, g_state.cfg.ts_double_drag_2nd, sizeof(g_state.cfg.ts_double_drag_2nd));
        }

        if (g_state.gesture_double_tap_waiting && g_state.gesture_deferred_tap_count > 0) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                int saved_original_ptr_id = f->original_ptr_id;
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                g_state.gesture_pending_deferred_double_count = 0;
                if (g_state.gesture_pending_double_count > 0) {
                    if (!has_active_double_tap_drag(&f->bindings)) {
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    } else {
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                if (saved_original_ptr_id >= 0) {
                    f->original_ptr_id = saved_original_ptr_id;
                }
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
        g_state.ptr_x = x;
        g_state.ptr_y = y;

        touchpad_finger_down(f, result);
    } else if (f->ptr_id != g_state.gesture_main_ptr_id) {
        if (g_state.long_tap_mode) {
            return;
        }

        if (g_state.gesture_double_tap_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                int saved_original_ptr_id = -1;
                for (int i = 0; i < MAX_FINGERS; i++) {
                    if (g_state.fingers[i].active && g_state.fingers[i].ptr_id == g_state.gesture_main_ptr_id) {
                        saved_original_ptr_id = g_state.fingers[i].original_ptr_id;
                        break;
                    }
                }
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                g_state.gesture_pending_deferred_double_count = 0;
                if (g_state.gesture_pending_double_count > 0) {
                    execute_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    g_state.gesture_pending_double_count = 0;
                }
                if (saved_original_ptr_id >= 0) {
                    for (int i = 0; i < MAX_FINGERS; i++) {
                        if (g_state.fingers[i].active && g_state.fingers[i].ptr_id == g_state.gesture_main_ptr_id) {
                            g_state.fingers[i].original_ptr_id = saved_original_ptr_id;
                            break;
                        }
                    }
                }
                f->active = false;
                return;
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        TouchFinger* main_finger = NULL;
        for (int mi = 0; mi < MAX_FINGERS; mi++) {
            if (g_state.fingers[mi].active && g_state.fingers[mi].ptr_id == g_state.gesture_main_ptr_id) {
                main_finger = &g_state.fingers[mi];
                break;
            }
        }
        if (main_finger) {
            main_finger->pending_resume_action_count = 0;
            if (g_state.gesture_is_action_held) {
                for (int i = 0; i < g_state.gesture_held_count && i < 8; i++) {
                    main_finger->pending_resume_action[i] = g_state.gesture_held_actions[i];
                    main_finger->pending_resume_action_count++;
                }
                release_held_actions(result);
            }
        } else {
            release_held_actions(result);
        }

        touchpad_finger_down(f, result);
    }
}

void handle_touchscreen_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    if (finger_has_engaged_element(f->ptr_id)) {
        TouchElement* elem = hit_test_element(x, y);
        if (elem && elem->current_ptr_id == f->ptr_id) {
            handle_element_move(elem, x, y, time_ms, result);
        } else {
            for (int _ei = 0; _ei < g_state.element_count; _ei++) {
                if (g_state.elements[_ei].engaged && g_state.elements[_ei].current_ptr_id == f->ptr_id) {
                    LOGD("touchscreen_move: finger outside bounds, still updating elem[%d] type=%d", _ei, g_state.elements[_ei].type);
                    handle_element_move(&g_state.elements[_ei], x, y, time_ms, result);
                    break;
                }
            }
        }
        return;
    }

    TouchElement* elem = hit_test_element(x, y);
    if (elem && elem->current_ptr_id == f->ptr_id) {
        handle_element_move(elem, x, y, time_ms, result);
    }

    if (f->state >= GESTURE_STATE_TAP_WAITING) {
        float dx = x - f->down_x;
        float dy = y - f->down_y;
        check_start_drag(f, dx, dy, time_ms, result);
    }

    g_state.ptr_x = x;
    g_state.ptr_y = y;
    add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
    f->last_x = x;
    f->last_y = y;
}

void handle_touchscreen_up(TouchFinger* f, float x, float y, TouchActionResult* result) {
    bool had_element = false;
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].current_ptr_id == f->ptr_id) {
            handle_element_up(&g_state.elements[i], x, y, now_ms(), result);
            had_element = true;
            break;
        }
    }
    if (had_element) {
        f->active = false;
        return;
    }

    if (f->ptr_id == g_state.gesture_main_ptr_id) {
        if (g_state.gesture_second_active) {
            f->deferred_second_finger_tap = true;
        } else {
            f->deferred_second_finger_tap = false;
        }

        f->pending_resume_action_count = 0;
        f->double_tap_original_id_set = false;

        touchpad_finger_up(f, result);
    } else if (f->second_double_tap_waiting) {
        f->second_double_tap_waiting = false;
        f->second_tap_fallback_count = 0;
        f->active = false;
    } else if (g_state.gesture_second_active && f->ptr_id != g_state.gesture_main_ptr_id) {
        TouchFinger* main = NULL;
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (g_state.fingers[i].active && g_state.fingers[i].ptr_id == g_state.gesture_main_ptr_id) {
                main = &g_state.fingers[i];
                break;
            }
        }
        if (main && main->pending_resume_action_count > 0) {
            hold_actions(result, main->pending_resume_action, main->pending_resume_action_count);
            main->pending_resume_action_count = 0;
            main->state = GESTURE_STATE_DRAGGING;
        } else {
            release_held_actions(result);
        }
        g_state.gesture_second_active = false;
        g_state.gesture_second_ptr_id = -1;
        f->active = false;
    } else {
        f->active = false;
    }
}
