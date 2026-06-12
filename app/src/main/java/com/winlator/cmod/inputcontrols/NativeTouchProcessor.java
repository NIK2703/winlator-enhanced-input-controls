package com.winlator.cmod.inputcontrols;

import android.os.Handler;
import android.os.Looper;
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
        public static final int ACT_GAMEPAD_RELEASE = 14;
        public static final int ACT_GAMEPAD_AXIS = 15;

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

        // Per-section toggle bitmasks for gesture bindings.
        // Bit i is set if the i-th non-NONE binding in the section is a toggle switch.
        public int tgSingleTap;
        public int tgLongPress;
        public int tgDoubleTap;
        public int tgSingleTapDrag;
        public int tgLongPressDrag;
        public int tgDoubleTapDrag;
        public int tgSingleTap2nd;
        public int tgDoubleTap2nd;
        public int tgSingleTapDrag2nd;
        public int tgDoubleTapDrag2nd;

        // Per-section auto-repeat bitmasks for gesture bindings.
        // Bit i is set if the i-th non-NONE binding in the section should auto-repeat.
        public int arSingleTap;
        public int arLongPress;
        public int arDoubleTap;
        public int arSingleTapDrag;
        public int arLongPressDrag;
        public int arDoubleTapDrag;
        public int arSingleTap2nd;
        public int arDoubleTap2nd;
        public int arSingleTapDrag2nd;
        public int arDoubleTapDrag2nd;

        // Per-section auto-repeat interval in ms for gesture bindings.
        public int arSingleTapIntervalMs;
        public int arLongPressIntervalMs;
        public int arDoubleTapIntervalMs;
        public int arSingleTapDragIntervalMs;
        public int arLongPressDragIntervalMs;
        public int arDoubleTapDragIntervalMs;
        public int arSingleTap2ndIntervalMs;
        public int arDoubleTap2ndIntervalMs;
        public int arSingleTapDrag2ndIntervalMs;
        public int arDoubleTapDrag2ndIntervalMs;

        // Render config (shared across all elements)
        public int colorPrimary = 0xFFFFFFFF;
        public int colorSecondary = 0xFF0277BD;
        public float strokeWidthDefault = 0.2f;
        public int fillAlphaInactiveDefault = 50;
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
        public float opacity;
        public int buttonLongPressHaptic = 1;
        public int buttonGestureHaptic = 1;

        public int rangeOrdinal;
        public int rangeMax;
        public int bindingCount;
        public int orientation;
        public int[] bindingSticky;
        public int[] bindingToggle;
        public int[] bindingAutoRepeat;
        public int[] bindingAutoRepeatIntervalMs;
        public int longPressToggleBitmask;
        public int gestureToggleBitmask;
    }

    private XServer xServer;
    private InputControlsView inputControlsView;
    private volatile boolean running;
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

    // Diagnostic: ring buffer of last 200 events for crash analysis
    private static final int EVENT_RING_SIZE = 200;
    private final String[] eventRing = new String[EVENT_RING_SIZE];
    private int eventRingHead = 0;
    private long lastDispatchTime = 0;
    private int dispatchCount = 0;
    private int dispatchErrorCount = 0;
    private long lastFingerDownTime = 0;
    private int fingerDownCount = 0;
    private long lastTickTime = 0;
    private int tickCount = 0;
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
    private int[] dispatchPacked;

    public NativeTouchProcessor() {
        this.handler = new Handler(Looper.getMainLooper());
        try {
            System.loadLibrary("touch_processor");
            loaded = true;
        } catch (UnsatisfiedLinkError e) {
            loaded = false;
        } catch (Exception e) {
            loaded = false;
        }
        Log.w("Winlator_Controls", "NativeTouchProcessor: library loaded=" + loaded);
    }

    // === DIAGNOSTIC METHODS ===

    private void recordEvent(String event) {
        eventRing[eventRingHead] = System.currentTimeMillis() + " " + event;
        eventRingHead = (eventRingHead + 1) % EVENT_RING_SIZE;
    }

    /** Call periodically (from tick) to dump full diagnostic state */
    public void dumpDiagnostics() {
        long now = System.currentTimeMillis();
        Log.w("Winlator_Diag", "=== DIAGNOSTIC DUMP t=" + now + " ===");
        Log.w("Winlator_Diag", "loaded=" + loaded + " running=" + running);
        Log.w("Winlator_Diag", "xServer=" + xServer + " winHandler=" + (xServer != null ? xServer.getWinHandler() : "null"));
        Log.w("Winlator_Diag", "dispatchPacked=" + (dispatchPacked != null ? "len=" + dispatchPacked.length : "NULL"));
        Log.w("Winlator_Diag", "fingerDownCount=" + fingerDownCount + " lastFingerDown=" + (now - lastFingerDownTime) + "ms ago");
        Log.w("Winlator_Diag", "tickCount=" + tickCount + " lastTick=" + (now - lastTickTime) + "ms ago");
        Log.w("Winlator_Diag", "dispatchCount=" + dispatchCount + " dispatchErrors=" + dispatchErrorCount);
        Log.w("Winlator_Diag", "lastDispatch=" + (now - lastDispatchTime) + "ms ago");

        // Dump inputControlsView state
        if (inputControlsView != null) {
            Log.w("Winlator_Diag", "icv: showTouchscreen=" + inputControlsView.isShowTouchscreenControls()
                + " profile=" + inputControlsView.getProfile());
        }

        // Dump last 50 events from ring buffer
        Log.w("Winlator_Diag", "--- last events (ring buffer) ---");
        int start = (eventRingHead - 50 + EVENT_RING_SIZE) % EVENT_RING_SIZE;
        for (int i = 0; i < 50; i++) {
            int idx = (start + i) % EVENT_RING_SIZE;
            if (eventRing[idx] != null) {
                Log.w("Winlator_Diag", "  [" + i + "] " + eventRing[idx]);
            }
        }
        Log.w("Winlator_Diag", "=== END DIAGNOSTIC ===");
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
    private static native ByteBuffer nativeGetElementGeometry();
    private static native int nativeGetElementCount();
    private static native void nativeSetDispatchPacked(int[] buf);
    private static native int nativeSyncVisualState(float[] positions, int[] states, float[] scrollOffsets, int[] visualLayers, int[] visualAlphas);

    public final float[] syncPositions = new float[200 * 2];
    public final int[] syncStates = new int[200];
    public final float[] syncScrollOffsets = new float[200];
    public final int[] syncVisualLayers = new int[200];
    public final int[] syncVisualAlphas = new int[200];

    public int syncVisualState(float[] outPositions, int[] outStates, float[] outScrollOffsets, int[] outVisualLayers, int[] outVisualAlphas) {
        if (!loaded) return 0;
        return nativeSyncVisualState(outPositions, outStates, outScrollOffsets, outVisualLayers, outVisualAlphas);
    }

    public void init(NativeConfig config) {
        if (!loaded) {
            Log.w("Winlator_Controls", "init: native library not loaded, aborting");
            return;
        }
        Log.w("Winlator_Controls", "init: called with config touchMode=" + config.touchMode + " inputMode=" + config.inputMode + " screen=" + config.screenW + "x" + config.screenH);
        nativeInit(config);
        dispatchPacked = new int[256]; // max 64 actions × 4 ints
        nativeSetDispatchPacked(dispatchPacked);
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
        if (!loaded) {
            Log.w("Winlator_Controls", "setElements: native library not loaded");
            return;
        }
        elementCount = (elements != null) ? elements.length : 0;
        Log.w("Winlator_Controls", "setElements: setting " + elementCount + " elements");
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

    private static BindPackage[] collectGestureActions(ControlsProfile profile) {
        BindPackage[] actions = new BindPackage[GestureType.COUNT];
        for (int i = 0; i < GestureType.COUNT; i++) {
            actions[i] = profile.getGestureAction(i);
        }
        return actions;
    }

    private static void setGestureField(NativeConfig c, int[][] ts, int[][] tp, int[] st, int[] tg, int[] ar, int[] arInterval) {
        c.tsSingleTap = ts[0]; c.tpSingleTap = tp[0];
        c.stSingleTap = st[0]; c.tgSingleTap = tg[0];
        c.arSingleTap = ar[0]; c.arSingleTapIntervalMs = arInterval[0];
        c.tsLongPress = ts[1]; c.tpLongPress = tp[1];
        c.stLongPress = st[1]; c.tgLongPress = tg[1];
        c.arLongPress = ar[1]; c.arLongPressIntervalMs = arInterval[1];
        c.tsDoubleTap = ts[2]; c.tpDoubleTap = tp[2];
        c.stDoubleTap = st[2]; c.tgDoubleTap = tg[2];
        c.arDoubleTap = ar[2]; c.arDoubleTapIntervalMs = arInterval[2];
        c.tsSingleTapDrag = ts[3]; c.tpSingleTapDrag = tp[3];
        c.stSingleTapDrag = st[3]; c.tgSingleTapDrag = tg[3];
        c.arSingleTapDrag = ar[3]; c.arSingleTapDragIntervalMs = arInterval[3];
        c.tsLongPressDrag = ts[4]; c.tpLongPressDrag = tp[4];
        c.stLongPressDrag = st[4]; c.tgLongPressDrag = tg[4];
        c.arLongPressDrag = ar[4]; c.arLongPressDragIntervalMs = arInterval[4];
        c.tsDoubleTapDrag = ts[5]; c.tpDoubleTapDrag = tp[5];
        c.stDoubleTapDrag = st[5]; c.tgDoubleTapDrag = tg[5];
        c.arDoubleTapDrag = ar[5]; c.arDoubleTapDragIntervalMs = arInterval[5];
        c.tsSingleTap2nd = ts[6]; c.tpSingleTap2nd = tp[6];
        c.stSingleTap2nd = st[6]; c.tgSingleTap2nd = tg[6];
        c.arSingleTap2nd = ar[6]; c.arSingleTap2ndIntervalMs = arInterval[6];
        c.tsDoubleTap2nd = ts[7]; c.tpDoubleTap2nd = tp[7];
        c.stDoubleTap2nd = st[7]; c.tgDoubleTap2nd = tg[7];
        c.arDoubleTap2nd = ar[7]; c.arDoubleTap2ndIntervalMs = arInterval[7];
        c.tsSingleTapDrag2nd = ts[8]; c.tpSingleTapDrag2nd = tp[8];
        c.stSingleTapDrag2nd = st[8]; c.tgSingleTapDrag2nd = tg[8];
        c.arSingleTapDrag2nd = ar[8]; c.arSingleTapDrag2ndIntervalMs = arInterval[8];
        c.tsDoubleTapDrag2nd = ts[9]; c.tpDoubleTapDrag2nd = tp[9];
        c.stDoubleTapDrag2nd = st[9]; c.tgDoubleTapDrag2nd = tg[9];
        c.arDoubleTapDrag2nd = ar[9]; c.arDoubleTapDrag2ndIntervalMs = arInterval[9];
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

        BindPackage[] gestureActions = collectGestureActions(profile);
        int[][] ts = new int[GestureType.COUNT][];
        int[][] tp = new int[GestureType.COUNT][];
        int[] st = new int[GestureType.COUNT];
        int[] tg = new int[GestureType.COUNT];
        int[] ar = new int[GestureType.COUNT];
        int[] arInterval = new int[GestureType.COUNT];

        for (int i = 0; i < GestureType.COUNT; i++) {
            BindPackage bp = gestureActions[i];
            int[] encoded = bp.encode();
            ts[i] = encoded;
            tp[i] = encoded;
            st[i] = bp.encodeStickyBitmask();
            tg[i] = bp.encodeToggleBitmask();
            ar[i] = bp.encodeAutoRepeatBitmask();
            arInterval[i] = bp.getAutoRepeatIntervalMs();
        }

        setGestureField(c, ts, tp, st, tg, ar, arInterval);

        // Render config
        c.colorPrimary = 0xFFFFFFFF;
        c.colorSecondary = 0xFF0277BD;
        c.strokeWidthDefault = profile.getStrokeWidth();
        c.fillAlphaInactiveDefault = profile.getFillAlphaInactive();

        return c;
    }

    public static NativeElement[] buildNativeElements(List<ControlElement> elements, TouchActivationMode activationMode) {
        return buildNativeElements(elements, activationMode, null);
    }

    public static NativeElement[] buildNativeElements(List<ControlElement> elements, TouchActivationMode activationMode, ControlsProfile profile) {
        if (elements == null) {
            Log.w("Winlator_Controls", "buildNativeElements: elements list is null");
            return null;
        }
        Log.w("Winlator_Controls", "buildNativeElements: processing " + elements.size() + " elements");
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
            ne.cornerRadius = ce.getEffectiveCornerRadius();
            ne.passthroughTouch = ce.isPassthroughTouch();
            ne.activationMode = activationMode != null ? activationMode.ordinal() : 0;
            ne.opacity = ce.getEffectiveOpacity();

            ne.buttonLongPressHaptic = p != null ? p.getButtonLongPressHaptic() : 1;
            ne.buttonGestureHaptic = p != null ? p.getButtonGestureHaptic() : 1;

            if (ce.getType() == ControlElement.Type.RANGE_BUTTON) {
                ne.rangeOrdinal = ce.getRange().ordinal();
                ne.rangeMax = ce.getRange().max;
                ne.bindingCount = ce.getBindingCount();
                ne.orientation = ce.getOrientation();
            }

            ne.elementLongPress = bindingListToEncoded(ce.getBindingsList(ControlElement.BindingSection.LONG_PRESS));
            ne.elementGesture = bindingListToEncoded(ce.getBindingsList(ControlElement.BindingSection.GESTURE));

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
                BindPackage pkg = ce.getSlotPackage(j);
                ne.bindingSticky[j] = pkg != null ? pkg.encodeStickyBitmask() : 0;
            }
            ne.bindingToggle = new int[ce.getBindingCount()];
            for (int j = 0; j < ce.getBindingCount() && j < 4; j++) {
                BindPackage pkg = ce.getSlotPackage(j);
                ne.bindingToggle[j] = pkg != null ? pkg.encodeToggleBitmask() : 0;
            }
            ne.bindingAutoRepeat = new int[ce.getBindingCount()];
            for (int j = 0; j < ce.getBindingCount() && j < 4; j++) {
                BindPackage pkg = ce.getSlotPackage(j);
                ne.bindingAutoRepeat[j] = pkg != null ? pkg.encodeAutoRepeatBitmask() : 0;
            }
            ne.bindingAutoRepeatIntervalMs = new int[ce.getBindingCount()];
            for (int j = 0; j < ce.getBindingCount() && j < 4; j++) {
                BindPackage pkg = ce.getSlotPackage(j);
                ne.bindingAutoRepeatIntervalMs[j] = pkg != null ? pkg.getAutoRepeatIntervalMs() : 100;
            }
            ne.longPressToggleBitmask = ce.computeToggleBitmask(ControlElement.BindingSection.LONG_PRESS);
            ne.gestureToggleBitmask = ce.computeToggleBitmask(ControlElement.BindingSection.GESTURE);
            if (ne.bindingToggle != null) {
                for (int j = 0; j < ne.bindingToggle.length && j < 4; j++) {
                    Log.w("Winlator_Controls", "  elem["+i+"] slot["+j+"] type="+ne.bindingTypes[j*2]+" toggleMask="+ne.bindingToggle[j]+" arMask="+(ne.bindingAutoRepeat != null ? ne.bindingAutoRepeat[j] : -1)+" arInterval="+(ne.bindingAutoRepeatIntervalMs != null ? ne.bindingAutoRepeatIntervalMs[j] : -1));
                }
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
        if (!loaded || !running) {
            Log.w("Winlator_Touch", "onFingerDown DROPPED: loaded="+loaded+" running="+running+" ptrId="+ptrId);
            return;
        }
        nativeOnFingerDown(ptrId, x, y, eventTime);
    }

    public void onFingerMove(int ptrId, float x, float y, long eventTime) {
        if (!loaded || !running) {
            Log.w("Winlator_Touch", "onFingerMove DROPPED: loaded="+loaded+" running="+running+" ptrId="+ptrId);
            return;
        }
        nativeOnFingerMove(ptrId, x, y, eventTime);
    }

    public void onFingerUp(int ptrId, float x, float y, long eventTime) {
        if (!loaded || !running) {
            Log.w("Winlator_Touch", "onFingerUp DROPPED: loaded="+loaded+" running="+running+" ptrId="+ptrId);
            return;
        }
        nativeOnFingerUp(ptrId, x, y, eventTime);
    }

    public void tick(long eventTime) {
        if (!loaded || !running) {
            Log.w("Winlator_Touch", "tick DROPPED: loaded="+loaded+" running="+running);
            return;
        }
        nativeTick(eventTime);
    }

    public ByteBuffer getVisualBuffer() {
        return visualBuffer;
    }

    public ByteBuffer getElementGeometryBuffer() {
        if (!loaded) return null;
        return nativeGetElementGeometry();
    }

    public int getElementCount() {
        if (!loaded) return 0;
        return nativeGetElementCount();
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
        Log.w("Winlator_Touch", "start: running=true");
    }
    public void stop() {
        running = false;
        Log.w("Winlator_Touch", "stop: running=false");
    }

    public void injectPointerMove(int x, int y) {
        if (xServer != null) xServer.injectPointerMove(x, y);
    }

    public void injectPointerMoveDelta(int dx, int dy) {
        if (xServer != null) xServer.injectPointerMoveDelta(dx, dy);
    }

    public void injectPointerButtonPress(int button) {
        if (xServer != null) xServer.injectPointerButtonPress(POINTER_BUTTONS[button]);
    }

    public void injectPointerButtonRelease(int button) {
        if (xServer != null) xServer.injectPointerButtonRelease(POINTER_BUTTONS[button]);
    }

    public void injectKeyPress(int keycode, boolean isDown) {
        if (xServer == null) return;
        if (keycode >= 0 && keycode < KEYCODES_BY_ID.length) {
            XKeycode kc = KEYCODES_BY_ID[keycode];
            if (kc != null) {
                if (isDown) xServer.injectKeyPress(kc);
                else xServer.injectKeyRelease(kc);
            }
        }
    }

    public void injectKeyRelease(int keycode, boolean isDown) {
        if (xServer == null) return;
        if (keycode >= 0 && keycode < KEYCODES_BY_ID.length) {
            XKeycode kc = KEYCODES_BY_ID[keycode];
            if (kc != null) {
                xServer.injectKeyRelease(kc);
            }
        }
    }

    public void mouseEvent(int flags, int dx, int dy) {
        if (xServer != null && xServer.getWinHandler() != null) {
            xServer.getWinHandler().mouseEvent(flags, dx, dy, 0);
        }
    }

    public void scrollEvent(int amount) {
        if (xServer != null && amount != 0) xServer.injectScroll(amount);
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
        if (xServer != null && xServer.getWinHandler() != null) {
            xServer.getWinHandler().sendGamepadState(btn, isDown);
        } else {
            Log.w("Winlator_StickBinding", "NTP.gamepadState DROPPED: xServer="+xServer+" winHandler="+(xServer != null ? xServer.getWinHandler() : "null"));
        }
    }

    public void gamepadAxis(int isLeft, int axisX, int axisY) {
        Log.d("Winlator_StickBinding", "NTP.gamepadAxis isLeft="+isLeft+" axisX="+axisX+" axisY="+axisY);
        if (xServer != null && xServer.getWinHandler() != null) {
            xServer.getWinHandler().sendGamepadAxis(isLeft != 0, axisX, axisY);
        } else {
            Log.w("Winlator_StickBinding", "NTP.gamepadAxis DROPPED: xServer="+xServer+" winHandler="+(xServer != null ? xServer.getWinHandler() : "null"));
        }
    }

    public void dispatchAllActions(int count) {
        int[] buf = dispatchPacked;
        if (buf == null) return;
        try {
            for (int i = 0; i < count; i++) {
                int base = i * 4;
                int type = buf[base];
                int a0 = buf[base + 1];
                int a1 = buf[base + 2];
                int a2 = buf[base + 3];
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
                    case 14: break; // ACT_GAMEPAD_RELEASE - handled natively
                    case 15: gamepadAxis(a0, a1, a2); break;
                }
            }
        } catch (Exception e) {
            Log.e("Winlator_Controls", "dispatchAllActions: exception at count="+count, e);
        }
    }
}
