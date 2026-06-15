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
    int32_t visual_flags;       // packed: VF_TAP|VF_LONG_TAP|VF_GESTURE | petal[0-3]<<3 | layers<<7
    int32_t petal_active[4];    // 0 or 1 each
    float range_scroll_offset;
    int32_t visual_long_press;   // 0 or 1 — long press activation visual
    // --- Render fields (Phase 1: native rendering support) ---
    float opacity;
    float corner_radius;
    uint32_t color_primary;
    uint32_t color_secondary;
    float stroke_width;
    int32_t fill_alpha_inactive;
} VisualStateEntry;
#pragma pack(pop)

#define VISUAL_STRIDE ((int)sizeof(VisualStateEntry))

static VisualStateEntry g_visual_buffer[MAX_ELEMENTS];
static jobject g_visual_buffer_ref = NULL;

static void visual_state_flush(void) {
    uint32_t any = 0;
    for (int b = 0; b < 4; b++) any |= g_state.visual_dirty_mask[b];
    if (__builtin_expect(!any, 1)) return;

    int n = g_state.element_count;
    int active_count = 0;
    for (int b = 0; b < 4; b++) {
        uint32_t mask = g_state.visual_dirty_mask[b];
        if (!mask) continue;
        g_state.visual_dirty_mask[b] = 0;
        int base = b * 32;
        do {
            int bit = __builtin_ctz(mask);
            int idx = base + bit;
            if (idx >= n) break;
            TouchElement* e = &g_state.elements[idx];
            g_visual_buffer[idx].visual_x = e->visual_x;
            g_visual_buffer[idx].visual_y = e->visual_y;
            g_visual_buffer[idx].visual_flags = e->visual_flags;
            g_visual_buffer[idx].petal_active[0] = e->petal_active[0] ? 1 : 0;
            g_visual_buffer[idx].petal_active[1] = e->petal_active[1] ? 1 : 0;
            g_visual_buffer[idx].petal_active[2] = e->petal_active[2] ? 1 : 0;
            g_visual_buffer[idx].petal_active[3] = e->petal_active[3] ? 1 : 0;
            g_visual_buffer[idx].range_scroll_offset = e->range_scroll_offset;
            g_visual_buffer[idx].visual_long_press = (e->visual_flags & VF_LONG_TAP) ? 1 : 0;
            g_visual_buffer[idx].opacity = e->opacity;
            g_visual_buffer[idx].corner_radius = e->corner_radius;
            g_visual_buffer[idx].color_primary = e->color_primary;
            g_visual_buffer[idx].color_secondary = e->color_secondary;
            g_visual_buffer[idx].stroke_width = e->stroke_width;
            g_visual_buffer[idx].fill_alpha_inactive = e->fill_alpha_inactive;
            if (e->visual_flags) {
                active_count++;
            }
            mask &= mask - 1;
        } while (mask);
    }

}

// Bulk write all elements' visual state into Java arrays (1 JNI call replaces N×8 ByteBuffer reads)
static jint nativeSyncVisualState(JNIEnv* env, jclass clazz, jfloatArray positions, jintArray states, jfloatArray scrollOffsets, jintArray visualLayers, jintArray visualAlphas) {
    int n = g_state.element_count;
    jfloat* pos = env->GetFloatArrayElements(positions, NULL);
    jint* st = env->GetIntArrayElements(states, NULL);
    jint* vl = visualLayers ? env->GetIntArrayElements(visualLayers, NULL) : NULL;
    jint* va = visualAlphas ? env->GetIntArrayElements(visualAlphas, NULL) : NULL;
    jfloat* scroll = scrollOffsets ? env->GetFloatArrayElements(scrollOffsets, NULL) : NULL;
    int max = env->GetArrayLength(positions) / 2;
    if (n > max) n = max;
    int active_count = 0;
    for (int i = 0; i < n; i++) {
        TouchElement* e = &g_state.elements[i];
        pos[i*2] = e->visual_x;
        pos[i*2+1] = e->visual_y;
        // Pack flags: bits 0-2 = visual_flags (VF_TAP|VF_LONG_TAP|VF_GESTURE)
        //             bits 3-6 = petal_active[0-3]
        //             bits 7-10 = visual layers (FILL|STROKE_TEXT|GLOW|OUTER_STROKE)
        //             bit 11 = long press active (VF_LONG_TAP + has LP bindings)
        int flags = e->visual_flags & 0x7;
        if (e->petal_active[0]) flags |= (1 << 3);
        if (e->petal_active[1]) flags |= (1 << 4);
        if (e->petal_active[2]) flags |= (1 << 5);
        if (e->petal_active[3]) flags |= (1 << 6);
        flags |= ((e->visual_layers & 0xF) << 7);
        if (e->element_long_press_count > 0 && (e->visual_flags & VF_LONG_TAP)) flags |= (1 << 11);
        if (e->visual_flags) active_count++;
        if (vl) vl[i] = (int)e->visual_layers;
        if (va) va[i] = (int)e->visual_alpha;
        if (scroll) scroll[i] = e->range_scroll_offset;

        st[i] = flags;
    }
    env->ReleaseFloatArrayElements(positions, pos, 0);
    env->ReleaseIntArrayElements(states, st, 0);
    if (vl) env->ReleaseIntArrayElements(visualLayers, vl, 0);
    if (va) env->ReleaseIntArrayElements(visualAlphas, va, 0);
    if (scroll) env->ReleaseFloatArrayElements(scrollOffsets, scroll, 0);
    return (jint)n;
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
    jfieldID scrollBindHaptic;
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

    // Toggle bitmasks for gesture bindings
    jfieldID tgSingleTap;
    jfieldID tgLongPress;
    jfieldID tgDoubleTap;
    jfieldID tgSingleTapDrag;
    jfieldID tgLongPressDrag;
    jfieldID tgDoubleTapDrag;
    jfieldID tgSingleTap2nd;
    jfieldID tgDoubleTap2nd;
    jfieldID tgSingleTapDrag2nd;
    jfieldID tgDoubleTapDrag2nd;

    // Auto-repeat bitmasks for gesture bindings
    jfieldID arSingleTap;
    jfieldID arLongPress;
    jfieldID arDoubleTap;
    jfieldID arSingleTapDrag;
    jfieldID arLongPressDrag;
    jfieldID arDoubleTapDrag;
    jfieldID arSingleTap2nd;
    jfieldID arDoubleTap2nd;
    jfieldID arSingleTapDrag2nd;
    jfieldID arDoubleTapDrag2nd;

    // Auto-repeat interval fields for gesture bindings
    jfieldID arSingleTapIntervalMs;
    jfieldID arLongPressIntervalMs;
    jfieldID arDoubleTapIntervalMs;
    jfieldID arSingleTapDragIntervalMs;
    jfieldID arLongPressDragIntervalMs;
    jfieldID arDoubleTapDragIntervalMs;
    jfieldID arSingleTap2ndIntervalMs;
    jfieldID arDoubleTap2ndIntervalMs;
    jfieldID arSingleTapDrag2ndIntervalMs;
    jfieldID arDoubleTapDrag2ndIntervalMs;

    // Render config fields
    jfieldID colorPrimary;
    jfieldID colorSecondary;
    jfieldID strokeWidthDefault;
    jfieldID fillAlphaInactiveDefault;

    // Scroll mode binding fields
    jfieldID scrollBindingsUp;
    jfieldID scrollBindingsDown;
    jfieldID scrollBindingsLeft;
    jfieldID scrollBindingsRight;
    jfieldID scrollThresholdPx;
    jfieldID scrollHoldThresholdPx;
    jfieldID scrollHoldV;
    jfieldID scrollHoldH;
    jfieldID scrollHoldHaptic;
};

