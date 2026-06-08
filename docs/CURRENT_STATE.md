# Current State — Gesture Engine Optimisation Round

## What has been done

### Structural & Performance Optimisations (Round 1–10)

1. **Critical fixes (C1–C4)** — `base.c`, `types.h`, `internal.h`
   - C1: Removed redundant `caps_has_double_tap` guard in `gesture_tick` DT timeout, `touchpad_finger_down` SDTW, `touchpad_finger_up` SDTW
   - C2: Cached `caps_mode_mask` → `current_mode_has_gestures()`
   - C3: Cached `caps_ts_mask` / `caps_tp_mask` → `is_tp_mode()` / `is_ts_mode()`
   - C4: Inlined `get_gesture_bindings_for_mode` into `get_mode_bindings()`

2. **Moderate + minor fixes (M2–M5, m1–m7)** — `entry.c`, `base.c`, `handler.c`
   - M2: Removed redundant outer guard for `execute_deferred_double`
   - M3: Precomputed `touch_processor_vibrator` null check
   - M4: Simplified `is_finger_active` to single expression
   - M5: Hot-path optimised `gesture_tick` finger loop (remove `% 4`, manual indent)
   - m1–m7: Various `if(x)` simplifications, early exits, inline `get_ptr_id`, etc.

3. **TouchElement struct reorder** — `touch_processor.h` (better cache locality)

4. **Prefetch hints** — `__builtin_prefetch` in finger loop

5. **Dead field removal** — removed `held_modifiers` from `TouchElement`

6. **Const correctness + `restrict`** — read-only pointers → `const`, non-overlapping params → `restrict`

7. **`static`/`inline` audit** — internal functions made static, small hot functions inlined

8. **Range button `scroll_size` precompute** — computed at config time instead of every frame

9. **Executor dedup + early returns** — `executor.c`

10. **JNI batch optimisation** — `nativeOnFingerBatch` for batched finger events

### Bug Fixes in Progress

11. **Post-DT early exit fix** — `base.c:check_start_drag` (removed guard that skipped DT logic when second finger went down after DT confirmation). ✅ Fixed.

12. **Double-tap fires on single tap when S=0** — Root cause: `enter_double_tap_waiting` saved D to `pending_deferred_double` which DT timeout used to fire D. Fixed two changes:
    - `enter_double_tap_waiting`: save D to BOTH `pending_double` AND `pending_deferred_double` (needed for two-tap promotion recovery after `handle_gesture_down` zeroes `pending_double`).
    - DT timeout in `gesture_tick`: removed `else if (pending_deferred_double_count > 0) execute_deferred_double()` — D now fires ONLY from actual two taps, never from timeout. ✅ Fixed.

### Round 13 — Gesture Decision Tree & Binding Optimisations

13. **Dead field removal** — `is_single_tap_pair` removed from `GestureBranchParams` (never read).

14. **Struct size reduction** — `hold_delay_ms` changed from `int` to `uint16_t` in both `GesturePairPlan` and `GestureBranchParams` (value range 0–10).

15. **Fast path for D/Dd and LP/LPd** — added `resolve_pair_no_compete()` bypassing the full `gesture_decide_branch` (which has unreachable competition/hold branches for these pairs).

16. **`GestureBindingSet` eliminated** — removed the intermediate struct; `touch_finger_cache_bs` inlines `count > 0` checks directly from `FingerBindings`.

17. **Local copy eliminated** — `setup_main_finger_bindings` no longer creates a 160-byte local copy of `GestureModeBindings`. Copies directly from `*get_mode_bindings()` via the macro, then nulls `single_tap_drag` for TP mode.

18. **Dead code removed** — `single_drag_2nd` zeroing in `setup_main_finger_bindings` removed (the macro never reads this field).

19. **`_Static_assert`** — layout compatibility check between `GestureModeBindings` and `FingerBindings`.

20. **Gate `handle_ts_single_tap_hold`** — call now guarded on `g_state.cfg.is_ts`, avoiding function call overhead in TP mode.

21. **Code dedup** — `current_time_ms()` removed from `executor.c`; uses `now_ms()` from header.

### Round 14 — Deep Data Flow & State Machine Optimisation (5 parallel agents)

22. **Dead stores eliminated** — Removed 10 writes to `gesture_handler_active` (field kept for struct compatibility but unused by any reader).

23. **Dead branch removed** (`handler.c:486`) — Always-true state check `(f->state >= GESTURE_STATE_TAP_WAITING || f->state == GESTURE_STATE_IDLE)` removed from hot move path.

24. **Stale read fix** (`base.c:226,238`) — LP cancel in `gesture_tick` now reads live `g_state.gesture_is_action_held` instead of stale pre-loop snapshot. Fixes multi-finger LP interaction.

25. **Redundant finger scan removed** (`entry.c:113-122`) — `confirm_second_finger_global_dt` no longer scans all 8 fingers when the main finger is already found.

