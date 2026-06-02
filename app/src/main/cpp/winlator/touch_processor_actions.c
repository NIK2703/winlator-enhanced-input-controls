#include "touch_processor_internal.h"

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
        default: break;
    }
    r->count++;
}

void release_held_actions(TouchActionResult* result) {
    if (!g_state.gesture_is_action_held) return;
    for (int i = g_state.gesture_held_count - 1; i >= 0; i--) {
        TouchBinding* b = &g_state.gesture_held_actions[i];
        if (b->type >= BINDING_GAMEPAD_BASE && b->type < BINDING_GAMEPAD_BASE + 24)
            add_action(result, ACT_GAMEPAD_STATE, b->type - BINDING_GAMEPAD_BASE, 0, 0);
        else if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST)
            add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
        else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5)
            add_action(result, ACT_POINTER_BUTTON_RELEASE, b->type - BINDING_MOUSE_LEFT, 0, 0);
    }
    g_state.gesture_held_count = 0;
    g_state.gesture_is_action_held = false;
}

bool is_modifier_binding(const TouchBinding* b) {
    if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST) {
        int kc = b->keycode;
        return kc == 0x32 || kc == 0x3F || kc == 0x25 || kc == 0x42 || kc == 0x6B || kc == 0x4E;
    }
    return false;
}

void hold_actions(TouchActionResult* result, const TouchBinding* actions, int count) {
    if (g_state.passthrough_active) return;
    release_held_actions(result);
    g_state.gesture_held_count = 0;

    // Press modifiers first
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) {
            if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST) {
                add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
            }
        }
    }

    // Press non-modifier actions and track for hold
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) continue;

        if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST) {
            add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
            g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
        } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
            add_action(result, ACT_POINTER_BUTTON_PRESS, b->type - BINDING_MOUSE_LEFT, 0, 0);
            g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
        } else if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) {
            add_action(result, ACT_SCROLL, b->type == BINDING_MOUSE_SCROLL_UP ? -1 : 1, 0, 0);
        } else if (b->type >= BINDING_MOUSE_MOVE_LEFT && b->type <= BINDING_MOUSE_MOVE_DOWN) {
            int dx = 0, dy = 0;
            if (b->type == BINDING_MOUSE_MOVE_LEFT) dx = -1;
            else if (b->type == BINDING_MOUSE_MOVE_RIGHT) dx = 1;
            else if (b->type == BINDING_MOUSE_MOVE_UP) dy = -1;
            else if (b->type == BINDING_MOUSE_MOVE_DOWN) dy = 1;
            add_action(result, ACT_START_MOUSE_MOVE, dx, dy, 1);
        }
    }
    g_state.gesture_is_action_held = true;
}

void execute_actions(TouchActionResult* result, const TouchBinding* actions, int count) {
    if (g_state.passthrough_active) return;
    // Press modifiers first
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) {
            if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST)
                add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
        }
    }
    // Execute non-modifier actions (press + release for tap)
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) continue;
        if (b->type >= BINDING_GAMEPAD_BASE && b->type < BINDING_GAMEPAD_BASE + 24) {
            int btn_idx = b->type - BINDING_GAMEPAD_BASE;
            add_action(result, ACT_GAMEPAD_STATE, btn_idx, 1, 0);
            add_action(result, ACT_GAMEPAD_STATE, btn_idx, 0, 0);
        } else if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) {
            add_action(result, ACT_SCROLL, b->type == BINDING_MOUSE_SCROLL_UP ? -1 : 1, 0, 0);
        } else if (b->type >= BINDING_MOUSE_MOVE_LEFT && b->type <= BINDING_MOUSE_MOVE_DOWN) {
            int dx = 0, dy = 0;
            if (b->type == BINDING_MOUSE_MOVE_LEFT) dx = -1;
            else if (b->type == BINDING_MOUSE_MOVE_RIGHT) dx = 1;
            else if (b->type == BINDING_MOUSE_MOVE_UP) dy = -1;
            else if (b->type == BINDING_MOUSE_MOVE_DOWN) dy = 1;
            add_action(result, ACT_START_MOUSE_MOVE, dx, dy, 0);
            add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
        } else if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST) {
            add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
            add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
        } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
            int btn = b->type - BINDING_MOUSE_LEFT;
            add_action(result, ACT_POINTER_BUTTON_PRESS, btn, 0, 0);
            add_action(result, ACT_POINTER_BUTTON_RELEASE, btn, 0, 0);
        }
    }
    // Release modifiers last
    for (int i = count - 1; i >= 0; i--) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) {
            if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST)
                add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
        }
    }
}

void release_binding(TouchActionResult* result, const TouchBinding* b) {
    if (b->type == BINDING_NONE) return;
    LOGD("release_binding: type=%d keycode=%d", b->type, b->keycode);
    if (b->type >= BINDING_GAMEPAD_BASE && b->type < BINDING_GAMEPAD_BASE + 24) {
        add_action(result, ACT_GAMEPAD_STATE, b->type - BINDING_GAMEPAD_BASE, 0, 0);
    } else if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST) {
        add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
    } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
        add_action(result, ACT_POINTER_BUTTON_RELEASE, b->type - BINDING_MOUSE_LEFT, 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) {
    } else if (b->type >= BINDING_MOUSE_MOVE_LEFT && b->type <= BINDING_MOUSE_MOVE_DOWN) {
        add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
    }
}

void press_binding(TouchActionResult* result, const TouchBinding* b, bool hold) {
    if (b->type == BINDING_NONE) return;
    LOGD("press_binding: type=%d keycode=%d hold=%d", b->type, b->keycode, hold);
    if (b->type >= BINDING_GAMEPAD_BASE && b->type < BINDING_GAMEPAD_BASE + 24) {
        int btn_idx = b->type - BINDING_GAMEPAD_BASE;
        add_action(result, ACT_GAMEPAD_STATE, btn_idx, 1, 0);
        if (!hold) add_action(result, ACT_GAMEPAD_STATE, btn_idx, 0, 0);
    } else if (b->type >= BINDING_KEYBOARD_FIRST && b->type <= BINDING_KEYBOARD_LAST) {
        if (is_modifier_binding(b)) {
            add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
        }
        add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
        if (!hold) add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
    } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
        int btn = b->type - BINDING_MOUSE_LEFT;
        add_action(result, ACT_POINTER_BUTTON_PRESS, btn, 0, 0);
        if (!hold) add_action(result, ACT_POINTER_BUTTON_RELEASE, btn, 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_UP) {
        add_action(result, ACT_SCROLL, -1, 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_DOWN) {
        add_action(result, ACT_SCROLL, 1, 0, 0);
    } else if (b->type >= BINDING_MOUSE_MOVE_LEFT && b->type <= BINDING_MOUSE_MOVE_DOWN) {
        int dx = 0, dy = 0;
        if (b->type == BINDING_MOUSE_MOVE_LEFT) dx = -1;
        else if (b->type == BINDING_MOUSE_MOVE_RIGHT) dx = 1;
        else if (b->type == BINDING_MOUSE_MOVE_UP) dy = -1;
        else if (b->type == BINDING_MOUSE_MOVE_DOWN) dy = 1;
        add_action(result, ACT_START_MOUSE_MOVE, dx, dy, hold ? 1 : 0);
        if (!hold) add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
    }
}
