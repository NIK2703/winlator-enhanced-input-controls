#include "../touch_processor_internal.h"
#include <android/log.h>

#define LOG_TAG "Gesture"

// ---- helpers ----
void on_drag_start(TouchFinger* f) {
    f->single_tap_hold_delay_ms = 0;
    g_state.second_tap_fallback_count = 0;
    g_state.pending_second_double_count = 0;
    g_state.gesture_deferred_tap_count = 0;
    g_state.gesture_pending_double_count = 0;
    g_state.gesture_deferred_second_finger_tap = false;
}

bool gesture_is_within_tap_distance(float x, float y) {
    return fabsf(x - g_state.gesture_last_tap_up_x) <= g_state.cfg.double_tap_distance_px
        && fabsf(y - g_state.gesture_last_tap_up_y) <= g_state.cfg.double_tap_distance_px;
}

void gesture_cancel_double_tap_wait(TouchActionResult* result) {
    g_state.gesture_double_tap_waiting = false;
    if (g_state.gesture_deferred_tap_count > 0) {
        execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
        g_state.gesture_deferred_tap_count = 0;
    }
    g_state.gesture_pending_deferred_double_count = 0;
    if (g_state.gesture_main_ptr_id >= 0) {
        for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
            TouchFinger* mf = &g_state.fingers[_fi];
            if (mf->active && mf->ptr_id == g_state.gesture_main_ptr_id) {
                mf->state = GESTURE_STATE_IDLE;
                break;
            }
        }
    }
}

