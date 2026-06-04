# Полный анализ жестового движка Winlator (Touchscreen/Touchpad)

## Структура биндов (6 основных жестов первого пальца + 4 второго)

**Первый палец:** S (single_tap), D (double_tap), Dd (double_tap_drag), L (long_press), Ld (long_press_drag), Sd (single_tap_drag)
**Второй палец:** S2, D2, Sd2, Dd2

---

## ЧАСТЬ 1: ПОЛНЫЙ АНАЛИЗ ВСЕХ КОМБИНАЦИЙ S/D/Dd

### Таблица 1: 8 комбинаций S/D/Dd

| # | S | D | Dd | Single Tap | Double Tap | DT+Drag | DT Timeout | 2F DT Confirm | Баги |
|---|----|----|----|------------|------------|---------|------------|--------------|------|
| 1 | + | - | - | **S** (hold) | **S,S** (2x S) | N/A | **S** | **S** | Нет |
| 2 | - | + | - | **∅** (ничего) | **D** (tap) | **D** (hold) | **∅** | **D** | **БАГ1: ∅ на single tap** |
| 3 | - | - | + | **∅** (zombie DT) | **∅** | **Dd** (drag) | **∅** | **∅** | Дизайн (корректно) |
| 4 | + | + | - | defer S → **S** | **D** (hold) | **D** (hold) | **S** | **D** | Нет |
| 5 | + | - | + | **S** (+zombie DT) | **S** | **S + Dd** | **S,S** (дважды!) | **S** | **БАГ2: S двойной срабат. (TS)** |
| 6 | - | + | + | **∅** | **D** | **D + Dd** | **∅** | **D** | **БАГ1: ∅ на single tap** |
| 7 | + | + | + | defer S → **S** | **D** | **D + Dd** | **S** | **D** | Нет |
| 8 | - | - | - | **∅** (TP: fallback LMB) | **∅** | **∅** | **∅** | **∅** | **БАГ3: LMB press без release** |

∅ = ничего не срабатывает

### Анализ оптимизаций S/D/Dd:

**caps_has_double_tap** включает Dd (zombie DT tracking) — корректно:
- combos #3, #5, #6, #7 → `caps_has_double_tap=true` (из-за Dd)
- combo #4 → `caps_has_double_tap=true` (из-за D)
- combos #1, #2, #8 → `caps_has_double_tap=false`

**handle_tap_up** правильно различает D (`active_has_double_tap`) и Dd (`active_has_double_tap_drag`):
- Path 4 (has_dt=true) → DT setup когда есть D
- Path 4 else (zombie DT) → только когда нет D, но есть Dd

**handle_ts_single_tap_hold** guard: `has_dt = has_active_double_tap || has_active_double_tap_drag` (включает Dd) — корректно для defer S.

---

## ЧАСТЬ 2: ПОЛНЫЙ АНАЛИЗ ВСЕХ КОМБИНАЦИЙ S/L/Ld

### Таблица 2: 8 комбинаций S/L/Ld

| # | S | L | Ld | Quick Tap | Long Press | LP + Drag | Tap + Drag | Баги |
|---|----|----|----|-----------|------------|-----------|------------|------|
| 1 | - | - | - | ∅ | ∅ | ∅ | ∅ | Нет |
| 2 | + | - | - | **S** (tap) | →S на up | →S на up (TS: курсор) | TS: S, TP: ∅ | Нет |
| 3 | - | + | - | ∅ | **L** (hold) | **L** (drag) | ∅ (LP cancelled) | Нет |
| 4 | - | - | + | ∅ | ∅ (LP timer → LONG_PRESSING, нет L) | **Ld** (drag) | TS: Ld, **TP: ∅** | **БАГ4:** |
| 5 | + | + | - | **S** (tap) | S↑ L↓ | L drag | S на up (TS курсор) | **БАГ5:** S held через LP |
| 6 | + | - | + | **S** (tap) | →S up (hold timer) | **Ld** (drag, S↑) | TS: Ld, **TP: ∅** | **БАГ4:** |
| 7 | - | + | + | ∅ | **L** (hold) | L↑ Ld↓ drag | TS: Ld, **TP: ∅** | **БАГ4:** |
| 8 | + | + | + | **S** (tap) | S↑ L↓ | S↑ L↑ Ld↓ drag | TS: Ld, **TP: ∅** | **БАГ4,5** |

