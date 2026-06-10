# Gesture Branching Tree of Winlator-Ludashi

## Summary

This document describes the complete decision tree for the gesture recognition system in Winlator-Ludashi. Each gesture is analyzed from the perspective of:
- Under what conditions it triggers
- What happens if a neighboring gesture is set/not set
- Differences between Touchpad (TP) and Touchscreen (TS) modes
- Differences between the first and second finger

---

## 1. Gesture Types

| Index | Constant | Notation | Description |
|--------|-----------|-------------|----------|
| 0 | `GESTURE_SINGLE_TAP` | S | Single tap |
| 1 | `GESTURE_LONG_PRESS` | L | Long press |
| 2 | `GESTURE_DOUBLE_TAP` | D | Double tap |
| 3 | `GESTURE_SINGLE_TAP_DRAG` | Sd | Single tap + drag |
| 4 | `GESTURE_LONG_PRESS_DRAG` | Ld | Long press + drag |
| 5 | `GESTURE_DOUBLE_TAP_DRAG` | Dd | Double tap + drag |
| 6 | `GESTURE_SINGLE_2ND` | S2 | Single tap with second finger |
| 7 | `GESTURE_DOUBLE_2ND` | D2 | Double tap with second finger |
| 8 | `GESTURE_SINGLE_DRAG_2ND` | Sd2 | Single tap + drag with second finger |
| 9 | `GESTURE_DOUBLE_DRAG_2ND` | Dd2 | Double tap + drag with second finger |

**Important:** There are no L and Ld (long-press) for the second finger. `GESTURE_TYPE_COUNT` = 10 (terminator).

---

## 2. Gesture Finite State Machine States (`GestureState`)

| State | Description | Entry | Exit |
|-----------|----------|------|-------|
| `IDLE` (0) | No gesture processing | finger-up, reset | finger-down → `TAP_WAITING` |
| `TAP_WAITING` (1) | Waiting for event development (tap/drag/long-press) | finger-down | `DRAGGING`, `LONG_PRESSING`, `DOUBLE_TAP_WAITING`, `IDLE` |
| `TOUCHING` (2) | Not used (legacy) | — | — |
| `DOUBLE_TAP_WAITING` (3) | First tap done, waiting for second | `handle_tap_up_impl()` Path 4 | `IDLE` (timeout/cancel/confirm) |
| `LONG_PRESSING` (4) | Long-press timer fired | `gesture_tick()` LP timer | `DRAGGING` (on movement) |
| `DRAGGING` (5) | Drag threshold exceeded | `check_start_drag()` | `IDLE` (finger-up) |

---

## 3. Key Configuration Parameters

| Parameter | Default | Description | Impact on Tree |
|----------|-----------|----------|-------------------|
| `long_press_timeout_ms` | 150 | Long press timeout | LP/Ld fire after this time |
| `double_tap_timeout_ms` | 150 | Second tap waiting window | D/Dd must fit within this window |
| `single_tap_delay_ms` | 0 (TS), 0 (TP) | Delay of S execution in TS (0-10) | TS S may be deferred on finger-down |
| `drag_threshold_px` | 10 | Drag threshold | Determines when TAP_WAITING→DRAGGING |
| `double_tap_distance_px` | 50 | Max distance between taps for DT | Determines validity of D/Dd |
| `binding_delay_ms` | 0 | Delay between key presses | (0-10) |
| `cursor_speed` | 1.0 | Cursor speed in TP | Does not affect gesture tree |
| `mouse_mode` | TOUCHPAD | TP or TS | Defines the entire tree |
| `drag_mode` | AUTO | AUTO/ALWAYS/NONE | Enable/disable Sd/Ld/Dd |
| `gesture_threshold_px` | 20 | Swipe threshold on element | Only for element gesture |

### Constants (immutable)

| Constant | Value | Description |
|-----------|----------|----------|
| `MAX_TAP_TRAVEL` | 10px | Max accumulated travel for `is_tap` |
| `TAP_MAX_TIME_MS` | 200ms | Max time from down to up for tap |
| `TWO_FINGER_SCROLL_DIST` | 350px | Two-finger scroll threshold |
| `SCROLL_ACCUM_THRESHOLD` | 100px | Scroll accumulation before emission |
| `GESTURE_TIMER_MS` | 50ms | Base timer tick |
| `CLICK_DELAY_MS` | 50ms | Simulated click delay |
| `MOUSE_WHEEL_DELTA` | 120 | Mouse wheel delta |

---

## 4. Function `gesture_decide_branch()` — Central Tree Element

**Location:** `gesture/types.h:73-94`

**Purpose:** Determines the processing strategy for each pair (non-drag / drag), e.g., S/Sd, L/Ld, D/Dd.

### Input Parameters

```c
GestureBranchParams {
    has_x:        bool  // non-drag variant has bindings
    has_xd:       bool  // drag variant has bindings
    has_competing_dt: bool  // D/Dd exist (competitors)
    has_competing_lp: bool  // L/Ld exist (competitors)
    is_ts:        bool  // Touchscreen mode
    is_second:    bool  // Second finger
    hold_delay_ms: int   // Delay from config
}
```

### Output Parameters

```c
GesturePairPlan {
    hold_delay_ms:  int  // >0: defer execution to timer
    pulse_on_up:    bool // true: execute on finger-up (press+release)
    press_on_drag:  bool // true: hold when drag threshold is crossed
    drag_available: bool // drag variant exists
}
```

### Complete Decision Tree

