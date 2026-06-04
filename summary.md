## Goal
- Port all touch activation logic (LOCK/TRACK/HOVER modes for on‑screen controls) from Java to pure C, eliminate latency between touch input and visual feedback, and make touchpad‑mode gesture handling (double‑tap, long‑press, drag) work identically to the already‑refined touchscreen path.

## Constraints & Preferences
- No project build (only on user request).
- Target Android device at `192.168.172.181`.
- Prefer generalising touchscreen + touchpad gesture handling into shared code rather than duplicating it.

## Progress
### Done
- Created `touch_processor_activation.c/.h` with all activation‑mode handling, tracked‑button management, hovered‑element tracking, and visual state query.
- Merged process + visual sync into a **single** JNI call per touch event; `nativeOnFingerDown/Move/Up/tick` now accept `float[] outPositions` + `byte[] outActive` and fill them inline.
- Packed **5 bytes/element** in the visual‑state byte array: `[visual_active, petal0, petal1, petal2, petal3]` so DPAD directions are pre‑computed in C.
- Added `ControlElement.syncVisualState(active, x, y, p0, p1, p2, p3)` – a direct field setter with zero DPAD math.
- Simplified `InputControlsView.java`: removed `syncVisualStatesFromNative()`, `nativeEngagedElements`, `trackedButtons`, `hoveredButtons` from the native path, replaced with `applyVisualStates()`.
- Deleted dead JNI methods: `nativeSyncVisualStates`, `nativeGetElementVisual`, `nativeActivateAt`, `nativeDeactivateAll`.
- Updated `CMakeLists.txt` to compile `touch_processor_activation.c`.
- Fixed **HOVER mode** in `handle_touchscreen_move`: replaced `handle_element_up` with `release_element_bindings` (no `current_ptr_id` guard) so the previous hovered element always deactivates.
- Fixed **HOVER mode** in `handle_touchpad_move`: same fix as touchscreen – `release_element_bindings` unconditionally.
- Fixed **up handler** (both modes): batch release (`tb->count > 1`) now clears `e->visual_active = false` so the last hovered/tracked button loses its highlight on finger‑up.
- Fixed **RANGE_BUTTON**: `element_range_button_up` now clears `visual_active`, `engaged`, `current_ptr_id`.
- Fixed **TRACK mode**: reverted the erroneous `release_element_bindings` to restore the original Java accumulation behaviour (buttons stay active, only first button gets gesture bindings).
- Added **`nativeUpdateConfig`** JNI binding (`touch_processor_update_config`) for runtime config re‑sync.
- Added **`globalCursorSpeed`** parameter to `NativeTouchProcessor.buildNativeConfig()` – multiplied into `cursorSpeed`.
- Updated `XServerDisplayActivity.updateGestureConfig()` to rebuild the `NativeConfig` and push it to the native processor via `nativeTouchProcessor.updateConfig()`.
- Fixed touchpad double‑tap drag: after double‑tap confirmation in `touchpad_finger_down` (`entry.c`), state set to `GESTURE_STATE_TAP_WAITING`.
- **Element‑vs‑gesture fix**: non‑BUTTON elements at point always set `handled = true` when not passthrough (removed `current_ptr_id` check in `touchpad.c`, `touchscreen.c`), preventing second finger from incorrectly entering gesture processing.
- **Removed second‑finger LP/LP‑drag** from across the entire codebase:
  - C gesture code (`entry.c`, `base.c`, `touchscreen.c`): no binding copies, LP timer posting, LONG_PRESSING paths, LP fire on up, main‑finger‑up LP emulation.
  - C headers (`touch_processor.h`, `touch_processor_internal.h`): removed `long_press_2nd`/`long_press_drag_2nd` from `FingerBindings`; removed `ts_long_2nd`/`tp_long_2nd`/`ts_long_drag_2nd`/`tp_long_drag_2nd` from `TouchConfig`; removed COPY macro lines.
  - JNI (`touch_processor_jni.cpp`): removed `READ_GESTURE_LIST` calls for the four fields.
  - Java gesture handlers (`GestureHandler.java`, `TouchpadGestureHandler.java`): removed fields, binding‑set construction, profile reads.
  - Profile/config (`ControlsProfile.java`, `NativeTouchProcessor.java`): removed fields, getters/setters, JSON serialization/deserialization, encoded‑array construction.
  - Dialogs (`TouchscreenGestureSettingsDialog.java`, `TouchpadGestureSettingsDialog.java`): removed UI sections and save handlers.
  - Old‑format reader (`InputControlsManager.java`): removed variable declarations, switch cases, and profile‑write lines.
  - Bundled `Default.icp` still contains old JSON keys; harmless (reader uses `skipValue()` fallback).
