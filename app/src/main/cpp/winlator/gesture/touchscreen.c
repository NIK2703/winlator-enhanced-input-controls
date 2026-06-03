#include "../touch_processor_internal.h"

void handle_touchscreen_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java TouchscreenGestureHandler.handlePointerDown: InputControlsView.findAt(x, y) iterates REVERSE
    g_state.passthrough_active = false;
    for (int i = g_state.element_count - 1; i >= 0; i--) {
        if (point_in_element(x, y, &g_state.elements[i])) {
            if (g_state.elements[i].passthrough_touch) g_state.passthrough_active = true;
            break;
        }
    }

    // LOCK mode: dispatch to ALL lock elements at point (matches Java handleDownByMode)
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

    // TRACK/HOVER: process non-BUTTON elements first (matches Java handleDownByMode)
    bool handled = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->type != ELEM_BUTTON && point_in_element(x, y, e)) {
            handle_element_down(e, f->ptr_id, x, y, time_ms, result);
            if (e->current_ptr_id == f->ptr_id) handled = true;
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

    // Java TouchscreenGestureHandler.handlePointerDown resets at the very top:
    //   postDoubleTapDrag = false;
    //   pendingDoubleTapAction = null;
    // NOTE: gesture_pending_deferred_double_count is NOT cleared here because
    // Java handlePointerDown only clears pendingDoubleTapAction (the primary field),
    // NOT pendingDeferredDoubleAction (the backup). The backup is consumed by
    // handleDoubleTapConfirmed: pendingDoubleTapAction = pendingDeferredDoubleAction.
    g_state.gesture_post_double_tap_drag = false;
    g_state.gesture_double_tap_consumed = false;
    g_state.gesture_pending_double_count = 0;

    // Java: setSecondFingerActive(false) called before double-tap check
    g_state.gesture_second_active = false;

    bool was_second_deferred = g_state.gesture_deferred_second_finger_tap;
    g_state.gesture_deferred_second_finger_tap = false;

    if (g_state.gesture_main_ptr_id < 0) {
        // Double-tap check FIRST — uses f->bindings (first-finger, not yet overwritten),
        // matching Java TouchscreenGestureHandler.handlePointerDown main-finger path
        // where setSecondFingerActive(false) is called before double-tap check.
        // Java only checks state == DOUBLE_TAP_WAITING (no deferred_tap guard).
        if (g_state.gesture_double_tap_waiting) {
            
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                // Java handleDoubleTapConfirmed: pendingDoubleTapAction = pendingDeferredDoubleAction
                // Recover from deferred backup before checking pending_double_count
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;
                if (g_state.gesture_pending_double_count > 0) {
                    if (g_state.cfg.ts_double_tap_drag_count == 0) {
                        
                        for (int _pi = 0; _pi < g_state.gesture_pending_double_count; _pi++)
                            press_binding(result, &g_state.gesture_pending_double[_pi], false);
                    } else {
                        
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                // Java: after handleDoubleTapConfirmed, the main finger enters
                // TAP_WAITING state so that up/move events work correctly
                f->state = GESTURE_STATE_TAP_WAITING;
                g_state.gesture_main_ptr_id = f->ptr_id;
                
                return;
            } else {
                
                gesture_cancel_double_tap_wait(result);
            }
        }

        // Java: after double-tap check, wasSecondFingerDeferred → setSecondFingerActive(true)
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

        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;

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
                // Java handleDoubleTapConfirmed: pendingDoubleTapAction = pendingDeferredDoubleAction
                // Recover from deferred backup before checking pending_double_count
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                g_state.gesture_pending_deferred_double_count = 0;

                // Initialize this finger's bindings from config (Java: second finger uses handler bindings,
                // which are the primary ts_* config, not ts_single_2nd).
                // This is needed so check_start_drag can find the drag binding after confirm.
                FingerBindings* fb2 = &f->bindings;
                fb2->single_tap_count = g_state.cfg.ts_single_tap_count;
                memcpy(fb2->single_tap, g_state.cfg.ts_single_tap, sizeof(g_state.cfg.ts_single_tap));
                fb2->long_press_count = g_state.cfg.ts_long_press_count;
                memcpy(fb2->long_press, g_state.cfg.ts_long_press, sizeof(g_state.cfg.ts_long_press));
                fb2->double_tap_count = g_state.cfg.ts_double_tap_count;
                memcpy(fb2->double_tap, g_state.cfg.ts_double_tap, sizeof(g_state.cfg.ts_double_tap));
                fb2->single_tap_drag_count = g_state.cfg.ts_single_tap_drag_count;
                memcpy(fb2->single_tap_drag, g_state.cfg.ts_single_tap_drag, sizeof(g_state.cfg.ts_single_tap_drag));
                fb2->long_press_drag_count = g_state.cfg.ts_long_press_drag_count;
                memcpy(fb2->long_press_drag, g_state.cfg.ts_long_press_drag, sizeof(g_state.cfg.ts_long_press_drag));
                fb2->double_tap_drag_count = g_state.cfg.ts_double_tap_drag_count;
                memcpy(fb2->double_tap_drag, g_state.cfg.ts_double_tap_drag, sizeof(g_state.cfg.ts_double_tap_drag));

                // Use config-level check for drag vs non-drag (finger bindings may be uninited)
                bool has_dt_drag = (g_state.cfg.ts_double_tap_drag_count > 0);
                if (g_state.gesture_pending_double_count > 0) {
                    if (!has_dt_drag) {
                        
                        for (int _pi = 0; _pi < g_state.gesture_pending_double_count; _pi++)
                            press_binding(result, &g_state.gesture_pending_double[_pi], false);
                    } else {
                        
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                // Reassign main pointer to second finger for drag (matches Java handleDoubleTapConfirmed:
                // mainPointerId = event.getPointerId(actionIndex))
                g_state.gesture_main_ptr_id = f->ptr_id;
                f->state = GESTURE_STATE_TAP_WAITING;
                f->down_x = f->x;
                f->down_y = f->y;
                
                if (saved_original_ptr_id >= 0) {
                    for (int i = 0; i < MAX_FINGERS; i++) {
                        if (g_state.fingers[i].active && g_state.fingers[i].ptr_id == g_state.gesture_main_ptr_id) {
                            g_state.fingers[i].original_ptr_id = saved_original_ptr_id;
                            break;
                        }
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

        touchpad_finger_down(f, result);
    }
}

void handle_touchscreen_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java processElementsTouchMove: iterate ALL elements, each handleTouchMove checks pointerId==currentPointerId
    bool had_element_move = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->engaged && e->current_ptr_id == f->ptr_id) {
            
            handle_element_move(e, x, y, time_ms, result);
            had_element_move = true;
        }
    }

    // TRACK/HOVER: tracked button processing runs every move (matches Java handleMoveByMode)
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
                            if (new_btn->current_ptr_id == -1)
                                handle_element_down(new_btn, f->ptr_id, x, y, time_ms, result);
                            if (new_btn->current_ptr_id == f->ptr_id && !new_btn->passthrough_touch && tb->count < MAX_TRACKED_PER_POINTER) {
                                if (tb->count == 1) first->long_press_arm = false;
                                tb->element_indices[tb->count++] = (int)(new_btn - g_state.elements);
                            }
                        }
                    }

                    // HOVER mode: hover transitions — prev.deactivate(), curr.activate()
                    if (first->activation_mode == ACTIVATION_HOVER) {
                        int hovered = g_state.hovered_element_per_ptr[pi];
                        TouchElement* prev = (hovered >= 0 && hovered < g_state.element_count) ? &g_state.elements[hovered] : NULL;
                        TouchElement* curr = hit_test_element(x, y);
                        if (prev && prev->current_ptr_id == f->ptr_id && (!curr || curr != prev))
                            handle_element_up(prev, x, y, time_ms, result);
                        if (curr && curr->type == ELEM_BUTTON && curr != prev) {
                            if (curr->current_ptr_id == -1)
                                handle_element_down(curr, f->ptr_id, x, y, time_ms, result);
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

    // Java handleMoveByMode: !passthrough check on first tracked → skip gesture
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

    // Legacy path: check hit_test for elements not found via engaged iteration
    TouchElement* elem = hit_test_element(x, y);
    if (elem && elem->current_ptr_id == f->ptr_id) {
        handle_element_move(elem, x, y, time_ms, result);
        f->last_x = x;
        f->last_y = y;
        return;
    }

    if (f->state >= GESTURE_STATE_TAP_WAITING) {
        if (f->ptr_id == g_state.gesture_main_ptr_id) {
            
            if (!f->cached_has_active_single_tap_drag && f->cached_has_active_single_tap && !g_state.gesture_post_double_tap_drag) {
                // Java TouchscreenGestureHandler.handlePointerMove:
                //   if !hasActiveSingleTapDrag: removeCallbacks(longPressRunnable); move pointer; return
                // Long-press cancelled in gesture_tick by checking travel_x/y
                // Skip early return when post_double_tap_drag is active — drag uses double_tap_drag binding
                
                g_state.ptr_x = x;
                g_state.ptr_y = y;
                add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
                f->last_x = x;
                f->last_y = y;
                return;
            }
        }
        float dx = x - f->down_x;
        float dy = y - f->down_y;
        
        check_start_drag(f, dx, dy, result);
    }

    g_state.ptr_x = x;
    g_state.ptr_y = y;
    add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
    f->last_x = x;
    f->last_y = y;
}

void handle_touchscreen_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java handleUpByMode: process tracked buttons FIRST (matches Java order)
    int pi = f->ptr_id % MAX_FINGERS;
    TrackedButtons* tb = &g_state.tracked[pi];
    bool had_tracked = false;
    if (tb->count > 0) {
        if (tb->count > 1) {
            for (int j = 0; j < tb->count; j++) {
                int idx = tb->element_indices[j];
                if (idx >= 0 && idx < g_state.element_count) {
                    TouchElement* e = &g_state.elements[idx];
                    if (e->current_ptr_id == f->ptr_id) {
                        release_element_bindings(e, result);
                    }
                }
            }
            had_tracked = true;
        }
        memset(tb, 0, sizeof(TrackedButtons));
    }
    g_state.hovered_element_per_ptr[pi] = -1;

    // Java onTouchEvent ACTION_UP: for (ControlElement element : profile.getElements()) element.handleTouchUp(pointerId)
    // Match: iterate ALL elements, release EVERY one with matching ptr_id (no break)
    bool had_element = false;
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].current_ptr_id == f->ptr_id) {
            
            handle_element_up(&g_state.elements[i], x, y, time_ms, result);
            // Java: passthrough handleTouchUp returns false — does NOT consume the touch
            if (!g_state.elements[i].passthrough_touch) {
                had_element = true;
            }
        }
    }
    if (had_tracked || had_element) {
        f->active = false;
        return;
    }

    if (f->ptr_id == g_state.gesture_main_ptr_id) {
        
        g_state.gesture_deferred_second_finger_tap = g_state.gesture_second_active;

        f->pending_resume_action_count = 0;
        f->double_tap_original_id_set = false;

        touchpad_finger_up(f, result);

        // Java: always clears secondFingerActive when main finger lifts
        g_state.gesture_second_active = false;
    } else if (f->second_double_tap_waiting) {
        f->second_double_tap_waiting = false;
        f->second_tap_fallback_count = 0;
        f->active = false;
    } else if (f->double_tap_original_id_set && g_state.gesture_main_ptr_id >= 0
               && f->ptr_id != g_state.gesture_main_ptr_id
               && f->ptr_id == f->original_ptr_id) {
        // Java TouchscreenGestureHandler: original pointer lift after double-tap reassigned main ptr
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
        // Java TouchscreenGestureHandler.handlePointerUp for secondPointerId:
        //   secondPointerId = -1;
        //   releaseHeldAction();
        //   if (pendingResumeAction != null) hold(pendingResumeAction);
        //   (does NOT clear secondFingerActive — stays true)
        TouchFinger* main = NULL;
        for (int mi = 0; mi < MAX_FINGERS; mi++) {
            if (g_state.fingers[mi].active && g_state.fingers[mi].ptr_id == g_state.gesture_main_ptr_id) {
                main = &g_state.fingers[mi];
                break;
            }
        }
        release_held_actions(result);
        if (main && main->pending_resume_action_count > 0) {
            hold_actions(result, main->pending_resume_action, main->pending_resume_action_count);
            main->pending_resume_action_count = 0;
            main->state = GESTURE_STATE_DRAGGING;
        }
        // Java: secondFingerActive stays true after second-finger lift
        g_state.gesture_second_ptr_id = -1;
        f->active = false;
    } else {
        if (g_state.gesture_post_double_tap_drag) {
            // Second-finger double-tap confirm: release any held action
            
            g_state.gesture_post_double_tap_drag = false;
            release_held_actions(result);
        }
        f->active = false;
    }
}