```
gesture_decide_branch(bp):
│
├─ 1. drag_available = bp.has_xd
│
├─ 2. if (!bp.has_x) → return {} (all fields = 0)
│     Gesture not configured → do nothing
│
├─ 3. if (has_x && !has_xd):
│   │
│   ├─ 3a. if (has_competing_dt || has_competing_lp):
│   │     │
│   │     └─ press_on_drag = true  // hold non-drag on drag
│   │        pulse_on_up   = true  // execute tap on finger-up
│   │     └─ Reason: competition requires deferring the decision
│   │        until it becomes clear which gesture triggered
│   │     └─ Examples:
│   │        - S with D present: S is deferred until finger-up
│   │        - S with L present: S is deferred until finger-up/drag
│   │        - L with D present: L is deferred until finger-up
│   │
│   ├─ 3b. else if (is_ts && hold_delay_ms > 0 && !is_second):
│   │     │  (TS, not second finger, has hold_delay)
│   │     └─ hold_delay_ms = bp.hold_delay_ms
│   │     └─ Reason: in TS mode, S may be executed
│   │        with a delay after finger-down (single_tap_delay_ms)
│   │     └─ Example: S in TS with single_tap_delay_ms=5
│   │
│   ├─ 3c. else:
│         │  (no competition, no hold_delay, or TP, or second finger)
│         └─ all fields = 0 (execute immediately)
│         └─ Examples:
│           - S in TP without D and L: execute immediately on finger-down
│           - S with second finger: execute immediately on finger-down
│
└─ 4. if (has_x && has_xd):
    │
    └─ pulse_on_up = true
       (hold_delay_ms = 0, press_on_drag = false)
    └─ Reason: when both non-drag and drag variants exist,
       non-drag executes as a tap on finger-up,
       and drag triggers when the drag threshold is crossed
    └─ Examples:
       - S + Sd: S executes on finger-up, Sd on drag
       - L + Ld: L executes on finger-up (or is deferred),
         Ld on drag
```

---

## 5. First Finger Branching Tree (Main Gestures S/L/D/Sd/Ld/Dd)

### 5.1. Finger-down

```
touch_processor_on_finger_down()
│
├─ 1. hit-test of element via spatial grid
├─ 2. If element found and it is passthrough:
│     └─ state = IDLE (gestures are disabled through passthrough_active gate)
│        Note: gesture_handler_active is not touched — this field
│        is write-only dead code and does not affect logic.
│
├─ 3. If element is LOCK → exit (do not process gestures)
│
├─ 4. Otherwise: start gesture engine
│   │
│   ├─ 4a. if (gesture_double_tap_waiting && 
│   │       within double_tap_distance_px of last tap-up):
│   │   └─ double_tap_confirm_internal() → D/Dd
│   │   └─ Cancel DT_WAITING
│   │
│   ├─ 4b. else:
│   │   └─ gesture_cancel_double_tap_wait()
│   │   └─ setting initial state:
│   │       state = TAP_WAITING
│   │       gesture_handler_active = finger_has_gesture(f)
│   │
│   ├─ 4c. If TS && first finger && S exists && nothing is held:
│   │   └─ handle_ts_single_tap_hold()
│   │     │ resolve_single_tap_pair() → GesturePairPlan
│   │     └─ execute_tap_on_finger_down()
│   │       ├─ hold_delay_ms > 0  → start hold_timer
│   │       ├─ pulse_on_up || press_on_drag → defer
│   │       └─ else → execute S immediately (force_hold or tap)
│   │
│   └─ 4d. If TP:
│       └─ touchpad_finger_down() → see below
```

### 5.2. `touchpad_finger_down()` Tree (first finger)

```
touchpad_finger_down():
│
├─ Condition: is_main && gesture_main_ptr_id < 0
│
├─ 1. if (!current_mode_has_gestures() && !gesture_double_tap_waiting):
│   └─ gesture_handler_active = false
│   └─ state = IDLE
│   └─ Finger works only as cursor (LMB fallback)
│
├─ 2. if (gesture_double_tap_waiting):
│   └─ double_tap_confirm_internal() → D/Dd
│
├─ 3. Initialization:
│   └─ is_second_finger = false
│   └─ gesture_main_ptr_id = ptr_id
│   └─ original_ptr_id = ptr_id
│   └─ reset hold timers
│   └─ state = TAP_WAITING
│
└─ 4. If TS:
    └─ handle_ts_single_tap_hold() → possible hold S
```

### 5.3. Movement Handling (`handle_gesture_move()`)

```
handle_gesture_move():
│
├─ 1. Handle element activation (TRACK/HOVER)
│
├─ 2. Check thresholds:
│   └─ travel > MAX_TAP_TRAVEL → is_tap = false
│
├─ 3. For TS: update cursor position
│
└─ 4. Check drag trigger:
    └─ if (state == TAP_WAITING || state == LONG_PRESSING):
        └─ check_start_drag() → see below
```

### 5.4. `check_start_drag()` Tree — Drag Start Decision

