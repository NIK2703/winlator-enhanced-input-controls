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

11. **Post-DT early exit fix** — `base.c:check_start_drag` (removed guard that skipped DT logic when second finger went down after DT confirmation). **APK built & installed, awaiting test.**

12. **Double-tap fires on single tap when S=0** — reported by user. Analysed `handle_tap_up_impl` path 4 — already uses original unconditional `enter_double_tap_waiting` logic. Root cause still being investigated. Possible candidates:
    - `execute_actions_hold` in `confirm_double_tap` vs old `execute_actions`
    - Interaction with resolver functions in `resolve_double_tap_pair` / `confirm_second_double_tap`
    - Need device test to reproduce.

## Files affected

| File | Changes |
|------|---------|
| `gesture/base.c` | C1, M2, M4, M5, m3, m5, m7, static/inline, Bug 11 fix |
| `gesture/entry.c` | C1, M2, m2, m4, m6, const/restrict, static/inline |
| `gesture/handler.c` | M2, m6, m7, const/restrict, static/inline |
| `gesture/touchscreen.c` | const/restrict |
| `gesture/touchpad.c` | const/restrict |
| `touch_processor.h` | TouchElement reorder, held_modifiers removal, const |
| `touch_processor_internal.h` | C2, C3, C4, const/restrict, static/inline |
| `touch_processor.c` | static/inline |
| `touch_processor_activation.h` | const/restrict |
| `touch_processor_jni.cpp` | JNI batch |
| `executor.c` | dedup + early returns |

## Current status
- Optimisations applied and verified via static analysis (no semantic changes).
- Bug 11 (second-finger DDT/D broken) — fix applied, **awaiting device test**.
- Bug 12 (DT fires on single tap when S=0) — **under investigation**.
- Build target: Android device at `192.168.172.181`.

## Build command
```bash
cd /home/nikita/projects/Winlator-Ludashi && ./gradlew installDebug \
  -Dorg.gradle.parallel=false -Dorg.gradle.daemon=true \
  -Pandroid.injected.invocation.stereotype=0 -x :input_controls:build \
  -x :audio_plugin:build -x :android_sysvshm:build
```

## Next steps
1. Test Bug 11 fix on device.
2. Investigate Bug 12 root cause.
3. Re-run lint / typecheck after all fixes confirmed.
