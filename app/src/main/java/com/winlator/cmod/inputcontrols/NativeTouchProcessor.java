package com.winlator.cmod.inputcontrols;

import android.os.SystemClock;

import com.winlator.cmod.widget.InputControlsView;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XKeycode;
import com.winlator.cmod.xserver.XServer;

import java.util.List;

import com.winlator.cmod.inputcontrols.TouchActivationMode;
import com.winlator.cmod.core.HapticUtils;

public class NativeTouchProcessor {
    private static final int BINDING_KEYBOARD_FIRST = 0x100;
    private static final int BINDING_KEYBOARD_LAST = 0x400;
    private static final int BINDING_GAMEPAD_BASE = 0x500;
    private static boolean loaded;

    public static class TouchActionResult {
        public static final int ACT_NONE = 0;
        public static final int ACT_POINTER_MOVE = 1;
        public static final int ACT_POINTER_MOVE_DELTA = 2;
        public static final int ACT_POINTER_BUTTON_PRESS = 3;
        public static final int ACT_POINTER_BUTTON_RELEASE = 4;
        public static final int ACT_KEY_PRESS = 5;
        public static final int ACT_KEY_RELEASE = 6;
        public static final int ACT_MOUSE_EVENT = 7;
        public static final int ACT_SCROLL = 8;
        public static final int ACT_HAPTIC = 9;
        public static final int ACT_SET_CURSOR_SPEED = 10;
        public static final int ACT_START_MOUSE_MOVE = 11;
        public static final int ACT_STOP_MOUSE_MOVE = 12;
        public static final int ACT_GAMEPAD_STATE = 13;
        public static final int ACT_GAMEPAD_AXIS = 14;

        public int count;
        public int[] types;
        public int[] intArgs; // 3 ints per action
    }

    public static class NativeConfig {
        public int touchMode;
        public int inputMode;
        public int secondFingerMode;
        public int longPressTimeoutMs;
        public int doubleTapTimeoutMs;
        public int buttonDoubleTapTimeoutMs;
        public int singleTapDelayMs;
        public int dragThresholdPx;
        public int doubleTapDistancePx;
        public int bindingDelayMs;
        public int longPressDelayMs;
        public int cursorSpeed;
        public int screenW;
        public int screenH;
        public int gestureLongPressHaptic;
        public boolean hapticEnabled;
        public int gestureThresholdPx;
        public float xformScaleX = 1.0f;
        public float xformScaleY = 1.0f;
        public int cursorAccelerationThreshold;
        public float cursorAccelerationFactor;

        // Touchscreen gesture bindings (12 lists, each as [type0, keycode0, type1, keycode1, ...])
        public int[] tsSingleTap;
        public int[] tsLongPress;
        public int[] tsDoubleTap;
        public int[] tsSingleTapDrag;
        public int[] tsLongPressDrag;
        public int[] tsDoubleTapDrag;
        public int[] tsSingleTap2nd;
        public int[] tsLongPress2nd;
        public int[] tsDoubleTap2nd;
        public int[] tsSingleTapDrag2nd;
        public int[] tsLongPressDrag2nd;
        public int[] tsDoubleTapDrag2nd;

        // Touchpad gesture bindings (12 lists)
        public int[] tpSingleTap;
        public int[] tpLongPress;
        public int[] tpDoubleTap;
        public int[] tpSingleTapDrag;
        public int[] tpLongPressDrag;
        public int[] tpDoubleTapDrag;
        public int[] tpSingleTap2nd;
        public int[] tpLongPress2nd;
        public int[] tpDoubleTap2nd;
        public int[] tpSingleTapDrag2nd;
        public int[] tpLongPressDrag2nd;
        public int[] tpDoubleTapDrag2nd;
    }

    public static class NativeElement {
        public int type;
        public int shape;
        public int x;
        public int y;
        public float w;
        public float h;
        public float scale;
        public float cornerRadius;
        public boolean passthroughTouch;
        public int activationMode;
        public int[] bindingTypes; // [type0, keycode0, type1, keycode1, ...]
        public int[] elementLongPress;   // [type0, keycode0, ...]
        public int[] elementGesture;     // [type0, keycode0, ...]
        public int[] elementDoubleTap;   // [type0, keycode0, ...]
        public boolean toggleSwitch;
        public float opacity;
        public int buttonLongPressHaptic = 1;
        public int buttonGestureHaptic = 1;

