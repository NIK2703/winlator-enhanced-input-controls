#include "../touch_processor_internal.h"

void element_stick_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Stick", "DOWN idx=%d type=%d ptr_id=%d x=%f y=%f", (int)(e - g_state.elements), e->type, ptr_id, x, y);
    (void)ptr_id;
    for (int i = 0; i < 4; i++) e->petal_active[i] = false;
    // Java ControlElement.handleTouchDown delegates immediately to handleTouchMove,
    // sending axis values on touch-down (C was deferring to first ACTION_MOVE)
    element_stick_move(e, x, y, time_ms, result);
}

void element_stick_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)time_ms;
    TouchProcessorState* s = &g_state;
    float radius = s->snapping_size * 6.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist < 0.0001f) return;
    float inv_dist = 1.0f / dist;
    float nx, ny;
    if (dist > radius) {
        float clamped = radius * inv_dist;
        nx = dx * inv_dist;
        ny = dy * inv_dist;
        e->visual_x = e->x + dx * clamped;
        e->visual_y = e->y + dy * clamped;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Stick", "MOVE clamped idx=%d type=%d radius=%f visual_x=%f visual_y=%f", (int)(e - g_state.elements), e->type, radius, e->visual_x, e->visual_y);
    } else {
        float inv_radius = 1.0f / radius;
        nx = dx * inv_radius;
        ny = dy * inv_radius;
        e->visual_x = e->x + dx;
        e->visual_y = e->y + dy;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Stick", "MOVE inside idx=%d type=%d visual_x=%f visual_y=%f", (int)(e - g_state.elements), e->type, e->visual_x, e->visual_y);
    }

    //TP_LOG(ANDROID_LOG_DEBUG, "Winlator_StickBinding",
    //    "stick_move dist=%f nx=%f ny=%f bind0_type=0x%x is_gamepad=%d mag=%f",
    //    dist, nx, ny, e->bindings[0].type, is_gamepad_binding(&e->bindings[0]),
    //    fminf(dist * inv_dist, 1.0f));

    bool is_gamepad = e->cached_bind0_is_gamepad;
    if (is_gamepad) {
        e->stick_value_x = nx;
        e->stick_value_y = ny;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Stick", "MOVE values idx=%d type=%d stick_value_x=%f stick_value_y=%f", (int)(e - g_state.elements), e->type, e->stick_value_x, e->stick_value_y);
        int is_left = !e->cached_bind0_is_right_stick;
        //TP_LOG(ANDROID_LOG_DEBUG, "Winlator_StickBinding",
        //    "stick_move GAMEPAD AXIS is_left=%d axis_x=%f axis_y=%f",
        //    is_left, axis_x, axis_y);
        add_action(result, ACT_GAMEPAD_AXIS, is_left, (int)(nx * 32767), (int)(ny * 32767));
    } else {
        //TP_LOG(ANDROID_LOG_DEBUG, "Winlator_StickBinding",
        //    "stick_move PETAL mode bind0=0x%x bind1=0x%x bind2=0x%x bind3=0x%x",
        //    e->bindings[0].type, e->bindings[1].type, e->bindings[2].type, e->bindings[3].type);
        element_set_petals(e, nx, ny, STICK_DEAD_ZONE, result);
    }
}

void element_stick_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)x; (void)y; (void)time_ms;
    e->visual_x = e->x;
    e->visual_y = e->y;
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Stick", "UP center idx=%d type=%d center_x=%f center_y=%f", (int)(e - g_state.elements), e->type, e->x, e->y);
    e->stick_value_x = 0;
    e->stick_value_y = 0;
    if (e->cached_has_any_binding) {
        for (int i = 0; i < 4; i++) {
            if (__builtin_expect(e->petal_active[i], 0)) {
                e->petal_active[i] = false;
                if (e->bindings[i].type != BINDING_NONE && !(e->primary_sticky_mask & (1 << i)))
                    release_binding(result, &e->bindings[i]);
            }
        }
    }
    if (e->cached_bind0_is_gamepad) {
        add_action(result, ACT_GAMEPAD_AXIS, !e->cached_bind0_is_right_stick, 0, 0);
    }
}
