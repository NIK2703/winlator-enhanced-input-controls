#include "../touch_processor_internal.h"
#include <android/log.h>

#define DELAYED_RELEASE_MS 30

// ---- Unified gesture handler: replaces both touchscreen.c and touchpad.c ----
// The only difference between TOUCH_MODE_TOUCHSCREEN and TOUCH_MODE_TOUCHPAD
// is cursor behavior (absolute vs relative). Gesture logic is identical.

void handle_gesture_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    g_state.main_ptr_id = f->ptr_id;

    // Check passthrough + disable pointer left if ANY element has MOUSE_LEFT binding
    g_state.passthrough_active = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* pe = &g_state.elements[i];
        if (pe->bindings[0].type == BINDING_MOUSE_LEFT)
            g_state.pointer_left_enabled = false;
        if (point_in_element(x, y, pe) && pe->passthrough_touch)
            g_state.passthrough_active = true;
    }

    // LOCK mode: dispatch to ALL lock elements at point
    {
        bool found_lock = false;
        for (int i = 0; i < g_state.element_count; i++) {
            if (point_in_element(x, y, &g_state.elements[i]) && g_state.elements[i].activation_mode == ACTIVATION_LOCK) {
                handle_element_down(&g_state.elements[i], f->ptr_id, x, y, time_ms, result);
                found_lock = true;
            }
        }
        if (found_lock) return;
    }

    // TRACK/HOVER: process non-BUTTON elements first
    bool handled = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->type != ELEM_BUTTON && point_in_element(x, y, e)) {
            handle_element_down(e, f->ptr_id, x, y, time_ms, result);
            if (!e->passthrough_touch) handled = true;
        }
    }

    // TRACK/HOVER: then process button at point
    TouchElement* btn = hit_test_element(x, y);
    if (btn && btn->type == ELEM_BUTTON) {
        if (btn->activation_mode == ACTIVATION_TRACK || btn->activation_mode == ACTIVATION_HOVER) {
            TrackedButtons* tb = &g_state.tracked[f->ptr_id % MAX_FINGERS];
            bool already = false;
            for (int j = 0; j < tb->count; j++) {
                if (tb->element_indices[j] == (int)(btn - g_state.elements)) { already = true; break; }
            }
            if (!already) {
                handle_element_down(btn, f->ptr_id, x, y, time_ms, result);
                if (btn->current_ptr_id == f->ptr_id && !btn->passthrough_touch && tb->count < MAX_TRACKED_PER_POINTER)
                    tb->element_indices[tb->count++] = (int)(btn - g_state.elements);
            }
            if (btn->activation_mode == ACTIVATION_HOVER)
                g_state.hovered_element_per_ptr[f->ptr_id % MAX_FINGERS] = (int)(btn - g_state.elements);
            if (!btn->passthrough_touch) handled = true;
        } else {
            handle_element_down(btn, f->ptr_id, x, y, time_ms, result);
            if (!btn->passthrough_touch) handled = true;
        }
    }

    if (handled) return;

    // Touchscreen-specific initial absolute pointer move
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        if (g_state.gesture_main_ptr_id < 0) {
            g_state.gesture_post_double_tap_drag = false;
            g_state.gesture_second_active = false;
        }
    }

    // Touchpad-specific gesture state reset
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD) {
        if (g_state.gesture_main_ptr_id < 0) {
            g_state.gesture_post_double_tap_drag = false;
            g_state.gesture_second_active = false;
        }
        g_state.scrolling = false;
        g_state.scroll_accum_y = 0;
    }

    // simTouchScreen handling (touchpad mode)
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && g_state.sim_touch_screen) {
        int nf = active_finger_count();
        if (nf == 1) {
            g_state.sim_continue_click = true;
            g_state.sim_click_press_time = time_ms + CLICK_DELAY_MS;
            g_state.sim_click_ptr_id = f->ptr_id;
            g_state.last_touch_x = (int)x;
            g_state.last_touch_y = (int)y;
        } else if (nf == 2) {
            TouchFinger* first = NULL;
            for (int i = 0; i < MAX_FINGERS; i++) {
                if (g_state.fingers[i].active && g_state.fingers[i].ptr_id != f->ptr_id) {
                    first = &g_state.fingers[i]; break;
                }
            }
            if (first && (time_ms - first->down_time_ms) >= CLICK_DELAY_MS) {
                g_state.sim_continue_click = true;
            } else {
                g_state.sim_continue_click = false;
                g_state.sim_click_press_time = 0;
            }
        }
    }

    // For touchscreen: handle gesture state BEFORE touchpad_finger_down 
    // (double-tap check, pointer identity, etc.)
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && g_state.gesture_main_ptr_id < 0) {
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_pending_double_count = 0;
        g_state.gesture_second_active = false;

        bool was_second_deferred = g_state.gesture_deferred_second_finger_tap;
        g_state.gesture_deferred_second_finger_tap = false;

        if (g_state.gesture_double_tap_waiting) {
            __android_log_print(ANDROID_LOG_INFO, "Gesture",
                "TS1_down DT_waiting: finger(%.1f,%.1f) last_up(%.1f,%.1f) dbl_dist=%d",
                f->x, f->y,
                g_state.gesture_last_tap_up_x, g_state.gesture_last_tap_up_y,
                g_state.cfg.double_tap_distance_px);
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                __android_log_print(ANDROID_LOG_INFO, "Gesture", "TS1_down DT_CONFIRMED");
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;
                if (g_state.gesture_pending_double_count > 0) {
                    bool has_dt_drag = g_state.cfg.ts_double_tap_drag_count > 0;
                    if (!has_dt_drag) {
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    } else {
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                f->state = GESTURE_STATE_TAP_WAITING;
                g_state.gesture_main_ptr_id = f->ptr_id;

                if (f->double_tap_original_id_set) {
                    bool found_orig = false;
                    int orig_ptr_id = -1;
                    for (int _oi = 0; _oi < MAX_FINGERS; _oi++) {
                        TouchFinger* _of = &g_state.fingers[_oi];
                        if (_of->active && _of->double_tap_original_id_set && _of->ptr_id != f->ptr_id) {
                            found_orig = true;
                            orig_ptr_id = _of->ptr_id;
                            break;
                        }
                    }
                    if (found_orig) {
                        g_state.gesture_second_active = false;
                        g_state.gesture_main_ptr_id = orig_ptr_id;
                        add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
                        g_state.ptr_x = x;
                        g_state.ptr_y = y;
                        f->state = GESTURE_STATE_TAP_WAITING;
                        return;
                    }
                }

                if (was_second_deferred) {
                    g_state.gesture_second_active = true;
                    f->is_second_finger = true;
                    FingerBindings* fb = &f->bindings;
                    fb->single_tap_count = g_state.cfg.ts_single_2nd_count;
                    memcpy(fb->single_tap, g_state.cfg.ts_single_2nd, sizeof(g_state.cfg.ts_single_2nd));
                    fb->long_press_count = 0;
                    fb->double_tap_count = g_state.cfg.ts_double_2nd_count;
                    memcpy(fb->double_tap, g_state.cfg.ts_double_2nd, sizeof(g_state.cfg.ts_double_2nd));
                    fb->single_tap_drag_count = g_state.cfg.ts_single_drag_2nd_count;
                    memcpy(fb->single_tap_drag, g_state.cfg.ts_single_drag_2nd, sizeof(g_state.cfg.ts_single_drag_2nd));
                    fb->long_press_drag_count = 0;
                    fb->double_tap_drag_count = g_state.cfg.ts_double_drag_2nd_count;
                    memcpy(fb->double_tap_drag, g_state.cfg.ts_double_drag_2nd, sizeof(g_state.cfg.ts_double_drag_2nd));
                    touch_finger_cache_bs(f);
                }
            } else {
                __android_log_print(ANDROID_LOG_INFO, "Gesture", "TS1_down DT_CANCELLED");
                gesture_cancel_double_tap_wait(result);
            }
        } else if (was_second_deferred) {
            g_state.gesture_second_active = true;
            f->is_second_finger = true;
            FingerBindings* fb = &f->bindings;
            fb->single_tap_count = g_state.cfg.ts_single_2nd_count;
            memcpy(fb->single_tap, g_state.cfg.ts_single_2nd, sizeof(g_state.cfg.ts_single_2nd));
            fb->long_press_count = 0;
            fb->double_tap_count = g_state.cfg.ts_double_2nd_count;
            memcpy(fb->double_tap, g_state.cfg.ts_double_2nd, sizeof(g_state.cfg.ts_double_2nd));
            fb->single_tap_drag_count = g_state.cfg.ts_single_drag_2nd_count;
            memcpy(fb->single_tap_drag, g_state.cfg.ts_single_drag_2nd, sizeof(g_state.cfg.ts_single_drag_2nd));
            fb->long_press_drag_count = 0;
            fb->double_tap_drag_count = g_state.cfg.ts_double_drag_2nd_count;
            memcpy(fb->double_tap_drag, g_state.cfg.ts_double_drag_2nd, sizeof(g_state.cfg.ts_double_drag_2nd));
            touch_finger_cache_bs(f);
        }

        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;

        add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
        g_state.ptr_x = x;
        g_state.ptr_y = y;

        touchpad_finger_down(f, result, time_ms);
        return;
    }

    // For touchscreen second finger path
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && g_state.gesture_main_ptr_id >= 0 && f->ptr_id != g_state.gesture_main_ptr_id) {
        if (g_state.gesture_double_tap_waiting) {
            __android_log_print(ANDROID_LOG_INFO, "Gesture",
                "TS2_down DT_waiting: finger(%.1f,%.1f) last_up(%.1f,%.1f) dbl_dist=%d",
                f->x, f->y,
                g_state.gesture_last_tap_up_x, g_state.gesture_last_tap_up_y,
                g_state.cfg.double_tap_distance_px);
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                __android_log_print(ANDROID_LOG_INFO, "Gesture", "TS2_down DT_CONFIRMED");
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;

                bool has_dt_drag = (g_state.cfg.ts_double_tap_drag_count > 0);
                if (g_state.gesture_pending_double_count > 0) {
                    if (!has_dt_drag) {
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    } else {
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_second_active = false;
                g_state.gesture_post_double_tap_drag = true;
                for (int _mi = 0; _mi < MAX_FINGERS; _mi++) {
                    TouchFinger* _mf = &g_state.fingers[_mi];
                    if (_mf->active && _mf->ptr_id == g_state.gesture_main_ptr_id) {
                        _mf->state = GESTURE_STATE_TAP_WAITING;
                        _mf->down_x = _mf->x;
                        _mf->down_y = _mf->y;
                        break;
                    }
                }
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
            }
        }
        release_held_actions(result);

        touchpad_finger_down(f, result, time_ms);
        return;
    }

    // Default path (shared gesture entry)
    touchpad_finger_down(f, result, time_ms);
}

