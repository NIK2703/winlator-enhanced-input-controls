# Toggle Switch & Autorepeat — Unified Specification

## 1. Executive Summary

This document unifies three concept analyses into a single specification for the Winlator-Ludashi C native touch processor:

- **Toggle Switch & Autorepeat** (original: toggle/AR data flow, element + gesture layers)
- **Button Multi-Action** (Tap / Long-Press / Gesture Swipe branching, `gesture_timer_armed` discovery, visual state guards)
- **Activation Modes Only Affect Buttons** (two-pass architecture, non-button LOCK semantics, dead `activation.c`)

Key architectural discoveries verified against codebase:
- `touch_processor_activation.c` is **100% dead code** — JNI exports exist but Java never calls them
- `gesture_timer_armed` is **toggle-only** — never set for gesture-only or LP-only elements
- `handler.c` is the **sole live path** for TRACK/HOVER button handling
- `element_button_move()` TRACK/HOVER toggle slide-over (~lines 192-233) is **dead code** — `handler.c` skips `handle_element_move` for engaged buttons
- The 50ms gesture timer block in `touch_processor.c` tick only fires for toggle elements
- **Phase 1 fixes applied**: LOCK double-fire, `gesture_toggled`/`lp_toggled` reset on new touch, 50ms debounce, toggle state save/restore across reset/set_elements

---

## 2. Architecture Overview

### 2.1 Two-Pass Architecture

The system has two parallel code paths for finger events:

**Pass A: Gesture Path (LIVE)** — `handler.c`
```
touch_processor_on_finger_down/move/up()
    → handle_gesture_down/move/up()
        → handle_element_down/move/up() (shared.c)
            → element_button_down/move/up() (button.c)
```

**Pass B: Activation Path (DEAD)** — `touch_processor_activation.c`
```
touch_processor_handle_down/move/up_by_mode()  // NEVER CALLED from Java
    → activation_handle_down/move/up()
        → handle_element_down/move/up() (shared.c)
```

### 2.2 Handler.c: The Sole Live Path

Every finger event flows through `handler.c`:
- `handle_gesture_down` (`handler.c:60-346`): grid-accelerated hit test, LOCK/TRACK/HOVER dispatch, gesture state setup
- `handle_gesture_move` (`handler.c:352-892`): engaged elements loop, toggle slide-over, TRACK/HOVER tracking
- `handle_gesture_up` (`handler.c:898-1036`): tracked cleanup, element cleanup, gesture up dispatch

### 2.3 Activation.c: Dead Code Status

**Finger-event handlers (`activation_handle_down/move/up`) — DEAD.**
Visual state / reset functions — LIVE.

| Evidence | Detail |
|----------|--------|
| JNI exports | `Java_..._touchProcessorHandleDown/Move/Up` exist in `touch_processor_jni.cpp` |
| Java calls | `NativeTouchProcessor.java` NEVER calls `handleDown`, `handleMove`, `handleUp` |
| All finger events | Go through `onTouchEvent()` → `onFingerDown/Move/Up()` → `touch_processor_on_finger_down/move/up()` → `handler.c` |
| Dead activation.c code | `activation_handle_down/move/up`, `toggle_slide_over`, `process_engaged_non_buttons` — unreachable |
| Live activation.c code | `activation_get_visual_states`, `activation_get_element_visual`, `activation_activate_at`, `activation_deactivate_all`, `activation_tracked_count`, `activation_hovered_for_ptr`, `activation_reset` — called from Java rendering/reset |

**Conclusion**: Do not modify the dead finger-event handlers in `activation.c` for behavioral changes.
All TRACK/HOVER/LOCK button logic is in `handler.c`.

---

## 3. Toggle Switch

### 3.1 Data Structures

**`TouchBinding.toggle`** — `touch_processor.h:42`:
```c
typedef struct {
    BindingType type;
    int keycode;
    int modifiers;
    bool toggle;   // 1 = toggle switch (press on first activation, release on second)
    bool auto_repeat;
    int auto_repeat_interval_ms;
} TouchBinding;
```