#define JNI_CHECK(env, msg) do { \
    if ((env)->ExceptionCheck()) { \
        (env)->ExceptionDescribe(); \
        (env)->ExceptionClear(); \
        return; \
    } \
} while(0)

#define JNI_CHECK_INT(env, msg, retval) do { \
    if ((env)->ExceptionCheck()) { \
        (env)->ExceptionDescribe(); \
        (env)->ExceptionClear(); \
        return retval; \
    } \
} while(0)

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
    jfieldID elementLongPress;
    jfieldID elementGesture;
    jfieldID bindingTypes;
    jfieldID bindingSticky;
    jfieldID bindingToggle;
    jfieldID bindingAutoRepeat;
    jfieldID bindingAutoRepeatIntervalMs;
    jfieldID longPressToggleBitmask;
    jfieldID gestureToggleBitmask;
    jfieldID rangeOrdinal;
    jfieldID rangeMax;
    jfieldID bindingCount;
    jfieldID orientation;
    jfieldID buttonLongPressHaptic;
    jfieldID buttonGestureHaptic;
    // Render fields
    jfieldID cornerRadius;
    jfieldID opacity;
};

static CachedElementFieldIDs g_elem;

static jobject g_dispatch_obj = NULL;
static jmethodID g_dispatchAllActions;
static jintArray g_dispatch_packed = NULL;
#define DISPATCH_MAX_ACTIONS 64



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


static void dispatch_actions_batch(JNIEnv* env, const TouchActionResult* r) {
    if (!g_dispatch_obj || !r || r->count == 0 || !g_dispatch_packed) {
        return;
    }
    int n = r->count;
    if (n > DISPATCH_MAX_ACTIONS) n = DISPATCH_MAX_ACTIONS;

    int* packed = env->GetIntArrayElements(g_dispatch_packed, NULL);
    for (int i = 0; i < n; i++) {
        int base = i * 4;
        int type = (int)r->actions[i].type;
        packed[base] = type;
        switch (type) {
            case ACT_POINTER_MOVE:
                packed[base+1] = r->actions[i].pointer_move.x;
                packed[base+2] = r->actions[i].pointer_move.y;
                packed[base+3] = 0;
                break;
            case ACT_POINTER_MOVE_DELTA:
                packed[base+1] = r->actions[i].pointer_delta.dx;
                packed[base+2] = r->actions[i].pointer_delta.dy;
                packed[base+3] = 0;
                break;
            case ACT_POINTER_BUTTON_PRESS:
                packed[base+1] = r->actions[i].pointer_button.button;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
            case ACT_POINTER_BUTTON_RELEASE:
                packed[base+1] = r->actions[i].pointer_button.button;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
            case ACT_KEY_PRESS:
                packed[base+1] = r->actions[i].key.keycode;
                packed[base+2] = r->actions[i].key.is_down ? 1 : 0;
                packed[base+3] = 0;
                break;
            case ACT_KEY_RELEASE:
                packed[base+1] = r->actions[i].key.keycode;
                packed[base+2] = r->actions[i].key.is_down ? 1 : 0;
                packed[base+3] = 0;
                break;
            case ACT_MOUSE_EVENT:
                packed[base+1] = r->actions[i].mouse_event.flags;
                packed[base+2] = r->actions[i].mouse_event.dx;
                packed[base+3] = r->actions[i].mouse_event.dy;
                break;
            case ACT_SCROLL:
                packed[base+1] = r->actions[i].scroll.amount;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
            case ACT_HAPTIC:
                packed[base+1] = r->actions[i].haptic.effect;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
            case ACT_SET_CURSOR_SPEED:
                packed[base+1] = r->actions[i].cursor_speed.speed;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
            case ACT_START_MOUSE_MOVE:
                packed[base+1] = r->actions[i].mouse_move.dx;
                packed[base+2] = r->actions[i].mouse_move.dy;
                packed[base+3] = r->actions[i].mouse_move.hold;
                break;
            case ACT_STOP_MOUSE_MOVE:
                packed[base+1] = 0;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
            case ACT_GAMEPAD_STATE:
                packed[base+1] = r->actions[i].key.keycode;
                packed[base+2] = r->actions[i].key.is_down ? 1 : 0;
                packed[base+3] = 0;
                break;
            case ACT_GAMEPAD_RELEASE:
                packed[base+1] = 0;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
            case ACT_GAMEPAD_AXIS:
                packed[base+1] = r->actions[i].gamepad_axis.is_left;
                packed[base+2] = r->actions[i].gamepad_axis.axis_x;
                packed[base+3] = r->actions[i].gamepad_axis.axis_y;
                break;
            default:
                packed[base] = 0;
                packed[base+1] = 0;
                packed[base+2] = 0;
                packed[base+3] = 0;
                break;
        }
    }

    for (int i = 0; i < n; i++) {
        const char* act_name = "UNK";
        switch (packed[i*4]) {
            case 3: act_name = "PTR_BTN_PRESS"; break;
            case 4: act_name = "PTR_BTN_RELEASE"; break;
            case 5: act_name = "KEY_PRESS"; break;
            case 6: act_name = "KEY_RELEASE"; break;
            case 7: act_name = "MOUSE_EVENT"; break;
        }
        __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "DISPATCH act[%d] type=%d arg0=%d (%s)", i, packed[i*4], packed[i*4+1], act_name);
    }
    env->ReleaseIntArrayElements(g_dispatch_packed, packed, 0);
    env->CallVoidMethod(g_dispatch_obj, g_dispatchAllActions, n);
    JNI_CHECK(env, "dispatchAllActions");
}

