#include <android/log.h>
#include <time.h>
#include "../touch_processor_internal.h"

#define MAX_HELD_ACTIONS 16

static int pointer_button_idx(const TouchBinding* b);

static uint64_t current_time_ms() {
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

void add_action(TouchActionResult* r, ActionType type, int a0, int a1, int a2) {
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
        case ACT_STOP_MOUSE_MOVE: r->actions[r->count].mouse_move.dx = 0; r->actions[r->count].mouse_move.dy = 0; r->actions[r->count].mouse_move.hold = 0; break;
        case ACT_GAMEPAD_STATE: r->actions[r->count].key.keycode = a0; r->actions[r->count].key.is_down = a1; break;
        case ACT_GAMEPAD_AXIS: r->actions[r->count].gamepad_axis.is_left = a0; r->actions[r->count].gamepad_axis.axis_x = a1; r->actions[r->count].gamepad_axis.axis_y = a2; break;
        case ACT_HAPTIC: r->actions[r->count].haptic.effect = a0; break;
        case ACT_SET_CURSOR_SPEED: r->actions[r->count].cursor_speed.speed = a0; break;
        default: break;
    }
    r->count++;
}

void release_held_actions(TouchActionResult* result) {
    if (!g_state.gesture_is_action_held) return;

    if (g_state.passthrough_active) {
        g_state.gesture_held_count = 0;
        g_state.gesture_is_action_held = false;
        return;
    }
    release_non_modifiers_first(result, g_state.gesture_held_actions, g_state.gesture_held_count);
    g_state.gesture_held_count = 0;
    g_state.gesture_is_action_held = false;
}

// Keyboard modifier keycodes (X11): 0x25=Left Shift, 0x32=Left Ctrl, 0x40=Left Alt
#define MOD_KEYCODE_SHIFT 0x25
#define MOD_KEYCODE_CTRL  0x32
#define MOD_KEYCODE_ALT   0x40

bool is_modifier_binding(const TouchBinding* b) {
    if (is_keyboard_binding(b)) {
        int kc = b->keycode;
        return kc == MOD_KEYCODE_SHIFT || kc == MOD_KEYCODE_CTRL || kc == MOD_KEYCODE_ALT;
    }
    return false;
}

void press_modifiers_first(TouchActionResult* result, const TouchBinding* actions, int count, bool hold) {
    for (int i = 0; i < count; i++)
        if (actions[i].type != BINDING_NONE && is_modifier_binding(&actions[i]))
            press_binding(result, &actions[i], hold);
    for (int i = 0; i < count; i++)
        if (actions[i].type != BINDING_NONE && !is_modifier_binding(&actions[i]))
            press_binding(result, &actions[i], hold);
}

void release_non_modifiers_first(TouchActionResult* result, const TouchBinding* actions, int count) {
    for (int i = count - 1; i >= 0; i--)
        if (actions[i].type != BINDING_NONE && !is_modifier_binding(&actions[i]))
            release_binding(result, &actions[i]);
    for (int i = count - 1; i >= 0; i--)
        if (actions[i].type != BINDING_NONE && is_modifier_binding(&actions[i]))
            release_binding(result, &actions[i]);
}

static void execute_actions_impl(TouchActionResult* result, const TouchBinding* actions, int count, bool force_hold) {
    if (g_state.passthrough_active) return;

    int binding_delay = g_state.cfg.binding_delay_ms;

    // Press modifier keyboard bindings first
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b))
            add_action(result, ACT_KEY_PRESS, b->keycode, 1, 0);
    }

    // For each non-modifier binding: if sticky (modifiers==1) or force_hold → press and hold;
    // otherwise → press and release (tap)
    int non_mod_count = 0;
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) continue;
        if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) continue;
        if (is_mouse_move_binding(b)) continue;

        bool hold = force_hold || (b->modifiers != 0);
        uint64_t press_delay = (binding_delay > 0 && non_mod_count > 0) ? (uint64_t)non_mod_count * binding_delay : 0;

        if (hold) {
            // Hold: press only (no release), track in gesture_held_actions
            if (binding_delay > 0 && press_delay > 0) {
                int act = is_keyboard_binding(b) ? ACT_KEY_PRESS :
                          (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) ? ACT_POINTER_BUTTON_PRESS :
                          ACT_GAMEPAD_STATE;
                schedule_action(*b, act, press_delay);
            } else {
                if (is_keyboard_binding(b))
                    add_action(result, ACT_KEY_PRESS, b->keycode, 1, 0);
                else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5)
                    add_action(result, ACT_POINTER_BUTTON_PRESS, pointer_button_idx(b), 0, 0);
                else if (is_gamepad_binding(b))
                    add_action(result, ACT_GAMEPAD_STATE, b->type - BINDING_GAMEPAD_BASE, 1, 0);
            }
            if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
                g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
            g_state.gesture_is_action_held = true;
            non_mod_count++;
            continue;
        }

        // Tap: press + release
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

    // Release modifier keyboard bindings last in reverse order
    if (binding_delay > 0 && non_mod_count > 0) {
        uint64_t mod_release_delay = (uint64_t)non_mod_count * binding_delay + binding_delay;
        for (int i = count - 1; i >= 0; i--) {
            const TouchBinding* b = &actions[i];
            if (b->type == BINDING_NONE) continue;
            if (is_modifier_binding(b))
                schedule_action(*b, ACT_KEY_RELEASE, mod_release_delay);
        }
    } else {
        for (int i = count - 1; i >= 0; i--) {
            const TouchBinding* b = &actions[i];
            if (b->type == BINDING_NONE) continue;
            if (is_modifier_binding(b))
                add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
        }
    }
}