**`TouchElement.selected`** — `touch_processor.h:112` — persistent boolean that survives finger-up for toggle elements.

**`TouchElement.cached_has_toggle`** — `touch_processor.h:115` — computed in `touch_processor_set_elements` at `touch_processor.c:106`.

**`TouchElement.toggle_debounce_last_time`** — `touch_processor.h:130` — 50ms debounce timestamp, added in Phase 1.

**`element_has_toggle()`** — `touch_processor_internal.h:408-410`:
```c
static inline bool element_has_toggle(const TouchElement* e) {
    return e->bindings[0].toggle || e->bindings[1].toggle
        || e->bindings[2].toggle || e->bindings[3].toggle;
}
```

**`element_is_toggle_active()`** — `touch_processor_internal.h:164-166`:
```c
static inline bool element_is_toggle_active(const TouchElement* e) {
    return (e->cached_has_toggle && e->selected) || e->lp_toggled || e->gesture_toggled;
}
```

**`gesture_toggled_actions[16]`** — `touch_processor_internal.h:131` — gesture-layer toggle registry.

### 3.2 Element-Level Toggle Path (`button.c`)

**Toggle + Autorepeat burst** — `button.c:14-88`:
```
element_button_down():
  if (cached_has_toggle && cached_has_auto_repeat):
    debounce guard (50ms)
    if selected:
      → deselect: release stale gesture/lp toggles,
        release all 4 bindings, set selected=false
    else:
      → select: press all bindings (toggle→hold, non-toggle→tap),
        set selected=true, set auto_repeat_last_time=time_ms
    return
```

**Normal toggle** — `button.c:90-114`:
```
  if (cached_has_toggle && selected):
    if lp_toggled || gesture_toggled → guard: keep selected
    debounce guard (50ms)
    if ACTIVATION_HOVER:
      → release binding, selected=false
    if ACTIVATION_TRACK:
      → set auto_repeat_primary_pressed=true, return
    if ACTIVATION_LOCK:
      → selected=false, visual_active=false (FIXED: flipped on DOWN, not UP)
      → release binding
    return
```

**Element-level toggle up** — `button.c:310-390`:
```
element_button_up():
  if (cached_has_toggle && !cached_has_auto_repeat && has_primary):
    if TRACK:
      debounce guard (50ms)
      → flip selected on UP
      → if deselecting: release stale gesture/LP, release primary
    if HOVER:
      → no-op (toggle managed on DOWN/MOVE entry)
    if LOCK:
      → toggle already flipped on DOWN
      → if selected: release stale gesture/LP (first UP after DOWN1)
      → if !selected: no-op (second UP after DOWN2)
    return
```

### 3.3 Gesture-Level Toggle Path (`executor.c`)

`execute_actions_impl` at `executor.c:111-156`: gesture toggle fires only on `gesture_is_down_event`. The `gesture_toggled_actions` array acts as persistent registry — press adds, subsequent press removes (swap-with-last).

### 3.4 element_is_toggle_active() — Guard Function

Used in 6 locations:
- `handle_element_down` (`shared.c:185`) — preserve toggle on re-engagement
- `handle_element_up` (`shared.c:227`) — prevent visual deactivation
- `release_element_bindings` (`shared.c:305`) — preserve bindings during release
- `activation_deactivate_all` (`activation.c:67`) — dead code guard
- `element_reset_runtime` (`shared.c:393`) — preserve visual state
- `touch_processor_tick` (`touch_processor.c:483`) — stale engaged recovery guard

### 3.5 gesture_timer_armed — Toggle-Only Debounce Guard

**Key discovery**: `gesture_timer_armed` is set to `true` **only** for elements with `cached_has_toggle == true`. It is:

