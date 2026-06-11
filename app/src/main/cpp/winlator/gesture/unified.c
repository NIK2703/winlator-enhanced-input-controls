#include "../touch_processor_internal.h"
#include "types.h"
#include "branch.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Winlator_Gesture", __VA_ARGS__)

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
    { GESTURE_SINGLE_TAP, GESTURE_SINGLE_TAP_DRAG,  0,  0,
      GESTURE_DOUBLE_TAP, GESTURE_LONG_PRESS, false },
    // Slot 1: D/Dd  (main finger double tap)
    { GESTURE_DOUBLE_TAP, GESTURE_DOUBLE_TAP_DRAG,  0,  0,
      0, 0, false },
    // Slot 2: L/Ld  (main finger long press)
    { GESTURE_LONG_PRESS, GESTURE_LONG_PRESS_DRAG,  0,  0,
      GESTURE_DOUBLE_TAP, 0, false },
    // Slot 3: S2/Sd2 (second finger single tap)
    { GESTURE_SINGLE_2ND, GESTURE_SINGLE_DRAG_2ND,
      GESTURE_SINGLE_2ND, GESTURE_SINGLE_DRAG_2ND,
      GESTURE_DOUBLE_2ND, 0, true },
    // Slot 4: D2/Dd2 (second finger double tap)
    { GESTURE_DOUBLE_2ND, GESTURE_DOUBLE_DRAG_2ND,
      GESTURE_DOUBLE_2ND, GESTURE_DOUBLE_DRAG_2ND,
      0, 0, true },
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
    bool cd = slot->comp_dt != 0
        && (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag);
    bool cl = slot->comp_lp != 0
        && f->cached_has_long_press_timer;

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
// DT slot selection — returns D/Dd for main, D2/Dd2 for second
// ============================================================
const GesturePairSlot* select_dt_slot(const TouchFinger* f) {
    return f->is_second_finger ? SLOT_D2() : SLOT_D();
}

// ============================================================
// S/Sd or S2/Sd2 slot selection
// ============================================================
const GesturePairSlot* select_s_slot(const TouchFinger* f) {
    return f->is_second_finger ? SLOT_S2() : SLOT_S();
}

// ============================================================
// MOVE slot selection — handles all 5 gesture types
// ============================================================
static inline bool has_actual_dt_state(const GestureFingerCtx* ctx) {
    return ctx->deferred_double_count > 0
        || ctx->post_double_tap_drag;
}

const GesturePairSlot* select_slot_for_move(
    const TouchFinger* f, const GestureFingerCtx* ctx)
{
    if (f->state == GESTURE_STATE_LONG_PRESSING)
        return f->is_second_finger ? NULL : SLOT_L();
    if (f->is_second_finger)
        return has_actual_dt_state(ctx) ? SLOT_D2() : SLOT_S2();
    if (ctx->post_double_tap_drag || ctx->deferred_double_count > 0)
        return SLOT_D();
    return SLOT_S();
}

// ============================================================
// UP slot selection
// ============================================================
const GesturePairSlot* select_slot_for_up(
    const TouchFinger* f, const GestureFingerCtx* ctx)
{
    if (f->state == GESTURE_STATE_LONG_PRESSING)
        return SLOT_L();
    if (f->is_second_finger)
        return has_actual_dt_state(ctx) ? SLOT_D2() : SLOT_S2();
    return SLOT_S();
}

// ============================================================
// Second-finger cleanup — consolidates duplicate code from UP
// ============================================================
void cleanup_second_finger(TouchFinger* f, GestureFingerCtx* ctx,
                           TouchActionResult* restrict result) {
    // Execute pending D2 from SDTW confirm before releasing state
    if (g_state.pending_second_double_count > 0
        && !g_state.gesture_is_action_held) {
        LOGD("CLEANUP ptr=%d PENDING_D2 cnt=%d",
            f->ptr_id, g_state.pending_second_double_count);
        execute_actions(result, g_state.pending_second_double,
            g_state.pending_second_double_count);
    }
    g_state.pending_second_double_count = 0;

    // SDTW was just entered — clear gesture_second_active so the NEXT second
    // finger DOWN can be recognized (otherwise it hits branch.c:46 as "3rd+ finger").
    // Preserve second_double_tap_waiting for the SDTW confirm check.
    if (ctx->dt_waiting) {
        LOGD("CLEANUP ptr=%d SDTW_JUST_ENTERED preserve dt_waiting", f->ptr_id);
        release_held_actions(result);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
        g_state.gesture_second_active = false;
        f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
        return;
    }

    g_state.second_double_tap_waiting = false;
    if (!(ctx->post_double_tap_drag && g_state.gesture_is_action_held)) {
        release_held_actions(result);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 0, 0, 0);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, 2, 0, 0);
    }
    gesture_clear_second_finger_globals();
    f->state = GESTURE_STATE_IDLE;
    deactivate_finger(f);
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