// ---- check_start_drag ----
void check_start_drag(TouchFinger* f, float dx, float dy, TouchActionResult* result) {
    if (f->state != GESTURE_STATE_TAP_WAITING && f->state != GESTURE_STATE_LONG_PRESSING) {
        __android_log_print(ANDROID_LOG_INFO, "Gesture", "DRAG skip: state=%d", f->state);
        return;
    }
    if (fabsf(dx) <= g_state.cfg.drag_threshold_px && fabsf(dy) <= g_state.cfg.drag_threshold_px) return;

    __android_log_print(ANDROID_LOG_INFO, "Gesture", "DRAG enter: ptr=%d is_2nd=%d post_dtd=%d gsa=%d state=%d",
        f->ptr_id, f->is_second_finger, g_state.gesture_post_double_tap_drag, g_state.gesture_second_active, f->state);

    g_state.gesture_handler_active = false;
    const FingerBindings* fb = &f->bindings;
    const TouchBinding* drag_binding = NULL;
    int drag_count = 0;

    if (f->state == GESTURE_STATE_LONG_PRESSING) {
        g_state.gesture_pending_deferred_long_press_count = 0;
        if (f->cached_has_active_long_press_drag) {
            drag_binding = fb->long_press_drag; drag_count = fb->long_press_drag_count;
        } else if (f->cached_has_active_long_press) {
            drag_binding = fb->long_press; drag_count = fb->long_press_count;
        } else {
            return;
        }
    } else if (g_state.gesture_post_double_tap_drag) {
        bool use_second = g_state.gesture_second_active;
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            if (use_second) {
                if (g_state.cfg.ts_double_drag_2nd_count > 0) {
                    drag_binding = g_state.cfg.ts_double_drag_2nd; drag_count = g_state.cfg.ts_double_drag_2nd_count;
                } else if (g_state.cfg.ts_double_2nd_count > 0) {
                    drag_binding = g_state.cfg.ts_double_2nd; drag_count = g_state.cfg.ts_double_2nd_count;
                } else if (g_state.cfg.ts_single_2nd_count > 0) {
                    drag_binding = g_state.cfg.ts_single_2nd; drag_count = g_state.cfg.ts_single_2nd_count;
                } else {
                    g_state.gesture_post_double_tap_drag = false; return;
                }
            } else {
                if (g_state.cfg.ts_double_tap_drag_count > 0) {
                    drag_binding = g_state.cfg.ts_double_tap_drag; drag_count = g_state.cfg.ts_double_tap_drag_count;
                } else if (g_state.cfg.ts_double_tap_count > 0) {
                    drag_binding = g_state.cfg.ts_double_tap; drag_count = g_state.cfg.ts_double_tap_count;
                } else if (g_state.cfg.ts_single_tap_count > 0) {
                    drag_binding = g_state.cfg.ts_single_tap; drag_count = g_state.cfg.ts_single_tap_count;
                } else {
                    g_state.gesture_post_double_tap_drag = false; return;
                }
            }
        } else {
            if (use_second) {
                if (g_state.cfg.tp_double_drag_2nd_count > 0) {
                    drag_binding = g_state.cfg.tp_double_drag_2nd; drag_count = g_state.cfg.tp_double_drag_2nd_count;
                } else if (g_state.cfg.tp_double_2nd_count > 0) {
                    drag_binding = g_state.cfg.tp_double_2nd; drag_count = g_state.cfg.tp_double_2nd_count;
                } else if (g_state.cfg.tp_single_2nd_count > 0) {
                    drag_binding = g_state.cfg.tp_single_2nd; drag_count = g_state.cfg.tp_single_2nd_count;
                } else {
                    g_state.gesture_post_double_tap_drag = false; return;
                }
            } else {
                if (g_state.cfg.tp_double_tap_drag_count > 0) {
                    drag_binding = g_state.cfg.tp_double_tap_drag; drag_count = g_state.cfg.tp_double_tap_drag_count;
                } else if (g_state.cfg.tp_double_tap_count > 0) {
                    drag_binding = g_state.cfg.tp_double_tap; drag_count = g_state.cfg.tp_double_tap_count;
                } else if (g_state.cfg.tp_single_tap_count > 0) {
                    drag_binding = g_state.cfg.tp_single_tap; drag_count = g_state.cfg.tp_single_tap_count;
                } else {
                    g_state.gesture_post_double_tap_drag = false; return;
                }
            }
        }
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_deferred_tap_count = 0;
        // When use_second, the post-dtd drag uses second-finger bindings.
        // Mark the second finger DRAGGING too, otherwise on the next move
        // event the second finger's own check (non-post-dtd) will re-enter
        // check_start_drag and start STD instead of Dd.
        if (use_second) {
            for (int _si = 0; _si < MAX_FINGERS; _si++) {
                TouchFinger* _sf = &g_state.fingers[_si];
                if (_sf->active && _sf->ptr_id == g_state.gesture_second_ptr_id && _sf != f) {
                    on_drag_start(_sf);
                    _sf->state = GESTURE_STATE_DRAGGING;
                    break;
                }
            }
        }
    } else if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
        if (!g_state.cfg.caps_has_drag_bindings) { __android_log_print(ANDROID_LOG_INFO, "Gesture", "CSD TS: no drag bindings"); return; }
        // Only use 2nd-finger bindings when the second finger itself is moving,
        // not when the main finger moves while a second finger happens to be present.
        // Post-double-tap drag (handled above) is the only context where the main
        // finger should use 2nd-finger bindings.
        // For second-finger direct drag, only use Sd (single-tap-drag).
        // Dd (double-tap-drag) is exclusive to the SDTW->post_dtd main-finger drag path,
        // and must not set gesture_is_action_held here or it will block SDTW on lift.
        if (g_state.gesture_second_active && f->is_second_finger) {
            __android_log_print(ANDROID_LOG_INFO, "Gesture", "CSD TS SF: ts_sd_2nd=%d", g_state.cfg.ts_single_drag_2nd_count);
            if (g_state.cfg.ts_single_drag_2nd_count > 0) {
                drag_binding = g_state.cfg.ts_single_drag_2nd; drag_count = g_state.cfg.ts_single_drag_2nd_count;
            } else {
                return;
            }
        } else if (f->cached_has_active_single_tap_drag) {
            drag_binding = fb->single_tap_drag; drag_count = fb->single_tap_drag_count;
        } else {
            return;
        }
    } else if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && g_state.gesture_second_active && f->is_second_finger) {
        if (!g_state.cfg.caps_has_drag_bindings) return;
        // Dd is exclusive to the post_dtd branch above — only STD here.
        if (g_state.cfg.tp_single_drag_2nd_count > 0) {
            drag_binding = g_state.cfg.tp_single_drag_2nd; drag_count = g_state.cfg.tp_single_drag_2nd_count;
        } else {
            return;
        }
    } else if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHPAD && !g_state.gesture_second_active) {
        if (!g_state.cfg.caps_has_drag_bindings) return;
        if (f->cached_has_active_single_tap_drag) {
            drag_binding = fb->single_tap_drag; drag_count = fb->single_tap_drag_count;
        } else {
            return;
        }
    } else {
        return;
    }

    bool same_as_held = g_state.gesture_is_action_held;
    if (same_as_held && drag_binding && g_state.gesture_held_count > 0) {
        same_as_held = (drag_count == g_state.gesture_held_count);
        if (same_as_held) {
            for (int _i = 0; _i < drag_count && _i < g_state.gesture_held_count; _i++) {
                if (drag_binding[_i].type != g_state.gesture_held_actions[_i].type ||
                    drag_binding[_i].keycode != g_state.gesture_held_actions[_i].keycode) {
                    same_as_held = false; break;
                }
            }
        }
    }

    if (drag_binding) {
        __android_log_print(ANDROID_LOG_INFO, "Gesture", "DRAG bind: ptr=%d type=%d count=%d same=%d held=%d",
            f->ptr_id, drag_binding[0].type, drag_count, same_as_held, g_state.gesture_is_action_held);
    }

    if (!same_as_held) {
        release_held_actions(result);
        hold_actions(result, drag_binding, drag_count);
    }

    g_state.gesture_pending_double_count = 0;
    g_state.gesture_deferred_tap_count = 0;

    on_drag_start(f);
    f->state = GESTURE_STATE_DRAGGING;
    __android_log_print(ANDROID_LOG_INFO, "Gesture", "DRAG done: ptr=%d state=DRAGGING gha=%d held=%d",
        f->ptr_id, g_state.gesture_handler_active, g_state.gesture_is_action_held);
}

