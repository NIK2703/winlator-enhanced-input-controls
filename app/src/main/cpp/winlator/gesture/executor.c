#include "../touch_processor_internal.h"

static int pointer_button_idx(const TouchBinding* b);
static void release_non_modifiers_first(TouchActionResult* restrict result, const TouchBinding* actions, int count);

// Keyboard modifier keycodes (X11): 0x25=KEY_CTRL_L(37), 0x32=KEY_SHIFT_L(50), 0x40=KEY_ALT_L(64)
#define MOD_KEYCODE_CTRL  0x25
#define MOD_KEYCODE_SHIFT 0x32
#define MOD_KEYCODE_ALT   0x40
#define MOVE_DIRECTION_COUNT 4

bool is_modifier_binding(const TouchBinding* b) {
    if (is_keyboard_binding(b)) {
        int kc = b->keycode;
        return kc == MOD_KEYCODE_SHIFT || kc == MOD_KEYCODE_CTRL || kc == MOD_KEYCODE_ALT;
    }
    return false;
}

static inline bool is_mouse_button_binding(const TouchBinding* b) {
    return b->type >= BINDING_MOUSE_LEFT && b->type <= BINDING_MOUSE_BUTTON5;
}

static inline int action_type_for_binding(const TouchBinding* b) {
    return is_keyboard_binding(b) ? ACT_KEY_PRESS :
           is_mouse_button_binding(b) ? ACT_POINTER_BUTTON_PRESS :
           ACT_GAMEPAD_STATE;
}

static inline int gamepad_button_index(const TouchBinding* b) {
    return b->type - BINDING_GAMEPAD_BASE;
}

static inline uint64_t calc_press_delay(int binding_delay, int non_mod_count) {
    return (binding_delay > 0 && non_mod_count > 0) ? (uint64_t)non_mod_count * binding_delay : 0;
}

static int schedule_action(TouchBinding binding, int action_type, uint64_t delay_from_now_ms) {
    if (g_state.scheduled_action_count >= MAX_SCHEDULED_ACTIONS) {
        return -1;
    }
    int idx = g_state.scheduled_action_count++;
    g_state.scheduled_actions[idx].binding = binding;
    g_state.scheduled_actions[idx].action_type = action_type;
    g_state.scheduled_actions[idx].scheduled_time_ms = now_ms() + delay_from_now_ms;
    g_state.scheduled_actions[idx].active = true;
    return idx;
}

static void release_modifiers_with_delay(TouchActionResult* restrict result, const TouchBinding* actions, int count, uint64_t mod_release_delay, bool use_schedule) {
    for (int i = count - 1; i >= 0; i--) {
        const TouchBinding* b = &actions[i];
        if (b->type == BINDING_NONE) continue;
        if (is_modifier_binding(b)) {
            if (use_schedule)
                schedule_action(*b, ACT_KEY_RELEASE, mod_release_delay);
            else
                add_action(result, ACT_KEY_RELEASE, b->keycode, 0, 0);
        }
    }
}

void add_action(TouchActionResult* restrict r, ActionType type, int param0, int param1, int param2) {
    if (r->count >= (int)(sizeof(r->actions) / sizeof(r->actions[0]))) {
        return;
    }
    r->actions[r->count].type = type;
    switch (type) {
        case ACT_POINTER_MOVE:          /* param0=x, param1=y */
            r->actions[r->count].pointer_move.x = param0;
            r->actions[r->count].pointer_move.y = param1;
            break;
        case ACT_POINTER_MOVE_DELTA:    /* param0=dx, param1=dy */
            r->actions[r->count].pointer_delta.dx = param0;
            r->actions[r->count].pointer_delta.dy = param1;
            break;
        case ACT_POINTER_BUTTON_PRESS:
        case ACT_POINTER_BUTTON_RELEASE: /* param0=button index */
            r->actions[r->count].pointer_button.button = param0;
            break;
        case ACT_KEY_PRESS:
        case ACT_KEY_RELEASE:           /* param0=keycode, param1=is_down */
            r->actions[r->count].key.keycode = param0;
            r->actions[r->count].key.is_down = param1;
            break;
        case ACT_MOUSE_EVENT:           /* param0=flags, param1=dx, param2=dy */
            r->actions[r->count].mouse_event.flags = param0;
            r->actions[r->count].mouse_event.dx = param1;
            r->actions[r->count].mouse_event.dy = param2;
            break;
        case ACT_SCROLL:                /* param0=amount (negative=up, positive=down) */
            r->actions[r->count].scroll.amount = param0;
            break;
        case ACT_START_MOUSE_MOVE:      /* param0=dx, param1=dy, param2=hold */
            r->actions[r->count].mouse_move.dx = param0;
            r->actions[r->count].mouse_move.dy = param1;
            r->actions[r->count].mouse_move.hold = param2;
            break;
        case ACT_STOP_MOUSE_MOVE:       /* no params */
            r->actions[r->count].mouse_move.dx = 0;
            r->actions[r->count].mouse_move.dy = 0;
            r->actions[r->count].mouse_move.hold = 0;
            break;
        case ACT_GAMEPAD_STATE:          /* param0=button_index, param1=is_down */
            r->actions[r->count].key.keycode = param0;
            r->actions[r->count].key.is_down = param1;
            break;
        case ACT_GAMEPAD_AXIS:           /* param0=is_left, param1=axis_x, param2=axis_y */
            r->actions[r->count].gamepad_axis.is_left = param0;
            r->actions[r->count].gamepad_axis.axis_x = param1;
            r->actions[r->count].gamepad_axis.axis_y = param2;
            break;
        case ACT_HAPTIC:
            r->actions[r->count].haptic.effect = param0;
            break;
        case ACT_SET_CURSOR_SPEED:
            r->actions[r->count].cursor_speed.speed = param0;
            break;
        default:
            break;
    }
    r->count++;
}