| Purpose | Detail |
|---------|--------|
| What it is | Debounce guard preventing re-trigger of toggle slide-over in TRACK/HOVER |
| Where set (handler.c) | Lines 164, 452, 463, 483, 567, 574, 599, 601, 622 — ALL guarded by `cached_has_toggle` |
| Where set (button.c) | Lines 280, 350, 383, 431 — ALL in toggle paths |
| Where checked (tick) | `touch_processor.c:606` — 50ms gesture timer block, only enters if `gesture_timer_armed == true` |
| For non-toggle elements | **Never set**. Non-toggle gesture-only elements rely on **movement** in `element_button_move()` |

**Impact**: The 50ms gesture timer in `touch_processor.c:606-618` only fires for toggle elements. Gesture-only or LP-only elements never use this timer. Their gesture bindings fire only when movement exceeds `gesture_threshold_px` (default 20px) in `element_button_move()`.

---

## 4. Autorepeat

### 4.1 Data Structures

- `TouchBinding.auto_repeat`, `auto_repeat_interval_ms` — `touch_processor.h:43-44`
- `TouchElement.cached_has_auto_repeat` — `touch_processor.h:116`
- `TouchElement.auto_repeat_last_time` — `touch_processor.h:129`
- `TouchElement.cached_auto_repeat_interval` — `touch_processor.h:124`
- `TouchElement.auto_repeat_primary_pressed` — `touch_processor.h:140`

### 4.2 Element-Level (`button.c`)

**Non-toggle AR on down** — `button.c:116-129`: press primary binding, set `auto_repeat_primary_pressed`, set `auto_repeat_last_time`.

**Non-toggle AR on move** — `button.c:237-253`: release on leave, re-press on re-enter. Only for non-toggle elements.

**Non-toggle AR on up** — `button.c:395-400`: release primary if still held.

### 4.3 D-Pad (`dpad.c`)

`element_dpad_down/move/up` delegate to `dpad_update` → `element_set_petals`. No explicit AR timing — tick loop handles dpad autorepeat via `cached_has_auto_repeat`.

### 4.4 Gesture-Level (`executor.c`)

AR press in `execute_actions_impl` (`executor.c:183-203`): press binding, hold in `gesture_held_actions[]`.

`process_scheduled_actions` (`executor.c:319-364`): ring buffer of 32 scheduled actions, fires on timeout.

### 4.5 Tick Autorepeat Burst (`touch_processor.c`)

`process_auto_repeat_burst` (`touch_processor.c:433-447`): iterates `gesture_toggled_actions[]` and `gesture_held_actions[]`, fires AR bindings at interval.

Element-level burst in tick (`touch_processor.c:490-543`):
```
if (toggle+AR && selected):
  → release + press all AR bindings at interval
else if (non-toggle AR && engaged):
  → alternate press/release primary
```

---

## 5. Toggle + Autorepeat Combination

**Button Down — Burst Path** (`button.c:14-88`): press all 4 bindings on select, release on deselect. Guards against deselect when `lp_toggled || gesture_toggled`.

**Selected State Persistence** (`button.c:297-301`): `selected` persists after finger-up in toggle+AR mode.

**Tick Burst Loop** (`touch_processor.c:490-514`): burst release+press for toggle+AR selected elements.

---

## 6. Multi-Action: Tap / Long-Press / Gesture Swipe

### 6.1 Branching Decision Diagram

