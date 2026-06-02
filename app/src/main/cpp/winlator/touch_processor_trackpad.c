#include "touch_processor_internal.h"

void element_trackpad_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id; (void)result;
    e->trackpad_last_x = x;
    e->trackpad_last_y = y;
    e->trackpad_last_time = time_ms;
}

void element_trackpad_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    float dx = x - e->trackpad_last_x;
    float dy = y - e->trackpad_last_y;
    uint64_t dt = time_ms - e->trackpad_last_time;
    if (dt > 0) {
        float speed = sqrtf(dx*dx + dy*dy) / dt * 1000.0f;
        if (speed < TRACKPAD_MIN_SPEED) { dx *= speed / TRACKPAD_MIN_SPEED; dy *= speed / TRACKPAD_MIN_SPEED; }
        if (speed > TRACKPAD_MAX_SPEED) { float s = TRACKPAD_MAX_SPEED / speed; dx *= s; dy *= s; }
        add_action(result, ACT_POINTER_MOVE_DELTA, (int)dx, (int)dy, 0);
    }
    e->trackpad_last_x = x;
    e->trackpad_last_y = y;
    e->trackpad_last_time = time_ms;
}

void element_trackpad_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y; (void)time_ms;
    for (int i = 0; i < 4; i++) {
        if (e->bindings[i].type != BINDING_NONE)
            press_binding(result, &e->bindings[i], false);
    }
    e->engaged = false;
    e->current_ptr_id = -1;
}
