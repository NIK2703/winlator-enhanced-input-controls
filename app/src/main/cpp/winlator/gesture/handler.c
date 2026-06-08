#include "../touch_processor_internal.h"

#define DELAYED_RELEASE_MS 30

static inline bool check_confirm_dt_waiting(TouchFinger* f, TouchActionResult* restrict result,
    TouchFinger* main_finger, bool use_main_bindings)
{
    if (!__builtin_expect(g_state.gesture_double_tap_waiting, 0)) return false;
    if (!gesture_is_within_tap_distance(f->x, f->y)) {
        gesture_cancel_double_tap_wait(result);
        return false;
    }
    TouchFinger* dt_target = use_main_bindings && main_finger ? main_finger : f;
    double_tap_confirm_internal(result, dt_target);
    TouchFinger* reset_target = use_main_bindings ? main_finger : f;
    if (reset_target) {
        reset_target->state = GESTURE_STATE_TAP_WAITING;
        reset_finger_tap_state(reset_target);
        if (use_main_bindings) {
            reset_target->down_x = reset_target->x;
            reset_target->down_y = reset_target->y;
        }
    }
    return true;
}

static inline void save_pending_resume_action(TouchFinger* main_finger) {
    if (!main_finger || !main_finger->active) return;
    main_finger->pending_resume_action_count = 0;
    if (g_state.gesture_is_action_held) {
        for (int i = 0; i < g_state.gesture_held_count && i < 8; i++) {
            main_finger->pending_resume_action[i] = g_state.gesture_held_actions[i];
            main_finger->pending_resume_action_count++;
        }
    }
}

static inline void cleanup_second_finger_up(TouchActionResult* restrict result) {
    TouchFinger* main = find_finger(g_state.gesture_main_ptr_id);
    if (main && main->pending_resume_action_count > 0) {
        execute_actions_hold(result, main->pending_resume_action, main->pending_resume_action_count);
        main->pending_resume_action_count = 0;
        main->state = GESTURE_STATE_DRAGGING;
    }
    g_state.gesture_second_ptr_id = -1;
}

static inline void schedule_pending_release(uint64_t* time_field, int* ptr_field, int* id_field, int ptr_id, uint64_t time_ms) {
    if (*id_field == ptr_id) {
        *time_field = time_ms + DELAYED_RELEASE_MS;
        *ptr_field = ptr_id;
        *id_field = -1;
    }
}

