#include "../touch_processor_internal.h"

#define RANGE_SCROLL_THRESHOLD 10

static void range_compute_sizes(const TouchElement* e, float* out_cw, float* out_ch, float* out_element_size) {
    TouchProcessorState* s = &g_state;
    float hs = s->snapping_size;
    *out_cw = hs * (e->range_binding_count * 2) * e->scale;
    *out_ch = hs * 2.0f * e->scale;
    if (e->range_orientation == 1) SWAP_F(*out_cw, *out_ch);
    int rbc = e->range_binding_count > 0 ? e->range_binding_count : 1;
    *out_element_size = (e->range_orientation == 0 ? *out_cw * 2.0f : *out_ch * 2.0f) / (float)rbc;
}

int range_keycode(int ordinal, int index) {
    static const int alphabet[26] = {38,56,54,40,26,41,42,43,31,44,45,46,58,57,32,33,24,27,39,28,30,55,25,53,29,52};
    static const int number[10]  = {10,11,12,13,14,15,16,17,18,19};
    static const int function[12]= {67,68,69,70,71,72,73,74,75,76,95,96};
    static const int numpad[10]  = {87,88,89,83,84,85,79,80,81,90};
    switch (ordinal) {
        case 0: if (index >= 0 && index < 26) return alphabet[index];
        case 1: if (index >= 0 && index < 10) return number[index];
        case 2: if (index >= 0 && index < 12) return function[index];
        case 3: if (index >= 0 && index < 10) return numpad[index];
    }
    return 0;
}

void element_range_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)ptr_id;
    e->range_scrolling = false;
    e->range_has_binding = false;
    e->range_hold_pressed = false;
    e->range_pending_tap_release = false;
    e->range_tap_release_time = 0;
    e->range_last_position = e->range_orientation == 0 ? x : y;

    float cw, ch, element_size;
    range_compute_sizes(e, &cw, &ch, &element_size);
    float left = e->x - cw;
    float top  = e->y - ch;

    float offset = e->range_orientation == 0 ? x - left - e->range_current_offset : y - top - e->range_current_offset;
    int index = (int)floorf(offset / element_size);
    if (e->range_max > 0) index %= e->range_max;
    if (index < 0) index += e->range_max;

    e->range_index = index;
    int kc = range_keycode(e->range_ordinal, index);
    e->range_initial_kc = kc;
    
    if (kc > 0) e->range_has_binding = true;
}

void element_range_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    TouchProcessorState* s = &g_state;
    float pos = e->range_orientation == 0 ? x : y;
    float delta = pos - e->range_last_position;

    if (!e->range_scrolling && fabsf(delta) >= RANGE_SCROLL_THRESHOLD) {
        e->range_scrolling = true;
        e->range_has_binding = false;
    }

    if (e->range_scrolling) {
        float cw, ch, element_size;
        range_compute_sizes(e, &cw, &ch, &element_size);
        float scroll_size = element_size * (float)e->range_max;

        e->range_current_offset += delta;
        e->range_scroll_offset = -fmodf(e->range_current_offset, scroll_size);
        if (e->range_scroll_offset < 0) e->range_scroll_offset += scroll_size;
        e->range_last_position = pos;
        s->visual_state_dirty = true;
    }
}

void element_range_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    (void)x; (void)y;
    (void)time_ms;

    if (e->range_hold_pressed) {
        // Hold press was already sent by tick — just release
        int kc = range_keycode(e->range_ordinal, e->range_index);
        if (kc > 0)
            add_action(result, ACT_KEY_RELEASE, kc, 0, 0);
        e->range_scrolling = false;
        e->range_has_binding = false;
        e->range_hold_pressed = false;
        return;
    }

    if (!e->range_has_binding) {
        // Java RangeScroller.handleTouchUp: when not a tap (scroll or hold-timeout),
        // sends handleInputEvent(binding, false) — release of the original touch-down binding.
        // The binding was captured at touch-down time and is NOT cleared during scroll.
        if (e->range_initial_kc > 0) {
            add_action(result, ACT_KEY_RELEASE, e->range_initial_kc, 0, 0);
        }
        e->range_scrolling = false;
        return;
    }

    uint64_t duration = time_ms - e->down_time_ms;
    bool is_tap = duration < RANGE_TAP_TIMEOUT_MS && !e->range_scrolling;

    int kc = range_keycode(e->range_ordinal, e->range_index);

    if (kc > 0) {
        if (is_tap) {
            // Tap (<200ms, no scroll): press immediately, schedule release for next tick
            // (matches Java postDelayed 30ms, but non-blocking)
            add_action(result, ACT_KEY_PRESS, kc, 1, 0);
            e->range_pending_tap_release = true;
            e->range_tap_release_time = now_ms() + 30;
            // Don't clear flags yet — deferred release in tick will do it
        } else {
            // Hold (>=200ms without hold-press, or scroll): press + release immediately
            add_action(result, ACT_KEY_PRESS, kc, 1, 0);
            add_action(result, ACT_KEY_RELEASE, kc, 0, 0);
            e->range_scrolling = false;
            e->range_has_binding = false;
            e->range_hold_pressed = false;
        }
    } else {
        e->range_scrolling = false;
        e->range_has_binding = false;
        e->range_hold_pressed = false;
    }
}
