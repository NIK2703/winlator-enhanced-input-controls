#include "touch_processor_internal.h"

void handle_touchpad_down(TouchFinger* f, float x, float y, TouchActionResult* result) {
    g_state.main_ptr_id = f->ptr_id;
    g_state.scrolling = false;
    g_state.scroll_accum_y = 0;

    TouchElement* elem = hit_test_element(x, y);
    if (elem && elem->type == ELEM_BUTTON) {
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN || elem->activation_mode == ACTIVATION_LOCK) {
            handle_element_down(elem, f->ptr_id, x, y, now_ms(), result);
            if (elem->bindings[0].type == BINDING_MOUSE_LEFT) g_state.pointer_left_enabled = false;
            return;
        }
        if (elem->activation_mode == ACTIVATION_TRACK || elem->activation_mode == ACTIVATION_HOVER) {
            TrackedButtons* tb = &g_state.tracked[f->ptr_id % MAX_FINGERS];
            bool already = false;
            for (int j = 0; j < tb->count; j++) {
                if (tb->element_indices[j] == (int)(elem - g_state.elements)) { already = true; break; }
            }
            if (!already && tb->count < MAX_TRACKED_PER_POINTER) {
                tb->element_indices[tb->count++] = (int)(elem - g_state.elements);
                handle_element_down(elem, f->ptr_id, x, y, now_ms(), result);
            }
            if (elem->activation_mode == ACTIVATION_HOVER)
                g_state.hovered_element_per_ptr[f->ptr_id % MAX_FINGERS] = (int)(elem - g_state.elements);
            if (!elem->passthrough_touch) return;
        }
    } else if (elem && elem->type != ELEM_BUTTON) {
        handle_element_down(elem, f->ptr_id, x, y, now_ms(), result);
        return;
    }

    touchpad_finger_down(f, result);
}

void handle_touchpad_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    TouchElement* elem = hit_test_element(x, y);
    if (elem && elem->current_ptr_id == f->ptr_id) {
        handle_element_move(elem, x, y, time_ms, result);
        if (elem->type != ELEM_TRACKPAD && elem->type != ELEM_STICK) return;
    }

    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD || g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        for (int pi = 0; pi < MAX_FINGERS; pi++) {
            TrackedButtons* tb = &g_state.tracked[pi];
            if (tb->count == 0) continue;
            int first_idx = tb->element_indices[0];
            if (first_idx >= 0 && first_idx < g_state.element_count) {
                TouchElement* first = &g_state.elements[first_idx];
                if (first->type == ELEM_BUTTON && !point_in_element(x, y, first)) {
                    TouchElement* new_btn = hit_test_element(x, y);
                    if (new_btn && new_btn->type == ELEM_BUTTON && new_btn != first) {
                        bool already = false;
                        for (int j = 0; j < tb->count; j++) {
                            if (tb->element_indices[j] == (int)(new_btn - g_state.elements)) { already = true; break; }
                        }
                        if (!already && tb->count < MAX_TRACKED_PER_POINTER) {
                            tb->element_indices[tb->count++] = (int)(new_btn - g_state.elements);
                            handle_element_down(new_btn, f->ptr_id, x, y, time_ms, result);
                        }
                    }
                    if (first->activation_mode == ACTIVATION_HOVER) {
                        int hovered = g_state.hovered_element_per_ptr[pi];
                        TouchElement* prev = (hovered >= 0 && hovered < g_state.element_count) ? &g_state.elements[hovered] : NULL;
                        TouchElement* curr = hit_test_element(x, y);
                        if (curr != prev) {
                            if (prev && prev->current_ptr_id == f->ptr_id) {
                                handle_element_up(prev, x, y, time_ms, result);
                            }
                            if (curr && curr->type == ELEM_BUTTON) {
                                handle_element_down(curr, f->ptr_id, x, y, time_ms, result);
                                g_state.hovered_element_per_ptr[pi] = (int)(curr - g_state.elements);
                            } else {
                                g_state.hovered_element_per_ptr[pi] = -1;
                            }
                        }
                    }
                }
            }
        }
    }

    if (f->state >= GESTURE_STATE_TAP_WAITING) {
        bool is_main = (f->ptr_id == g_state.gesture_main_ptr_id);
        TouchFinger* main_f = NULL;
        for (int _mi = 0; _mi < MAX_FINGERS && !is_main; _mi++) {
            if (g_state.fingers[_mi].active && g_state.fingers[_mi].ptr_id == g_state.gesture_main_ptr_id) {
                main_f = &g_state.fingers[_mi]; break;
            }
        }
        float ref_x = is_main ? f->down_x : (main_f ? main_f->down_x : f->down_x);
        float ref_y = is_main ? f->down_y : (main_f ? main_f->down_y : f->down_y);
        float dx = x - ref_x;
        float dy = y - ref_y;

        if (f->state == GESTURE_STATE_DRAGGING) {
        } else {
            check_start_drag(f, dx, dy, time_ms, result);
        }
    }

    if (active_finger_count() == 2 && !g_state.sim_touch_screen) {
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
                return;
            } else if (dist >= TWO_FINGER_SCROLL_DIST && f2->travel_x < MAX_TAP_TRAVEL && f2->travel_y < MAX_TAP_TRAVEL) {
                if (!g_state.pointer_left_enabled) { g_state.pointer_left_enabled = true; }
                add_action(result, ACT_POINTER_BUTTON_PRESS, 0, 0, 0);
                g_state.finger_pointer_left = f->ptr_id;
                return;
            }
        }
    }

    if (!g_state.scrolling && active_finger_count() <= 2) {
        if (g_state.sim_touch_screen) {
            uint64_t elapsed = now_ms() - f->down_time_ms;
            if (elapsed > CLICK_DELAY_MS)
                add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
        } else {
            float dx = x - f->last_x;
            float dy = y - f->last_y;
            if (g_state.cfg.cursor_acceleration_factor > 0.0f &&
                g_state.cfg.cursor_acceleration_threshold > 0) {
                if (fabsf(dx) > g_state.cfg.cursor_acceleration_threshold) {
                    float sign = dx > 0 ? 1.0f : -1.0f;
                    dx = fabsf(dx) * g_state.cfg.cursor_acceleration_factor * sign;
                }
                if (fabsf(dy) > g_state.cfg.cursor_acceleration_threshold) {
                    float sign = dy > 0 ? 1.0f : -1.0f;
                    dy = fabsf(dy) * g_state.cfg.cursor_acceleration_factor * sign;
                }
            }
            if (dx != 0 || dy != 0) {
                if (g_state.cfg.input_mode == INPUT_RELATIVE)
                    add_action(result, ACT_MOUSE_EVENT, 0, (int)dx, (int)dy);
                else
                    add_action(result, ACT_POINTER_MOVE_DELTA, (int)dx, (int)dy, 0);
            }
        }
    }

    f->last_x = x; f->last_y = y;
}