26. **TouchBinding copy → memcpy** — Element-by-element TouchBinding copies in `enter_double_tap_waiting`, `double_tap_confirm_internal`, `confirm_double_tap`, `touchpad_finger_down`, and `save_pending_resume_action` replaced with `memcpy`.

27. **bindings_equal fixed** (`base.c:54`) — Now compares `modifiers` field too (not just `type`+`keycode`).

28. **TouchBinding reordered** — Moved `auto_repeat_interval_ms` before `modifiers`/bools to eliminate padding (9 bytes data, 1 byte trailing alignment).

29. **Cache-critical struct reorder** — Moved `elements[128]` (76KB) and `spatial_grid[64][128]` (32KB) to the very end of `TouchProcessorState`. Hot fields (cfg, fingers, ptr_x/y, gesture state, grid metadata) now packed contiguously in L1 cache without a 76KB gap.

30. **Division → multiply in hit-test** — Cached `grid_inv_cell_w/h = 1.0f / grid_cell_w/h` in `build_spatial_grid`; `hit_test_element` uses multiply instead of division in the hot per-event path.

31. **Gesture threshold squared cached** — `gesture_threshold_sq` precomputed at init/reset/config-update; `element_button_move` avoids per-move multiply.

32. **visual_dirty_any single flag** — Combined 4-word dirty-mask loop into one `uint32_t visual_dirty_any` check. Faster early-exit in `visual_state_flush` called on every touch event.

33. **Computed-goto hot path** — `pointer_button_idx` switch replaced with static const lookup table. `execute_actions_impl` hold/auto-repeat deduped, hot g_state fields copied to locals.

34. **Extraneous stores removed** — Redundant `free_finger_hint` store in `on_finger_up` removed; redundant `bindings_generation=0` after `memset` in `finger_init` removed; redundant `abs_x >= 1.0f` bezier check removed.

35. **Range button up optimized** — `element_range_button_up` reuses cached `range_initial_kc` instead of recomputing via switch+array.

36. **Gesture flag clears deduped** — `element_button_up` consolidated three gesture-swiped/long-press/timer resets to single `cleanup:` label.

37. **Prefetch added** — `activation_activate_at` (called on every finger-down) now prefetches elements ahead.

## Files affected (cumulative)

| File | Changes |
|------|---------|
| `gesture/base.c` | C1, M2, M4, M5, m3, m5, m7, static/inline, Bug 11, Bug 12, resolve_long_press_pair→resolve_pair_no_compete, stale is_action_held fix, memcpy for binding copies, bindings_equal fix, gesture_handler_active writes removed |
| `gesture/entry.c` | C1, M2, m2, m4, m6, const/restrict, static/inline, handle_ts_single_tap_hold gate, resolve→new API, redundant finger scan removed, memcpy for binding copies, gesture_handler_active writes removed |
| `gesture/handler.c` | M2, m6, m7, const/restrict, static/inline, always-true state check removed, pointer_left_enabled short-circuit, save_pending_resume_action memcpy |
| `gesture/executor.c` | dedup + early returns, removed duplicate current_time_ms, hold/auto-repeat dedup, toggle dispatch dedup, hot g_state locals, pointer_button_idx LUT |
| `gesture/types.h` | is_single_tap_pair removal, hold_delay_ms→uint16_t, GestureBindingSet removed, resolve_pair_no_compete added |
| `touch_processor.h` | TouchElement reorder, held_modifiers removal, const, TouchBinding reorder (9 bytes data) |
| `touch_processor_internal.h` | C2, C3, C4, const/restrict, static/inline, setup_main_finger_bindings, _Static_assert, elements+spatial_grid moved to end, grid_inv_cell_w/h, gesture_threshold_sq, visual_dirty_any |
| `element/shared.c` | touch_finger_cache_bs inlined (GestureBindingSet removed), grid_inv_cell compute, redundant bezier check removed, hit_test division→multiply |
| `element/button.c` | gesture_threshold_sq used, gesture flag clears deduped |
| `element/range_button.c` | range_initial_kc reuse |
| `touch_processor.c` | static/inline, targeted memset, redundant stores removed, gesture_threshold_sq init |
| `touch_processor_activation.h` | const/restrict |
| `touch_processor_activation.c` | prefetch added in activation_activate_at |
| `touch_processor_jni.cpp` | JNI batch, visual_dirty_any single check, isCopy+JNI_ABORT, memcpy in batch |

## Current status
- All Bug fixes applied ✅
- Gesture decision tree & binding optimised (fast paths, smaller structs)
- Memory layout optimised (76KB gap removed from hot region)
- JNI dispatch optimised (isCopy, memcpy, dirty_any)
- Build target: Android device at `192.168.175.230`.

## Build
```bash
cd /home/nikita/projects/Winlator-Ludashi && ./gradlew :app:assembleRelease
```