- **Fixed pre‑existing incomplete statement** in `touch_processor_jni.cpp` (`c.single_tap_delay_ms` missing assignment).
- **Removed touchscreen hold mode** (`SecondFingerMode` / `long_tap_mode`) entirely:
  - C: removed `SecondFingerMode` enum and `second_finger_mode` field from `touch_processor.h`; removed `long_tap_mode` from `touch_processor_internal.h`; removed all `long_tap_mode` assignments in `touch_processor.c`; removed `if (long_tap_mode)` early return in `touchscreen.c`; removed `is_ts_longtap_main` and `long_tap_mode` cancel logic from `gesture_tick` in `base.c`; removed `second_finger_mode` JNI reads from `touch_processor_jni.cpp`.
  - Java: removed `isLongTapMode` field and all its usages from `TouchscreenGestureHandler.java` (including unconditional LP timer post on main‑finger down and early return on second‑finger down); removed `SecondFingerMode` import and hold mode spinner/visibility logic from `TouchscreenGestureSettingsDialog.java`; removed `secondFingerMode` field, getter, setter, JSON serialization/deserialization from `ControlsProfile.java` and `NativeTouchProcessor.java`; removed XML UI elements for hold mode (`SPHoldMode`, `LLHoldModeOptions`, `LLTwoFingerSection` wrapper).
  - Hold Gestures (Long Press / Long Press Drag) and Two-Finger Gestures (Single Tap 2nd / Double Tap 2nd / ...) are now **always visible and configurable simultaneously**.
- **Note**: The `long_press_mode` removal also makes main‑finger long‑press not cancelled when second finger goes down — both can fire simultaneously.

### In Progress
- Second-finger single-tap-drag not working in touchpad native mode (needs investigation).

### Blocked
- *(none)*

## Key Decisions
- Merge process + visual sync into one JNI call per event to halve JNI overhead.
- Store DPAD petal state in C `TouchElement.petal_active[]` and pack into the visual‑sync byte array; Java never runs `sqrt`/`atan2`/dead‑zone.
- Use `release_element_bindings` for HOVER transitions (matching Java `prev.deactivate()`) instead of `handle_element_up` – avoids the `current_ptr_id` guard and correctly resets all button state.
- Keep the legacy Java fallback path untouched so the app still works if `libtouch_processor.so` fails to load.
- Add `globalCursorSpeed` to `buildNativeConfig()` rather than applying it as a separate multiplier so the native cursor speed is consistent with the old `touchpadView.setSensitivity()` formula.
- Re‑sync config via `nativeUpdateConfig` inside `updateGestureConfig()` so runtime changes to touchpad settings take effect immediately without requiring a game restart.
- Element‑vs‑gesture: any non‑passthrough element at point now unconditionally sets `handled = true`, diverging from Java semantics (which checks `currentPointerId`) — prevents second finger from entering gesture path even if element is already taken.
- Second‑finger LP/LP‑drag: removed entirely with no backward‑compat shim; single‑finger LP/LP‑drag completely untouched.
- Hold mode: removed entirely. Long-press (first-finger) and second-finger gestures are now always available both in UI and processing.