```
                    finger-down
                        │
                        ▼
              ┌─────────────────────┐
              │  element_button_down │
              │  defer_primary =     │
              │  (LP || gesture)     │
              │  && !toggle          │
              │  long_press_arm = LP │
              └──────────┬──────────┘
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
      ┌──────────────┐    ┌────────────────┐
      │ Has LP or    │    │ Plain button   │
      │ gesture?     │    │ (no LP/gesture)│
      └──────┬───────┘    └────────┬───────┘
             │                     │ press primary
             ▼                     ▼ on DOWN
    ┌────────────────┐    ┌────────────────┐
    │ ARM state      │    │ HOLD state     │
    │ primary DEFERRED│   │ primary held   │
    │ LP timer ticking│    └───────┬────────┘
    └───────┬────────┘            │
            │                     │
   ┌────────┴────────┐            │
   ▼                 ▼            │
┌──────┐       ┌────────┐        │
│MOVE >│       │ LP     │        │
│20px  │       │ timer  │        │
└──┬───┘       └──┬─────┘        │
   │              │              │
   ▼              ▼              │
┌────────┐  ┌──────────┐        │
│GESTURE │  │LP_ACTIVE │        │
│swipe   │  │long press│        │
│trigger │  │triggered │        │
│LP cancel│ │gesture   │        │
│        │  │blocked   │        │
└───┬────┘  └────┬─────┘        │
    │            │              │
    └─────┬──────┘              │
          │ finger-up           │ finger-up
          ▼                     ▼
    ┌──────────┐        ┌──────────┐
    │ release  │        │ release  │
    │ LP or    │        │ primary  │
    │ gesture  │        │          │
    │ +primary │        └──────────┘
    └──────────┘
    
    finger-up WITHOUT LP/gesture (quick tap):
    ┌──────────────────────────────────┐
    │ defer_primary? → press+release   │
    │ primary (tap)                    │
    │ !defer_primary? → release primary│
    └──────────────────────────────────┘
```

### 6.2 Scenario Analysis

#### Gesture-only button (no LP, no toggle)

| Scenario | Result |
|----------|--------|
| Quick tap (80ms, no movement) | Tap: primary press+release |
| Movement > 20px | Gesture: element_gesture + primary (both held until up) |
| Hold 5s without movement | **Nothing** — `gesture_timer_armed == false`, no timer fires |
| Hold 5s then move | Gesture fires at threshold crossing |
| **LOCK mode** | ✅ `element_button_move()` called → gesture works |
| **TRACK mode** | ❌ `element_button_move()` NOT called for first tracked button → gesture broken |
| **HOVER mode** | ✅ `element_button_move()` called for first non-toggle tracked button → gesture works |

#### Gesture + LP button (no toggle)

| Scenario | Winner | Mechanism |
|----------|--------|-----------|
| Fast movement before LP timer | Gesture | Movement > threshold, `long_press_arm = false`, LP cancelled |
| No movement, LP timer fires | LP | `gesture_long_press_triggered = true`, gesture blocked |
| Slow movement after LP timer | LP | Movement detected but `gesture_long_press_triggered` blocks gesture |
| Quick tap (80ms, no movement) | Tap | Neither LP nor gesture fire, `defer_primary` → tap |

#### LP-only button

| Scenario | Result |
|----------|--------|
| Quick tap | Tap: primary press+release |
| Hold > 200ms | LP: long_press + primary |
| Movement inside button | LP still fires (no outside check for LP) |
| Leave element before LP | `long_press_arm = false` → LP cancelled → tap |

### 6.3 Non-Toggle Gesture Detection

For non-toggle elements, gesture detection is **movement-only** (`element_button_move`, ~lines 155-185):
```c
if (dist_sq > gesture_threshold * gesture_threshold) {
    e->gesture_swipe_triggered = true;
    e->long_press_arm = false;
    // press gesture bindings
}
```

No time check — movement > threshold at any point triggers gesture. The 50ms timer block in the tick function is gated by `gesture_timer_armed`, which is **only set for toggle elements**.

---

## 7. Activation Modes

### 7.1 General Principle

> **Activation Mode (TRACK/HOVER/LOCK) modulates behavior of BUTTONS only.**
> **Non-button elements (DPAD, STICK, TRACKPAD, RANGE_BUTTON) always behave as LOCK:**
> - Activated on DOWN at touch point
> - Stay locked to element (no new non-button activation on move)
> - Receive move events even when finger leaves bounds
> - Released only on UP

### 7.2 Capability Matrix

| Feature | LOCK | TRACK | HOVER |
|---------|------|-------|-------|
| Tap | ✅ | ✅ | ✅ |
| Long-Press | ✅ | ✅ | ✅ |
| Gesture Swipe | ✅ | ❌ (first button) | ✅ (first non-toggle) |
| Slide-over toggle | N/A (no entry) | ✅ | ✅ |
| Multi-action (full) | ✅ BEST | ⚠️ (no gesture) | ✅ (except 2+ buttons) |