void execute_actions(TouchActionResult* result, const TouchBinding* actions, int count) {
    execute_actions_impl(result, actions, count, false);
}

void execute_actions_hold(TouchActionResult* result, const TouchBinding* actions, int count) {
    execute_actions_impl(result, actions, count, true);
}

static int pointer_button_idx(const TouchBinding* b) {
    // C enum: LEFT=1, RIGHT=2, MIDDLE=3, BUTTON4=4, BUTTON5=5
    // Java Pointer.Button.values(): LEFT=0, MIDDLE=1, RIGHT=2, SCROLL_UP=3, SCROLL_DOWN=4
    switch (b->type) {
        case BINDING_MOUSE_LEFT: return 0;
        case BINDING_MOUSE_RIGHT: return 2;
        case BINDING_MOUSE_MIDDLE: return 1;
        case BINDING_MOUSE_BUTTON4: return 3;
        case BINDING_MOUSE_BUTTON5: return 4;
        default: return 0;
    }
}

void release_binding(TouchActionResult* result, const TouchBinding* b) {
    if (b->type == BINDING_NONE) return;
    if (is_gamepad_binding(b)) {
        add_action(result, ACT_GAMEPAD_STATE, b->type - BINDING_GAMEPAD_BASE, 0, 0);
    } else if (is_keyboard_binding(b)) {
        add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
    } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
        add_action(result, ACT_POINTER_BUTTON_RELEASE, pointer_button_idx(b), 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) {
    } else if (is_mouse_move_binding(b)) {
        add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
    }
}

void press_binding(TouchActionResult* result, const TouchBinding* b, bool hold) {
    if (b->type == BINDING_NONE) return;
    if (is_gamepad_binding(b)) {
        int btn_idx = b->type - BINDING_GAMEPAD_BASE;
        add_action(result, ACT_GAMEPAD_STATE, btn_idx, 1, 0);
        if (!hold) { add_action(result, ACT_GAMEPAD_STATE, btn_idx, 0, 0); }
    } else if (is_keyboard_binding(b)) {
        add_action(result, ACT_KEY_PRESS, b->keycode, 1, 0);
        if (!hold) { add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0); }
    } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
        int btn = pointer_button_idx(b);
        add_action(result, ACT_POINTER_BUTTON_PRESS, btn, 0, 0);
        if (!hold) { add_action(result, ACT_POINTER_BUTTON_RELEASE, btn, 0, 0); }
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
            if (!hold) { add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0); }
        }
    }
}

void process_scheduled_actions(TouchActionResult* result, uint64_t time_ms) {
    for (int i = 0; i < g_state.scheduled_action_count; i++) {
        ScheduledAction* sa = &g_state.scheduled_actions[i];
        if (!sa->active) continue;
        if (time_ms >= sa->scheduled_time_ms) {
            sa->active = false;
            switch (sa->action_type) {
                case ACT_KEY_PRESS:
                    add_action(result, ACT_KEY_PRESS, sa->binding.keycode, 1, 0);
                    break;
                case ACT_KEY_RELEASE:
                    add_action(result, ACT_KEY_RELEASE, sa->binding.keycode, 0, 0);
                    break;
                case ACT_POINTER_BUTTON_PRESS:
                    add_action(result, ACT_POINTER_BUTTON_PRESS, pointer_button_idx(&sa->binding), 0, 0);
                    break;
                case ACT_POINTER_BUTTON_RELEASE:
                    add_action(result, ACT_POINTER_BUTTON_RELEASE, pointer_button_idx(&sa->binding), 0, 0);
                    break;
                case ACT_GAMEPAD_STATE:
                    add_action(result, ACT_GAMEPAD_STATE, sa->binding.type - BINDING_GAMEPAD_BASE, 1, 0);
                    break;
                case ACT_GAMEPAD_RELEASE:
                    add_action(result, ACT_GAMEPAD_STATE, sa->binding.type - BINDING_GAMEPAD_BASE, 0, 0);
                    break;
            }
        }
    }
    int write_idx = 0;
    for (int i = 0; i < g_state.scheduled_action_count; i++) {
        if (g_state.scheduled_actions[i].active) {
            if (write_idx != i)
                g_state.scheduled_actions[write_idx] = g_state.scheduled_actions[i];
            write_idx++;
        }
    }
    g_state.scheduled_action_count = write_idx;
}
