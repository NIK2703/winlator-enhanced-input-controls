#include "../touch_processor_internal.h"

static void dpad_normalize(float dx, float dy, float radius, float* restrict out_nx, float* restrict out_ny) {
    float dist = sqrtf(dx*dx + dy*dy);
    float scale = (dist > radius) ? (1.0f / dist) : (1.0f / radius);
    *out_nx = dx * scale;
    *out_ny = dy * scale;
}

static void dpad_update(TouchElement* e, float x, float y, TouchActionResult* restrict result) {
    TouchProcessorState* s = &g_state;
    float radius = s->snapping_size * 7.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    if (fabsf(dx) < 0.0001f && fabsf(dy) < 0.0001f) return;
    float nx, ny;
    dpad_normalize(dx, dy, radius, &nx, &ny);
    element_set_petals(e, nx, ny, DPAD_DEAD_ZONE, result);
}

void element_dpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id; (void)time_ms;
    __android_log_print(ANDROID_LOG_WARN, "Winlator_DBG", "DPAD_DOWN[%d] pos(%d,%d) finger(%.0f,%.0f) hw=%.0f hh=%.0f",
        (int)(e - g_state.elements), e->x, e->y, x, y, e->hw, e->hh);
    dpad_update(e, x, y, result);
}

void element_dpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)time_ms;
    float fx = -1.0f, fy = -1.0f;
    TouchFinger* fp = find_finger(e->current_ptr_id);
    if (fp) { fx = fp->x; fy = fp->y; }
    __android_log_print(ANDROID_LOG_WARN, "Winlator_DBG", "DPAD_MOVE[%d] finger(%.0f,%.0f) dx=%.0f dy=%.0f fxy=(%.0f,%.0f) down=(%.0f,%.0f) vis=(%.0f,%.0f) ret=%p",
        (int)(e - g_state.elements), x, y, x - e->x, y - e->y,
        fx, fy, e->down_x, e->down_y, e->visual_x, e->visual_y,
        __builtin_return_address(0));
    dpad_update(e, x, y, result);
}

void element_dpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)x; (void)y; (void)time_ms;
    if (!e->cached_has_any_binding) return;
    for (int i = 0; i < 4; i++) {
        if (__builtin_expect(e->petal_active[i], 0)) {
            e->petal_active[i] = false;
            if (e->bindings[i].type != BINDING_NONE && !(e->primary_sticky_mask & (1 << i)))
                release_binding(result, &e->bindings[i]);
        }
    }
}