## Next Steps
1. Investigate second-finger single-tap-drag not working in touchpad native mode.
2. Build and install APK to verify on device.

## Critical Context
- C side uses `g_state.elements[i].visual_x/y`, `.visual_active`, `.petal_active[0..3]` as the single source of truth for on‑screen visual state.
- JNI loops write `float[count*2]` (positions) and `byte[count*5]` (active + 4 petals) in one pass every touch event.
- The touchpad gesture handler (`gesture/touchpad.c`) shares most of its state with the touchscreen handler (`gesture/touchscreen.c`) via the same `TouchProcessorState.gesture_*` flags – the only difference is the per‑finger binding set and the initial deferred‑tap setup.
- Double-tap detection flow: `handle_tap_up` (finger‑up) sets up `gesture_double_tap_waiting` + deferred single‑tap + pending double‑tap; next `touchpad_finger_down` (finger‑down) confirms if within distance; state must be `TAP_WAITING` for `check_start_drag` to run on subsequent move events.

## Relevant Files
- `app/src/main/cpp/winlator/gesture/entry.c` – `touchpad_finger_down`, `touchpad_finger_up`; second-finger binding copy, LP timer cancellation, hold-mode-independent second-finger path
- `app/src/main/cpp/winlator/gesture/base.c` – `handle_tap_up`, `gesture_tick`; removed `long_tap_mode` and `is_ts_longtap_main` from LP logic
- `app/src/main/cpp/winlator/gesture/touchpad.c` – element `handled` flag fix
- `app/src/main/cpp/winlator/gesture/touchscreen.c` – element `handled` flag fix; removed `long_tap_mode` early return for second finger
- `app/src/main/cpp/winlator/touch_processor.h` – removed `SecondFingerMode` enum and `second_finger_mode` field; removed `long_press_2nd`/`long_press_drag_2nd` from `FingerBindings` and config struct
- `app/src/main/cpp/winlator/touch_processor_internal.h` – removed `long_tap_mode` state; removed COPY macro lines for 2nd-finger LP/LP-drag
- `app/src/main/cpp/winlator/touch_processor.c` – removed `long_tap_mode` initialization
- `app/src/main/cpp/winlator/touch_processor_jni.cpp` – removed `second_finger_mode` and 2nd-finger LP/LP-drag JNI reads; fixed single_tap_delay_ms
- `app/src/main/java/com/winlator/cmod/widget/GestureHandler.java` – removed `longPress2ndFingerAction`/`longPress2ndFingerDragAction` fields
- `app/src/main/java/com/winlator/cmod/widget/TouchscreenGestureHandler.java` – removed `isLongTapMode`, unconditional LP post, second-finger early return
- `app/src/main/java/com/winlator/cmod/widget/TouchpadGestureHandler.java` – removed 2nd-finger LP/LP-drag references
- `app/src/main/java/com/winlator/cmod/contentdialog/TouchscreenGestureSettingsDialog.java` – removed hold mode spinner; hold + two-finger sections always visible
- `app/src/main/java/com/winlator/cmod/contentdialog/TouchpadGestureSettingsDialog.java` – removed 2nd-finger LP/LP-drag UI sections
- `app/src/main/java/com/winlator/cmod/inputcontrols/ControlsProfile.java` – removed `secondFingerMode` and LP/LP-drag fields/JSON
- `app/src/main/java/com/winlator/cmod/inputcontrols/NativeTouchProcessor.java` – removed `secondFingerMode` and LP/LP-drag encoding
- `app/src/main/java/com/winlator/cmod/inputcontrols/InputControlsManager.java` – removed LP/LP-drag and `secondFingerMode` from old-format reader
- `app/src/main/res/layout/touchscreen_gesture_settings_dialog.xml` – removed hold mode spinner/options; two-finger section un-wrapped
- `app/src/main/res/values/strings.xml` – `hold_mode_help` may still exist but unused
