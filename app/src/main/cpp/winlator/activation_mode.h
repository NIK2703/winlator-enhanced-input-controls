#ifndef ACTIVATION_MODE_H
#define ACTIVATION_MODE_H

#include "touch_processor.h"

typedef enum {
    BTN_DOWN_ALL,       // LOCK: engage all hit buttons
    BTN_DOWN_FIRST,     // TRACK/HOVER: engage first button only (tracked)
} ButtonDownMode;

typedef enum {
    BTN_MOVE_LOCK,      // LOCK: no new buttons on move
    BTN_MOVE_ACCUM,     // TRACK: accumulate buttons as finger slides
    BTN_MOVE_TRANS,     // HOVER: transition (release prev, activate curr)
} ButtonMoveMode;

typedef enum {
    NONBTN_MOVE_LOCK,        // LOCK: only engaged non-buttons
    NONBTN_MOVE_ACCUM,       // TRACK: engaged + activate new under finger
    NONBTN_MOVE_HOVER,       // HOVER: release-on-exit + activate new
} NonButtonMoveMode;

typedef enum {
    UP_RELEASE_ENGAGED,     // LOCK/HOVER: release engaged elements only
    UP_RELEASE_TRACKED,     // TRACK: release all tracked buttons
} UpReleaseMode;

// Complete parameterization of activation mode behavior
// A mode is fully defined by 8 parameters:
//   down: how buttons engage on finger-down
//   move: how non-buttons and buttons behave during finger-move
//   up:   how elements release on finger-up
typedef struct {
    ButtonDownMode     down_button;
    bool               down_set_hovered;
    bool               down_skip_off_toggle;

    NonButtonMoveMode  move_nonbutton;
    ButtonMoveMode     move_button;
    bool               move_gesture_move;

    UpReleaseMode      up_release;
    bool               up_clear_hovered;
    bool               up_reset_tracked;
    bool               skip_reentry_down;  // skip handle_element_down on re-entry of first tracked button
} ActivationModeParams;

extern const ActivationModeParams ACTIVATION_MODE_LOCK;
extern const ActivationModeParams ACTIVATION_MODE_TRACK;
extern const ActivationModeParams ACTIVATION_MODE_HOVER;

// Unified element-down handler (all 3 activation modes)
// Hit-tests at (x,y), engages elements according to *params.
bool activation_mode_down(int ptr_id, float x, float y, uint64_t time_ms,
                          TouchActionResult* result, const ActivationModeParams* params);

// Unified non-button move handler (all 3 modes)
// Processes engaged non-buttons: move events + optional release-on-exit + optional new activation.
void activation_mode_move_nonbuttons(int ptr_id, float x, float y, uint64_t time_ms,
                                     TouchActionResult* result, const ActivationModeParams* params);

// Unified button move handler (all 3 modes)
// Manages tracking lists, transitions, toggle-slide-over per *params.
// Returns the hit button index at (x,y), or -1 if none.
int activation_mode_move_buttons(int ptr_id, float x, float y, uint64_t time_ms,
                                 TouchActionResult* result, const ActivationModeParams* params);

// Unified element-up handler (all 3 modes)
// Releases elements according to *params.
bool activation_mode_up(int ptr_id, float x, float y, uint64_t time_ms,
                        TouchActionResult* result, const ActivationModeParams* params);

#endif
