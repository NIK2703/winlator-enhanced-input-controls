#include "touch_processor_internal.h"

void element_stick_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)time_ms; (void)result;
    e->stick_center_x = x;
    e->stick_center_y = y;
}

void element_stick_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    float radius = e->w * g_state.snapping_size * 0.5f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist > radius) { dx = dx / dist * radius; dy = dy / dist * radius; }
    float nx = dx / radius;
    float ny = dy / radius;

    if (e->bindings[0].type != BINDING_NONE) {
        float mag = sqrtf(nx*nx + ny*ny);
        if (mag > STICK_DEAD_ZONE) {
            e->stick_value_x = (nx / mag) * fminf((mag - STICK_DEAD_ZONE) / (1.0f - STICK_DEAD_ZONE) * STICK_SENSITIVITY, 1.0f);
            e->stick_value_y = (ny / mag) * fminf((mag - STICK_DEAD_ZONE) / (1.0f - STICK_DEAD_ZONE) * STICK_SENSITIVITY, 1.0f);
            int mx = (int)(e->stick_value_x * 50);
            int my = (int)(e->stick_value_y * 50);
            if (mx != 0 || my != 0) add_action(result, ACT_MOUSE_EVENT, 0, mx, my);
        }
    } else {
        bool up = ny <= -STICK_DEAD_ZONE;
        bool right = nx >= STICK_DEAD_ZONE;
        bool down = ny >= STICK_DEAD_ZONE;
        bool left = nx <= -STICK_DEAD_ZONE;
        const TouchBinding* dirs[] = {&e->bindings[0], &e->bindings[1], &e->bindings[2], &e->bindings[3]};
        bool states[] = {up, right, down, left};
        for (int i = 0; i < 4; i++) {
            if (states[i] && dirs[i]->type != BINDING_NONE)
                press_binding(result, dirs[i], true);
        }
    }
}

void element_stick_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y; (void)time_ms;
    e->stick_value_x = 0;
    e->stick_value_y = 0;
    for (int i = 0; i < 4; i++) {
        if (e->bindings[i].type != BINDING_NONE)
            press_binding(result, &e->bindings[i], false);
    }
    e->engaged = false;
    e->current_ptr_id = -1;
}