### 7.3 TRACK Mode Specifics

- `element_button_move()` is **NOT called** for the first tracked button (`handler.c:405` `skip_buttons=true`)
- Long-press works via `gesture_tick` (independent of move)
- Gesture-by-movement does **NOT work** for first TRACK button (architectural limit)
- Gesture-by-movement for first tracked button CAN be added via `handle_element_move` call (see TRACK gesture-by-movement routing in handler.c)

### 7.4 HOVER Mode Specifics

- `element_button_move()` IS called for first non-toggle tracked button (`handler.c:691-696`)
- Gesture bindings RELEASED on slide-off (non-toggle only, `handler.c:519-527`)
- Toggle bindings (non-toggle via `gesture_toggled`/`lp_toggled`) are PRESERVED
- Restore block neutralized (`handler.c:528-532` sets saved flags to false)

### 7.5 Gesture Zone Priority

Already correct — elements, once engaged by a finger, block gesture processing for that finger until finger-up:

| Down on | Move to | Gesture active? | Why |
|---------|---------|-----------------|-----|
| Gesture zone | Non-button | YES | Non-button not engaged on MOVE (gesture path) |
| Gesture zone | TRACK/HOVER button | YES (via tick) | Button tracked, move blocked, but gesture_tick continues |
| Element (any) | Empty space | NO | Element stays in engaged list, had_element_move=true |
| Element (any) | Another element | NO | First element still engaged |

---

## 8. Visual State Guards

### 8.1 All `visual_active = false` Assignments (26 total)

| # | File:line | Context | Guarded? |
|---|-----------|---------|----------|
| 1 | `button.c:115` | defer_primary + !has_primary | ✅ GAP 1 FIXED: added `!gesture_toggled && !lp_toggled` |
| 2 | `button.c:273` | NONE-binding guard in move | ✅ GAP 2 FIXED: added `!gesture_toggled && !lp_toggled` |
| 3 | `button.c:283` | HOVER outside in move | ✅ GAP 3 FIXED: added gesture/LP flags |
| 4 | `shared.c:213` | HOVER outside in handle_element_move | ✅ GAP 4 FIXED: added gesture/LP flags |
| 5 | `touch_processor.c:483` | Stale engaged recovery in tick | ✅ GAP 5 FIXED: added `element_is_toggle_active(e)` |
| 6-10 | `button.c` various | Toggle deselect, defer paths | ✅ All guarded |
| 11-14 | `shared.c` | HOVER outside, handle_element_up, release bindings, reset | ✅ All guarded |
| 15-18 | `touch_processor.c` | Init, cancel paths | ✅ GAP 5 fixed |
| 19-20 | `handler.c` | Toggle deselect, cleanup | ✅ Guarded |
| 21-24 | `activation.c` | Dead code | ✅ Guarded |
| 25-26 | `element_button_down/up` | LOCK immediate flip | ✅ All guarded |

### 8.2 Guard Pattern

For buttons without primary binding: `visual_active` must stay `true` when any of these are active:
- `gesture_swipe_triggered` (per-touch)
- `gesture_long_press_triggered` (per-touch)
- `gesture_toggled` (persistent)
- `lp_toggled` (persistent)

Standard guard:
```c
if (!has_primary && !e->gesture_swipe_triggered && !e->gesture_long_press_triggered
    && !e->gesture_toggled && !e->lp_toggled) {
    e->visual_active = false;
}
```

---

## 9. Release Semantics

### 9.1 Release Matrix

| Binding type | Activation Mode | Slide-off release | Finger-up release | Mechanism |
|---|---|---|---|---|
| Primary | LOCK/TRACK | NO | YES | `element_button_up` |
| Primary | HOVER | YES | YES | `release_element_bindings(result, false)` |
| Gesture swipe (non-toggle) | LOCK/TRACK | NO | YES | `element_button_up` |
| Gesture swipe (non-toggle) | HOVER | YES | YES | `handler.c:520-523` |
| Gesture swipe (toggle) | ALL | NO | Only on deselect | `gesture_toggled` guard |
| Long-press (non-toggle) | LOCK/TRACK | NO | YES | `element_button_up` |
| Long-press (non-toggle) | HOVER | YES | YES | `handler.c:524-527` |
| Long-press (toggle) | ALL | NO | Only on deselect | `lp_toggled` guard |

