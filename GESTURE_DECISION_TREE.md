# Итоговое дерево решений распознавания жестов (TS/TP)

> Синтез 5 вариантов анализа, верифицирован по исходному коду `base.c`, `entry.c`, `handler.c`, `types.h`, `touch_processor.h`, `touch_processor_internal.h`.

---

## 1. ПРЕДВЫЧИСЛЕННЫЕ ФЛАГИ (`compute_gesture_caps`)

```
┌─ caps_has_gesture_bindings  = any ts/tp binding count > 0
├─ caps_has_drag_bindings     = any Sd/Ld/Dd/Sd2/Dd2 > 0
├─ caps_has_double_tap        = any D/Dd/D2/Dd2 > 0
├─ caps_has_long_press        = any ts/tp L > 0
└─ caps_has_long_press_timer  = caps_has_long_press || Ld > 0
```

Per-finger кешированные флаги (`touch_finger_cache_bs`):
```
cached_has_active_single_tap        = fb->single_tap_count > 0
cached_has_active_double_tap        = fb->double_tap_count > 0
cached_has_active_long_press        = fb->long_press_count > 0
cached_has_active_single_tap_drag   = fb->single_tap_drag_count > 0
cached_has_active_long_press_drag   = fb->long_press_drag_count > 0
cached_has_active_double_tap_drag   = fb->double_tap_drag_count > 0
cached_can_hold_long_press          = L && type not in {SCROLL_UP, SCROLL_DOWN, MOVE_*}
cached_has_long_press_timer         = L || Ld
```

---

## 2. ТОЧКА ВХОДА: `handle_gesture_down()` (handler.c:10)

```
TOUCH_EVENT_FINGER_DOWN(ptr_id, x, y, time_ms)
│
├─ [Шаг 1] LOCK-элементы в точке? → handle_element_down, return
├─ [Шаг 2] TRACK/HOVER-элементы? → handle_element_down, если не passthrough → return
│
├─ Определить тип пальца:
│   is_first   = (gesture_main_ptr_id < 0)
│   is_second  = !is_first && (ptr_id != gesture_main_ptr_id)
│
├─ TS + MAIN (handler.c:80-192):
│   │
│   ├─ gesture_deferred_second_finger_trap → false (сброс)
│   │
│   ├─ [3a] gesture_double_tap_waiting?
│   │   ├─ within_tap_distance → DT CONFIRMED:
│   │   │   ├─ restore pending_double из pending_deferred_double (backup)
│   │   │   ├─ D-only (Dd=0): hold_actions(D)
│   │   │   ├─ D+Dd: pending_deferred_double = D (exec на up)
│   │   │   ├─ Dd-only: nothing (zombie DT)
│   │   │   ├─ post_double_tap_drag = true
│   │   │   ├─ state = TAP_WAITING
│   │   │   ├─ pointer switch по original_ptr_id → возможно
│   │   │   ├─ was_second_deferred? → restore second-finger bindings
│   │   │   └─ return
│   │   └─ !within_tap_distance → DT CANCELLED:
│   │       └─ gesture_cancel_double_tap_wait → exec deferred_tap (S)
│   │
│   ├─ [4] was_second_deferred без DT → restore second-finger bindings
│   │
│   ├─ [5-6] original_ptr_id + ACT_POINTER_MOVE(x,y)
│   │
│   └─ [7] touchpad_finger_down(f, ...) → вход в state machine
│       (см. секцию 3)
│
├─ TS + SECOND (handler.c:197-266):
│   │
│   ├─ [2] gesture_double_tap_waiting?
│   │   ├─ within_tap_distance → DT CONFIRMED:
│   │   │   ├─ (та же inline-логика)
│   │   │   ├─ state=TAP_WAITING (main), IDLE (second)
│   │   │   └─ return
│   │   └─ !within_tap_distance → gesture_cancel_double_tap_wait
│   │
│   ├─ save main held → pending_resume_action
│   ├─ release_held_actions()
│   └─ touchpad_finger_down(f, ...) → вход в state machine
│       (см. секцию 3)
│
└─ TP (handler.c:271-303):
    ├─ reset scroll state (first finger)
    ├─ simTouchScreen init
    └─ touchpad_finger_down(f, ...) → вход в state machine
```

---

## 3. STATE MACHINE: `touchpad_finger_down()` (entry.c:86)