void release_held_actions(TouchActionResult* restrict result) {
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "RELEASE_HELD is_held=%d held_count=%d sched_count=%d",
        g_state.gesture_is_action_held, g_state.gesture_held_count, g_state.scheduled_action_count);
    if (!g_state.gesture_is_action_held) return;

    if (g_state.passthrough_active) {
        g_state.gesture_held_count = 0;
        g_state.gesture_is_action_held = false;
        memset(g_state.gesture_auto_repeat_last_time_held, 0, sizeof(g_state.gesture_auto_repeat_last_time_held));
        return;
    }

    // Cancel all pending scheduled actions — gesture is ending, any scheduled
    // presses must not fire after the held release (would cause orphaned press).
    int cancelled = 0;
    for (int i = 0; i < g_state.scheduled_action_count; i++) {
        if (g_state.scheduled_actions[i].active) {
            g_state.scheduled_actions[i].active = false;
            cancelled++;
        }
    }
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "RELEASE_HELD cancelled_sched=%d", cancelled);

    release_non_modifiers_first(result, g_state.gesture_held_actions, g_state.gesture_held_count);
    g_state.gesture_held_count = 0;
    g_state.gesture_is_action_held = false;
    memset(g_state.gesture_auto_repeat_last_time_held, 0, sizeof(g_state.gesture_auto_repeat_last_time_held));
}

static void release_non_modifiers_first(TouchActionResult* restrict result, const TouchBinding* actions, int count) {
    for (int i = count - 1; i >= 0; i--)
        if (actions[i].type != BINDING_NONE && actions[i].modifiers == 0)
            release_binding(result, &actions[i]);
    for (int i = count - 1; i >= 0; i--)
        if (actions[i].type != BINDING_NONE && actions[i].modifiers != 0)
            release_binding(result, &actions[i]);
}

static bool execute_toggle_actions(TouchActionResult* restrict result, const TouchBinding* b,
                                   int non_mod_count, int binding_delay) {
    if (!g_state.gesture_is_down_event) return true;

    bool found = false;
    for (int t = 0; t < g_state.gesture_toggled_count; t++) {
        if (g_state.gesture_toggled_actions[t].type == b->type &&
            g_state.gesture_toggled_actions[t].keycode == b->keycode) {
            found = true;
            break;
        }
    }
    if (found) {
        release_binding(result, b);
        gesture_remove_toggled_action(b->type, b->keycode);
    } else {
        if (binding_delay > 0 && non_mod_count > 0) {
            bool pending = false;
            int si = -1;
            for (si = 0; si < g_state.scheduled_action_count; si++) {
                if (g_state.scheduled_actions[si].active
                    && g_state.scheduled_actions[si].needs_toggle_record
                    && g_state.scheduled_actions[si].binding.type == b->type
                    && g_state.scheduled_actions[si].binding.keycode == b->keycode) {
                    pending = true;
                    break;
                }
            }
            if (pending) {
                release_binding(result, b);
                g_state.scheduled_actions[si].active = false;
                g_state.scheduled_actions[si].needs_toggle_record = false;
            } else {
                int sched_idx = schedule_action(*b, action_type_for_binding(b), (uint64_t)non_mod_count * binding_delay);
                if (sched_idx >= 0)
                    g_state.scheduled_actions[sched_idx].needs_toggle_record = true;
            }
        } else {
            press_binding(result, b, true);
            if (g_state.gesture_toggled_count < MAX_HELD_ACTIONS)
                g_state.gesture_toggled_actions[g_state.gesture_toggled_count++] = *b;
        }
    }
    return true;
}

