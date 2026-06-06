#include <jni.h>
#include <android/log.h>
#include "touch_processor.h"
#include "touch_processor_internal.h"
#include <string.h>
#include <stdio.h>

static JavaVM* g_jvm = NULL;

// --- Native visual state buffer (zero-copy, no JNI array ops) ---
// Packed layout per element — directly mappable to Java ByteBuffer
#pragma pack(push, 1)
typedef struct {
    float visual_x;
    float visual_y;
    int32_t visual_active;       // 0 or 1
    int32_t petal_active[4];     // 0 or 1 each
    float range_scroll_offset;
} VisualStateEntry;
#pragma pack(pop)

#define VISUAL_STRIDE ((int)sizeof(VisualStateEntry))

static VisualStateEntry g_visual_buffer[MAX_ELEMENTS];
static jobject g_visual_buffer_ref = NULL;

static void visual_state_flush(void) {
    if (!g_state.visual_state_dirty) return;
    g_state.visual_state_dirty = false;
    int n = g_state.element_count;
    for (int i = 0; i < n; i++) {
        g_visual_buffer[i].visual_x = g_state.elements[i].visual_x;
        g_visual_buffer[i].visual_y = g_state.elements[i].visual_y;
        g_visual_buffer[i].visual_active = g_state.elements[i].visual_active ? 1 : 0;
        g_visual_buffer[i].petal_active[0] = g_state.elements[i].petal_active[0] ? 1 : 0;
        g_visual_buffer[i].petal_active[1] = g_state.elements[i].petal_active[1] ? 1 : 0;
        g_visual_buffer[i].petal_active[2] = g_state.elements[i].petal_active[2] ? 1 : 0;
        g_visual_buffer[i].petal_active[3] = g_state.elements[i].petal_active[3] ? 1 : 0;
        g_visual_buffer[i].range_scroll_offset = g_state.elements[i].range_scroll_offset;
    }
}

struct CachedFieldIDs {
    jclass configClass;
    jfieldID touchMode;
    jfieldID inputMode;
    jfieldID longPressTimeoutMs;
    jfieldID doubleTapTimeoutMs;
    jfieldID singleTapDelayMs;
    jfieldID dragThresholdPx;
    jfieldID doubleTapDistancePx;
    jfieldID bindingDelayMs;
    jfieldID longPressDelayMs;
    jfieldID cursorSpeed;
    jfieldID gestureThresholdPx;
    jfieldID cursorAccelerationThreshold;
    jfieldID cursorAccelerationFactor;
    jfieldID screenW;
    jfieldID screenH;
    jfieldID xformScaleX;
    jfieldID xformScaleY;
    jfieldID viewOffsetX;
    jfieldID viewOffsetY;
    jfieldID gestureLongPressHaptic;
    jfieldID hapticEnabled;
    jfieldID tsSingleTap;
    jfieldID tsLongPress;
    jfieldID tsDoubleTap;
    jfieldID tsSingleTapDrag;
    jfieldID tsLongPressDrag;
    jfieldID tsDoubleTapDrag;
    jfieldID tsSingleTap2nd;
    jfieldID tsDoubleTap2nd;
    jfieldID tsSingleTapDrag2nd;
    jfieldID tsDoubleTapDrag2nd;
    jfieldID tpSingleTap;
    jfieldID tpLongPress;
    jfieldID tpDoubleTap;
    jfieldID tpSingleTapDrag;
    jfieldID tpLongPressDrag;
    jfieldID tpDoubleTapDrag;
    jfieldID tpSingleTap2nd;
    jfieldID tpDoubleTap2nd;
    jfieldID tpSingleTapDrag2nd;
    jfieldID tpDoubleTapDrag2nd;
    jfieldID stSingleTap;
    jfieldID stLongPress;
    jfieldID stDoubleTap;
    jfieldID stSingleTapDrag;
    jfieldID stLongPressDrag;
    jfieldID stDoubleTapDrag;
    jfieldID stSingleTap2nd;
    jfieldID stDoubleTap2nd;
    jfieldID stSingleTapDrag2nd;
    jfieldID stDoubleTapDrag2nd;
};

static CachedFieldIDs g_config;
static bool g_config_cached = false;

struct CachedElementFieldIDs {
    jclass elemClass;
    jfieldID type;
    jfieldID shape;
    jfieldID x;
    jfieldID y;
    jfieldID w;
    jfieldID h;
    jfieldID scale;
    jfieldID passthroughTouch;
    jfieldID activationMode;
    jfieldID toggleSwitch;
    jfieldID autoRepeat;
    jfieldID autoRepeatIntervalMs;
    jfieldID elementLongPress;
    jfieldID elementGesture;
    jfieldID bindingTypes;
    jfieldID bindingSticky;
    jfieldID rangeOrdinal;
    jfieldID rangeMax;
    jfieldID bindingCount;
    jfieldID orientation;
    jfieldID buttonLongPressHaptic;
    jfieldID buttonGestureHaptic;
};

static CachedElementFieldIDs g_elem;

static jobject g_dispatch_obj = NULL;
static jmethodID g_injectPointerMove;
static jmethodID g_injectPointerMoveDelta;
static jmethodID g_injectPointerButtonPress;
static jmethodID g_injectPointerButtonRelease;
static jmethodID g_injectKeyPress;
static jmethodID g_injectKeyRelease;
static jmethodID g_mouseEvent;
static jmethodID g_scrollEvent;
static jmethodID g_hapticEvent;
static jmethodID g_setCursorSpeed;
static jmethodID g_startMouseMove;
static jmethodID g_stopMouseMove;
static jmethodID g_gamepadState;
static jmethodID g_gamepadAxis;
static jmethodID g_dispatchAllActions;



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

