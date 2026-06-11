#include "../touch_processor_internal.h"

#ifdef TOUCH_STICK_DEBUG
#define STICK_DEBUG_LOG(e, fmt, ...) \
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Stick", "idx=%d type=%d " fmt, \
           (int)((e) - g_state.elements), (e)->type, ##__VA_ARGS__)
#else
#define STICK_DEBUG_LOG(e, fmt, ...) ((void)0)
#endif

void element_stick_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    STICK_DEBUG_LOG(e, "DOWN ptr_id=%d x=%f y=%f", ptr_id, x, y);
    (void)ptr_id;
    for (int i = 0; i < MAX_PETALS; i++) e->petal_active[i] = false;
    // Java ControlElement.handleTouchDown delegates immediately to handleTouchMove,
    // sending axis values on touch-down (C was deferring to first ACTION_MOVE)
    element_stick_move(e, x, y, time_ms, result);
}

void element_stick_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)time_ms;
    if (!e->cached_has_any_binding && !e->cached_bind0_is_gamepad) return;
    TouchProcessorState* s = &g_state;
    float radius = s->snapping_size * STICK_RADIUS_MULTIPLIER * e->scale;
    if (radius < NEAR_ZERO_THRESHOLD) return;
    float dx = x - e->x;
    float dy = y - e->y;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist < NEAR_ZERO_THRESHOLD) return;
    // EXACT DUPLICATION: stick.c:31-47 has identical normalize+clamp logic. Refactor into shared helper when touching both files.
    float inv_dist = 1.0f / dist;
    float nx, ny;
    if (dist > radius) {
        float clamped = radius * inv_dist;
        nx = dx * inv_dist;
        ny = dy * inv_dist;
        e->visual_x = e->x + dx * clamped;
        e->visual_y = e->y + dy * clamped;
        STICK_DEBUG_LOG(e, "MOVE clamped radius=%f visual_x=%f visual_y=%f", radius, e->visual_x, e->visual_y);
    } else {
        float inv_radius = 1.0f / radius;
        nx = dx * inv_radius;
        ny = dy * inv_radius;
        e->visual_x = e->x + dx;
        e->visual_y = e->y + dy;
        STICK_DEBUG_LOG(e, "MOVE inside visual_x=%f visual_y=%f", e->visual_x, e->visual_y);
    }

    bool is_gamepad = e->cached_bind0_is_gamepad;
    if (is_gamepad) {
        e->stick_value_x = nx;
        e->stick_value_y = ny;
        STICK_DEBUG_LOG(e, "MOVE values stick_value_x=%f stick_value_y=%f", e->stick_value_x, e->stick_value_y);
        int is_left = !e->cached_bind0_is_right_stick;
        // Use lroundf (not cast) for proper rounding at axis extremes
        add_action(result, ACT_GAMEPAD_AXIS, is_left, (int)lroundf(nx * GAMEPAD_AXIS_MAX), (int)lroundf(ny * GAMEPAD_AXIS_MAX));
    } else {
        element_set_petals(e, nx, ny, STICK_DEAD_ZONE, result);
    }
}

void element_stick_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)x; (void)y; (void)time_ms;
    e->visual_x = e->x;
    e->visual_y = e->y;
    STICK_DEBUG_LOG(e, "UP center center_x=%f center_y=%f", e->x, e->y);
    e->stick_value_x = 0;
    e->stick_value_y = 0;
    if (e->cached_has_any_binding) {
        release_element_petals(e, result);
    }
    if (e->cached_bind0_is_gamepad) {
        add_action(result, ACT_GAMEPAD_AXIS, !e->cached_bind0_is_right_stick, 0, 0);
    }
}