extern "C" {

void touch_processor_set_view_offset(float offset_x, float offset_y) {
    g_state.cfg.view_offset_x = offset_x;
    g_state.cfg.view_offset_y = offset_y;
}

static inline void apply_modifier_mask(JNIEnv* env, jobject config,
    jfieldID fid, GestureBindingSlot* slot, int modifier_value) {
    int mask = env->GetIntField(config, fid);
    for (int j = 0; j < slot->count; j++)
        if (mask & (1 << j))
            slot->arr[j].modifiers = modifier_value;
}

static inline void apply_toggle_mask(JNIEnv* env, jobject config,
    jfieldID fid, GestureBindingSlot* slot) {
    int mask = env->GetIntField(config, fid);
    for (int j = 0; j < slot->count; j++)
        if (mask & (1 << j))
            slot->arr[j].toggle = true;
}

static inline void apply_auto_repeat_mask(JNIEnv* env, jobject config,
    jfieldID fid, GestureBindingSlot* slot, jfieldID interval_fid) {
    int mask = env->GetIntField(config, fid);
    int interval = env->GetIntField(config, interval_fid);
    if (interval <= 0) interval = 100;
    for (int j = 0; j < slot->count; j++) {
        if (mask & (1 << j))
            slot->arr[j].auto_repeat = true;
        slot->arr[j].auto_repeat_interval_ms = interval;
    }
}

static void read_config_from_java(JNIEnv* env, jobject config, TouchProcessorConfig* c) {
    c->touch_mode = (TouchMode)env->GetIntField(config, g_config.touchMode);
    c->input_mode = (InputMode)env->GetIntField(config, g_config.inputMode);
    c->long_press_timeout_ms = env->GetIntField(config, g_config.longPressTimeoutMs);
    c->double_tap_timeout_ms = env->GetIntField(config, g_config.doubleTapTimeoutMs);
    c->single_tap_delay_ms = env->GetIntField(config, g_config.singleTapDelayMs);
    c->drag_threshold_px = env->GetIntField(config, g_config.dragThresholdPx);
    c->double_tap_distance_px = env->GetIntField(config, g_config.doubleTapDistancePx);
    c->binding_delay_ms = env->GetIntField(config, g_config.bindingDelayMs);
    c->long_press_delay_ms = env->GetIntField(config, g_config.longPressDelayMs);
    c->cursor_speed = env->GetIntField(config, g_config.cursorSpeed);
    c->gesture_threshold_px = env->GetIntField(config, g_config.gestureThresholdPx);
    c->cursor_acceleration_threshold = env->GetIntField(config, g_config.cursorAccelerationThreshold);
    c->cursor_acceleration_factor = env->GetFloatField(config, g_config.cursorAccelerationFactor);
    c->screen_w = env->GetIntField(config, g_config.screenW);
    c->screen_h = env->GetIntField(config, g_config.screenH);
    c->xform_scale_x = env->GetFloatField(config, g_config.xformScaleX);
    if (c->xform_scale_x <= 0.0f) c->xform_scale_x = 1.0f;
    c->xform_scale_y = env->GetFloatField(config, g_config.xformScaleY);
    if (c->xform_scale_y <= 0.0f) c->xform_scale_y = 1.0f;
    c->view_offset_x = env->GetFloatField(config, g_config.viewOffsetX);
    c->view_offset_y = env->GetFloatField(config, g_config.viewOffsetY);
    c->gesture_long_press_haptic = env->GetIntField(config, g_config.gestureLongPressHaptic);
    c->scroll_bind_haptic = env->GetIntField(config, g_config.scrollBindHaptic);
    c->haptic_enabled = env->GetBooleanField(config, g_config.hapticEnabled);

    struct BindingSlotPair { GestureBindingSlot* slot; jfieldID fid; };
    BindingSlotPair binding_slots[] = {
        { &c->ts[GESTURE_SINGLE_TAP], g_config.tsSingleTap },
        { &c->ts[GESTURE_LONG_PRESS], g_config.tsLongPress },
        { &c->ts[GESTURE_DOUBLE_TAP], g_config.tsDoubleTap },
        { &c->ts[GESTURE_SINGLE_TAP_DRAG], g_config.tsSingleTapDrag },
        { &c->ts[GESTURE_LONG_PRESS_DRAG], g_config.tsLongPressDrag },
        { &c->ts[GESTURE_DOUBLE_TAP_DRAG], g_config.tsDoubleTapDrag },
        { &c->ts[GESTURE_SINGLE_2ND], g_config.tsSingleTap2nd },
        { &c->ts[GESTURE_DOUBLE_2ND], g_config.tsDoubleTap2nd },
        { &c->ts[GESTURE_SINGLE_DRAG_2ND], g_config.tsSingleTapDrag2nd },
        { &c->ts[GESTURE_DOUBLE_DRAG_2ND], g_config.tsDoubleTapDrag2nd },
        { &c->tp[GESTURE_SINGLE_TAP], g_config.tpSingleTap },
        { &c->tp[GESTURE_LONG_PRESS], g_config.tpLongPress },
        { &c->tp[GESTURE_DOUBLE_TAP], g_config.tpDoubleTap },
        { &c->tp[GESTURE_SINGLE_TAP_DRAG], g_config.tpSingleTapDrag },
        { &c->tp[GESTURE_LONG_PRESS_DRAG], g_config.tpLongPressDrag },
        { &c->tp[GESTURE_DOUBLE_TAP_DRAG], g_config.tpDoubleTapDrag },
        { &c->tp[GESTURE_SINGLE_2ND], g_config.tpSingleTap2nd },
        { &c->tp[GESTURE_DOUBLE_2ND], g_config.tpDoubleTap2nd },
        { &c->tp[GESTURE_SINGLE_DRAG_2ND], g_config.tpSingleTapDrag2nd },
        { &c->tp[GESTURE_DOUBLE_DRAG_2ND], g_config.tpDoubleTapDrag2nd },
    };
    for (int i = 0; i < (int)(sizeof(binding_slots)/sizeof(binding_slots[0])); i++) {
        jintArray arr = (jintArray)env->GetObjectField(config, binding_slots[i].fid);
        binding_slots[i].slot->count = read_binding_list(env, arr, binding_slots[i].slot->arr, 8);
    }

    struct StickySlotPair { jfieldID fid; GestureBindingSlot* slot; };
    StickySlotPair sticky_slots[] = {
        { g_config.stSingleTap, &c->ts[GESTURE_SINGLE_TAP] },
        { g_config.stLongPress, &c->ts[GESTURE_LONG_PRESS] },
        { g_config.stDoubleTap, &c->ts[GESTURE_DOUBLE_TAP] },
        { g_config.stSingleTapDrag, &c->ts[GESTURE_SINGLE_TAP_DRAG] },
        { g_config.stLongPressDrag, &c->ts[GESTURE_LONG_PRESS_DRAG] },
        { g_config.stDoubleTapDrag, &c->ts[GESTURE_DOUBLE_TAP_DRAG] },
        { g_config.stSingleTap2nd, &c->ts[GESTURE_SINGLE_2ND] },
        { g_config.stDoubleTap2nd, &c->ts[GESTURE_DOUBLE_2ND] },
        { g_config.stSingleTapDrag2nd, &c->ts[GESTURE_SINGLE_DRAG_2ND] },
        { g_config.stDoubleTapDrag2nd, &c->ts[GESTURE_DOUBLE_DRAG_2ND] },
        { g_config.stSingleTap, &c->tp[GESTURE_SINGLE_TAP] },
        { g_config.stLongPress, &c->tp[GESTURE_LONG_PRESS] },
        { g_config.stDoubleTap, &c->tp[GESTURE_DOUBLE_TAP] },
        { g_config.stSingleTapDrag, &c->tp[GESTURE_SINGLE_TAP_DRAG] },
        { g_config.stLongPressDrag, &c->tp[GESTURE_LONG_PRESS_DRAG] },
        { g_config.stDoubleTapDrag, &c->tp[GESTURE_DOUBLE_TAP_DRAG] },
        { g_config.stSingleTap2nd, &c->tp[GESTURE_SINGLE_2ND] },
        { g_config.stDoubleTap2nd, &c->tp[GESTURE_DOUBLE_2ND] },
        { g_config.stSingleTapDrag2nd, &c->tp[GESTURE_SINGLE_DRAG_2ND] },
        { g_config.stDoubleTapDrag2nd, &c->tp[GESTURE_DOUBLE_DRAG_2ND] },
    };
    for (int i = 0; i < (int)(sizeof(sticky_slots)/sizeof(sticky_slots[0])); i++)
        apply_modifier_mask(env, config, sticky_slots[i].fid, sticky_slots[i].slot, 1);

    struct ToggleSlotPair { jfieldID fid; GestureBindingSlot* slot; };
    ToggleSlotPair toggle_slots[] = {
        { g_config.tgSingleTap, &c->ts[GESTURE_SINGLE_TAP] },
        { g_config.tgLongPress, &c->ts[GESTURE_LONG_PRESS] },
        { g_config.tgDoubleTap, &c->ts[GESTURE_DOUBLE_TAP] },
        { g_config.tgSingleTapDrag, &c->ts[GESTURE_SINGLE_TAP_DRAG] },
        { g_config.tgLongPressDrag, &c->ts[GESTURE_LONG_PRESS_DRAG] },
        { g_config.tgDoubleTapDrag, &c->ts[GESTURE_DOUBLE_TAP_DRAG] },
        { g_config.tgSingleTap2nd, &c->ts[GESTURE_SINGLE_2ND] },
        { g_config.tgDoubleTap2nd, &c->ts[GESTURE_DOUBLE_2ND] },
        { g_config.tgSingleTapDrag2nd, &c->ts[GESTURE_SINGLE_DRAG_2ND] },
        { g_config.tgDoubleTapDrag2nd, &c->ts[GESTURE_DOUBLE_DRAG_2ND] },
        { g_config.tgSingleTap, &c->tp[GESTURE_SINGLE_TAP] },
        { g_config.tgLongPress, &c->tp[GESTURE_LONG_PRESS] },
        { g_config.tgDoubleTap, &c->tp[GESTURE_DOUBLE_TAP] },
        { g_config.tgSingleTapDrag, &c->tp[GESTURE_SINGLE_TAP_DRAG] },
        { g_config.tgLongPressDrag, &c->tp[GESTURE_LONG_PRESS_DRAG] },
        { g_config.tgDoubleTapDrag, &c->tp[GESTURE_DOUBLE_TAP_DRAG] },
        { g_config.tgSingleTap2nd, &c->tp[GESTURE_SINGLE_2ND] },
        { g_config.tgDoubleTap2nd, &c->tp[GESTURE_DOUBLE_2ND] },
        { g_config.tgSingleTapDrag2nd, &c->tp[GESTURE_SINGLE_DRAG_2ND] },
        { g_config.tgDoubleTapDrag2nd, &c->tp[GESTURE_DOUBLE_DRAG_2ND] },
    };
    for (int i = 0; i < (int)(sizeof(toggle_slots)/sizeof(toggle_slots[0])); i++)
        apply_toggle_mask(env, config, toggle_slots[i].fid, toggle_slots[i].slot);

    // Phase 4: auto-repeat bitmasks + intervals (per-binding, like toggle)
    struct AutoRepeatSlotPair { jfieldID fid; GestureBindingSlot* slot; jfieldID interval_fid; };
    AutoRepeatSlotPair ar_slots[] = {
        { g_config.arSingleTap, &c->ts[GESTURE_SINGLE_TAP], g_config.arSingleTapIntervalMs },
        { g_config.arLongPress, &c->ts[GESTURE_LONG_PRESS], g_config.arLongPressIntervalMs },
        { g_config.arDoubleTap, &c->ts[GESTURE_DOUBLE_TAP], g_config.arDoubleTapIntervalMs },
        { g_config.arSingleTapDrag, &c->ts[GESTURE_SINGLE_TAP_DRAG], g_config.arSingleTapDragIntervalMs },
        { g_config.arLongPressDrag, &c->ts[GESTURE_LONG_PRESS_DRAG], g_config.arLongPressDragIntervalMs },
        { g_config.arDoubleTapDrag, &c->ts[GESTURE_DOUBLE_TAP_DRAG], g_config.arDoubleTapDragIntervalMs },
        { g_config.arSingleTap2nd, &c->ts[GESTURE_SINGLE_2ND], g_config.arSingleTap2ndIntervalMs },
        { g_config.arDoubleTap2nd, &c->ts[GESTURE_DOUBLE_2ND], g_config.arDoubleTap2ndIntervalMs },
        { g_config.arSingleTapDrag2nd, &c->ts[GESTURE_SINGLE_DRAG_2ND], g_config.arSingleTapDrag2ndIntervalMs },
        { g_config.arDoubleTapDrag2nd, &c->ts[GESTURE_DOUBLE_DRAG_2ND], g_config.arDoubleTapDrag2ndIntervalMs },
        { g_config.arSingleTap, &c->tp[GESTURE_SINGLE_TAP], g_config.arSingleTapIntervalMs },
        { g_config.arLongPress, &c->tp[GESTURE_LONG_PRESS], g_config.arLongPressIntervalMs },
        { g_config.arDoubleTap, &c->tp[GESTURE_DOUBLE_TAP], g_config.arDoubleTapIntervalMs },
        { g_config.arSingleTapDrag, &c->tp[GESTURE_SINGLE_TAP_DRAG], g_config.arSingleTapDragIntervalMs },
        { g_config.arLongPressDrag, &c->tp[GESTURE_LONG_PRESS_DRAG], g_config.arLongPressDragIntervalMs },
        { g_config.arDoubleTapDrag, &c->tp[GESTURE_DOUBLE_TAP_DRAG], g_config.arDoubleTapDragIntervalMs },
        { g_config.arSingleTap2nd, &c->tp[GESTURE_SINGLE_2ND], g_config.arSingleTap2ndIntervalMs },
        { g_config.arDoubleTap2nd, &c->tp[GESTURE_DOUBLE_2ND], g_config.arDoubleTap2ndIntervalMs },
        { g_config.arSingleTapDrag2nd, &c->tp[GESTURE_SINGLE_DRAG_2ND], g_config.arSingleTapDrag2ndIntervalMs },
        { g_config.arDoubleTapDrag2nd, &c->tp[GESTURE_DOUBLE_DRAG_2ND], g_config.arDoubleTapDrag2ndIntervalMs },
    };
    for (int i = 0; i < (int)(sizeof(ar_slots)/sizeof(ar_slots[0])); i++)
        apply_auto_repeat_mask(env, config, ar_slots[i].fid, ar_slots[i].slot, ar_slots[i].interval_fid);

    c->color_primary = (uint32_t)env->GetIntField(config, g_config.colorPrimary);
    c->color_secondary = (uint32_t)env->GetIntField(config, g_config.colorSecondary);
    c->stroke_width_default = env->GetFloatField(config, g_config.strokeWidthDefault);
    c->fill_alpha_inactive_default = env->GetIntField(config, g_config.fillAlphaInactiveDefault);

    // Read scroll mode bindings: 5 gestures × 4 directions
    c->scroll_threshold_px = env->GetIntField(config, g_config.scrollThresholdPx);
    c->scroll_hold_threshold_px = env->GetIntField(config, g_config.scrollHoldThresholdPx);
    c->scroll_hold_haptic = env->GetIntField(config, g_config.scrollHoldHaptic);
    struct { jfieldID fid; int dir; } scroll_dirs[] = {
        { g_config.scrollBindingsUp, SCROLL_DIR_UP },
        { g_config.scrollBindingsDown, SCROLL_DIR_DOWN },
        { g_config.scrollBindingsLeft, SCROLL_DIR_LEFT },
        { g_config.scrollBindingsRight, SCROLL_DIR_RIGHT },
    };
    for (int d = 0; d < 4; d++) {
        jobjectArray dirArray = (jobjectArray)env->GetObjectField(config, scroll_dirs[d].fid);
        if (!dirArray) continue;
        jsize len = env->GetArrayLength(dirArray);
        for (int s = 0; s < len && s < 5; s++) {
            jintArray gestureArr = (jintArray)env->GetObjectArrayElement(dirArray, s);
            if (gestureArr) {
                c->scroll_bindings[s].dirs[scroll_dirs[d].dir].count =
                    read_binding_list(env, gestureArr, c->scroll_bindings[s].dirs[scroll_dirs[d].dir].arr, 8);
                env->DeleteLocalRef(gestureArr);
            }
        }
        env->DeleteLocalRef(dirArray);
    }

    // Read scroll hold flags
    jbooleanArray holdV = (jbooleanArray)env->GetObjectField(config, g_config.scrollHoldV);
    if (holdV) {
        jboolean* elems = env->GetBooleanArrayElements(holdV, NULL);
        jsize len = env->GetArrayLength(holdV);
        for (int i = 0; i < len && i < 5; i++)
            c->scroll_hold_v[i] = elems[i];
        env->ReleaseBooleanArrayElements(holdV, elems, 0);
        env->DeleteLocalRef(holdV);
    }
    jbooleanArray holdH = (jbooleanArray)env->GetObjectField(config, g_config.scrollHoldH);
    if (holdH) {
        jboolean* elems = env->GetBooleanArrayElements(holdH, NULL);
        jsize len = env->GetArrayLength(holdH);
        for (int i = 0; i < len && i < 5; i++)
            c->scroll_hold_h[i] = elems[i];
        env->ReleaseBooleanArrayElements(holdH, elems, 0);
        env->DeleteLocalRef(holdH);
    }
}

static void cache_config_field_ids(JNIEnv* env, jobject config) {
    if (g_config.configClass) env->DeleteGlobalRef(g_config.configClass);
    jclass cls = env->GetObjectClass(config);
    if (!cls) { return; }
    g_config.configClass = (jclass)env->NewGlobalRef(cls);

    struct { jfieldID* dst; const char* name; const char* sig; } scalar[] = {
        { &g_config.touchMode, "touchMode", "I" },
        { &g_config.inputMode, "inputMode", "I" },
        { &g_config.longPressTimeoutMs, "longPressTimeoutMs", "I" },
        { &g_config.doubleTapTimeoutMs, "doubleTapTimeoutMs", "I" },
        { &g_config.singleTapDelayMs, "singleTapDelayMs", "I" },
        { &g_config.dragThresholdPx, "dragThresholdPx", "I" },
        { &g_config.doubleTapDistancePx, "doubleTapDistancePx", "I" },
        { &g_config.bindingDelayMs, "bindingDelayMs", "I" },
        { &g_config.longPressDelayMs, "longPressDelayMs", "I" },
        { &g_config.cursorSpeed, "cursorSpeed", "I" },
        { &g_config.gestureThresholdPx, "gestureThresholdPx", "I" },
        { &g_config.cursorAccelerationThreshold, "cursorAccelerationThreshold", "I" },
        { &g_config.cursorAccelerationFactor, "cursorAccelerationFactor", "F" },
        { &g_config.screenW, "screenW", "I" },
        { &g_config.screenH, "screenH", "I" },
        { &g_config.xformScaleX, "xformScaleX", "F" },
        { &g_config.xformScaleY, "xformScaleY", "F" },
        { &g_config.viewOffsetX, "viewOffsetX", "F" },
        { &g_config.viewOffsetY, "viewOffsetY", "F" },
        { &g_config.gestureLongPressHaptic, "gestureLongPressHaptic", "I" },
        { &g_config.scrollBindHaptic, "scrollBindHaptic", "I" },
        { &g_config.hapticEnabled, "hapticEnabled", "Z" },
    };
    for (int i = 0; i < (int)(sizeof(scalar)/sizeof(scalar[0])); i++) {
        *scalar[i].dst = env->GetFieldID(cls, scalar[i].name, scalar[i].sig);
        JNI_CHECK(env, scalar[i].name);
    }

    struct { jfieldID* dst; const char* name; } arrays[] = {
        { &g_config.tsSingleTap, "tsSingleTap" },
        { &g_config.tsLongPress, "tsLongPress" },
        { &g_config.tsDoubleTap, "tsDoubleTap" },
        { &g_config.tsSingleTapDrag, "tsSingleTapDrag" },
        { &g_config.tsLongPressDrag, "tsLongPressDrag" },
        { &g_config.tsDoubleTapDrag, "tsDoubleTapDrag" },
        { &g_config.tsSingleTap2nd, "tsSingleTap2nd" },
        { &g_config.tsDoubleTap2nd, "tsDoubleTap2nd" },
        { &g_config.tsSingleTapDrag2nd, "tsSingleTapDrag2nd" },
        { &g_config.tsDoubleTapDrag2nd, "tsDoubleTapDrag2nd" },
        { &g_config.tpSingleTap, "tpSingleTap" },
        { &g_config.tpLongPress, "tpLongPress" },
        { &g_config.tpDoubleTap, "tpDoubleTap" },
        { &g_config.tpSingleTapDrag, "tpSingleTapDrag" },
        { &g_config.tpLongPressDrag, "tpLongPressDrag" },
        { &g_config.tpDoubleTapDrag, "tpDoubleTapDrag" },
        { &g_config.tpSingleTap2nd, "tpSingleTap2nd" },
        { &g_config.tpDoubleTap2nd, "tpDoubleTap2nd" },
        { &g_config.tpSingleTapDrag2nd, "tpSingleTapDrag2nd" },
        { &g_config.tpDoubleTapDrag2nd, "tpDoubleTapDrag2nd" },
    };
    for (int i = 0; i < (int)(sizeof(arrays)/sizeof(arrays[0])); i++) {
        *arrays[i].dst = env->GetFieldID(cls, arrays[i].name, "[I");
        JNI_CHECK(env, arrays[i].name);
    }

    struct { jfieldID* dst; const char* name; const char* sig; } render_fields[] = {
        { &g_config.colorPrimary, "colorPrimary", "I" },
        { &g_config.colorSecondary, "colorSecondary", "I" },
        { &g_config.strokeWidthDefault, "strokeWidthDefault", "F" },
        { &g_config.fillAlphaInactiveDefault, "fillAlphaInactiveDefault", "I" },
    };
    for (int i = 0; i < (int)(sizeof(render_fields)/sizeof(render_fields[0])); i++) {
        *render_fields[i].dst = env->GetFieldID(cls, render_fields[i].name, render_fields[i].sig);
        JNI_CHECK(env, render_fields[i].name);
    }

    struct { jfieldID* dst; const char* name; const char* sig; } scroll_fields[] = {
        { &g_config.scrollBindingsUp, "scrollBindingsUp", "[[I" },
        { &g_config.scrollBindingsDown, "scrollBindingsDown", "[[I" },
        { &g_config.scrollBindingsLeft, "scrollBindingsLeft", "[[I" },
        { &g_config.scrollBindingsRight, "scrollBindingsRight", "[[I" },
        { &g_config.scrollThresholdPx, "scrollThresholdPx", "I" },
        { &g_config.scrollHoldThresholdPx, "scrollHoldThresholdPx", "I" },
        { &g_config.scrollHoldV, "scrollHoldV", "[Z" },
        { &g_config.scrollHoldH, "scrollHoldH", "[Z" },
        { &g_config.scrollHoldHaptic, "scrollHoldHaptic", "I" },
    };
    for (int i = 0; i < (int)(sizeof(scroll_fields)/sizeof(scroll_fields[0])); i++) {
        *scroll_fields[i].dst = env->GetFieldID(cls, scroll_fields[i].name, scroll_fields[i].sig);
        JNI_CHECK(env, scroll_fields[i].name);
    }

    struct { jfieldID* dst; const char* name; } stickies[] = {
        { &g_config.stSingleTap, "stSingleTap" },
        { &g_config.stLongPress, "stLongPress" },
        { &g_config.stDoubleTap, "stDoubleTap" },
        { &g_config.stSingleTapDrag, "stSingleTapDrag" },
        { &g_config.stLongPressDrag, "stLongPressDrag" },
        { &g_config.stDoubleTapDrag, "stDoubleTapDrag" },
        { &g_config.stSingleTap2nd, "stSingleTap2nd" },
        { &g_config.stDoubleTap2nd, "stDoubleTap2nd" },
        { &g_config.stSingleTapDrag2nd, "stSingleTapDrag2nd" },
        { &g_config.stDoubleTapDrag2nd, "stDoubleTapDrag2nd" },
    };
    for (int i = 0; i < (int)(sizeof(stickies)/sizeof(stickies[0])); i++) {
        *stickies[i].dst = env->GetFieldID(cls, stickies[i].name, "I");
        JNI_CHECK(env, stickies[i].name);
    }

    struct { jfieldID* dst; const char* name; } toggles[] = {
        { &g_config.tgSingleTap, "tgSingleTap" },
        { &g_config.tgLongPress, "tgLongPress" },
        { &g_config.tgDoubleTap, "tgDoubleTap" },
        { &g_config.tgSingleTapDrag, "tgSingleTapDrag" },
        { &g_config.tgLongPressDrag, "tgLongPressDrag" },
        { &g_config.tgDoubleTapDrag, "tgDoubleTapDrag" },
        { &g_config.tgSingleTap2nd, "tgSingleTap2nd" },
        { &g_config.tgDoubleTap2nd, "tgDoubleTap2nd" },
        { &g_config.tgSingleTapDrag2nd, "tgSingleTapDrag2nd" },
        { &g_config.tgDoubleTapDrag2nd, "tgDoubleTapDrag2nd" },
    };
    for (int i = 0; i < (int)(sizeof(toggles)/sizeof(toggles[0])); i++) {
        *toggles[i].dst = env->GetFieldID(cls, toggles[i].name, "I");
        JNI_CHECK(env, toggles[i].name);
    }

    struct { jfieldID* dst; const char* name; } auto_repeats[] = {
        { &g_config.arSingleTap, "arSingleTap" },
        { &g_config.arLongPress, "arLongPress" },
        { &g_config.arDoubleTap, "arDoubleTap" },
        { &g_config.arSingleTapDrag, "arSingleTapDrag" },
        { &g_config.arLongPressDrag, "arLongPressDrag" },
        { &g_config.arDoubleTapDrag, "arDoubleTapDrag" },
        { &g_config.arSingleTap2nd, "arSingleTap2nd" },
        { &g_config.arDoubleTap2nd, "arDoubleTap2nd" },
        { &g_config.arSingleTapDrag2nd, "arSingleTapDrag2nd" },
        { &g_config.arDoubleTapDrag2nd, "arDoubleTapDrag2nd" },
    };
    for (int i = 0; i < (int)(sizeof(auto_repeats)/sizeof(auto_repeats[0])); i++) {
        *auto_repeats[i].dst = env->GetFieldID(cls, auto_repeats[i].name, "I");
        JNI_CHECK(env, auto_repeats[i].name);
    }

    struct { jfieldID* dst; const char* name; } ar_intervals[] = {
        { &g_config.arSingleTapIntervalMs, "arSingleTapIntervalMs" },
        { &g_config.arLongPressIntervalMs, "arLongPressIntervalMs" },
        { &g_config.arDoubleTapIntervalMs, "arDoubleTapIntervalMs" },
        { &g_config.arSingleTapDragIntervalMs, "arSingleTapDragIntervalMs" },
        { &g_config.arLongPressDragIntervalMs, "arLongPressDragIntervalMs" },
        { &g_config.arDoubleTapDragIntervalMs, "arDoubleTapDragIntervalMs" },
        { &g_config.arSingleTap2ndIntervalMs, "arSingleTap2ndIntervalMs" },
        { &g_config.arDoubleTap2ndIntervalMs, "arDoubleTap2ndIntervalMs" },
        { &g_config.arSingleTapDrag2ndIntervalMs, "arSingleTapDrag2ndIntervalMs" },
        { &g_config.arDoubleTapDrag2ndIntervalMs, "arDoubleTapDrag2ndIntervalMs" },
    };
    for (int i = 0; i < (int)(sizeof(ar_intervals)/sizeof(ar_intervals[0])); i++) {
        *ar_intervals[i].dst = env->GetFieldID(cls, ar_intervals[i].name, "I");
        JNI_CHECK(env, ar_intervals[i].name);
    }

    env->DeleteLocalRef(cls);
    g_config_cached = true;
}

static void nativeInit(JNIEnv* env, jclass clazz, jobject config) {
    if (!g_config_cached) cache_config_field_ids(env, config);
    if (!g_config_cached) return;

    TouchProcessorConfig c;
    memset(&c, 0, sizeof(c));
    read_config_from_java(env, config, &c);
    touch_processor_init(&c);
}

#pragma pack(push, 1)
typedef struct {
    int32_t type;
    int32_t shape;
    int32_t x, y;
    float hw, hh;
    float scale;
    float corner_radius;
    float opacity;
    uint32_t color_primary;
    uint32_t color_secondary;
    float stroke_width;
    int32_t fill_alpha_inactive;
    int32_t activation_mode;
    int32_t range_orientation;
    int32_t range_binding_count;
} ElementGeometryEntry;
#pragma pack(pop)

static ElementGeometryEntry g_geom_buffer[MAX_ELEMENTS];
static jobject g_geom_buffer_ref = NULL;
static int g_geom_count = 0;
static bool g_geom_dirty = false;

static void element_geometry_flush(void) {
    if (!g_geom_dirty) return;
    g_geom_dirty = false;
    int n = g_state.element_count;
    for (int i = 0; i < n; i++) {
        const TouchElement* e = &g_state.elements[i];
        g_geom_buffer[i].type = (int32_t)e->type;
        g_geom_buffer[i].shape = (int32_t)e->shape;
        g_geom_buffer[i].x = e->x;
        g_geom_buffer[i].y = e->y;
        g_geom_buffer[i].hw = e->hw;
        g_geom_buffer[i].hh = e->hh;
        g_geom_buffer[i].scale = e->scale;
        g_geom_buffer[i].corner_radius = e->corner_radius;
        g_geom_buffer[i].opacity = e->opacity;
        g_geom_buffer[i].color_primary = e->color_primary;
        g_geom_buffer[i].color_secondary = e->color_secondary;
        g_geom_buffer[i].stroke_width = e->stroke_width;
        g_geom_buffer[i].fill_alpha_inactive = e->fill_alpha_inactive;
        g_geom_buffer[i].activation_mode = (int32_t)e->activation_mode;
        g_geom_buffer[i].range_orientation = e->range_orientation;
        g_geom_buffer[i].range_binding_count = e->range_binding_count;
    }
    g_geom_count = n;
}

static void cache_element_field_ids(JNIEnv* env) {
    if (g_elem.elemClass) return;
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
    g_elem.elementLongPress = env->GetFieldID(ec, "elementLongPress", "[I");
    g_elem.elementGesture = env->GetFieldID(ec, "elementGesture", "[I");
    g_elem.bindingTypes = env->GetFieldID(ec, "bindingTypes", "[I");
    g_elem.bindingSticky = env->GetFieldID(ec, "bindingSticky", "[I");
    g_elem.bindingToggle = env->GetFieldID(ec, "bindingToggle", "[I");
    g_elem.bindingAutoRepeat = env->GetFieldID(ec, "bindingAutoRepeat", "[I");
    g_elem.bindingAutoRepeatIntervalMs = env->GetFieldID(ec, "bindingAutoRepeatIntervalMs", "[I");
    g_elem.longPressToggleBitmask = env->GetFieldID(ec, "longPressToggleBitmask", "I");
    g_elem.gestureToggleBitmask = env->GetFieldID(ec, "gestureToggleBitmask", "I");
    g_elem.rangeOrdinal = env->GetFieldID(ec, "rangeOrdinal", "I");
    g_elem.rangeMax = env->GetFieldID(ec, "rangeMax", "I");
    g_elem.bindingCount = env->GetFieldID(ec, "bindingCount", "I");
    g_elem.orientation = env->GetFieldID(ec, "orientation", "I");
    g_elem.buttonLongPressHaptic = env->GetFieldID(ec, "buttonLongPressHaptic", "I");
    g_elem.buttonGestureHaptic = env->GetFieldID(ec, "buttonGestureHaptic", "I");
    g_elem.cornerRadius = env->GetFieldID(ec, "cornerRadius", "F");
    g_elem.opacity = env->GetFieldID(ec, "opacity", "F");
    env->DeleteLocalRef(ec);
}

static void nativeSetElements(JNIEnv* env, jclass clazz, jobjectArray elements) {
    if (elements == NULL) return;

    cache_element_field_ids(env);

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
        elems[i].range_ordinal = env->GetIntField(je, g_elem.rangeOrdinal);
        elems[i].range_max = env->GetIntField(je, g_elem.rangeMax);
        elems[i].range_binding_count = env->GetIntField(je, g_elem.bindingCount);
        elems[i].range_orientation = env->GetIntField(je, g_elem.orientation);

        elems[i].button_long_press_haptic = env->GetIntField(je, g_elem.buttonLongPressHaptic);
        elems[i].button_gesture_haptic = env->GetIntField(je, g_elem.buttonGestureHaptic);
        elems[i].corner_radius = env->GetFloatField(je, g_elem.cornerRadius);
        elems[i].opacity = env->GetFloatField(je, g_elem.opacity);

        elems[i].element_long_press_count = 0;
        elems[i].element_gesture_count = 0;
        elems[i].primary_sticky_mask = 0;

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

        if (elems[i].type == ELEM_BUTTON) {
            elems[i].element_long_press_count = read_binding_list(env,
                (jintArray)env->GetObjectField(je, g_elem.elementLongPress), elems[i].element_long_press, 8);
            {
                jint lpToggle = env->GetIntField(je, g_elem.longPressToggleBitmask);
                for (int k = 0; k < elems[i].element_long_press_count; k++)
                    elems[i].element_long_press[k].toggle = (lpToggle >> k) & 1;
            }
            elems[i].element_gesture_count = read_binding_list(env,
                (jintArray)env->GetObjectField(je, g_elem.elementGesture), elems[i].element_gesture, 8);
            {
                jint gToggle = env->GetIntField(je, g_elem.gestureToggleBitmask);
                for (int k = 0; k < elems[i].element_gesture_count; k++)
                    elems[i].element_gesture[k].toggle = (gToggle >> k) & 1;
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
                }
            }
            {
                jintArray toggleArr = (jintArray)env->GetObjectField(je, g_elem.bindingToggle);
                if (toggleArr) {
                    jint* toggles = env->GetIntArrayElements(toggleArr, NULL);
                    jsize toggleLen = env->GetArrayLength(toggleArr);
                    for (int j = 0; j < toggleLen && j < 4; j++)
                        if (toggles[j] & 1)
                            elems[i].bindings[j].toggle = true;
                    env->ReleaseIntArrayElements(toggleArr, toggles, JNI_ABORT);
                }
            }
            {
                jintArray arArr = (jintArray)env->GetObjectField(je, g_elem.bindingAutoRepeat);
                if (arArr) {
                    jint* ar = env->GetIntArrayElements(arArr, NULL);
                    jsize arLen = env->GetArrayLength(arArr);
                    for (int j = 0; j < arLen && j < 4; j++)
                        if (ar[j] & 1)
                            elems[i].bindings[j].auto_repeat = true;
                    env->ReleaseIntArrayElements(arArr, ar, JNI_ABORT);
                }
            }
            {
                jintArray arIntervalArr = (jintArray)env->GetObjectField(je, g_elem.bindingAutoRepeatIntervalMs);
                if (arIntervalArr) {
                    jint* arInterval = env->GetIntArrayElements(arIntervalArr, NULL);
                    jsize arIntervalLen = env->GetArrayLength(arIntervalArr);
                    for (int j = 0; j < arIntervalLen && j < 4; j++)
                        if (arInterval[j] > 0)
                            elems[i].bindings[j].auto_repeat_interval_ms = arInterval[j];
                    env->ReleaseIntArrayElements(arIntervalArr, arInterval, JNI_ABORT);
                }
            }
        }

        elems[i].current_ptr_id = -1;
        elems[i].engaged = false;
        for (int p = 0; p < MAX_PETALS; p++) elems[i].petal_active[p] = false;

        env->DeleteLocalRef(je);
    }

    touch_processor_set_elements(elems, len);
    g_geom_dirty = true;
    element_geometry_flush();
}

