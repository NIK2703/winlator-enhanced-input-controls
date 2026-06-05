#ifndef TOUCH_PROCESSOR_ACTIVATION_INTERNAL_H
#define TOUCH_PROCESSOR_ACTIVATION_INTERNAL_H

#include "touch_processor_internal.h"

// Flip a toggle switch: release current binding / press new binding, update selected and visual state
static void flip_toggle_switch(TouchElement* btn, TouchActionResult* result);

// When two fingers are tracked, disable long-press on the first to prevent scroll conflicts
static void apply_two_finger_guard(TrackedButtons* tb);

// Process non-button element moves for a given pointer
static void process_non_button_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);

// Shared move logic for TRACK and HOVER modes (parameterized by is_hover)
static void handle_track_or_hover_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result, bool is_hover);

#endif // TOUCH_PROCESSOR_ACTIVATION_INTERNAL_H