        // Range button fields
        public int rangeOrdinal;
        public int rangeMax;
        public int bindingCount;
        public int orientation;
    }

    private XServer xServer;
    private InputControlsView inputControlsView;
    private boolean running;

    public NativeTouchProcessor() {
        try {
            System.loadLibrary("touch_processor");
            loaded = true;
        } catch (UnsatisfiedLinkError e) {
            loaded = false;
        }
    }

    public boolean isLoaded() { return loaded; }

    public void setXServer(XServer xServer) { this.xServer = xServer; }
    public void setInputControlsView(InputControlsView icv) { this.inputControlsView = icv; }

    // Native methods
    private static native void nativeInit(NativeConfig config);
    private static native void nativeSetElements(NativeElement[] elements);
    private static native TouchActionResult nativeOnFingerDown(int ptrId, float x, float y, long timeMs, float[] outPositions, byte[] outActive);
    private static native TouchActionResult nativeOnFingerMove(int ptrId, float x, float y, long timeMs, float[] outPositions, byte[] outActive);
    private static native TouchActionResult nativeOnFingerUp(int ptrId, float x, float y, long timeMs, float[] outPositions, byte[] outActive);
    private static native TouchActionResult nativeTick(long timeMs, float[] outPositions, byte[] outActive);
    private static native void nativeReset();
    private static native boolean nativeIsPassthroughActive();
    private static native void nativeSetSnappingSize(float size);
    private static native void nativeSetResolutionScale(float scale);
    private static native void nativeSetSimTouchScreen(boolean enabled);
    private static native void nativeUpdateConfig(NativeConfig config);
    private static native void nativeSetXformScale(float scaleX, float scaleY);
    private static native boolean nativeGetElementState(int elemIndex, float[] outXY);

    private static native boolean nativeHandleDownByMode(int ptrId, float x, float y, long timeMs);
    private static native boolean nativeHandleUpByMode(int ptrId, float x, float y, long timeMs);
    private static native void nativeHandleMoveByMode(int ptrId, float x, float y, long timeMs);
    private static native int nativeTrackedCount(int ptrId);
    private static native int nativeHoveredIndex(int ptrId);

    public void init(NativeConfig config) {
        if (!loaded) return;
        nativeInit(config);
    }

    public void updateConfig(NativeConfig config) {
        if (!loaded) return;
        nativeUpdateConfig(config);
    }

    public void setElements(NativeElement[] elements) {
        if (!loaded) return;
        nativeSetElements(elements);
    }

    public void setSnappingSize(float size) {
        if (!loaded) return;
        nativeSetSnappingSize(size);
    }

    public void setResolutionScale(float scale) {
        if (!loaded) return;
        nativeSetResolutionScale(scale);
    }

    public void setSimTouchScreen(boolean enabled) {
        if (!loaded) return;
        nativeSetSimTouchScreen(enabled);
    }

    public void setXformScale(float scaleX, float scaleY) {
        if (!loaded) return;
        nativeSetXformScale(scaleX, scaleY);
    }

    public boolean handleDownByMode(int ptrId, float x, float y) {
        if (!loaded) return false;
        return nativeHandleDownByMode(ptrId, x, y, SystemClock.uptimeMillis());
    }

    public boolean handleUpByMode(int ptrId, float x, float y) {
        if (!loaded) return false;
        return nativeHandleUpByMode(ptrId, x, y, SystemClock.uptimeMillis());
    }

    public void handleMoveByMode(int ptrId, float x, float y) {
        if (!loaded) return;
        nativeHandleMoveByMode(ptrId, x, y, SystemClock.uptimeMillis());
    }

    public int getTrackedCount(int ptrId) {
        if (!loaded) return 0;
        return nativeTrackedCount(ptrId);
    }

    public int getHoveredIndex(int ptrId) {
        if (!loaded) return -1;
        return nativeHoveredIndex(ptrId);
    }

    /**
     * Build a NativeConfig from a ControlsProfile and screen dimensions.
     */
    public static NativeConfig buildNativeConfig(ControlsProfile profile, int screenW, int screenH) {
        return buildNativeConfig(profile, screenW, screenH, 1.0f);
    }

