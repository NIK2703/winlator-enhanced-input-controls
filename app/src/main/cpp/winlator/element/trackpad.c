#include "../touch_processor_internal.h"

#define BIND_UP 0
#define BIND_RIGHT 1
#define BIND_DOWN 2
#define BIND_LEFT 3

// Hardcoded Java constants for trackpad acceleration (ControlElement.TRACKPAD_ACCELERATION_THRESHOLD=4)
#define TP_ACCEL_THRESHOLD 4.0f
// TouchpadView.CURSOR_ACCELERATION_THRESHOLD=6, CURSOR_ACCELERATION=1.25f
#define TP_CURSOR_ACCEL_THRESHOLD 6.0f
#define TP_CURSOR_ACCEL 1.25f

#define TRACKPAD_NORMALIZATION_FACTOR 0.05f
#define CURSOR_ACCEL_CLAMP_FACTOR (1.0f / 6.0f) // = 1.0f / TP_CURSOR_ACCEL_THRESHOLD
#define TRACKPAD_DEADZONE_PX 1.0f // 1.0f in view-pixel space; on high-DPI screens this maps to <1 physical pixel
// Used only in trackpad_move_gamepad — consider inlining if formula changes
#define ACCEL_MULTIPLIER(v, threshold, sensitivity) \
    (1.0f + ((sensitivity) - 1.0f) * ((v) - (threshold)) / ((v) + (threshold)))

static inline float apply_acceleration(float value, float abs_value, float threshold, float multiplier) {
    if (abs_value > threshold)
        return value * multiplier;
    return value;
}

static inline float normalize_with_sign(float value, float factor) {
    float n = fminf(1.0f, fabsf(value * factor));
    return (value < 0) ? -n : n;
}

static void trackpad_move_gamepad(TouchElement* e, float dx, float dy, TouchActionResult* restrict result) {
    // Java: TRACKPAD_ACCELERATION_THRESHOLD=4, STICK_SENSITIVITY=2.0f
    float abs_dx = fabsf(dx), abs_dy = fabsf(dy);
    float accel_mult_x = ACCEL_MULTIPLIER(abs_dx, TP_ACCEL_THRESHOLD, STICK_SENSITIVITY);
    float accel_mult_y = ACCEL_MULTIPLIER(abs_dy, TP_ACCEL_THRESHOLD, STICK_SENSITIVITY);
    float value_x = apply_acceleration(dx, abs_dx, TP_ACCEL_THRESHOLD, accel_mult_x);
    float value_y = apply_acceleration(dy, abs_dy, TP_ACCEL_THRESHOLD, accel_mult_y);

    // Normalize by TRACKPAD_NORMALIZATION_FACTOR and clamp to [-1, 1]
    float nx = normalize_with_sign(value_x, TRACKPAD_NORMALIZATION_FACTOR);
    float ny = normalize_with_sign(value_y, TRACKPAD_NORMALIZATION_FACTOR);

    // Java CubicBezierInterpolator with control points (0.075, 0.95, 0.45, 0.95)
    // Use cubic bezier binary search for x -> y mapping
    float interp_x = cubic_bezier_interpolate_trackpad(nx);
    float interp_y = cubic_bezier_interpolate_trackpad(ny);

    int is_left = !e->cached_bind0_is_right_stick;
    add_action(result, ACT_GAMEPAD_AXIS, is_left, (int)lroundf(interp_x * GAMEPAD_AXIS_MAX), (int)lroundf(interp_y * GAMEPAD_AXIS_MAX));

    e->trackpad_vel_x = interp_x;
    e->trackpad_vel_y = interp_y;
}

static void process_trackpad_cursor_binding(TouchElement* e, const TouchBinding* b, int index,
        float value_x, float value_y, int* cursor_dx, int* cursor_dy,
        const bool* states, TouchActionResult* restrict result) {
    if (b->type == BINDING_NONE) return;

    if (is_mouse_move_binding(b)) {
        float value = (index == BIND_RIGHT || index == BIND_LEFT) ? value_x : value_y;
        int delta = lroundf(value);
        if (index == BIND_RIGHT || index == BIND_LEFT) *cursor_dx += delta;
        if (index == BIND_UP || index == BIND_DOWN) *cursor_dy += delta;
        return;
    }

    bool active = states[index];
    if (active != e->petal_active[index]) {
        e->petal_active[index] = active;
        if (active) {
            press_binding(result, b, true);
        } else {
            release_binding(result, b);
        }
    }
}

static void trackpad_move_cursor(TouchElement* e, float dx, float dy, TouchActionResult* restrict result) {
    // Non-gamepad: per-slot bindings with TRACKPAD_MIN_SPEED as dead-zone threshold
    bool raw_up = dy <= -TRACKPAD_MIN_SPEED;
    bool raw_right = dx >= TRACKPAD_MIN_SPEED;
    bool raw_down = dy >= TRACKPAD_MIN_SPEED;
    bool raw_left = dx <= -TRACKPAD_MIN_SPEED;
    bool states[4] = {raw_up, raw_right, raw_down, raw_left};

    // Java: TouchpadView.CURSOR_ACCELERATION_THRESHOLD=6, CURSOR_ACCELERATION=1.25f
    float abs_dx = fabsf(dx), abs_dy = fabsf(dy);
    float accel_mult_x = 1.0f + (TP_CURSOR_ACCEL - 1.0f) * fminf((abs_dx - TP_CURSOR_ACCEL_THRESHOLD) * CURSOR_ACCEL_CLAMP_FACTOR, 1.0f);
    float accel_mult_y = 1.0f + (TP_CURSOR_ACCEL - 1.0f) * fminf((abs_dy - TP_CURSOR_ACCEL_THRESHOLD) * CURSOR_ACCEL_CLAMP_FACTOR, 1.0f);
    float value_x = apply_acceleration(dx, abs_dx, TP_CURSOR_ACCEL_THRESHOLD, accel_mult_x);
    float value_y = apply_acceleration(dy, abs_dy, TP_CURSOR_ACCEL_THRESHOLD, accel_mult_y);

    int cursor_dx = 0, cursor_dy = 0;
    if (e->cached_has_any_binding) {
        for (int i = 0; i < MAX_BINDINGS_PER_ELEMENT; i++) {
            process_trackpad_cursor_binding(e, &e->bindings[i], i,
                value_x, value_y, &cursor_dx, &cursor_dy, states, result);
        }
    }

    if (cursor_dx != 0 || cursor_dy != 0) {
        add_action(result, ACT_POINTER_MOVE_DELTA, cursor_dx, cursor_dy, 0);
    }
}

void element_trackpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id; (void)result;
    e->trackpad_last_x = x;
    e->trackpad_last_y = y;
    e->trackpad_last_time = time_ms;
    for (int i = 0; i < MAX_BINDINGS_PER_ELEMENT; i++) e->petal_active[i] = false;
    e->trackpad_vel_x = 0;
    e->trackpad_vel_y = 0;
}

void element_trackpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)time_ms;
    TouchProcessorState* s = &g_state;
    float dx = x - e->trackpad_last_x;
    float dy = y - e->trackpad_last_y;

    // Apply xform scale: maps view-pixels to Wine-screen-pixels (Java computeDeltaPoint)
    dx *= s->cfg.xform_scale_x;
    dy *= s->cfg.xform_scale_y;

    if (dx*dx + dy*dy < TRACKPAD_DEADZONE_PX * TRACKPAD_DEADZONE_PX) return;

    if (e->cached_bind0_is_gamepad)
        trackpad_move_gamepad(e, dx, dy, result);
    else
        trackpad_move_cursor(e, dx, dy, result);

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