#define READ_CACHED_BINDING_LIST(env, config, fieldId, dst, count) do { \
    jintArray _arr = (jintArray)env->GetObjectField(config, fieldId); \
    count = read_binding_list(env, _arr, dst, 8); \
} while(0)



static void dispatch_actions(JNIEnv* env, const TouchActionResult* r) {
    if (!g_dispatch_obj || !r) {
        return;
    }
    for (int i = 0; i < r->count; i++) {
        switch (r->actions[i].type) {
            case ACT_POINTER_MOVE:
                env->CallVoidMethod(g_dispatch_obj, g_injectPointerMove,
                    r->actions[i].pointer_move.x, r->actions[i].pointer_move.y);
                break;
            case ACT_POINTER_MOVE_DELTA:
                env->CallVoidMethod(g_dispatch_obj, g_injectPointerMoveDelta,
                    r->actions[i].pointer_delta.dx, r->actions[i].pointer_delta.dy);
                break;
            case ACT_POINTER_BUTTON_PRESS:
                env->CallVoidMethod(g_dispatch_obj, g_injectPointerButtonPress,
                    r->actions[i].pointer_button.button);
                break;
            case ACT_POINTER_BUTTON_RELEASE:
                env->CallVoidMethod(g_dispatch_obj, g_injectPointerButtonRelease,
                    r->actions[i].pointer_button.button);
                break;
            case ACT_KEY_PRESS:
                env->CallVoidMethod(g_dispatch_obj, g_injectKeyPress,
                    r->actions[i].key.keycode, JNI_TRUE);
                break;
            case ACT_KEY_RELEASE:
                env->CallVoidMethod(g_dispatch_obj, g_injectKeyRelease,
                    r->actions[i].key.keycode, JNI_FALSE);
                break;
            case ACT_MOUSE_EVENT:
                env->CallVoidMethod(g_dispatch_obj, g_mouseEvent,
                    r->actions[i].mouse_event.flags, r->actions[i].mouse_event.dx, r->actions[i].mouse_event.dy);
                break;
            case ACT_SCROLL:
                env->CallVoidMethod(g_dispatch_obj, g_scrollEvent,
                    r->actions[i].scroll.amount);
                break;
            case ACT_HAPTIC:
                env->CallVoidMethod(g_dispatch_obj, g_hapticEvent,
                    r->actions[i].haptic.effect);
                break;
            case ACT_SET_CURSOR_SPEED:
                env->CallVoidMethod(g_dispatch_obj, g_setCursorSpeed,
                    r->actions[i].cursor_speed.speed);
                break;
            case ACT_START_MOUSE_MOVE:
                env->CallVoidMethod(g_dispatch_obj, g_startMouseMove,
                    r->actions[i].mouse_move.dx, r->actions[i].mouse_move.dy, r->actions[i].mouse_move.hold);
                break;
            case ACT_STOP_MOUSE_MOVE:
                env->CallVoidMethod(g_dispatch_obj, g_stopMouseMove);
                break;
            case ACT_GAMEPAD_STATE:
                env->CallVoidMethod(g_dispatch_obj, g_gamepadState,
                    r->actions[i].key.keycode, r->actions[i].key.is_down ? JNI_TRUE : JNI_FALSE);
                break;
            case ACT_GAMEPAD_AXIS:
                env->CallVoidMethod(g_dispatch_obj, g_gamepadAxis,
                    r->actions[i].gamepad_axis.is_left, r->actions[i].gamepad_axis.axis_x, r->actions[i].gamepad_axis.axis_y);
                break;
            default:
                break;
        }
    }
}

static void dispatch_actions_batch(JNIEnv* env, const TouchActionResult* r) {
    if (!g_dispatch_obj || !r || r->count == 0) {
        return;
    }
    __android_log_print(ANDROID_LOG_DEBUG, "Winlator_JNI", "dispatch_actions_batch: count=%d", r->count);
    for (int i = 0; i < r->count; i++) {
        __android_log_print(ANDROID_LOG_DEBUG, "Winlator_JNI", "  action[%d]: type=%d args=%d,%d,%d",
            i, r->actions[i].type,
            r->actions[i].pointer_move.x,
            r->actions[i].pointer_delta.dx,
            r->actions[i].pointer_delta.dy);
    }

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

    jintArray types = env->NewIntArray(r->count);
    jintArray intArgs = env->NewIntArray(r->count * 3);
    env->SetIntArrayRegion(types, 0, r->count, typeBuf);
    env->SetIntArrayRegion(intArgs, 0, r->count * 3, argBuf);

    env->CallVoidMethod(g_dispatch_obj, g_dispatchAllActions, types, intArgs, (jint)r->count);

    env->DeleteLocalRef(types);
    env->DeleteLocalRef(intArgs);
}

