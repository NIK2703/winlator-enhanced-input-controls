#include "../touch_processor_internal.h"

// Hardcoded Java constants for trackpad acceleration (ControlElement.TRACKPAD_ACCELERATION_THRESHOLD=4)
#define TP_ACCEL_THRESHOLD 4.0f
// TouchpadView.CURSOR_ACCELERATION_THRESHOLD=6, CURSOR_ACCELERATION=1.25f
#define TP_CURSOR_ACCEL_THRESHOLD 6.0f
#define TP_CURSOR_ACCEL 1.25f

void element_trackpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)result;
    e->trackpad_last_x = x;
    e->trackpad_last_y = y;
    e->trackpad_last_time = time_ms;
    for (int i = 0; i < 4; i++) e->petal_active[i] = false;
}

void element_trackpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    float dx = x - e->trackpad_last_x;
    float dy = y - e->trackpad_last_y;

    // Apply xform scale: maps view-pixels to Wine-screen-pixels (Java computeDeltaPoint)
    dx *= g_state.cfg.xform_scale_x;
    dy *= g_state.cfg.xform_scale_y;

    bool is_gamepad = e->bindings[0].type >= BINDING_GAMEPAD_BASE && e->bindings[0].type < BINDING_GAMEPAD_BASE + 24;

    if (is_gamepad) {
        // Java: TRACKPAD_ACCELERATION_THRESHOLD=4, STICK_SENSITIVITY=2.0f
        float value_x = dx, value_y = dy;
        if (fabsf(value_x) > TP_ACCEL_THRESHOLD)
            value_x *= STICK_SENSITIVITY;
        if (fabsf(value_y) > TP_ACCEL_THRESHOLD)
            value_y *= STICK_SENSITIVITY;

        // Normalize by TRACKPAD_MAX_SPEED and clamp to [-1, 1]
        float nx = fminf(1.0f, fabsf(value_x / TRACKPAD_MAX_SPEED));
        float ny = fminf(1.0f, fabsf(value_y / TRACKPAD_MAX_SPEED));
        if (value_x < 0) nx = -nx;
        if (value_y < 0) ny = -ny;

        // Java CubicBezierInterpolator with control points (0.075, 0.95, 0.45, 0.95)
        // Use cubic bezier binary search for x -> y mapping
        float interp_x = cubic_bezier_interpolate(nx, 0.075f, 0.95f);
        float interp_y = cubic_bezier_interpolate(ny, 0.075f, 0.95f);

        int bt = e->bindings[0].type - BINDING_GAMEPAD_BASE;
        int is_left = 1;
        if (bt >= 16 && bt <= 19) is_left = 0; // GAMEPAD_RIGHT_THUMB = ordinals 16-19
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
        float value_x = dx, value_y = dy;
        if (fabsf(value_x) > TP_CURSOR_ACCEL_THRESHOLD)
            value_x *= TP_CURSOR_ACCEL;
        if (fabsf(value_y) > TP_CURSOR_ACCEL_THRESHOLD)
            value_y *= TP_CURSOR_ACCEL;

        int cursor_dx = 0, cursor_dy = 0;
        for (int i = 0; i < 4; i++) {
            const TouchBinding* b = &e->bindings[i];
            if (b->type == BINDING_NONE) continue;

            if (is_mouse_move_binding(b)) {
                float value = (i == 1 || i == 3) ? value_x : value_y;
                // Java Mathf.roundPoint: (int)(x <= 0 ? floor(x) : ceil(x))
                if (i == 1 || i == 3) cursor_dx = (int)(value <= 0 ? floorf(value) : ceilf(value));
                if (i == 0 || i == 2) cursor_dy = (int)(value <= 0 ? floorf(value) : ceilf(value));
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

        if (cursor_dx != 0 || cursor_dy != 0) {
            add_action(result, ACT_POINTER_MOVE_DELTA, cursor_dx, cursor_dy, 0);
        }
    }

    e->trackpad_last_x = x;
    e->trackpad_last_y = y;
    e->trackpad_last_time = time_ms;
}

void element_trackpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y; (void)time_ms;
    for (int i = 0; i < 4; i++) {
        if (e->petal_active[i]) {
            e->petal_active[i] = false;
            if (e->bindings[i].type != BINDING_NONE)
                release_binding(result, &e->bindings[i]);
        }
    }
    // Java ControlElement.handleTouchUp: zero gamepad axes on finger-up for stick/trackpad
    if (e->bindings[0].type >= BINDING_GAMEPAD_BASE && e->bindings[0].type < BINDING_GAMEPAD_BASE + 24) {
        int bt = e->bindings[0].type - BINDING_GAMEPAD_BASE;
        int is_left = 1;
        if (bt >= 16 && bt <= 19) is_left = 0;
        add_action(result, ACT_GAMEPAD_AXIS, is_left, 0, 0);
    }
    e->engaged = false;
    e->current_ptr_id = -1;
}