```
check_start_drag(f):
│
├─ Guard: f->state is not TAP_WAITING and not LONG_PRESSING → return
├─ Guard: |dx| <= drag_threshold && |dy| <= drag_threshold → return
│
├─ BRANCH A: state == LONG_PRESSING
│   ├─ If no L and no Ld → start_drag_no_binding(), return
│   ├─ gesture_clear_pending_long_press()
│   └─ resolve_drag_binding(xd=Ld, x=NULL, press_on_drag=false)
│     ├─ Success → start_drag_with_binding()
│     └─ Failure → start_drag_no_binding()
│
├─ BRANCH B: gesture_post_double_tap_drag (after DT)
│   ├─ TP + second_active + !is_second_finger → return (only second finger drags)
│   ├─ No D/Dd/D2/Dd2 → clear post_dt, return
│   ├─ second_active → resolve_drag_binding with second finger bindings
│   └─ resolve_drag_binding(xd=Dd, x=D, fb=S, press_on_drag=true)
│     ├─ If resolved == D → clear pending_deferred_double_count
│     ├─ clear gesture_post_double_tap_drag
│     └─ start_drag_with_binding()
│
├─ BRANCH C: pending_deferred_double_count > 0
│   └─ transition to DRAGGING without on_drag_start()
│   └─ (pending_deferred_double must survive for finger-up)
│
├─ BRANCH D: second_active && f->is_second_finger (second finger drag)
│   ├─ Check binding: if no Sd2/Dd2/S2/D2 → start_drag_no_binding()
│   ├─ TP: reads Sd2 from config (finger bindings are zeroed for TP)
│   └─ resolve_drag_binding(xd=Sd2, x=S2, press_on_drag=has_competition)
│
├─ BRANCH E: main finger, no second_active
│   ├─ No S or Sd → start_drag_no_binding()
│   ├─ TP && !Sd → return (do nothing — cursor without binding)
│   └─ resolve_drag_binding(xd=Sd, x=S, press_on_drag=has_competition)
│
└─ BRANCH F: else → return

After resolve → start_drag_with_binding():
  ├─ Compare resolved binding with current held actions
  ├─ If different → release_held_actions() + execute_actions_hold()
  └─ start_drag_no_binding() → state = DRAGGING
```

### 5.5. Finger-up Handling (`touchpad_finger_up()`)

```
touchpad_finger_up() — second finger:
│
├─ if (gesture_is_action_held): clear state (action continues)
├─ else if (D/Dd exists): enter_sdtw() (waiting for second tap)
├─ else if (second_tap_fallback_count > 0): execute S2 fallback
├─ exec pending second-double
├─ release_held_actions()
├─ release pointer buttons (0 and 2)
└─ clear all second-finger globals

touchpad_finger_up() — first finger:
│
└─ switch(state):
    │
    ├─ TAP_WAITING:
    │   ├─ TP + no gestures → release_held, cleanup
    │   ├─ TP + (travel > threshold || time > TAP_MAX_TIME_MS):
    │   │   └─ consume DT, execute S, release, cleanup
    │   ├─ else → handle_tap_up() (possible enter DT_WAITING)
    │   └─ tap_up_cleanup()
    │
    ├─ LONG_PRESSING:
    │   ├─ execute deferred D (if pending)
    │   ├─ execute deferred LP (or LP bindings)
    │   └─ release_held_actions()
    │
    ├─ DRAGGING:
    │   ├─ execute deferred D (if not matching held)
    │   └─ release_held_actions()
    │
    ├─ DOUBLE_TAP_WAITING:
    │   ├─ execute S (in case of 3+ taps)
    │   └─ cancel DT waiting
    │
    └─ default: post-DT drag cleanup
```

### 5.6. `handle_tap_up_impl()` Tree — 4 Paths

```
handle_tap_up_impl():
│
├─ PATH 1: pending_deferred_double_count > 0
│   ├─ Execute deferred D (execute_deferred_double)
│   ├─ double_tap_consumed = true
│   └─ state = IDLE
│   └─ Condition: DT was confirmed by confirm_double_tap(),
│       D was deferred until finger-up (pulse_on_up=true) →
│       D executes now
│   └─ If D already executed as hold (pulse_on_up=false):
│       just clear pending_deferred_double_count
│
├─ PATH 2: gesture_post_double_tap_drag
│   ├─ clear flag
│   ├─ double_tap_consumed = true (prevents S on 3rd tap)
│   └─ state = IDLE
│   └─ Condition: DT was confirmed, Dd is active →
│       nothing needs to be executed (Dd already held)
│
├─ PATH 3: gesture_double_tap_consumed
│   ├─ clear consumed flag
│   ├─ fire_single_and_idle() — execute S
│   └─ state = IDLE
│   └─ Condition: 3rd tap (triple tap) —
│       double already consumed, execute S
│
└─ PATH 4: normal first tap-up
    ├─ has_dt = exists D
    ├─ if (has_dt):
    │   └─ enter_double_tap_waiting():
    │     ├─ save S in gesture_deferred_tap[]
    │     ├─ save D in gesture_pending_double[]
    │     ├─ save D in gesture_pending_deferred_double[]
    │     ├─ gesture_double_tap_waiting = true
    │     └─ state = DOUBLE_TAP_WAITING
    ├─ else if (Dd exists without D):
    │   └─ execute S on up, enter DT_WAITING (without deferred bindings)
    └─ else:
        └─ execute S, state = IDLE (single tap completed)
```

### 5.7. Timers (`gesture_tick()`)

