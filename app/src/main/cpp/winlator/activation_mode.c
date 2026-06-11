#include "touch_processor_internal.h"
#include "activation_mode.h"

#define PTR_SLOT(ptr_id) ((uint32_t)(ptr_id) % MAX_FINGERS)

// ─── Mode constants ──────────────────────────────────────────────────────────

const ActivationModeParams ACTIVATION_MODE_LOCK = {
    .down_button = BTN_DOWN_ALL,
    .down_set_hovered = false,
    .down_skip_off_toggle = false,
    .move_nonbutton = NONBTN_MOVE_LOCK,
    .move_button = BTN_MOVE_LOCK,
    .move_gesture_move = true,
    .up_release = UP_RELEASE_ENGAGED,
    .up_clear_hovered = false,
    .up_reset_tracked = false,
    .skip_reentry_down = false,
};

const ActivationModeParams ACTIVATION_MODE_TRACK = {
    .down_button = BTN_DOWN_FIRST,
    .down_set_hovered = false,
    .down_skip_off_toggle = true,
    .move_nonbutton = NONBTN_MOVE_ACCUM,
    .move_button = BTN_MOVE_ACCUM,
    .move_gesture_move = false,
    .up_release = UP_RELEASE_TRACKED,
    .up_clear_hovered = false,
    .up_reset_tracked = false,
    .skip_reentry_down = false,
};

const ActivationModeParams ACTIVATION_MODE_HOVER = {
    .down_button = BTN_DOWN_FIRST,
    .down_set_hovered = true,
    .down_skip_off_toggle = true,
    .move_nonbutton = NONBTN_MOVE_HOVER,
    .move_button = BTN_MOVE_TRANS,
    .move_gesture_move = true,
    .up_release = UP_RELEASE_ENGAGED,
    .up_clear_hovered = true,
    .up_reset_tracked = true,
    .skip_reentry_down = false,
};

// ─── Static helpers (moved from touch_processor_activation.c) ─────────────────

static inline void toggle_slide_over(TouchElement* btn, TouchActionResult* restrict result, uint64_t time_ms) {
    if (btn->selected) {
        if (element_has_gesture_toggle(btn)) {
            btn->visual_active = true;
            TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=1 (blocked_other_toggle)", (int)(btn - g_state.elements));
            mark_element_dirty(btn);
            return;
        }
        release_toggled_alternate_bindings(btn, result, true);
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++) {
            TouchBinding* tb = &btn->bindings[k];
            if (tb->type == BINDING_NONE) continue;
            release_binding(result, tb);
            if (tb->toggle)
                gesture_remove_toggled_action(tb->type, tb->keycode);
            else if (tb->auto_repeat)
                gesture_remove_held_action(tb->type, tb->keycode);
        }
        if (g_state.gesture_held_count == 0)
            g_state.gesture_is_action_held = false;
        btn->selected = false;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=0 (deselect)", (int)(btn - g_state.elements));
        btn->visual_active = false;
    } else {
        for (int k = 0; k < MAX_BINDINGS_PER_ELEMENT; k++) {
            TouchBinding* tb = &btn->bindings[k];
            if (tb->type == BINDING_NONE) continue;
            if (tb->toggle || tb->auto_repeat)
                press_binding(result, tb, true);
            else
                press_binding(result, tb, false);
        }
        btn->selected = true;
        btn->auto_repeat_last_time = time_ms;
        TP_LOG(ANDROID_LOG_DEBUG, "Winlator_Vis", "toggle_slide_over[%d] visual=1 (select)", (int)(btn - g_state.elements));
        btn->visual_active = true;
    }
    mark_element_dirty(btn);
}

static inline void process_engaged_non_buttons(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result, bool hover_release) {
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(f == NULL, 0)) return;
    uint8_t cnt = f->engaged_elem_count;
    uint8_t i = 0;
    while (i < cnt) {
        TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
        if (e->type != ELEM_BUTTON && e->current_ptr_id == ptr_id) {
            if (hover_release && !point_in_element(x, y, e)) {
                handle_element_up(e, x, y, time_ms, result);
                finger_remove_engaged(f, f->engaged_elem_indices[i]);
                cnt = f->engaged_elem_count;
                continue;
            }
            handle_element_move(e, x, y, time_ms, result);
        }
        i++;
    }
}

