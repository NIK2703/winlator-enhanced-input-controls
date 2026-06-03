#include "../touch_processor_internal.h"

void element_stick_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id;
    for (int i = 0; i < 4; i++) e->petal_active[i] = false;
    // Java ControlElement.handleTouchDown delegates immediately to handleTouchMove,
    // sending axis values on touch-down (C was deferring to first ACTION_MOVE)
    element_stick_move(e, x, y, time_ms, result);
}

void element_stick_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    float radius = g_state.snapping_size * 6.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist > radius) { dx = dx / dist * radius; dy = dy / dist * radius; }
    float nx = dx / radius;
    float ny = dy / radius;

    bool is_gamepad = e->bindings[0].type >= BINDING_GAMEPAD_BASE && e->bindings[0].type < BINDING_GAMEPAD_BASE + 24;

    if (is_gamepad) {
        // Gamepad stick: Java-compatible dead zone — magnitude > STICK_DEAD_ZONE guard,
        // then max(0, mag-0.01f) * STICK_SENSITIVITY
        float mag = fminf(dist / radius, 1.0f);
        float axis_x = 0, axis_y = 0;
        if (mag > STICK_DEAD_ZONE) {
            float scaled = fminf(fmaxf(0.0f, mag - 0.01f) * STICK_SENSITIVITY, 1.0f);
            axis_x = (nx / mag) * scaled;
            axis_y = (ny / mag) * scaled;
        }
        e->stick_value_x = axis_x;
        e->stick_value_y = axis_y;
        // Determine left vs right thumb from first binding
        int is_left = 1;
        int bt = e->bindings[0].type - BINDING_GAMEPAD_BASE;
        if (bt >= 16 && bt <= 19) is_left = 0; // GAMEPAD_RIGHT_THUMB = ordinals 16-19
        add_action(result, ACT_GAMEPAD_AXIS, is_left, (int)(axis_x * 32767), (int)(axis_y * 32767));
    } else {
        // Per-binding directional mode (matches Java handleTouchMove non-gamepad branch)
        bool raw_up = ny <= -STICK_DEAD_ZONE;
        bool raw_right = nx >= STICK_DEAD_ZONE;
        bool raw_down = ny >= STICK_DEAD_ZONE;
        bool raw_left = nx <= -STICK_DEAD_ZONE;
        bool states[4] = {raw_up, raw_right, raw_down, raw_left};
        for (int i = 0; i < 4; i++) {
            const TouchBinding* b = &e->bindings[i];
            if (b->type == BINDING_NONE) continue;
            bool active;
            if (is_mouse_move_binding(b))
                active = states[i] || states[(i + 2) % 4]; // bidirectional: up||down, right||left
            else
                active = states[i];
            if (active != e->petal_active[i]) {
                e->petal_active[i] = active;
                if (active) {
                    press_binding(result, b, true);
                } else {
                    release_binding(result, b);
                }
            }
        }
    }
}

void element_stick_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y; (void)time_ms;
    e->stick_value_x = 0;
    e->stick_value_y = 0;
    for (int i = 0; i < 4; i++) {
        if (e->petal_active[i]) {
            e->petal_active[i] = false;
            if (e->bindings[i].type != BINDING_NONE)
                release_binding(result, &e->bindings[i]);
        }
    }
    // Java handleTouchUp: zero axis values on finger-up (gamepad only)
    if (e->bindings[0].type >= BINDING_GAMEPAD_BASE && e->bindings[0].type < BINDING_GAMEPAD_BASE + 24) {
        int bt = e->bindings[0].type - BINDING_GAMEPAD_BASE;
        int is_left = 1;
        if (bt >= 16 && bt <= 19) is_left = 0;
        add_action(result, ACT_GAMEPAD_AXIS, is_left, 0, 0);
    }
    e->engaged = false;
    e->current_ptr_id = -1;
}
