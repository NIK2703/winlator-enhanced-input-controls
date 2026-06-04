# Анализ системы жестов Winlator-Ludashi (Touchscreen/Touchpad)

## 1. Архитектура

### 1.1. Типы жестов (6 основных, 12 слотов)

| Жест | 1-й палец | 2-й палец | Описание |
|------|-----------|-----------|----------|
| Single Tap | S | S2 | Касание + отпускание |
| Long Press | L | L2 (не реализован) | Удержание > long_press_timeout |
| Double Tap | D | D2 | Два быстрых касания |
| Single Tap Drag | Sd | Sd2 | Касание + перетаскивание |
| Long Press Drag | Ld | Ld2 | Удержание + перетаскивание |
| Double Tap Drag | Dd | Dd2 | Двойное касание + перетаскивание |

### 1.2. Файлы реализации

- `touch_processor.h` — конфиг, `TouchFinger`, `compute_gesture_caps()`
- `touch_processor_internal.h` — глобальное состояние, `COPY_FINGER_BINDINGS`
- `gesture/types.h` — `GestureBindingSet`, `gesture_build_binding_set()`
- `gesture/handler.c` — `handle_gesture_down/move/up()` (унифицированный обработчик)
- `gesture/entry.c` — `touchpad_finger_down/up()` (конечный автомат)
- `gesture/base.c` — `gesture_tick()`, `check_start_drag()`, `handle_tap_up()`, `on_drag_start()`
- `gesture/executor.c` — `hold_actions()`, `execute_actions()`, `release_held_actions()`
- `element/shared.c` — `touch_finger_cache_bs()`

### 1.3. Состояния конечного автомата

| Состояние | Описание |
|-----------|----------|
| `GESTURE_STATE_IDLE` | Нет активности |
| `GESTURE_STATE_TAP_WAITING` | Палец опущен, ожидание разрешения тапа/DT/LP |
| `GESTURE_STATE_DOUBLE_TAP_WAITING` | После первого тапа, ожидание второго |
| `GESTURE_STATE_LONG_PRESSING` | LP таймер сработал, действие удерживается |
| `GESTURE_STATE_DRAGGING` | Превышен порог drag_threshold, действие drag |

---

## 2. РЕЗУЛЬТАТЫ АНАЛИЗА ПО ВСЕМ КОНФИГУРАЦИЯМ

### 2.1. TOUCHSCREEN — все бинды установлены (S+L+D+Sd+Ld+Dd + все 2-го пальца)

| Сценарий | Что срабатывает | Механизм |
|----------|----------------|----------|
| Single Tap | **S** (по DT timeout) | S отложен для DT ожидания → timeout → `execute_actions(S)` |
| Long Press | **L** (удерживается), отпускается при up | LP timeout → `hold_actions(L)` |
| Double Tap | **D** (execute на 2-м tap-up) | DT подтверждение → D отложен (т.к. Dd есть) → Path1 handle_tap_up → `execute_actions(D)` |
| S-tap Drag | **Sd** (удерживается), отпускается при up | check_start_drag → hold_actions(Sd) |
| L-press Drag | **L→Ld** | LP timeout → L, drag → Ld (L отпущен) |
| D-tap Drag | **Dd** (drag) + **D** (tap при up) | DT confirm → D deferred; drag → Dd; up → execute_actions(D) + release Dd |
| Two-finger sequential tap | **D** (1-го пальца) | Второй палец приземляется как "новый первый" → DT confirm |
| Two-finger drag | Нет drag от 2-го пальца | check_start_drag только для `gesture_main_ptr_id` |
| 2nd-finger DT | **Dd2** (на 2-м up) | second_double_tap_waiting → pending_second_double → execute |

**Вердикт: КОРРЕКТНО**

---

### 2.2. TOUCHSCREEN — отсутствует один из биндов 1-го пальца

#### Config A: Нет S (single_tap_count=0)

| Сценарий | Результат | Вердикт |
|----------|-----------|---------|
| Tap | Ничего (S нет) | ✅ |
| DT | **D** срабатывает | ✅ |
| LP | **L** срабатывает | ✅ |
| Sd Drag | **Sd** | ✅ |
| Ld Drag | **Ld** | ✅ |
| Dd Drag | **Dd** | ✅ |

#### Config B: Нет L (long_press_count=0, Ld есть)

| Сценарий | Результат | Вердикт |
|----------|-----------|---------|
| LP timeout | LP таймер срабатывает (от Ld), но L пустой → **ничего**. **HAPTIC срабатывает ложно!** | ⚠️ **БАГ: ложный haptic** |

**Причина**: `base.c:256` проверяет `cached_has_long_press_timer` (true от Ld), а не наличие actual L.

#### Config C: Нет D (double_tap_count=0, Dd есть)

