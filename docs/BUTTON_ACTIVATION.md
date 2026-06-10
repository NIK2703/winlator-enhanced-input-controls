# Concept: Activation Modes affect ONLY buttons

## 1. Problem

The system has two parallel code paths for processing touches (gesture path and
activation path). In the activation path (legacy), TRACK and HOVER modes incorrectly
handle non-button elements (Stick, DPad, Trackpad, RangeButton):

- **TRACK move**: activates new non-button elements when sliding a finger onto them
- **HOVER move**: releases non-button when exiting boundaries + activates new ones
- **TRACK UP**: does not release engaged non-button elements

Non-button element handlers do not have boundary checks — this is correct by design:
dpad, stick, trackpad, range_button always handle move, even outside boundaries.
Only `element_button_move` checks for hit-testing within the element.

## 2. Core Principle

> **Activation Mode (TRACK/HOVER/LOCK) modulates the behavior of ONLY buttons.**
> **All non-button elements always behave as in LOCK mode:**
> - Activate on DOWN at the touch point
> - Remain locked to the element (no new non-button elements activated on finger slide)
> - Receive move events even when the finger moves outside boundaries
> - Release only on UP

```
Non-Button Elements (always LOCK semantics):
  DOWN ──► Element activated
  MOVE ──► Always move (even outside boundaries). Never activate new non-button elements
  UP   ──► Release

Button Elements (depend on activation_mode):
         LOCK            TRACK                 HOVER
  DOWN ─► One button    track + handle_down   handle_down + hovered_element
  MOVE ─► Same button   accumulate buttons    enter/leave between buttons
  UP   ─► release_engaged  tracked + non-buttons  release_engaged
```

## 3. Gesture Zone Priority

### Behavior: already correct

**Key mechanism**: an element once engaged by a finger blocks
gesture processing for that finger forever (until finger-up).

### 3.1 Finger pressed in gesture zone → moved onto elements

```
Finger DOWN on empty space:
  → gesture processing STARTED (state = TAP_WAITING)
  → long-press timer started

Finger MOVE onto element:
  → TRACK/HOVER button: handle_element_down + tb->count++
  → Non-button: NOT engaged on MOVE (only on DOWN)
  → tb->count > 0 → return (gesture move blocked)
  → BUT: gesture_tick CONTINUES WORKING (long-press may fire) ✓
```

### 3.2 Finger pressed on element → moved into gesture zone

```
Finger DOWN on element → gesture processing NOT STARTED
Finger MOVE onto empty space → element is still engaged
  → had_element_move = true on each move
  → gesture processing NEVER starts for this finger ✓
```

### 3.3 Matrix: Gesture Zone ↔ Elements

| Initially pressed on | Moving onto | Gesture remains? | Comment |
|---|---|---|---|
| Gesture zone | Non-button | **YES** | Non-button not engaged on MOVE |
| Gesture zone | TRACK/HOVER button | **YES** (via tick) | gesture_tick works |
| Gesture zone | LOCK button | **NO** | LOCK not engaged on MOVE |
| Element (any) | Empty space | **NO** | had_element_move = true |
| Element (any) | Another element | **NO** | First element is still engaged |

Gesture zone priority is consistent across all three modes (LOCK/TRACK/HOVER).

### 3.4 Two fingers

| Finger 1 | Finger 2 | Result |
|---------|---------|-----------|
| Gesture zone | Element | Finger 1: gesture. Finger 2: element. Independent. |
| Element | Gesture zone | Finger 1: locked. Finger 2: gesture zone. |
| Gesture zone (LP ticking) | Gesture zone | main + second finger processing |

---

## 4. Long-press/Gesture release

### 4.1 Release architecture

Two independent mechanisms for tracking held bindings:
1. **Gesture system**: `gesture_held_actions[]` — for global gestures (tap, LP, drag)
2. **Element system**: per-element flags (`gesture_swipe_triggered`, `gesture_toggled`,
   `lp_toggled`) — uses `press_binding`/`release_binding` directly

### 4.2 LOCK/TRACK: release only on finger-up

```
Finger DOWN with long-press/gesture → defer_primary = true
Finger MOVE (Track: slide onto another button):
  → First button in tb->element_indices, bindings NOT released
Finger MOVE (Lock: slide outside button):
  → handle_element_move, gesture/long-press NOT released
Finger UP:
  → gesture_long_press_triggered: release (if !lp_toggled) ✓
  → gesture_swipe_triggered: release (if !gesture_toggled) ✓
```

### 4.3 HOVER: non-toggle gesture bindings release on slide-off