```
gesture_tick():
│
├─ Caps gate: if no D → clear SDTW, DT_WAITING, pending_deferred
├─ If no gesture bindings → return
│
├─ TIMER A: Long-press (for each finger in TAP_WAITING)
│   ├─ Cancel: finger moved beyond threshold || action is held
│   ├─ If time_ms - down_time_ms >= long_press_timeout_ms:
│   │   ├─ gesture_clear_second_finger_state()
│   │   └─ resolve_long_press_pair(f) → lp_plan
│   │   ├─ pulse_on_up:
│   │   │   ├─ with Ld: defer L bindings → pending_deferred_long_press
│   │   │   │  state = LONG_PRESSING
│   │   │   └─ without Ld: execute L immed, if S exists → execute S
│   │   │     state = LONG_PRESSING
│   │   └─ !pulse_on_up: execute L as hold, state = LONG_PRESSING
│   └─ haptic if gesture_long_pass_haptic > 0
│
├─ TIMER B: Single-tap hold (TS first finger)
│   ├─ If single_tap_hold_delay_ms > 0 && timer expired:
│   │   ├─ If nothing is held && finger has not moved → execute S
│   │   ├─ clear deferred_tap_count, pending_double_count
│   │   └─ DO NOT clear pending_deferred_double_count!
│   │     (must survive for finger-up)
│
├─ TIMER C: SDTW (Second Double-Tap Waiting timeout)
│   ├─ If second_double_tap_waiting && D2/Dd2 exists &&
│   │   time_ms - second_tap_fallback_time >= double_tap_timeout_ms:
│   │   ├─ clear second_double_tap_waiting, pending_second_double
│   │   ├─ if second_tap_fallback exists → execute fallback (S2)
│   │   └─ main finger: if not DRAGGING → state = IDLE
│
├─ TIMER D: DT timeout
│   ├─ If gesture_double_tap_waiting &&
│   │   timer >= double_tap_timeout_ms:
│   │   ├─ clear DT waiting
│   │   ├─ if deferred S exists → execute S
│   │   ├─ gesture_clear_deferred_tap()
│   │   └─ main finger: if not DRAGGING → state = IDLE
│
└─ TIMER E: Deferred single-tap (replacement for nanosleep)
    └─ For each finger: if single_tap_deferred && timer expired:
      ├─ clear single_tap_deferred
      ├─ handle_tap_up_impl()
      └─ tap_up_cleanup()
```

---

## 6. Second Finger Branching Tree (S2/D2/Sd2/Dd2)

### 6.1. Second Finger Initialization

```
Second finger (3rd and beyond are ignored for gestures):
│
├─ Guard: if gesture_second_active is already true → state=IDLE (ignore)
├─ Guard: if no S2/D2/Dd2/Sd2 AND no SDTW/DT_WAITING → state=IDLE
│
├─ setup_second_finger_bindings():
│   └─ Copy from mb.*_2nd → fb.single_tap/long_press/double_tap/...
│   └─ long_press = NULL, long_press_drag = NULL (no L2/Ld2)
│   └─ TP: single_tap_drag = NULL (zeroed)
│
├─ Save first finger held-actions in pending_resume_action[]
├─ Cancel first finger LP timer
│
├─ If second_double_tap_waiting → confirm_second_double_tap()
├─ If global DT_WAITING → confirm_second_finger_global_dt()
│   (second finger confirms first finger's DT)
│
├─ If S2 bindings exist → execute_tap_on_finger_down()
│   (S2 executes on finger-down)
│   └─ _drag_paused = true, if first finger has
│       pending_resume_action (S2 is not released until up)
│
└─ state = TAP_WAITING, down_time_ms = time_ms
```

### 6.2. DT Confirmation by Second Finger

```
confirm_second_double_tap():
│
├─ If second_double_tap_waiting:
│   ├─ resolve D2/Dd2 pair → GesturePairPlan
│   ├─ confirm_double_tap() → D2 or deferred D2
│   ├─ clear SDTW
│   └─ state = TAP_WAITING
│
confirm_second_finger_global_dt():
│
├─ If global DT_WAITING && second finger within double_tap_distance:
│   └─ First finger's DT is confirmed by second finger
│   └─ reset first finger tap state
│   └─ clear second-finger globals
```

### 6.3. Second Finger Drag

```
check_start_drag() BRANCH D (second finger):
│
├─ TP: reads Sd2 from config (finger bindings = NULL for TP)
│
├─ If S2 is held AND no Sd2 AND no Dd2 → start_drag_no_binding()
│   (no drag-binding available — S2 remains held solo)
│
├─ resolve_drag_binding(xd=Sd2, x=S2, fb=Dd2, press_on_drag=has_competition, use_fallback=true)
│   ├─ Sd2 exists → drag-binding = Sd2
│   ├─ else S2 + press_on_drag → drag-binding = S2
│   ├─ else Dd2 (use_fallback) → drag-binding = Dd2
│   └─ else → return (no drag-binding)
│
├─ If S2 is held:
│   │  execute_actions_hold(drag-binding)  // S2 already held — add adjacent
│   └─ start_drag_no_binding()
│
└─ Else → start_drag_with_binding()
```

### 6.4. Second Finger Finger-up

```
touchpad_finger_up() — second finger:
│
├─ If action is held → clear state
│     S2 — temporary modifier, lives only while second finger
│     is touching. Main finger resumes its paused drag
│     via cleanup_second_finger_up().
├─ If D/Dd exists → enter_sdtw() (waiting for second finger double-tap)
├─ Else if S2 fallback exists → execute S2
├─ exec pending second-double
├─ release_held_actions() (except post-DT drag)
├─ release pointer buttons
└─ clear second-finger state
```

### 6.5. SDTW Timeout

