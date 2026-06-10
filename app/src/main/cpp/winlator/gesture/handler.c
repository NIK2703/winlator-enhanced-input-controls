#include "../touch_processor_internal.h"
#include "../activation_mode.h"

#define DELAYED_RELEASE_MS 30

static inline void gesture_repress_saved_bindings(TouchElement* e, bool had_gest_swipe, bool had_gest_lp, TouchActionResult* restrict result);

static inline void gesture_restore_first_button_state(TouchFinger* f, int pi, int saved_hovered_idx,
    bool first_btn_has_gesture, bool first_btn_lp_arm, bool first_btn_gest_swipe,
    bool first_btn_gest_lp, bool first_btn_gest_timer,
    TrackedButtons* tb, TouchActionResult* restrict result)
{
    if (!first_btn_has_gesture || saved_hovered_idx < 0
        || saved_hovered_idx >= g_state.element_count) return;
    int current_hovered = g_state.hovered_element_per_ptr[pi];
    if (current_hovered == saved_hovered_idx) return;
    TouchElement* prev = &g_state.elements[saved_hovered_idx];
    if (prev != &g_state.elements[tb->element_indices[0]]) return;
#ifndef NDEBUG
    __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "gesture_HOVER restore lp_arm=%d gest_lp=%d gest_timer=%d",
        first_btn_lp_arm, first_btn_gest_lp, first_btn_gest_timer);
#endif
    prev->long_press_arm = first_btn_lp_arm;
    if (first_btn_gest_swipe || first_btn_gest_lp) {
        gesture_repress_saved_bindings(prev, first_btn_gest_swipe, first_btn_gest_lp, result);
        prev->current_ptr_id = f->ptr_id;
        prev->engaged = true;
        prev->visual_active = true;
    }
    prev->gesture_swipe_triggered = first_btn_gest_swipe;
    prev->gesture_long_press_triggered = first_btn_gest_lp;
    prev->gesture_timer_armed = first_btn_gest_timer;
}

