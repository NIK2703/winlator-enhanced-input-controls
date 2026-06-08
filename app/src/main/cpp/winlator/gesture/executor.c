#include "../touch_processor_internal.h"

static int pointer_button_idx(const TouchBinding* b);

static uint64_t current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int schedule_action(TouchBinding binding, int action_type, uint64_t delay_from_now_ms) {
    if (g_state.scheduled_action_count >= MAX_SCHEDULED_ACTIONS) return -1;
    int idx = g_state.scheduled_action_count++;
    g_state.scheduled_actions[idx].binding = binding;
    g_state.scheduled_actions[idx].action_type = action_type;
    g_state.scheduled_actions[idx].scheduled_time_ms = current_time_ms() + delay_from_now_ms;
    g_state.scheduled_actions[idx].active = true;
    return idx;
}

void release_binding(TouchActionResult* restrict result, const TouchBinding* b) {
    if (b->type == BINDING_NONE) return;
    if (is_gamepad_binding(b))
        add_action(result, ACT_GAMEPAD_STATE, b->type - BINDING_GAMEPAD_BASE, 0, 0);
    else if (is_keyboard_binding(b))
        add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
    else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5)
        add_action(result, ACT_POINTER_BUTTON_RELEASE, pointer_button_idx(b), 0, 0);
    else if (is_mouse_move_binding(b))
        add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
}

void press_binding(TouchActionResult* restrict result, const TouchBinding* b, bool hold) {
    if (b->type == BINDING_NONE) return;
    if (is_gamepad_binding(b)) {
        int btn_idx = b->type - BINDING_GAMEPAD_BASE;
        add_action(result, ACT_GAMEPAD_STATE, btn_idx, 1, 0);
        if (!hold) add_action(result, ACT_GAMEPAD_STATE, btn_idx, 0, 0);
    } else if (is_keyboard_binding(b)) {
        add_action(result, ACT_KEY_PRESS, b->keycode, 1, 0);
        if (!hold) add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
    } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
        int btn = pointer_button_idx(b);
        add_action(result, ACT_POINTER_BUTTON_PRESS, btn, 0, 0);
        if (!hold) add_action(result, ACT_POINTER_BUTTON_RELEASE, btn, 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_UP) {
        add_action(result, ACT_SCROLL, -1, 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_DOWN) {
        add_action(result, ACT_SCROLL, 1, 0, 0);
    } else if (is_mouse_move_binding(b)) {
        static const int move_dx[] = { -1, 1, 0, 0 };
        static const int move_dy[] = { 0, 0, -1, 1 };
        int idx = b->type - BINDING_MOUSE_MOVE_LEFT;
        if (idx >= 0 && idx < 4) {
            add_action(result, ACT_START_MOUSE_MOVE, move_dx[idx], move_dy[idx], hold ? 1 : 0);
            if (!hold) add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
        }
    }
}

void add_action(TouchActionResult* restrict r, ActionType type, int a0, int a1, int a2) {
    if (r->count >= 32) return;
    r->actions[r->count].type = type;
    switch (type) {
        case ACT_POINTER_MOVE: r->actions[r->count].pointer_move.x = a0; r->actions[r->count].pointer_move.y = a1; break;
        case ACT_POINTER_MOVE_DELTA: r->actions[r->count].pointer_delta.dx = a0; r->actions[r->count].pointer_delta.dy = a1; break;
        case ACT_POINTER_BUTTON_PRESS: case ACT_POINTER_BUTTON_RELEASE: r->actions[r->count].pointer_button.button = a0; break;
        case ACT_KEY_PRESS: case ACT_KEY_RELEASE: r->actions[r->count].key.keycode = a0; r->actions[r->count].key.is_down = a1; break;
        case ACT_MOUSE_EVENT: r->actions[r->count].mouse_event.flags = a0; r->actions[r->count].mouse_event.dx = a1; r->actions[r->count].mouse_event.dy = a2; break;
        case ACT_SCROLL: r->actions[r->count].scroll.amount = a0; break;
        case ACT_START_MOUSE_MOVE: r->actions[r->count].mouse_move.dx = a0; r->actions[r->count].mouse_move.dy = a1; r->actions[r->count].mouse_move.hold = a2; break;
        case ACT_STOP_MOUSE_MOVE: break;
        case ACT_GAMEPAD_STATE: r->actions[r->count].key.keycode = a0; r->actions[r->count].key.is_down = a1; break;
        case ACT_GAMEPAD_AXIS: r->actions[r->count].gamepad_axis.is_left = a0; r->actions[r->count].gamepad_axis.axis_x = a1; r->actions[r->count].gamepad_axis.axis_y = a2; break;
        case ACT_HAPTIC: r->actions[r->count].haptic.effect = a0; break;
        case ACT_SET_CURSOR_SPEED: r->actions[r->count].cursor_speed.speed = a0; break;
        default: break;
    }
    r->count++;
}

static void release_non_modifiers_first(TouchActionResult* restrict result, const TouchBinding* actions, int count) {
    for (int i = count - 1; i >= 0; i--)
        if (actions[i].type != BINDING_NONE && !is_modifier_binding(&actions[i]))
            release_binding(result, &actions[i]);
    for (int i = count - 1; i >= 0; i--)
        if (actions[i].type != BINDING_NONE && __builtin_expect(is_modifier_binding(&actions[i]), 0))
            release_binding(result, &actions[i]);
}

void release_held_actions(TouchActionResult* restrict result) {
    if (!g_state.gesture_is_action_held) return;
    if (g_state.passthrough_active) {
        g_state.gesture_held_count = 0;
        g_state.gesture_is_action_held = false;
        memset(g_state.gesture_auto_repeat_last_time_held, 0, sizeof(g_state.gesture_auto_repeat_last_time_held));
        return;
    }
    release_non_modifiers_first(result, g_state.gesture_held_actions, g_state.gesture_held_count);
    g_state.gesture_held_count = 0;
    g_state.gesture_is_action_held = false;
    memset(g_state.gesture_auto_repeat_last_time_held, 0, sizeof(g_state.gesture_auto_repeat_last_time_held));
}

static void press_modifiers_first(TouchActionResult* restrict result, const TouchBinding* actions, int count, bool hold) {
    for (int i = 0; i < count; i++)
        if (actions[i].type != BINDING_NONE && __builtin_expect(is_modifier_binding(&actions[i]), 0))
            press_binding(result, &actions[i], hold);
    for (int i = 0; i < count; i++)
        if (actions[i].type != BINDING_NONE && !is_modifier_binding(&actions[i]))
            press_binding(result, &actions[i], hold);
}

static inline int binding_press_action_type(const TouchBinding* b) {
    if (is_keyboard_binding(b)) return ACT_KEY_PRESS;
    if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) return ACT_POINTER_BUTTON_PRESS;
    return ACT_GAMEPAD_STATE;
}