static bool execute_hold_actions(TouchActionResult* restrict result, const TouchBinding* b,
                                 bool force_hold, int non_mod_count, int binding_delay) {
    bool hold = force_hold || (b->modifiers != 0);
    if (!hold && !b->auto_repeat) return false;

    uint64_t press_delay = calc_press_delay(binding_delay, non_mod_count);
    if (binding_delay > 0 && press_delay > 0) {
        schedule_action(*b, action_type_for_binding(b), press_delay);
    } else {
        press_binding(result, b, true);
    }
    if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
        g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
    g_state.gesture_is_action_held = true;
    return true;
}

static void execute_tap_actions(TouchActionResult* restrict result, const TouchBinding* b,
                                int non_mod_count, int binding_delay) {
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "EXEC_TAP type=%d kc=%d delay=%d non_mod=%d actions_before=%d",
        b->type, b->keycode, binding_delay, non_mod_count, result->count);
    uint64_t press_delay = calc_press_delay(binding_delay, non_mod_count);
    if (is_gamepad_binding(b)) {
        int btn_idx = gamepad_button_index(b);
        if (binding_delay > 0) {
            schedule_action(*b, ACT_GAMEPAD_STATE, press_delay);
        } else {
            add_action(result, ACT_GAMEPAD_STATE, btn_idx, 1, 0);
        }
    } else if (is_keyboard_binding(b)) {
        if (binding_delay > 0) {
            schedule_action(*b, ACT_KEY_PRESS, press_delay);
        } else {
            add_action(result, ACT_KEY_PRESS, b->keycode, 1, 0);
        }
    } else if (is_mouse_button_binding(b)) {
        int btn = pointer_button_idx(b);
        if (binding_delay > 0) {
            schedule_action(*b, ACT_POINTER_BUTTON_PRESS, press_delay);
        } else {
            add_action(result, ACT_POINTER_BUTTON_PRESS, btn, 0, 0);
        }
    }
    if (g_state.gesture_held_count < MAX_HELD_ACTIONS)
        g_state.gesture_held_actions[g_state.gesture_held_count++] = *b;
    g_state.gesture_is_action_held = true;
}

static void execute_actions_impl(TouchActionResult* restrict result, const TouchBinding* restrict actions, int count, bool force_hold) {
    if (g_state.passthrough_active) {
        __android_log_print(ANDROID_LOG_WARN, "ScrollDbg", "execute_actions: BLOCKED passthrough_active");
        return;
    }
    if (count == 0) {
        __android_log_print(ANDROID_LOG_WARN, "ScrollDbg", "execute_actions: BLOCKED count=0");
        return;
    }
    __android_log_print(ANDROID_LOG_WARN, "ScrollDbg", "execute_actions: count=%d type[0]=%d keycode[0]=%d", count, actions[0].type, actions[0].keycode);

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
        if (is_mouse_move_binding(b)) continue;

        // Scroll bindings: fire as tap (press+release) directly
        if (b->type == BINDING_MOUSE_SCROLL_UP || b->type == BINDING_MOUSE_SCROLL_DOWN) {
            press_binding(result, b, false);
            non_mod_count++;
            continue;
        }

        // Toggle switch: press on first activation, release on second
        if (b->toggle) {
            execute_toggle_actions(result, b, non_mod_count, binding_delay);
            non_mod_count++;
            continue;
        }

        // Hold (sticky modifiers or force_hold) and auto-repeat: press and track
        if (execute_hold_actions(result, b, force_hold, non_mod_count, binding_delay)) {
            non_mod_count++;
            continue;
        }

        // Tap: press + release
        execute_tap_actions(result, b, non_mod_count, binding_delay);
        non_mod_count++;
    }

    // Release modifier keyboard bindings last in reverse order
    uint64_t press_delay = calc_press_delay(binding_delay, non_mod_count);
    uint64_t mod_release_delay = press_delay > 0 ? press_delay + binding_delay : 0;
    release_modifiers_with_delay(result, actions, count, mod_release_delay, press_delay > 0);
}

void execute_actions(TouchActionResult* restrict result, const TouchBinding* actions, int count) {
    execute_actions_impl(result, actions, count, false);
}

void execute_actions_hold(TouchActionResult* restrict result, const TouchBinding* actions, int count) {
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
        default: return 0; /* MOUSE_LEFT fallback */
    }
}