static void nativeSetSnappingSize(JNIEnv* env, jclass clazz, jfloat size) {
    touch_processor_set_snapping_size(size);
    g_geom_dirty = true;
}

static void nativeSetResolutionScale(JNIEnv* env, jclass clazz, jfloat scale) {
    touch_processor_set_resolution_scale(scale);
}

static void nativeSetSimTouchScreen(JNIEnv* env, jclass clazz, jboolean enabled) {
    touch_processor_set_sim_touch_screen(enabled == JNI_TRUE);
}

static void dispatch_and_flush(JNIEnv* env, const TouchActionResult* r, const char* event_name) {
    if (r && r->count > 0) {
        for (int i = 0; i < r->count && i < 8; i++) {
            __android_log_print(ANDROID_LOG_DEBUG, "SigTrace", "EVT_%s act[%d] type=%d a0=%d a1=%d", event_name, i, (int)r->actions[i].type, r->actions[i].pointer_button.button, 0);
        }
    }
    visual_state_flush();
    dispatch_actions_batch(env, r);
}

static void nativeOnFingerDown(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y,
                                jlong timeMs) {
    TouchActionResult r = touch_processor_on_finger_down(ptrId, x, y, (uint64_t)timeMs);
    dispatch_and_flush(env, &r, "down");
}

