#include "../touch_processor_internal.h"
#include "types.h"

#define LOGD(...) do {} while(0)

// Binding group indices — names match FingerBindings fields they resolve to
#define GROUP_UNMAPPED          0
#define GROUP_SINGLE_TAP        1  // → fb->single_tap
#define GROUP_DOUBLE_TAP        2  // → fb->double_tap
#define GROUP_LONG_PRESS        3  // → fb->long_press
#define GROUP_SINGLE_TAP_DRAG   4  // → fb->single_tap_drag
#define GROUP_DOUBLE_TAP_DRAG   5  // → fb->double_tap_drag
#define GROUP_LONG_PRESS_DRAG   6  // → fb->long_press_drag
#define GROUP_COUNT             7

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
        return SLOT_L();
    }
    if (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag) {
        return f->is_second_finger ? SLOT_D2() : SLOT_D();
    }
    return f->is_second_finger ? SLOT_S2() : SLOT_S();
}

// ============================================================
// Unified binding resolution — switch-case dispatch
// ============================================================
static const TouchBinding* group_get(int g, const FingerBindings* fb) {
    switch (g) {
    case GROUP_SINGLE_TAP:      return fb->single_tap;
    case GROUP_DOUBLE_TAP:      return fb->double_tap;
    case GROUP_LONG_PRESS:      return fb->long_press;
    case GROUP_SINGLE_TAP_DRAG: return fb->single_tap_drag;
    case GROUP_DOUBLE_TAP_DRAG: return fb->double_tap_drag;
    case GROUP_LONG_PRESS_DRAG: return fb->long_press_drag;
    default: return NULL;
    }
}

static int group_get_count(int g, const FingerBindings* fb) {
    switch (g) {
    case GROUP_SINGLE_TAP:      return fb->single_tap_count;
    case GROUP_DOUBLE_TAP:      return fb->double_tap_count;
    case GROUP_LONG_PRESS:      return fb->long_press_count;
    case GROUP_SINGLE_TAP_DRAG: return fb->single_tap_drag_count;
    case GROUP_DOUBLE_TAP_DRAG: return fb->double_tap_drag_count;
    case GROUP_LONG_PRESS_DRAG: return fb->long_press_drag_count;
    default: return 0;
    }
}

static bool group_get_cached(int g, const TouchFinger* f) {
    switch (g) {
    case GROUP_SINGLE_TAP:      return f->cached_has_active_single_tap;
    case GROUP_DOUBLE_TAP:      return f->cached_has_active_double_tap;
    case GROUP_LONG_PRESS:      return f->cached_has_active_long_press;
    case GROUP_SINGLE_TAP_DRAG: return f->cached_has_active_single_tap_drag;
    case GROUP_DOUBLE_TAP_DRAG: return f->cached_has_active_double_tap_drag;
    case GROUP_LONG_PRESS_DRAG: return f->cached_has_active_long_press_drag;
    default: return false;
    }
}

// Canonical mapping: GestureType → group
static const int g_type_to_group[GESTURE_TYPE_COUNT] = {
    [GESTURE_SINGLE_TAP]        = GROUP_SINGLE_TAP,
    [GESTURE_LONG_PRESS]        = GROUP_LONG_PRESS,
    [GESTURE_DOUBLE_TAP]        = GROUP_DOUBLE_TAP,
    [GESTURE_SINGLE_TAP_DRAG]   = GROUP_SINGLE_TAP_DRAG,
    [GESTURE_LONG_PRESS_DRAG]   = GROUP_LONG_PRESS_DRAG,
    [GESTURE_DOUBLE_TAP_DRAG]   = GROUP_DOUBLE_TAP_DRAG,
    [GESTURE_SINGLE_2ND]        = GROUP_SINGLE_TAP,
    [GESTURE_DOUBLE_2ND]        = GROUP_DOUBLE_TAP,
    [GESTURE_SINGLE_DRAG_2ND]   = GROUP_SINGLE_TAP_DRAG,
    [GESTURE_DOUBLE_DRAG_2ND]   = GROUP_DOUBLE_TAP_DRAG,
};

static void gesture_type_resolve_binding(GestureType type, const FingerBindings* fb,
                                          const TouchBinding** out, int* out_count) {
    if (type < 0 || type >= GESTURE_TYPE_COUNT) { *out = NULL; *out_count = 0; return; }
    int g = g_type_to_group[type];
    if (g == GROUP_UNMAPPED) { *out = NULL; *out_count = 0; return; }
    *out = group_get(g, fb);
    *out_count = group_get_count(g, fb);
}