__attribute__((hot))
static inline bool check_confirm_dt_waiting(TouchFinger* restrict f, TouchActionResult* restrict result,
    TouchFinger* restrict main_finger, bool use_main_bindings)
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
        int n = g_state.gesture_held_count < MAX_DEFERRED_BINDINGS ? g_state.gesture_held_count : MAX_DEFERRED_BINDINGS;
        memcpy(main_finger->pending_resume_action, g_state.gesture_held_actions, n * sizeof(TouchBinding));
        main_finger->pending_resume_action_count = n;
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
__attribute__((hot))
void handle_gesture_down(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    f->gesture_activated_in_touch = false;
    g_state.main_ptr_id = f->ptr_id;

    g_state.passthrough_active = false;
    bool found_lock = false;
    bool handled = false;
    TouchElement* elements = g_state.elements;
    int element_count = g_state.element_count;
    const bool has_mouse_left = g_state.cfg.caps_has_mouse_left_element;
    const bool has_passthrough = g_state.cfg.caps_has_passthrough_elements;
    TouchFinger* main_finger = NULL;
    TouchElement* btn = NULL;
    if (has_mouse_left && __builtin_expect(g_state.pointer_left_enabled, 1)) {
        for (int i = 0; i < element_count; i++) {
            if (elements[i].bindings[0].type == BINDING_MOUSE_LEFT) {
                g_state.pointer_left_enabled = false;
                break;
            }
        }
    }

    // Grid-accelerated hit-test (3x3 cells around the touch point)
    if (g_state.grid_cell_w > 0.0f && g_state.grid_cell_h > 0.0f) {
        int cx = (int)((x - g_state.grid_min_x) * g_state.grid_inv_cell_w);
        int cy = (int)((y - g_state.grid_min_y) * g_state.grid_inv_cell_h);
        if (cx >= 0 && cx < GRID_COLS && cy >= 0 && cy < GRID_ROWS) {
            int start_cx = cx - 1 < 0 ? 0 : cx - 1;
            int end_cx = cx + 1 >= GRID_COLS ? GRID_COLS - 1 : cx + 1;
            int start_cy = cy - 1 < 0 ? 0 : cy - 1;
            int end_cy = cy + 1 >= GRID_ROWS ? GRID_ROWS - 1 : cy + 1;
            for (int r = start_cy; r <= end_cy; r++) {
                for (int c = start_cx; c <= end_cx; c++) {
                    int cell_idx = r * GRID_COLS + c;
                    for (int j = g_state.grid_cell_start[cell_idx]; j < g_state.grid_cell_start[cell_idx + 1]; j++) {
                        TouchElement* pe = &g_state.elements[g_state.grid_cell_to_elems[j]];
                        if (!point_in_element(x, y, pe)) continue;

                        if (has_passthrough && __builtin_expect(pe->passthrough_touch, 0))
                            g_state.passthrough_active = true;

                        ActivationMode am = pe->activation_mode;
                        if (__builtin_expect(am == ACTIVATION_LOCK, 0)) {
                            handle_element_down(pe, f->ptr_id, x, y, time_ms, result);
                            found_lock = true;
                        } else if (__builtin_expect(pe->type != ELEM_BUTTON, 0)) {
                            handle_element_down(pe, f->ptr_id, x, y, time_ms, result);
                            if (pe->engaged && !pe->passthrough_touch) handled = true;
                        } else if (btn == NULL) {
                            btn = pe;
                        }
                    }
                }
            }
        }
    }

    // Fallback: linear scan for elements missed by the grid (stale grid, edge cells, etc.)
    if (!found_lock && !handled) {
        for (int i = 0; i < element_count; i++) {
            TouchElement* pe = &elements[i];
            if (!point_in_element(x, y, pe)) continue;

            if (has_passthrough && __builtin_expect(pe->passthrough_touch, 0))
                g_state.passthrough_active = true;

            ActivationMode am = pe->activation_mode;
            if (__builtin_expect(am == ACTIVATION_LOCK, 0)) {
                if (!found_lock) {
                    handle_element_down(pe, f->ptr_id, x, y, time_ms, result);
                    found_lock = true;
                }
            } else if (__builtin_expect(pe->type != ELEM_BUTTON, 0)) {
                if (!handled) {
                    handle_element_down(pe, f->ptr_id, x, y, time_ms, result);
                    if (pe->engaged && !pe->passthrough_touch) handled = true;
                }
            } else if (btn == NULL) {
                btn = pe;
            }
        }
    }
    if (__builtin_expect(found_lock, 0)) return;

    // TRACK/HOVER BUTTON
    if (btn == NULL) btn = hit_test_element(x, y);
    if (btn && btn->type == ELEM_BUTTON) {
        if (btn->activation_mode == ACTIVATION_TRACK || btn->activation_mode == ACTIVATION_HOVER) {
            TrackedButtons* tb = get_tracked_buttons(f->ptr_id);
            bool already = is_tracked(tb, (int)(btn - g_state.elements));
            if (!already) {
                int16_t prev_ptr = btn->current_ptr_id;
                handle_element_down(btn, f->ptr_id, x, y, time_ms, result);
                // Toggle+AR elements in TRACK mode: don't add to tracked list so
                // slide-over section can re-enter on Move. Use gesture_timer_armed
                // to prevent immediate re-trigger instead.
                if (btn->cached_has_toggle && btn->cached_has_auto_repeat && btn->activation_mode == ACTIVATION_TRACK) {
                    btn->gesture_timer_armed = true;
                } else if (prev_ptr != f->ptr_id && btn->current_ptr_id == f->ptr_id && tb->count < MAX_TRACKED_PER_POINTER) {
                    tb->element_indices[tb->count++] = (int)(btn - g_state.elements);
                }
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
        main_finger = find_finger(g_state.gesture_main_ptr_id);
        if (!main_finger || !main_finger->active) {
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
                TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
                if (sf) {
                    sf->is_second_finger = true;
                    setup_second_finger_bindings(sf);
                }
                g_state.gesture_second_active = true;
            }
            return;
        }

        // was_second_deferred without DT — only restore if second finger is still active
        if (!__builtin_expect(g_state.gesture_double_tap_waiting, 0) && was_second_deferred && g_state.gesture_second_ptr_id >= 0) {
            TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
            if (sf) {
                sf->is_second_finger = true;
                setup_second_finger_bindings(sf);
            }
            g_state.gesture_second_active = true;
        }

        f->original_ptr_id = f->ptr_id;
        f->double_tap_original_id_set = true;
        update_ts_pointer(x, y, result);

        gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_DOWN, f->x, f->y);
        return;
    }

    // ============================================================
    // TS SECOND FINGER
    // ============================================================
    if (g_state.cfg.is_ts && is_second) {
        // Guard: 3rd+ finger while second is already active
        if (__builtin_expect(g_state.gesture_second_active, 0)) {
            f->state = GESTURE_STATE_IDLE;
            return;
        }
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

        TouchFinger* _mf = main_finger;
        if (check_confirm_dt_waiting(f, result, _mf, true)) {
            g_state.gesture_second_active = false;
            return;
        }

        save_pending_resume_action(main_finger);
        release_held_actions(result);
        gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_DOWN, f->x, f->y);
        return;
    }

    // ============================================================
    // TOUCHPAD
    // ============================================================
    if (is_first) {
        g_state.scrolling = false;
        g_state.scroll_accum_y = 0;
    } else if (is_second && !g_state.gesture_second_active) {
        save_pending_resume_action(main_finger);
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

    gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_DOWN, f->x, f->y);
}