```
SDTW timeout (gesture_tick() TIMER C):
│
├─ Fires after double_tap_timeout_ms from second finger up
├─ If second finger has not returned → timeout
│
├─ clear second_double_tap_waiting
├─ clear pending_second_double
├─ If S2 fallback exists → execute S2 bindings
└─ main finger: if not DRAGGING → state = IDLE
```

---

## 7. Combination Matrix: What Happens When One Gesture Is Set and Another Is Not

### 7.1. Main S/D/Dd Combinations (TS, first finger)

| S | D | Dd | Finger-down | Drag | Finger-up |
|-------|-----|-----|-------------|------|-----------|
| 0 | 0 | 0 | NOP | NOP | NOP |
| 0 | 0 | 1 | NOP | Dd (if drag) | release |
| 0 | 1 | 0 | NOP | NOP (D deferred) | enter DT_WAITING, TIME → S fallback? **No S → silence** |
| 0 | 1 | 1 | NOP | Dd (DT confirmed) | enter DT_WAITING, TIME→Dd fallback |
| 1 | 0 | 0 | **S executed** (hold in TS, tap in TP) | Sd? no → release S | release S |
| 1 | 0 | 1 | **S deferred** (press_on_drag) | **S pressed** (drag) | release S, TIME→Dd? no |
| 1 | 1 | 0 | **S deferred** (pulse_on_up) | D deferred, S pressed | enter DT_WAITING, TIME→ **S fired** |
| 1 | 1 | 1 | **S deferred** (pulse_on_up) | Dd (DT confirm), S pressed | enter DT_WAITING, TIME→ S fired or Dd |

**Key observations:**
- Without S: no feedback on tap — user does not know if something triggered
- D without S: second tap in DT window gives D, but first tap is silent
- S + D (without Dd): S is deferred (press_on_drag), executes on drag or on finger-up (pulse_on_up)
- S + Dd: S is deferred, Dd on DT confirmation

### 7.2. S/L/Ld Combinations

| S | L | Ld | Finger-down | LP timer | Drag | Finger-up |
|-------|-----|-----|-------------|----------|------|-----------|
| 1 | 0 | 0 | execute S | — | — | release S |
| 1 | 1 | 0 | **S deferred** | L executed, **S cancelled** (suppressed by LP competition) | — | release |
| 1 | 1 | 1 | **S deferred** | L deferred (pulse_on_up) | Ld (drag) | L fired (if not drag) |
| 1 | 0 | 1 | **S deferred** | — | **S pressed** (drag) | release S |
| 0 | 1 | 0 | — | L executed | — | release L |
| 0 | 1 | 1 | — | L deferred | Ld (drag) | L fired (if not drag) |

**Key observations:**
- L + Ld: L is deferred until finger-up (pulse_on_up), Ld on drag
- S + L: S is deferred due to LP competition, cancelled on LP timer (S+L simultaneously on long-press is undesirable — would give an unexpected left+right click)
- S without L, without Sd: S executes immediately on finger-down

### 7.3. Second Finger Combinations (S2/D2/Sd2/Dd2)

| S2 | D2 | Sd2 | Dd2 | Finger-down | Drag | Finger-up |
|------|-----|------|-----|-------------|------|-----------|
| 1 | 0 | 0 | 0 | **S2 held** | — | release S2, enter SDTW? no |
| 1 | 1 | 0 | 0 | **S2 held** | — | enter SDTW, TIME→S2 fallback |
| 1 | 0 | 1 | 0 | **S2 held** | Sd2 | release |
| 1 | 0 | 0 | 1 | **S2 held** | S2 (press_on_drag) | release |
| 0 | 1 | 0 | 0 | — | — | enter SDTW, TIME→NOP (no S2 fallback) |
| 0 | 1 | 0 | 1 | — | Dd2 (drag) | enter SDTW, TIME→NOP |
| 1 | 1 | 0 | 1 | **S2 held** | Dd2 (DT confirm) | enter SDTW, TIME→S2 fallback |

**Key observations:**
- No L/Ld for second finger (LONG_PRESS is zeroed in setup_second_finger_bindings)
- Sd2 in TP: read directly from config, not from finger bindings (which are zeroed for TP)
- S2 executes on finger-down (hold) always (force_hold, without considering D2 competition)

### 7.4. Combinations: Nothing Set (All Gestures = 0)

```
current_mode_has_gestures() = false:
│
├─ gesture_handler_active = false
├─ state = IDLE
├─ Finger works as cursor (LMB fallback in TP)
└─ finger-up → release_held_actions(), cleanup
```

In this mode, **no gesture triggers**. The finger only controls the cursor. If LMB is needed, it must be set explicitly through a control element.

---

## 8. TP vs TS Differences

### 8.1. Touchscreen (TS)

| Aspect | Behavior | Reason |
|--------|-----------|---------|
| Cursor | Absolute (finger → screen position) | Finger coordinates directly map to screen |
| S execution | May have delay (hold_delay) if single_tap_delay_ms is set | TS single_tap_hold mechanism |
| Sd | Enabled | In TS, Sd is useful for drag-and-drop |
| `setup_main_finger_bindings` | All bindings are copied as-is | TS uses all types |
| Drag threshold | S+Sd: S on finger-down (hold), Sd on drag | `gesture_decide_branch()` → pulse_on_up |

### 8.2. Touchpad (TP)