// ============================================================
// handle_gesture_down
// ============================================================
void handle_gesture_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    g_state.main_ptr_id = f->ptr_id;

    g_state.passthrough_active = false;
    bool found_lock = false;
    bool handled = false;
    TouchElement* elements = g_state.elements;
    int element_count = g_state.element_count;
    for (int i = 0; i < element_count; i++) {
        TouchElement* pe = &elements[i];
        if (g_state.cfg.caps_has_mouse_left_element && __builtin_expect(pe->bindings[0].type == BINDING_MOUSE_LEFT, 0))
            g_state.pointer_left_enabled = false;
        if (!point_in_element(x, y, pe)) continue;

        if (g_state.cfg.caps_has_passthrough_elements && __builtin_expect(pe->passthrough_touch, 0))
            g_state.passthrough_active = true;

        ActivationMode am = pe->activation_mode;
        if (__builtin_expect(am == ACTIVATION_LOCK, 0)) {
            handle_element_down(pe, f->ptr_id, x, y, time_ms, result);
            found_lock = true;
        } else if (__builtin_expect(pe->type != ELEM_BUTTON, 0)) {
            handle_element_down(pe, f->ptr_id, x, y, time_ms, result);
            if (!pe->passthrough_touch) handled = true;
        }
    }
    if (__builtin_expect(found_lock, 0)) return;

    // TRACK/HOVER BUTTON
    TouchElement* btn = hit_test_element(x, y);
    if (btn && btn->type == ELEM_BUTTON) {
        if (btn->activation_mode == ACTIVATION_TRACK || btn->activation_mode == ACTIVATION_HOVER) {
            TrackedButtons* tb = &g_state.tracked[(uint32_t)f->ptr_id % MAX_FINGERS];
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
                g_state.hovered_element_per_ptr[(uint32_t)f->ptr_id % MAX_FINGERS] = (int)(btn - g_state.elements);
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
    if (__builtin_expect(g_state.passthrough_active, 0)) {
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
        TouchFinger* _mf = find_finger(g_state.gesture_main_ptr_id);
        if (!_mf || !_mf->active) {
            g_state.gesture_main_ptr_id = -1;
            g_state.gesture_second_active = false;
        }
    }
    bool is_first = (g_state.gesture_main_ptr_id < 0);
    bool is_second = !is_first && (f->ptr_id != g_state.gesture_main_ptr_id);
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "handle_gesture_down ptr=%d role=%s", f->ptr_id, is_first ? "main" : "second");

    if (is_first) {
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = false;
        g_state.gesture_second_active = false;
        g_state.gesture_pending_double_count = 0;
    }

    // ============================================================
    // TS MAIN FINGER
    // ============================================================
    if (g_state.cfg.is_ts && is_first) {
        bool was_second_deferred = g_state.gesture_deferred_second_finger_tap;
        g_state.gesture_deferred_second_finger_tap = false;

        // DT waiting check
        if (check_confirm_dt_waiting(f, result, NULL, false)) {
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
                    update_ts_pointer(x, y, result);
                    f->state = GESTURE_STATE_TAP_WAITING;
                    return;
                }
            }

            // restore second-finger bindings if deferred and still active
            if (was_second_deferred && g_state.gesture_second_ptr_id >= 0) {
                g_state.gesture_second_active = true;
                f->is_second_finger = true;
                setup_second_finger_bindings(f);
            }
            return;
        }

        // was_second_deferred without DT — only restore if second finger is still active
        if (!__builtin_expect(g_state.gesture_double_tap_waiting, 0) && was_second_deferred && g_state.gesture_second_ptr_id >= 0) {
            g_state.gesture_second_active = true;
            f->is_second_finger = true;
            setup_second_finger_bindings(f);
        }

        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;
        update_ts_pointer(x, y, result);

        touchpad_finger_down(f, result, time_ms);
        return;
    }

    // ============================================================
    // TS SECOND FINGER
    // ============================================================
    if (g_state.cfg.is_ts && is_second) {
        // Early exit: no second-finger gesture bindings in TS mode and no pending DT
        if (!(g_state.cfg.caps_ts_mask & (GESTURE_MASK(GESTURE_SINGLE_2ND) | GESTURE_MASK(GESTURE_DOUBLE_2ND)
            | GESTURE_MASK(GESTURE_SINGLE_DRAG_2ND) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND)))
            && !__builtin_expect(g_state.gesture_double_tap_waiting, 0)) {
            g_state.gesture_second_active = true;
            g_state.gesture_second_ptr_id = f->ptr_id;
            f->is_second_finger = true;
            f->state = GESTURE_STATE_IDLE;
            return;
        }

        TouchFinger* _mf = find_finger(g_state.gesture_main_ptr_id);
        if (check_confirm_dt_waiting(f, result, _mf, true)) {
            g_state.gesture_second_active = false;
            return;
        }

        save_pending_resume_action(find_finger(g_state.gesture_main_ptr_id));
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
    } else if (is_second) {
        save_pending_resume_action(find_finger(g_state.gesture_main_ptr_id));
        release_held_actions(result);
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
void handle_gesture_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    // Cache hit_test_element result to avoid redundant spatial grid lookups
    TouchElement* ht_elem = NULL;
    bool ht_elem_valid = false;

    // Track whether finger has ever moved beyond drag_threshold (for LP cancel logic)
    if (__builtin_expect(fabsf(f->x - f->down_x) > g_state.cfg.drag_threshold_px || fabsf(f->y - f->down_y) > g_state.cfg.drag_threshold_px, 0))
        f->cached_has_moved_beyond_threshold = true;

    // Save first tracked button's gesture state before element processing
    // (element_button_move may disarm timers when finger leaves button bounds)
    bool first_btn_has_gesture = false;
    bool first_btn_lp_arm = false;
    bool first_btn_gest_swipe = false;
    bool first_btn_gest_lp = false;
    bool first_btn_gest_timer = false;
    if (g_state.cfg.caps_has_track_hover_buttons) {
        int saved_pi = (uint32_t)f->ptr_id % MAX_FINGERS;
        TrackedButtons* saved_tb = &g_state.tracked[saved_pi];
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
    }

    bool had_element_move = false;
    TouchElement* elements = g_state.elements;
    int element_count = g_state.element_count;
    for (int i = 0; i < element_count; i++) {
        TouchElement* e = &elements[i];
        if (__builtin_expect(e->engaged && e->current_ptr_id == f->ptr_id, 0)) {
            if (e->passthrough_touch) continue;
            handle_element_move(e, x, y, time_ms, result);
            had_element_move = true;
        }
    }

    // Toggle switch slide-over: handle toggles under finger regardless of tb->count
    // (works even when finger starts on empty space)
    if (g_state.cfg.caps_has_element_toggle) {
        if (!ht_elem_valid) { ht_elem = hit_test_element(x, y); ht_elem_valid = true; }
        TouchElement* toggle_btn = ht_elem;
        if (toggle_btn && toggle_btn->type == ELEM_BUTTON && toggle_btn->cached_has_toggle) {
            ActivationMode mode = g_state.activation_mode;
            if (mode == ACTIVATION_TRACK || mode == ACTIVATION_HOVER) {
                int pi = (uint32_t)f->ptr_id % MAX_FINGERS;
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
                        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "gesture_move[%d] type=%d visual=0 (toggle_deselect)", (int)(toggle_btn - g_state.elements), toggle_btn->type);
                        toggle_btn->visual_active = false;
                    } else {
                        if (toggle_btn->bindings[0].type != BINDING_NONE)
                            press_binding(result, &toggle_btn->bindings[0], true);
                        toggle_btn->selected = true;
                        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "gesture_move[%d] type=%d visual=1 (toggle_select)", (int)(toggle_btn - g_state.elements), toggle_btn->type);
                        toggle_btn->visual_active = true;
                    }
                    toggle_btn->gesture_timer_armed = true;
                    mark_element_dirty(toggle_btn);
                }
            }
        } else {
            // Finger not on a toggle — reset gates so re-entry can toggle again
            for (int i = 0; i < element_count; i++)
                if (elements[i].cached_has_toggle)
                    elements[i].gesture_timer_armed = false;
        }
    }

    // TRACK/HOVER button tracking
    if (g_state.cfg.caps_has_track_hover_buttons) {
        int pi = (uint32_t)f->ptr_id % MAX_FINGERS;
        TrackedButtons* tb = &g_state.tracked[pi];
        if (tb->count > 0) {
            int first_idx = tb->element_indices[0];
            if (first_idx >= 0 && first_idx < g_state.element_count) {
                TouchElement* first = &g_state.elements[first_idx];
                if (first->type == ELEM_BUTTON) {
                    if (!ht_elem_valid) { ht_elem = hit_test_element(x, y); ht_elem_valid = true; }
                    TouchElement* new_btn = ht_elem;
                    if (new_btn && new_btn->type == ELEM_BUTTON) {
                        if (!new_btn->cached_has_toggle) {
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
                        if (!ht_elem_valid) { ht_elem = hit_test_element(x, y); ht_elem_valid = true; }
                        TouchElement* curr = ht_elem;
                        if (prev && (!curr || curr != prev) && !prev->cached_has_toggle) {
                            bool prev_is_first = prev == &g_state.elements[tb->element_indices[0]];
                            release_element_bindings(prev, result);
                            if (prev_is_first && first_btn_has_gesture) {
                                prev->long_press_arm = first_btn_lp_arm;
                                prev->gesture_swipe_triggered = first_btn_gest_swipe;
                                prev->gesture_long_press_triggered = first_btn_gest_lp;
                                prev->gesture_timer_armed = first_btn_gest_timer;
                                prev->current_ptr_id = f->ptr_id;
                                prev->engaged = true;
                                prev->visual_active = true;
                            }
                        }
                        if (curr && curr->type == ELEM_BUTTON && curr != prev) {
                            if (curr->cached_has_toggle) {
                                g_state.hovered_element_per_ptr[pi] = -1;
                            } else {
                                if (curr->bindings[0].type != BINDING_NONE) {
                                    press_binding(result, &curr->bindings[0], true);
                                }
                                TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "gesture_move[%d] type=%d visual=1 (hover_activate)", (int)(curr - g_state.elements), curr->type);
                                curr->visual_active = true;
                                curr->engaged = true;
                                curr->current_ptr_id = f->ptr_id;
                                mark_element_dirty(curr);
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

    if (__builtin_expect(had_element_move, 0)) {
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // Skip gesture if tracked non-passthrough button
    if (g_state.cfg.caps_has_track_hover_buttons) {
        TrackedButtons* tb = &g_state.tracked[(uint32_t)f->ptr_id % MAX_FINGERS];
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
    if (!ht_elem_valid) { ht_elem = hit_test_element(x, y); ht_elem_valid = true; }
    TouchElement* elem = ht_elem;
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
        if (g_state.cfg.is_ts
            && !g_state.gesture_second_active
            && !f->cached_has_active_single_tap_drag
            && !f->cached_has_active_long_press_drag
            && f->cached_has_active_single_tap
            && !f->cached_has_active_double_tap
            && !f->cached_has_active_double_tap_drag
            && !f->cached_has_long_press_timer
            && !g_state.gesture_post_double_tap_drag
            && f->state != GESTURE_STATE_LONG_PRESSING) {
            update_ts_pointer(x, y, result);
            f->last_x = x;
            f->last_y = y;
            return;
        }

        float dx = x - f->down_x;
        float dy = y - f->down_y;
        // Drag threshold for main finger's own bindings
        check_start_drag(f, dx, dy, result);

        // TS: main-finger movement triggers second-finger STD (single-tap-drag)
        // when the second finger is held and has STD configured.
        if (g_state.cfg.is_ts && g_state.gesture_second_active) {
            TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
            if (sf) {
                float sf_dx = x - g_state.gesture_second_main_ref_x;
                float sf_dy = y - g_state.gesture_second_main_ref_y;
                check_start_drag(sf, sf_dx, sf_dy, result);
            }
        }
    }

    // TS second finger gesture move (own movement)
    if (g_state.cfg.is_ts
        && f->state >= GESTURE_STATE_TAP_WAITING
        && g_state.gesture_second_active
        && f->ptr_id == g_state.gesture_second_ptr_id) {
        float dx = x - f->down_x;
        float dy = y - f->down_y;
        // TS second-finger STD is triggered by main-finger movement (above).
        // This own-move path also handles press_on_drag for non-Sd S2.
        // Only process if second-finger bindings exist in TS mode
        uint32_t ts_second = GESTURE_MASK(GESTURE_SINGLE_2ND) | GESTURE_MASK(GESTURE_DOUBLE_2ND)
                           | GESTURE_MASK(GESTURE_SINGLE_DRAG_2ND) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);
        if (g_state.cfg.caps_ts_mask & ts_second)
            check_start_drag(f, dx, dy, result);
    }

    // TS: update absolute pointer
    if (g_state.cfg.is_ts) {
        if (g_state.gesture_main_ptr_id < 0 || f->ptr_id == g_state.gesture_main_ptr_id)
            update_ts_pointer(x, y, result);
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // TP second finger gesture move (aggregate two-finger drag threshold)
    if (g_state.cfg.is_tp
        && g_state.gesture_second_active
        && f->ptr_id == g_state.gesture_second_ptr_id
        && f->state >= GESTURE_STATE_TAP_WAITING
        && f->state != GESTURE_STATE_DRAGGING) {
        float dx = x - f->down_x;
        float dy = y - f->down_y;
        TouchFinger* _mf = find_finger(g_state.gesture_main_ptr_id);
        if (_mf) {
            dx += _mf->x - g_state.gesture_second_main_ref_x;
            dy += _mf->y - g_state.gesture_second_main_ref_y;
        }
        // Only process if second-finger bindings exist in TP mode
        uint32_t tp_second = GESTURE_MASK(GESTURE_SINGLE_2ND) | GESTURE_MASK(GESTURE_DOUBLE_2ND)
                           | GESTURE_MASK(GESTURE_SINGLE_DRAG_2ND) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);
        if (g_state.cfg.caps_tp_mask & tp_second)
            check_start_drag(f, dx, dy, result);
    }



    // TP: cursor movement
    int afc = active_finger_count();
    if (g_state.cfg.is_tp && !g_state.scrolling && afc <= 2) {
        if (g_state.sim_touch_screen) {
            if (f->travel_x > MAX_TAP_TRAVEL || f->travel_y > MAX_TAP_TRAVEL)
                g_state.sim_continue_click = false;
            uint64_t elapsed = time_ms - f->down_time_ms;
            if (elapsed > CLICK_DELAY_MS)
                add_action(result, ACT_POINTER_MOVE, (int)x, (int)y, 0);
        } else {
            float dx = x - f->last_x;
            float dy = y - f->last_y;
            float sx = g_state.cfg.xform_scale_x;
            float sy = g_state.cfg.xform_scale_y;
            float sens = g_state.cfg.cursor_speed / 100.0f;
            float accel_factor = g_state.cfg.cursor_acceleration_factor;
            float accel_threshold = (float)g_state.cfg.cursor_acceleration_threshold;
            dx *= sx;
            dy *= sy;
            dx *= sens;
            dy *= sens;
            if (accel_factor > 0.0f && accel_threshold > 0.0f) {
                float adx = fabsf(dx);
                if (adx > accel_threshold)
                    dx = adx * accel_factor * (dx > 0 ? 1.0f : -1.0f);
                float ady = fabsf(dy);
                if (ady > accel_threshold)
                    dy = ady * accel_factor * (dy > 0 ? 1.0f : -1.0f);
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
void handle_gesture_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "handle_gesture_up ptr=%d", f->ptr_id);
    int pi = (uint32_t)f->ptr_id % MAX_FINGERS;
    TrackedButtons* tb = &g_state.tracked[pi];
    bool had_tracked = false;
    if (g_state.cfg.caps_has_track_hover_buttons) {
        if (tb->count > 0) {
            if (tb->count > 1) {
                for (int j = 0; j < tb->count; j++) {
                    int idx = tb->element_indices[j];
                    if (idx >= 0 && idx < g_state.element_count) {
                        TouchElement* e = &g_state.elements[idx];
                        handle_element_up(e, x, y, time_ms, result);
                        if (__builtin_expect(e->gesture_swipe_triggered, 0))
                            release_bindings_list(result, e->element_gesture, e->element_gesture_count);
                        if (__builtin_expect(e->gesture_long_press_triggered, 0))
                            release_bindings_list(result, e->element_long_press, e->element_long_press_count);
                        e->long_press_arm = false;
                        e->gesture_long_press_triggered = false;
                        e->gesture_swipe_triggered = false;
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
                    if (!element_is_toggle_active(e)) {
                        e->visual_active = false;
                    }
                }
                had_tracked = true;
            }
            memset(tb, 0, sizeof(TrackedButtons));
        }
    }
    g_state.hovered_element_per_ptr[pi] = -1;

    TouchElement* elements = g_state.elements;
    int element_count = g_state.element_count;
    if (g_state.cfg.caps_has_element_toggle) {
        for (int i = 0; i < element_count; i++) {
            if (!elements[i].cached_has_toggle) continue;
            elements[i].gesture_timer_armed = false;
        }
    }

    bool had_element = false;
    for (int i = 0; i < element_count; i++) {
        if (__builtin_expect(elements[i].current_ptr_id == f->ptr_id, 0)) {
            handle_element_up(&elements[i], x, y, time_ms, result);
            had_element = true;
        }
    }
    if (had_tracked || had_element) {
        g_state.main_ptr_id = -1;
        f->active = false;
        return;
    }

    // Gesture up: skip gesture-specific handling if no gestures in current mode
    if (!current_mode_has_gestures() && !__builtin_expect(g_state.gesture_double_tap_waiting, 0)
        && !__builtin_expect(g_state.gesture_post_double_tap_drag, 0)
        && !__builtin_expect(g_state.second_double_tap_waiting, 0)) {
        f->active = false;
        g_state.main_ptr_id = -1;
        return;
    }

    // TS-specific up handling
    if (g_state.cfg.is_ts) {
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
            touchpad_finger_up(f, result, time_ms);
            cleanup_second_finger_up(result);
        } else {
            f->active = false;
        }
    } else {
        // TP up handling
        // Clear pending resume if main finger lifts during drag-pause
        if (f->ptr_id == g_state.gesture_main_ptr_id) {
            f->pending_resume_action_count = 0;
        }
        touchpad_finger_up(f, result, time_ms);

        // Restore interrupted first-finger drag on second-finger up
        if (f->is_second_finger) {
            cleanup_second_finger_up(result);
        }

        schedule_pending_release(&g_state.pending_left_release_time, &g_state.pending_left_release_ptr_id, &g_state.finger_pointer_left, f->ptr_id, time_ms);
        schedule_pending_release(&g_state.pending_right_release_time, &g_state.pending_right_release_ptr_id, &g_state.finger_pointer_right, f->ptr_id, time_ms);
    }

    g_state.main_ptr_id = -1;
}
