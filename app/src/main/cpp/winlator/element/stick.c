#include "../touch_processor_internal.h"
#include <android/log.h>

void element_stick_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id;
    for (int i = 0; i < 4; i++) e->petal_active[i] = false;
    // Java ControlElement.handleTouchDown delegates immediately to handleTouchMove,
    // sending axis values on touch-down (C was deferring to first ACTION_MOVE)
    element_stick_move(e, x, y, time_ms, result);
}

void element_stick_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)time_ms;
    TouchProcessorState* s = &g_state;
    float radius = s->snapping_size * 6.0f * e->scale;
    float dx = x - e->x;
    float dy = y - e->y;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist < 0.0001f) return;
    float inv_dist = dist > 0.0f ? 1.0f / dist : 0.0f;
    float nx, ny;
    if (dist > radius) {
        float clamped = radius * inv_dist;
        nx = dx * inv_dist;
        ny = dy * inv_dist;
        e->visual_x = e->x + dx * clamped;
        e->visual_y = e->y + dy * clamped;
    } else {
        float inv_radius = 1.0f / radius;
        nx = dx * inv_radius;
        ny = dy * inv_radius;
        e->visual_x = e->x + dx;
        e->visual_y = e->y + dy;
    }

    //__android_log_print(ANDROID_LOG_DEBUG, "Winlator_StickBinding",
    //    "stick_move dist=%f nx=%f ny=%f bind0_type=0x%x is_gamepad=%d mag=%f",
    //    dist, nx, ny, e->bindings[0].type, is_gamepad_binding(&e->bindings[0]),
    //    fminf(dist * inv_dist, 1.0f));

    if (is_gamepad_binding(&e->bindings[0])) {
        float mag = fminf(dist * inv_dist, 1.0f);
        float axis_x = 0, axis_y = 0;
        if (mag > STICK_DEAD_ZONE) {
            float scaled = fminf(fmaxf(0.0f, mag - 0.01f) * STICK_SENSITIVITY, 1.0f);
            axis_x = nx * scaled;
            axis_y = ny * scaled;
        }
        e->stick_value_x = axis_x;
        e->stick_value_y = axis_y;
        int is_left = !is_right_stick_binding(e);
        //__android_log_print(ANDROID_LOG_DEBUG, "Winlator_StickBinding",
        //    "stick_move GAMEPAD AXIS is_left=%d axis_x=%f axis_y=%f",
        //    is_left, axis_x, axis_y);
        add_action(result, ACT_GAMEPAD_AXIS, is_left, (int)(axis_x * 32767), (int)(axis_y * 32767));
    } else {
        //__android_log_print(ANDROID_LOG_DEBUG, "Winlator_StickBinding",
        //    "stick_move PETAL mode bind0=0x%x bind1=0x%x bind2=0x%x bind3=0x%x",
        //    e->bindings[0].type, e->bindings[1].type, e->bindings[2].type, e->bindings[3].type);
        element_set_petals(e, nx, ny, STICK_DEAD_ZONE, result);
    }
}

void element_stick_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y; (void)time_ms;
    e->visual_x = e->x;
    e->visual_y = e->y;
    e->stick_value_x = 0;
    e->stick_value_y = 0;
    for (int i = 0; i < 4; i++) {
        if (e->petal_active[i]) {
            e->petal_active[i] = false;
            if (e->bindings[i].type != BINDING_NONE && !(e->primary_sticky_mask & (1 << i)))
                release_binding(result, &e->bindings[i]);
        }
    }
    if (is_gamepad_binding(&e->bindings[0])) {
        add_action(result, ACT_GAMEPAD_AXIS, !is_right_stick_binding(e), 0, 0);
    }
}