In HOVER, non-toggle gesture bindings are released on slide-off. Toggle flags
(`gesture_toggled`, `lp_toggled`) are PRESERVED.

```
Finger DOWN on button A with long-press/gesture → gesture fires
Finger MOVE from A to B:
  → release_element_bindings(A): primary released
  → gesture_swipe_triggered && !gesture_toggled: RELEASE gesture bindings
  → gesture_long_press_triggered && !lp_toggled: RELEASE LP bindings
  → restore block neutralized (will not restore gesture on A)
  → A: current_ptr_id = -1, engaged = false
  → B: handle_element_down

Gesture bindings:
  - Non-toggle: RELEASED on slide-off ✓
  - Toggle (gesture_toggled/lp_toggled): PRESERVED ✓
```

### 4.4 Release matrix

| Binding type | Mode | Slide-off | Finger-up |
|---|---|---|---|
| Primary | LOCK/TRACK | NO | YES |
| Primary | HOVER | **YES** | YES |
| Gesture swipe (non-toggle) | LOCK/TRACK | NO | YES |
| Gesture swipe (non-toggle) | HOVER | **YES** | YES |
| Gesture swipe (toggle) | LOCK/TRACK | NO | YES (on deselected) |
| Gesture swipe (toggle) | HOVER | NO | YES (on deselected) |
| Long-press (non-toggle) | LOCK/TRACK | NO | YES |
| Long-press (non-toggle) | HOVER | **YES** | YES |
| Long-press (toggle) | LOCK/TRACK | NO | YES (on deselected) |
| Long-press (toggle) | HOVER | NO | YES (on deselected) |

**Key pattern**: while `gesture_toggled`/`lp_toggled` is true — bindings are NOT released
(neither on slide-off nor on finger-up). Release only on explicit deselected.

---

## 5. Full scenario analysis

### 5.1 LOCK Mode (no changes)

| # | Scenario | Behavior |
|---|----------|-----------|
| 1 | Press button A → release | A activates → deactivates |
| 2 | Press button A → slide to B | A locked, B not activated |
| 3 | Press stick → slide outside stick | Stick locked (continues move) |
| 4 | Press stick → slide onto button | Stick locked, button NOT activated |
| 5 | Press button → slide onto stick | Button locked, stick NOT activated |
| 6 | Gesture zone → slide onto button | Button NOT engaged (LOCK not engaged on MOVE) |
| 7 | Gesture zone → slide onto stick | Stick NOT engaged on MOVE |

### 5.2 TRACK Mode

| # | Scenario | Behavior |
|---|----------|-----------|
| 1 | Button A → B | A+B tracked (buttons: TRACK) |
| 2 | Button → Stick | Stick NOT activated (non-button LOCK) |
| 3 | Stick → Button | Stick locked, button NOT activated (non-button engaged blocks buttons) |
| 4 | Trackpad → Button | Trackpad locked (cursor continues moving), button NOT activated |
| 5 | Stick1 → Stick2 | Stick1 locked, Stick2 NOT activated |
| 6 | TRACK UP (stick + buttons) | Tracked buttons + engaged non-buttons — all released |
| 7 | Gesture zone → Button | gesture_tick continues |
| 8 | Button (LP ticking) → another button | LP continues ticking via gesture_tick |
| 9 | Gesture swipe on first TRACK button (non-toggle) | **Does NOT work** — `element_button_move` is not called for buttons in TRACK |
| 10 | Gesture swipe on second TRACK button | Does not work — suppressed |
| 11 | LP on first TRACK button | Works via gesture_tick |
| 12 | LP on second TRACK button | Does not work — suppressed |

**Important:** Gesture-by-movement (swipe) **does NOT work in TRACK** — `element_button_move()`
is never called for buttons in TRACK (architectural limitation).
Gesture decision (swipe threshold vs LP timer first-wins) for the first button
is not applied — LP works via tick, swipe via move is blocked.

### 5.3 HOVER Mode

| # | Scenario | Behavior |
|---|----------|-----------|
| 1 | Button A → B | A deactivate, B activate |
| 2 | Button → Stick | Stick NOT activated (non-button LOCK) |
| 3 | Stick → Button | Stick locked, button NOT activated (non-button engaged blocks buttons) |
| 4 | Trackpad → Button | Trackpad locked (cursor continues moving), button NOT activated |
| 5 | Stick → (outside boundaries) | Stick locked (continues move) |
| 6 | Button A (gesture non-toggle) → B | Gesture bindings A RELEASED on slide-off |
| 7 | Button A (toggle gesture) → B | `gesture_toggled` preserved, bindings NOT released |
| 8 | Gesture zone → Button | gesture continues |
| 9 | Button A (toggle) → B | toggle_slide_over |