### 9.2 HOVER Gesture Release Code (`handler.c:519-532`)

```
release_element_bindings(prev, result, false);  // primary only

// Manual gesture release for non-toggle:
if (prev->gesture_swipe_triggered && !prev->gesture_toggled)
    release_bindings_list + gesture_swipe_triggered = false
if (prev->gesture_long_press_triggered && !prev->lp_toggled)
    release_bindings_list + gesture_long_press_triggered = false

// Neutralize restore block:
first_btn_gest_swipe = false
first_btn_gest_lp = false
first_btn_lp_arm = false
first_btn_gest_timer = false
```

---

## 10. State Machines

### 10.1 Gesture Finger States

```
IDLE → TAP_WAITING (finger-down)
TAP_WAITING → LONG_PRESSING (LP timer fires)
TAP_WAITING → DRAGGING (drag threshold exceeded)
TAP_WAITING → DOUBLE_TAP_WAITING (first tap-up)
DOUBLE_TAP_WAITING → TAP_WAITING (second tap confirm)
DOUBLE_TAP_WAITING → IDLE (timeout → deferred S)
LONG_PRESSING → DRAGGING (drag after LP)
DRAGGING → IDLE (finger-up)
```

### 10.2 Element Toggle States

```
idle (selected=false)
  → finger-down + toggle flag
    → selected=true, binding pressed
    → finger-up
      → selected=true (persists for toggle)
      → finger-down again
        → selected=false, binding released
```

### 10.3 Tick Autorepeat

```
toggle+AR && selected:
  if interval elapsed: release + press all AR bindings

non-toggle AR && engaged:
  if inside + interval: alternate press/release primary
```

---

## 11. Edge Cases & Defensive Measures

### 11.1 LOCK Mode Double-Fire (FIXED Phase 1)

**Fix**: `selected` flipped on DOWN (not UP). `button.c:100-104`:
```c
e->selected = false;
e->visual_active = false;
if (has_primary) release_binding(result, &e->bindings[0]);
```
UP handler now no-ops for LOCK deselect: `button.c:374-383`.

### 11.2 gesture_toggled/lp_toggled Stale Bindings (FIXED Phase 1)

**Fix**: Always clear on new touch in `handle_element_down` (`shared.c:183-190`). Release any still-held bindings first.

### 11.3 Gesture Handler Re-Entry (FIXED Phase 1)

**Fix**: `handler.c:630-645` — after `handle_element_down` clears toggle flags, re-press toggle-type gesture bindings that were active before the call.

### 11.4 Toggle State Lost on Reset (FIXED Phase 1)

**Fix**: `touch_processor.c:715-735` — save `selected`, `gesture_toggled`, `lp_toggled` before `element_reset_runtime` and restore after.

### 11.5 Toggle State Lost on Set Elements (FIXED Phase 1)

**Fix**: `touch_processor.c:64-185` — save/restore toggle state by element index across `memcpy`.

### 11.6 50ms Debounce (FIXED Phase 1)

**Fix**: `TOGGLE_DEBOUNCE_MS` 50ms guard on all 4 toggle flip paths:
- Toggle+AR deselect (`button.c:22-25`)
- Toggle+AR select (`button.c:85-88`)
- HOVER deselect (`button.c:101-104`)
- LOCK deselect (`button.c:109-112`)
- TRACK up flip (`button.c:338-341`)

### 11.7 release_element_bindings Signature Change (Phase 1)

**Change**: Added `bool release_gestures` parameter (`shared.c:288`):
- `release_element_bindings(e, result, true)` — full release (cancel, LOCK up)
- `release_element_bindings(e, result, false)` — primary only (HOVER transition)

### 11.8 MAX_GESTURE_TOGGLED_ACTIONS = 16

