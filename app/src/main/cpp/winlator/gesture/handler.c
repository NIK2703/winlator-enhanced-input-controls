#include "../touch_processor_internal.h"
#include <unistd.h>

#define DELAYED_RELEASE_MS 30

static inline void delay_ms(int ms) {
    if (ms > 0) usleep(ms * 1000);
}

// ============================================================
// handle_gesture_down
// ============================================================
void handle_gesture_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    g_state.main_ptr_id = f->ptr_id;

    g_state.passthrough_active = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* pe = &g_state.elements[i];
        if (pe->bindings[0].type == BINDING_MOUSE_LEFT)
            g_state.pointer_left_enabled = false;
        if (point_in_element(x, y, pe) && pe->passthrough_touch)
            g_state.passthrough_active = true;
    }

    // LOCK elements
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

    // TRACK/HOVER non-BUTTON
    bool handled = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->type != ELEM_BUTTON && point_in_element(x, y, e)) {
            handle_element_down(e, f->ptr_id, x, y, time_ms, result);
            if (!e->passthrough_touch) handled = true;
        }
    }

    // TRACK/HOVER BUTTON
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
                if (btn->engaged && tb->count < MAX_TRACKED_PER_POINTER)
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

    // Passthrough: this finger's touch point is on a passthrough element.
    // Set gesture_main_ptr_id so a subsequent finger enters as is_second
    // and receives second-finger bindings. Single-finger gestures for this
    // finger are disabled (state = IDLE), but cursor/pointer tracking
    // and two-finger gestures via the second finger work normally.
    if (g_state.passthrough_active) {
        g_state.gesture_handler_active = true;
        f->state = GESTURE_STATE_IDLE;
        if (g_state.gesture_main_ptr_id < 0) {
            g_state.gesture_main_ptr_id = f->ptr_id;
            g_state.gesture_post_double_tap_drag = false;
            g_state.gesture_double_tap_consumed = false;
            g_state.gesture_second_active = false;
        }
        return;
    }

    // Determine finger role. If the main finger is no longer active (e.g.
    // passthrough finger lifted), reset gesture_main_ptr_id so this finger
    // becomes the new main gesture finger.
    if (g_state.gesture_main_ptr_id >= 0) {
        bool main_active = false;
        for (int _mi = 0; _mi < MAX_FINGERS; _mi++) {
            if (g_state.fingers[_mi].active && g_state.fingers[_mi].ptr_id == g_state.gesture_main_ptr_id) {
                main_active = true;
                break;
            }
        }
        if (!main_active) {
            g_state.gesture_main_ptr_id = -1;
            g_state.gesture_second_active = false;
        }
    }
    bool is_first = (g_state.gesture_main_ptr_id < 0);
    bool is_second = !is_first && (f->ptr_id != g_state.gesture_main_ptr_id);

    if (is_first) {
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = false;
        g_state.gesture_second_active = false;
        g_state.gesture_pending_double_count = 0;
    }

    // ============================================================
    // TS MAIN FINGER
    // ============================================================
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && is_first) {
        bool was_second_deferred = g_state.gesture_deferred_second_finger_tap;
        g_state.gesture_deferred_second_finger_tap = false;

        // DT waiting check
        if (g_state.gesture_double_tap_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
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
                    GesturePairPlan d_plan = gesture_decide_branch(
                        true,  // has_x: D exists
                        g_state.cfg.ts_double_tap_drag_count > 0,
                        false, false,   // no competing for D
                        true,           // D is always holdable
                        true,           // TS mode
                        false,          // main finger
                        0,              // no hold delay
                        false           // is_single_tap_pair: D/Dd
                    );
                    if (d_plan.hold_now) {
                        hold_actions(result, g_state.gesture_pending_double,
                                     g_state.gesture_pending_double_count);
                    } else if (d_plan.pulse_on_up) {
                        g_state.gesture_pending_deferred_double_count = g_state.gesture_pending_double_count;
                        for (int _i = 0; _i < g_state.gesture_pending_double_count; _i++)
                            g_state.gesture_pending_deferred_double[_i] = g_state.gesture_pending_double[_i];
                    }
                    g_state.gesture_pending_double_count = 0;
                }
                g_state.gesture_post_double_tap_drag = true;
                f->state = GESTURE_STATE_TAP_WAITING;
                f->cached_has_long_press_timer = false;
                g_state.gesture_main_ptr_id = f->ptr_id;

                // pointer switch — original main finger lifting
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
                        int tx, ty;
                        touch_transform_coords(x, y, &tx, &ty);
                        add_action(result, ACT_POINTER_MOVE, tx, ty, 0);
                        g_state.ptr_x = tx;
                        g_state.ptr_y = ty;
                        f->state = GESTURE_STATE_TAP_WAITING;
                        return;
                    }
                }

                // restore second-finger bindings if deferred and still active
                if (was_second_deferred && g_state.gesture_second_ptr_id >= 0) {
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
                return;
            } else {
                gesture_cancel_double_tap_wait(result);
            }
        }

        // was_second_deferred without DT — only restore if second finger is still active
        if (!g_state.gesture_double_tap_waiting && was_second_deferred && g_state.gesture_second_ptr_id >= 0) {
            g_state.gesture_second_active = true;
            f->is_second_finger = true;
            FingerBindings* fb = &f->bindings;
            memset(fb, 0, sizeof(FingerBindings));
            fb->single_tap_count = g_state.cfg.ts_single_2nd_count;
            memcpy(fb->single_tap, g_state.cfg.ts_single_2nd, sizeof(g_state.cfg.ts_single_2nd));
            fb->double_tap_count = g_state.cfg.ts_double_2nd_count;
            memcpy(fb->double_tap, g_state.cfg.ts_double_2nd, sizeof(g_state.cfg.ts_double_2nd));
            fb->single_tap_drag_count = g_state.cfg.ts_single_drag_2nd_count;
            memcpy(fb->single_tap_drag, g_state.cfg.ts_single_drag_2nd, sizeof(g_state.cfg.ts_single_drag_2nd));
            fb->double_tap_drag_count = g_state.cfg.ts_double_drag_2nd_count;
            memcpy(fb->double_tap_drag, g_state.cfg.ts_double_drag_2nd, sizeof(g_state.cfg.ts_double_drag_2nd));
            touch_finger_cache_bs(f);
        }

        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;
        {
            int tx, ty;
            touch_transform_coords(x, y, &tx, &ty);
            add_action(result, ACT_POINTER_MOVE, tx, ty, 0);
            g_state.ptr_x = tx;
            g_state.ptr_y = ty;
        }

        touchpad_finger_down(f, result, time_ms);
        return;
    }

    // ============================================================
    // TS SECOND FINGER
    // ============================================================
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && is_second) {
        if (g_state.gesture_double_tap_waiting) {
            if (gesture_is_within_tap_distance(f->x, f->y)) {
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
                    GesturePairPlan d_plan = gesture_decide_branch(
                        true,  // has_x: D exists
                        g_state.cfg.ts_double_tap_drag_count > 0,
                        false, false,   // no competing for D
                        true,           // D is always holdable
                        true,           // TS mode
                        false,          // main finger
                        0,              // no hold delay
                        false           // is_single_tap_pair: D/Dd
                    );
                    if (d_plan.hold_now) {
                        hold_actions(result, g_state.gesture_pending_double,
                                     g_state.gesture_pending_double_count);
                    } else if (d_plan.pulse_on_up) {
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
                        _mf->cached_has_long_press_timer = false;
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

    // ============================================================
    // TOUCHPAD
    // ============================================================
    if (is_first) {
        g_state.scrolling = false;
        g_state.scroll_accum_y = 0;
    }

    if (g_state.sim_touch_screen) {
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
            if (first && (time_ms - first->down_time_ms) >= CLICK_DELAY_MS)
                g_state.sim_continue_click = true;
            else {
                g_state.sim_continue_click = false;
                g_state.sim_click_press_time = 0;
            }
        }
    }

    touchpad_finger_down(f, result, time_ms);
}

// ============================================================
// handle_gesture_move
// ============================================================
void handle_gesture_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    // Track whether finger has ever moved beyond drag_threshold (for LP cancel logic)
    if (fabsf(f->x - f->down_x) > g_state.cfg.drag_threshold_px || fabsf(f->y - f->down_y) > g_state.cfg.drag_threshold_px)
        f->cached_has_moved_beyond_threshold = true;

    // Save first tracked button's gesture state before element processing
    // (element_button_move may disarm timers when finger leaves button bounds)
    int saved_pi = f->ptr_id % MAX_FINGERS;
    TrackedButtons* saved_tb = &g_state.tracked[saved_pi];
    bool first_btn_has_gesture = false;
    bool first_btn_lp_arm = false;
    bool first_btn_gest_swipe = false;
    bool first_btn_gest_lp = false;
    bool first_btn_gest_timer = false;
    if (saved_tb->count > 0) {
        int first_idx = saved_tb->element_indices[0];
        if (first_idx >= 0 && first_idx < g_state.element_count) {
            TouchElement* first_btn = &g_state.elements[first_idx];
            if (first_btn->type == ELEM_BUTTON && first_btn->activation_mode == ACTIVATION_HOVER) {
                first_btn_lp_arm = first_btn->long_press_arm;
                first_btn_gest_swipe = first_btn->gesture_swipe_triggered;
                first_btn_gest_lp = first_btn->gesture_long_press_triggered;
                first_btn_gest_timer = first_btn->gesture_timer_armed;
                first_btn_has_gesture = true;
            }
        }
    }

    bool had_element_move = false;
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (e->engaged && e->current_ptr_id == f->ptr_id) {
            if (e->passthrough_touch) continue;
            handle_element_move(e, x, y, time_ms, result);
            had_element_move = true;
        }
    }

    // Toggle switch slide-over: handle toggles under finger regardless of tb->count
    // (works even when finger starts on empty space)
    {
        TouchElement* toggle_btn = hit_test_element(x, y);
        if (toggle_btn && toggle_btn->type == ELEM_BUTTON && toggle_btn->toggle_switch) {
            ActivationMode mode = g_state.element_count > 0 ?
                g_state.elements[0].activation_mode : ACTIVATION_LOCK;
            if (mode == ACTIVATION_TRACK || mode == ACTIVATION_HOVER) {
                int pi = f->ptr_id % MAX_FINGERS;
                TrackedButtons* tb = &g_state.tracked[pi];
                bool already_tracked = false;
                for (int j = 0; j < tb->count; j++) {
                    if (tb->element_indices[j] == (int)(toggle_btn - g_state.elements)) { already_tracked = true; break; }
                }
                if (!already_tracked && !toggle_btn->gesture_timer_armed) {
                    if (toggle_btn->selected) {
                        if (toggle_btn->bindings[0].type != BINDING_NONE)
                            release_binding(result, &toggle_btn->bindings[0]);
                        toggle_btn->selected = false;
                        toggle_btn->visual_active = false;
                    } else {
                        if (toggle_btn->bindings[0].type != BINDING_NONE)
                            press_binding(result, &toggle_btn->bindings[0], true);
                        toggle_btn->selected = true;
                        toggle_btn->visual_active = true;
                    }
                    toggle_btn->gesture_timer_armed = true;
                    g_state.visual_state_dirty = true;
                }
            }
        } else {
            // Finger not on a toggle — reset gates so re-entry can toggle again
            for (int i = 0; i < g_state.element_count; i++)
                if (g_state.elements[i].toggle_switch)
                    g_state.elements[i].gesture_timer_armed = false;
        }
    }

    // TRACK/HOVER button tracking
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
                        if (!new_btn->toggle_switch) {
                            bool already = false;
                            for (int j = 0; j < tb->count; j++) {
                                if (tb->element_indices[j] == (int)(new_btn - g_state.elements)) { already = true; break; }
                            }
                            if (!already) {
                                if (new_btn->current_ptr_id == -1) {
                                    handle_element_down(new_btn, f->ptr_id, x, y, time_ms, result);
                                }
                                if (tb->count < MAX_TRACKED_PER_POINTER) {
                                    tb->element_indices[tb->count++] = (int)(new_btn - g_state.elements);
                                    if (tb->count > 1) {
                                        suppress_element_gestures(new_btn, result);
                                    }
                                }
                            }
                        }
                    }

                    if (first->activation_mode == ACTIVATION_HOVER) {
                        int hovered = g_state.hovered_element_per_ptr[pi];
                        TouchElement* prev = (hovered >= 0 && hovered < g_state.element_count) ? &g_state.elements[hovered] : NULL;
                        TouchElement* curr = hit_test_element(x, y);
                        if (prev && (!curr || curr != prev) && !prev->toggle_switch) {
                            bool prev_is_first = prev == &g_state.elements[tb->element_indices[0]];
                            release_element_bindings(prev, result);
                            if (prev_is_first && first_btn_has_gesture) {
                                prev->long_press_arm = first_btn_lp_arm;
                                prev->gesture_swipe_triggered = first_btn_gest_swipe;
                                prev->gesture_long_press_triggered = first_btn_gest_lp;
                                prev->gesture_timer_armed = first_btn_gest_timer;
                                prev->current_ptr_id = f->ptr_id;
                                prev->engaged = true;
                            }
                        }
                        if (curr && curr->type == ELEM_BUTTON && curr != prev) {
                            if (curr->toggle_switch) {
                                g_state.hovered_element_per_ptr[pi] = -1;
                            } else {
                                if (curr->bindings[0].type != BINDING_NONE) {
                                    press_binding(result, &curr->bindings[0], true);
                                }
                                curr->visual_active = true;
                                curr->engaged = true;
                                curr->current_ptr_id = f->ptr_id;
                                g_state.visual_state_dirty = true;
                                if (curr != &g_state.elements[tb->element_indices[0]] && !curr->gesture_suppressed) {
                                    suppress_element_gestures(curr, result);
                                }
                                g_state.hovered_element_per_ptr[pi] = (int)(curr - g_state.elements);
                            }
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

    // Skip gesture if tracked non-passthrough button
    {
        TrackedButtons* tb = &g_state.tracked[f->ptr_id % MAX_FINGERS];
        if (tb->count > 0) {
            int first_idx = tb->element_indices[0];
            if (first_idx >= 0 && first_idx < g_state.element_count) {
                TouchElement* first = &g_state.elements[first_idx];
                if (first->type == ELEM_BUTTON && !first->passthrough_touch)
                    return;
            }
        }
    }

    // Legacy element hit test (passthrough elements are skipped — they've already
    // been filtered out above in the first element loop via continue for passthrough).
    TouchElement* elem = hit_test_element(x, y);
    if (elem && elem->current_ptr_id == f->ptr_id && !elem->passthrough_touch) {
        handle_element_move(elem, x, y, time_ms, result);
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // Gesture move processing
    // Allow IDLE main finger (passthrough) to trigger TS second-finger STD
    if ((f->state >= GESTURE_STATE_TAP_WAITING || f->state == GESTURE_STATE_IDLE) && f->ptr_id == g_state.gesture_main_ptr_id) {
        // TS cursor optimization (skip when competing D/Dd or L/Ld present —
        // they need gesture processing for press_on_drag on drag threshold)
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN
            && !g_state.gesture_second_active
            && !f->cached_has_active_single_tap_drag
            && !f->cached_has_active_long_press_drag
            && f->cached_has_active_single_tap
            && !f->cached_has_active_double_tap
            && !f->cached_has_active_double_tap_drag
            && !f->cached_has_long_press_timer
            && !g_state.gesture_post_double_tap_drag
            && f->state != GESTURE_STATE_LONG_PRESSING) {
            {
                int tx, ty;
                touch_transform_coords(x, y, &tx, &ty);
                g_state.ptr_x = tx;
                g_state.ptr_y = ty;
                add_action(result, ACT_POINTER_MOVE, tx, ty, 0);
            }
            f->last_x = x;
            f->last_y = y;
            return;
        }

        float dx = x - f->down_x;
        float dy = y - f->down_y;
        // Drag threshold for main finger's own bindings
        if (f->state != GESTURE_STATE_DRAGGING)
            check_start_drag(f, dx, dy, result);

        // TS: main-finger movement triggers second-finger STD (single-tap-drag)
        // when the second finger is held and has STD configured.
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN && g_state.gesture_second_active) {
            TouchFinger* sf = NULL;
            for (int mi = 0; mi < MAX_FINGERS; mi++) {
                if (g_state.fingers[mi].active && g_state.fingers[mi].ptr_id == g_state.gesture_second_ptr_id) {
                    sf = &g_state.fingers[mi];
                    break;
                }
            }
            if (sf && sf->state != GESTURE_STATE_DRAGGING) {
                float sf_dx = x - g_state.gesture_second_main_ref_x;
                float sf_dy = y - g_state.gesture_second_main_ref_y;
                check_start_drag(sf, sf_dx, sf_dy, result);
            }
        }
    }

    // TS second finger gesture move (own movement)
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN
        && f->state >= GESTURE_STATE_TAP_WAITING
        && g_state.gesture_second_active
        && f->ptr_id == g_state.gesture_second_ptr_id
        && f->ptr_id != g_state.gesture_main_ptr_id) {
        float dx = x - f->down_x;
        float dy = y - f->down_y;
        // TS second-finger STD is triggered by main-finger movement (above).
        // This own-move path also handles press_on_drag for non-Sd S2.
        if (f->state != GESTURE_STATE_DRAGGING)
            check_start_drag(f, dx, dy, result);
    }

    // TS: update absolute pointer
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        if (g_state.gesture_main_ptr_id < 0 || f->ptr_id == g_state.gesture_main_ptr_id) {
            int tx, ty;
            touch_transform_coords(x, y, &tx, &ty);
            g_state.ptr_x = tx;
            g_state.ptr_y = ty;
            add_action(result, ACT_POINTER_MOVE, tx, ty, 0);
        }
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // TP second finger gesture move (aggregate two-finger drag threshold)
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD
        && g_state.gesture_second_active
        && f->ptr_id == g_state.gesture_second_ptr_id
        && f->state >= GESTURE_STATE_TAP_WAITING
        && f->state != GESTURE_STATE_DRAGGING) {
        float dx = x - f->down_x;
        float dy = y - f->down_y;
        // Sum main-finger movement from ref (set at second-finger landing) for combined threshold
        for (int _mi = 0; _mi < MAX_FINGERS; _mi++) {
            TouchFinger* _mf = &g_state.fingers[_mi];
            if (_mf->active && _mf->ptr_id == g_state.gesture_main_ptr_id) {
                dx += _mf->x - g_state.gesture_second_main_ref_x;
                dy += _mf->y - g_state.gesture_second_main_ref_y;
                break;
            }
        }
        check_start_drag(f, dx, dy, result);
    }



    // TP: cursor movement
    int afc = active_finger_count();
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
            if (g_state.cfg.cursor_acceleration_factor > 0.0f && g_state.cfg.cursor_acceleration_threshold > 0) {
                float adx = fabsf(dx);
                if (adx > g_state.cfg.cursor_acceleration_threshold)
                    dx = adx * g_state.cfg.cursor_acceleration_factor * (dx > 0 ? 1.0f : -1.0f);
                float ady = fabsf(dy);
                if (ady > g_state.cfg.cursor_acceleration_threshold)
                    dy = ady * g_state.cfg.cursor_acceleration_factor * (dy > 0 ? 1.0f : -1.0f);
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

// ============================================================
// handle_gesture_up
// ============================================================
void handle_gesture_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    int pi = f->ptr_id % MAX_FINGERS;
    TrackedButtons* tb = &g_state.tracked[pi];
    bool had_tracked = false;
    if (tb->count > 0) {
        if (tb->count > 1) {
            for (int j = 0; j < tb->count; j++) {
                int idx = tb->element_indices[j];
                if (idx >= 0 && idx < g_state.element_count) {
                    TouchElement* e = &g_state.elements[idx];
                    if (e->toggle_switch) {
                        handle_element_up(e, x, y, time_ms, result);
                    } else {
                        if (e->bindings[0].type != BINDING_NONE)
                            release_binding(result, &e->bindings[0]);
                    }
                    if (e->gesture_swipe_triggered) {
                        for (int k = e->element_gesture_count - 1; k >= 0; k--)
                            if (e->element_gesture[k].type != BINDING_NONE && !is_modifier_binding(&e->element_gesture[k]))
                                release_binding(result, &e->element_gesture[k]);
                        for (int k = e->element_gesture_count - 1; k >= 0; k--)
                            if (e->element_gesture[k].type != BINDING_NONE && is_modifier_binding(&e->element_gesture[k]))
                                release_binding(result, &e->element_gesture[k]);
                    }
                    if (e->gesture_long_press_triggered) {
                        for (int k = e->element_long_press_count - 1; k >= 0; k--)
                            if (e->element_long_press[k].type != BINDING_NONE && !is_modifier_binding(&e->element_long_press[k]))
                                release_binding(result, &e->element_long_press[k]);
                        for (int k = e->element_long_press_count - 1; k >= 0; k--)
                            if (e->element_long_press[k].type != BINDING_NONE && is_modifier_binding(&e->element_long_press[k]))
                                release_binding(result, &e->element_long_press[k]);
                    }
                    e->long_press_arm = false;
                    e->gesture_long_press_triggered = false;
                    e->gesture_swipe_triggered = false;
                    if (!e->toggle_switch) {
                        e->current_ptr_id = -1;
                        e->engaged = false;
                        e->visual_active = false;
                        g_state.visual_state_dirty = true;
                    }
                }
            }
            had_tracked = true;
        } else {
            // Single tracked button: ensure full cleanup via handle_element_up
            // Fixes bug where visual_active could remain true after finger-up
            // when the element loop doesn't find it (current_ptr_id mismatch)
            int idx = tb->element_indices[0];
            if (idx >= 0 && idx < g_state.element_count) {
                TouchElement* e = &g_state.elements[idx];
                handle_element_up(e, x, y, time_ms, result);
                if (!e->toggle_switch) {
                    e->visual_active = false;
                }
            }
            had_tracked = true;
        }
        memset(tb, 0, sizeof(TrackedButtons));
    }
    g_state.hovered_element_per_ptr[pi] = -1;

    // Reset slide-over toggle flags so the next gesture can toggle them again
    for (int i = 0; i < g_state.element_count; i++)
        if (g_state.elements[i].toggle_switch)
            g_state.elements[i].gesture_timer_armed = false;

    bool had_element = false;
    for (int i = 0; i < g_state.element_count; i++) {
        if (g_state.elements[i].current_ptr_id == f->ptr_id) {
            handle_element_up(&g_state.elements[i], x, y, time_ms, result);
            had_element = true;
        }
    }
    if (had_tracked || had_element) {
        g_state.main_ptr_id = -1;
        f->active = false;
        return;
    }


    // TS-specific up handling
    if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        if (f->ptr_id == g_state.gesture_main_ptr_id) {
        g_state.gesture_deferred_second_finger_tap = g_state.gesture_second_active;
        f->pending_resume_action_count = 0;
        f->double_tap_original_id_set = false;
        touchpad_finger_up(f, result, time_ms);
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
            touchpad_finger_up(f, result, time_ms);
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
        // TP up handling
        touchpad_finger_up(f, result, time_ms);

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
