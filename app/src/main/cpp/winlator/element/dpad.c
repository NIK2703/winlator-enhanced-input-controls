#include "../touch_processor_internal.h"

static void dpad_normalize(float dx, float dy, float radius, float* out_nx, float* out_ny) {
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist > radius) {
        dx = dx / dist * radius;
        dy = dy / dist * radius;
    }
    *out_nx = fmaxf(-1.0f, fminf(1.0f, dx / radius));
    *out_ny = fmaxf(-1.0f, fminf(1.0f, dy / radius));
}

static void dpad_set_petals(TouchElement* e, float nx, float ny, TouchActionResult* result) {
    bool raw_up = ny <= -DPAD_DEAD_ZONE;
    bool raw_right = nx >= DPAD_DEAD_ZONE;
    bool raw_down = ny >= DPAD_DEAD_ZONE;
    bool raw_left = nx <= -DPAD_DEAD_ZONE;
    bool states[4] = {raw_up, raw_right, raw_down, raw_left};
    for (int i = 0; i < 4; i++) {
        const TouchBinding* b = &e->bindings[i];
        if (b->type == BINDING_NONE) continue;
        bool active;
        if (is_mouse_move_binding(b))
            active = states[i] || states[(i + 2) % 4]; // bidirectional: up||down, left||right
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

static void dpad_update(TouchElement* e, float x, float y, TouchActionResult* result) {
    float radius = g_state.snapping_size * 7.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float nx, ny;
    dpad_normalize(dx, dy, radius, &nx, &ny);
    dpad_set_petals(e, nx, ny, result);
}

void element_dpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)time_ms;
    dpad_update(e, x, y, result);
}

void element_dpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    dpad_update(e, x, y, result);
}

void element_dpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y; (void)time_ms;
    for (int i = 0; i < 4; i++) {
        if (e->petal_active[i]) {
            e->petal_active[i] = false;
            if (e->bindings[i].type != BINDING_NONE)
                release_binding(result, &e->bindings[i]);
        }
    }
    e->engaged = false;
    e->current_ptr_id = -1;
}