extern "C" {

void touch_processor_set_view_offset(float offset_x, float offset_y) {
    g_state.cfg.view_offset_x = offset_x;
    g_state.cfg.view_offset_y = offset_y;
}

static void nativeInit(JNIEnv* env, jclass clazz, jobject config) {
    if (!g_config_cached) {
        jclass cls = env->GetObjectClass(config);
        g_config.configClass = (jclass)env->NewGlobalRef(cls);
        g_config.touchMode = env->GetFieldID(cls, "touchMode", "I");
        g_config.inputMode = env->GetFieldID(cls, "inputMode", "I");
        g_config.longPressTimeoutMs = env->GetFieldID(cls, "longPressTimeoutMs", "I");
        g_config.doubleTapTimeoutMs = env->GetFieldID(cls, "doubleTapTimeoutMs", "I");
        g_config.singleTapDelayMs = env->GetFieldID(cls, "singleTapDelayMs", "I");
        g_config.dragThresholdPx = env->GetFieldID(cls, "dragThresholdPx", "I");
        g_config.doubleTapDistancePx = env->GetFieldID(cls, "doubleTapDistancePx", "I");
        g_config.bindingDelayMs = env->GetFieldID(cls, "bindingDelayMs", "I");
        g_config.longPressDelayMs = env->GetFieldID(cls, "longPressDelayMs", "I");
        g_config.cursorSpeed = env->GetFieldID(cls, "cursorSpeed", "I");
        g_config.gestureThresholdPx = env->GetFieldID(cls, "gestureThresholdPx", "I");
        g_config.cursorAccelerationThreshold = env->GetFieldID(cls, "cursorAccelerationThreshold", "I");
        g_config.cursorAccelerationFactor = env->GetFieldID(cls, "cursorAccelerationFactor", "F");
        g_config.screenW = env->GetFieldID(cls, "screenW", "I");
        g_config.screenH = env->GetFieldID(cls, "screenH", "I");
        g_config.xformScaleX = env->GetFieldID(cls, "xformScaleX", "F");
        g_config.xformScaleY = env->GetFieldID(cls, "xformScaleY", "F");
        g_config.viewOffsetX = env->GetFieldID(cls, "viewOffsetX", "F");
        g_config.viewOffsetY = env->GetFieldID(cls, "viewOffsetY", "F");
        g_config.gestureLongPressHaptic = env->GetFieldID(cls, "gestureLongPressHaptic", "I");
        g_config.hapticEnabled = env->GetFieldID(cls, "hapticEnabled", "Z");
        g_config.tsSingleTap = env->GetFieldID(cls, "tsSingleTap", "[I");
        g_config.tsLongPress = env->GetFieldID(cls, "tsLongPress", "[I");
        g_config.tsDoubleTap = env->GetFieldID(cls, "tsDoubleTap", "[I");
        g_config.tsSingleTapDrag = env->GetFieldID(cls, "tsSingleTapDrag", "[I");
        g_config.tsLongPressDrag = env->GetFieldID(cls, "tsLongPressDrag", "[I");
        g_config.tsDoubleTapDrag = env->GetFieldID(cls, "tsDoubleTapDrag", "[I");
        g_config.tsSingleTap2nd = env->GetFieldID(cls, "tsSingleTap2nd", "[I");
        g_config.tsDoubleTap2nd = env->GetFieldID(cls, "tsDoubleTap2nd", "[I");
        g_config.tsSingleTapDrag2nd = env->GetFieldID(cls, "tsSingleTapDrag2nd", "[I");
        g_config.tsDoubleTapDrag2nd = env->GetFieldID(cls, "tsDoubleTapDrag2nd", "[I");
        g_config.tpSingleTap = env->GetFieldID(cls, "tpSingleTap", "[I");
        g_config.tpLongPress = env->GetFieldID(cls, "tpLongPress", "[I");
        g_config.tpDoubleTap = env->GetFieldID(cls, "tpDoubleTap", "[I");
        g_config.tpSingleTapDrag = env->GetFieldID(cls, "tpSingleTapDrag", "[I");
        g_config.tpLongPressDrag = env->GetFieldID(cls, "tpLongPressDrag", "[I");
        g_config.tpDoubleTapDrag = env->GetFieldID(cls, "tpDoubleTapDrag", "[I");
        g_config.tpSingleTap2nd = env->GetFieldID(cls, "tpSingleTap2nd", "[I");
        g_config.tpDoubleTap2nd = env->GetFieldID(cls, "tpDoubleTap2nd", "[I");
        g_config.tpSingleTapDrag2nd = env->GetFieldID(cls, "tpSingleTapDrag2nd", "[I");
        g_config.tpDoubleTapDrag2nd = env->GetFieldID(cls, "tpDoubleTapDrag2nd", "[I");
        g_config.stSingleTap = env->GetFieldID(cls, "stSingleTap", "I");
        g_config.stLongPress = env->GetFieldID(cls, "stLongPress", "I");
        g_config.stDoubleTap = env->GetFieldID(cls, "stDoubleTap", "I");
        g_config.stSingleTapDrag = env->GetFieldID(cls, "stSingleTapDrag", "I");
        g_config.stLongPressDrag = env->GetFieldID(cls, "stLongPressDrag", "I");
        g_config.stDoubleTapDrag = env->GetFieldID(cls, "stDoubleTapDrag", "I");
        g_config.stSingleTap2nd = env->GetFieldID(cls, "stSingleTap2nd", "I");
        g_config.stDoubleTap2nd = env->GetFieldID(cls, "stDoubleTap2nd", "I");
        g_config.stSingleTapDrag2nd = env->GetFieldID(cls, "stSingleTapDrag2nd", "I");
        g_config.stDoubleTapDrag2nd = env->GetFieldID(cls, "stDoubleTapDrag2nd", "I");
        env->DeleteLocalRef(cls);
        g_config_cached = true;
    }

    TouchProcessorConfig c;
    memset(&c, 0, sizeof(c));

    c.touch_mode = (TouchMode)env->GetIntField(config, g_config.touchMode);
    c.input_mode = (InputMode)env->GetIntField(config, g_config.inputMode);
    c.long_press_timeout_ms = env->GetIntField(config, g_config.longPressTimeoutMs);
    c.double_tap_timeout_ms = env->GetIntField(config, g_config.doubleTapTimeoutMs);
    c.single_tap_delay_ms = env->GetIntField(config, g_config.singleTapDelayMs);
    c.drag_threshold_px = env->GetIntField(config, g_config.dragThresholdPx);
    c.double_tap_distance_px = env->GetIntField(config, g_config.doubleTapDistancePx);
    c.binding_delay_ms = env->GetIntField(config, g_config.bindingDelayMs);
    c.long_press_delay_ms = env->GetIntField(config, g_config.longPressDelayMs);
    c.cursor_speed = env->GetIntField(config, g_config.cursorSpeed);
    c.gesture_threshold_px = env->GetIntField(config, g_config.gestureThresholdPx);
    c.cursor_acceleration_threshold = env->GetIntField(config, g_config.cursorAccelerationThreshold);
    c.cursor_acceleration_factor = env->GetFloatField(config, g_config.cursorAccelerationFactor);
    c.screen_w = env->GetIntField(config, g_config.screenW);
    c.screen_h = env->GetIntField(config, g_config.screenH);
    c.xform_scale_x = env->GetFloatField(config, g_config.xformScaleX);
    if (c.xform_scale_x <= 0.0f) c.xform_scale_x = 1.0f;
    c.xform_scale_y = env->GetFloatField(config, g_config.xformScaleY);
    if (c.xform_scale_y <= 0.0f) c.xform_scale_y = 1.0f;
    c.view_offset_x = env->GetFloatField(config, g_config.viewOffsetX);
    c.view_offset_y = env->GetFloatField(config, g_config.viewOffsetY);
    c.gesture_long_press_haptic = env->GetIntField(config, g_config.gestureLongPressHaptic);
    c.haptic_enabled = env->GetBooleanField(config, g_config.hapticEnabled);

    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTap, c.ts_single_tap, c.ts_single_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsLongPress, c.ts_long_press, c.ts_long_press_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTap, c.ts_double_tap, c.ts_double_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTapDrag, c.ts_single_tap_drag, c.ts_single_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsLongPressDrag, c.ts_long_press_drag, c.ts_long_press_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTapDrag, c.ts_double_tap_drag, c.ts_double_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTap2nd, c.ts_single_2nd, c.ts_single_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTap2nd, c.ts_double_2nd, c.ts_double_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTapDrag2nd, c.ts_single_drag_2nd, c.ts_single_drag_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTapDrag2nd, c.ts_double_drag_2nd, c.ts_double_drag_2nd_count);

    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTap, c.tp_single_tap, c.tp_single_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpLongPress, c.tp_long_press, c.tp_long_press_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTap, c.tp_double_tap, c.tp_double_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTapDrag, c.tp_single_tap_drag, c.tp_single_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpLongPressDrag, c.tp_long_press_drag, c.tp_long_press_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTapDrag, c.tp_double_tap_drag, c.tp_double_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTap2nd, c.tp_single_2nd, c.tp_single_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTap2nd, c.tp_double_2nd, c.tp_double_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTapDrag2nd, c.tp_single_drag_2nd, c.tp_single_drag_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTapDrag2nd, c.tp_double_drag_2nd, c.tp_double_drag_2nd_count);

    // Apply sticky bitmasks to TouchBinding.modifiers (1=hold, 0=tap)