| Aspect | Behavior | Reason |
|--------|-----------|---------|
| Cursor | Relative (movement delta → cursor offset) | Delta with acceleration |
| S execution | On finger-up (via handle_tap_up → fire_single_and_idle) | TP must distinguish tap from cursor movement. If S fired on finger-down, every touch would give a click |
| Sd | **Zeroed** | TP: Sd is unavailable for gestures (see `setup_main_finger_bindings()` line 273) |
| `setup_main_finger_bindings` | `single_tap_drag = NULL` | Forced disable of Sd in TP |
| Drag threshold | S+Sd: S on finger-up (pulse_on_up) without drag | `gesture_decide_branch()` → pulse_on_up |
| Two-finger drag | Summed delta of both fingers | Two fingers for scrolling |
| LMB fallback | If no gestures → finger = cursor | Basic TP behavior |
| `check_start_drag()` BRANCH D (second finger) | Reads Sd2 from config directly | finger bindings are zeroed for TP |

### 8.3. Critical Differences in the Tree

```
Situation: S + Sd configured
├─ TS: S → hold on finger-down, Sd → on drag
└─ TP: S → pulse_on_up (finger-up), Sd → NONE (zeroed)
      Result: drag has no binding in TP

Situation: D + Dd configured
├─ TS: D → on DT confirmation, Dd → on drag after DT
└─ TP: D → on DT confirmation, Dd → on drag after DT
      (Dd works in TP, unlike Sd)

Situation: S2 + Sd2 (second finger)
├─ TS: S2 on finger-down, Sd2 on drag
└─ TP: S2 on finger-down, Sd2 on drag (read from config)
      (Sd2 works in TP!)
```

---

## 9. Resolution Chains (Fallback Chains)

### 9.1. Drag (check_start_drag)

```
check_start_drag → resolve_drag_binding(xd, x, fb, press, fallback)

Priority:
1. xd (drag variant: Sd/Dd/Ld/Sd2) — if exists
2. x (non-drag: S/D/L/S2) — only if press_on_drag=true
3. fb (fallback: S) — only if use_fallback=true
```

### 9.2. Tap-up (handle_tap_up_impl)

```
handle_tap_up_impl → 4 paths:

PATH 1: pending_deferred_double_count > 0 → execute D
PATH 2: post_double_tap_drag → cleanup (D already held)
PATH 3: double_tap_consumed → execute S (3+ tap)
PATH 4: has_dt → enter DT_WAITING
  └─ if Dd without D → execute S + DT_WAITING (empty)
  └─ else → execute S + IDLE
```

### 9.3. Long-press (resolve_long_press_pair)

```
resolve_long_press_pair(f) → GesturePairPlan

1. L + Ld: pulse_on_up = true (L deferred until up, Ld on drag)
2. L without Ld: 
   ├─ has competition (D/Dd) → press_on_drag + pulse_on_up
   └─ no competition → hold_delay (execute on LP timer)
3. Only Ld: press_on_drag = true (Ld on drag, no L)

Competition with D: detect via f->cached_has_active_double_tap ||
f->cached_has_active_double_tap_drag
```

---

## 10. Usage Scenarios (All Combinations)

### 10.1. Standard Gaming (FPS)

| Gesture | Binding | Expectation |
|------|---------|----------|
| S | MOUSE_LEFT | Shooting |
| S2 | MOUSE_RIGHT | Aim |
| Sd | MOUSE_MOVE | Look around |
| Sd2 | MOUSE_LEFT | ADS shooting |

**Tree:**
- One finger: S → LMB on tap, Sd → drag for looking around
- Second finger: S2 → RMB, Sd2 → LMB on drag
- TP: Sd is zeroed, so drag for looking around will not work in TP

### 10.2. RTS/Strategy

| Gesture | Binding | Expectation |
|------|---------|----------|
| S | MOUSE_LEFT | Selection |
| D | MOUSE_RIGHT | Move |
| Sd | KEY_CTRL + MOUSE_LEFT | Add to selection |

**Tree:**
- S + D: S is deferred (pulse_on_up), on DT → D
- Tap → D (if double tap) or S (if single tap)
- Sd: drag with Ctrl to add to selection
- In TP: Sd unavailable → Ctrl+LMB does not work on drag

### 10.3. RPG/MMO

| Gesture | Binding | Expectation |
|------|---------|----------|
| S | MOUSE_LEFT | Interaction |
| L | MOUSE_RIGHT | Context menu |
| Sd | MOUSE_MOVE | Camera |
| D | KEY_SPACE | Jump (consecutive) |

**Tree:**
- S + L: S deferred → on LP timer L + S, on tap → S
- D: jump on double tap
- Sd: drag for camera
- Conflict: if pressed and held — L fires after 150ms

### 10.4. Second Finger as Dedicated Button

| Gesture | Binding | Expectation |
|------|---------|----------|
| S2 | MOUSE_RIGHT | Aim |
| D2 | KEY_R | Reload |
| S | MOUSE_LEFT | Shooting |

**Tree:**
- S2 → RMB immediately on second finger touch-down
- D2 → R on double tap with second finger
- S → LMB on tap
- Second finger without competition → S2 immediately

### 10.5. TP-only: Two Fingers for Scrolling

| Gesture | Binding | Expectation |
|------|---------|----------|
| S | MOUSE_LEFT | Click |
| S2 | MOUSE_RIGHT | RMB |
| Sd2 | MOUSE_SCROLL | Scroll on two-finger drag |

**Tree:**
- TP: Sd2 is available (read directly from config)
- Two fingers on TP: summed delta for drag
- Sd2 → scroll on two-finger movement
- two-finger-scroll-dist = 350px for start

---