### Анализ LP cancellation (gesture_tick):

**TS** (base.c:231-237): правильно guard — `cancel_lp = !Sd && !Ld && movement > drag_threshold`
- Ld/Sd присутствует → LP НЕ отменяется → gesture может перейти в LONG_PRESSING → check_start_drag запускает Ld/Sd
- Ld/Sd отсутствует → LP отменяется → gesture остаётся в TAP_WAITING → S срабатывает на up

**TP** (base.c:238-242): **всегда** отменяет LP при movement, игнорируя Ld! → **БАГ4**

---

## ЧАСТЬ 3: АНАЛИЗ DRAG (Sd) + ПУСТЫЕ БИНДЫ

### Таблица 3: Комбинации с Sd

| # | S | Sd | D | Dd | Tap | Drag (TS) | Drag (TP) | DT+Drag |
|---|----|----|----|----|-----|-----------|-----------|---------|
| 1 | + | - | - | - | S | ∅ (курсор TS) | ∅ | N/A |
| 2 | + | + | - | - | S (execute) | **Sd** | **∅** (TP no-drag) | N/A |
| 3 | + | + | + | - | S deferred→S | Sd (TAP_WAIT) | ∅ | D held |
| 4 | + | + | - | + | S + zombie DT | Sd (TAP_WAIT) | ∅ | Dd drag |
| 5 | + | + | + | + | S deferred→S | Sd (TAP_WAIT) | ∅ | D + Dd |
| 6 | - | + | - | - | ∅ | **Sd** (drag) | ∅ | N/A |
| 7 | - | - | - | - | ∅ (TP: fallback) | ∅ | ∅ | ∅ |

### Анализ fallback tap (пустые бинды):

**TP fallback** (handler.c:577-598 / touchpad.c:386-410):
- `gesture_handler_active=false` → `is_tap` → `add_action(ACT_POINTER_BUTTON_PRESS, 0)`
- **БАГ: нет ACT_POINTER_BUTTON_RELEASE** → левая кнопка мыши залипает

**TS fallback**: нет fallback tap → finger-up генерирует ноль действий (только move на down/move)

---

## ЧАСТЬ 4: АНАЛИЗ ВТОРОГО ПАЛЬЦА (S2/D2/Sd2/Dd2)

Второй палец имеет:
- **long_press_count всегда = 0** (нет LP для второго пальца)
- Свои binding chains: Sd2 → Dd2 → null (TP drag), Dd2 → D2 → S2 → null (TS/TP post-DT)
- `second_double_tap_waiting` — отдельный DT флаг для второго пальца

### 16 комбинаций S2/D2/Sd2/Dd2 (сводка):

| S2 | D2 | Sd2 | Dd2 | 2F Tap | 2F Double | 2F Drag | 2F DT Drag |
|----|----|-----|-----|--------|-----------|---------|------------|
| + | - | - | - | **S2** | **S2,S2** (2x) | ∅ | ∅ |
| - | + | - | - | ∅ | **D2** | ∅ | ∅ |
| - | - | + | - | ∅ | ∅ | **Sd2** (TS/TP) | ∅ |
| - | - | - | + | ∅ | ∅ | **Dd2** (TP) | **Dd2** (TS) |
| + | + | + | + | S2 deferred | D2 | Sd2 | D2 + Dd2 |
| ... | ... | ... | ... | ... | ... | ... | ... |

Ключевое: `pending_second_double` + `second_tap_fallback` механизм работает корректно.
`pending_resume_action` восстанавливает held actions главного пальца после up второго.

---

## СВОДКА ВСЕХ НАЙДЕННЫХ БАГОВ