// ============================================================
// handle_toggle_entry — unified toggle entry logic for slide-over
// ============================================================
static inline void gesture_repress_saved_bindings(
    TouchElement* e, 
    bool had_gest_swipe, bool had_gest_lp,
    TouchActionResult* restrict result)
{
    if (had_gest_swipe && !e->gesture_swipe_triggered && !e->gesture_toggled)
        press_bindings_list(result, e->element_gesture, e->element_gesture_count);
    if (had_gest_lp && !e->gesture_long_press_triggered && !e->lp_toggled)
        press_bindings_list(result, e->element_long_press, e->element_long_press_count);
}

// ============================================================
static inline void handle_toggle_entry(TouchElement* e, TouchActionResult* restrict result) {
    if (element_is_toggle_active(e)) {
        // Don't deselect via slide-over if a gesture/long-press toggle is still active
        if (element_has_gesture_toggle(e)) {
            e->visual_active = true;
            mark_element_dirty(e);
            return;
        }
        // Release stale gesture/long-press toggles that lingered after finger-up
        release_toggled_alternate_bindings(e, result, true);
        // Release all bindings across all slots
        for (int k = 0; k < 4; k++) {
            if (e->bindings[k].type == BINDING_NONE) continue;
            release_binding(result, &e->bindings[k]);
        }
        e->selected = false;
        e->visual_active = false;
        mark_element_dirty(e);
        return;
    }
    if (element_has_primary(e))
        press_binding(result, &e->bindings[0], true);
    e->selected = true;
    e->visual_active = true;
    e->gesture_timer_armed = true;
    mark_element_dirty(e);
}

// ============================================================
// handle_gesture_move
// ============================================================
__attribute__((hot))
void handle_gesture_move(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
#ifndef NDEBUG
    __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "gesture_MOVE ptr=%d x=%.0f y=%.0f engaged_cnt=%d", f->ptr_id, x, y, f->active);