static inline bool release_engaged_elements(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* restrict result) {
    bool handled = false;
    TouchFinger* f = find_finger(ptr_id);
    if (__builtin_expect(f == NULL, 0)) return false;
    uint8_t cnt = f->engaged_elem_count;
    for (uint8_t i = 0; i < cnt; i++) {
        TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
        if (e->current_ptr_id == ptr_id) {
            handle_element_up(e, x, y, time_ms, result);
            handled = true;
        }
    }
    f->engaged_elem_count = 0;
    return handled;
}

typedef bool (*GridScanCallback)(void* ctx, TouchElement* e);

static inline void process_hit_element(TouchElement* e, int ptr_id, float x, float y,
                                       uint64_t time_ms, TouchActionResult* restrict result,
                                       bool* out_handled, TouchElement** out_btn) {
    if (e->type != ELEM_BUTTON) {
        handle_element_down(e, ptr_id, x, y, time_ms, result);
        *out_handled = true;
    } else if (!*out_btn) {
        *out_btn = e;
    }
}

static bool scan_grid_elements(float x, float y, GridScanCallback cb, void* ctx) {
    if (g_state.grid_cell_w <= 0.0f || g_state.grid_cell_h <= 0.0f) return false;
    if (x < g_state.grid_min_x || x > g_state.grid_max_x ||
        y < g_state.grid_min_y || y > g_state.grid_max_y) return false;

    int col = (int)((x - g_state.grid_min_x) * g_state.grid_inv_cell_w);
    int row = (int)((y - g_state.grid_min_y) * g_state.grid_inv_cell_h);
    if (col < 0) col = 0; if (col >= GRID_COLS) col = GRID_COLS - 1;
    if (row < 0) row = 0; if (row >= GRID_ROWS) row = GRID_ROWS - 1;

    int min_c = col > 0 ? col - 1 : 0;
    int max_c = col < GRID_COLS - 1 ? col + 1 : GRID_COLS - 1;
    int min_r = row > 0 ? row - 1 : 0;
    int max_r = row < GRID_ROWS - 1 ? row + 1 : GRID_ROWS - 1;

    for (int r = min_r; r <= max_r; r++) {
        for (int c = min_c; c <= max_c; c++) {
            int cell = r * GRID_COLS + c;
            for (int j = g_state.grid_cell_start[cell]; j < g_state.grid_cell_start[cell + 1]; j++) {
                TouchElement* e = &g_state.elements[g_state.grid_cell_to_elems[j]];
                if (!point_in_element(x, y, e)) continue;
                if (cb(ctx, e)) return true;
            }
        }
    }
    return true;
}

typedef struct {
    float x, y;
    int ptr_id;
    uint64_t time_ms;
    TouchActionResult* restrict result;
    bool handled;
} EngageAllCtx;

static bool engage_all_cb(void* ctx, TouchElement* e) {
    EngageAllCtx* c = (EngageAllCtx*)ctx;
    handle_element_down(e, c->ptr_id, c->x, c->y, c->time_ms, c->result);
    c->handled = true;
    return false;
}

static bool grid_engage_all(int ptr_id, float x, float y, uint64_t time_ms, TouchActionResult* result) {
    EngageAllCtx ctx = { .x = x, .y = y, .ptr_id = ptr_id, .time_ms = time_ms, .result = result, .handled = false };
    if (scan_grid_elements(x, y, engage_all_cb, &ctx))
        return ctx.handled;
    for (int i = 0; i < g_state.element_count; i++) {
        if (point_in_element(x, y, &g_state.elements[i])) {
            handle_element_down(&g_state.elements[i], ptr_id, x, y, time_ms, result);
            ctx.handled = true;
        }
    }
    return ctx.handled;
}

typedef struct {
    float x, y;
    int ptr_id;
    uint64_t time_ms;
    TouchActionResult* restrict result;
    bool handled;
    TouchElement* btn;
    int hit_count;
} ScanButtonCtx;

static bool scan_button_cb(void* ctx, TouchElement* e) {
    ScanButtonCtx* c = (ScanButtonCtx*)ctx;
    c->hit_count++;
    process_hit_element(e, c->ptr_id, c->x, c->y, c->time_ms, c->result, &c->handled, &c->btn);
    return false;
}

static void scan_all_elements_fallback(float x, float y, int ptr_id, uint64_t time_ms,
                                        TouchActionResult* restrict result,
                                        bool* out_handled, TouchElement** out_btn,
                                        int* hit_count) {
    for (int i = 0; i < g_state.element_count; i++) {
        TouchElement* e = &g_state.elements[i];
        if (!point_in_element(x, y, e)) continue;
        if (hit_count) (*hit_count)++;
        process_hit_element(e, ptr_id, x, y, time_ms, result, out_handled, out_btn);
    }
}