### 5.4 Requirement 1: Gesture zone with elements

| # | Scenario | Result |
|---|----------|-----------|
| 1 | Gesture zone DOWN → slide onto TRACK button → slide onto another → UP | LP via gesture_tick. A→B tracked. |
| 2 | Gesture zone DOWN (LP ticking) → slide onto stick → slide back | LP continues. Stick not engaged on MOVE. |
| 3 | Element DOWN → slide into gesture zone → slide onto another | First locked. Second button may be tracked. |
| 4 | Gesture zone DOWN → DRAGGING → slide onto element | DRAGGING preserved (gesture_tick). |

### 5.5 Requirement 2: Long-press/gesture release

| # | Scenario | Result |
|---|----------|-----------|
| 1 | LOCK: button with LP → slide outside button → UP | LP bindings held until UP |
| 2 | TRACK: button with gesture → slide onto another → UP | First: gesture preserved. Second: suppressed. |
| 3 | HOVER: button with gesture (non-toggle) → slide onto another | Gesture RELEASED on slide-off |
| 4 | HOVER: button with gesture → slide onto empty space | Non-toggle gesture RELEASED. Toggle preserved. |
| 5 | TRACK: toggle with LP → slide onto another toggle | toggle_slide_over. LP toggle preserved. |
| 6 | TRACK: button with gesture → slide onto another non-toggle | First: gesture-by-movement does not work (TRACK). Second: suppressed. |

---

## 6. Behavioral specification

### 6.1 Non-button LOCK semantics

In TRACK and HOVER move:
- Non-button elements receive move-forward (no release on boundary exit)
- New non-button elements are NOT activated when sliding a finger
- Non-button elements are already locked on DOWN — LOCK semantics preserved

### 6.2 Non-button Engagement Priority

> **A non-button element (Trackpad, Stick, DPad, RangeButton), once engaged,
> blocks activation of ANY buttons when sliding a finger.**

This is true for all three modes (LOCK/TRACK/HOVER). If a finger starts on a non-button
and slides onto a button:
- Non-button continues to receive move events (trackpad moves cursor, stick controls axis)
- Button is NOT activated (not added to tracked, does not receive handle_element_down)
- Toggle slide-over does NOT fire
- HOVER transition does NOT start

Implementation: in `handle_gesture_move` an early return on `had_element_move` after
the engaged cycle prevents execution of toggle slide-over and HOVER/TRACK tracking code.

### 6.3 HOVER gesture release

- Non-toggle gesture bindings (swipe, LP) are released on slide-off from a button
- Toggle gestures (`gesture_toggled`, `lp_toggled`) are PRESERVED
- Restore mechanism does not restore gesture state after slide-off

### 6.4 TRACK — gesture-by-movement does not work

- `element_button_move()` is NOT called for buttons in TRACK mode (architectural limitation)
- LP works via `gesture_tick` (independent of move)
- Gesture-by-movement for the first TRACK button is **unavailable**
- Desired behavior (swipe on the first TRACK button) requires adding
  a call to `handle_element_move` in the TRACK section of `handle_gesture_move`

### 6.5 Gesture-by-movement routing

- **LOCK**: `element_button_move` is always called (via engaged loop)
- **TRACK**: `element_button_move` is NOT called for buttons. Gesture-by-movement does not work.
- **HOVER**: `element_button_move` is called for the first tracked non-toggle button
- Toggle buttons are excluded — use `gesture_timer_armed` and toggle_slide_over

---

## 7. Verification matrix

```
Finger Down → Slide →
                  | LOCK Button | TRACK Button | HOVER Button | Stick | DPad | Trackpad | Gesture Zone
──────────────────┼─────────────┼──────────────┼──────────────┼───────┼──────┼──────────┼─────────────
LOCK Button       | LOCK locked | LOCK locked  | LOCK locked  | LOCK  | LOCK | LOCK     | LOCK locked
TRACK Button      | tracked     | tracked      | tracked      | LOCK  | LOCK | LOCK     | tracked
HOVER Button      | hover→off   | hover→off    | hover→off    | LOCK  | LOCK | LOCK     | hover→off
Stick             | LOCK locked | LOCK locked  | LOCK locked  | LOCK  | LOCK | LOCK     | LOCK locked
DPad              | LOCK locked | LOCK locked  | LOCK locked  | LOCK  | LOCK | LOCK     | LOCK locked
Trackpad          | LOCK locked | LOCK locked  | LOCK locked  | LOCK  | LOCK | LOCK     | LOCK locked
Gesture Zone      | gesture*    | gesture*     | gesture*     | LOCK**|LOCK**| LOCK**   | gesture
```