    public static NativeConfig buildNativeConfig(ControlsProfile profile, int screenW, int screenH, float globalCursorSpeed) {
        NativeConfig c = new NativeConfig();

        // Derive touchMode from MouseMode
        if (profile.getMouseMode() == com.winlator.cmod.inputcontrols.MouseMode.TOUCHSCREEN) {
            c.touchMode = 1; // TOUCH_MODE_TOUCHSCREEN
        } else {
            c.touchMode = 0; // TOUCH_MODE_TOUCHPAD
        }
        c.inputMode = profile.getInputMode() == com.winlator.cmod.inputcontrols.InputMode.RELATIVE ? 0 : 1;
        c.secondFingerMode = profile.getSecondFingerMode() == SecondFingerMode.LONG_TAP_ACTION ? 1 : 0;
        // Use mode-appropriate timeout/drag values
        if (c.touchMode == 0) { // TOUCHPAD
            c.longPressTimeoutMs = profile.getTouchpadLongPressTimeout();
            c.doubleTapTimeoutMs = profile.getTouchpadDoubleTapTimeout();
            c.dragThresholdPx = profile.getTouchpadDragThreshold();
            c.cursorSpeed = (int)(profile.getTouchpadCursorSpeed() * globalCursorSpeed * 100);
        } else { // TOUCHSCREEN
            c.longPressTimeoutMs = profile.getLongPressTimeout();
            c.doubleTapTimeoutMs = profile.getDoubleTapTimeout();
            c.dragThresholdPx = profile.getDragThreshold();
            c.cursorSpeed = (int)(profile.getCursorSpeed() * globalCursorSpeed * 100);
        }
        c.buttonDoubleTapTimeoutMs = profile.getButtonDoubleTapTimeout();
        c.singleTapDelayMs = profile.getSingleTapDelay();
        c.doubleTapDistancePx = profile.getDoubleTapDistance();
        c.bindingDelayMs = profile.getBindingDelay();
        c.longPressDelayMs = profile.getLongPressDelay();
        c.screenW = screenW;
        c.screenH = screenH;
        c.gestureLongPressHaptic = profile.getGestureLongPressHaptic();
        c.hapticEnabled = c.gestureLongPressHaptic > 0;
        c.gestureThresholdPx = profile.getGestureThreshold();
        c.cursorAccelerationThreshold = 6;
        c.cursorAccelerationFactor = 1.25f;

        // Touchscreen gesture bindings
        c.tsSingleTap = bindingListToEncoded(profile.getSingleTapAction());
        c.tsLongPress = bindingListToEncoded(profile.getLongPressAction());
        c.tsDoubleTap = bindingListToEncoded(profile.getDoubleTapAction());
        c.tsSingleTapDrag = bindingListToEncoded(profile.getSingleTapDragAction());
        c.tsLongPressDrag = bindingListToEncoded(profile.getLongPressDragAction());
        c.tsDoubleTapDrag = bindingListToEncoded(profile.getDoubleTapDragAction());
        c.tsSingleTap2nd = bindingListToEncoded(profile.getSingleTap2ndFingerAction());
        c.tsLongPress2nd = bindingListToEncoded(profile.getLongPress2ndFingerAction());
        c.tsDoubleTap2nd = bindingListToEncoded(profile.getDoubleTap2ndFingerAction());
        c.tsSingleTapDrag2nd = bindingListToEncoded(profile.getSingleTap2ndFingerDragAction());
        c.tsLongPressDrag2nd = bindingListToEncoded(profile.getLongPress2ndFingerDragAction());
        c.tsDoubleTapDrag2nd = bindingListToEncoded(profile.getDoubleTap2ndFingerDragAction());

        // Touchpad gesture bindings
        c.tpSingleTap = bindingListToEncoded(profile.getTouchpadSingleTapAction());
        c.tpLongPress = bindingListToEncoded(profile.getTouchpadLongPressAction());
        c.tpDoubleTap = bindingListToEncoded(profile.getTouchpadDoubleTapAction());
        c.tpSingleTapDrag = bindingListToEncoded(profile.getTouchpadSingleTapDragAction());
        c.tpLongPressDrag = bindingListToEncoded(profile.getTouchpadLongPressDragAction());
        c.tpDoubleTapDrag = bindingListToEncoded(profile.getTouchpadDoubleTapDragAction());
        c.tpSingleTap2nd = bindingListToEncoded(profile.getTouchpadSingleTap2ndFingerAction());
        c.tpLongPress2nd = bindingListToEncoded(profile.getTouchpadLongPress2ndFingerAction());
        c.tpDoubleTap2nd = bindingListToEncoded(profile.getTouchpadDoubleTap2ndFingerAction());
        c.tpSingleTapDrag2nd = bindingListToEncoded(profile.getTouchpadSingleTap2ndFingerDragAction());
        c.tpLongPressDrag2nd = bindingListToEncoded(profile.getTouchpadLongPress2ndFingerDragAction());
        c.tpDoubleTapDrag2nd = bindingListToEncoded(profile.getTouchpadDoubleTap2ndFingerDragAction());

        return c;
    }