#endif

    const bool is_ts = g_state.cfg.is_ts;
    const bool is_tp = g_state.cfg.is_tp;
    const bool has_track_hover = g_state.cfg.caps_has_track_hover_buttons;
    const bool has_element_toggle = g_state.cfg.caps_has_element_toggle;

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
    int saved_hovered_idx = -1;
    if (has_track_hover) {
        int saved_pi = (uint32_t)f->ptr_id % MAX_FINGERS;
        saved_hovered_idx = g_state.hovered_element_per_ptr[saved_pi];
        TrackedButtons* saved_tb = get_tracked_buttons(f->ptr_id);
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
    {
        uint8_t cnt = f->engaged_elem_count;
        bool skip_buttons = (g_state.activation_mode == ACTIVATION_TRACK || g_state.activation_mode == ACTIVATION_HOVER);
        for (uint8_t i = 0; i < cnt; i++) {
            TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
            if (__builtin_expect(e->current_ptr_id == f->ptr_id, 1) && __builtin_expect(e->passthrough_touch, 0))
                continue;
            if (e->current_ptr_id == f->ptr_id) {
                if (__builtin_expect(skip_buttons && e->type == ELEM_BUTTON, 0))
                    continue;
                handle_element_move(e, x, y, time_ms, result);
                had_element_move = true;
            }
        }
    }

    // If any engaged non-button element has moved (trackpad, stick, dpad, range_button),
    // return immediately to prevent button activation from toggle slide-over or
    // HOVER/TRACK tracking. The finger stays in control of its original element
    // (e.g. trackpad cursor continues moving when finger slides over a button).
    if (__builtin_expect(had_element_move, 0)) {
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // Determine if finger should skip element activation:
    // 1. Finger originated in gesture zone (state > IDLE) — gesture FSM is running
    // 2. Finger has passthrough elements engaged — passthrough passes through to gesture zone
    // In both cases, buttons under the finger must NOT be activated.
    bool is_gesture_zone_finger = __builtin_expect(f->state > GESTURE_STATE_IDLE, 0);
    bool has_passthrough_engagement = false;
    if (__builtin_expect(g_state.cfg.caps_has_passthrough_elements, 0)) {
        uint8_t _cnt = f->engaged_elem_count;
        for (uint8_t _i = 0; _i < _cnt; _i++) {
            if (g_state.elements[f->engaged_elem_indices[_i]].passthrough_touch) {
                has_passthrough_engagement = true;
                break;
            }
        }
    }
    bool skip_element_activation = is_gesture_zone_finger || has_passthrough_engagement;

    // Toggle switch slide-over: handle toggles under finger regardless of tb->count
    // (works even when finger starts on empty space, but NOT for gesture/passthrough fingers)
    if (has_element_toggle && !skip_element_activation) {
        if (!ht_elem_valid) { ht_elem = hit_test_element(x, y); ht_elem_valid = true; }
        TouchElement* toggle_btn = ht_elem;
        if (toggle_btn && toggle_btn->type == ELEM_BUTTON && toggle_btn->cached_has_toggle) {
            ActivationMode mode = g_state.activation_mode;
            if (mode == ACTIVATION_TRACK || mode == ACTIVATION_HOVER) {
                int pi = (uint32_t)f->ptr_id % MAX_FINGERS;
                TrackedButtons* tb = &g_state.tracked[pi];
                bool already_tracked = is_tracked(tb, (int)(toggle_btn - g_state.elements));
                if (!already_tracked && !toggle_btn->gesture_timer_armed) {
                    if (toggle_btn->cached_has_auto_repeat) {
                        // Toggle+AR: use full element path (all 4 slots)
                        if (mode == ACTIVATION_TRACK) {
                            // Must clear engagement guard before re-entry, otherwise
                            // handle_element_down (shared.c:159) is a no-op and the
                            // toggle can never be deselected via slide-over.
                            if (toggle_btn->current_ptr_id >= 0) {
                                int16_t eidx = (int16_t)(toggle_btn - g_state.elements);
                                finger_remove_engaged(f, eidx);
                                toggle_btn->current_ptr_id = -1;
                                toggle_btn->engaged = false;
                            }
                            handle_element_down(toggle_btn, f->ptr_id, x, y, time_ms, result);
                            toggle_btn->gesture_timer_armed = true;
                            mark_element_dirty(toggle_btn);
                        }
                        // HOVER toggle+AR: skip — handled by HOVER section below
                    } else if (mode != ACTIVATION_HOVER) {
                        // Pure toggle (non-AR): unified entry (HOVER handled by HOVER section below)
#ifndef NDEBUG
                        int tbi = (int)(toggle_btn - g_state.elements);
                        if (!element_is_toggle_active(toggle_btn))
                            __android_log_print(ANDROID_LOG_WARN, "Winlator_Hover", "gesture_toggle[%d] SELECT", tbi);
#endif
                        handle_toggle_entry(toggle_btn, result);
                    }
                }
            }
        } else {
            // Finger not on a toggle — reset gates so re-entry can toggle again
            for (int i = 0; i < g_state.element_count; i++)
                if (g_state.elements[i].cached_has_toggle && g_state.elements[i].current_ptr_id == f->ptr_id)
                    g_state.elements[i].gesture_timer_armed = false;
        }
    }

    // TRACK/HOVER button tracking (unified via activation_mode)
    if (has_track_hover && !skip_element_activation) {
        int pi = (uint32_t)f->ptr_id % MAX_FINGERS;
        TrackedButtons* tb = get_tracked_buttons(f->ptr_id);
        ActivationMode mode = g_state.activation_mode;

        if (mode == ACTIVATION_HOVER) {
            int prev_idx = g_state.hovered_element_per_ptr[pi];
            // Use HOVER params but with skip_reentry_down=true so unified
            // skips handle_element_down on re-entry; the re-entry block below
            // handles the full release/re-press cycle with gesture state.
            ActivationModeParams hover_params = ACTIVATION_MODE_HOVER;
            hover_params.skip_reentry_down = true;
            int curr_idx = activation_mode_move_buttons(f->ptr_id, x, y, time_ms, result, &hover_params);

            bool was_transition = (curr_idx != prev_idx);

            // Re-entry of first tracked button: preserve gesture toggle state
            // across re-init (handle_element_down clears gesture_toggled/lp_toggled).
            if (was_transition && curr_idx >= 0 && tb->count > 0
                && curr_idx == tb->element_indices[0])
            {
                TouchElement* curr = &g_state.elements[curr_idx];
                bool already = is_tracked(tb, curr_idx);
                if (already && !curr->gesture_timer_armed) {
                    if (!curr->cached_has_toggle || element_is_toggle_active(curr)) {
                        bool hgs = curr->gesture_swipe_triggered;
                        bool hgl = curr->gesture_long_press_triggered;
                        bool hgt = curr->gesture_toggled;
                        bool hlt = curr->lp_toggled;
                        bool hla = curr->long_press_arm;
                        bool hgtimer = curr->gesture_timer_armed;
                        if (hgs && !hgt)
                            release_bindings_list(result, curr->element_gesture, curr->element_gesture_count);
                        if (hgl && !hlt)
                            release_bindings_list(result, curr->element_long_press, curr->element_long_press_count);
                        curr->current_ptr_id = -1;
                        curr->engaged = false;
                        handle_element_down(curr, f->ptr_id, x, y, time_ms, result);
                        if (hgt)
                            press_bindings_list(result, curr->element_gesture, curr->element_gesture_count);
                        if (hlt)
                            press_bindings_list(result, curr->element_long_press, curr->element_long_press_count);
                        if (hgs && !hgt)
                            press_bindings_list(result, curr->element_gesture, curr->element_gesture_count);
                        if (hgl && !hlt)
                            press_bindings_list(result, curr->element_long_press, curr->element_long_press_count);
                        curr->gesture_swipe_triggered = hgs;
                        curr->gesture_long_press_triggered = hgl;
                        curr->long_press_arm = hla;
                        curr->gesture_timer_armed = hgtimer;
                        curr->gesture_toggled = hgt;
                        curr->lp_toggled = hlt;
                    } else {
                        suppress_element_gestures(curr, result);
                    }
                } else if (tb->count > 1 || prev_idx < 0) {
                    suppress_element_gestures(curr, result);
                    if (element_is_toggle_active(curr) && curr->cached_has_toggle)
                        curr->gesture_timer_armed = true;
                }
            }

            // Prevent gesture restore block from restoring state on PREV button
            // when transitioning away from it.
            if (was_transition && prev_idx >= 0 && prev_idx < g_state.element_count) {
                first_btn_gest_swipe = false;
                first_btn_gest_lp = false;
                first_btn_lp_arm = false;
                first_btn_gest_timer = false;
            }

            // Restore gesture state on first tracked button after transition
            gesture_restore_first_button_state(f, pi, saved_hovered_idx,
                first_btn_has_gesture, first_btn_lp_arm, first_btn_gest_swipe,
                first_btn_gest_lp, first_btn_gest_timer, tb, result);

            // Gesture-by-movement on first non-toggle button
            if (curr_idx >= 0 && tb->count > 0 && curr_idx == tb->element_indices[0]
                && !g_state.elements[curr_idx].cached_has_toggle) {
                handle_element_move(&g_state.elements[curr_idx], x, y, time_ms, result);
                had_element_move = true;
            }
        } else if (tb->count > 0) {
            // TRACK mode: accumulate buttons as finger slides
            activation_mode_move_buttons(f->ptr_id, x, y, time_ms, result, &ACTIVATION_MODE_TRACK);

            // Gesture restore
            gesture_restore_first_button_state(f, pi, saved_hovered_idx,
                first_btn_has_gesture, first_btn_lp_arm, first_btn_gest_swipe,
                first_btn_gest_lp, first_btn_gest_timer, tb, result);
        }
    }

    if (__builtin_expect(had_element_move, 0)) {
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // Skip gesture if tracked non-passthrough button
    if (has_track_hover) {
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
    if (f->ptr_id == g_state.gesture_main_ptr_id) {
        // TS cursor optimization (skip when competing D/Dd or L/Ld present —
        // they need gesture processing for press_on_drag on drag threshold)
        if (is_ts
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
        gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, dx, dy);

        // TS: main-finger movement triggers second-finger STD (single-tap-drag)
        // when the second finger is held and has STD configured.
        if (is_ts && g_state.gesture_second_active) {
            TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
            if (sf) {
                float sf_dx = x - g_state.gesture_second_main_ref_x;
                float sf_dy = y - g_state.gesture_second_main_ref_y;
                gesture_process_finger(sf, &g_ctx[(int)(sf - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, sf_dx, sf_dy);
            }
        }
        // TP: main-finger movement triggers second-finger drag binding
        // (Sd2/Dd2). The second finger hasn't moved (dx=0, dy=0);
        // gesture_branch internally aggregates with the main finger delta
        // for TP second-finger mode.
        if (is_tp && g_state.gesture_second_active) {
            TouchFinger* sf = find_finger(g_state.gesture_second_ptr_id);
            if (sf && sf->state >= GESTURE_STATE_TAP_WAITING
                && sf->state != GESTURE_STATE_DRAGGING) {
                gesture_process_finger(sf, &g_ctx[(int)(sf - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, 0.0f, 0.0f);
            }
        }
    }

    // TS second finger gesture move (own movement)
    if (is_ts
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
            gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, dx, dy);
    }

    // TS: update absolute pointer
    if (is_ts) {
        if (g_state.gesture_main_ptr_id < 0 || f->ptr_id == g_state.gesture_main_ptr_id)
            update_ts_pointer(x, y, result);
        f->last_x = x;
        f->last_y = y;
        return;
    }

    // TP second finger gesture move (aggregate two-finger drag threshold)
    if (is_tp
        && g_state.gesture_second_active
        && f->ptr_id == g_state.gesture_second_ptr_id
        && f->state >= GESTURE_STATE_TAP_WAITING
        && f->state != GESTURE_STATE_DRAGGING) {
        float dx = x - f->down_x;
        float dy = y - f->down_y;
        // Note: TP second-finger aggregation with main finger delta is handled
        // internally by gesture_branch's MOVE handler for is_second_finger + is_tp.
        uint32_t tp_second = GESTURE_MASK(GESTURE_SINGLE_2ND) | GESTURE_MASK(GESTURE_DOUBLE_2ND)
                           | GESTURE_MASK(GESTURE_SINGLE_DRAG_2ND) | GESTURE_MASK(GESTURE_DOUBLE_DRAG_2ND);
        if (g_state.cfg.caps_tp_mask & tp_second)
            gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_MOVE, dx, dy);
    }



    // TP: cursor movement
    int afc = active_finger_count();
    if (is_tp && !g_state.scrolling && afc <= 2) {
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
                int id = lrintf(dx);
                int jd = lrintf(dy);
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
__attribute__((hot))
void handle_gesture_up(TouchFinger* f, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Controls", "handle_gesture_up ptr=%d", f->ptr_id);
    int pi = (uint32_t)f->ptr_id % MAX_FINGERS;
    TrackedButtons* tb = &g_state.tracked[pi];
    bool had_tracked = false;
    if (g_state.cfg.caps_has_track_hover_buttons && tb->ptr_id == f->ptr_id && tb->count > 0) {
        had_tracked = activation_mode_up(f->ptr_id, x, y, time_ms, result,
            g_state.activation_mode == ACTIVATION_TRACK
                ? &ACTIVATION_MODE_TRACK
                : &ACTIVATION_MODE_HOVER);
    }
    // activation_mode_up for HOVER already clears hovered_element_per_ptr,
    // for TRACK does not. Gesture handler always clears unconditionally.
    g_state.hovered_element_per_ptr[pi] = -1;

    const int pid = f->ptr_id;
    bool had_element = false;
    TouchFinger* finger_up = find_finger(pid);
    if (__builtin_expect(finger_up != NULL, 1)) {
        uint8_t cnt = finger_up->engaged_elem_count;
        for (uint8_t i = 0; i < cnt; i++) {
            TouchElement* e = &g_state.elements[finger_up->engaged_elem_indices[i]];
            if (e->current_ptr_id == pid) {
                handle_element_up(e, x, y, time_ms, result);
                had_element = true;
            }
        }
        finger_up->engaged_elem_count = 0;
    }
    if (had_tracked || had_element) {
        // Element fingers must NEVER enter gesture states...
        g_state.main_ptr_id = -1;
        deactivate_finger(f);
        return;
    }

    // Gesture up: skip gesture-specific handling if no gestures in current mode
    if (!current_mode_has_gestures() && !__builtin_expect(g_state.gesture_double_tap_waiting, 0)
        && !__builtin_expect(g_state.gesture_post_double_tap_drag, 0)
        && !__builtin_expect(g_state.second_double_tap_waiting, 0)) {
        deactivate_finger(f);
        g_state.main_ptr_id = -1;
        return;
    }

    // TS-specific up handling
    if (g_state.cfg.is_ts) {
        if (f->ptr_id == g_state.gesture_main_ptr_id) {
        g_state.gesture_deferred_second_finger_tap = g_state.gesture_second_active;
        f->pending_resume_action_count = 0;
        f->double_tap_original_id_set = false;
        gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, f->x, f->y);
        } else if (g_state.second_double_tap_waiting) {
            gesture_clear_second_finger_state();
            deactivate_finger(f);
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
            deactivate_finger(f);
        } else if (g_state.gesture_second_active && f->ptr_id != g_state.gesture_main_ptr_id) {
            gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, f->x, f->y);
            cleanup_second_finger_up(result);
        } else if (f->state == GESTURE_STATE_IDLE
                   && (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag)) {
            gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, f->x, f->y);
        } else {
            deactivate_finger(f);
        }
    } else {
        // TP up handling
        // Clear pending resume if main finger lifts during drag-pause
        if (f->ptr_id == g_state.gesture_main_ptr_id) {
            f->pending_resume_action_count = 0;
        }
        gesture_process_finger(f, &g_ctx[(int)(f - g_state.fingers)], result, time_ms, GESTURE_EVENT_UP, f->x, f->y);

        // Restore interrupted first-finger drag on second-finger up
        if (f->is_second_finger) {
            cleanup_second_finger_up(result);
        }

        schedule_pending_release(&g_state.pending_left_release_time, &g_state.pending_left_release_ptr_id, &g_state.finger_pointer_left, f->ptr_id, time_ms);
        schedule_pending_release(&g_state.pending_right_release_time, &g_state.pending_right_release_ptr_id, &g_state.finger_pointer_right, f->ptr_id, time_ms);
    }

    g_state.main_ptr_id = -1;
}