void handle_gesture_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Process elements first (same for both modes)
    bool had_element_move = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->engaged && e->current_ptr_id == f->ptr_id) {
            handle_element_move(e, x, y, time_ms, result);
            if (!e->passthrough_touch) had_element_move = true;
        }
    }

    // TRACK/HOVER: tracked button processing every move
    {
        int pi = f->ptr_id % MAX_FINGERS;
        TrackedButtons* tb = &g_state.tracked[pi];
        if (tb->count > 0) {
            int first_idx = tb->element_indices[0];
            if (first_idx >= 0 && first_idx < g_state.element_count) {
                TouchElement* first = &g_state.elements[first_idx];
                if (first->type == ELEM_BUTTON) {
                    TouchElement* new_btn = hit_test_element(x, y);
                    if (new_btn && new_btn->type == ELEM_BUTTON) {
                        bool already = false;
                        for (int j = 0; j < tb->count; j++) {
                            if (tb->element_indices[j] == (int)(new_btn - g_state.elements)) { already = true; break; }
                        }
                        if (!already) {
                            if (new_btn->current_ptr_id == -1) {
                                if (tb->count == 0) {
                                    handle_element_down(new_btn, f->ptr_id, x, y, time_ms, result);
                                } else {
                                    if (new_btn->bindings[0].type != BINDING_NONE) {
                                        press_binding(result, &new_btn->bindings[0], true);
                                        new_btn->visual_active = true;
                                    }
                                }
                            }
                            if (!new_btn->passthrough_touch && tb->count < MAX_TRACKED_PER_POINTER) {
                                if (tb->count == 1) first->long_press_arm = false;
                                tb->element_indices[tb->count++] = (int)(new_btn - g_state.elements);
                            }
                        }
                    }

                    // HOVER mode hover transitions
                    if (first->activation_mode == ACTIVATION_HOVER) {
                        int hovered = g_state.hovered_element_per_ptr[pi];
                        TouchElement* prev = (hovered >= 0 && hovered < g_state.element_count) ? &g_state.elements[hovered] : NULL;
                        TouchElement* curr = hit_test_element(x, y);
                        if (prev && (!curr || curr != prev))
                            release_element_bindings(prev, result);
                        if (curr && curr->type == ELEM_BUTTON && curr != prev) {
                            if (curr->current_ptr_id == -1) {
                                if (curr->bindings[0].type != BINDING_NONE) {
                                    press_binding(result, &curr->bindings[0], true);
                                    curr->visual_active = true;
                                }
                            }
                            g_state.hovered_element_per_ptr[pi] = (int)(curr - g_state.elements);
                        } else if (!curr || curr->type != ELEM_BUTTON) {
                            g_state.hovered_element_per_ptr[pi] = -1;
                        }
                    }
                }
            }
        }
    }

    if (had_element_move) {
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // Skip gesture if tracked non-passthrough button exists
    {
        TrackedButtons* tb = &g_state.tracked[f->ptr_id % MAX_FINGERS];
        if (tb->count > 0) {
            int first_idx = tb->element_indices[0];
            if (first_idx >= 0 && first_idx < g_state.element_count) {
                TouchElement* first = &g_state.elements[first_idx];
                if (first->type == ELEM_BUTTON && !first->passthrough_touch) {
                    return;
                }
            }
        }
    }

    // Legacy element hit test
    TouchElement* elem = hit_test_element(x, y);
    if (elem && elem->current_ptr_id == f->ptr_id) {
        handle_element_move(elem, x, y, time_ms, result);
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // Gesture processing
    if (f->state >= GESTURE_STATE_TAP_WAITING) {
        if (f->ptr_id == g_state.gesture_main_ptr_id) {
            // Touchscreen: move cursor only when no drag binding could start
            // (gesture_second_active → drag resolution done by check_start_drag)
            if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN
                && !g_state.gesture_second_active
                && !f->cached_has_active_single_tap_drag
                && f->cached_has_active_single_tap
                && !g_state.gesture_post_double_tap_drag) {
                g_state.ptr_x = x;
                g_state.ptr_y = y;
                add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
                f->last_x = x;
                f->last_y = y;
                return;
            }

            // Drag threshold check (common for both modes)
            float dx = x - f->down_x;
            float dy = y - f->down_y;
            if (f->state != GESTURE_STATE_DRAGGING) {
                check_start_drag(f, dx, dy, result);
            }
        }
    }

    // Touchscreen: update absolute pointer position only for the main finger
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        if (g_state.gesture_main_ptr_id < 0 || f->ptr_id == g_state.gesture_main_ptr_id) {
            g_state.ptr_x = x;
            g_state.ptr_y = y;
            add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
        }
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // Touchpad: two-finger scroll
    int afc = active_finger_count();
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && afc == 2 && !g_state.sim_touch_screen) {
        TouchFinger* f2 = NULL;
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (g_state.fingers[i].active && g_state.fingers[i].ptr_id != f->ptr_id) { f2 = &g_state.fingers[i]; break; }
        }
        if (f2 && !g_state.gesture_second_active) {
            float dist = hypotf(f->x - f2->x, f->y - f2->y) * g_state.resolution_scale;
            if (dist < TWO_FINGER_SCROLL_DIST) {
                float mid_prev = (f->last_y + f2->last_y) * 0.5f;
                float mid_curr = (f->y + f2->y) * 0.5f;
                g_state.scroll_accum_y += mid_curr - mid_prev;
                if (g_state.scroll_accum_y < -SCROLL_ACCUM_THRESHOLD) {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 4, 0, 0);
                    add_action(result, ACT_POINTER_BUTTON_RELEASE, 4, 0, 0);
                    g_state.scroll_accum_y = 0;
                } else if (g_state.scroll_accum_y > SCROLL_ACCUM_THRESHOLD) {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 3, 0, 0);
                    add_action(result, ACT_POINTER_BUTTON_RELEASE, 3, 0, 0);
                    g_state.scroll_accum_y = 0;
                }
                g_state.scrolling = true;
                f->last_x = x; f->last_y = y;
                return;
            } else if (dist >= TWO_FINGER_SCROLL_DIST && f2->travel_x < MAX_TAP_TRAVEL && f2->travel_y < MAX_TAP_TRAVEL) {
                if (!g_state.pointer_left_enabled) { g_state.pointer_left_enabled = true; }
                add_action(result, ACT_POINTER_BUTTON_PRESS, 0, 0, 0);
                g_state.finger_pointer_left = f->ptr_id;
                f->last_x = x; f->last_y = y;
                return;
            }
        }
    }

    // Touchpad: cursor movement
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && !g_state.scrolling && afc <= 2) {
        if (g_state.sim_touch_screen) {
            if (f->travel_x > MAX_TAP_TRAVEL || f->travel_y > MAX_TAP_TRAVEL)
                g_state.sim_continue_click = false;
            uint64_t elapsed = time_ms - f->down_time_ms;
            if (elapsed > CLICK_DELAY_MS)
                add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
        } else {
            float dx = x - f->last_x;
            float dy = y - f->last_y;

            dx *= g_state.cfg.xform_scale_x;
            dy *= g_state.cfg.xform_scale_y;

            float sens = g_state.cfg.cursor_speed / 100.0f;
            dx *= sens;
            dy *= sens;

            if (g_state.cfg.cursor_acceleration_factor > 0.0f &&
                g_state.cfg.cursor_acceleration_threshold > 0) {
                float adx = fabsf(dx);
                if (adx > g_state.cfg.cursor_acceleration_threshold) {
                    dx = adx * g_state.cfg.cursor_acceleration_factor * (dx > 0 ? 1.0f : -1.0f);
                }
                float ady = fabsf(dy);
                if (ady > g_state.cfg.cursor_acceleration_threshold) {
                    dy = ady * g_state.cfg.cursor_acceleration_factor * (dy > 0 ? 1.0f : -1.0f);
                }
            }
            if (dx != 0 || dy != 0) {
                int id = (int)(dx <= 0 ? floorf(dx) : ceilf(dx));
                int jd = (int)(dy <= 0 ? floorf(dy) : ceilf(dy));
                if (g_state.cfg.input_mode == INPUT_RELATIVE)
                    add_action(result, ACT_MOUSE_EVENT, 0, id, jd);
                else
                    add_action(result, ACT_POINTER_MOVE_DELTA, id, jd, 0);
            }
        }
    }

    f->last_x = x; f->last_y = y;
}

