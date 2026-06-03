#include "../touch_processor_internal.h"

#define MAX_HELD_ACTIONS 16

static inline void delay_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts = {0, ms * 1000000};
    nanosleep(&ts, NULL);
}

static int pointer_button_idx(const TouchBinding* b);

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
    
    // Java GestureActionExecutor.releaseHeldAction: if passthrough active, silently clear without emitting events
    if (g_state.passthrough_active) {
        g_state.gesture_held_count = 0;
        g_state.gesture_is_action_held = false;
        return;
    }
    // Pass 1: release non-keyboard bindings first (forward order)
    for (int i = 0; i < g_state.gesture_held_count; i++) {
        const TouchBinding* b = &g_state.gesture_held_actions[i];
        if (!is_keyboard_binding(b))
            release_binding(result, b);
    }
    // Pass 2: release keyboard bindings in forward order (mirrors Java releaseHeldAction)
    for (int i = 0; i < g_state.gesture_held_count; i++) {
        const TouchBinding* b = &g_state.gesture_held_actions[i];
        if (is_keyboard_binding(b))
            release_binding(result, b);
    }
    g_state.gesture_held_count = 0;
    g_state.gesture_is_action_held = false;
}

bool is_modifier_binding(const TouchBinding* b) {
    // Java Binding.isModifier(): only MOD_CTRL(→KEY_CTRL_L=0x25), MOD_SHIFT(→KEY_SHIFT_L=0x32), MOD_ALT(→KEY_ALT_L=0x40)
    // Right-hand variants (KEY_CTRL_R=0x69, KEY_SHIFT_R=0x3E, KEY_ALT_R=0x6C) are concrete keys, NOT modifiers
    if (is_keyboard_binding(b)) {
        int kc = b->keycode;
        return kc == 0x25 || kc == 0x32 || kc == 0x40;
    }
    return false;
}

void hold_actions(TouchActionResult* result, const TouchBinding* actions, int count) {
    if (g_state.passthrough_active) return;
    
    release_held_actions(result);
    g_state.gesture_held_count = 0;

    int binding_delay = g_state.cfg.binding_delay_ms;

    // Java GestureActionExecutor.executeActionsAndHold:
    //   - Pass 1: Press ONLY modifier keyboard bindings (Java toKeyboardBinding -> handleInputEvent(true))
    //   - Pass 2: Hold non-modifier, non-scroll, non-mouse-move bindings (Java handleInputEvent(true))
    //   - Scroll and mouse-move are SKIPPED in hold mode
    // Pass 1: press only MODIFIER keyboard bindings first
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) {
            add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
            if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
                g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
        }
    }
    // Pass 2: non-modifier keyboard, mouse buttons, gamepad states; skip scroll and mouse-move
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) continue;
        if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) continue;
        if (is_mouse_move_binding(b)) continue;
        if (is_keyboard_binding(b)) {
            add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
            if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
                g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
        } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
            add_action(result, ACT_POINTER_BUTTON_PRESS, pointer_button_idx(b), 0, 0);
            if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
                g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
        } else if (is_gamepad_binding(b)) {
            int btn = b->type - BINDING_GAMEPAD_BASE;
            add_action(result, ACT_GAMEPAD_STATE, btn, 1, 0);
            if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
                g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
        }
        // Java: bindingDelay sleep between items
        if (i < count - 1)
            delay_ms(binding_delay);
    }
    g_state.gesture_is_action_held = true;
}

void execute_actions(TouchActionResult* result, const TouchBinding* actions, int count) {
    if (g_state.passthrough_active) return;
    
    int binding_delay = g_state.cfg.binding_delay_ms;

    // Press modifier keyboard bindings first (mirrors Java GestureActionExecutor.executeActions:
    //   toKeyboardBinding() → handleInputEvent(true) for MOD_CTRL/SHIFT/ALT only)
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b))
            add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
    }
    // Execute non-modifier, non-scroll, non-mouse-move actions (press + release for tap).
    // Mirrors Java GestureActionExecutor.executeActions:
    //   press → sleep(bindingDelay) → release → sleep(bindingDelay) → next item
    for (int i = 0; i < count; i++) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) continue;
        if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) continue;
        if (is_mouse_move_binding(b)) continue;

        if (is_gamepad_binding(b)) {
            int btn_idx = b->type - BINDING_GAMEPAD_BASE;
            add_action(result, ACT_GAMEPAD_STATE, btn_idx, 1, 0);
            delay_ms(binding_delay);
            add_action(result, ACT_GAMEPAD_STATE, btn_idx, 0, 0);
        } else if (is_keyboard_binding(b)) {
            add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
            delay_ms(binding_delay);
            add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
        } else if (b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5) {
            int btn = pointer_button_idx(b);
            add_action(result, ACT_POINTER_BUTTON_PRESS, btn, 0, 0);
            delay_ms(binding_delay);
            add_action(result, ACT_POINTER_BUTTON_RELEASE, btn, 0, 0);
        }
        if (i < count - 1)
            delay_ms(binding_delay);
    }

    // Release modifier keyboard bindings last in reverse order (matches Java: toKeyboardBinding → release)
    for (int i = count - 1; i >= 0; i--) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b))
            add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
    }
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
        if (!hold) add_action(result, ACT_GAMEPAD_STATE, btn_idx, 0, 0);
    } else if (is_keyboard_binding(b)) {
        add_action(result, ACT_KEY_PRESS, b->keycode, 0, 0);
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
        int dx = 0, dy = 0;
        if (b->type == BINDING_MOUSE_MOVE_LEFT) dx = -1;
        else if (b->type == BINDING_MOUSE_MOVE_RIGHT) dx = 1;
        else if (b->type == BINDING_MOUSE_MOVE_UP) dy = -1;
        else if (b->type == BINDING_MOUSE_MOVE_DOWN) dy = 1;
        add_action(result, ACT_START_MOUSE_MOVE, dx, dy, hold ? 1 : 0);
        if (!hold) add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
    }
}