    /**
     * Build a NativeElement[] from a list of ControlElements and a global activation mode.
     */
    public static NativeElement[] buildNativeElements(List<ControlElement> elements, TouchActivationMode activationMode) {
        return buildNativeElements(elements, activationMode, null);
    }

    public static NativeElement[] buildNativeElements(List<ControlElement> elements, TouchActivationMode activationMode, ControlsProfile profile) {
        if (elements == null) return null;
        NativeElement[] arr = new NativeElement[elements.size()];
        ControlsProfile p = profile;
        for (int i = 0; i < elements.size(); i++) {
            ControlElement ce = elements.get(i);
            NativeElement ne = new NativeElement();
            ne.type = ce.getType().ordinal();
            ne.shape = ce.getShape().ordinal();
            ne.x = ce.getX();
            ne.y = ce.getY();
            ne.w = ce.getElementWidth();
            ne.h = ce.getElementHeight();
            ne.scale = ce.getScale();
            ne.cornerRadius = ce.getCornerRadius();
            ne.passthroughTouch = ce.isPassthroughTouch();
            ne.activationMode = activationMode != null ? activationMode.ordinal() : 0;
            ne.toggleSwitch = ce.isToggleSwitch();
            ne.opacity = ce.getEffectiveOpacity();

            // Per-element haptic settings from profile
            ne.buttonLongPressHaptic = p != null ? p.getButtonLongPressHaptic() : 1;
            ne.buttonGestureHaptic = p != null ? p.getButtonGestureHaptic() : 1;

            // Range button fields
            if (ce.getType() == ControlElement.Type.RANGE_BUTTON) {
                ne.rangeOrdinal = ce.getRange().ordinal();
                ne.rangeMax = ce.getRange().max;
                ne.bindingCount = ce.getBindingCount();
                ne.orientation = ce.getOrientation();
            }

            // Element-specific gesture bindings
            ne.elementLongPress = bindingListToEncoded(ce.getLongPressBindings());
            ne.elementGesture = bindingListToEncoded(ce.getGestureBindings());
            ne.elementDoubleTap = bindingListToEncoded(ce.getDoubleTapBindings());

            // Encode up to 4 bindings as [type0, keycode0, type1, keycode1, ...]
            ne.bindingTypes = new int[ce.getBindingCount() * 2];
            for (int j = 0; j < ce.getBindingCount() && j < 4; j++) {
                Binding b = ce.getBindingAt(j);
                int typeVal;
                int keycodeVal = 0;
                if (b == null || b == Binding.NONE) {
                    typeVal = 0; // BINDING_NONE
                } else if (b.isGamepad()) {
                    int ordinal = b.ordinal() - Binding.GAMEPAD_BUTTON_A.ordinal();
                    typeVal = BINDING_GAMEPAD_BASE + ordinal;
                    keycodeVal = ordinal;
                } else if (b == Binding.MOUSE_LEFT_BUTTON) {
                    typeVal = 1; // BINDING_MOUSE_LEFT
                } else if (b == Binding.MOUSE_RIGHT_BUTTON) {
                    typeVal = 2; // BINDING_MOUSE_RIGHT
                } else if (b == Binding.MOUSE_MIDDLE_BUTTON) {
                    typeVal = 3; // BINDING_MOUSE_MIDDLE
                } else if (b == Binding.MOUSE_SCROLL_UP) {
                    typeVal = 6; // BINDING_MOUSE_SCROLL_UP
                } else if (b == Binding.MOUSE_SCROLL_DOWN) {
                    typeVal = 7; // BINDING_MOUSE_SCROLL_DOWN
                } else if (b == Binding.MOUSE_MOVE_LEFT) {
                    typeVal = 8;
                } else if (b == Binding.MOUSE_MOVE_RIGHT) {
                    typeVal = 9;
                } else if (b == Binding.MOUSE_MOVE_UP) {
                    typeVal = 10;
                } else if (b == Binding.MOUSE_MOVE_DOWN) {
                    typeVal = 11;
                } else if (b.isModifier()) {
                    Binding kb = b.toKeyboardBinding();
                    if (kb != null) {
                        typeVal = BINDING_KEYBOARD_FIRST + kb.keycode.id;
                        keycodeVal = kb.keycode.id;
                    } else {
                        typeVal = 0;
                    }
                } else if (b.isKeyboard()) {
                    // Keyboard key: encode as BINDING_KEYBOARD_FIRST + X11 keycode
                    typeVal = BINDING_KEYBOARD_FIRST + b.keycode.id;
                    keycodeVal = b.keycode.id;
                } else {
                    typeVal = 0;
                }
                ne.bindingTypes[j * 2] = typeVal;
                ne.bindingTypes[j * 2 + 1] = keycodeVal;
            }
            arr[i] = ne;
        }
        return arr;
    }