static void nativeOnFingerMove(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y,
                                jlong timeMs) {
    TouchActionResult r = touch_processor_on_finger_move(ptrId, x, y, (uint64_t)timeMs);
    dispatch_and_flush(env, &r, "move");
}

static void nativeOnFingerUp(JNIEnv* env, jclass clazz, jint ptrId, jfloat x, jfloat y,
                              jlong timeMs) {
    TouchActionResult r = touch_processor_on_finger_up(ptrId, x, y, (uint64_t)timeMs);
    dispatch_and_flush(env, &r, "up");
}

static void nativeTick(JNIEnv* env, jclass clazz, jlong timeMs) {
    TouchActionResult r = touch_processor_tick((uint64_t)timeMs);
    dispatch_and_flush(env, &r, "tick");
}

static void nativeCancelAll(JNIEnv* env, jclass clazz) {
    TouchActionResult r = touch_processor_cancel_all();
    dispatch_and_flush(env, &r, "cancel");
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
    read_config_from_java(env, config, &c);
    touch_processor_update_config(&c);
}

static void nativeSetXformScale(JNIEnv* env, jclass clazz, jfloat scaleX, jfloat scaleY) {
    touch_processor_set_xform_scale(scaleX, scaleY);
}

static void nativeSetViewOffset(JNIEnv* env, jclass clazz, jfloat offsetX, jfloat offsetY) {
    touch_processor_set_view_offset(offsetX, offsetY);
}