### 🛑 БАГ1 (Критический): D-only single tap — ничего не срабатывает
**Где:** `base.c:416-431` (handle_tap_up Path 4)
**Что:** Когда есть D (double_tap) но нет S (single_tap):
- `deferred_tap = active_single` → пусто (count=0)
- `pending_double = D` → сохранено
- DT timeout: `deferred_tap_count == 0` → ничего не выполняется
- D тихо теряется. Single tap генерирует ноль действий.

### 🛑 БАГ2 (Критический): S+Dd (TS) — S срабатывает дважды
**Где:** `entry.c:45-67` (handle_ts_single_tap_hold) + `base.c:442-449` (zombie DT)
**Что:** TS S+Dd:
1. finger-down: `handle_ts_single_tap_hold` → `deferred_tap = S`
2. finger-up: `handle_tap_up` → zombie DT → **S fires** но `deferred_tap_count` НЕ очищен
3. DT timeout: `deferred_tap_count > 0` → `execute_actions(S)` → **S fires AGAIN**

### 🛑 БАГ3 (Средний): TP fallback LMB PRESS без RELEASE
**Где:** `handler.c:586`, `touchpad.c:397`
**Что:** Пустые бинды + TP mode → `add_action(ACT_POINTER_BUTTON_PRESS, 0)` но нет `ACT_POINTER_BUTTON_RELEASE`.

### 🛑 БАГ4 (Критический, TP): LP cancellation игнорирует Ld присутствие
**Где:** `base.c:238-243` (gesture_tick)
**Что:** TP `cancel_lp` не имеет guard `!Sd && !Ld`:
```c
// TP (base.c:238-243) — БАГ:
cancel_lp = movement > drag_threshold;  // Ld/Sd игнорируются!
// TS (base.c:233-237) — ПРАВИЛЬНО:
cancel_lp = !Sd && !Ld && movement > drag_threshold;
```
**Результат:** Любой movement до LP timeout на TP отменяет LP. Ld drag никогда не сработает на TP.

### 🛑 БАГ5 (Средний): S двойной срабат. через hold timer + DT timeout (S+D)
**Где:** `base.c:262-271` + `base.c:416-427` + `base.c:300-319`
**Что:** TS S+D:
1. Hold timer fires → S held
2. Finger-up: `handle_tap_up` re-saves S→deferred, D→pending
3. `release_held_actions` → S released
4. DT timeout: `execute_actions(deferred_tap=S)` → **S fires AGAIN**

### 🛑 БАГ6 (Средний): Stale pending_deferred_double при смене конфига
**Где:** `base.c:276-279` (gesture_tick caps_has_double_tap guard)
**Что:** `gesture_tick` не очищает `pending_deferred_double` когда `caps_has_double_tap=false`, что может привести к phantom D action при runtime смене конфига.

### ⚠️ БАГ7 (Низкий): Non-holdable L + S → S остаётся held через LP
**Где:** `base.c:253-254` (gesture_tick LP execution)
**Что:** Когда L не может быть held (scroll/mouse-move binding):
- S pre-held (через hold timer)
- LP fires → `execute_actions(L)` (НЕ `hold_actions`, так что `release_held_actions` не вызывается)
- S остаётся held до finger-up

### ⚠️ БАГ8 (Низкий): Post-LP drag stall
**Где:** `handler.c:423-428` + `base.c:245-255`
**Что:** `check_start_drag` для LONG_PRESSING только на move событиях.
Если movement случился до LP timeout, а после LP timeout движения нет — Ld drag никогда не стартует.

---

## ПРЕДЛАГАЕМАЯ ЛОГИКА АНАЛИЗА И РАСПОЗНАВАНИЯ ЖЕСТОВ

На основе анализа предлагаю следующую оптимизированную логику:

### 1. Precomputed Capability Flags (уже есть, нужны исправления)