| Сценарий | Результат | Вердикт |
|----------|-----------|---------|
| Tap | **S** execute (немедленно), zombie DT ожидание | ✅ |
| DT (2 тапа) | 1-й: **S**. 2-й: **НИЧЕГО** | ❌ **БАГ: silent second tap** |

**Причина**: `base.c:442-448` — zombie DT_WAITING перехватывает 2-й tap как "DT confirm", но D=0 → ничего не срабатывает. Пользователь ожидает S на каждый независимый tap.

#### Config D: Нет Sd (single_tap_drag_count=0)

| Сценарий | Результат | Вердикт |
|----------|-----------|---------|
| Долгое удержание > double_tap_timeout | **S срабатывает ДВАЖДЫ**: hold_actions(timer) + execute_actions(DT timeout) | ⚠️ **БАГ: double S fire** |

**Причина**: `gesture_tick:262-271` удерживает S по таймеру. `handle_tap_up:417-426` пересохраняет S как deferred. DT timeout вызывает `execute_actions(S)` повторно.

#### Config E: Нет Ld (L есть)

| Сценарий | Результат | Вердикт |
|----------|-----------|---------|
| LP Drag | **L** как drag (fallback Ld→L) | ✅ |

#### Config F: Нет Dd (D есть)

| Сценарий | Результат | Вердикт |
|----------|-----------|---------|
| DT | **D** hold (на 2-м down), release (на 2-м up) | ✅ |
| DT Drag | **D** как drag (fallback Dd→D) | ✅ |

**Чистые конфиги без багов**: A, E, F

---

### 2.3. TOUCHSCREEN — отсутствуют бинды 2-го пальца

#### Config G (нет S2), H (нет D2), I (нет Sd2), J (нет Dd2)

| Сценарий | G | H | I | J |
|----------|---|---|---|---|
| 2F tap (sequential) | Все: D 1-го пальца (одинаково) | | | |
| 2F DT confirm | Dd2 fires | Dd2 fires | Dd2 fires | **D2 held** (нет Dd2) |
| Resume: S hold | skip (S=0) | skip (Sd2=T) | **S hold logic** (Sd2=0) | skip (Sd2=T) |
| Resume: LP | fires | fires | fires | fires |
| Resume: post-DT drag | Dd2→D2→S2 | D2=0→S2 fires | Dd2 fires | D2→S2 |
| Resume: Sd2 drag | Sd2 works | Sd2 works | **Нет Sd2 drag** | Sd2 works |

**Главный баг: Config I (нет Sd2)** — при resume нет Sd2 drag, что корректно (бинда нет). **Config J (нет Dd2)** — D2 удерживается сразу на DT confirm (вместо deferred). **Корректно.**

#### Config K: Все бинды 2-го пальца = 0

| Сценарий | Результат |
|----------|-----------|
| 2F DT | Тишина — S2=0, D2=0, Sd2=0, Dd2=0 → ни DT ожидание, ни fallback S2 не срабатывают |
| Resume | Все cached=false → нет S hold, нет LP timer, нет post-DT drag, нет Sd2 drag |

#### Config L: Только 2-й палец (1-й = 0)

Поведение 1-го пальца драматически изменено — все жесты 1-го пальца молчат.

#### Config M (S2+Dd2) / N (D2+Sd2)

Оба корректны в пределах заданных биндов.

---

### 2.4. TOUCHPAD — все конфигурации

| Конфиг | Тап | DT | LP | Drag TAP_WAITING | Drag LONG_PRESS | Drag post-DT | 2F scroll | 2F tap fallback |
|--------|-----|----|----|------------------|-----------------|--------------|-----------|-----------------|
| TP1 All | S deferred | D | L | Нет (TP main) | Ld | Dd | Блокирован | Нет (handler active) |
| TP2 No S | DT пустой | D | L | Нет | Ld | Dd | Блокирован | Нет |
| TP3 No D | **S немедленно** | **S+ничего** (2й tap) | L | Нет | Ld | Dd | Блокирован | Нет |
| TP4 No Sd | S deferred | D | L | Нет | Ld | Dd | Блокирован | Нет |
| TP5 No Dd | S deferred | **D held** | L | Нет | Ld | **D fallback** | Блокирован | Нет |
| TP6 No L | S deferred | D | **L timer→ничего→LONG_PRESSING** | Нет | **Ld** | Dd | Блокирован | Нет |
| TP7 Only S | **S hold** | Каждый tap=S | N/A | Нет | N/A | N/A | Блокирован | Нет |
| TP8 No bindings | **Left-click** | Left-click | N/A | N/A | N/A | N/A | **Работает** | **Right-click + Left-click** ⚠️ |
| TP9 Only 2nd | Ничего (1й пуст) | Ничего | N/A | N/A | N/A | N/A | **Блокирован** | **Right-click + Left-click** ⚠️ |

