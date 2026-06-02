#include "touch_processor_internal.h"

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
    bool up = ny <= -DPAD_DEAD_ZONE;
    bool right = nx >= DPAD_DEAD_ZONE;
    bool down = ny >= DPAD_DEAD_ZONE;
    bool left = nx <= -DPAD_DEAD_ZONE;
    for (int i = 0; i < 4; i++) {
        bool active = (i == 0) ? up : ((i == 1) ? right : ((i == 2) ? down : left));
        if (active != e->petal_active[i]) {
            e->petal_active[i] = active;
            LOGD("  petal[%d] %s (bind.type=%d)", i, active ? "PRESS" : "RELEASE", e->bindings[i].type);
            if (e->bindings[i].type != BINDING_NONE)
                press_binding(result, &e->bindings[i], active);
        }
    }
}

void element_dpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)time_ms;
    float radius = g_state.snapping_size * 7.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float nx, ny;
    dpad_normalize(dx, dy, radius, &nx, &ny);
    LOGD("element_dpad_down: touch=(%.0f,%.0f) center=(%d,%d) radius=%.0f nx=%.2f ny=%.2f",
         x, y, e->x, e->y, radius, nx, ny);
    dpad_set_petals(e, nx, ny, result);
}

void element_dpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    float radius = g_state.snapping_size * 7.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float nx, ny;
    dpad_normalize(dx, dy, radius, &nx, &ny);

    bool inside = (dx*dx + dy*dy) <= radius*radius;
    LOGD("elem_move dpad: (%.0f,%.0f) center=(%d,%d) radius=%.0f nx=%.2f ny=%.2f %s",
         x, y, e->x, e->y, radius, nx, ny, inside ? "INSIDE" : "OUTSIDE");

    dpad_set_petals(e, nx, ny, result);
}

void element_dpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y; (void)time_ms;
    for (int i = 0; i < 4; i++) {
        e->petal_active[i] = false;
        if (e->bindings[i].type != BINDING_NONE)
            press_binding(result, &e->bindings[i], false);
    }
    e->engaged = false;
    e->current_ptr_id = -1;
}