void handle_gesture_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Process tracked buttons FIRST
    int pi = f->ptr_id % MAX_FINGERS;
    TrackedButtons* tb = &g_state.tracked[pi];
    bool had_tracked = false;
    if (tb->count > 0) {
        if (tb->count > 1) {
            for (int j = 0; j < tb->count; j++) {
                int idx = tb->element_indices[j];
                if (idx >= 0 && idx < g_state.element_count) {
                    TouchElement* e = &g_state.elements[idx];
                    if (e->bindings[0].type != BINDING_NONE)
                        release_binding(result, &e->bindings[0]);
                    if (e->gesture_swipe_triggered) {
                        for (int k = e->element_gesture_count - 1; k >= 0; k--)
                            if (e->element_gesture[k].type != BINDING_NONE)
                                release_binding(result, &e->element_gesture[k]);
                    }
                    if (e->gesture_long_press_triggered) {
                        for (int k = e->element_long_press_count - 1; k >= 0; k--)
                            if (e->element_long_press[k].type != BINDING_NONE)
                                release_binding(result, &e->element_long_press[k]);
                    }
                    e->long_press_arm = false;
                    e->gesture_long_press_triggered = false;
                    e->gesture_swipe_triggered = false;
                    e->current_ptr_id = -1;
                    e->engaged = false;
                    e->visual_active = false;
                }
            }
            had_tracked = true;
        }
        memset(tb, 0, sizeof(TrackedButtons));
    }
    g_state.hovered_element_per_ptr[pi] = -1;

    // Process elements with matching ptr_id
    bool had_element = false;
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].current_ptr_id == f->ptr_id) {
            handle_element_up(&g_state.elements[i], x, y, time_ms, result);
            if (!g_state.elements[i].passthrough_touch) {
                had_element = true;
            }
        }
    }
    if (had_tracked || had_element) {
        g_state.main_ptr_id = -1;
        f->active = false;
        return;
    }

    // Touchpad: fallback tap handling when no gesture handler active
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && !g_state.gesture_handler_active) {
        bool is_tap = f->travel_x < MAX_TAP_TRAVEL && f->travel_y < MAX_TAP_TRAVEL
            && (time_ms - f->down_time_ms) < TAP_MAX_TIME_MS;
        if (is_tap) {
            int nf = active_finger_count();
            if (nf == 1) {
                if (g_state.sim_touch_screen) {
                    g_state.sim_click_release_time = time_ms + CLICK_DELAY_MS;
                } else {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 0, 0, 0);
                }
            } else if (nf == 2) {
                TouchFinger* other = NULL;
                for (int i = 0; i < MAX_FINGERS; i++) {
                    if (g_state.fingers[i].active && g_state.fingers[i].ptr_id != f->ptr_id) { other = &g_state.fingers[i]; break; }
                }
                if (other) {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 1, 0, 0);
                }
            }
        }
    }

    // Common gesture up
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        // Touchscreen-specific: deferred second finger tracking
        if (f->ptr_id == g_state.gesture_main_ptr_id) {
            g_state.gesture_deferred_second_finger_tap = g_state.gesture_second_active;
            f->pending_resume_action_count = 0;
            f->double_tap_original_id_set = false;

            // Clear second_active BEFORE touchpad_finger_up so handle_tap_up
            // uses correct (main finger) bindings, not second-finger bindings
            g_state.gesture_second_active = false;

            touchpad_finger_up(f, result, time_ms);

            // Note: gesture_second_active already cleared above
        } else if (g_state.second_double_tap_waiting) {
            g_state.second_double_tap_waiting = false;
            g_state.second_tap_fallback_count = 0;
            f->active = false;
        } else if (f->double_tap_original_id_set && g_state.gesture_main_ptr_id >= 0
                   && f->ptr_id != g_state.gesture_main_ptr_id
                   && f->ptr_id == f->original_ptr_id) {
            if (!g_state.gesture_second_active) {
                release_held_actions(result);
                f->pending_resume_action_count = 0;
                f->double_tap_original_id_set = false;
                f->state = GESTURE_STATE_IDLE;
                g_state.gesture_main_ptr_id = -1;
                g_state.gesture_second_active = false;
            } else {
                f->double_tap_original_id_set = false;
            }
            if (g_state.gesture_post_double_tap_drag) {
                g_state.gesture_post_double_tap_drag = false;
                release_held_actions(result);
            }
            f->active = false;
        } else if (g_state.gesture_second_active && f->ptr_id != g_state.gesture_main_ptr_id) {
            TouchFinger* main = NULL;
            for (int mi = 0; mi < MAX_FINGERS; mi++) {
                if (g_state.fingers[mi].active && g_state.fingers[mi].ptr_id == g_state.gesture_main_ptr_id) {
                    main = &g_state.fingers[mi];
                    break;
                }
            }

            // Process second finger's state machine via touchpad_finger_up
            // This fires deferred single-tap, clears gesture_second_active,
            // and releases held actions
            touchpad_finger_up(f, result, time_ms);

            // Touchscreen-specific: restore main finger's pending resume actions
            if (main && main->pending_resume_action_count > 0) {
                hold_actions(result, main->pending_resume_action, main->pending_resume_action_count);
                main->pending_resume_action_count = 0;
                main->state = GESTURE_STATE_DRAGGING;
            }
            g_state.gesture_second_ptr_id = -1;
        } else {
            f->active = false;
        }
    } else {
        // Touchpad: gesture handler processes finger-up BEFORE pointer button releases
        touchpad_finger_up(f, result, time_ms);

        // Touchpad delayed pointer button release
        if (g_state.finger_pointer_left == f->ptr_id) {
            g_state.pending_left_release_time = time_ms + DELAYED_RELEASE_MS;
            g_state.pending_left_release_ptr_id = f->ptr_id;
            g_state.finger_pointer_left = -1;
        }
        if (g_state.finger_pointer_right == f->ptr_id) {
            g_state.pending_right_release_time = time_ms + DELAYED_RELEASE_MS;
            g_state.pending_right_release_ptr_id = f->ptr_id;
            g_state.finger_pointer_right = -1;
        }
    }

    g_state.main_ptr_id = -1;
}