// --- Element geometry buffer (static, updated on set_elements) ---
static jobject nativeGetElementGeometry(JNIEnv* env, jclass clazz) {
    if (g_geom_buffer_ref == NULL) {
        g_geom_buffer_ref = env->NewGlobalRef(
            env->NewDirectByteBuffer(g_geom_buffer, sizeof(g_geom_buffer)));
    }
    element_geometry_flush();
    return g_geom_buffer_ref;
}

static jint nativeGetElementCount(JNIEnv* env, jclass clazz) {
    return (jint)g_state.element_count;
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

static void nativeSetDispatchPacked(JNIEnv* env, jclass clazz, jintArray packed) {
    if (g_dispatch_packed) {
        env->DeleteGlobalRef(g_dispatch_packed);
        g_dispatch_packed = NULL;
    }
    if (packed) {
        g_dispatch_packed = (jintArray)env->NewGlobalRef(packed);
    }
}

static void nativeRegisterDispatcher(JNIEnv* env, jclass clazz, jobject dispatcher) {
    if (g_dispatch_obj) {
        env->DeleteGlobalRef(g_dispatch_obj);
        g_dispatch_obj = NULL;
    }
    if (!dispatcher) return;
    g_dispatch_obj = env->NewGlobalRef(dispatcher);
    jclass dcls = env->GetObjectClass(dispatcher);
    g_dispatchAllActions = env->GetMethodID(dcls, "dispatchAllActions", "(I)V");
    JNI_CHECK(env, "dispatchAllActions");
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
        {"nativeCancelAll", "()V", (void*)nativeCancelAll},
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
        {"nativeSetDispatchPacked", "([I)V", (void*)nativeSetDispatchPacked},
        {"nativeUpdateConfig", "(Lcom/winlator/cmod/inputcontrols/NativeTouchProcessor$NativeConfig;)V", (void*)nativeUpdateConfig},
        {"nativeGetElementGeometry", "()Ljava/nio/ByteBuffer;", (void*)nativeGetElementGeometry},
        {"nativeGetElementCount", "()I", (void*)nativeGetElementCount},
        {"nativeSyncVisualState", "([F[I[F[I[I)I", (void*)nativeSyncVisualState},
    };

    int result = env->RegisterNatives(clazz, methods, sizeof(methods)/sizeof(methods[0]));
    env->DeleteLocalRef(clazz);

    if (result != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

} // extern "C"