    /**
     * Convert a List<Binding> to an int[] encoded as [type0, keycode0, type1, keycode1, ...].
     */
    private static int[] bindingListToEncoded(List<Binding> list) {
        if (list == null) return new int[0];
        // Count non-NONE entries
        int count = 0;
        for (Binding b : list) {
            if (b != null && b != Binding.NONE) count++;
        }
        int[] result = new int[count * 2];
        int idx = 0;
        for (Binding b : list) {
            if (b == null || b == Binding.NONE) continue;
            int typeVal;
            int keycodeVal = 0;
            if (b == Binding.MOUSE_LEFT_BUTTON) {
                typeVal = 1;
            } else if (b == Binding.MOUSE_RIGHT_BUTTON) {
                typeVal = 2;
            } else if (b == Binding.MOUSE_MIDDLE_BUTTON) {
                typeVal = 3;
            } else if (b == Binding.MOUSE_SCROLL_UP) {
                typeVal = 6;
            } else if (b == Binding.MOUSE_SCROLL_DOWN) {
                typeVal = 7;
            } else if (b.isMouseMove()) {
                if (b == Binding.MOUSE_MOVE_LEFT) typeVal = 8;
                else if (b == Binding.MOUSE_MOVE_RIGHT) typeVal = 9;
                else if (b == Binding.MOUSE_MOVE_UP) typeVal = 10;
                else typeVal = 11;
            } else if (b.isKeyboard()) {
                typeVal = BINDING_KEYBOARD_FIRST + b.keycode.id;
                keycodeVal = b.keycode.id;
            } else if (b.isGamepad()) {
                int ordinal = b.ordinal() - Binding.GAMEPAD_BUTTON_A.ordinal();
                typeVal = BINDING_GAMEPAD_BASE + ordinal;
                keycodeVal = ordinal;
            } else if (b.isModifier()) {
                Binding kb = b.toKeyboardBinding();
                if (kb != null) {
                    typeVal = BINDING_KEYBOARD_FIRST + kb.keycode.id;
                    keycodeVal = kb.keycode.id;
                } else {
                    typeVal = 0;
                }
            } else {
                typeVal = 0;
            }
            result[idx++] = typeVal;
            result[idx++] = keycodeVal;
        }
        return result;
    }

    public void onFingerDown(int ptrId, float x, float y) {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeOnFingerDown(ptrId, x, y, t, null, null);
        executeResult(r);
    }

    public void onFingerDown(int ptrId, float x, float y, float[] outPositions, byte[] outActive) {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeOnFingerDown(ptrId, x, y, t, outPositions, outActive);
        executeResult(r);
    }

    public void onFingerMove(int ptrId, float x, float y) {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeOnFingerMove(ptrId, x, y, t, null, null);
        executeResult(r);
    }

    public void onFingerMove(int ptrId, float x, float y, float[] outPositions, byte[] outActive) {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeOnFingerMove(ptrId, x, y, t, outPositions, outActive);
        executeResult(r);
    }

    public void onFingerUp(int ptrId, float x, float y) {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeOnFingerUp(ptrId, x, y, t, null, null);
        executeResult(r);
    }

    public void onFingerUp(int ptrId, float x, float y, float[] outPositions, byte[] outActive) {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeOnFingerUp(ptrId, x, y, t, outPositions, outActive);
        executeResult(r);
    }

    public void tick() {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeTick(t, null, null);
        executeResult(r);
    }

    public void tick(float[] outPositions, byte[] outActive) {
        if (!loaded || !running) return;
        long t = SystemClock.uptimeMillis();
        TouchActionResult r = nativeTick(t, outPositions, outActive);
        executeResult(r);
    }

    public void reset() {
        if (!loaded) return;
        nativeReset();
    }