## 11. Problematic Scenarios (Timer Races and Conflicts)

### 11.1. lp_timeout < dt_timeout

```
S + L + D (all set):
│
├─ Finger-down: S deferred (competition with L and D)
├─ After long_press_timeout (150ms):
│   ├─ L fires (state = LONG_PRESSING)
│   └─ S cancelled (deferred_tap_count = 0)
│
├─ Finger-up within double_tap_timeout (150ms):
│   ├─ state LONG_PRESSING → execute deferred D
│   └─ D orphaned: L was executed, D too
│
└─ Result: extra D action
```

**Problem:** When LP_timeout < DT_timeout, LP fires before the second tap can confirm DT. D remains pending and executes on finger-up together with LP.

### 11.2. TS single_tap_delay_ms > 0 with LP

```
S + L + single_tap_delay_ms > 0:
│
├─ Finger-down: S deferred (hold_timer)
├─ hold_timer fires: execute S
├─ LP timer fires: L already executed S → ???
│
└─ Conflict: if S fired by hold timer, LP may
    not execute (hold_timer clears deferred_tap and
    pending_double, but NOT pending_deferred_double)
```

### 11.3. D Without S

```
D only (S = 0):
│
├─ First tap → PATH 4: enter DT_WAITING (without S defer)
│   └─ No feedback on first tap!
│
├─ Second tap in window → D
├─ Second tap outside window → DT timeout → NOP (no S for fallback)
│
└─ User does not know if the first tap was registered
```

### 11.4. Sd in TP (Unavailable)

```
S + Sd in TP:
│
├─ setup_main_finger_bindings(): single_tap_drag = NULL
│
├─ Finger-down: S deferred (pulse_on_up)
├─ Drag: no Sd → start_drag_no_binding()
│   └─ Drag has no binding!
│
├─ Finger-up: S on up (pulse_on_up)
│
└─ Result: S fires on up, not on down.
    User expects S on down and Sd on drag.
    In TP: S on up, Sd does not work.
```

### 11.5. Two Fingers + LP Cancel

```
LP active (state = LONG_PRESSING), second finger arrives:
│
├─ gesture_clear_second_finger_state() is not called on LP
│  (only in gesture_tick() on LP trigger)
│
├─ Second finger: setup → cancel main LP timer
│
└─ Main finger remains in TAP_WAITING (LP reset)
   If finger moved → check_start_drag may trigger
```

---

## 12. Quick Reference Table

### 12.1. When Each Gesture Triggers

| Gesture | Triggers when | Under what conditions |
|------|------------------|--------------------|
| S (single) | finger-up (TP) or finger-down+timer (TS) | S set and no competition |
| S (deferred) | finger-up (pulse_on_up) | S set with D or L |
| S (held) | on drag (press_on_drag) | S set with L or D without Sd |
| L | on LP timer (long_press_timeout_ms) | L set, finger has not moved |
| L (deferred) | finger-up (pulse_on_up) | L + Ld |
| D | second tap in DT window | D set, two taps within window |
| D (deferred) | finger-up (PATH 1) | D set + pulse_on_up |
| Sd | on drag | Sd set, TP? no (TS only!) |
| Ld | on drag | Ld set, LP timer fired |
| Dd | on drag after DT | Dd set, DT confirmed |
| S2 | second finger finger-down | S2 set |
| D2 | second finger second tap in SDTW window | D2 set |
| Sd2 | on second finger drag | Sd2 set (TP: from config) |
| Dd2 | on drag after second finger DT | Dd2 set |

### 12.2. What Happens When a Neighboring Gesture Is Absent

| Situation | Effect |
|----------|--------|
| S exists, no Sd, D/L exist | S is deferred until finger-up (pulse_on_up) |
| S exists, no Sd, no D/L | S immediately (hold in TS, tap in TP) |
| L exists, no Ld, D exists | L is deferred until finger-up/drag |
| L exists, no Ld, no D | L on timer |
| D exists, no Dd, no S | First tap silent, DT timeout → silence |
| D exists, no Dd, S exists | S on finger-up if DT timeout |
| Sd exists, no S | Drag without S — Sd on drag |
| Nothing exists | Finger = cursor (TP LMB fallback) |

---

## 13. File Architecture (Data Flow Diagram)

```
TouchpadView.onTouchEvent()
    │
    ▼
NativeTouchProcessor.onFingerDown/Move/Up()
    │  JNI
    ▼
touch_processor_on_finger_down/move/up()    [touch_processor.c]
    │
    ├─ handle_gesture_down/move/up()         [gesture/handler.c]
    │   │
    │   ├─ element_button/dpad/stick/...()   [element/*.c]
    │   ├─ touchpad_finger_down/up()         [gesture/entry.c]
    │   │   │
    │   │   ├─ handle_ts_single_tap_hold()
    │   │   ├─ double_tap_confirm_internal()
    │   │   ├─ resolve_single_tap_pair()     → gesture_decide_branch()
    │   │   ├─ resolve_double_tap_pair()     → gesture_decide_branch()
    │   │   ├─ enter_sdtw()
    │   │   └─ confirm_second_double_tap()
    │   │
    │   └─ check_start_drag()                [gesture/base.c]
    │       │
    │       ├─ resolve_drag_binding()
    │       ├─ start_drag_with_binding()
    │       │   ├─ release_held_actions()    [gesture/executor.c]
    │       │   └─ execute_actions_hold()    [gesture/executor.c]
    │       └─ start_drag_no_binding()
    │
    ▼
touch_processor_tick()                      [touch_processor.c]
    │
    └─ gesture_tick()                        [gesture/base.c]
        │
        ├─ Long-press timer
        ├─ Single-tap hold timer
        ├─ SDTW timeout
        ├─ DT timeout
        └─ Deferred single-tap timer
            │
            └─ handle_tap_up_impl()
                ├─ PATH 1-4
                └─ execute_actions()         [gesture/executor.c]

VSync:  touch_processor → JNI → InputControlsView.syncVisualStates()
```

