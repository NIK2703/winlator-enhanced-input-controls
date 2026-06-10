#include "../touch_processor_internal.h"
#include "types.h"
#include "branch.h"

// ============================================================
// Per-finger gesture context
// ============================================================
GestureFingerCtx g_ctx[MAX_FINGERS];

// ============================================================
// Gesture pair slots — encode ALL differences as data
//   Slot 0: S/Sd  (main finger single tap)
//   Slot 1: D/Dd  (main finger double tap)
//   Slot 2: L/Ld  (main finger long press)
//   Slot 3: S2/Sd2 (second finger single tap)
//   Slot 4: D2/Dd2 (second finger double tap)
// ============================================================
const GesturePairSlot GESTURE_SLOTS[5] = {
    // Slot 0: S/Sd  (main finger single tap)
    //  bind_non_drag     bind_drag              tp_non   tp_drag     comp_dt              comp_lp         is2 comp_dt comp_lp
    { GESTURE_SINGLE_TAP, GESTURE_SINGLE_TAP_DRAG,  0,       0,         GESTURE_DOUBLE_TAP,  GESTURE_LONG_PRESS,     false, true,  true,
    //  down_save_fb  agg_tp_delta  hold_check  resolve_fb  press_always  gate_tp_nodrag  tick_s_exec  tick_dt_fb
        false,        false,        false,      false,      false,        true,           false,       false },

    // Slot 1: D/Dd  (main finger double tap)
    { GESTURE_DOUBLE_TAP, GESTURE_DOUBLE_TAP_DRAG,  0,       0,         0,                   0,                      false, false, false,
        false, false, false, false, false, true,  false, false },

    // Slot 2: L/Ld  (main finger long press)
    { GESTURE_LONG_PRESS, GESTURE_LONG_PRESS_DRAG,  0,       0,         GESTURE_DOUBLE_TAP,  0,                      false, true,  false,
        false, false, false, false, false, true,  false, false },

    // Slot 3: S2/Sd2 (second finger single tap)
    { GESTURE_SINGLE_2ND, GESTURE_SINGLE_DRAG_2ND,  GESTURE_SINGLE_2ND, GESTURE_SINGLE_DRAG_2ND, GESTURE_DOUBLE_2ND, 0,  true,  true,  false,
        true,  true,  true,  true,  true,  false, true,  true },

    // Slot 4: D2/Dd2 (second finger double tap)
    { GESTURE_DOUBLE_2ND, GESTURE_DOUBLE_DRAG_2ND,  GESTURE_DOUBLE_2ND, GESTURE_DOUBLE_DRAG_2ND, 0,                   0,  true,  false, false,
        true,  true,  true,  true,  true,  false, false, true  },
};

// ============================================================
// Slot selection — picks the right slot for a finger
// ============================================================
const GesturePairSlot* select_gesture_slot(const TouchFinger* f) {
    if (f->cached_has_active_long_press || f->cached_has_active_long_press_drag) {
        if (f->is_second_finger) return NULL; // no L/Ld for second finger
        return &GESTURE_SLOTS[2];
    }
    if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag) {
        return f->is_second_finger ? &GESTURE_SLOTS[4] : &GESTURE_SLOTS[1];
    }
    return f->is_second_finger ? &GESTURE_SLOTS[3] : &GESTURE_SLOTS[0];
}