    public boolean isPassthroughActive() {
        if (!loaded) return false;
        return nativeIsPassthroughActive();
    }

    public void start() { running = true; }
    public void stop() { running = false; }

    private void executeResult(TouchActionResult r) {
        if (r == null || r.count <= 0) return;
        if (xServer == null) return;

        for (int i = 0; i < r.count && i < r.types.length; i++) {
            int type = r.types[i];
            int a0 = r.intArgs[i * 3];
            int a1 = r.intArgs[i * 3 + 1];
            int a2 = r.intArgs[i * 3 + 2];

            switch (type) {
                case TouchActionResult.ACT_POINTER_MOVE:
                    xServer.injectPointerMove(a0, a1);
                    break;
                case TouchActionResult.ACT_POINTER_MOVE_DELTA:
                    xServer.injectPointerMoveDelta(a0, a1);
                    break;
                case TouchActionResult.ACT_POINTER_BUTTON_PRESS:
                    if (a0 >= 0 && a0 < Pointer.Button.values().length)
                        xServer.injectPointerButtonPress(Pointer.Button.values()[a0]);
                    break;
                case TouchActionResult.ACT_POINTER_BUTTON_RELEASE:
                    if (a0 >= 0 && a0 < Pointer.Button.values().length)
                        xServer.injectPointerButtonRelease(Pointer.Button.values()[a0]);
                    break;
                case TouchActionResult.ACT_KEY_PRESS:
                    for (XKeycode kc : XKeycode.values()) {
                        if (kc.id == a0) { xServer.injectKeyPress(kc); break; }
                    }
                    break;
                case TouchActionResult.ACT_KEY_RELEASE:
                    for (XKeycode kc : XKeycode.values()) {
                        if (kc.id == a0) { xServer.injectKeyRelease(kc); break; }
                    }
                    break;
                case TouchActionResult.ACT_MOUSE_EVENT: {
                    // Forward directly to WinHandler
                    int flags = a0 != 0 ? a0 : MouseEventFlags.MOVE;
                    if (xServer.getWinHandler() != null) {
                        xServer.getWinHandler().mouseEvent(flags, a1, a2, 0);
                    }
                    break;
                }
                case TouchActionResult.ACT_SCROLL:
                    // Native: -1 = scroll up, +1 = scroll down
                    if (a0 < 0) {
                        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
                    } else if (a0 > 0) {
                        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
                    }
                    break;
                case TouchActionResult.ACT_HAPTIC:
                    // Haptic feedback: forward to inputControlsView if available
                    if (inputControlsView != null && inputControlsView.getTouchpadView() != null) {
                        com.winlator.cmod.core.HapticUtils.perform(
                            inputControlsView.getTouchpadView().getContext(), a0 != 0 ? a0 : 1);
                    }
                    break;
                case TouchActionResult.ACT_SET_CURSOR_SPEED:
                    // Cursor speed adjustment
                    break;
                case TouchActionResult.ACT_START_MOUSE_MOVE: {
                    if (inputControlsView != null) {
                        inputControlsView.startMouseMove(a0, a1, a2 != 0);
                    }
                    break;
                }
                case TouchActionResult.ACT_STOP_MOUSE_MOVE: {
                    if (inputControlsView != null) {
                        inputControlsView.stopMouseMove();
                    }
                    break;
                }
                case TouchActionResult.ACT_GAMEPAD_STATE: {
                    if (inputControlsView != null && inputControlsView.getProfile() != null && xServer.getWinHandler() != null) {
                        GamepadState state = inputControlsView.getProfile().getGamepadState();
                        if (a1 != 0) {
                            state.setPressed(a0, true);
                        } else {
                            state.setPressed(a0, false);
                        }
                        xServer.getWinHandler().sendGamepadState();
                    }
                    break;
                }
                case TouchActionResult.ACT_GAMEPAD_AXIS: {
                    if (inputControlsView != null && inputControlsView.getProfile() != null && xServer.getWinHandler() != null) {
                        GamepadState state = inputControlsView.getProfile().getGamepadState();
                        boolean isLeft = a0 != 0;
                        float axisX = a1 / 32767.0f;
                        float axisY = a2 / 32767.0f;
                        if (isLeft) {
                            state.thumbLX = axisX;
                            state.thumbLY = axisY;
                        } else {
                            state.thumbRX = axisX;
                            state.thumbRY = axisY;
                        }
                        xServer.getWinHandler().sendGamepadState();
                    }
                    break;
                }
            }
        }
    }
}