**БАГ TP8/TP9**: `handler.c:577-598` — когда два пальца поднимаются последовательно, первый up видит nf=2 → right-click, второй up видит nf=1 → spurious left-click. Нужен флаг `two_finger_tap_fired`.

---

## 3. ВСЕ НАЙДЕННЫЕ БАГИ

| # | Файл:строка | Описание | Конфиг | Серьезность |
|---|-------------|----------|--------|-------------|
| 1 | `base.c:256` | LP timer → haptic срабатывает когда L пустой, но Ld есть | B (нет L) | ⚠️ Minor |
| 2 | `base.c:442-448` | Второй tap DT молчит когда D=0, Dd=SET | C (нет D) | ❌ Real |
| 3 | `base.c:262-271`+`base.c:417-426` | Двойной S fire при долгом удержании (>DT timeout) | D (нет Sd) | ⚠️ Minor |
| 4 | `handler.c:577-598` | Spurious left-click после two-finger right-click | TP8, TP9 | ❌ Real |
| 5 | `entry.c:28-29` (D-only case) | `post_double_tap_drag=true` устанавливается даже для D-only (нет drag) | D-only DT | 🧹 Semantic |
| 6 | `executor.c:5-9` `base.c:331-335` | `nanosleep()` блокирует поток событий (в Java был неблокирующий Handler) | Все | 🧹 Perf regression |

---

## 4. АНАЛИЗ ОПТИМИЗАЦИЙ

| Оптимизация | Файл | Корректна? |
|-------------|------|------------|
| `touchpad_finger_down()` fast path | `entry.c:86` | ✅ — два условия короткого замыкания |
| `gesture_tick()` ранний выход | `base.c:218` | ✅ — `caps_has_gesture_bindings` покрывает все списки |
| `handle_tap_up()` ранний выход | `base.c:324` | ✅ — zombie DT проходит через Dd в caps |
| TS move cursor shortcut | `handler.c:408-421` | ✅ — все условия корректны |
| DT flag cleanup в tick | `base.c:276-279` | ✅ — `caps_has_double_tap` покрывает все DT/Dd |

---

## 5. ПРЕДЛАГАЕМАЯ ЛОГИКА АНАЛИЗА И РАСПОЗНАВАНИЯ ЖЕСТОВ

### 5.1. Оптимизированная блок-схема принятия решений

```
FINGER DOWN:
  Если caps_has_gesture_bindings == false И !gesture_double_tap_waiting:
    → IDLE, gesture_handler_active=false (выход)
  
  Если main_ptr_id < 0 (первый палец):
    Если gesture_double_tap_waiting:
      Проверить расстояние от последнего tap_up
      Если в пределах double_tap_distance → DT CONFIRMED
        Если pending_double_count > 0:
          Если has_dt_drag → deferred D (для execute на up)
          Иначе → hold_actions(D)
        post_dt_drag = true
      Иначе → CANCEL DT
  
    state = TAP_WAITING
    Если TS режим → handle_ts_single_tap_hold()

  Иначе если второй палец:
    Отменить LP таймер первого пальца
    Скопировать бинды 2-го пальца
    Если second_double_tap_waiting → подтвердить 2F DT
    Если gesture_double_tap_waiting → подтвердить DT
    release_held_actions()
    Сохранить S2 как fallback
    state = TAP_WAITING

TICK (периодический):
  Если !caps_has_gesture_bindings → выход
  
  Для каждого пальца в TAP_WAITING:
    Если has_long_press_timer && время >= long_press_timeout:
      Если движение > drag_threshold → отмена LP
      Иначе:
        Если can_hold_long_press → hold_actions(L) (ИНАЧЕ execute_actions(L))
        state = LONG_PRESSING
    
    Если single_tap_hold_timer сработал:
      Если !gesture_is_action_held → hold_actions(S)
  
  Если !caps_has_double_tap → очистить все DT ожидания
  
  second_double_tap_waiting timeout → execute_actions(S2_fallback)
  gesture_double_tap_waiting timeout → execute_actions(deferred_tap S)

FINGER MOVE:
  Если state >= TAP_WAITING И палец == main_ptr_id:
    TS shortcut: если только S (нет Sd/Ld/post-DT/LP) → cursor move, выход
    
    dx/dy = x-down_x, y-down_y
    Если state != DRAGGING → check_start_drag()

check_start_drag():
  Если !TAP_WAITING && !LONG_PRESSING → выход
  Если dx/dy ≤ drag_threshold → выход
  
  Выбор binding (приоритет):
    LONG_PRESSING:
      Ld → L → null
    post-DT:
      TS/TP: Dd → D → S → null (1-й палец)
      TS/TP: Dd2 → D2 → S2 → null (2-й палец)
    TS TAP_WAITING:
      Sd (1-й), Sd2 (2-й)
    TP TAP_WAITING 2-й палец:
      Sd2 → Dd2 → null
  
  Если binding отличается от текущего held → заменить
  on_drag_start() → state = DRAGGING

FINGER UP:
  Второй палец:
    Если есть DT/Dd2 → second_double_tap_waiting
    Иначе если S2 fallback → execute_actions(S2)
    Если pending_second_double → execute_actions(pending_second_double)
    release_held_actions()
  
  Первый палец:
    switch(state):
      TAP_WAITING:
        handle_tap_up()
      LONG_PRESSING:
        Если deferred D → execute(D)
        Если held L → release
      DRAGGING:
        Если deferred D → execute(D)
        release_held_actions()
      DOUBLE_TAP_WAITING:
        S fire, cleanup
      IDLE:
        Если post_dt_drag → cleanup

handle_tap_up():
  Если pending_deferred_double → execute(D) (или hold если no Dd)
  Если post_dt_drag → cleanup
  Если double_tap_consumed → execute(S)
  Если has_double_tap:
    Сохранить S как deferred_tap
    Сохранить D как pending_double
    Сохранить D backup как pending_deferred_double
    gesture_double_tap_waiting = true
  Иначе:
    execute(S) (hold если нет Sd, execute если есть Sd)
    Если has_double_tap_drag → zombie DT_WAITING (для location tracking)
```