// ---- gesture_tick ----
void gesture_tick(uint64_t time_ms, TouchActionResult* result) {
    if (!g_state.cfg.caps_has_gesture_bindings) return;

    for (int i = 0; i < MAX_FINGERS; i++) {
        TouchFinger* f = &g_state.fingers[i];
        if (!f->active) continue;
        if (f->state != GESTURE_STATE_TAP_WAITING) continue;

        // LP timer
        if (f->cached_has_long_press_timer) {
            // LP fires only if finger never moved beyond drag_threshold during
            // the entire timeout, AND no other action (e.g. DT) is currently held.
            bool cancel_lp = f->cached_has_moved_beyond_threshold
                          || g_state.gesture_is_action_held;

            if (!cancel_lp && time_ms - f->down_time_ms >= g_state.cfg.long_press_timeout_ms) {
                g_state.gesture_handler_active = false;
                g_state.second_tap_fallback_count = 0;
                memset(g_state.second_tap_fallback, 0, sizeof(g_state.second_tap_fallback));
                g_state.pending_second_double_count = 0;
                memset(g_state.pending_second_double, 0, sizeof(g_state.pending_second_double));

                if (f->cached_has_active_long_press_drag) {
                    g_state.gesture_pending_deferred_long_press_count = f->bindings.long_press_count;
                    for (int _li = 0; _li < f->bindings.long_press_count && _li < 8; _li++)
                        g_state.gesture_pending_deferred_long_press[_li] = f->bindings.long_press[_li];
                    f->single_tap_hold_delay_ms = 0;
                    f->state = GESTURE_STATE_LONG_PRESSING;
                } else if (f->cached_can_hold_long_press) {
                    hold_actions(result, f->bindings.long_press, f->bindings.long_press_count);
                    f->single_tap_hold_delay_ms = 0;
                    f->state = GESTURE_STATE_LONG_PRESSING;
                } else {
                    execute_actions(result, f->bindings.long_press, f->bindings.long_press_count);
                    if (f->cached_has_active_single_tap && !g_state.gesture_is_action_held)
                        execute_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
                    f->single_tap_hold_delay_ms = 0;
                    f->cached_has_long_press_timer = false;
                    f->cached_has_active_long_press = false;
                    f->cached_can_hold_long_press = false;
                    f->state = GESTURE_STATE_LONG_PRESSING;
                }
                if (g_state.cfg.gesture_long_press_haptic > 0)
                    add_action(result, ACT_HAPTIC, g_state.cfg.gesture_long_press_haptic, 0, 0);
            }
        }

        // S hold timer (TS finger-down hold)
        if (f->single_tap_hold_delay_ms > 0
            && time_ms - f->single_tap_hold_timer >= f->single_tap_hold_delay_ms) {
            f->single_tap_hold_delay_ms = 0;
            if (!g_state.gesture_is_action_held && !f->cached_has_moved_beyond_threshold)
                hold_actions(result, f->bindings.single_tap, f->bindings.single_tap_count);
            g_state.gesture_deferred_tap_count = 0;
            g_state.gesture_pending_double_count = 0;
            // R1 fix: do NOT clear pending_deferred_double_count here.
            // It must survive S hold timer to fire on finger-up.
            // When the delayed hold fires for a second finger, clear SDTW state
            if (f->is_second_finger) {
                g_state.second_tap_fallback_count = 0;
                g_state.second_double_tap_waiting = false;
            }
        }
    }

    // caps gate
    if (!g_state.cfg.caps_has_double_tap) {
        g_state.second_double_tap_waiting = false;
        g_state.gesture_double_tap_waiting = false;
        g_state.gesture_pending_deferred_double_count = 0;
    }

    // SDTW timeout
    if (g_state.second_double_tap_waiting
        && time_ms - g_state.second_tap_fallback_time >= g_state.cfg.double_tap_timeout_ms) {
        g_state.second_double_tap_waiting = false;
        __android_log_print(ANDROID_LOG_INFO, "Gesture", "STATE sdtw=0 SDTW_timeout");
        if (g_state.second_tap_fallback_count > 0) {
            execute_actions(result, g_state.second_tap_fallback, g_state.second_tap_fallback_count);
            g_state.second_tap_fallback_count = 0;
        }
        if (g_state.gesture_main_ptr_id >= 0) {
            for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
                TouchFinger* mf = &g_state.fingers[_fi];
                if (mf->active && mf->ptr_id == g_state.gesture_main_ptr_id) {
                    // R6 fix: don't set to IDLE if DRAGGING
                    if (mf->state != GESTURE_STATE_DRAGGING)
                        mf->state = GESTURE_STATE_IDLE;
                    break;
                }
            }
        }
    }

    // DT timeout
    if (g_state.gesture_double_tap_waiting &&
        time_ms - g_state.gesture_double_tap_start_time >= g_state.cfg.double_tap_timeout_ms) {
        g_state.gesture_double_tap_waiting = false;
        if (g_state.gesture_deferred_tap_count > 0) {
            execute_actions(result, g_state.gesture_deferred_tap, g_state.gesture_deferred_tap_count);
            g_state.gesture_deferred_tap_count = 0;
        } else if (g_state.gesture_pending_deferred_double_count > 0) {
            execute_actions(result, g_state.gesture_pending_deferred_double,
                            g_state.gesture_pending_deferred_double_count);
        }
        g_state.gesture_pending_deferred_double_count = 0;

        if (g_state.gesture_main_ptr_id >= 0) {
            for (int _fi = 0; _fi < MAX_FINGERS; _fi++) {
                TouchFinger* mf = &g_state.fingers[_fi];
                if (mf->active && mf->ptr_id == g_state.gesture_main_ptr_id) {
                    mf->state = GESTURE_STATE_IDLE;
                    break;
                }
            }
        }
    }
}