static bool scan_first_button(int ptr_id, float x, float y, uint64_t time_ms,
                              TouchActionResult* result, bool skip_off_toggle, bool set_hovered)
{
    ScanButtonCtx ctx = {
        .x = x, .y = y, .ptr_id = ptr_id, .time_ms = time_ms,
        .result = result, .handled = false, .btn = NULL, .hit_count = 0
    };

    if (scan_grid_elements(x, y, scan_button_cb, &ctx)) {
        if (ctx.hit_count == 0)
            scan_all_elements_fallback(x, y, ptr_id, time_ms, result, &ctx.handled, &ctx.btn, &ctx.hit_count);
    } else {
        scan_all_elements_fallback(x, y, ptr_id, time_ms, result, &ctx.handled, &ctx.btn, NULL);
    }

    if (ctx.btn && ctx.btn->type == ELEM_BUTTON) {
        int btn_idx = (int)(ctx.btn - g_state.elements);
        bool skip = skip_off_toggle && ctx.btn->cached_has_toggle
                    && !ctx.btn->selected && !ctx.btn->lp_toggled && !ctx.btn->gesture_toggled;

        if (!skip) {
            TrackedButtons* tb = get_tracked_buttons(ptr_id);
            if (!is_tracked(tb, btn_idx) && tb->count < MAX_TRACKED_PER_POINTER) {
                tb->element_indices[tb->count++] = btn_idx;
                handle_element_down(ctx.btn, ptr_id, x, y, time_ms, result);
                if (ctx.btn->cached_has_toggle && ctx.btn->cached_has_auto_repeat)
                    ctx.btn->gesture_timer_armed = true;
            }
            if (set_hovered)
                g_state.hovered_element_per_ptr[PTR_SLOT(ptr_id)] = btn_idx;
        }
        ctx.handled = true;
    }

    return ctx.handled;
}

bool activation_mode_down(int ptr_id, float x, float y, uint64_t time_ms,
                          TouchActionResult* result, const ActivationModeParams* params)
{
    if (__builtin_expect(g_state.element_count == 0, 0)) return false;

    if (params->down_button == BTN_DOWN_ALL)
        return grid_engage_all(ptr_id, x, y, time_ms, result);

    return scan_first_button(ptr_id, x, y, time_ms, result,
                             params->down_skip_off_toggle, params->down_set_hovered);
}

// ─── MOVE (non-buttons): process engaged + optionally activate new ───────────

void activation_mode_move_nonbuttons(int ptr_id, float x, float y, uint64_t time_ms,
                                     TouchActionResult* result, const ActivationModeParams* params)
{
    if (params->move_nonbutton == NONBTN_MOVE_LOCK) {
        // LOCK: move ALL engaged elements (including buttons) — matches original
        TouchFinger* f = find_finger(ptr_id);
        if (__builtin_expect(f != NULL, 1)) {
            uint8_t cnt = f->engaged_elem_count;
            for (uint8_t i = 0; i < cnt; i++) {
                TouchElement* e = &g_state.elements[f->engaged_elem_indices[i]];
                if (e->current_ptr_id == ptr_id)
                    handle_element_move(e, x, y, time_ms, result);
            }
        }
    } else {
        bool hover_release = (params->move_nonbutton == NONBTN_MOVE_HOVER);
        process_engaged_non_buttons(ptr_id, x, y, time_ms, result, hover_release);

        TouchElement* hit = hit_test_element(x, y);
        if (hit && hit->type != ELEM_BUTTON && hit->current_ptr_id < 0)
            handle_element_down(hit, ptr_id, x, y, time_ms, result);
    }
}

// ─── MOVE (buttons): tracking / accumulation / transition ───────────────────