#define APPLY_STICKY(arr, cnt, sid) do { \
    int _st = env->GetIntField(config, g_config.sid); \
    for (int _i = 0; _i < (cnt); _i++) if (_st & (1 << _i)) (arr)[_i].modifiers = 1; \
} while(0)
    APPLY_STICKY(c.ts_single_tap, c.ts_single_tap_count, stSingleTap);
    APPLY_STICKY(c.ts_long_press, c.ts_long_press_count, stLongPress);
    APPLY_STICKY(c.ts_double_tap, c.ts_double_tap_count, stDoubleTap);
    APPLY_STICKY(c.ts_single_tap_drag, c.ts_single_tap_drag_count, stSingleTapDrag);
    APPLY_STICKY(c.ts_long_press_drag, c.ts_long_press_drag_count, stLongPressDrag);
    APPLY_STICKY(c.ts_double_tap_drag, c.ts_double_tap_drag_count, stDoubleTapDrag);
    APPLY_STICKY(c.ts_single_2nd, c.ts_single_2nd_count, stSingleTap2nd);
    APPLY_STICKY(c.ts_double_2nd, c.ts_double_2nd_count, stDoubleTap2nd);
    APPLY_STICKY(c.ts_single_drag_2nd, c.ts_single_drag_2nd_count, stSingleTapDrag2nd);
    APPLY_STICKY(c.ts_double_drag_2nd, c.ts_double_drag_2nd_count, stDoubleTapDrag2nd);

    APPLY_STICKY(c.tp_single_tap, c.tp_single_tap_count, stSingleTap);
    APPLY_STICKY(c.tp_long_press, c.tp_long_press_count, stLongPress);
    APPLY_STICKY(c.tp_double_tap, c.tp_double_tap_count, stDoubleTap);
    APPLY_STICKY(c.tp_single_tap_drag, c.tp_single_tap_drag_count, stSingleTapDrag);
    APPLY_STICKY(c.tp_long_press_drag, c.tp_long_press_drag_count, stLongPressDrag);
    APPLY_STICKY(c.tp_double_tap_drag, c.tp_double_tap_drag_count, stDoubleTapDrag);
    APPLY_STICKY(c.tp_single_2nd, c.tp_single_2nd_count, stSingleTap2nd);
    APPLY_STICKY(c.tp_double_2nd, c.tp_double_2nd_count, stDoubleTap2nd);
    APPLY_STICKY(c.tp_single_drag_2nd, c.tp_single_drag_2nd_count, stSingleTapDrag2nd);
    APPLY_STICKY(c.tp_double_drag_2nd, c.tp_double_drag_2nd_count, stDoubleTapDrag2nd);
