#include "../touch_processor_internal.h"

// Hardcoded Java constants for trackpad acceleration (ControlElement.TRACKPAD_ACCELERATION_THRESHOLD=4)
#define TP_ACCEL_THRESHOLD 4.0f
// TouchpadView.CURSOR_ACCELERATION_THRESHOLD=6, CURSOR_ACCELERATION=1.25f
#define TP_CURSOR_ACCEL_THRESHOLD 6.0f
#define TP_CURSOR_ACCEL 1.25f

void element_trackpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id; (void)result;
    e->trackpad_last_x = x;
    e->trackpad_last_y = y;
    e->trackpad_last_time = time_ms;
    for (int i = 0; i < 4; i++) e->petal_active[i] = false;
}

void element_trackpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    TouchProcessorState* s = &g_state;
    float dx = x - e->trackpad_last_x;
    float dy = y - e->trackpad_last_y;
    if (fabsf(dx) < 1.0f && fabsf(dy) < 1.0f) return;

    // Apply xform scale: maps view-pixels to Wine-screen-pixels (Java computeDeltaPoint)
    dx *= s->cfg.xform_scale_x;
    dy *= s->cfg.xform_scale_y;

    if (e->cached_bind0_is_gamepad) {
        // Java: TRACKPAD_ACCELERATION_THRESHOLD=4, STICK_SENSITIVITY=2.0f
        float abs_dx = fabsf(dx), abs_dy = fabsf(dy);
        float value_x = dx, value_y = dy;
        if (abs_dx > TP_ACCEL_THRESHOLD)
            value_x *= 1.0f + (STICK_SENSITIVITY - 1.0f) * (abs_dx - TP_ACCEL_THRESHOLD) / (abs_dx + TP_ACCEL_THRESHOLD);
        if (abs_dy > TP_ACCEL_THRESHOLD)
            value_y *= 1.0f + (STICK_SENSITIVITY - 1.0f) * (abs_dy - TP_ACCEL_THRESHOLD) / (abs_dy + TP_ACCEL_THRESHOLD);

        // Normalize by TRACKPAD_MAX_SPEED and clamp to [-1, 1]
        float nx = fminf(1.0f, fabsf(value_x * 0.05f));
        float ny = fminf(1.0f, fabsf(value_y * 0.05f));
        if (value_x < 0) nx = -nx;
        if (value_y < 0) ny = -ny;

        // Java CubicBezierInterpolator with control points (0.075, 0.95, 0.45, 0.95)
        // Use cubic bezier binary search for x -> y mapping
        float interp_x = cubic_bezier_interpolate_trackpad(nx);
        float interp_y = cubic_bezier_interpolate_trackpad(ny);

        int is_left = !e->cached_bind0_is_right_stick;
        add_action(result, ACT_GAMEPAD_AXIS, is_left, (int)(interp_x * 32767), (int)(interp_y * 32767));

        e->trackpad_vel_x = interp_x;
        e->trackpad_vel_y = interp_y;
    } else {
        // Non-gamepad: per-slot bindings with TRACKPAD_MIN_SPEED as dead-zone threshold
        bool raw_up = dy <= -TRACKPAD_MIN_SPEED;
        bool raw_right = dx >= TRACKPAD_MIN_SPEED;
        bool raw_down = dy >= TRACKPAD_MIN_SPEED;
        bool raw_left = dx <= -TRACKPAD_MIN_SPEED;
        bool states[4] = {raw_up, raw_right, raw_down, raw_left};

        // Java: TouchpadView.CURSOR_ACCELERATION_THRESHOLD=6, CURSOR_ACCELERATION=1.25f
        float abs_dx = fabsf(dx), abs_dy = fabsf(dy);
        float value_x = dx, value_y = dy;
        if (abs_dx > TP_CURSOR_ACCEL_THRESHOLD)
            value_x *= 1.0f + (TP_CURSOR_ACCEL - 1.0f) * fminf((abs_dx - TP_CURSOR_ACCEL_THRESHOLD) * 0.16666667f, 1.0f);
        if (abs_dy > TP_CURSOR_ACCEL_THRESHOLD)
            value_y *= 1.0f + (TP_CURSOR_ACCEL - 1.0f) * fminf((abs_dy - TP_CURSOR_ACCEL_THRESHOLD) * 0.16666667f, 1.0f);

        int cursor_dx = 0, cursor_dy = 0;
        if (e->cached_has_any_binding) {
            for (int i = 0; i < 4; i++) {
                const TouchBinding* b = &e->bindings[i];
                if (b->type == BINDING_NONE) continue;

                if (is_mouse_move_binding(b)) {
                    float value = (i == 1 || i == 3) ? value_x : value_y;
                    int delta = lrintf(value);
                    if (i == 1 || i == 3) cursor_dx += delta;
                    if (i == 0 || i == 2) cursor_dy += delta;
                    continue;
                }

                bool active = states[i];
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

        if (cursor_dx != 0 || cursor_dy != 0) {
            add_action(result, ACT_POINTER_MOVE_DELTA, cursor_dx, cursor_dy, 0);
        }
    }

    e->trackpad_last_x = x;
    e->trackpad_last_y = y;
}

void element_trackpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)x; (void)y; (void)time_ms;
    if (e->cached_has_any_binding) {
        release_element_petals(e, result);
        if (e->cached_bind0_is_gamepad) {
            add_action(result, ACT_GAMEPAD_AXIS, !e->cached_bind0_is_right_stick, 0, 0);
        }
    }
}