// ============================================================
// Unified binding resolution
// ============================================================
void resolve_binding_slot(
    const TouchFinger* f,
    const GesturePairSlot* slot,
    const TouchBinding** non_drag, int* non_drag_count,
    const TouchBinding** drag, int* drag_count)
{
    if (slot->is_second_finger && g_state.cfg.is_tp) {
        *non_drag = g_state.cfg.tp[slot->tp_non_drag].arr;
        *non_drag_count = g_state.cfg.tp[slot->tp_non_drag].count;
        *drag = g_state.cfg.tp[slot->tp_drag].arr;
        *drag_count = g_state.cfg.tp[slot->tp_drag].count;
    } else {
        const FingerBindings* fb = &f->bindings;
        // NOTE: setup_second_finger_bindings maps 2nd-variant bindings into
        // PRIMARY fields (single_tap ← S2, double_tap ← D2, etc.) and NULLs
        // the 2nd-variant fields. So S2/D2 slots resolve from PRIMARY fields.
        switch (slot->bindings_non_drag) {
            case GESTURE_SINGLE_TAP:    *non_drag = fb->single_tap;       *non_drag_count = fb->single_tap_count;       break;
            case GESTURE_DOUBLE_TAP:    *non_drag = fb->double_tap;       *non_drag_count = fb->double_tap_count;       break;
            case GESTURE_LONG_PRESS:    *non_drag = fb->long_press;       *non_drag_count = fb->long_press_count;       break;
            case GESTURE_SINGLE_2ND:    *non_drag = fb->single_tap;       *non_drag_count = fb->single_tap_count;       break;
            case GESTURE_DOUBLE_2ND:    *non_drag = fb->double_tap;       *non_drag_count = fb->double_tap_count;       break;
            default: *non_drag = NULL; *non_drag_count = 0; break;
        }
        switch (slot->bindings_drag) {
            case GESTURE_SINGLE_TAP_DRAG:  *drag = fb->single_tap_drag;   *drag_count = fb->single_tap_drag_count;   break;
            case GESTURE_DOUBLE_TAP_DRAG:  *drag = fb->double_tap_drag;   *drag_count = fb->double_tap_drag_count;   break;
            case GESTURE_LONG_PRESS_DRAG:  *drag = fb->long_press_drag;   *drag_count = fb->long_press_drag_count;   break;
            case GESTURE_SINGLE_DRAG_2ND:  *drag = fb->single_tap_drag;   *drag_count = fb->single_tap_drag_count;   break;
            case GESTURE_DOUBLE_DRAG_2ND:  *drag = fb->double_tap_drag;   *drag_count = fb->double_tap_drag_count;   break;
            default: *drag = NULL; *drag_count = 0; break;
        }
    }
}

// ============================================================
// Unified pair plan resolution
// ============================================================
GesturePairPlan resolve_gesture_pair(
    const TouchFinger* f,
    const GesturePairSlot* slot)
{
    bool cd = slot->has_comp_dt
        && (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag);
    bool cl = slot->has_comp_lp
        && (f->cached_has_long_press_timer
         || f->cached_has_active_long_press
         || f->cached_has_active_long_press_drag);

    bool hx =
        slot->bindings_non_drag == GESTURE_SINGLE_TAP  ? f->cached_has_active_single_tap :
        slot->bindings_non_drag == GESTURE_DOUBLE_TAP  ? f->cached_has_active_double_tap :
        slot->bindings_non_drag == GESTURE_LONG_PRESS  ? f->cached_has_active_long_press :
        slot->bindings_non_drag == GESTURE_SINGLE_2ND  ? f->cached_has_active_single_tap :
        slot->bindings_non_drag == GESTURE_DOUBLE_2ND  ? f->cached_has_active_double_tap : false;

    bool hxd =
        slot->bindings_drag == GESTURE_SINGLE_TAP_DRAG ? f->cached_has_active_single_tap_drag :
        slot->bindings_drag == GESTURE_DOUBLE_TAP_DRAG ? f->cached_has_active_double_tap_drag :
        slot->bindings_drag == GESTURE_LONG_PRESS_DRAG ? f->cached_has_active_long_press_drag :
        slot->bindings_drag == GESTURE_SINGLE_DRAG_2ND ? f->cached_has_active_single_tap_drag :
        slot->bindings_drag == GESTURE_DOUBLE_DRAG_2ND ? f->cached_has_active_double_tap_drag : false;

    // S slot gets hold_delay from config; others use 0
    int hd = (slot == &GESTURE_SLOTS[0]) ? g_state.cfg.single_tap_delay_ms : 0;

    return gesture_decide_branch(gesture_branch_params(hx, hxd, cd, cl,
        g_state.cfg.is_ts, slot->is_second_finger, hd));
}

// ============================================================
// G_ctx lifecycle
// ============================================================
void g_ctx_init(GestureFingerCtx* ctx, bool is_second) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->is_second_finger = is_second;
}

// ============================================================
// ---- Unified event handlers (static, per-event-type) ----
// ============================================================

// ============================================================
// gesture_process_finger — THE unified entry point
// ============================================================
void gesture_process_finger(
    TouchFinger* f, GestureFingerCtx* ctx,
    TouchActionResult* result, uint64_t time_ms,
    GestureProcessEvent event,
    float x, float y)
{
    // Delegate to gesture_branch.
    // For MOVE: x,y are the caller-provided delta (dx, dy)
    // For DOWN/UP: x,y are finger position (unused)
    // For TICK: x,y are 0 (unused)
    gesture_branch(f, ctx, result, time_ms, event, x, y);
}