### 5.2. Предлагаемые исправления багов

#### Баг #1 (ложный haptic при LP без L):
```c
// base.c:256 — вместо:
if (f->cached_has_long_press_timer && g_state.cfg.gesture_long_press_haptic > 0)
// должно быть:
if (f->cached_has_active_long_press && g_state.cfg.gesture_long_press_haptic > 0)
```

#### Баг #2 (второй tap молчит при D=0, Dd=SET):
```c
// base.c:442-448 — zombie DT для Dd-only нужно проверять наличие D:
if (active_has_double_tap_drag) {
// вместо zombie DT, просто выйти в IDLE:
// Дополнительно: если zombie DT уже активен, второй tap должен проигнорировать DT confirm
// и просто выполнить S снова (через Path3 double_tap_consumed)
```

**Исправление**: после того как zombie DT_WAITING поймал второй tap как "DT confirm" и ничего не сделал (D=0), установить `gesture_double_tap_consumed = true`. Тогда третий+ tap будут работать через Path3. Второй tap всё ещё silent — но это приемлемо для Dd-only конфига (double tap drag без double tap binding).

Альтернатива: не устанавливать zombie DT_WAITING когда D=0, даже если Dd>0. Второй tap будет просто S.

#### Баг #3 (двойной S при долгом удержании):
```c
// base.c:269-271 — когда single_tap_hold таймер срабатывает:
// проблема: handle_tap_up Path4 опять сохраняет S как deferred
// решение: не устанавливать DT_WAITING если S уже был выполнен
// Проверять g_state.gesture_is_action_held перед Path4
```

#### Баг #4 (spurious left-click после two-finger tap):
```c
// handler.c:577-598 — добавить флаг:
if (!g_state.gesture_handler_active && f->is_tap) {
    int nf = active_finger_count();
    if (nf == 1) {
        if (g_state.two_finger_tap_fired) {
            g_state.two_finger_tap_fired = false;  // подавить
        } else {
            // normal left-click
        }
    } else if (nf == 2) {
        g_state.two_finger_tap_fired = true;
        // right-click
    }
}
```

### 5.3. Оптимизация цепей fallback

```
TS TAP_WAITING drag:
  1-й палец: Sd → null
  2-й палец: Sd2 → null

TS LONG_PRESSING drag:
  1-й палец: Ld → L → null

TS post-DT drag:
  1-й палец: Dd → D → S → null
  2-й палец: Dd2 → D2 → S2 → null

TP TAP_WAITING drag:
  1-й палец: null (no drag)
  2-й палец: Sd2 → Dd2 → null

TP LONG_PRESSING drag:
  1-й палец: Ld → L → null

TP post-DT drag:
  1-й палец: Dd → D → S → null
  2-й палец: Dd2 → D2 → S2 → null
```

### 5.4. Контрольные точки для тестирования

Для каждой комбинации биндов (2^6 = 64 для первого пальца + комбинации второго) проверить:

1. **Достижимость всех биндов**: каждый заданный бинд должен сработать при корректном жесте
2. **Отсутствие ложных срабатываний**: бинды, которые не заданы, не должны срабатывать
3. **Приоритеты разрешения конфликтов**:
   - Drag отменяет Tap (Sd перехватывает движение до S)
   - LP отменяется вторым пальцем
   - Dd откладывает D (D срабатывает при up)
4. **Корректность таймеров**: DT timeout, LP timeout, second-finger DT timeout
5. **Resume второго пальца** (TS): возвращение первого пальца при активном втором
