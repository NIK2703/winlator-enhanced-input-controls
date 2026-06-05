# Рефакторинг нативного C-кода тач-управления

## Текущее состояние: quality score 6/10

Код функционален и архитектурно осмыслен, но накопил критический технический долг:
- порт Java-кода в C выполнен «в лоб» — монолитные функции вместо декомпозиции
- отсутствует defensive programming (NULL-гварды, валидация границ)
- 3+ случая неопределённого поведения (buffer over-read, отрицательный индекс массива, чтение inactive union field)
- брошенный на полпути рефакторинг (4 мёртвые декларации в `activation_internal.h`)
- дублирование логики в 2–3 местах (конфиг, DT confirmation, toggle-switch)

---

## 1. Устранить неопределённое поведение (critical)

### 1.1. Отрицательный индекс массива в `handler.c`

**Где:** `handler.c:360,399,431,508,673`

```c
g_state.tracked[f->ptr_id % MAX_FINGERS]
```

**Проблема:** если `ptr_id == -1` (sentinel), `%` в C даёт отрицательный результат → out-of-bounds доступ.

**Решение:**
```c
int slot = (f->ptr_id >= 0 && f->ptr_id < MAX_FINGERS) ? f->ptr_id : 0;
// или
int slot = abs(f->ptr_id % MAX_FINGERS);
```

Проверить все места, где `ptr_id` используется как индекс или в арифметике.

### 1.2. Buffer over-read в spatial grid

**Где:** `shared.c:320-322`

**Проблема:** `build_spatial_grid` не проверяет, что ячейка переполнена (>128 элементов). Запись в массив не происходит, но `spatial_grid_count[cell]` увеличивается. `hit_test_element` читает данные за пределами массива.

**Решение:**
```c
if (cnt < MAX_ELEMENTS) {
    g_state.spatial_grid[cell][cnt] = i;
    g_state.spatial_grid_count[cell] = cnt + 1;
} // иначе просто пропустить — элемент не попадёт в grid
```

### 1.3. Чтение неактивного поля union в логах

**Где:** `touch_processor_jni.cpp:221-228`

**Проблема:** `dispatch_actions_batch` читает `pointer_move.x`, `pointer_delta.dx`, `pointer_delta.dy` для **всех** типов экшенов. Для `ACT_KEY_PRESS` это чтение неактивного поля union — UB.

**Решение:** сделать лог осмысленным, читая только активное поле в зависимости от `action.type`:
```c
switch (action.type) {
    case ACT_KEY_PRESS:     LOGI("keycode=%d", action.key.keycode); break;
    case ACT_POINTER_MOVE:  LOGI("move=(%d,%d)", action.pointer_move.x, ...); break;
    // ...
}
```

---

## 2. Устранить логические баги

### 2.1. Потеря held-actions в `on_finger_cancel`

**Где:** `touch_processor.c:231-239`

**Проблема:** при cancel пальца `release_held_actions` пишет действия в локальный `cancel_result`, который никогда не диспатчится. Wine не получает release → клавиши остаются зажатыми.

**Решение:** вызвать `dispatch_actions(&cancel_result)` после `release_held_actions`, либо перенести вызов в `handle_gesture_up` в коде gesture handler.

### 2.2. Пропущенный `break` в `winhandler.c`

**Где:** `winhandler.c:378`

```c
case RC_EXEC:
    handleExec(buffer + 5, len - 5);
case RC_KILL_PROCESS:  // ← fallthrough!
    handleKillProcess(buffer + 1, len - 1);
```

**Проблема:** каждый `RC_EXEC` также убивает процесс.

**Решение:** добавить `break;` после `handleExec(...)`.

### 2.3. Division by zero в `range_button.c`

**Где:** `range_button.c:10,42`

**Проблема:** деление на `range_binding_count` и `% range_max` без проверки на 0.

**Решение:**
```c
// строка 10
*out_element_size = e->range_binding_count > 0
    ? ((float)e->range_max * hs_snap) / (float)e->range_binding_count
    : 0.0f;

// строка 42
if (e->range_max > 0) {
    index %= e->range_max;
    if (index < 0) index += e->range_max;
}
```

### 2.4. Игнорирование кода возврата `schedule_action`

**Где:** `executor.c:123,130,138,180,181,188,189,197,198,214`

**Проблема:** при переполнении очереди (32 слота) действия молча теряются.

**Решение:** как минимум залогировать потерю. В идеале — увеличить размер очереди или обработать ситуацию:
```c
int idx = schedule_action(binding, binding_delay, ...);
if (idx < 0) {
    LOGW("schedule_action queue full, action dropped");
}
```