```
touchpad_finger_down(f, result, time_ms)
│
├─ FAST PATH: !caps_has_gesture_bindings && !gesture_double_tap_waiting?
│   └─ gesture_handler_active=false, main_ptr_id=f->ptr_id, state=IDLE, return
│
├─ MAIN FINGER (gesture_main_ptr_id < 0):
│   │
│   ├─ is_second_finger = false
│   ├─ gesture_main_ptr_id = f->ptr_id
│   ├─ original_ptr_id = f->ptr_id
│   │
│   ├─ gesture_double_tap_waiting?
│   │   ├─ within_tap_distance → double_tap_confirm_internal():
│   │   │   ├─ restore pending_double из pending_deferred_double (backup)
│   │   │   ├─ D-only: hold_actions(D)
│   │   │   ├─ D+Dd:   pending_deferred_double = D
│   │   │   └─ Dd-only: nothing (zombie)
│   │   │   post_double_tap_drag = true
│   │   │   state = TAP_WAITING
│   │   │   return
│   │   └─ !within_tap_distance → gesture_cancel_double_tap_wait
│   │
│   ├─ gesture_handler_active = has_gesture
│   ├─ state = TAP_WAITING
│   │
│   └─ [TS только] handle_ts_single_tap_hold():
│       ├─ skip если: is_second_finger || action_held || !S || Sd>0
│       ├─ has DT (D || Dd)?
│       │   ├─ Да: deferred_tap=S, pending_double=D, hold_delay=double_tap_timeout
│       │   └─ Нет:
│       │       ├─ single_tap_delay>0? → hold_delay=single_tap_delay
│       │       └─ иначе → hold_actions(S) IMMEDIATELY
│
├─ SECOND FINGER (ptr_id != main):
│   │
│   ├─ is_second_finger = true
│   ├─ gesture_second_active = true
│   ├─ gesture_second_ptr_id = f->ptr_id
│   │
│   ├─ [TP] Anchor main finger drag threshold
│   ├─ Cancel main finger LP timer
│   │
│   ├─ Copy second-finger bindings из cfg (ts_2nd/tp_2nd)
│   ├─ touch_finger_cache_bs(f)
│   │
│   ├─ second_double_tap_waiting?
│   │   ├─ Dd2>0 → pending_second_double=D2, post_dt_drag=true
│   │   ├─ D2>0, Dd2=0 → hold_actions(D2)
│   │   ├─ state=TAP_WAITING, return
│   │
│   ├─ gesture_double_tap_waiting? (глобальный DT_WAITING)
│   │   ├─ within_tap_distance → double_tap_confirm_internal():
│   │   │   main→TAP_WAITING, second→IDLE (поглощён)
│   │   │   post_dt_drag=true, return
│   │   └─ !within_tap_distance → gesture_cancel_double_tap_wait
│   │
│   ├─ release_held_actions() (НЕ для DT confirm — ранний return выше)
│   ├─ second_tap_fallback = S2 (для SDTW timeout)
│   ├─ state = TAP_WAITING
│   └─ [TS] handle_ts_single_tap_hold() — Bug 4: second finger тоже входит
```

---

## 4. ДВИЖЕНИЕ: `handle_gesture_move()` (handler.c:306)

```
TOUCH_EVENT_FINGER_MOVE(ptr_id, x, y, time_ms)
│
├─ Engaged/hovered/tracked элементы? → handle_element_move
│  non-passthrough? → return
│
├─ [TS Main + оптимизация] !second_active && !Sd && !Ld && S && !post_dt && state!=LP?
│   └─ ACT_POINTER_MOVE(x,y), return (cursor-only)
│
├─ [Main finger] state >= TAP_WAITING?
│   └─ state != DRAGGING → check_start_drag(f, dx, dy, result)
│       (см. секцию 5)
│
├─ [TS] ACT_POINTER_MOVE(x,y) — абсолютная позиция
│
├─ [TP + 2 пальца + !second_active + simTouchScreen] — two-finger scroll
│   ├─ distance < 350 → scroll (wheel up/down)
│   ├─ distance >= 350 && little travel → LMB press (дальние пальцы)
│   └─ scrolling → return
│
└─ [TP + !scrolling] — относительный курсор (delta + accel)
```

---

## 5. DRAG RESOLUTION: `check_start_drag()` (base.c:63)