static void dispatch_binding_action(TouchActionResult* restrict result, const TouchBinding* b, bool is_press) {
    if (b->type == BINDING_NONE) return;
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "DISPATCH_ACTION type=%d keycode=%d modifiers=%d press=%d actions_before=%d", b->type, b->keycode, b->modifiers, is_press, result->count);
    if (is_gamepad_binding(b)) {
        int btn_idx = gamepad_button_index(b);
        add_action(result, ACT_GAMEPAD_STATE, btn_idx, is_press ? 1 : 0, 0);
    } else if (is_keyboard_binding(b)) {
        add_action(result, is_press ? ACT_KEY_PRESS : ACT_KEY_RELEASE, b->keycode, is_press ? 1 : 0, 0);
    } else if (is_mouse_button_binding(b)) {
        int btn = pointer_button_idx(b);
        add_action(result, is_press ? ACT_POINTER_BUTTON_PRESS : ACT_POINTER_BUTTON_RELEASE, btn, 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_UP) {
        if (is_press) add_action(result, ACT_SCROLL, -1, 0, 0);
    } else if (b->type == BINDING_MOUSE_SCROLL_DOWN) {
        if (is_press) add_action(result, ACT_SCROLL, 1, 0, 0);
    } else if (is_mouse_move_binding(b)) {
        if (!is_press) {
            add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0);
        }
    }
}

void release_binding(TouchActionResult* restrict result, const TouchBinding* b) {
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "RELEASE_BINDING type=%d keycode=%d actions_before=%d", b->type, b->keycode, result->count);
    dispatch_binding_action(result, b, false);
}

void press_binding(TouchActionResult* restrict result, const TouchBinding* b, bool hold) {
    __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "PRESS_BINDING type=%d keycode=%d hold=%d actions_before=%d", b->type, b->keycode, hold, result->count);
    if (b->type == BINDING_NONE) return;
    if (is_mouse_move_binding(b)) {
        static const int move_dx[] = { -1, 1, 0, 0 };
        static const int move_dy[] = { 0, 0, -1, 1 };
        int idx = b->type - BINDING_MOUSE_MOVE_LEFT;
        if (idx >= 0 && idx < MOVE_DIRECTION_COUNT) {
            add_action(result, ACT_START_MOUSE_MOVE, move_dx[idx], move_dy[idx], hold ? 1 : 0);
            if (!hold) { add_action(result, ACT_STOP_MOUSE_MOVE, 0, 0, 0); }
        }
        return;
    }
    dispatch_binding_action(result, b, true);
    if (!hold) { dispatch_binding_action(result, b, false); }
}

static void compact_scheduled_actions(int old_count) {
    ScheduledAction* sched_actions = g_state.scheduled_actions;
    int write_idx = 0;
    for (int i = 0; i < g_state.scheduled_action_count; i++) {
        if (sched_actions[i].active) {
            if (write_idx != i)
                sched_actions[write_idx] = sched_actions[i];
            write_idx++;
        }
    }
    g_state.scheduled_action_count = write_idx;
    for (int i = write_idx; i < old_count; i++) {
        sched_actions[i].active = false;
        sched_actions[i].binding.type = BINDING_NONE;
    }
}

void process_scheduled_actions(TouchActionResult* restrict result, uint64_t time_ms) {
    if (g_state.scheduled_action_count == 0) return;
    int old_count = g_state.scheduled_action_count;
    ScheduledAction* sched_actions = g_state.scheduled_actions;
    int current_count = old_count;
    for (int i = 0; i < current_count; i++) {
        ScheduledAction* sa = &sched_actions[i];
        if (!sa->active) continue;
        if (time_ms >= sa->scheduled_time_ms) {
            sa->active = false;
            switch (sa->action_type) {
                case ACT_KEY_PRESS:
                case ACT_POINTER_BUTTON_PRESS:
                case ACT_GAMEPAD_STATE:
                    press_binding(result, &sa->binding, true);
                    break;
                case ACT_KEY_RELEASE:
                case ACT_POINTER_BUTTON_RELEASE:
                case ACT_GAMEPAD_RELEASE:
                    release_binding(result, &sa->binding);
                    break;
            }
            if (sa->needs_toggle_record) {
                if (g_state.gesture_toggled_count < MAX_HELD_ACTIONS)
                    g_state.gesture_toggled_actions[g_state.gesture_toggled_count++] = sa->binding;
                sa->needs_toggle_record = false;
            }
        }
    }
    compact_scheduled_actions(old_count);
}