**Key files:**

| File | Role |
|------|------|
| `touch_processor.h` | Public API, GestureType, TouchFinger, config |
| `touch_processor.c` | Orchestration: down/move/up/tick/reset |
| `touch_processor_internal.h` | Internal state, constants, macros |
| `gesture/types.h` | `GesturePairPlan`, `GestureBranchParams`, `gesture_decide_branch()` |
| `gesture/entry.c` | `touchpad_finger_down/up()` — FSM entry |
| `gesture/handler.c` | `handle_gesture_down/move/up()` — unified handler |
| `gesture/base.c` | `gesture_tick()`, `check_start_drag()`, `handle_tap_up_impl()` |
| `gesture/executor.c` | `execute_actions()`, `hold_actions()`, `release_held_actions()` |
| `element/shared.c` | Hit-test, spatial grid, coordinate transform |
| `ControlsProfile.java` | Gesture configuration (JSON), 10 BindPackage |
| `NativeTouchProcessor.java` | JNI bridge, dispatchAllActions, NativeConfig |
| `TouchpadView.java` | Raw touch events, xform, dispatch |

---

## 14. `compute_gesture_caps()` Algorithm (Pre-computation)

```
compute_gesture_caps():
│
├─ Scan all 10 GestureType slots for ts[] and tp[]
│
├─ caps_ts_mask   = bitmask of gestures present in TS
├─ caps_tp_mask   = bitmask of gestures present in TP
├─ caps_has_gesture_bindings = (ts_mask | tp_mask) != 0
├─ caps_has_drag_bindings    = any drag variant present
├─ caps_has_double_tap       = D or Dd present
├─ caps_has_long_press       = L only (without Ld)
├─ caps_has_long_press_timer = L or Ld
├─ caps_mode_mask            = ts_mask or tp_mask depending on mode
├─ caps_second_mask          = all 4 second-finger types
├─ is_ts                     = (touch_mode == TOUCH_MODE_TOUCHSCREEN)
├─ is_tp                     = (touch_mode == TOUCH_MODE_TOUCHPAD)
└─ cached_mode_bindings      = pre-computed pointers+counters
```

This algorithm is called on initialization and on config update. Results are cached for fast access.

---

## 15. Action Execution (executor.c)

```
execute_actions_impl(result, actions, count, force_hold):
│
├─ Modifiers (Ctrl, Shift, Alt) are pressed FIRST
│
├─ Main bindings:
│   ├─ toggle → switch (on/off)
│   ├─ sticky (force_hold=true) → press + hold in gesture_held_actions[]
│   └─ tap (force_hold=false) → press + release
│
├─ binding_delay_ms delay between presses
│
└─ Modifiers are released LAST, in reverse order

release_held_actions(result):
├─ If passthrough_active → clear state without generating release
└─ Normal: release non-modifiers first, then modifiers (reverse)
```

---

## Appendix A: State Diagram

```
                    ┌─────────────────────────────────────┐
                    │              IDLE                    │
                    └──────────┬──────────────────────────┘
                               │ finger-down
                               ▼
                    ┌─────────────────────────────────────┐
                    │          TAP_WAITING                  │
                    │  (waiting for event development)      │
                    └──┬──────────┬─────────────┬──────────┘
                       │          │             │
          LP timer     │  drag    │  tap-up     │
          fired        │  threshold│  (1st time) │
                       ▼          ▼             ▼
              ┌────────────┐ ┌──────────┐ ┌──────────────────┐
              │LONG_PRESSING│ │DRAGGING  │ │DOUBLE_TAP_WAITING│
              │(L executed) │ │(Sd/Ld/Dd)│ │(waiting 2nd tap) │
              └──────┬─────┘ └────┬─────┘ └────────┬─────────┘
                     │            │                 │
           drag      │  finger-up │      2nd tap    │  DT timeout
                     ▼            ▼       in window ▼
              ┌──────────┐  ┌──────────┐  ┌──────────────────┐
              │DRAGGING  │  │  IDLE    │  │CONFIRM DT → IDLE │
              │(Ld drag) │  │(release) │  │(D/Dd executed)   │
              └──────────┘  └──────────┘  └──────────────────┘
```

## Appendix B: Glossary

| Term | Description |
|--------|----------|
| S | Single Tap |
| L | Long Press |
| D | Double Tap |
| Sd | Single Tap Drag |
| Ld | Long Press Drag |
| Dd | Double Tap Drag |
| S2 | Single Tap with 2nd finger |
| D2 | Double Tap with 2nd finger |
| Sd2 | Single Tap Drag with 2nd finger |
| Dd2 | Double Tap Drag with 2nd finger |
| TS | Touchscreen mode — absolute mode |
| TP | Touchpad mode — relative mode |
| DT_WAITING | State of waiting for the second tap |
| SDTW | Second-finger Double-Tap Waiting |
| LMB | Left Mouse Button |
| RMB | Right Mouse Button |
| NOP | No Operation — nothing happens |