```
Finger UP → Release:
  LOCK:      all engaged
  TRACK:     tracked buttons + engaged non-buttons
  HOVER:     current hover + all engaged
             non-toggle gesture bindings released on hover transition
  Gesture:   gesture state machine cleanup
```

* = Via gesture_tick (long-press/double-tap). Move processing blocked.
** = Non-button not engaged on MOVE in gesture path

### Non-button LOCK semantics
- Non-buttons: move only, never released on boundary, never activate new ones
- TRACK UP: non-button release together with tracked buttons

### Gesture release
- HOVER slide-off: non-toggle gesture bindings released, toggle preserved
- LOCK/TRACK: bindings held until finger-up

### Gesture-by-movement routing
- TRACK: does NOT work — `element_button_move` not called for buttons
- HOVER: first hovered button (non-toggle) — gesture-by-movement works
- Second+ buttons: suppressed (gesture does not work)
- Toggle buttons: use timer-based gesture, not movement-based

---

## 8. Toggle Switch Persistence

### 8.1 Rule

> **Toggle switch (activated via LP/swipe/gesture) persists across
> touch boundaries.** The `gesture_toggled`/`lp_toggled` flags are NOT reset on
> a new finger-down. Toggle deactivation occurs ONLY on repeated
> activation of the same gesture (LP/swipe) that activated it.

```
Touch N:  LP fires       → lp_toggled=true  → ON
Touch N+1: Quick tap     → lp_toggled=true   persists → ON
Touch N+2: LP fires      → lp_toggled=false  → OFF
Touch N+3: LP fires      → lp_toggled=true   → ON
```

Visual activity persists via `element_is_toggle_active()`:
```
  (e->cached_has_toggle && e->selected) || e->lp_toggled || e->gesture_toggled
```

### 8.2 All scenarios (toggle LP, no primary binding)

| # | Scenario | Behavior |
|---|----------|-----------|
| 1 | Quick tap (LP did not fire) | toggle OFF (was never ON) |
| 2 | LP fired → toggle ON | toggle ON, visual stays |
| 3 | Quick tap AFTER toggle ON | toggle ON (persist) |
| 4 | LP fired AFTER toggle ON | toggle OFF (alternation) |
| 5 | Quick tap AFTER toggle OFF | toggle OFF |
| 6 | LP fired AFTER toggle OFF | toggle ON (alternation) |

### 8.3 All scenarios (gesture toggle, no primary binding)

| # | Scenario | Behavior |
|---|----------|-----------|
| 1 | Swipe → toggle ON | toggle ON |
| 2 | Quick tap AFTER toggle ON | toggle ON (persist) |
| 3 | Swipe AFTER toggle ON | toggle OFF (alternation) |
| 4 | Quick tap AFTER toggle OFF | toggle OFF |

### 8.4 All scenarios (primary toggle + LP toggle on the same button)

| # | Scenario | Behavior |
|---|----------|-----------|
| 1 | Tap → primary toggle ON | selected=true, visual ON |
| 2 | Tap → primary toggle OFF | selected=false, visual OFF |
| 3 | LP → LP toggle ON | lp_toggled=true, visual ON |
| 4 | Tap AFTER LP toggle ON | primary toggle does not deselect — `element_button_down` sees `lp_toggled` and blocks deselect |
| 5 | Tap again (selected=true) | blocked — return without changes |
| 6 | LP → LP toggle OFF | lp_toggled=false, visual ON (selected=true) |
| 7 | Tap → primary toggle OFF | selected=false → `element_is_toggle_active` = false → visual OFF |

---

## 9. HOVER Slide-Over: Toggle preservation + gesture suppression

### 9.1 Rules

> **Rule 1 — Toggle (selected=true) is not deselected on slide-over in HOVER.**
> Entering an already activated toggle preserves its state (selected=true,
> visual_active=true). Deselect — only via direct finger-down on the element
> (element_button_down).

> **Rule 2 — When entering from gesture zone (empty space), element gestures
> (LP, gesture-by-movement) are suppressed.** A finger that came from gesture zone
> does not activate element LP/gesture bindings. When entering from another button,
> multi-action works normally.

### 9.2 All scenarios

**Scenario A: Non-toggle → Toggle → Another button → Back to Toggle**