#undef APPLY_STICKY

    touch_processor_init(&c);
}

static void nativeSetElements(JNIEnv* env, jclass clazz, jobjectArray elements) {
    if (elements == NULL) return;

    if (!g_elem.elemClass) {
        jclass ec = env->FindClass("com/winlator/cmod/inputcontrols/NativeTouchProcessor$NativeElement");
        g_elem.elemClass = (jclass)env->NewGlobalRef(ec);
        g_elem.type = env->GetFieldID(ec, "type", "I");
        g_elem.shape = env->GetFieldID(ec, "shape", "I");
        g_elem.x = env->GetFieldID(ec, "x", "I");
        g_elem.y = env->GetFieldID(ec, "y", "I");
        g_elem.w = env->GetFieldID(ec, "w", "F");
        g_elem.h = env->GetFieldID(ec, "h", "F");
        g_elem.scale = env->GetFieldID(ec, "scale", "F");
        g_elem.passthroughTouch = env->GetFieldID(ec, "passthroughTouch", "Z");
        g_elem.activationMode = env->GetFieldID(ec, "activationMode", "I");
        g_elem.toggleSwitch = env->GetFieldID(ec, "toggleSwitch", "Z");
        g_elem.autoRepeat = env->GetFieldID(ec, "autoRepeat", "Z");
        g_elem.autoRepeatIntervalMs = env->GetFieldID(ec, "autoRepeatIntervalMs", "I");
        g_elem.elementLongPress = env->GetFieldID(ec, "elementLongPress", "[I");
        g_elem.elementGesture = env->GetFieldID(ec, "elementGesture", "[I");
        g_elem.bindingTypes = env->GetFieldID(ec, "bindingTypes", "[I");
        g_elem.bindingSticky = env->GetFieldID(ec, "bindingSticky", "[I");
        g_elem.rangeOrdinal = env->GetFieldID(ec, "rangeOrdinal", "I");
        g_elem.rangeMax = env->GetFieldID(ec, "rangeMax", "I");
        g_elem.bindingCount = env->GetFieldID(ec, "bindingCount", "I");
        g_elem.orientation = env->GetFieldID(ec, "orientation", "I");
        g_elem.buttonLongPressHaptic = env->GetFieldID(ec, "buttonLongPressHaptic", "I");
        g_elem.buttonGestureHaptic = env->GetFieldID(ec, "buttonGestureHaptic", "I");
        env->DeleteLocalRef(ec);
    }

    jsize len = env->GetArrayLength(elements);
    if (len > MAX_ELEMENTS) len = MAX_ELEMENTS;

    TouchElement elems[MAX_ELEMENTS];
    memset(elems, 0, len * sizeof(TouchElement));

    for (int i = 0; i < len; i++) {
        jobject je = env->GetObjectArrayElement(elements, i);
        if (!je) continue;

        elems[i].type = (ElementType)env->GetIntField(je, g_elem.type);
        elems[i].shape = (ElementShape)env->GetIntField(je, g_elem.shape);
        elems[i].x = env->GetIntField(je, g_elem.x);
        elems[i].y = env->GetIntField(je, g_elem.y);
        elems[i].w = env->GetFloatField(je, g_elem.w);
        elems[i].h = env->GetFloatField(je, g_elem.h);
        elems[i].scale = env->GetFloatField(je, g_elem.scale);
        elems[i].passthrough_touch = env->GetBooleanField(je, g_elem.passthroughTouch);
        elems[i].activation_mode = (ActivationMode)env->GetIntField(je, g_elem.activationMode);
        elems[i].toggle_switch = env->GetBooleanField(je, g_elem.toggleSwitch);
        elems[i].auto_repeat = env->GetBooleanField(je, g_elem.autoRepeat);
        elems[i].auto_repeat_interval_ms = env->GetIntField(je, g_elem.autoRepeatIntervalMs);
        if (elems[i].auto_repeat_interval_ms <= 0) elems[i].auto_repeat_interval_ms = 100;

        elems[i].range_ordinal = env->GetIntField(je, g_elem.rangeOrdinal);
        elems[i].range_max = env->GetIntField(je, g_elem.rangeMax);
        elems[i].range_binding_count = env->GetIntField(je, g_elem.bindingCount);
        elems[i].range_orientation = env->GetIntField(je, g_elem.orientation);

        elems[i].button_long_press_haptic = env->GetIntField(je, g_elem.buttonLongPressHaptic);
        elems[i].button_gesture_haptic = env->GetIntField(je, g_elem.buttonGestureHaptic);

        elems[i].element_long_press_count = read_binding_list(env,
            (jintArray)env->GetObjectField(je, g_elem.elementLongPress), elems[i].element_long_press, 8);
        elems[i].element_gesture_count = read_binding_list(env,
            (jintArray)env->GetObjectField(je, g_elem.elementGesture), elems[i].element_gesture, 8);
        jintArray bindingTypes = (jintArray)env->GetObjectField(je, g_elem.bindingTypes);
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

        {
            jintArray stickyArr = (jintArray)env->GetObjectField(je, g_elem.bindingSticky);
            if (stickyArr) {
                jint* sticky = env->GetIntArrayElements(stickyArr, NULL);
                jsize stickyLen = env->GetArrayLength(stickyArr);
                int mask = 0;
                for (int j = 0; j < stickyLen && j < 4; j++) {
                    if (sticky[j] != 0) mask |= (1 << j);
                }
                elems[i].primary_sticky_mask = mask;
                env->ReleaseIntArrayElements(stickyArr, sticky, JNI_ABORT);
            } else {
                elems[i].primary_sticky_mask = 0;
            }
        }

        elems[i].current_ptr_id = -1;
        elems[i].engaged = false;
        for (int p = 0; p < MAX_PETALS; p++) elems[i].petal_active[p] = false;

        env->DeleteLocalRef(je);
    }

    touch_processor_set_elements(elems, len);
}