// ---- handle_tap_up ----
void handle_tap_up(TouchFinger* f, TouchActionResult* result, uint64_t time_ms) {
    if (!g_state.cfg.caps_has_gesture_bindings && !g_state.gesture_double_tap_waiting && !g_state.gesture_post_double_tap_drag) {
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    if (g_state.cfg.single_tap_delay_ms > 0) {
        struct timespec ts_delay;
        ts_delay.tv_sec = g_state.cfg.single_tap_delay_ms / 1000;
        ts_delay.tv_nsec = (g_state.cfg.single_tap_delay_ms % 1000) * 1000000L;
        nanosleep(&ts_delay, NULL);
    }

    const FingerBindings* fb = &f->bindings;

    int active_single_count = fb->single_tap_count;
    const TouchBinding* active_single = fb->single_tap;
    int active_double_count = fb->double_tap_count;
    const TouchBinding* active_double = fb->double_tap;
    bool active_has_single_tap_drag = f->cached_has_active_single_tap_drag;
    bool active_has_double_tap = f->cached_has_active_double_tap;
    bool active_has_double_tap_drag = f->cached_has_active_double_tap_drag;

    if (f->is_second_finger && g_state.gesture_second_active) {
        if (g_state.cfg.touch_mode == TOUCH_MODE_TOUCHSCREEN) {
            active_single = g_state.cfg.ts_single_2nd;
            active_single_count = g_state.cfg.ts_single_2nd_count;
            active_double = g_state.cfg.ts_double_2nd;
            active_double_count = g_state.cfg.ts_double_2nd_count;
            active_has_single_tap_drag = g_state.cfg.ts_single_drag_2nd_count > 0;
            active_has_double_tap = g_state.cfg.ts_double_2nd_count > 0;
            active_has_double_tap_drag = g_state.cfg.ts_double_drag_2nd_count > 0;
        } else {
            active_single = g_state.cfg.tp_single_2nd;
            active_single_count = g_state.cfg.tp_single_2nd_count;
            active_double = g_state.cfg.tp_double_2nd;
            active_double_count = g_state.cfg.tp_double_2nd_count;
            active_has_single_tap_drag = g_state.cfg.tp_single_drag_2nd_count > 0;
            active_has_double_tap = g_state.cfg.tp_double_2nd_count > 0;
            active_has_double_tap_drag = g_state.cfg.tp_double_drag_2nd_count > 0;
        }
    }

    // Path 1: deferred D from DT confirm (DT deferred when Dd set; fire on finger-up only if no drag)
    if (g_state.gesture_pending_deferred_double_count > 0) {
        __android_log_print(ANDROID_LOG_INFO, "Gesture", "tap_up path=1 count=%d consumed=%d", g_state.gesture_pending_deferred_double_count, g_state.gesture_double_tap_consumed);
        execute_actions(result, g_state.gesture_pending_deferred_double, g_state.gesture_pending_deferred_double_count);
        g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true;
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Path 2: post_double_tap_drag cleanup
    if (g_state.gesture_post_double_tap_drag) {
        __android_log_print(ANDROID_LOG_INFO, "Gesture", "tap_up path=2 consumed=%d", g_state.gesture_double_tap_consumed);
        g_state.gesture_deferred_tap_count = 0;
        g_state.gesture_pending_deferred_double_count = 0;
        g_state.gesture_post_double_tap_drag = false;
        g_state.gesture_double_tap_consumed = true;
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Path 3: double_tap_consumed (3+ tap)
    if (g_state.gesture_double_tap_consumed) {
        __android_log_print(ANDROID_LOG_INFO, "Gesture", "tap_up path=3");
        g_state.gesture_double_tap_consumed = false;
        if (!g_state.gesture_is_action_held) {
            if (!active_has_single_tap_drag)
                hold_actions(result, active_single, active_single_count);
            else
                execute_actions(result, active_single, active_single_count);
        }
        f->state = GESTURE_STATE_IDLE;
        return;
    }

    // Path 4: Normal first tap-up
    bool has_dt = active_has_double_tap;
    if (has_dt) {
        g_state.gesture_deferred_tap_count = 0;
        for (int i = 0; i < active_single_count && i < 8; i++)
            g_state.gesture_deferred_tap[g_state.gesture_deferred_tap_count++] = active_single[i];
        g_state.gesture_pending_double_count = 0;
        for (int i = 0; i < active_double_count && i < 8; i++)
            g_state.gesture_pending_double[g_state.gesture_pending_double_count++] = active_double[i];
        g_state.gesture_pending_deferred_double_count = 0;
        for (int i = 0; i < active_double_count && i < 8; i++)
            g_state.gesture_pending_deferred_double[g_state.gesture_pending_deferred_double_count++] = active_double[i];
        g_state.gesture_double_tap_waiting = true;
        g_state.gesture_double_tap_start_time = time_ms;
        g_state.gesture_last_tap_up_x = f->tap_up_x;
        g_state.gesture_last_tap_up_y = f->tap_up_y;
        f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
    } else {
        if (!g_state.gesture_is_action_held) {
            if (!active_has_single_tap_drag)
                hold_actions(result, active_single, active_single_count);
            else
                execute_actions(result, active_single, active_single_count);
        }
        if (active_has_double_tap_drag) {
            g_state.gesture_deferred_tap_count = 0;
            g_state.gesture_last_tap_up_x = f->tap_up_x;
            g_state.gesture_last_tap_up_y = f->tap_up_y;
            g_state.gesture_double_tap_waiting = true;
            g_state.gesture_double_tap_start_time = time_ms;
            f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
        } else {
            f->state = GESTURE_STATE_IDLE;
        }
    }
}