---

## 3. Завершить брошенный рефакторинг

### 3.1. Удалить или реализовать мёртвые декларации

**Где:** `touch_processor_activation_internal.h:7-16`

4 `static` функции объявлены, но не реализованы:
- `flip_toggle_switch`
- `apply_two_finger_guard`
- `process_non_button_move`
- `handle_track_or_hover_move`

**Решение:** либо реализовать их и заменить дублированный код в `touch_processor_activation.c`, либо удалить объявления, если план изменился.

### 3.2. Устранить копипасту DT confirmation

**Где:** `handler.c:127-155`, `handler.c:248-275`, `entry.c:7-13`

Блок double-tap confirmation повторяется в 3 местах.

**Решение:** вынести в общую функцию:
```c
static bool try_double_tap_confirm(TouchFinger *f, int *out_pair_count, ...) {
    // общая логика
}
```

### 3.3. Устранить дублирование JNI config чтения

**Где:** `touch_processor_jni.cpp:324-422` vs `569-619`

Два блока ~50 строк, читающих одни и те же поля конфига.

**Решение:**
```c
static void fill_config_from_java(JNIEnv *env, jobject configObj,
                                   TouchProcessorConfig *cfg) {
    // единое место чтения всех полей
}
```

---

## 4. Добавить defensive programming

### 4.1. NULL-проверки на входные параметры

Во всех функциях, которые принимают `TouchFinger* f`, `TouchElement* e`, `TouchActionResult* result` — добавить проверку. Пример:

```c
bool element_button_down(TouchElement *e, int ptr_id, float x, float y,
                         uint64_t time_ms, TouchActionResult *result) {
    if (!e || !result) return false;
    // ...
}
```

Особенно критично в файлах:
- `gesture/handler.c` (3 giant functions, 0 NULL checks)
- `gesture/touchscreen.c` (3 functions, 0 NULL checks)
- `element/button.c` (3 functions, 0 NULL checks)
- `element/shared.c` (14 functions, частичные проверки)

### 4.2. Guard clauses для инвариантов

Добавить ранние возвраты для состояний, которые не должны обрабатываться:

```c
if (!f->active) return;
if (ptr_id < 0) return;
if (elem_index < 0 || elem_index >= g_state.element_count) return;
```

### 4.3. Валидация конфига

В `touch_processor_update_config` добавить проверку полей конфига:
- `auto_repeat_interval_ms > 0` (иначе — потенциально отрицательный `uint64_t`)
- `range_binding_count > 0` перед делением
- `long_press_count / double_tap_count <= 8`

---

## 5. Декомпозировать монолитные функции

| Файл | Функция | Строк | План декомпозиции |
|------|---------|-------|-------------------|
| `handler.c` | `handle_gesture_down` | 348 | Выделить: passthrough, LOCK/Track/HOVER, toggle-switch, TS main, TS second, TP |
| `handler.c` | `handle_gesture_move` | 315 | Выделить: element move, toggle-switch slide, Track/Hover tracking, gesture move |
| `handler.c` | `handle_gesture_up` | 145 | Выделить: element release, gesture cleanup, DT confirmation |
| `touchscreen.c` | `handle_touchscreen_down` | 247 | Выделить: deferred DT, second-finger bind, DT confirm |
| `touchscreen.c` | `handle_touchscreen_move` | 222 | Выделить: LOCK 처리, passthrough, gesture move |
| `gesture/base.c` | `check_start_drag` | 181 | Выделить 4 ветки в отдельные функции |
| `gesture/base.c` | `gesture_tick` | 132 | Выделить: planned actions, deferred LP, scheduled actions |
| `touch_processor_activation.c` | `activation_handle_move` | 134 | Выделить: toggle-switch flip (2×), Track/Hover |

---

## 6. Устранить утечки памяти

### 6.1. asprintf в fakeinput.cpp

**Где:** `fakeinput.cpp:125, 338, 467`

Каждый вызов `asprintf` должен иметь соответствующий `free`. Предлагаемое решение — заменить на статический буфер или RAII-обёртку (C++ файл):

```cpp
// Вариант 1: статический буфер
static thread_local char g_fake_path_buf[PATH_MAX];
snprintf(g_fake_path_buf, sizeof(g_fake_path_buf), "%s/%s", fake_evdev_dir, ...);
return g_fake_path_buf;

// Вариант 2: обёртка с авто-освобождением (C++17)
struct StrBuf {
    char *ptr;
    StrBuf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vasprintf(&ptr, fmt, ap); va_end(ap); }
    ~StrBuf() { free(ptr); }
    operator char*() { return ptr; }
};
```