static void nativeSetSnappingSize(JNIEnv* env, jclass clazz, jfloat size) {
    touch_processor_set_snapping_size(size);
}

static void nativeSetResolutionScale(JNIEnv* env, jclass clazz, jfloat scale) {
    touch_processor_set_resolution_scale(scale);
}

static void nativeSetSimTouchScreen(JNIEnv* env, jclass clazz, jboolean enabled) {
    touch_processor_set_sim_touch_screen(enabled == JNI_TRUE);
}

static void nativeOnFingerDown(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y,
                                jlong timeMs) {
    TouchActionResult r = touch_processor_on_finger_down(ptrId, x, y, (uint64_t)timeMs);
    visual_state_flush();
    dispatch_actions_batch(env, &r);
}

static void nativeOnFingerMove(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y,
                                jlong timeMs) {
    TouchActionResult r = touch_processor_on_finger_move(ptrId, x, y, (uint64_t)timeMs);
    visual_state_flush();
    dispatch_actions_batch(env, &r);
}

static void nativeOnFingerUp(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y,
                              jlong timeMs) {
    TouchActionResult r = touch_processor_on_finger_up(ptrId, x, y, (uint64_t)timeMs);
    visual_state_flush();
    dispatch_actions_batch(env, &r);
}

static void nativeTick(JNIEnv* env, jclass clazz, jlong timeMs) {
    TouchActionResult r = touch_processor_tick((uint64_t)timeMs);
    visual_state_flush();
    dispatch_actions_batch(env, &r);
}

static void nativeReset(JNIEnv* env, jclass clazz) {
    touch_processor_reset();
    memset(g_visual_buffer, 0, sizeof(g_visual_buffer));
}

static jobject nativeGetVisualBuffer(JNIEnv* env, jclass clazz) {
    if (g_visual_buffer_ref == NULL) {
        g_visual_buffer_ref = env->NewGlobalRef(
            env->NewDirectByteBuffer(g_visual_buffer, sizeof(g_visual_buffer)));
    }
    return g_visual_buffer_ref;
}

static jboolean nativeIsPassthroughActive(JNIEnv* env, jclass clazz) {
    return touch_processor_is_passthrough_active() ? JNI_TRUE : JNI_FALSE;
}