No overflow warning if limit exceeded. Element-level toggle path bypasses this array, so risk is limited to gesture-level toggles.

### 11.9 Timer Drift

`process_auto_repeat_burst` uses direct time comparison — no accumulator. Missed ticks do not accumulate.

### 11.10 Cancel Path Safety Net

`touch_processor_on_finger_cancel` force-releases all bindings even if `selected`, bypassing toggle preservation.

---

## 12. Configuration & Data Flow

### 12.1 Java Side

- `BindPackage.java`: `toggleSwitch`/`autoRepeat` fields, `encodeToggleBitmask()`/`encodeAutoRepeatBitmask()`
- `NativeTouchProcessor.java`: `buildNativeConfig()` (gesture toggle/AR bitmasks), `buildNativeElements()` (per-slot toggle/AR/interval), `processTimer()` → `tick()`, `onTouchEvent()` → native

### 12.2 JNI Bridge (`touch_processor_jni.cpp`)

- `read_config_from_java()`: toggle bitmasks → `binding.toggle`, AR bitmasks + intervals → `binding.auto_repeat` + `binding.auto_repeat_interval_ms`
- `nativeSetElements()`: reads per-element toggle/AR/interval arrays, calls `touch_processor_set_elements()`

### 12.3 Native Config (`touch_processor.c`)

`touch_processor_set_elements()` computes:
- `e->cached_has_toggle` = `element_has_toggle(e)`
- `e->cached_has_auto_repeat` = `element_has_auto_repeat(e)`
- `g_state.cfg.caps_has_element_toggle` = any element has toggle
- `g_state.cfg.caps_has_auto_repeat_buttons` = any button has AR
- `e->cached_auto_repeat_interval` = first non-zero toggle/AR interval

---

## 13. Phase 1 Changes Summary

| Fix | Files | Lines Changed | Impact |
|-----|-------|---------------|--------|
| LOCK double-fire | `button.c:100-104, 148-151, 374-387` | ~30 | Flipped selected on DOWN, UP cleanup only |
| gesture_toggled clear | `shared.c:181-190` | ~10 | Release+clear on each new touch |
| 50ms debounce | `button.c:5, 22-25, 85-88, 101-104, 338-341` | ~20 | Prevent rapid toggle flip |
| Gesture re-entry fix | `handler.c:630-645` | ~15 | Re-press toggle bindings after handle_element_down |
| Toggle save/restore reset | `touch_processor.c:715-735` | ~20 | Save before reset, restore after |
| Toggle save/restore set_elements | `touch_processor.c:64-185` | ~25 | Save/restore by element index |
| release_element_bindings param | `shared.c:288-305`, callers | ~30 | Added bool release_gestures |
| Visual guard GAP 1-5 | `button.c`, `shared.c`, `touch_processor.c` | ~20 | All 5 gaps confirmed fixed |

---

## 14. Dead Code Inventory

**Finger-event handlers — DEAD:**
| File | Function | Lines | Notes |
|------|----------|-------|-------|
| `activation.c` | `activation_handle_down` | 193-364 | Never called from Java |
| `activation.c` | `activation_handle_move` | 367-532 | Never called from Java |
| `activation.c` | `activation_handle_up` | 534-598 | Never called from Java |
| `activation.c` | `toggle_slide_over` | 86-154 | Only called from dead code |
| `button.c:192-233` | `element_button_move` toggle slide-over | ~42 | Dead — handler.c skips handle_element_move for buttons |
| `activation.c` | `process_engaged_non_buttons` | ~18 | Only called from dead code |

**Visual state / reset functions — LIVE:**
| File | Function | Lines | Notes |
|------|----------|-------|-------|
| `activation.c` | `activation_get_visual_states` | 55-70 | Called from Java rendering |
| `activation.c` | `activation_get_element_visual` | 72-86 | Called from Java rendering |
| `activation.c` | `activation_activate_at` | 88-94 | Reachable |
| `activation.c` | `activation_deactivate_all` | 96-113 | Reachable |
| `activation.c` | `activation_tracked_count` / `activation_hovered_for_ptr` | 115-130 | Called |
| `activation.c` | `activation_reset` | 131-137 | Called from reset path |