static bool gesture_type_resolve_cached(GestureType type, const TouchFinger* f) {
    if (type < 0 || type >= GESTURE_TYPE_COUNT) return false;
    int g = g_type_to_group[type];
    if (g == GROUP_UNMAPPED) return false;
    return group_get_cached(g, f);
}

static bool gesture_type_has_binding(GestureType type, const FingerBindings* fb,
                                     const TouchFinger* f) {
    return gesture_type_resolve_cached(type, f);
}

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
        gesture_type_resolve_binding(slot->bindings_non_drag, fb, non_drag, non_drag_count);
        gesture_type_resolve_binding(slot->bindings_drag, fb, drag, drag_count);
    }
}

// ============================================================
// Unified pair plan resolution
// ============================================================
GesturePairPlan resolve_gesture_pair(
    const TouchFinger* f,
    const GesturePairSlot* slot)
{
    bool has_competing_dt = slot->comp_dt != 0
        && (f->cached_has_active_double_tap || f->cached_has_active_double_tap_drag);
    bool has_competing_lp = slot->comp_lp != 0
        && f->cached_has_long_press_timer;

    bool has_non_drag = gesture_type_has_binding(slot->bindings_non_drag, &f->bindings, f);
    bool has_drag = gesture_type_has_binding(slot->bindings_drag, &f->bindings, f);

    // S slot gets hold_delay from config; others use 0
    int hold_delay_ms = (slot == SLOT_S()) ? g_state.cfg.single_tap_delay_ms : 0;

    return gesture_decide_branch(gesture_branch_params(has_non_drag, has_drag, has_competing_dt, has_competing_lp,
        g_state.cfg.is_ts, slot->is_second_finger, hold_delay_ms));
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
// MOVE/UP slot selection — unified for both event types
// ============================================================
static inline bool has_actual_dt_state(const GestureFingerCtx* ctx) {
    return ctx->deferred_double.count > 0
        || ctx->post_double_tap_drag;
}

const GesturePairSlot* select_slot_common(
    const TouchFinger* f, const GestureFingerCtx* ctx, bool is_up_event)
{
    if (f->state == GESTURE_STATE_LONG_PRESSING)
        return f->is_second_finger ? NULL : SLOT_L();
    if (f->is_second_finger)
        return has_actual_dt_state(ctx) ? SLOT_D2() : SLOT_S2();
    if (!is_up_event && has_actual_dt_state(ctx))
        return SLOT_D();
    return SLOT_S();
}

// ============================================================
// Second-finger cleanup — consolidates duplicate code from UP
// ============================================================
static void execute_pending_second_dt(TouchFinger* f, TouchActionResult* restrict result) {
    if (g_state.pending_second_double_count > 0
        && !g_state.gesture_is_action_held) {
        execute_actions(result, g_state.pending_second_double,
            g_state.pending_second_double_count);
    }
    g_state.pending_second_double_count = 0;
}

static void release_second_finger_buttons(TouchActionResult* restrict result,
                                           bool skip_release) {
    if (!skip_release) {
        release_held_actions(result);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, MOUSE_BTN_LEFT, 0, 0);
        add_action(result, ACT_POINTER_BUTTON_RELEASE, MOUSE_BTN_MIDDLE, 0, 0);
    }
}

static void deactivate_second_finger(TouchFinger* f) {
    g_state.second_double_tap_waiting = false;
    gesture_clear_second_finger_globals();
    f->state = GESTURE_STATE_IDLE;
    deactivate_finger(f);
}

void cleanup_second_finger(TouchFinger* f, GestureFingerCtx* ctx,
                           TouchActionResult* restrict result) {
    execute_pending_second_dt(f, result);

    // SDTW was just entered — clear gesture_second_active so the NEXT second
    // finger DOWN can be recognized (otherwise it hits branch.c:46 as "3rd+ finger").
    // Preserve second_double_tap_waiting for the SDTW confirm check.
    if (ctx->dt_waiting) {
        release_second_finger_buttons(result, false);
        g_state.gesture_second_active = false;
        f->state = GESTURE_STATE_DOUBLE_TAP_WAITING;
        return;
    }

    bool skip_release = ctx->post_double_tap_drag && g_state.gesture_is_action_held;
    release_second_finger_buttons(result, skip_release);
    deactivate_second_finger(f);
}

// ============================================================
// G_ctx lifecycle
// ============================================================
void g_ctx_init(GestureFingerCtx* ctx, bool is_second) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->is_second_finger = is_second;
}