static void nativeUpdateConfig(JNIEnv* env, jclass clazz, jobject config) {
    TouchProcessorConfig c;
    memset(&c, 0, sizeof(c));

    c.touch_mode = (TouchMode)env->GetIntField(config, g_config.touchMode);
    c.input_mode = (InputMode)env->GetIntField(config, g_config.inputMode);
    c.long_press_timeout_ms = env->GetIntField(config, g_config.longPressTimeoutMs);
    c.double_tap_timeout_ms = env->GetIntField(config, g_config.doubleTapTimeoutMs);
    c.single_tap_delay_ms = env->GetIntField(config, g_config.singleTapDelayMs);
    c.drag_threshold_px = env->GetIntField(config, g_config.dragThresholdPx);
    c.double_tap_distance_px = env->GetIntField(config, g_config.doubleTapDistancePx);
    c.binding_delay_ms = env->GetIntField(config, g_config.bindingDelayMs);
    c.long_press_delay_ms = env->GetIntField(config, g_config.longPressDelayMs);
    c.cursor_speed = env->GetIntField(config, g_config.cursorSpeed);
    c.gesture_threshold_px = env->GetIntField(config, g_config.gestureThresholdPx);
    c.cursor_acceleration_threshold = env->GetIntField(config, g_config.cursorAccelerationThreshold);
    c.cursor_acceleration_factor = env->GetFloatField(config, g_config.cursorAccelerationFactor);
    c.screen_w = env->GetIntField(config, g_config.screenW);
    c.screen_h = env->GetIntField(config, g_config.screenH);
    c.xform_scale_x = env->GetFloatField(config, g_config.xformScaleX);
    if (c.xform_scale_x <= 0.0f) c.xform_scale_x = 1.0f;
    c.xform_scale_y = env->GetFloatField(config, g_config.xformScaleY);
    if (c.xform_scale_y <= 0.0f) c.xform_scale_y = 1.0f;
    c.view_offset_x = env->GetFloatField(config, g_config.viewOffsetX);
    c.view_offset_y = env->GetFloatField(config, g_config.viewOffsetY);
    c.gesture_long_press_haptic = env->GetIntField(config, g_config.gestureLongPressHaptic);
    c.haptic_enabled = env->GetBooleanField(config, g_config.hapticEnabled);

    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTap, c.ts_single_tap, c.ts_single_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsLongPress, c.ts_long_press, c.ts_long_press_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTap, c.ts_double_tap, c.ts_double_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTapDrag, c.ts_single_tap_drag, c.ts_single_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsLongPressDrag, c.ts_long_press_drag, c.ts_long_press_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTapDrag, c.ts_double_tap_drag, c.ts_double_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTap2nd, c.ts_single_2nd, c.ts_single_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTap2nd, c.ts_double_2nd, c.ts_double_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsSingleTapDrag2nd, c.ts_single_drag_2nd, c.ts_single_drag_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tsDoubleTapDrag2nd, c.ts_double_drag_2nd, c.ts_double_drag_2nd_count);

    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTap, c.tp_single_tap, c.tp_single_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpLongPress, c.tp_long_press, c.tp_long_press_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTap, c.tp_double_tap, c.tp_double_tap_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTapDrag, c.tp_single_tap_drag, c.tp_single_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpLongPressDrag, c.tp_long_press_drag, c.tp_long_press_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTapDrag, c.tp_double_tap_drag, c.tp_double_tap_drag_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTap2nd, c.tp_single_2nd, c.tp_single_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTap2nd, c.tp_double_2nd, c.tp_double_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpSingleTapDrag2nd, c.tp_single_drag_2nd, c.tp_single_drag_2nd_count);
    READ_CACHED_BINDING_LIST(env, config, g_config.tpDoubleTapDrag2nd, c.tp_double_drag_2nd, c.tp_double_drag_2nd_count);

#define APPLY_STICKY(arr, cnt, sid) do { \
    int _st = env->GetIntField(config, g_config.sid); \
    for (int _i = 0; _i < (cnt); _i++) if (_st & (1 << _i)) (arr)[_i].modifiers = 1; \
} while(0)
    APPLY_STICKY(c.ts_single_tap, c.ts_single_tap_count, stSingleTap);
    APPLY_STICKY(c.ts_long_press, c.ts_long_press_count, stLongPress);
    APPLY_STICKY(c.ts_double_tap, c.ts_double_tap_count, stDoubleTap);
    APPLY_STICKY(c.ts_single_tap_drag, c.ts_single_tap_drag_count, stSingleTapDrag);
    APPLY_STICKY(c.ts_long_press_drag, c.ts_long_press_drag_count, stLongPressDrag);
    APPLY_STICKY(c.ts_double_tap_drag, c.ts_double_tap_drag_count, stDoubleTapDrag);
    APPLY_STICKY(c.ts_single_2nd, c.ts_single_2nd_count, stSingleTap2nd);
    APPLY_STICKY(c.ts_double_2nd, c.ts_double_2nd_count, stDoubleTap2nd);
    APPLY_STICKY(c.ts_single_drag_2nd, c.ts_single_drag_2nd_count, stSingleTapDrag2nd);
    APPLY_STICKY(c.ts_double_drag_2nd, c.ts_double_drag_2nd_count, stDoubleTapDrag2nd);

    APPLY_STICKY(c.tp_single_tap, c.tp_single_tap_count, stSingleTap);
    APPLY_STICKY(c.tp_long_press, c.tp_long_press_count, stLongPress);
    APPLY_STICKY(c.tp_double_tap, c.tp_double_tap_count, stDoubleTap);
    APPLY_STICKY(c.tp_single_tap_drag, c.tp_single_tap_drag_count, stSingleTapDrag);
    APPLY_STICKY(c.tp_long_press_drag, c.tp_long_press_drag_count, stLongPressDrag);
    APPLY_STICKY(c.tp_double_tap_drag, c.tp_double_tap_drag_count, stDoubleTapDrag);
    APPLY_STICKY(c.tp_single_2nd, c.tp_single_2nd_count, stSingleTap2nd);
    APPLY_STICKY(c.tp_double_2nd, c.tp_double_2nd_count, stDoubleTap2nd);
    APPLY_STICKY(c.tp_single_drag_2nd, c.tp_single_drag_2nd_count, stSingleTapDrag2nd);
    APPLY_STICKY(c.tp_double_drag_2nd, c.tp_double_drag_2nd_count, stDoubleTapDrag2nd);
#undef APPLY_STICKY

    touch_processor_update_config(&c);
}

static void nativeSetXformScale(JNIEnv* env, jclass clazz, jfloat scaleX, jfloat scaleY) {
    touch_processor_set_xform_scale(scaleX, scaleY);
}

static void nativeSetViewOffset(JNIEnv* env, jclass clazz, jfloat offsetX, jfloat offsetY) {
    touch_processor_set_view_offset(offsetX, offsetY);
}