```c
typedef struct {
    // Флаги наличия биндов
    bool has_single_tap;         // S
    bool has_double_tap;         // D
    bool has_double_tap_drag;    // Dd
    bool has_single_tap_drag;    // Sd
    bool has_long_press;         // L
    bool has_long_press_drag;    // Ld

    // Производные оптимизационные флаги
    bool can_enter_dt_waiting;   // D || Dd (zombie tracking)
    bool has_dt_action;          // D (имеет действие, не только трекинг)
    bool can_long_press;         // L || Ld (LP timer нужен)
    bool can_drag;               // Sd || Ld || Dd
    bool can_hold_long_press;    // L && not scroll/mouse-move
    bool has_any_gestures;       // любой из 6
} GestureCaps;
```

### 2. Optimized State Machine Decision Tree

```
finger_down:
  if (!caps.has_any_gestures):
    if (TP mode): schedule delayed LMB press (с RELEASE!)
    state = IDLE; return

  if (gesture_double_tap_waiting):
    if (finger within DT distance):
      CONFIRM DOUBLE TAP
      if (has_dt_action):  // D exists
        if (has_double_tap_drag): defer D
        else: hold_actions(D)
      post_double_tap_drag = true
      state = TAP_WAITING
    else:
      CANCEL DT: execute deferred S (if any)
      state = IDLE (НЕ TAP_WAITING — баг-фикс!)
    return

  if (is_second_finger):
    // см. second-finger handler
    state = TAP_WAITING
    return

  // Первый палец
  state = TAP_WAITING
  if (TS mode && has_single_tap && !has_single_tap_drag && !has_double_tap && !has_double_tap_drag):
    // S-only, немедленно hold
    hold_actions(S)
  else if (TS mode && has_single_tap && can_enter_dt_waiting):
    // S+D/Dd: defer S, setup DT timer
    deferred_tap = S
    pending_double = D (если has_dt_action)
    single_tap_hold_delay = double_tap_timeout_ms

finger_move:
  if (state == IDLE): return (or handle cursor/scrolling)
  if (state == DRAGGING): return (drag handler)

  if (state == TAP_WAITING):
    if (can_drag && movement > drag_threshold):
      if (post_double_tap_drag):
        // post-DT drag: Dd → D → S chain
      elif (state == LONG_PRESSING):  // or LP timer just fired
        // LP drag: Ld → L chain
      elif (could_start_drag):
        // Sd drag (TS only for main finger)
        start_drag(drag_binding)

  // LP timer
  if (can_long_press && time >= long_press_timeout_ms):
    cancel_lp = false
    if (TP mode || (TS && !has_single_tap_drag && !has_long_press_drag)):
      cancel_lp = movement > drag_threshold
    if (!cancel_lp):
      transition_to(LONG_PRESSING)
      if (has_long_press):
        if (can_hold_long_press): hold_actions(L)
        else: execute_actions(L)
      has_gesture_handler = false
```

### 3. Key Bug Fixes Summary

| Баг | Файл:Строка | Исправление |
|-----|------------|------------|
| **1** ∅ single tap | base.c:416-431 | В handle_tap_up Path 4: если active_single_count==0 и есть D, то при DT timeout выполнять pending_double |
| **2** S double-fire | entry.c:45-67 + base.c:442-449 | Добавить `deferred_tap_count=0` в блок zombie DT (base.c:442-449) |
| **3** LMB no release | handler.c:586 | Добавить `add_action(ACT_POINTER_BUTTON_RELEASE, 0)` или переделать в tap (press+release) |
| **4** TP LP cancel | base.c:238-243 | Добавить guard `!Sd && !Ld` в TP ветку |
| **5** S double (hold+DT) | base.c:262-271 | В hold timer: SAVE факт что S уже сработал, проверять в handle_tap_up |
| **6** Stale deferred | base.c:276-279 | Добавить очистку `pending_deferred_double_count=0` в `!caps_has_double_tap` блок |
| **7** S held through LP | base.c:253-254 | Добавить `release_held_actions` перед `execute_actions(L)`, с восстановлением если execute |
| **8** Drag stall | handler.c:423-428 | После LP timeout в LONG_PRESSING: проверить threshold немедленно, не ждать move |