```
check_start_drag(f, dx, dy, result)
│
├─ Guard: state != TAP_WAITING && state != LONG_PRESSING → return
├─ Guard: |dx| <= threshold && |dy| <= threshold → return
│
├─ Gesture_handler_active = false
│
├─ [STATE = LONG_PRESSING]:
│   │  chain: Ld → L → null
│   ├─ Ld>0 → hold(Ld)
│   ├─ L>0 → hold(L)
│   └─ иначе → return
│
├─ [post_double_tap_drag = true]:
│   │  chain: Dd → D → S → null (использует cfg-level bindings!)
│   ├─ use_second:
│   │   ├─ Dd2>0 → hold(Dd2)
│   │   ├─ D2>0 → hold(D2)
│   │   ├─ S2>0 → hold(S2)
│   │   └─ иначе → return
│   └─ !use_second:
│       ├─ Dd>0 → hold(Dd)
│       ├─ D>0 → hold(D)
│       ├─ S>0 → hold(S)
│       └─ иначе → return
│   post_double_tap_drag = false
│
├─ [TS + TAP_WAITING]:
│   │  main: Sd only (NO fallback)
│   ├─ second: Sd2 only (Bug: Dd2 not checked for TS)
│   └─ caps_has_drag_bindings = false → return
│
├─ [TP + second + TAP_WAITING]:
│   │  chain: Sd2 → Dd2 → null (TP-only fallback)
│   ├─ Sd2>0 → hold(Sd2)
│   ├─ Dd2>0 → hold(Dd2)
│   └─ caps_has_drag_bindings = false → return
│
├─ [TP + main + TAP_WAITING]:
│   └─ return (Sd недостижим на TP main — design limitation)
│
├─ same_as_held? skip release+re-hold
├─ release_held_actions() + hold_actions(drag)
├─ clear pending_double + deferred_tap (НЕ pending_deferred_double — Bug 3 fix)
├─ on_drag_start(f) → state = DRAGGING
└─ f->state = DRAGGING
```

---

## 6. ОТРЫВ: `touchpad_finger_up()` (entry.c:276)

```
TOUCH_EVENT_FINGER_UP(ptr_id, x, y, time_ms)
│
├─ SECOND FINGER UP:
│   ├─ !gesture_second_active? → return
│   ├─ action_held? → clear SDTW
│   ├─ has D2/Dd2? → second_double_tap_waiting = true
│   ├─ else S2? → execute_actions(S2) (fire-and-forget)
│   ├─ pending_second_double? → execute_actions(D2)
│   ├─ release_held_actions()
│   ├─ TP: release mouse buttons (LMB + RMB)
│   ├─ gesture_second_active = false
│   └─ state = IDLE (если !SDTW && !DRAGGING)
│
├─ MAIN FINGER UP — switch(state):
│   │
│   ├─ TAP_WAITING:
│   │   ├─ TP: validate tap (travel < 10px && time < 200ms)
│   │   │   └─ invalid tap → consumed? hold/exec S; release; cleanup
│   │   └─ handle_tap_up(f, result, time_ms) (см. секцию 7)
│   │   release_held_actions()
│   │
│   ├─ LONG_PRESSING:
│   │   ├─ pending_deferred_double? → execute_actions(D)
│   │   ├─ !held && L && can_hold? → execute_actions(L)
│   │   └─ release_held_actions()
│   │
│   ├─ DRAGGING:
│   │   ├─ pending_deferred_double? → execute_actions(D)
│   │   └─ release_held_actions()
│   │
│   ├─ DOUBLE_TAP_WAITING:
│   │   ├─ !Sd? → hold_actions(S) + release (immediate fire)
│   │   ├─ Sd? → execute_actions(S)
│   │   └─ gesture_double_tap_waiting = false
│   │
│   └─ IDLE/default:
│       ├─ post_double_tap_drag? → release_held
│       └─ cleanup_main_finger()
│
├─ [TS] Main finger up handler.c:604-617:
│   ├─ gesture_deferred_second_finger_tap = gesture_second_active (save)
│   ├─ gesture_second_active = false (ПЕРЕД touchpad_finger_up!)
│   └─ touchpad_finger_up(f, ...)
│
├─ [TS] Second finger up + second_active handler.c:640-663:
│   ├─ touchpad_finger_up(f, ...)
│   └─ restore main pending_resume_action → hold, state=DRAGGING
│
├─ [TS] original_id_set + !main handler.c:622-639:
│   └─ release_held, cleanup main
│
└─ [TP] Second finger up handler.c:664-704:
    ├─ save main held actions
    ├─ touchpad_finger_up(f, ...)
    └─ restore main held → continue drag
```

---

## 7. TAP UP: `handle_tap_up()` (base.c:347)