---

## 15. Reference Index

| File | Path | Lines | Content |
|------|------|-------|---------|
| `touch_processor.h` | `app/src/main/cpp/winlator/touch_processor.h` | 535 | Public API: TouchBinding (toggle, auto_repeat), TouchElement (selected, cached flags, toggle_debounce_last_time), TouchProcessorConfig |
| `touch_processor_internal.h` | `app/src/main/cpp/winlator/touch_processor_internal.h` | 539 | Internal state: gesture_toggled_actions[16], element_has_toggle/auto_repeat, element_is_toggle_active |
| `touch_processor.c` | `app/src/main/cpp/winlator/touch_processor.c` | 776 | Core: set_elements (cached flags + toggle save/restore), tick (AR burst, LP/gesture timers), reset, cancel |
| `touch_processor_jni.cpp` | `app/src/main/cpp/winlator/touch_processor_jni.cpp` | 1114 | JNI: read_config_from_java (toggle/AR bitmasks), nativeSetElements |
| `element/button.c` | `app/src/main/cpp/winlator/element/button.c` | 432 | Element-level toggle+AR: down (burst + normal + LOCK fix), move, up (flip, debounce) |
| `element/shared.c` | `app/src/main/cpp/winlator/element/shared.c` | 419 | Dispatch: handle_element_down/move/up, release_element_bindings (with release_gestures param), element_reset_runtime |
| `element/dpad.c` | `app/src/main/cpp/winlator/element/dpad.c` | 47 | DPad: petal activation with dead zone |
| `gesture/executor.c` | `app/src/main/cpp/winlator/gesture/executor.c` | 364 | Gesture actions: execute_actions_impl (toggle + AR), press_binding, process_scheduled_actions |
| `gesture/handler.c` | `app/src/main/cpp/winlator/gesture/handler.c` | 1056 | Gesture FSM (SOLE LIVE PATH): handle_gesture_down/move/up, TRACK/HOVER tracking, toggle slide-over, HOVER gesture release, re-entry fix |
| `gesture/entry.c` | `app/src/main/cpp/winlator/gesture/entry.c` | 382 | Gesture entry: touchpad_finger_down/up, double-tap confirm |
| `gesture/base.c` | `app/src/main/cpp/winlator/gesture/base.c` | 552 | Gesture core: check_start_drag, gesture_tick, tap_up_impl (4 paths) |
| `touch_processor_activation.c` | `app/src/main/cpp/winlator/touch_processor_activation.c` | 614 | **DEAD CODE**: activation_handle_down/move/up, toggle_slide_over, process_engaged_non_buttons |
| `BindPackage.java` | `app/src/main/java/.../BindPackage.java` | 299 | Java config: toggleSwitch/autoRepeat, encodeToggleBitmask, encodeAutoRepeatBitmask |
| `NativeTouchProcessor.java` | `app/src/main/java/.../NativeTouchProcessor.java` | 838 | Java bridge: buildNativeConfig, buildNativeElements, tick/event dispatch |

---

## 16. Key Constants

| Constant | Value | File | Line |
|----------|-------|------|------|
| `MAX_HELD_ACTIONS` | 16 | `touch_processor_internal.h` | 33 |
| `MAX_SCHEDULED_ACTIONS` | 32 | `touch_processor_internal.h` | 32 |
| `MAX_ELEMENTS` | 128 | `touch_processor.h` | 13 |
| `MAX_FINGERS` | 8 | `touch_processor.h` | 11 |
| `MAX_TRACKED_PER_POINTER` | 8 | `touch_processor_internal.h` | 20 |
| `GESTURE_TIMER_MS` | 50 | `touch_processor_internal.h` | 24 |
| `TOGGLE_DEBOUNCE_MS` | 50 | `button.c` | 4 |
| `BUTTON_MIN_KEEP_PRESSED_MS` | 300 | `touch_processor.h` | 20 |
