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

static void dpad_update(TouchElement* e, float x, float y, TouchActionResult* result) {
    float radius = g_state.snapping_size * 7.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float nx, ny;
    dpad_normalize(dx, dy, radius, &nx, &ny);
    element_set_petals(e, nx, ny, DPAD_DEAD_ZONE, result);
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