```
handle_tap_up(f, result, time_ms)
│
├─ EARLY EXIT: !caps_has_gesture_bindings && !DT_waiting && !post_dt → IDLE
├─ nanosleep(single_tap_delay_ms)
├─ Если second_active: использовать cfg-level S2/D2/Sd2/Dd2
│
├─ [PATH 1] pending_deferred_double_count > 0?
│   │  (D сохранён DT confirm'ом с Dd)
│   ├─ has Dd? → execute_actions(D) (fire-and-forget)
│   └─ !Dd → hold_actions(D) (press-hold)
│   post_dt_drag = false
│   double_tap_consumed = true
│   state = IDLE
│   return
│
├─ [PATH 2] post_double_tap_drag?
│   │  (DT подтверждён, D уже обработан)
│   → cleanup: deferred=0, pending_deferred=0, post_dt=false
│   → double_tap_consumed = true
│   → state = IDLE, return
│
├─ [PATH 3] double_tap_consumed?
│   │  (третий+ тап после завершённого DT)
│   ├─ !Sd? → hold_actions(S)
│   └─ Sd? → execute_actions(S)
│   state = IDLE, return
│
└─ [PATH 4] Нормальный первый tap-up:
    │
    ├─ has_dt (cached_has_active_double_tap)?
    │   ├─ deferred_tap = S (для DT_TIMEOUT)
    │   ├─ pending_double = D (для DT_CONFIRM)
    │   ├─ pending_deferred_double = D (backup, переживает finger-down)
    │   ├─ gesture_double_tap_waiting = true
    │   ├─ double_tap_start_time = time_ms
    │   ├─ last_tap_up = tap_up_x/y
    │   └─ state = DOUBLE_TAP_WAITING
    │
    ├─ !has_dt && Dd>0 (zombie DT)?
    │   ├─ deferred_tap = 0 (Bug 6: очищен от stale S)
    │   ├─ last_tap_up = tap_up_x/y (location only)
    │   ├─ gesture_double_tap_waiting = true
    │   └─ state = DOUBLE_TAP_WAITING
    │
    └─ Нет DT биндов?
        ├─ !Sd? → hold_actions(S)
        ├─ Sd? → execute_actions(S)
        └─ state = IDLE
```

---

## 8. ТАЙМЕРЫ: `gesture_tick()` (base.c:217)

```
gesture_tick(time_ms)
│
├─ !caps_has_gesture_bindings? → return
│
├─ PER-FINGER (state == TAP_WAITING):
│   │
│   ├─ [LP TIMER] cached_has_long_press_timer?
│   │   ├─ CANCEL check:
│   │   │   ├─ TS main: cancel = !Sd && !Ld && moved > threshold
│   │   │   ├─ TP:      cancel = !Ld && moved > threshold
│   │   │   │           (примечание: нет guard !Sd — Bug 11)
│   │   │   └─ cancel_lp → таймер сбрасывается, остаёмся в TAP_WAITING
│   │   │
│   │   └─ timeout (LP сработал):
│   │       ├─ clear second_tap_fallback + pending_second_double
│   │       ├─ can_hold_long_press?
│   │       │   ├─ Да: hold_actions(L), state=LONG_PRESSING
│   │       │   └─ Нет (scroll/move):
│   │       │       ├─ execute_actions(L) (fire-and-forget)
│   │       │       ├─ [Bug 10] S && !held → execute_actions(S)
│   │       │       └─ clear cached_has_long_press_timer (не повторять)
│   │       ├─ haptic feedback
│   │       └─ state = LONG_PRESSING
│   │
│   └─ [S HOLD TIMER] TS single_tap_hold_delay > 0 && timeout:
│       └─ hold_actions(S)
│          clear: deferred_tap=0, pending_double=0
│          [R1] pending_deferred_double = 0 (Bug: D теряется при долгом удержании)
│
├─ !caps_has_double_tap? → clear SDTW + DT_WAITING + pending_deferred_double [Bug 8]
│
├─ [SDTW TIMEOUT] second_double_tap_waiting && timeout:
│   ├─ execute_actions(second_tap_fallback) → S2
│   ├─ clear SDTW
│   └─ main finger state = IDLE (если не DRAGGING) [Bug: side-effect]
│
└─ [DT TIMEOUT] gesture_double_tap_waiting && timeout:
    ├─ deferred_tap > 0? → execute_actions(S) [нормальный путь]
    ├─ else pending_deferred_double > 0? → execute_actions(D) [Bug 1 fix]
    ├─ clear DT_WAITING + pending_deferred_double
    └─ main finger state = IDLE
```

---

## 9. ЦЕПОЧКИ FALLBACK (резольвинг биндов)

