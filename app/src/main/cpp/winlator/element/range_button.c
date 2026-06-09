#include "../touch_processor_internal.h"

#define RANGE_SCROLL_THRESHOLD 10

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

void element_range_button_down(TouchElement* e, int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)ptr_id;
    e->range_scrolling = false;
    e->range_has_binding = false;
    e->range_hold_pressed = false;
    e->range_pending_tap_release = false;
    e->range_tap_release_time = 0;
    e->range_last_position = e->range_orientation == 0 ? x : y;

    float cw, ch, element_size;
    cw = e->cached_range_cw;
    ch = e->cached_range_ch;
    element_size = e->cached_range_element_size;
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
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_down[%d] x=%.0f y=%.0f ordinal=%d max=%d has_binding=%d range_index=%d initial_kc=%d",
        (int)(e - g_state.elements), x, y, e->range_ordinal, e->range_max, e->range_has_binding, e->range_index, kc);
}

void element_range_button_move(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    TouchProcessorState* s = &g_state;
    float pos = e->range_orientation == 0 ? x : y;
    float delta = pos - e->range_last_position;
    int idx = (int)(e - g_state.elements);

    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_move[%d] x=%.0f y=%.0f pos=%.0f delta=%.0f scrolling=%d",
        idx, x, y, pos, delta, e->range_scrolling);

    if (!e->range_scrolling && fabsf(delta) >= RANGE_SCROLL_THRESHOLD) {
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_move[%d] scroll_activated threshold=%d delta=%.0f",
            idx, RANGE_SCROLL_THRESHOLD, delta);
        e->range_scrolling = true;
        e->range_has_binding = false;
    }

    if (e->range_scrolling) {
        float cw, ch, element_size;
        cw = e->cached_range_cw;
        ch = e->cached_range_ch;
        element_size = e->cached_range_element_size;
        float scroll_size = element_size * (float)e->range_max;
        float inv_scroll_size = 1.0f / scroll_size;

        e->range_current_offset += delta;
        float offset_mod = e->range_current_offset - ((int)(e->range_current_offset * inv_scroll_size)) * scroll_size;
        e->range_scroll_offset = -offset_mod;
        e->range_last_position = pos;
        mark_element_dirty(e);
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_move[%d] scroll_offset=%.0f current_offset=%.0f scroll_size=%.0f",
            idx, e->range_scroll_offset, e->range_current_offset, scroll_size);
    }
}

void element_range_button_up(TouchElement* e, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    (void)x; (void)y;
    (void)time_ms;
    int idx = (int)(e - g_state.elements);

    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] x=%.0f y=%.0f hold_pressed=%d has_binding=%d scrolling=%d initial_kc=%d",
        idx, x, y, e->range_hold_pressed, e->range_has_binding, e->range_scrolling, e->range_initial_kc);

    if (e->range_hold_pressed) {
        // Hold press was already sent by tick — just release
        int kc = range_keycode(e->range_ordinal, e->range_index);
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] HOLD_RELEASE kc=%d", idx, kc);
        if (kc > 0)
            add_action(result, ACT_KEY_RELEASE, kc, 0, 0);
        e->range_scrolling = false;
        e->range_has_binding = false;
        e->range_hold_pressed = false;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] final_state hold_pressed=0 has_binding=0 scrolling=0", idx);
        return;
    }

    if (!e->range_has_binding) {
        // Java RangeScroller.handleTouchUp: when not a tap (scroll or hold-timeout),
        // sends handleInputEvent(binding, false) — release of the original touch-down binding.
        // The binding was captured at touch-down time and is NOT cleared during scroll.
        if (e->range_initial_kc > 0) {
            TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] SCROLL_RELEASE initial_kc=%d", idx, e->range_initial_kc);
            add_action(result, ACT_KEY_RELEASE, e->range_initial_kc, 0, 0);
        }
        e->range_scrolling = false;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] final_state scrolling=0 (no_binding)", idx);
        return;
    }

    uint64_t duration = time_ms - e->down_time_ms;
    bool is_tap = duration < RANGE_TAP_TIMEOUT_MS && !e->range_scrolling;
    const char* decision = is_tap ? "TAP" : "HOLD_IMMEDIATE";
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] decision=%s duration=%llu scrolling=%d",
        idx, decision, (unsigned long long)duration, e->range_scrolling);

    int kc = range_keycode(e->range_ordinal, e->range_index);

    if (kc > 0) {
        if (is_tap) {
            // Tap (<200ms, no scroll): press immediately, schedule release for next tick
            // (matches Java postDelayed 30ms, but non-blocking)
            TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] TAP press kc=%d schedule_release", idx, kc);
            add_action(result, ACT_KEY_PRESS, kc, 1, 0);
            e->range_pending_tap_release = true;
            e->range_tap_release_time = time_ms + 30;
            // Don't clear flags yet — deferred release in tick will do it
        } else {
            // Hold (>=200ms without hold-press, or scroll): press + release immediately
            TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] HOLD_IMMEDIATE press+release kc=%d", idx, kc);
            add_action(result, ACT_KEY_PRESS, kc, 1, 0);
            add_action(result, ACT_KEY_RELEASE, kc, 0, 0);
            e->range_scrolling = false;
            e->range_has_binding = false;
            e->range_hold_pressed = false;
        }
    } else {
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] kc=0 clearing state", idx);
        e->range_scrolling = false;
        e->range_has_binding = false;
        e->range_hold_pressed = false;
    }
    TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Range", "range_up[%d] final_state hold_pressed=%d has_binding=%d scrolling=%d pending_release=%d",
        idx, e->range_hold_pressed, e->range_has_binding, e->range_scrolling, e->range_pending_tap_release);
}
