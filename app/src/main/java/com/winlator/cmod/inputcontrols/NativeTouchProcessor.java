package com.winlator.cmod.inputcontrols;

import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;

import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.widget.InputControlsView;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.winhandler.WinHandler;
import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XKeycode;
import com.winlator.cmod.xserver.XServer;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HashMap;
import java.util.List;

import com.winlator.cmod.core.HapticUtils;
import com.winlator.cmod.inputcontrols.TouchActivationMode;

public class NativeTouchProcessor {
    private static final int BINDING_KEYBOARD_FIRST = 0x100;
    private static final int BINDING_KEYBOARD_LAST = 0x400;
    private static final int BINDING_GAMEPAD_BASE = 0x500;
    private static boolean loaded;
    private static final XKeycode[] KEYCODES_BY_ID = new XKeycode[256];
    private static final HashMap<Integer, XKeycode> keycodeMap = new HashMap<>();
    static {
        for (XKeycode kc : XKeycode.values()) {
            if (kc.id >= 0 && kc.id < 256) KEYCODES_BY_ID[kc.id] = kc;
            keycodeMap.put((int)kc.id, kc);
        }
    }
    private static final Pointer.Button[] POINTER_BUTTONS = Pointer.Button.values();

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
        public int[] intArgs;
    }

    public static class NativeConfig {
        public int touchMode;
        public int inputMode;
        public int longPressTimeoutMs;
        public int doubleTapTimeoutMs;
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
        public float viewOffsetX;
        public float viewOffsetY;
        public int cursorAccelerationThreshold;
        public float cursorAccelerationFactor;

        public int[] tsSingleTap;
        public int[] tsLongPress;
        public int[] tsDoubleTap;
        public int[] tsSingleTapDrag;
        public int[] tsLongPressDrag;
        public int[] tsDoubleTapDrag;
        public int[] tsSingleTap2nd;
        public int[] tsDoubleTap2nd;
        public int[] tsSingleTapDrag2nd;
        public int[] tsDoubleTapDrag2nd;

        public int[] tpSingleTap;
        public int[] tpLongPress;
        public int[] tpDoubleTap;
        public int[] tpSingleTapDrag;
        public int[] tpLongPressDrag;
        public int[] tpDoubleTapDrag;
        public int[] tpSingleTap2nd;
        public int[] tpDoubleTap2nd;
        public int[] tpSingleTapDrag2nd;
        public int[] tpDoubleTapDrag2nd;

        // Per-section sticky bitmasks for gesture bindings.
        // Bit i is set if the i-th non-NONE binding in the section is sticky (pinned).
        public int stSingleTap;
        public int stLongPress;
        public int stDoubleTap;
        public int stSingleTapDrag;
        public int stLongPressDrag;
        public int stDoubleTapDrag;
        public int stSingleTap2nd;
        public int stDoubleTap2nd;
        public int stSingleTapDrag2nd;
        public int stDoubleTapDrag2nd;
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
        public int[] bindingTypes;
        public int[] elementLongPress;
        public int[] elementGesture;
        public boolean toggleSwitch;
        public boolean autoRepeat;
        public int autoRepeatIntervalMs = 100;
        public float opacity;
        public int buttonLongPressHaptic = 1;
        public int buttonGestureHaptic = 1;

        public int rangeOrdinal;
        public int rangeMax;
        public int bindingCount;
        public int orientation;
        public int[] bindingSticky;
    }

    private XServer xServer;
    private InputControlsView inputControlsView;
    private boolean running;
    private static final String TAG = "NativeTouchProc";
    private Handler handler;
    private InputMode inputMode;
    private boolean hapticEnabled;
    private int cursorSpeed;
    private int mouseMoveDx;
    private int mouseMoveDy;
    private int mouseMoveHold;
    private Runnable mouseMoveRunnable;
    private Runnable mouseMoveTask;
    {
        mouseMoveTask = () -> {
            if (xServer == null) return;
            if (inputMode == InputMode.RELATIVE) {
                WinHandler wh = xServer.getWinHandler();
                if (wh != null) wh.mouseEvent(MouseEventFlags.MOVE, mouseMoveDx, mouseMoveDy, 0);
            } else {
                xServer.injectPointerMoveDelta(mouseMoveDx, mouseMoveDy);
            }
            handler.postDelayed(mouseMoveTask, 16);
        };
    }
    private ByteBuffer visualBuffer;
    private int elementCount;

    public NativeTouchProcessor() {
        try {
            System.loadLibrary("touch_processor");
            loaded = true;
        } catch (UnsatisfiedLinkError e) {
            loaded = false;
        } catch (Exception e) {
            loaded = false;
        }
    }

    public boolean isLoaded() { return loaded; }

    public void setXServer(XServer xServer) { this.xServer = xServer; }
    public void setInputControlsView(InputControlsView icv) { this.inputControlsView = icv; }

    // Native methods
    private static native void nativeInit(NativeConfig config);
    private static native void nativeSetElements(NativeElement[] elements);
    private static native void nativeOnFingerDown(int ptrId, float x, float y, long timeMs);
    private static native void nativeOnFingerMove(int ptrId, float x, float y, long timeMs);
    private static native void nativeOnFingerUp(int ptrId, float x, float y, long timeMs);
    private static native void nativeTick(long timeMs);
    private static native void nativeReset();
    private static native ByteBuffer nativeGetVisualBuffer();
    private static native boolean nativeIsPassthroughActive();
    private static native void nativeSetSnappingSize(float size);
    private static native void nativeSetResolutionScale(float scale);
    private static native void nativeSetSimTouchScreen(boolean enabled);
    private static native void nativeUpdateConfig(NativeConfig config);
    private static native void nativeSetXformScale(float scaleX, float scaleY);
    private static native void nativeSetViewOffset(float offsetX, float offsetY);
    private static native boolean nativeGetElementState(int elemIndex, float[] outXY);
    private static native boolean nativeHandleDownByMode(int ptrId, float x, float y, long timeMs);
    private static native boolean nativeHandleUpByMode(int ptrId, float x, float y, long timeMs);
    private static native void nativeHandleMoveByMode(int ptrId, float x, float y, long timeMs);
    private static native int nativeTrackedCount(int ptrId);
    private static native int nativeHoveredIndex(int ptrId);
    private static native void nativeRegisterDispatcher(Object dispatcher);

    public void init(NativeConfig config) {
        if (!loaded) {
            return;
        }
        nativeInit(config);
        nativeRegisterDispatcher(this);
        visualBuffer = nativeGetVisualBuffer();
        if (visualBuffer != null) visualBuffer.order(ByteOrder.LITTLE_ENDIAN);
        hapticEnabled = config.hapticEnabled;
    }

    public void updateConfig(NativeConfig config) {
        if (!loaded) return;
        nativeUpdateConfig(config);
        hapticEnabled = config.hapticEnabled;
    }

    public void setElements(NativeElement[] elements) {
        if (!loaded) return;
        elementCount = (elements != null) ? elements.length : 0;
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

    public void setViewOffset(float offsetX, float offsetY) {
        if (!loaded) return;
        nativeSetViewOffset(offsetX, offsetY);
    }

    public boolean handleDownByMode(int ptrId, float x, float y) {
        return handleDownByMode(ptrId, x, y, SystemClock.uptimeMillis());
    }

    public boolean handleDownByMode(int ptrId, float x, float y, long eventTime) {
        if (!loaded) return false;
        return nativeHandleDownByMode(ptrId, x, y, eventTime);
    }

    public boolean handleUpByMode(int ptrId, float x, float y) {
        return handleUpByMode(ptrId, x, y, SystemClock.uptimeMillis());
    }

    public boolean handleUpByMode(int ptrId, float x, float y, long eventTime) {
        if (!loaded) return false;
        return nativeHandleUpByMode(ptrId, x, y, eventTime);
    }

    public void handleMoveByMode(int ptrId, float x, float y) {
        handleMoveByMode(ptrId, x, y, SystemClock.uptimeMillis());
    }

    public void handleMoveByMode(int ptrId, float x, float y, long eventTime) {
        if (!loaded) return;
        nativeHandleMoveByMode(ptrId, x, y, eventTime);
    }

    public int getTrackedCount(int ptrId) {
        if (!loaded) return 0;
        return nativeTrackedCount(ptrId);
    }

    public int getHoveredIndex(int ptrId) {
        if (!loaded) return -1;
        return nativeHoveredIndex(ptrId);
    }

    public static NativeConfig buildNativeConfig(ControlsProfile profile, int screenW, int screenH) {
        return buildNativeConfig(profile, screenW, screenH, 1.0f);
    }

    public static NativeConfig buildNativeConfig(ControlsProfile profile, int screenW, int screenH, float globalCursorSpeed) {
        NativeConfig c = new NativeConfig();

        if (profile.getMouseMode() == com.winlator.cmod.inputcontrols.MouseMode.TOUCHSCREEN) {
            c.touchMode = 1;
        } else {
            c.touchMode = 0;
        }
        c.inputMode = profile.getInputMode() == com.winlator.cmod.inputcontrols.InputMode.RELATIVE ? 0 : 1;
        c.longPressTimeoutMs = profile.getLongPressTimeout();
        c.doubleTapTimeoutMs = profile.getDoubleTapTimeout();
        c.dragThresholdPx = profile.getDragThreshold();
        c.cursorSpeed = (int)(profile.getCursorSpeed() * globalCursorSpeed * 100);
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

        c.tsSingleTap = profile.getGestureSingleTapAction().encode();
        c.tsLongPress = profile.getGestureLongPressAction().encode();
        c.tsDoubleTap = profile.getGestureDoubleTapAction().encode();
        c.tsSingleTapDrag = profile.getGestureSingleTapDragAction().encode();
        c.tsLongPressDrag = profile.getGestureLongPressDragAction().encode();
        c.tsDoubleTapDrag = profile.getGestureDoubleTapDragAction().encode();
        c.tsSingleTap2nd = profile.getGestureSingleTap2ndFingerAction().encode();
        c.tsDoubleTap2nd = profile.getGestureDoubleTap2ndFingerAction().encode();
        c.tsSingleTapDrag2nd = profile.getGestureSingleTap2ndFingerDragAction().encode();
        c.tsDoubleTapDrag2nd = profile.getGestureDoubleTap2ndFingerDragAction().encode();

        c.tpSingleTap = c.tsSingleTap;
        c.tpLongPress = c.tsLongPress;
        c.tpDoubleTap = c.tsDoubleTap;
        c.tpSingleTapDrag = c.tsSingleTapDrag;
        c.tpLongPressDrag = c.tsLongPressDrag;
        c.tpDoubleTapDrag = c.tsDoubleTapDrag;
        c.tpSingleTap2nd = c.tsSingleTap2nd;
        c.tpDoubleTap2nd = c.tsDoubleTap2nd;
        c.tpSingleTapDrag2nd = c.tsSingleTapDrag2nd;
        c.tpDoubleTapDrag2nd = c.tsDoubleTapDrag2nd;

        // Sticky bitmasks from BindPackage
        c.stSingleTap = profile.getGestureSingleTapAction().encodeStickyBitmask();
        c.stLongPress = profile.getGestureLongPressAction().encodeStickyBitmask();
        c.stDoubleTap = profile.getGestureDoubleTapAction().encodeStickyBitmask();
        c.stSingleTapDrag = profile.getGestureSingleTapDragAction().encodeStickyBitmask();
        c.stLongPressDrag = profile.getGestureLongPressDragAction().encodeStickyBitmask();
        c.stDoubleTapDrag = profile.getGestureDoubleTapDragAction().encodeStickyBitmask();
        c.stSingleTap2nd = profile.getGestureSingleTap2ndFingerAction().encodeStickyBitmask();
        c.stDoubleTap2nd = profile.getGestureDoubleTap2ndFingerAction().encodeStickyBitmask();
        c.stSingleTapDrag2nd = profile.getGestureSingleTap2ndFingerDragAction().encodeStickyBitmask();
        c.stDoubleTapDrag2nd = profile.getGestureDoubleTap2ndFingerDragAction().encodeStickyBitmask();

        return c;
    }

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
            ne.autoRepeat = ce.isAutoRepeat();
            ne.autoRepeatIntervalMs = ce.getAutoRepeatIntervalMs();
            ne.opacity = ce.getEffectiveOpacity();

            ne.buttonLongPressHaptic = p != null ? p.getButtonLongPressHaptic() : 1;
            ne.buttonGestureHaptic = p != null ? p.getButtonGestureHaptic() : 1;

            if (ce.getType() == ControlElement.Type.RANGE_BUTTON) {
                ne.rangeOrdinal = ce.getRange().ordinal();
                ne.rangeMax = ce.getRange().max;
                ne.bindingCount = ce.getBindingCount();
                ne.orientation = ce.getOrientation();
            }

            ne.elementLongPress = bindingListToEncoded(ce.getLongPressBindings());
            ne.elementGesture = bindingListToEncoded(ce.getGestureBindings());

            ne.bindingTypes = new int[ce.getBindingCount() * 2];
            for (int j = 0; j < ce.getBindingCount() && j < 4; j++) {
                Binding b = ce.getBindingAt(j);
                int typeVal;
                int keycodeVal = 0;
                if (b == null || b == Binding.NONE) {
                    typeVal = 0;
                } else if (b.isGamepad()) {
                    int ordinal = b.ordinal() - Binding.GAMEPAD_BUTTON_A.ordinal();
                    typeVal = BINDING_GAMEPAD_BASE + ordinal;
                    keycodeVal = ordinal;
                } else if (b == Binding.MOUSE_LEFT_BUTTON) {
                    typeVal = 1;
                } else if (b == Binding.MOUSE_RIGHT_BUTTON) {
                    typeVal = 2;
                } else if (b == Binding.MOUSE_MIDDLE_BUTTON) {
                    typeVal = 3;
                } else if (b == Binding.MOUSE_SCROLL_UP) {
                    typeVal = 6;
                } else if (b == Binding.MOUSE_SCROLL_DOWN) {
                    typeVal = 7;
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
                    typeVal = BINDING_KEYBOARD_FIRST + b.keycode.id;
                    keycodeVal = b.keycode.id;
                } else {
                    typeVal = 0;
                }
                ne.bindingTypes[j * 2] = typeVal;
                ne.bindingTypes[j * 2 + 1] = keycodeVal;
            }
            ne.bindingSticky = new int[ce.getBindingCount()];
            for (int j = 0; j < ce.getBindingCount() && j < 4; j++) {
                ne.bindingSticky[j] = ce.isBindingSticky(j, 0) ? 1 : 0;
            }
            arr[i] = ne;
        }
        return arr;
    }

    private static int[] bindingListToEncoded(List<Binding> list) {
        if (list == null) return new int[0];
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

    public void onFingerDown(int ptrId, float x, float y, long eventTime) {
        if (!loaded || !running) return;
        nativeOnFingerDown(ptrId, x, y, eventTime);
    }

    public void onFingerMove(int ptrId, float x, float y, long eventTime) {
        if (!loaded || !running) return;
        nativeOnFingerMove(ptrId, x, y, eventTime);
    }

    public void onFingerUp(int ptrId, float x, float y, long eventTime) {
        if (!loaded || !running) return;
        nativeOnFingerUp(ptrId, x, y, eventTime);
    }

    public void tick(long eventTime) {
        if (!loaded || !running) return;
        nativeTick(eventTime);
    }

    public ByteBuffer getVisualBuffer() {
        return visualBuffer;
    }

    public void reset() {
        if (!loaded) return;
        nativeReset();
    }

    public boolean isPassthroughActive() {
        if (!loaded) return false;
        return nativeIsPassthroughActive();
    }

    public void start() {
        running = true;
    }
    public void stop() {
        running = false;
    }

    public void injectPointerMove(int x, int y) {
        xServer.injectPointerMove(x, y);
    }

    public void injectPointerMoveDelta(int dx, int dy) {
        xServer.injectPointerMoveDelta(dx, dy);
    }

    public void injectPointerButtonPress(int button) {
        xServer.injectPointerButtonPress(POINTER_BUTTONS[button]);
    }

    public void injectPointerButtonRelease(int button) {
        xServer.injectPointerButtonRelease(POINTER_BUTTONS[button]);
    }

    public void injectKeyPress(int keycode, boolean isDown) {
        if (keycode >= 0 && keycode < KEYCODES_BY_ID.length) {
            XKeycode kc = KEYCODES_BY_ID[keycode];
            if (kc != null) {
                if (isDown) xServer.injectKeyPress(kc);
                else xServer.injectKeyRelease(kc);
            }
        }
    }

    public void injectKeyRelease(int keycode, boolean isDown) {
        if (keycode >= 0 && keycode < KEYCODES_BY_ID.length) {
            XKeycode kc = KEYCODES_BY_ID[keycode];
            if (kc != null) {
                xServer.injectKeyRelease(kc);
            }
        }
    }

    public void mouseEvent(int flags, int dx, int dy) {
        xServer.getWinHandler().mouseEvent(flags, dx, dy, 0);
    }

    public void scrollEvent(int amount) {
        if (amount != 0) xServer.injectScroll(amount);
    }

    public void hapticEvent(int effect) {
        if (hapticEnabled && inputControlsView != null) {
            HapticUtils.perform(inputControlsView.getContext(), effect);
        }
    }

    public void setCursorSpeed(int speed) {
        cursorSpeed = speed;
    }

    public void startMouseMove(int dx, int dy, int hold) {
        if (mouseMoveRunnable != null) {
            stopMouseMove();
        }
        mouseMoveDx = dx;
        mouseMoveDy = dy;
        mouseMoveHold = hold;
        mouseMoveRunnable = mouseMoveTask;
        handler.post(mouseMoveTask);
    }

    public void stopMouseMove() {
        if (mouseMoveRunnable != null) {
            handler.removeCallbacks(mouseMoveTask);
            mouseMoveRunnable = null;
        }
    }

    public void gamepadState(int btn, boolean isDown) {
        Log.d("Winlator_StickBinding", "NTP.gamepadState btn="+btn+" isDown="+isDown);
        if (xServer.getWinHandler() != null) {
            xServer.getWinHandler().sendGamepadState(btn, isDown);
        }
    }

    public void gamepadAxis(int isLeft, int axisX, int axisY) {
        Log.d("Winlator_StickBinding", "NTP.gamepadAxis isLeft="+isLeft+" axisX="+axisX+" axisY="+axisY);
        if (xServer.getWinHandler() != null) {
            xServer.getWinHandler().sendGamepadAxis(isLeft != 0, axisX, axisY);
        }
    }

    public void dispatchAllActions(int[] types, int[] intArgs, int count) {
        for (int i = 0; i < count; i++) {
            int type = types[i];
            int a0 = intArgs[i * 3];
            int a1 = intArgs[i * 3 + 1];
            int a2 = intArgs[i * 3 + 2];
            if (type == 13 || type == 14) {
                Log.d("Winlator_StickBinding", "NTP.dispatchAllActions type="+type+" a0="+a0+" a1="+a1+" a2="+a2);
            }
            switch (type) {
                case 0: break; // ACT_NONE
                case 1: injectPointerMove(a0, a1); break;
                case 2: injectPointerMoveDelta(a0, a1); break;
                case 3: injectPointerButtonPress(a0); break;
                case 4: injectPointerButtonRelease(a0); break;
                case 5: injectKeyPress(a0, a1 != 0); break;
                case 6: injectKeyRelease(a0, a1 != 0); break;
                case 7: mouseEvent(a0, a1, a2); break;
                case 8: scrollEvent(a0); break;
                case 9: hapticEvent(a0); break;
                case 10: setCursorSpeed(a0); break;
                case 11: startMouseMove(a0, a1, a2); break;
                case 12: stopMouseMove(); break;
                case 13: gamepadState(a0, a1 != 0); break;
                case 14: gamepadAxis(a0, a1, a2); break;
            }
        }
    }
}