| Step | Action | Behavior |
|-----|----------|-----------|
| 1 | Slide from non-toggle A to toggle B (OFF) | B SELECT: selected=true, visual=true, gesture_timer_armed=true |
| 2 | Slide from B to C | B: gesture_timer_armed=false, B remains in tracked |
| 3 | Slide from C back to B | B selected=true → visual STAYS ON (deselect blocked) |
| 4 | Finger DOWN on B (direct tap) | element_button_down → DESELECT: selected=false, visual=false |

**Scenario B: Gesture zone → Non-toggle button**

| Step | Action | Behavior |
|-----|----------|-----------|
| 1 | Slide from empty space onto button A | handle_element_down → suppress_element_gestures (prev_idx < 0) |
| 2 | Hold on A | LP timer does NOT fire (long_press_arm reset) |
| 3 | Movement > threshold on A | Gesture-by-movement does NOT fire (gesture_suppressed=true) |

**Scenario C: Gesture zone → Toggle button**

| Step | Action | Behavior |
|-----|----------|-----------|
| 1 | Slide from empty space onto toggle B | Phase 2: SELECT (or preserve). Phase 3: suppress + gesture_timer_armed preserved |
| 2 | Slide from B to C and back | Same as Scenario A: deselect blocked |

---

## 10. Complete isolation of gesture zone and elements

### 10.1 Principle

> **A finger that started in the gesture zone (empty space) NEVER activates
> control elements. A finger that started on an element NEVER triggers
> the gesture zone (touch gesture panel).**

Exception: **passthrough_touch** — a button with this flag passes the touch
through to the gesture zone for two-finger gestures, blocking single-finger ones.

### 10.2 Isolation matrix

| Finger started on | Moving onto | Elements | Gesture zone |
|----------------|-------------|----------|--------------|
| Gesture zone | Element | **NO** ❌ | Continues ✅ |
| Gesture zone | Empty space | — | Continues ✅ |
| Element (any) | Element | First locked ✅ | **NO** ❌ |
| Element (any) | Empty space | First locked ✅ | **NO** ❌ |
| Passthrough button | Element | **NO** ❌ | Continues ✅ |
| Passthrough button | Empty space | Passthrough locked ✅ | Continues ✅ |

### 10.3 Mechanism

**Element finger** (state == IDLE, engaged_elem_count > 0):
- In `handle_gesture_move`: engaged cycle handles element move
- `had_element_move = true` → early return (for non-button)
- For TRACK/HOVER buttons: toggle slide-over and tracking work (without gestures)
- For LOCK buttons: engaged cycle returns early, gestures are not started
- In `handle_gesture_up`: element releases, return before gesture up

**Gesture finger** (state > IDLE, gesture FSM started):
- In `handle_gesture_move`: `skip_element_activation = true`
- Toggle slide-over: **SKIP**
- HOVER/TRACK tracking: **SKIP**
- Gesture processing (drag, cursor, tick): **CONTINUES**

**Passthrough finger** (passthrough element in engaged list):
- `has_passthrough_engagement = true` → `skip_element_activation = true`
- Toggle slide-over and tracking: **SKIP**
- Gesture processing: continues (state == IDLE, but registered as main)
- `execute_actions_impl` blocks single-finger gestures via `passthrough_active`
- Second finger can enter as second finger → two-finger gestures work

### 10.4 Passthrough Touch

**Purpose:** A button that:
1. Executes its own binding (e.g., left mouse click)
2. Passes the touch through to the gesture zone
3. Blocks single-finger gestures (tap, LP, swipe)
4. Allows two-finger gestures (two-finger tap, scroll)

**Implementation:**
- `handle_gesture_down`: button finds and activates (binding fires),
  `handled` remains false → passthrough handler: `f->state = IDLE`,
  `gesture_main_ptr_id = f->ptr_id`
- Two-finger scenario:
  - Finger A on passthrough → registered as main
  - Finger B on gesture zone → enters as second finger
  - Second finger B gets second-finger gesture bindings (S2, D2, Sd2, Dd2)
  - Gesture execution works (passthrough_active = false at the time B enters)
- `executor.c`: `execute_actions_impl` checks `passthrough_active` —
  blocks execution of single gestures, but does not affect second finger

| Scenario | Behavior |
|----------|-----------|
| Tap on passthrough button | Binding fires (e.g., MOUSE_LEFT). Tap gesture is not processed |
| Hold on passthrough | Binding held. LP does not fire |
| Swipe on passthrough | Binding held. Gesture swipe does not fire |
| Passthrough + second finger on gesture zone | Two-finger gestures work (right-click, scroll) |
| Both fingers on passthrough | Gesture processing blocked (passthrough_active = true) |
| Second finger on passthrough (not on gesture zone) | Two-finger gestures do NOT work — second finger must be on gesture zone |