static jboolean nativeGetElementState(JNIEnv* env, jclass clazz, jint elemIndex, jfloatArray outXY) {
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

static jboolean nativeHandleDownByMode(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs) {
    return touch_processor_handle_down_by_mode(ptrId, x, y, (uint64_t)timeMs) ? JNI_TRUE : JNI_FALSE;
}

static jboolean nativeHandleUpByMode(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs) {
    return touch_processor_handle_up_by_mode(ptrId, x, y, (uint64_t)timeMs) ? JNI_TRUE : JNI_FALSE;
}

static void nativeHandleMoveByMode(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y, jlong timeMs) {
    touch_processor_handle_move_by_mode(ptrId, x, y, (uint64_t)timeMs);
}

static jint nativeTrackedCount(JNIEnv* env, jclass clazz, jint ptrId) {
    return (jint)touch_processor_tracked_count(ptrId);
}

static jint nativeHoveredIndex(JNIEnv* env, jclass clazz, jint ptrId) {
    return (jint)touch_processor_hovered_index(ptrId);
}

static void nativeRegisterDispatcher(JNIEnv* env, jclass clazz, jobject dispatcher) {
    if (g_dispatch_obj) {
        env->DeleteGlobalRef(g_dispatch_obj);
        g_dispatch_obj = NULL;
    }
    if (!dispatcher) return;
    g_dispatch_obj = env->NewGlobalRef(dispatcher);
    jclass dcls = env->GetObjectClass(dispatcher);
    g_injectPointerMove = env->GetMethodID(dcls, "injectPointerMove", "(II)V");
    g_injectPointerMoveDelta = env->GetMethodID(dcls, "injectPointerMoveDelta", "(II)V");
    g_injectPointerButtonPress = env->GetMethodID(dcls, "injectPointerButtonPress", "(I)V");
    g_injectPointerButtonRelease = env->GetMethodID(dcls, "injectPointerButtonRelease", "(I)V");
    g_injectKeyPress = env->GetMethodID(dcls, "injectKeyPress", "(IZ)V");
    g_injectKeyRelease = env->GetMethodID(dcls, "injectKeyRelease", "(IZ)V");
    g_mouseEvent = env->GetMethodID(dcls, "mouseEvent", "(III)V");
    g_scrollEvent = env->GetMethodID(dcls, "scrollEvent", "(I)V");
    g_hapticEvent = env->GetMethodID(dcls, "hapticEvent", "(I)V");
    g_setCursorSpeed = env->GetMethodID(dcls, "setCursorSpeed", "(I)V");
    g_startMouseMove = env->GetMethodID(dcls, "startMouseMove", "(III)V");
    g_stopMouseMove = env->GetMethodID(dcls, "stopMouseMove", "()V");
    g_gamepadState = env->GetMethodID(dcls, "gamepadState", "(IZ)V");
    g_gamepadAxis = env->GetMethodID(dcls, "gamepadAxis", "(III)V");
    g_dispatchAllActions = env->GetMethodID(dcls, "dispatchAllActions", "([I[II)V");
    env->DeleteLocalRef(dcls);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jclass clazz = env->FindClass("com/winlator/cmod/inputcontrols/NativeTouchProcessor");
    if (!clazz) {
        return JNI_ERR;
    }

    JNINativeMethod methods[] = {
        {"nativeInit", "(Lcom/winlator/cmod/inputcontrols/NativeTouchProcessor$NativeConfig;)V", (void*)nativeInit},
        {"nativeSetElements", "([Lcom/winlator/cmod/inputcontrols/NativeTouchProcessor$NativeElement;)V", (void*)nativeSetElements},
        {"nativeOnFingerDown", "(IFFJ)V", (void*)nativeOnFingerDown},
        {"nativeOnFingerMove", "(IFFJ)V", (void*)nativeOnFingerMove},
        {"nativeOnFingerUp", "(IFFJ)V", (void*)nativeOnFingerUp},
        {"nativeTick", "(J)V", (void*)nativeTick},
        {"nativeHandleDownByMode", "(IFFJ)Z", (void*)nativeHandleDownByMode},
        {"nativeHandleUpByMode", "(IFFJ)Z", (void*)nativeHandleUpByMode},
        {"nativeHandleMoveByMode", "(IFFJ)V", (void*)nativeHandleMoveByMode},
        {"nativeReset", "()V", (void*)nativeReset},
        {"nativeGetVisualBuffer", "()Ljava/nio/ByteBuffer;", (void*)nativeGetVisualBuffer},
        {"nativeIsPassthroughActive", "()Z", (void*)nativeIsPassthroughActive},
        {"nativeSetSnappingSize", "(F)V", (void*)nativeSetSnappingSize},
        {"nativeSetResolutionScale", "(F)V", (void*)nativeSetResolutionScale},
        {"nativeSetSimTouchScreen", "(Z)V", (void*)nativeSetSimTouchScreen},
        {"nativeSetXformScale", "(FF)V", (void*)nativeSetXformScale},
        {"nativeSetViewOffset", "(FF)V", (void*)nativeSetViewOffset},
        {"nativeGetElementState", "(I[F)Z", (void*)nativeGetElementState},
        {"nativeTrackedCount", "(I)I", (void*)nativeTrackedCount},
        {"nativeHoveredIndex", "(I)I", (void*)nativeHoveredIndex},
        {"nativeRegisterDispatcher", "(Ljava/lang/Object;)V", (void*)nativeRegisterDispatcher},
        {"nativeUpdateConfig", "(Lcom/winlator/cmod/inputcontrols/NativeTouchProcessor$NativeConfig;)V", (void*)nativeUpdateConfig},
    };

    int result = env->RegisterNatives(clazz, methods, sizeof(methods)/sizeof(methods[0]));
    env->DeleteLocalRef(clazz);

    if (result != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

} // extern "C"