static inline void add_binding_press(TouchActionResult* restrict r, const TouchBinding* b) {
    if (is_keyboard_binding(b))
        add_action(r, ACT_KEY_PRESS, b->keycode, 1, 0);
    else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5)
        add_action(r, ACT_POINTER_BUTTON_PRESS, pointer_button_idx(b), 0, 0);
    else if (is_gamepad_binding(b))
        add_action(r, ACT_GAMEPAD_STATE, b->type - BINDING_GAMEPAD_BASE, 1, 0);
}

static void execute_actions_impl(TouchActionResult* restrict result, const TouchBinding* restrict actions, int count, bool force_hold) {
    if (g_state.passthrough_active) return;
    if (count == 0) return;
    int binding_delay = g_state.cfg.binding_delay_ms;
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (__builtin_expect(b->type == BINDING_NONE, 0)) continue;
        if (__builtin_expect(is_modifier_binding(b), 0))
            add_action(result, ACT_KEY_PRESS, b->keycode, 1, 0);
    }
    int non_mod_count = 0;
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (__builtin_expect(b->type == BINDING_NONE, 0)) continue;
        if (__builtin_expect(is_modifier_binding(b), 0)) continue;
        if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) continue;
        if (is_mouse_move_binding(b)) continue;
        if (b->toggle) {
            if (g_state.gesture_is_down_event) {
                bool found = false;
                for (int t = 0; t < g_state.gesture_toggled_count; t++) {
                    if (g_state.gesture_toggled_actions[t].type == b->type &&
                        g_state.gesture_toggled_actions[t].keycode == b->keycode) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    if (is_keyboard_binding(b))
                        add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
                    else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5)
                        add_action(result, ACT_POINTER_BUTTON_RELEASE, pointer_button_idx(b), 0, 0);
                    else if (is_gamepad_binding(b))
                        add_action(result, ACT_GAMEPAD_STATE, b->type - BINDING_GAMEPAD_BASE, 0, 0);
                    else if (is_mouse_move_binding(b))
                        add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
                    for (int t = 0; t < g_state.gesture_toggled_count; t++) {
                        if (g_state.gesture_toggled_actions[t].type == b->type &&
                            g_state.gesture_toggled_actions[t].keycode == b->keycode) {
                            g_state.gesture_toggled_actions[t] = g_state.gesture_toggled_actions[--g_state.gesture_toggled_count];
                            g_state.gesture_auto_repeat_last_time[t] = g_state.gesture_auto_repeat_last_time[g_state.gesture_toggled_count];
                            break;
                        }
                    }
                } else {
                    if (binding_delay > 0 && non_mod_count > 0) {
                        int act = binding_press_action_type(b);
                        schedule_action(*b, act, (uint64_t)non_mod_count * binding_delay);
                    } else {
                        add_binding_press(result, b);
                        if (is_mouse_move_binding(b)) {
                            static const int move_dx[] = { -1, 1, 0, 0 };
                            static const int move_dy[] = { 0, 0, -1, 1 };
                            int idx = b->type - BINDING_MOUSE_MOVE_LEFT;
                            if (idx >= 0 && idx < 4)
                                add_action(result, ACT_START_MOUSE_MOVE, move_dx[idx], move_dy[idx], 1);
                        }
                    }
                    if (g_state.gesture_toggled_count < MAX_HELD_ACTIONS)
                        g_state.gesture_toggled_actions[g_state.gesture_toggled_count++] = *b;
                }
            }
            non_mod_count++;
            continue;
        }
        bool hold = force_hold || (b->modifiers != 0);
        uint64_t press_delay = (binding_delay > 0 && non_mod_count > 0) ? (uint64_t)non_mod_count * binding_delay : 0;
        if (hold) {
            if (binding_delay > 0 && press_delay > 0) {
                int act = binding_press_action_type(b);
                schedule_action(*b, act, press_delay);
            } else {
                add_binding_press(result, b);
            }
            if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
                g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
            g_state.gesture_is_action_held = true;
            non_mod_count++;
            continue;
        }
        if (b->auto_repeat) {
            if (binding_delay > 0 && press_delay > 0) {
                int act = binding_press_action_type(b);
                schedule_action(*b, act, press_delay);
            } else {
                add_binding_press(result, b);
            }
            if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
                g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
            g_state.gesture_is_action_held = true;
            non_mod_count++;
            continue;
        }
        uint64_t release_delay = press_delay + binding_delay;
        if (is_gamepad_binding(b)) {
            int btn_idx = b->type - BINDING_GAMEPAD_BASE;
            if (binding_delay > 0) {
                schedule_action(*b, ACT_GAMEPAD_STATE, press_delay);
                schedule_action(*b, ACT_GAMEPAD_RELEASE, release_delay);
            } else {
                add_action(result, ACT_GAMEPAD_STATE, btn_idx, 1, 0);
                add_action(result, ACT_GAMEPAD_STATE, btn_idx, 0, 0);
            }
        } else if (is_keyboard_binding(b)) {
            if (binding_delay > 0) {
                schedule_action(*b, ACT_KEY_PRESS, press_delay);
                schedule_action(*b, ACT_KEY_RELEASE, release_delay);
            } else {
                add_action(result, ACT_KEY_PRESS, b->keycode, 1, 0);
                add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
            }
        } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
            int btn = pointer_button_idx(b);
            if (binding_delay > 0) {
                schedule_action(*b, ACT_POINTER_BUTTON_PRESS, press_delay);
                schedule_action(*b, ACT_POINTER_BUTTON_RELEASE, release_delay);
            } else {
                add_action(result, ACT_POINTER_BUTTON_PRESS, btn, 0, 0);
                add_action(result, ACT_POINTER_BUTTON_RELEASE, btn, 0, 0);
            }
        }
        non_mod_count++;
    }
    if (binding_delay > 0 && non_mod_count > 0) {
        uint64_t mod_release_delay = (uint64_t)non_mod_count * binding_delay + binding_delay;
        for (int i = count - 1; i >= 0; i--) {
            const TouchBinding* b = &actions[i];
            if (__builtin_expect(b->type == BINDING_NONE, 0)) continue;
            if (__builtin_expect(is_modifier_binding(b), 0))
                schedule_action(*b, ACT_KEY_RELEASE, mod_release_delay);
        }
    } else {
        for (int i = count - 1; i >= 0; i--) {
            const TouchBinding* b = &actions[i];
            if (__builtin_expect(b->type == BINDING_NONE, 0)) continue;
            if (__builtin_expect(is_modifier_binding(b), 0))
                add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
        }
    }
}

