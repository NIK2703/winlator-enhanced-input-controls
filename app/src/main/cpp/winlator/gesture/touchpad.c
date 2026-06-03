#include "../touch_processor_internal.h"

#define DELAYED_RELEASE_MS 30

void handle_touchpad_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
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

    // Java handleDownByMode(LOCK): iterate ALL elements, dispatch to EVERY lock element at point
    {
        bool found_lock = false;
        for (int i = 0; i < g_state.element_count; i++) {
            if (point_in_element(x, y, &g_state.elements[i]) && g_state.elements[i].activation_mode == ACTIVATION_LOCK) {
                handle_element_down(&g_state.elements[i], f->ptr_id, x, y, time_ms, result);
                found_lock = true;
            }
        }
        if (found_lock) {
            
            return;
        }
    }

    // Java handleDownByMode(TRACK/HOVER): process non-BUTTON elements FIRST (matches Java order)
    bool handled = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->type != ELEM_BUTTON && point_in_element(x, y, e)) {
            
            handle_element_down(e, f->ptr_id, x, y, time_ms, result);
            if (e->current_ptr_id == f->ptr_id) {
                handled = true;
            }
        }
    }

    // Java handleDownByMode(TRACK/HOVER): then process buttons at point
    TouchElement* btn = hit_test_element(x, y);
    if (btn && btn->type == ELEM_BUTTON) {
        
        if (btn->activation_mode == ACTIVATION_TRACK || btn->activation_mode == ACTIVATION_HOVER) {
            TrackedButtons* tb = &g_state.tracked[f->ptr_id % MAX_FINGERS];
            bool already = false;
            for (int j = 0; j < tb->count; j++) {
                if (tb->element_indices[j] == (int)(btn - g_state.elements)) { already = true; break; }
            }
            if (!already) {
                // Java: handleTouchDown called for both passthrough and non-passthrough
                // handle_element_down now guards current_ptr_id >= 0 (matches Java handleTouchDown)
                handle_element_down(btn, f->ptr_id, x, y, time_ms, result);
                // Java: !isPassthroughTouch() → add to tracked only if engagement succeeded
                if (btn->current_ptr_id == f->ptr_id && !btn->passthrough_touch && tb->count < MAX_TRACKED_PER_POINTER) {
                    tb->element_indices[tb->count++] = (int)(btn - g_state.elements);
                }
            }
            if (btn->activation_mode == ACTIVATION_HOVER)
                g_state.hovered_element_per_ptr[f->ptr_id % MAX_FINGERS] = (int)(btn - g_state.elements);
            // Java: !isPassthroughTouch() → handled = true (regardless of engagement, matches Java)
            if (!btn->passthrough_touch) {
                handled = true;
                
            }
        } else {
            // Non-TRACK/HOVER button at point: still process (matching Java handleTouchDown for buttons)
            handle_element_down(btn, f->ptr_id, x, y, time_ms, result);
            if (!btn->passthrough_touch) {
                handled = true;
                
            }
        }
    }

    if (handled) {
        
        return;
    }

    

    // Java TouchpadGestureHandler.onFingerDown resets these after LOCK/TOUCHSCREEN handling
    g_state.gesture_post_double_tap_drag = false;
    g_state.gesture_second_active = false;
    g_state.scrolling = false;
    g_state.scroll_accum_y = 0;

    // Java TouchpadView.handleFingerDown: simTouchScreen schedules delayed press (50ms clickDelay)
    // pointerId 0 = first finger, pointerId 1 = second finger with cancel-if-still-pending logic
    if (g_state.sim_touch_screen) {
        int nf = active_finger_count();
        if (nf == 1) {
            // First finger: always schedule delayed press
            g_state.sim_continue_click = true;
            g_state.sim_click_press_time = time_ms + CLICK_DELAY_MS;
            g_state.sim_click_ptr_id = f->ptr_id;
            g_state.last_touch_x = (int)x;
            g_state.last_touch_y = (int)y;
        } else if (nf == 2) {
            // Second finger: cancel pending delayed press if first finger's 50ms hasn't elapsed
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

    // Java TouchpadGestureHandler.onFingerDown: release held actions when second finger arrives
    if (g_state.gesture_main_ptr_id >= 0) {
        release_held_actions(result);
    }
    touchpad_finger_down(f, result, time_ms);
}

void handle_touchpad_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java processElementsTouchMove: iterate ALL elements, each handleTouchMove checks pointerId==currentPointerId
    bool had_element_move = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->engaged && e->current_ptr_id == f->ptr_id) {
            
            handle_element_move(e, x, y, time_ms, result);
            had_element_move = true;
        }
    }

    // Java handleMoveByMode TRACK/HOVER: tracked button processing runs EVERY MOVE,
    // regardless of whether processElementsTouchMove returned true.
    // This matches Java: findButtonAt + tracked button addition + hover transitions.
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD || g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        int pi = f->ptr_id % MAX_FINGERS;
        TrackedButtons* tb = &g_state.tracked[pi];
        if (tb->count > 0) {
            int first_idx = tb->element_indices[0];
            if (first_idx >= 0 && first_idx < g_state.element_count) {
                TouchElement* first = &g_state.elements[first_idx];
                if (first->type == ELEM_BUTTON) {
                    // Java: findButtonAt(x, y) always runs — find new button under finger
                    TouchElement* new_btn = hit_test_element(x, y);
                    if (new_btn && new_btn->type == ELEM_BUTTON) {
                        bool already = false;
                        for (int j = 0; j < tb->count; j++) {
                            if (tb->element_indices[j] == (int)(new_btn - g_state.elements)) { already = true; break; }
                        }
                        if (!already) {
                            // Java: first tracked button uses handleTouchDown (sets pointer for gesture);
                            // subsequent tracked buttons use activate() — press primary binding only, no gesture
                            if (new_btn->current_ptr_id == -1) {
                                if (tb->count == 0) {
                                    handle_element_down(new_btn, f->ptr_id, x, y, time_ms, result);
                                } else {
                                    if (new_btn->bindings[0].type != BINDING_NONE)
                                        press_binding(result, &new_btn->bindings[0], true);
                                }
                            }
                            // Add to tracked list for finger-up release
                            if (!new_btn->passthrough_touch && tb->count < MAX_TRACKED_PER_POINTER) {
                                if (tb->count == 1) {
                                    first->long_press_arm = false;
                                }
                                tb->element_indices[tb->count++] = (int)(new_btn - g_state.elements);
                            }
                        }
                    }

                    // Java HOVER mode: hover transitions — prev.deactivate(), curr.activate()
                    // Release prev if moving to different target (button or empty)
                    if (first->activation_mode == ACTIVATION_HOVER) {
                        int hovered = g_state.hovered_element_per_ptr[pi];
                        TouchElement* prev = (hovered >= 0 && hovered < g_state.element_count) ? &g_state.elements[hovered] : NULL;
                        TouchElement* curr = hit_test_element(x, y);
                        if (prev && prev->current_ptr_id == f->ptr_id && (!curr || curr != prev))
                            handle_element_up(prev, x, y, time_ms, result);
                        if (curr && curr->type == ELEM_BUTTON && curr != prev) {
                            if (curr->current_ptr_id == -1) {
                                // Java activate(): press primary binding only (no gesture pointer)
                                if (curr->bindings[0].type != BINDING_NONE)
                                    press_binding(result, &curr->bindings[0], true);
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
        
        return;
    }

    // Java handleMoveByMode: !passthrough check on first tracked → h = true
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

    if (f->state >= GESTURE_STATE_TAP_WAITING) {
        // Java: main finger uses fingerDownX/Y, second finger uses secondFingerDownX/Y (own down pos)
        // C: use each finger's own down_x/down_y as reference (matches Java per-finger tracking)
        float dx = x - f->down_x;
        float dy = y - f->down_y;

        if (f->state != GESTURE_STATE_DRAGGING) {
            check_start_drag(f, dx, dy, result);
        }
    }

    int afc = active_finger_count();
    if (afc == 2 && !g_state.sim_touch_screen) {
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

    if (!g_state.scrolling && afc <= 2) {
        if (g_state.sim_touch_screen) {
            // Java TouchpadView.handleFingerMove: suppress click if finger moved during delay
            if (f->travel_x > MAX_TAP_TRAVEL || f->travel_y > MAX_TAP_TRAVEL)
                g_state.sim_continue_click = false;
            uint64_t elapsed = time_ms - f->down_time_ms;
            if (elapsed > CLICK_DELAY_MS)
                add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
        } else {
            float dx = x - f->last_x;
            float dy = y - f->last_y;

            // Apply xform scale: maps view-pixels to Wine-screen-pixels (matches element/trackpad.c)
            dx *= g_state.cfg.xform_scale_x;
            dy *= g_state.cfg.xform_scale_y;

            // Apply cursor speed sensitivity (matches Java TouchpadView.Finger.deltaX * sensitivity)
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
                // Use Java Mathf.roundPoint rounding: (int)(x <= 0 ? floor(x) : ceil(x))
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

void handle_touchpad_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java handleUpByMode: process tracked buttons FIRST (order matches Java)
    int pi = f->ptr_id % MAX_FINGERS;
    TrackedButtons* tb = &g_state.tracked[pi];
    bool had_tracked = false;
    if (tb->count > 0) {
        if (tb->count > 1) {
            // Java: tracked.size() > 1 → deactivate each (release bindings regardless of current_ptr_id)
            
            for (int j = 0; j < tb->count; j++) {
                int idx = tb->element_indices[j];
                if (idx >= 0 && idx < g_state.element_count) {
                    TouchElement* e = &g_state.elements[idx];
                    // Java deactivate(): cancelPendingLongPress + releaseHeldBindings
                    // Release primary binding (pressed by handle_element_down or activate)
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
                    if (e->gesture_double_tap_triggered) {
                        for (int k = e->element_double_tap_count - 1; k >= 0; k--)
                            if (e->element_double_tap[k].type != BINDING_NONE)
                                release_binding(result, &e->element_double_tap[k]);
                    }
                    // Java cancelPendingLongPress: clear all gesture state
                    e->long_press_arm = false;
                    e->gesture_long_press_triggered = false;
                    e->gesture_swipe_triggered = false;
                    e->gesture_double_tap_triggered = false;
                    e->double_tap_waiting = false;
                    e->current_ptr_id = -1;
                    e->engaged = false;
                }
            }
            had_tracked = true;
        } else {
            // Java: tracked.size() == 1 → just clear list, handleTouchUp via outer loop
            
        }
        memset(tb, 0, sizeof(TrackedButtons));
    }
    g_state.hovered_element_per_ptr[pi] = -1;

    // Java: for (ControlElement element : profile.getElements()) element.handleTouchUp(pointerId)
    // Match: iterate ALL elements, release every one with matching ptr_id (no break)
    bool had_element = false;
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].current_ptr_id == f->ptr_id) {
            
            handle_element_up(&g_state.elements[i], x, y, time_ms, result);
            // Java: passthrough handleTouchUp returns false, so handled stays unchanged
            // Only non-passthrough elements should consume the touch (skip gesture)
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

    if (!g_state.gesture_handler_active) {
        // Java TouchpadView.handleFingerUp: finger.isTap() checks travel AND time (MAX_TAP_MILLISECONDS=200)
        bool is_tap = f->travel_x < MAX_TAP_TRAVEL && f->travel_y < MAX_TAP_TRAVEL
            && (time_ms - f->down_time_ms) < TAP_MAX_TIME_MS;
        if (is_tap) {
            int nf = active_finger_count();
            if (nf == 1) {
                if (g_state.sim_touch_screen) {
                    // Java: delayed release 50ms after finger-up (clickDelay Runnable)
                    g_state.sim_click_release_time = time_ms + CLICK_DELAY_MS;
                } else {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 0, 0, 0);
                }
            } else if (nf == 2) {
                TouchFinger* other = NULL;
                for (int i = 0; i < MAX_FINGERS; i++) {
                    if (g_state.fingers[i].active && g_state.fingers[i].ptr_id != f->ptr_id) { other = &g_state.fingers[i]; break; }
                }
                // Java TouchpadView two-finger tap: only checks lifted finger's travel, not the other
                if (other) {
                    add_action(result, ACT_POINTER_BUTTON_PRESS, 1, 0, 0);
                }
            }
        }
    }

    // Java: gesture handler processes finger-up BEFORE pointer button releases
    touchpad_finger_up(f, result, time_ms);

    // Java TouchpadView.releasePointerButtonLeft/Right uses 30ms postDelayed
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

    g_state.main_ptr_id = -1;
}