void handle_touchpad_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    bool had_element = false;
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].current_ptr_id == f->ptr_id) {
            handle_element_up(&g_state.elements[i], x, y, time_ms, result);
            had_element = true;
            break;
        }
    }

    for (int pi = 0; pi < MAX_FINGERS; pi++) {
        TrackedButtons* tb = &g_state.tracked[pi];
        if (tb->count > 0) {
            for (int j = 0; j < tb->count; j++) {
                int idx = tb->element_indices[j];
                if (idx >= 0 && idx < g_state.element_count) {
                    TouchElement* e = &g_state.elements[idx];
                    if (e->current_ptr_id == f->ptr_id)
                        handle_element_up(e, x, y, time_ms, result);
                }
            }
            memset(tb, 0, sizeof(TrackedButtons));
        }
        g_state.hovered_element_per_ptr[pi] = -1;
    }

    if (had_element) {
        g_state.main_ptr_id = -1;
        f->active = false;
        return;
    }

    if (!g_state.gesture_handler_active || !g_state.gesture_is_action_held) {
        bool is_tap = f->travel_x < MAX_TAP_TRAVEL && f->travel_y < MAX_TAP_TRAVEL &&
                      (now_ms() - f->down_time_ms) < TAP_MAX_TIME_MS;
        if (is_tap) {
            int nf = active_finger_count();
            if (nf == 1) {
                if (g_state.sim_touch_screen) {
                    add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
                } else if (!g_state.gesture_handler_active) {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 0, 0, 0);
                }
            } else if (nf == 2) {
                TouchFinger* other = NULL;
                for (int i = 0; i < MAX_FINGERS; i++) {
                    if (g_state.fingers[i].active && g_state.fingers[i].ptr_id != f->ptr_id) { other = &g_state.fingers[i]; break; }
                }
                if (other && other->travel_x < MAX_TAP_TRAVEL && other->travel_y < MAX_TAP_TRAVEL && !g_state.gesture_handler_active) {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 1, 0, 0);
                }
            }
        }
    }

    if (g_state.finger_pointer_left == f->ptr_id) {
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        g_state.finger_pointer_left = -1;
    }
    if (g_state.finger_pointer_right == f->ptr_id) {
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 1, 0, 0);
        g_state.finger_pointer_right = -1;
    }

    touchpad_finger_up(f, result);
    g_state.main_ptr_id = -1;
    g_state.pointer_left_enabled = true;
}
