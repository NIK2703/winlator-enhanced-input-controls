#ifndef TOUCH_PROCESSOR_ACTIVATION_H
#define TOUCH_PROCESSOR_ACTIVATION_H

#include "touch_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Activation Mode Query ---

// Get current activation mode for a specific element
ActivationMode activation_get_mode(int elem_index);

// Set activation mode for all elements (batch update)
void activation_set_mode_all(ActivationMode mode);

// --- Per-Pointer Activation State ---

// Get visual state for all elements packed into arrays.
// positions: output array of float[count*2] holding (visual_x, visual_y) for each element
// active: output array of byte[count] holding 1 if visually active, 0 otherwise
// count: number of elements to read (up to element_count)
// Returns: number of elements actually written
int activation_get_visual_states(float* positions, uint8_t* active, int count);

// Get a single element's visual state
bool activation_get_element_visual(int elem_index, float* out_x, float* out_y, bool* out_active);

// --- Batch Activation Operations ---

// Activate all elements at the given point (sets visual_active for elements containing the point)
void activation_activate_at(float x, float y);

// Deactivate all elements (clears all visual_active flags)
void activation_deactivate_all(void);

// Get the number of tracked buttons for a given pointer
int activation_tracked_count(int ptr_id);

// Get tracked button element index at position for a pointer
int activation_tracked_at(int ptr_id, int index);

// Get hovered element index for a pointer (-1 if none)
int activation_hovered_for_ptr(int ptr_id);

// --- Legacy Java-compatible Activation Mode Handlers ---
// These mirror the InputControlsView.handleDownByMode/MoveByMode/UpByMode logic
// for the non-native fallback path, now implemented in C.

// Handle finger down with activation mode logic
// Returns true if the event was consumed by an element
bool activation_handle_down(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);

// Handle finger move with activation mode logic (LOCK/TRACK/HOVER)
void activation_handle_move(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);

// Handle finger up with activation mode logic
// Returns true if the event was consumed by tracked elements
bool activation_handle_up(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result);

// Reset all activation state (clear tracked, hovered, visual flags)
void activation_reset(void);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_PROCESSOR_ACTIVATION_H