| Контекст | TS Main | TS Second | TP Main | TP Second |
|----------|---------|-----------|---------|-----------|
| TAP_WAITING drag | Sd → null | Sd2 → null **(Dd2 ignored)** | **NO DRAG** | Sd2 → Dd2 → null |
| LONG_PRESSING drag | Ld → L → null | — (L2=0) | Ld → L → null | — |
| Post-DT drag | Dd → D → S → null | Dd2 → D2 → S2 → null | Dd → D → S → null | Dd2 → D2 → S2 → null |
| DT confirm D-only | hold(D) | hold(D2) | hold(D) | hold(D2) |
| DT confirm D+Dd | D→pending_deferred | D2→pending_second | D→pending_deferred | D2→pending_second |
| DT confirm Dd-only | nothing (zombie) | nothing (zombie) | nothing (zombie) | nothing (zombie) |

---

## 10. МАТРИЦА ДОСТИЖИМОСТИ БИНДОВ

| Бинд | TS Main | TP Main | TS Second | TP Second | Условия срабатывания |
|------|---------|---------|-----------|-----------|---------------------|
| **S** | ✅ всегда | ✅ если tap valid | ❌ | ❌ | Tap-up (Path 3/4) или DT_TIMEOUT (deferred_tap) |
| **L** | ✅ | ✅ | ❌ | ❌ | LP_TIMEOUT (cancel если move && !Ld && TS:!Sd) |
| **D** | ✅ | ✅ | ❌ | ❌ | DT_CONFIRM (hold/exec) или DT_TIMEOUT (Bug 1: D без S) |
| **Sd** | ✅ drag | ❌ **НИКОГДА** | ❌ | ❌ | Драг до LP в TAP_WAITING |
| **Ld** | ✅ drag | ✅ drag | ❌ | ❌ | Драг после LP_TIMEOUT (state=LONG_PRESSING) |
| **Dd** | ✅ drag | ✅ drag | ❌ | ❌ | Драг после DT_CONFIRM (post_dt_drag) |
| **S2** | ❌ | ❌ | ✅ | ✅ | Tap-up (fallback) или SDTW_TIMEOUT |
| **D2** | ❌ | ❌ | ✅ | ✅ | SDTW_CONFIRM (second finger повторный down) |
| **Sd2** | ❌ | ❌ | ✅ | ✅ | Драг до SDTW в TAP_WAITING |
| **Dd2** | ❌ | ❌ | ⚠️ только post-DT | ✅ TAP_WAITING или post-DT | TP: fallback Sd2→Dd2; TS: ТОЛЬКО post-DT |

---

## 11. НАЙДЕННЫЕ ПРОБЛЕМЫ

| # | Файл:строка | Серьёзность | Описание |
|---|-------------|-------------|----------|
| **R1** | `base.c:287` | **MEDIUM** | `pending_deferred_double_count` очищается S hold timer'ом → D теряется, если юзер держит палец > double_tap_timeout после DT confirm |
| **R2** | `handler.c:239` | LOW | TS second DT confirm: ранний return без `touch_finger_cache_bs` → stale cached_* |
| **R3** | `handler.c:137` | LOW | TS pointer-switch early return без `touchpad_finger_down` → stale cached_* |
| **R4** | `base.c:182-184` | INFO | TP main finger Sd — принципиально НЕ достижим (дизайн из Java) |
| **R5** | `base.c:161-166` | LOW | TS second finger TAP_WAITING drag не включает Dd2 fallback — только Sd2 |
| **R6** | `base.c:308-316` | LOW | SDTW timeout сбрасывает main finger в IDLE (side-effect) |
| **R7** | `entry.c:272` | INFO | TS second finger вызывает handle_ts_single_tap_hold — не влияет (guard is_second_finger есть) |

---

## 12. КЛЮЧЕВЫЕ ДИЗАЙН-РЕШЕНИЯ

1. **Dual path для DT confirm**: дублирован в handler.c (TS main/second inline) и entry.c (double_tap_confirm_internal). handler.c путь использует config-level `ts_double_tap_drag_count`, entry.c — тот же. Несоответствия нет, но дублирование кода.

2. **pending_deferred_double как backup**: переживает `on_drag_start()` (не чистится — Bug 3 fix) и finger-down (восстанавливается в `double_tap_confirm_internal`). Единственное место где теряется — S hold timer (R1).

3. **TS vs TP различия минимальны**: Только cursor (абсолютный vs относительный), pre-hold S (TS-only), LP cancellation guard (`!Sd` только в TS), и TAP_WAITING drag для main (TP никогда не драг).

4. **Second finger не имеет L/Ld**: bindings копируются только S2/D2/Sd2/Dd2.

5. **SDTW vs DT_WAITING**: SDTW (second_double_tap_waiting) — отдельный механизм для второго пальца, не путать с глобальным gesture_double_tap_waiting.
