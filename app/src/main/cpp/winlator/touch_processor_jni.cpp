#include <jni.h>
#include "touch_processor.h"
#include "touch_processor_internal.h"
#include <string.h>
#include <stdio.h>

static jclass g_result_class = NULL;
static jmethodID g_result_ctor;
static jfieldID g_count_field;
static jfieldID g_types_field;
static jfieldID g_intArgs_field;

// Helper: read a TouchBinding binding list from a Java int[] array.
// The Java array stores [type0, keycode0, type1, keycode1, ...].
// Up to max_count entries are read into dst.
static int read_binding_list(JNIEnv* env, jintArray arr, TouchBinding* dst, int max_count) {
    if (!arr) return 0;
    jsize len = env->GetArrayLength(arr);
    if (len <= 0) return 0;
    jint* elems = env->GetIntArrayElements(arr, NULL);
    int count = (len / 2) < max_count ? (len / 2) : max_count;
    for (int i = 0; i < count; i++) {
        dst[i].type = (BindingType)elems[i * 2];
        dst[i].keycode = elems[i * 2 + 1];
        dst[i].modifiers = 0;
    }
    env->ReleaseIntArrayElements(arr, elems, JNI_ABORT);
    return count;
}

// Macro to read one gesture binding list from a Java int[] field into a C struct field.
#define READ_GESTURE_LIST(env, obj, cls, javaFieldName, cfg_field_8, cfg_count) do { \
    jintArray _arr = (jintArray)env->GetObjectField(obj, \
        env->GetFieldID(cls, javaFieldName, "[I")); \
    cfg_count = read_binding_list(env, _arr, cfg_field_8, 8); \
} while(0)

