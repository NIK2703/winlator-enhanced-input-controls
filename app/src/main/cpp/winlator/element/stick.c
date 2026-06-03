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

    if (is_gamepad_binding(&e->bindings[0])) {
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
        int is_left = !is_right_stick_binding(e);
        add_action(result, ACT_GAMEPAD_AXIS, is_left, (int)(axis_x * 32767), (int)(axis_y * 32767));
    } else {
        element_set_petals(e, nx, ny, STICK_DEAD_ZONE, result);
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
    if (is_gamepad_binding(&e->bindings[0])) {
        add_action(result, ACT_GAMEPAD_AXIS, !is_right_stick_binding(e), 0, 0);
    }
    e->engaged = false;
    e->current_ptr_id = -1;
}
