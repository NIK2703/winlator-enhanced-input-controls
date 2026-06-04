#include "../touch_processor_internal.h"

void handle_touchscreen_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java TouchscreenGestureHandler.handlePointerDown: InputControlsView.findAt(x, y) iterates REVERSE
    g_state.passthrough_active = false;
    for (int i = g_state.element_count - 1; i >= 0; i--) {
        if (point_in_element(x, y, &g_state.elements[i]) && g_state.elements[i].passthrough_touch) {
            g_state.passthrough_active = true;
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

    // Java TouchscreenGestureHandler.handlePointerDown resets at the very top:
    //   postDoubleTapDrag = false;
    //   pendingDoubleTapAction = null;
    // NOTE: doubleTapConsumed is NOT reset in Java handlePointerDown — it persists
    // across fingers to handle the third-tap single-tap after a double-tap.
    // gesture_pending_deferred_double_count is NOT cleared here because
    // Java handlePointerDown only clears pendingDoubleTapAction (the primary field),
    // NOT pendingDeferredDoubleAction (the backup). The backup is consumed by
    // handleDoubleTapConfirmed: pendingDoubleTapAction = pendingDeferredDoubleAction.
    g_state.gesture_post_double_tap_drag = false;
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
                        // Mirror Java handleDoubleTapConfirmed:
                        // executeActionsAndHold(pendingDoubleTapAction) — press + hold
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
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

                // Java TouchscreenGestureHandler.handlePointerDown:
                //   savedOriginalPointerId >= 0 → return (switch to original ptr)
                //   savedOriginalPointerId < 0 → fall through (normal case)
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

                // Java TouchscreenGestureHandler.handlePointerDown:
                //   wasSecondFingerDeferred → setSecondFingerActive(true) inside success branch
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
                
                gesture_cancel_double_tap_wait(result);
            }
        } else if (was_second_deferred) {
            // No double-tap waiting, but deferred second finger was active:
            // restore second-finger bindings (matches Java's post-double-tap-check fallthrough)
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
    } else if (f->ptr_id != g_state.gesture_main_ptr_id) {

        if (g_state.gesture_double_tap_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
                // Java TouchscreenGestureHandler.handlePointerDown second-finger path:
                //   state == DOUBLE_TAP_WAITING → handleDoubleTapConfirmed() + return
                //   Does NOT swap mainPointerId, does NOT set secondPointerId
                //   Does NOT set up gesture state for this finger
                g_state.gesture_double_tap_waiting = false;
                g_state.gesture_deferred_tap_count = 0;
                // Java handleDoubleTapConfirmed: pendingDoubleTapAction = pendingDeferredDoubleAction
                if (g_state.gesture_pending_deferred_double_count > 0 && g_state.gesture_pending_double_count == 0) {
                    g_state.gesture_pending_double_count = g_state.gesture_pending_deferred_double_count;
                    for (int _i = 0; _i < g_state.gesture_pending_deferred_double_count; _i++)
                        g_state.gesture_pending_double[_i] = g_state.gesture_pending_deferred_double[_i];
                    g_state.gesture_pending_deferred_double_count = 0;
                }
                // Java handleDoubleTapConfirmed: pendingDeferredDoubleAction = null
                g_state.gesture_pending_deferred_double_count = 0;

                bool has_dt_drag = (g_state.cfg.ts_double_tap_drag_count > 0);
                if (g_state.gesture_pending_double_count > 0) {
                    if (!has_dt_drag) {
                        // Java: executeActionsAndHold(pendingDoubleTapAction) — press + hold
                        hold_actions(result, g_state.gesture_pending_double, g_state.gesture_pending_double_count);
                    } else {
                        // Java: keep pendingDoubleTapAction for later handling
                        // (discarded on drag start, executed on finger-up via handleTapUp)
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                // Java handleDoubleTapConfirmed: setSecondFingerActive(false); state = IDLE; postDoubleTapDrag = true
                // Then TouchscreenGestureHandler.handlePointerDown sets state = TAP_WAITING
                g_state.gesture_second_active = false;
                g_state.gesture_post_double_tap_drag = true;
                // Transition main finger from DOUBLE_TAP_WAITING to TAP_WAITING so drag can start
                for (int _mi = 0; _mi < MAX_FINGERS; _mi++) {
                    TouchFinger* _mf = &g_state.fingers[_mi];
                    if (_mf->active && _mf->ptr_id == g_state.gesture_main_ptr_id) {
                        _mf->state = GESTURE_STATE_TAP_WAITING;
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
    }
}

void handle_touchscreen_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Java processElementsTouchMove: iterate ALL elements, each handleTouchMove checks pointerId==currentPointerId
    bool had_element_move = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->engaged && e->current_ptr_id == f->ptr_id) {
            
            handle_element_move(e, x, y, time_ms, result);
            if (!e->passthrough_touch) had_element_move = true;
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

                    // HOVER mode: hover transitions — prev.deactivate(), curr.activate()
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
            if (g_state.gesture_second_active) {
                // Java TouchscreenGestureHandler.handlePointerMove:
                //   cur = secondFingerSet
                //   hasActiveSingleTapDrag() → secondFingerSet.hasActiveSingleTapDrag
                //   hasActiveSingleTap() → secondFingerSet.hasActiveSingleTap
                bool sf_has_st_drag = g_state.cfg.ts_single_drag_2nd_count > 0;
                if (!sf_has_st_drag && g_state.cfg.ts_single_2nd_count > 0) {
                    // Java: hasActiveSingleTap() → move pointer, return (no drag)
                    
                    g_state.ptr_x = x;
                    g_state.ptr_y = y;
                    add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
                    f->last_x = x;
                    f->last_y = y;
                    return;
                }
            } else {
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

        // TS second finger gesture move (e.g. post-double-tap drag)
        if (g_state.gesture_second_active
            && f->ptr_id == g_state.gesture_second_ptr_id
            && f->ptr_id != g_state.gesture_main_ptr_id) {
            float dx = x - f->down_x;
            float dy = y - f->down_y;
            if (f->state != GESTURE_STATE_DRAGGING)
                check_start_drag(f, dx, dy, result);
        }
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

        touchpad_finger_up(f, result, time_ms);

        // Java: always clears secondFingerActive when main finger lifts
        g_state.gesture_second_active = false;
    } else if (g_state.second_double_tap_waiting) {
        g_state.second_double_tap_waiting = false;
        g_state.second_tap_fallback_count = 0;
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
        // Java TouchscreenGestureHandler.handlePointerUp: non-matching fingers are silently
        // ignored. The held action (if any) is released on the next finger event through
        // normal gesture processing (handleTapUp → releaseHeldAction), NOT immediately here.
        f->active = false;
    }
}