### 6.2. Linked list PidNode в winhandler.c

**Где:** `winhandler.c` — `PidNode` в `handleChildProcesses`

Список растёт бесконечно.

**Решение:** чистить при каждом новом вызове или использовать `std::vector<int>`.

---

## 7. Thread-safety

**Проблема:** весь `g_state` — глобальный mutable struct. JNI-методы вызываются из Java UI thread, tick — из timer thread, scheduled actions — из executor thread.

**Первичные меры (low-hanging fruit):**
- `g_state.gesture_held_count`, `g_state.gesture_main_ptr_id` — вынести за `atomic_int` или guarded переменные
- `g_state.elements` — при обновлении (`set_elements`) не менять in-place, а собирать новый массив и заменять указатель атомарно (RCU-style)

**Полное решение (если появятся баги гонок):**
- `rwlock` для `g_state.elements` (читают все, пишет только `set_elements`)
- mutex для секций, меняющих состояние жестов

---

## 8. Улучшить именование и кодстайл

### 8.1. Убрать идентификаторы с ведущим подчёркиванием

**Где:** `touchscreen.c:93,106,123,188,204,214`, `handler.c`, `entry.c`

```c
// Было:
for (int _i = 0; _i < g_state.gesture_held_count; _i++)

// Стало:
for (int i = 0; i < g_state.gesture_held_count; i++)
```

C11 §7.1.3 резервирует идентификаторы, начинающиеся с `_`, для реализации — формально это UB.

### 8.2. Заменить магические числа на константы

- `8` → `MAX_FINGERS` / `MAX_BINDINGS_PER_GESTURE`
- `32` → `MAX_ACTION_RESULTS`
- `128` → `MAX_ELEMENTS` (уже есть, не везде используется)
- `10` → раскомментировать в `COPY_FINGER_BINDINGS`

### 8.3. Переименовать `selected` в `button.c`

Поле `e->selected` используется как latch/debounce, а не как выделение элемента. Переименовать в `is_pressed` или `latched`.

---

## 9. Оптимизации

### 9.1. Убрать `nanosleep` из event pipeline

**Где:** `gesture/base.c:362-365`

Блокирующий вызов на время `single_tap_delay_ms` (~50 мс) роняет throughput.

**Решение:** использовать существующий `ScheduledAction` из `touch_processor_internal.h:22-28`.

### 9.2. Кешировать `point_in_element` в `button.c`

**Где:** `button.c` — `point_in_element` вызывается 7+ раз за один move.

**Решение:** вычислить `inside` один раз в начале и передавать как параметр во внутренние ветки.

### 9.3. Вычисление grid_cell_w/h с одинарной точностью

Убедиться, что `grid_cell_w` и `grid_cell_h` не теряют синхронизацию при обнулении.

---

## 10. Пересмотреть архитектуру наследования полей

### 10.1. Split `TouchElement` struct

**Где:** `touch_processor.h:57-115`

`TouchElement` — 50+ полей, смешивающих конфиг и runtime state.

**Решение:**
```c
struct TouchElementConfig {
    // x, y, w, h, type, scale, opacity, bindings, ...
};

struct TouchElementState {
    bool engaged;
    int current_ptr_id;
    bool selected;
    bool petal_active;
    // ...
};

struct TouchElement {
    struct TouchElementConfig config;
    struct TouchElementState state;
};
```

Это улучшит читаемость и позволит безопасно обновлять конфиг, не трогая runtime state.

---

## Приоритеты

| Приоритет | Категория | Что делать | Примерная трудоёмкость |
|-----------|-----------|------------|----------------------|
| **P0** | UB / Crash | Пункты 1.1, 1.2, 1.3, 2.3 | 1 день |
| **P0** | Логика | Пункты 2.1, 2.2, 2.4 | 1 день |
| **P1** | Техдолг | Пункты 3.1, 3.2, 3.3 | 2 дня |
| **P1** | Defensive | Пункт 4 (NULL checks) | 2 дня |
| **P2** | Архитектура | Пункты 5, 10 (декомпозиция) | 3-4 дня |
| **P2** | Утечки | Пункт 6 | 1 день |
| **P3** | Thread-safety | Пункт 7 | 2-3 дня |
| **P3** | Кодстайл | Пункт 8 | 1 день |
| **P3** | Оптимизации | Пункт 9 | 1 день |

**Итого:** ~12-16 дней на полный рефакторинг для одного разработчика.
