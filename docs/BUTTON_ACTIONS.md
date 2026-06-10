# Multi-action concept on buttons: Tap / Long-Press / Gesture Swipe

## Summary

This document describes the behavior of three types of actions on a single button, using **only existing mechanisms**:

- **Tap** — quick press and release
- **Long-Press (LP)** — holding the finger without moving (or with movement within the button)
- **Gesture Swipe** — swiping a finger across the button

Goal: stable execution of each action without accidentally triggering another.

---

## 1. Multi-action principle

### 1.1. Deferred primary (defer_primary)

If a button has at least one of: LP binding, gesture binding — the primary binding is not pressed on finger-down. It is deferred until it becomes clear which action is being performed:

| Condition | What happens with primary |
|-----------|---------------------------|
| No LP and no gesture | Pressed immediately on DOWN |
| Has LP or gesture | Deferred, pressed together with the winning action |

### 1.2. First-wins: LP vs Gesture

Two actions compete for a single touch. The one that triggers first wins:

```
  ├─ Movement > threshold BEFORE long_press_delay → GESTURE (LP canceled)
  ├─ LP timer fired BEFORE movement → LP (gesture blocked)
  └─ Quick tap without movement → TAP
```

**LP timer** — hold timer (usually 200-300ms). If the finger does not move beyond the threshold during this time — LP activates.

**Gesture threshold** — minimum movement distance (default 20px). As soon as the finger moves beyond this threshold — gesture activates, LP is canceled.

**Mutual exclusion:** After one action is activated, the second is blocked until finger-up.

### 1.3. Role of gesture_timer_armed (50ms timer)

The 50ms timer (`gesture_timer_armed`) **does not affect** multi-action. It exists exclusively for toggle buttons:
- Serves as a debounce guard during slide-over in TRACK/HOVER modes
- Prevents toggle re-triggering from the same movement
- For all non-toggle buttons, this timer is inactive

Gesture on non-toggle buttons triggers **only by movement** — without timer, without delay.

---

## 2. Branching: decision diagram

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
      │ Has LP or    │    │ No LP and      │
      │ gesture?     │    │ gesture        │
      │              │    │ (plain button) │
      └──────┬───────┘    └────────┬───────┘
             │                     │ press primary
             ▼                     ▼ on DOWN
    ┌────────────────┐    ┌────────────────┐
    │ ARM state      │    │ HOLD state     │
    │ primary NOT    │    │ primary held   │
    │ pressed (defer)│    └───────┬────────┘
    │ LP timer ticking│           │
    └───────┬────────┘           │
            │                    │
   ┌────────┴────────┐           │
   ▼                 ▼           │
┌──────┐       ┌────────┐       │
│MOVE >│       │ LP     │       │
│thresh│       │ timer  │       │
│20px  │       │200ms   │       │
└──┬───┘       └──┬─────┘       │
   │              │             │
   ▼              ▼             │
┌────────┐  ┌──────────┐       │
│GESTURE │  │LP_ACTIVE │       │
│swipe   │  │long press│       │
│trigger │  │triggered │       │
│LP can- │  │gesture   │       │
│celed   │  │blocked   │       │
└───┬────┘  └────┬─────┘       │
    │            │             │
    └─────┬──────┘             │
          │ finger-up          │ finger-up
          ▼                    ▼
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

---

## 3. Usage scenarios

### 3.1. Gesture-only button (LP=NONE)

```
Bindings: Primary=MOUSE_LEFT, Gesture=KEY_SPACE, LP=NONE
```

| Scenario | Result |
|----------|--------|
| Quick tap without movement | Tap: MOUSE_LEFT press+release |
| Movement > threshold | Gesture: KEY_SPACE + MOUSE_LEFT (held until up) |
| Hold without movement | **Nothing.** Without LP and without gesture-timer — the button waits for either movement or up |
| Hold, then movement | Gesture will trigger at the moment the threshold is crossed |

**Key point:** On a gesture-only button there is no LP and no 50ms gesture-timer. Holding without movement does nothing — only movement activates the gesture.

### 3.2. Gesture + LP button

```
Bindings: Primary=MOUSE_LEFT, Gesture=KEY_SPACE, LP=MOUSE_RIGHT
```