void execute_actions(TouchActionResult* restrict result, const TouchBinding* actions, int count) {
    execute_actions_impl(result, actions, count, false);
}

void execute_actions_hold(TouchActionResult* restrict result, const TouchBinding* actions, int count) {
    execute_actions_impl(result, actions, count, true);
}

static int pointer_button_idx(const TouchBinding* b) {
    static const uint8_t lut[] = {0, 2, 1, 3, 4};
    int idx = (int)b->type - BINDING_MOUSE_LEFT;
    if (__builtin_expect(idx >= 0 && idx < 5, 1)) return lut[idx];
    return 0;
}

__attribute__((hot))
void process_scheduled_actions(TouchActionResult* restrict result, uint64_t time_ms) {
    if (__builtin_expect(g_state.scheduled_action_count == 0, 1)) return;
    ScheduledAction* sched_actions = g_state.scheduled_actions;
    for (int i = 0; i < g_state.scheduled_action_count; i++) {
        if (i + 4 < g_state.scheduled_action_count)
            __builtin_prefetch(&g_state.scheduled_actions[i + 4], 1, 1);
        if (__builtin_expect(sched_actions[i].binding.type == BINDING_NONE, 0)) continue;
        ScheduledAction* sa = &sched_actions[i];
        if (!sa->active) continue;
        if (time_ms >= sa->scheduled_time_ms) {
            sa->active = false;
            {
                static const void* const sa_dispatch[] = {
                    [ACT_KEY_PRESS] = &&L_SA_KEY_PRESS,
                    [ACT_KEY_RELEASE] = &&L_SA_KEY_RELEASE,
                    [ACT_POINTER_BUTTON_PRESS] = &&L_SA_PTR_PRESS,
                    [ACT_POINTER_BUTTON_RELEASE] = &&L_SA_PTR_RELEASE,
                    [ACT_GAMEPAD_STATE] = &&L_SA_GAMEPAD,
                    [ACT_GAMEPAD_RELEASE] = &&L_SA_GAMEPAD_REL,
                };
                int at = sa->action_type;
                if (at >= 0 && at <= ACT_GAMEPAD_AXIS && sa_dispatch[at])
                    goto *sa_dispatch[at];
                goto L_SA_DONE;
            L_SA_KEY_PRESS:
                add_action(result, ACT_KEY_PRESS, sa->binding.keycode, 1, 0);
                goto L_SA_DONE;
            L_SA_KEY_RELEASE:
                add_action(result, ACT_KEY_RELEASE, sa->binding.keycode, 0, 0);
                goto L_SA_DONE;
            L_SA_PTR_PRESS:
                add_action(result, ACT_POINTER_BUTTON_PRESS, pointer_button_idx(&sa->binding), 0, 0);
                goto L_SA_DONE;
            L_SA_PTR_RELEASE:
                add_action(result, ACT_POINTER_BUTTON_RELEASE, pointer_button_idx(&sa->binding), 0, 0);
                goto L_SA_DONE;
            L_SA_GAMEPAD:
                add_action(result, ACT_GAMEPAD_STATE, sa->binding.type - BINDING_GAMEPAD_BASE, 1, 0);
                goto L_SA_DONE;
            L_SA_GAMEPAD_REL:
                add_action(result, ACT_GAMEPAD_STATE, sa->binding.type - BINDING_GAMEPAD_BASE, 0, 0);
            L_SA_DONE: ;
            }
        }
    }
    int write_idx = 0;
    for (int i = 0; i < g_state.scheduled_action_count; i++) {
        if (sched_actions[i].active) {
            if (write_idx != i)
                sched_actions[write_idx] = sched_actions[i];
            write_idx++;
        }
    }
    g_state.scheduled_action_count = write_idx;
}