// Helper to convert TouchActionResult to Java TouchActionResult
static jobject to_java_result(JNIEnv* env, const TouchActionResult* r) {
    if (!g_result_class) {
        g_result_class = (jclass)env->NewGlobalRef(
            env->FindClass("com/winlator/cmod/inputcontrols/NativeTouchProcessor$TouchActionResult"));
        g_result_ctor = env->GetMethodID(g_result_class, "<init>", "()V");
        g_count_field = env->GetFieldID(g_result_class, "count", "I");
        g_types_field = env->GetFieldID(g_result_class, "types", "[I");
        g_intArgs_field = env->GetFieldID(g_result_class, "intArgs", "[I");
    }

    jintArray types = env->NewIntArray(r->count);
    jintArray intArgs = env->NewIntArray(r->count * 3);

    jint typeBuf[32];
    jint argBuf[32 * 3];

    for (int i = 0; i < r->count; i++) {
        typeBuf[i] = (jint)r->actions[i].type;
        switch (r->actions[i].type) {
            case ACT_POINTER_MOVE:
                argBuf[i * 3] = r->actions[i].pointer_move.x;
                argBuf[i * 3 + 1] = r->actions[i].pointer_move.y;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_POINTER_MOVE_DELTA:
                argBuf[i * 3] = r->actions[i].pointer_delta.dx;
                argBuf[i * 3 + 1] = r->actions[i].pointer_delta.dy;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_POINTER_BUTTON_PRESS:
            case ACT_POINTER_BUTTON_RELEASE:
                argBuf[i * 3] = r->actions[i].pointer_button.button;
                argBuf[i * 3 + 1] = 0;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_KEY_PRESS:
            case ACT_KEY_RELEASE:
                argBuf[i * 3] = r->actions[i].key.keycode;
                argBuf[i * 3 + 1] = r->actions[i].key.is_down ? 1 : 0;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_MOUSE_EVENT:
                argBuf[i * 3] = r->actions[i].mouse_event.flags;
                argBuf[i * 3 + 1] = r->actions[i].mouse_event.dx;
                argBuf[i * 3 + 2] = r->actions[i].mouse_event.dy;
                break;
            case ACT_SCROLL:
                argBuf[i * 3] = r->actions[i].scroll.amount;
                argBuf[i * 3 + 1] = 0;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_HAPTIC:
                argBuf[i * 3] = r->actions[i].haptic.effect;
                argBuf[i * 3 + 1] = 0;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_SET_CURSOR_SPEED:
                argBuf[i * 3] = r->actions[i].cursor_speed.speed;
                argBuf[i * 3 + 1] = 0;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_START_MOUSE_MOVE:
                argBuf[i * 3] = r->actions[i].mouse_move.dx;
                argBuf[i * 3 + 1] = r->actions[i].mouse_move.dy;
                argBuf[i * 3 + 2] = r->actions[i].mouse_move.hold;
                break;
            case ACT_STOP_MOUSE_MOVE:
                argBuf[i * 3] = 0;
                argBuf[i * 3 + 1] = 0;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_GAMEPAD_STATE:
                argBuf[i * 3] = r->actions[i].key.keycode;
                argBuf[i * 3 + 1] = r->actions[i].key.is_down ? 1 : 0;
                argBuf[i * 3 + 2] = 0;
                break;
            case ACT_GAMEPAD_AXIS:
                argBuf[i * 3] = r->actions[i].gamepad_axis.is_left;
                argBuf[i * 3 + 1] = r->actions[i].gamepad_axis.axis_x;
                argBuf[i * 3 + 2] = r->actions[i].gamepad_axis.axis_y;
                break;
            default:
                argBuf[i * 3] = 0;
                argBuf[i * 3 + 1] = 0;
                argBuf[i * 3 + 2] = 0;
                break;
        }
    }

    env->SetIntArrayRegion(types, 0, r->count, typeBuf);
    env->SetIntArrayRegion(intArgs, 0, r->count * 3, argBuf);



    jobject result = env->NewObject(g_result_class, g_result_ctor);

    env->SetIntField(result, g_count_field, (jint)r->count);
    env->SetObjectField(result, g_types_field, types);
    env->SetObjectField(result, g_intArgs_field, intArgs);

    return result;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeInit(
    JNIEnv* env, jclass clazz, jobject config) {
    TouchProcessorConfig c;
    memset(&c, 0, sizeof(c));

    jclass configClass = env->GetObjectClass(config);
    c.touch_mode = (TouchMode)env->GetIntField(config, env->GetFieldID(configClass, "touchMode", "I"));
    c.input_mode = (InputMode)env->GetIntField(config, env->GetFieldID(configClass, "inputMode", "I"));
    c.second_finger_mode = (SecondFingerMode)env->GetIntField(config, env->GetFieldID(configClass, "secondFingerMode", "I"));
    c.long_press_timeout_ms = env->GetIntField(config, env->GetFieldID(configClass, "longPressTimeoutMs", "I"));
    c.double_tap_timeout_ms = env->GetIntField(config, env->GetFieldID(configClass, "doubleTapTimeoutMs", "I"));
    c.single_tap_delay_ms = env->GetIntField(config, env->GetFieldID(configClass, "singleTapDelayMs", "I"));
    c.drag_threshold_px = env->GetIntField(config, env->GetFieldID(configClass, "dragThresholdPx", "I"));
    c.double_tap_distance_px = env->GetIntField(config, env->GetFieldID(configClass, "doubleTapDistancePx", "I"));
    c.binding_delay_ms = env->GetIntField(config, env->GetFieldID(configClass, "bindingDelayMs", "I"));
    c.long_press_delay_ms = env->GetIntField(config, env->GetFieldID(configClass, "longPressDelayMs", "I"));
    c.cursor_speed = env->GetIntField(config, env->GetFieldID(configClass, "cursorSpeed", "I"));
    c.gesture_threshold_px = env->GetIntField(config, env->GetFieldID(configClass, "gestureThresholdPx", "I"));
    c.cursor_acceleration_threshold = env->GetIntField(config, env->GetFieldID(configClass, "cursorAccelerationThreshold", "I"));
    c.cursor_acceleration_factor = env->GetFloatField(config, env->GetFieldID(configClass, "cursorAccelerationFactor", "F"));
    c.screen_w = env->GetIntField(config, env->GetFieldID(configClass, "screenW", "I"));
    c.screen_h = env->GetIntField(config, env->GetFieldID(configClass, "screenH", "I"));
    c.xform_scale_x = env->GetFloatField(config, env->GetFieldID(configClass, "xformScaleX", "F"));
    if (c.xform_scale_x <= 0.0f) c.xform_scale_x = 1.0f;
    c.xform_scale_y = env->GetFloatField(config, env->GetFieldID(configClass, "xformScaleY", "F"));
    if (c.xform_scale_y <= 0.0f) c.xform_scale_y = 1.0f;
    c.gesture_long_press_haptic = env->GetIntField(config, env->GetFieldID(configClass, "gestureLongPressHaptic", "I"));
    c.haptic_enabled = env->GetBooleanField(config, env->GetFieldID(configClass, "hapticEnabled", "Z"));

    // Touchscreen gesture bindings (12 lists, each read individually)
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTap",      c.ts_single_tap,      c.ts_single_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tsLongPress",      c.ts_long_press,      c.ts_long_press_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTap",      c.ts_double_tap,      c.ts_double_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTapDrag",  c.ts_single_tap_drag, c.ts_single_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tsLongPressDrag",  c.ts_long_press_drag, c.ts_long_press_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTapDrag",  c.ts_double_tap_drag, c.ts_double_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTap2nd",   c.ts_single_2nd,      c.ts_single_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTap2nd",   c.ts_double_2nd,      c.ts_double_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTapDrag2nd", c.ts_single_drag_2nd, c.ts_single_drag_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTapDrag2nd",  c.ts_double_drag_2nd, c.ts_double_drag_2nd_count);

    // Touchpad gesture bindings (12 lists)
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTap",      c.tp_single_tap,      c.tp_single_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tpLongPress",      c.tp_long_press,      c.tp_long_press_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTap",      c.tp_double_tap,      c.tp_double_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTapDrag",  c.tp_single_tap_drag, c.tp_single_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tpLongPressDrag",  c.tp_long_press_drag, c.tp_long_press_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTapDrag",  c.tp_double_tap_drag, c.tp_double_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTap2nd",   c.tp_single_2nd,      c.tp_single_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTap2nd",   c.tp_double_2nd,      c.tp_double_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTapDrag2nd", c.tp_single_drag_2nd, c.tp_single_drag_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTapDrag2nd",  c.tp_double_drag_2nd, c.tp_double_drag_2nd_count);



    // Touchscreen gesture bindings logged above via READ_GESTURE_LIST

    touch_processor_init(&c);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeSetElements(
    JNIEnv* env, jclass clazz, jobjectArray elements) {
    if (elements == NULL) return;

    jsize len = env->GetArrayLength(elements);
    if (len > MAX_ELEMENTS) len = MAX_ELEMENTS;

    TouchElement elems[MAX_ELEMENTS];
    memset(elems, 0, sizeof(elems));

    jclass elemClass = env->FindClass("com/winlator/cmod/inputcontrols/NativeTouchProcessor$NativeElement");

    for (int i = 0; i < len; i++) {
        jobject je = env->GetObjectArrayElement(elements, i);
        if (!je) continue;

        elems[i].type = (ElementType)env->GetIntField(je, env->GetFieldID(elemClass, "type", "I"));
        elems[i].shape = (ElementShape)env->GetIntField(je, env->GetFieldID(elemClass, "shape", "I"));
        elems[i].x = env->GetIntField(je, env->GetFieldID(elemClass, "x", "I"));
        elems[i].y = env->GetIntField(je, env->GetFieldID(elemClass, "y", "I"));
        elems[i].w = env->GetFloatField(je, env->GetFieldID(elemClass, "w", "F"));
        elems[i].h = env->GetFloatField(je, env->GetFieldID(elemClass, "h", "F"));
        elems[i].scale = env->GetFloatField(je, env->GetFieldID(elemClass, "scale", "F"));
        elems[i].passthrough_touch = env->GetBooleanField(je, env->GetFieldID(elemClass, "passthroughTouch", "Z"));
        elems[i].activation_mode = (ActivationMode)env->GetIntField(je, env->GetFieldID(elemClass, "activationMode", "I"));
        elems[i].toggle_switch = env->GetBooleanField(je, env->GetFieldID(elemClass, "toggleSwitch", "Z"));

        // Range button fields
        jfieldID range_ord_id = env->GetFieldID(elemClass, "rangeOrdinal", "I");
        elems[i].range_ordinal = range_ord_id ? env->GetIntField(je, range_ord_id) : 0;
        jfieldID range_max_id = env->GetFieldID(elemClass, "rangeMax", "I");
        elems[i].range_max = range_max_id ? env->GetIntField(je, range_max_id) : 26;
        jfieldID range_bc_id = env->GetFieldID(elemClass, "bindingCount", "I");
        elems[i].range_binding_count = range_bc_id ? env->GetIntField(je, range_bc_id) : 4;
        jfieldID range_orient_id = env->GetFieldID(elemClass, "orientation", "I");
        elems[i].range_orientation = range_orient_id ? env->GetIntField(je, range_orient_id) : 0;

        // Read per-element haptic settings (added at end of NativeElement for backward compat)
        // Default to 1 (HAPTIC_TICK) if fields don't exist
        jfieldID lp_haptic_id = env->GetFieldID(elemClass, "buttonLongPressHaptic", "I");
        elems[i].button_long_press_haptic = lp_haptic_id ? env->GetIntField(je, lp_haptic_id) : 1;
        jfieldID g_haptic_id = env->GetFieldID(elemClass, "buttonGestureHaptic", "I");
        elems[i].button_gesture_haptic = g_haptic_id ? env->GetIntField(je, g_haptic_id) : 1;

        // Read element-specific gesture bindings
        elems[i].element_long_press_count = read_binding_list(env,
            (jintArray)env->GetObjectField(je, env->GetFieldID(elemClass, "elementLongPress", "[I")),
            elems[i].element_long_press, 8);
        elems[i].element_gesture_count = read_binding_list(env,
            (jintArray)env->GetObjectField(je, env->GetFieldID(elemClass, "elementGesture", "[I")),
            elems[i].element_gesture, 8);
        // Read bindings — Java sends [type0, keycode0, type1, keycode1, ...]
        jintArray bindingTypes = (jintArray)env->GetObjectField(je, env->GetFieldID(elemClass, "bindingTypes", "[I"));
        if (bindingTypes) {
            jint* bt = env->GetIntArrayElements(bindingTypes, NULL);
            jsize btLen = env->GetArrayLength(bindingTypes);
            int slot = 0;
            for (int j = 0; j + 1 < btLen && slot < 4; j += 2, slot++) {
                int typeVal = bt[j];
                int keycodeVal = bt[j + 1];
                elems[i].bindings[slot].type = (BindingType)typeVal;
                if (typeVal != BINDING_NONE) {
                    elems[i].bindings[slot].keycode = keycodeVal;
                }
            }
            for (int j = slot; j < 4; j++) {
                elems[i].bindings[j].type = BINDING_NONE;
                elems[i].bindings[j].keycode = 0;
            }
            env->ReleaseIntArrayElements(bindingTypes, bt, JNI_ABORT);
        }

        elems[i].current_ptr_id = -1;
        elems[i].engaged = false;
        for (int p = 0; p < MAX_PETALS; p++) elems[i].petal_active[p] = false;

        env->DeleteLocalRef(je);
    }

    touch_processor_set_elements(elems, len);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeSetSnappingSize(
    JNIEnv* env, jclass clazz, jfloat size) {
    touch_processor_set_snapping_size(size);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeSetResolutionScale(
    JNIEnv* env, jclass clazz, jfloat scale) {
    touch_processor_set_resolution_scale(scale);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeSetSimTouchScreen(
    JNIEnv* env, jclass clazz, jboolean enabled) {
    touch_processor_set_sim_touch_screen(enabled == JNI_TRUE);
}

JNIEXPORT jobject JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeOnFingerDown(
    JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs,
    jfloatArray outPositions, jbyteArray outActive) {
    TouchActionResult r = touch_processor_on_finger_down(ptrId, x, y, (uint64_t)timeMs);
    // Fill visual state output arrays in the same call
    if (outPositions != NULL && outActive != NULL) {
        jfloat* pos = env->GetFloatArrayElements(outPositions, NULL);
        jbyte* act = env->GetByteArrayElements(outActive, NULL);
        int count = g_state.element_count;
        int maxPos = env->GetArrayLength(outPositions) / 3;
        int maxAct = env->GetArrayLength(outActive) / 5;
        int limit = count < maxPos ? (count < maxAct ? count : maxAct) : (maxPos < maxAct ? maxPos : maxAct);
        for (int i = 0; i < limit; i++) {
            pos[i * 3] = g_state.elements[i].visual_x;
            pos[i * 3 + 1] = g_state.elements[i].visual_y;
            pos[i * 3 + 2] = g_state.elements[i].range_scroll_offset;
            int base = i * 5;
            act[base] = g_state.elements[i].visual_active ? 1 : 0;
            act[base + 1] = g_state.elements[i].petal_active[0] ? 1 : 0;
            act[base + 2] = g_state.elements[i].petal_active[1] ? 1 : 0;
            act[base + 3] = g_state.elements[i].petal_active[2] ? 1 : 0;
            act[base + 4] = g_state.elements[i].petal_active[3] ? 1 : 0;
        }
        env->ReleaseFloatArrayElements(outPositions, pos, 0);
        env->ReleaseByteArrayElements(outActive, act, 0);
    }
    return to_java_result(env, &r);
}

JNIEXPORT jobject JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeOnFingerMove(
    JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs,
    jfloatArray outPositions, jbyteArray outActive) {
    TouchActionResult r = touch_processor_on_finger_move(ptrId, x, y, (uint64_t)timeMs);
    if (outPositions != NULL && outActive != NULL) {
        jfloat* pos = env->GetFloatArrayElements(outPositions, NULL);
        jbyte* act = env->GetByteArrayElements(outActive, NULL);
        int count = g_state.element_count;
        int maxPos = env->GetArrayLength(outPositions) / 3;
        int maxAct = env->GetArrayLength(outActive) / 5;
        int limit = count < maxPos ? (count < maxAct ? count : maxAct) : (maxPos < maxAct ? maxPos : maxAct);
        for (int i = 0; i < limit; i++) {
            pos[i * 3] = g_state.elements[i].visual_x;
            pos[i * 3 + 1] = g_state.elements[i].visual_y;
            pos[i * 3 + 2] = g_state.elements[i].range_scroll_offset;
            int base = i * 5;
            act[base] = g_state.elements[i].visual_active ? 1 : 0;
            act[base + 1] = g_state.elements[i].petal_active[0] ? 1 : 0;
            act[base + 2] = g_state.elements[i].petal_active[1] ? 1 : 0;
            act[base + 3] = g_state.elements[i].petal_active[2] ? 1 : 0;
            act[base + 4] = g_state.elements[i].petal_active[3] ? 1 : 0;
        }
        env->ReleaseFloatArrayElements(outPositions, pos, 0);
        env->ReleaseByteArrayElements(outActive, act, 0);
    }
    return to_java_result(env, &r);
}

JNIEXPORT jobject JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeOnFingerUp(
    JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs,
    jfloatArray outPositions, jbyteArray outActive) {
    TouchActionResult r = touch_processor_on_finger_up(ptrId, x, y, (uint64_t)timeMs);
    if (outPositions != NULL && outActive != NULL) {
        jfloat* pos = env->GetFloatArrayElements(outPositions, NULL);
        jbyte* act = env->GetByteArrayElements(outActive, NULL);
        int count = g_state.element_count;
        int maxPos = env->GetArrayLength(outPositions) / 3;
        int maxAct = env->GetArrayLength(outActive) / 5;
        int limit = count < maxPos ? (count < maxAct ? count : maxAct) : (maxPos < maxAct ? maxPos : maxAct);
        for (int i = 0; i < limit; i++) {
            pos[i * 3] = g_state.elements[i].visual_x;
            pos[i * 3 + 1] = g_state.elements[i].visual_y;
            pos[i * 3 + 2] = g_state.elements[i].range_scroll_offset;
            int base = i * 5;
            act[base] = g_state.elements[i].visual_active ? 1 : 0;
            act[base + 1] = g_state.elements[i].petal_active[0] ? 1 : 0;
            act[base + 2] = g_state.elements[i].petal_active[1] ? 1 : 0;
            act[base + 3] = g_state.elements[i].petal_active[2] ? 1 : 0;
            act[base + 4] = g_state.elements[i].petal_active[3] ? 1 : 0;
        }
        env->ReleaseFloatArrayElements(outPositions, pos, 0);
        env->ReleaseByteArrayElements(outActive, act, 0);
    }
    return to_java_result(env, &r);
}

JNIEXPORT jobject JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeTick(
    JNIEnv* env, jclass clazz, jlong timeMs,
    jfloatArray outPositions, jbyteArray outActive) {
    TouchActionResult r = touch_processor_tick((uint64_t)timeMs);
    if (outPositions != NULL && outActive != NULL) {
        jfloat* pos = env->GetFloatArrayElements(outPositions, NULL);
        jbyte* act = env->GetByteArrayElements(outActive, NULL);
        int count = g_state.element_count;
        int maxPos = env->GetArrayLength(outPositions) / 3;
        int maxAct = env->GetArrayLength(outActive) / 5;
        int limit = count < maxPos ? (count < maxAct ? count : maxAct) : (maxPos < maxAct ? maxPos : maxAct);
        for (int i = 0; i < limit; i++) {
            pos[i * 3] = g_state.elements[i].visual_x;
            pos[i * 3 + 1] = g_state.elements[i].visual_y;
            pos[i * 3 + 2] = g_state.elements[i].range_scroll_offset;
            int base = i * 5;
            act[base] = g_state.elements[i].visual_active ? 1 : 0;
            act[base + 1] = g_state.elements[i].petal_active[0] ? 1 : 0;
            act[base + 2] = g_state.elements[i].petal_active[1] ? 1 : 0;
            act[base + 3] = g_state.elements[i].petal_active[2] ? 1 : 0;
            act[base + 4] = g_state.elements[i].petal_active[3] ? 1 : 0;
        }
        env->ReleaseFloatArrayElements(outPositions, pos, 0);
        env->ReleaseByteArrayElements(outActive, act, 0);
    }
    return to_java_result(env, &r);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeReset(
    JNIEnv* env, jclass clazz) {
    touch_processor_reset();
}

JNIEXPORT jboolean JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeIsPassthroughActive(
    JNIEnv* env, jclass clazz) {
    return touch_processor_is_passthrough_active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeUpdateConfig(
    JNIEnv* env, jclass clazz, jobject config) {
    TouchProcessorConfig c;
    memset(&c, 0, sizeof(c));
    // Re-read all fields from the Java NativeConfig (same as nativeInit)
    jclass configClass = env->GetObjectClass(config);
    c.touch_mode = (TouchMode)env->GetIntField(config, env->GetFieldID(configClass, "touchMode", "I"));
    c.input_mode = (InputMode)env->GetIntField(config, env->GetFieldID(configClass, "inputMode", "I"));
    c.second_finger_mode = (SecondFingerMode)env->GetIntField(config, env->GetFieldID(configClass, "secondFingerMode", "I"));
    c.long_press_timeout_ms = env->GetIntField(config, env->GetFieldID(configClass, "longPressTimeoutMs", "I"));
    c.double_tap_timeout_ms = env->GetIntField(config, env->GetFieldID(configClass, "doubleTapTimeoutMs", "I"));
    c.single_tap_delay_ms = env->GetIntField(config, env->GetFieldID(configClass, "singleTapDelayMs", "I"));

    // Touchscreen gesture bindings (12 lists, each read individually)
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTap",      c.ts_single_tap,      c.ts_single_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tsLongPress",      c.ts_long_press,      c.ts_long_press_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTap",      c.ts_double_tap,      c.ts_double_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTapDrag",  c.ts_single_tap_drag, c.ts_single_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tsLongPressDrag",  c.ts_long_press_drag, c.ts_long_press_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTapDrag",  c.ts_double_tap_drag, c.ts_double_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTap2nd",   c.ts_single_2nd,      c.ts_single_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTap2nd",   c.ts_double_2nd,      c.ts_double_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tsSingleTapDrag2nd", c.ts_single_drag_2nd, c.ts_single_drag_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tsDoubleTapDrag2nd",  c.ts_double_drag_2nd, c.ts_double_drag_2nd_count);

    // Touchpad gesture bindings (12 lists)
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTap",      c.tp_single_tap,      c.tp_single_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tpLongPress",      c.tp_long_press,      c.tp_long_press_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTap",      c.tp_double_tap,      c.tp_double_tap_count);
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTapDrag",  c.tp_single_tap_drag, c.tp_single_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tpLongPressDrag",  c.tp_long_press_drag, c.tp_long_press_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTapDrag",  c.tp_double_tap_drag, c.tp_double_tap_drag_count);
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTap2nd",   c.tp_single_2nd,      c.tp_single_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTap2nd",   c.tp_double_2nd,      c.tp_double_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tpSingleTapDrag2nd", c.tp_single_drag_2nd, c.tp_single_drag_2nd_count);
    READ_GESTURE_LIST(env, config, configClass, "tpDoubleTapDrag2nd",  c.tp_double_drag_2nd, c.tp_double_drag_2nd_count);

    touch_processor_init(&c);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeSetXformScale(
    JNIEnv* env, jclass clazz, jfloat scaleX, jfloat scaleY) {
    touch_processor_set_xform_scale(scaleX, scaleY);
}

JNIEXPORT jboolean JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeGetElementState(
    JNIEnv* env, jclass clazz, jint elemIndex, jfloatArray outXY) {
    float sx, sy;
    bool engaged;
    if (!touch_processor_get_element_state(elemIndex, &sx, &sy, &engaged, NULL))
        return JNI_FALSE;
    if (outXY && env->GetArrayLength(outXY) >= 2) {
        jfloat vals[2] = { sx, sy };
        env->SetFloatArrayRegion(outXY, 0, 2, vals);
    }
    return engaged ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeHandleDownByMode(
    JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs)
{
    return touch_processor_handle_down_by_mode(ptrId, x, y, (uint64_t)timeMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeHandleUpByMode(
    JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs)
{
    return touch_processor_handle_up_by_mode(ptrId, x, y, (uint64_t)timeMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeHandleMoveByMode(
    JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs)
{
    touch_processor_handle_move_by_mode(ptrId, x, y, (uint64_t)timeMs);
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeTrackedCount(
    JNIEnv* env, jclass clazz, jint ptrId)
{
    return (jint)touch_processor_tracked_count(ptrId);
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_inputcontrols_NativeTouchProcessor_nativeHoveredIndex(
    JNIEnv* env, jclass clazz, jint ptrId)
{
    return (jint)touch_processor_hovered_index(ptrId);
}

} // extern "C"