| Scenario | Result |
|----------|--------|
| **A:** Fast movement (before LP timer) | **GESTURE.** KEY_SPACE + MOUSE_LEFT. LP canceled |
| **B:** No movement (LP timer fires) | **LP.** MOUSE_RIGHT + MOUSE_LEFT. Gesture blocked |
| **C:** Slow movement (threshold crossed after LP) | **LP.** Gesture blocked — LP already won |
| **D:** Quick tap (80ms, no movement) | **TAP.** MOUSE_LEFT press+release. Neither LP nor gesture triggered |

**Rule:** whoever reaches the threshold first wins. The second is blocked until finger-up.

### 3.3. LP-only button (gesture=NONE)

```
Bindings: Primary=MOUSE_LEFT, LP=MOUSE_RIGHT
```

| Scenario | Result |
|----------|--------|
| Quick tap | Tap: MOUSE_LEFT press+release |
| Hold > LP delay | LP: MOUSE_RIGHT + MOUSE_LEFT |
| Movement inside the button | LP still triggers (only checks for leaving the element) |
| Exit the element before LP | LP canceled → tap on up |

### 3.4. Plain button (primary only)

```
Bindings: Primary=MOUSE_LEFT
```

| Scenario | Result |
|----------|--------|
| Tap | MOUSE_LEFT pressed on DOWN, released on UP |
| Hold | MOUSE_LEFT held the entire time |

No interaction with gesture/LP — they simply don't exist.

---

## 4. Impact of activation modes (LOCK / TRACK / HOVER)

### 4.1. LOCK mode

`element_button_move()` is called for all engaged elements on each move.

| Action | Result |
|--------|--------|
| Tap | ✅ |
| Long-Press | ✅ |
| Gesture Swipe | ✅ |

**LOCK — the only mode where multi-action works fully.** Tap, LP, and gesture are all available simultaneously.

### 4.2. TRACK mode

`element_button_move()` **is not called** for the first tracked button.

| Action | Result | Reason |
|--------|--------|--------|
| Tap | ✅ | UP is processed |
| Long-Press | ✅ | LP timer works in tick |
| Gesture Swipe | ❌ | Movement is not detected — `element_button_move` is blocked |

**TRACK — gesture-by-movement does not work.** Architecture limitation: movement on a button in TRACK does not call element_button_move.

### 4.3. HOVER mode

`element_button_move()` is called only for the **first tracked non-toggle button** on move. All other buttons in HOVER receive move only on switching (entry).

| Action | Result | Condition |
|--------|--------|-----------|
| Tap | ✅ | All buttons |
| Long-Press | ❌ when entering from gesture zone | `suppress_element_gestures` when `prev_idx < 0` |
| | ✅ when entering from another button | LP works if not suppressed |
| Gesture Swipe | ❌ when entering from gesture zone | `suppress_element_gestures` when `prev_idx < 0` |
| | ✅ Only the first non-toggle button | When entering from another button |

**HOVER — multi-action works for the first non-toggle button**, but **only when entering from another button, not from the gesture zone**. When entering from empty space (gesture zone) element LP/gesture are suppressed via `suppress_element_gestures`. Other buttons do not receive gesture in any case.

### 4.4. Summary table

| Capability | LOCK | TRACK | HOVER |
|------------|------|-------|-------|
| Tap | ✅ | ✅ | ✅ |
| Long-Press | ✅ | ✅ | ✅ |
| Gesture Swipe | ✅ | ❌ | ✅ (first non-toggle) |
| Slide-over toggle | ⚠️ (no cancel zone) | ✅ | ✅ |
| Full multi-action | ✅ BEST | ⚠️ (without gesture) | ✅ (except 2+ buttons) |

---

## 5. Practical conclusions

### 5.1. For stable multi-action — LOCK mode

LOCK is the only mode where tap, LP, and gesture work simultaneously without limitations:
- Tap: press+release primary on UP (via defer_primary)
- LP: hold > `long_press_delay_ms` without movement
- Gesture: movement > `gesture_threshold_px` (default 20px)

### 5.2. TRACK mode — gesture does not work

Architecture limitation: `element_button_move()` is not called for buttons in TRACK.
Solution: do not use gesture bindings on buttons in TRACK mode.

### 5.3. HOVER mode — gesture only on the first non-toggle button

Gesture swipe works, but only for the first non-toggle button in the tracked list. Gesture is unavailable for other buttons.

### 5.4. Visual feedback for LP

`visual_long_press_active` is passed from C to Java but is ignored at the rendering level. LP is visually indistinguishable from tap. To display LP (e.g., in blue via `secondaryColor`), the Java part (`ControlElement.syncVisualState()`) needs to be updated.