static bool handle_btn_move_transition(int ptr_id, float x, float y, TouchElement* btn,
                                       int curr_idx, TrackedButtons* tb, uint64_t time_ms,
                                       TouchActionResult* restrict result,
                                       const ActivationModeParams* params) {
    int pid_slot = PTR_SLOT(ptr_id);
    int prev_idx = g_state.hovered_element_per_ptr[pid_slot];

    if (curr_idx >= 0 && curr_idx == prev_idx)
        return true;

    if (prev_idx >= 0 && prev_idx < g_state.element_count) {
        TouchElement* prev = &g_state.elements[prev_idx];
        release_element_bindings(prev, result, false);
        release_non_toggle_gestures(prev, result);
        clear_element_gesture_flags(prev, false);
        prev->current_ptr_id = -1;
        prev->engaged = false;
        TouchFinger* f = find_finger(ptr_id);
        if (f) finger_remove_engaged(f, prev_idx);
    }

    if (curr_idx >= 0 && btn->cached_has_toggle && !btn->gesture_timer_armed) {
        toggle_slide_over(btn, result, time_ms);
        btn->gesture_timer_armed = true;
    } else if (curr_idx >= 0) {
        bool already_tracked = is_tracked(tb, curr_idx);
        if (!already_tracked && tb->count < MAX_TRACKED_PER_POINTER) {
            tb->element_indices[tb->count++] = curr_idx;
            reset_first_tracked_long_press(tb);
        }
        bool reentry_first = (already_tracked && tb->count > 0
            && curr_idx == tb->element_indices[0]);
        if (!(params->skip_reentry_down && reentry_first))
            handle_element_down(btn, ptr_id, x, y, time_ms, result);
        if (tb->count > 1 || already_tracked)
            suppress_element_gestures(btn, result);
    }

    g_state.hovered_element_per_ptr[pid_slot] = curr_idx >= 0 ? curr_idx : -1;
    return false;
}

// Returns: current hovered element index, or -1 if none.
// The caller can use this for gesture state save/restore around the call.
int activation_mode_move_buttons(int ptr_id, float x, float y, uint64_t time_ms,
                                 TouchActionResult* result, const ActivationModeParams* params)
{
    TrackedButtons* tb = get_tracked_buttons(ptr_id);
    if (tb->count == 0) return -1;

    TouchElement* btn = hit_test_element(x, y);
    int curr_idx = btn && btn->type == ELEM_BUTTON ? (int)(btn - g_state.elements) : -1;

    switch (params->move_button) {
    case BTN_MOVE_LOCK:
        break;

    case BTN_MOVE_ACCUM: {
        if (curr_idx < 0) break;
        // Original handler TRACK had no toggle activation on move (empty block).
        // Original activation.c TRACK had toggle_slide_over but it was buggy
        // (deselected finger-down toggles on first move). Match handler behavior.
        if (!btn->cached_has_toggle) {
            if (!is_tracked(tb, curr_idx) && tb->count < MAX_TRACKED_PER_POINTER) {
                tb->element_indices[tb->count++] = curr_idx;
                handle_element_down(btn, ptr_id, x, y, time_ms, result);
                if (tb->count > 1)
                    suppress_element_gestures(btn, result);
                reset_first_tracked_long_press(tb);
            }
        }
        break;
    }

    case BTN_MOVE_TRANS:
        if (handle_btn_move_transition(ptr_id, x, y, btn, curr_idx, tb, time_ms, result, params))
            return curr_idx;
        break;
    }

    return curr_idx;
}

// ─── UP: release elements per mode ──────────────────────────────────────────

bool activation_mode_up(int ptr_id, float x, float y, uint64_t time_ms,
                        TouchActionResult* result, const ActivationModeParams* params)
{
    if (__builtin_expect(g_state.element_count == 0, 0)) return false;

    int pid_slot = PTR_SLOT(ptr_id);
    bool handled = false;

    if (params->up_release == UP_RELEASE_TRACKED) {
        TrackedButtons* tb = &g_state.tracked[pid_slot];
        if (tb->ptr_id != ptr_id) goto done;
        for (int j = 0; j < tb->count; j++) {
            int idx = tb->element_indices[j];
            if (idx < 0 || idx >= g_state.element_count) continue;
            handle_element_up(&g_state.elements[idx], x, y, time_ms, result);
            if (!element_is_toggle_active(&g_state.elements[idx]))
                g_state.elements[idx].visual_active = false;
        }
        reset_tracked_slot(tb, pid_slot);
        handled = true;
    } else {
        if (params->up_reset_tracked)
            reset_tracked_slot(&g_state.tracked[pid_slot], pid_slot);
        handled = release_engaged_elements(ptr_id, x, y, time_ms, result);
    }

done:
    reset_unused_toggle_gesture_timers();

    if (params->up_clear_hovered)
        g_state.hovered_element_per_ptr[pid_slot] = -1;

    return handled;
}
